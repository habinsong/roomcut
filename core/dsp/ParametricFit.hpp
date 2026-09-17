/*
 * ParametricFit.hpp — finds the parametric EQ bands that flatten a measured
 * response.
 *
 * Two users: a headphone measurement the listener imports ("make this curve
 * flat"), and the headphone bed renderer's own diffuse-field response. Both hand
 * over (frequency, dB) points; the bands come back in the same form the user's
 * parametric EQ stores, and each band is judged by the exact response of the
 * biquad the engine will run (Biquad, RBJ cookbook), not an approximation of it.
 *
 * Method, all of it deterministic:
 *   1. The points go onto a 1/24-octave grid (straight lines in log frequency),
 *      only where the data reaches — no extrapolation.
 *   2. The target is the response turned upside down around its own mean, then
 *      smoothed over 1/6 octave (a narrow notch is where the measurement is least
 *      trustworthy, and filling one with a boost is how EQ gets harsh) and
 *      limited to maxBoostDb / maxCutDb.
 *   3. A bell goes on each peak and dip of the target, the biggest (height times
 *      width) first, up to maxBands. An end of the range that leans away from
 *      zero counts as one. The first Q comes from where the target keeps half
 *      its height.
 *   4. Every band's frequency, gain and Q are then fitted together:
 *      Levenberg-Marquardt on the squared error (Levenberg 1944, Marquardt
 *      1963), numerical Jacobian, a step that leaves the bounds pulled back onto
 *      them.
 *   5. The band whose removal costs least (the others left as they are) is
 *      taken out and the rest fitted again; that stands if it cost less than
 *      0.05 dB of RMS, and is tried again. Fitting every candidate again
 *      instead chose the same bands on the curves measured, at 3-4x the time.
 *
 * Why together and not one band at a time: on the AUSpatialMixer diffuse field
 * (1.38 dB RMS to begin with), adding bands one by one and refining each alone
 * stopped at 2-4 bands and 0.85-0.92 dB — every band stayed where the ones
 * before it had pushed it. Fitted together from the extrema: 5 bands and
 * 0.34-0.36 dB at every unit rate (test_spatial_mixer_bed).
 *
 * Not for the render thread: it allocates.
 */
#ifndef ROOMCUT_PARAMETRIC_FIT_HPP
#define ROOMCUT_PARAMETRIC_FIT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "Biquad.hpp"
#include "ParametricEQ.hpp"

namespace roomcut {

struct ResponsePoint {
    double freqHz = 0.0;
    double db = 0.0;
};

struct ParametricFitSettings {
    double minHz = 20.0;
    double maxHz = 16000.0;
    std::size_t maxBands = ParametricEQ::kNumBands;
    double maxBoostDb = 6.0;
    double maxCutDb = 12.0;
    double sampleRate = 48000.0;   // the rate the bands are evaluated at
    double minQ = 0.3;
    double maxQ = 6.0;
};

struct ParametricFitResult {
    std::array<ParametricBand, ParametricEQ::kNumBands> bands{};
    std::size_t bandsUsed = 0;
    std::size_t gridPoints = 0;
    double rmsBeforeDb = 0.0;   // of the (smoothed, limited) target, i.e. with no EQ
    double rmsAfterDb = 0.0;    // target minus what the bands do
    double worstAfterDb = 0.0;
};

namespace parametric_fit {

constexpr double kStepsPerOctave = 24.0;
constexpr double kSmoothingOctaves = 1.0 / 6.0;
constexpr double kMinImprovementDb = 0.05;
constexpr int kMaxIterations = 50;
constexpr double kConvergence = 1e-3;   // an accepted step that takes less than this share off the squared error ends the fit

inline double bandDb(const ParametricBand& band, double freqHz, double fs) {
    Biquad filter;
    filter.set(static_cast<BiquadType>(band.type), fs, band.freqHz, band.gainDb, band.q);
    return filter.magnitudeDbAt(freqHz, fs);
}

struct Problem {
    std::vector<double> freqs, target;
    double fs = 48000.0;

    std::vector<double> curve(const ParametricBand& band) const {
        std::vector<double> out(freqs.size());
        Biquad filter;
        filter.set(static_cast<BiquadType>(band.type), fs, band.freqHz, band.gainDb, band.q);
        for (std::size_t i = 0; i < freqs.size(); ++i) out[i] = filter.magnitudeDbAt(freqs[i], fs);
        return out;
    }

    std::vector<double> sum(const std::vector<ParametricBand>& bands) const {
        std::vector<double> total(freqs.size(), 0.0);
        for (const auto& band : bands) {
            const auto c = curve(band);
            for (std::size_t i = 0; i < total.size(); ++i) total[i] += c[i];
        }
        return total;
    }

    double squaredError(const std::vector<ParametricBand>& bands) const {
        const auto applied = sum(bands);
        double total = 0.0;
        for (std::size_t i = 0; i < freqs.size(); ++i) {
            const double e = target[i] - applied[i];
            total += e * e;
        }
        return total;
    }

    double rms(const std::vector<ParametricBand>& bands) const {
        return std::sqrt(squaredError(bands) / static_cast<double>(freqs.size()));
    }
};

inline std::vector<ParametricBand> bellsOnExtrema(const Problem& p, const ParametricFitSettings& s) {
    struct Extremum { std::size_t at; double size, octaves; };
    std::vector<Extremum> found;
    const auto& t = p.target;
    const std::size_t last = t.size() - 1;
    for (std::size_t i = 0; i <= last; ++i) {
        const double v = t[i];
        if (v == 0.0) continue;
        // A plateau counts once, at its first point.
        const bool into = i == 0 || (v > 0.0 ? v > t[i - 1] : v < t[i - 1]);
        const bool out = i == last || (v > 0.0 ? v >= t[i + 1] : v <= t[i + 1]);
        if (!into || !out) continue;
        std::size_t lo = i, hi = i;
        while (lo > 0 && t[lo - 1] * v > 0.5 * v * v) --lo;
        while (hi < last && t[hi + 1] * v > 0.5 * v * v) ++hi;
        const double octaves = std::max(1.0 / kStepsPerOctave, std::log2(p.freqs[hi] / p.freqs[lo]));
        found.push_back({i, std::fabs(v) * octaves, octaves});
    }
    std::stable_sort(found.begin(), found.end(), [](const Extremum& a, const Extremum& b) { return a.size > b.size; });
    std::vector<ParametricBand> bands;
    for (const auto& e : found) {
        if (bands.size() == s.maxBands) break;
        const double width = std::pow(2.0, e.octaves);
        bands.push_back({true, static_cast<int>(BiquadType::Bell), p.freqs[e.at],
                         std::clamp(t[e.at], -s.maxCutDb, s.maxBoostDb),
                         std::clamp(std::sqrt(width) / (width - 1.0), s.minQ, s.maxQ)});
    }
    return bands;
}

// Gaussian elimination with partial pivoting; false when the system is singular.
inline bool solve(std::vector<double> a, std::vector<double> b, std::size_t m, std::vector<double>& x) {
    for (std::size_t c = 0; c < m; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < m; ++r)
            if (std::fabs(a[r * m + c]) > std::fabs(a[pivot * m + c])) pivot = r;
        if (!(std::fabs(a[pivot * m + c]) > 1e-300)) return false;
        if (pivot != c) {
            for (std::size_t k = 0; k < m; ++k) std::swap(a[c * m + k], a[pivot * m + k]);
            std::swap(b[c], b[pivot]);
        }
        for (std::size_t r = c + 1; r < m; ++r) {
            const double f = a[r * m + c] / a[c * m + c];
            for (std::size_t k = c; k < m; ++k) a[r * m + k] -= f * a[c * m + k];
            b[r] -= f * b[c];
        }
    }
    x.assign(m, 0.0);
    for (std::size_t c = m; c-- > 0;) {
        double v = b[c];
        for (std::size_t k = c + 1; k < m; ++k) v -= a[c * m + k] * x[k];
        x[c] = v / a[c * m + c];
    }
    return std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); });
}

// Parameters per band: log frequency, gain in dB, log Q.
inline void fitTogether(const Problem& p, std::vector<ParametricBand>& bands, const ParametricFitSettings& s) {
    if (bands.empty()) return;
    const std::size_t m = 3 * bands.size(), points = p.freqs.size();
    auto withParameters = [&](const std::vector<double>& x) {
        std::vector<ParametricBand> out = bands;
        for (std::size_t b = 0; b < out.size(); ++b) {
            out[b].freqHz = std::clamp(std::exp(x[3 * b]), p.freqs.front(), p.freqs.back());
            out[b].gainDb = std::clamp(x[3 * b + 1], -s.maxCutDb, s.maxBoostDb);
            out[b].q = std::clamp(std::exp(x[3 * b + 2]), s.minQ, s.maxQ);
        }
        return out;
    };
    double error = p.squaredError(bands);
    double lambda = 1e-2;
    std::vector<double> x(m), jacobian(points * m), normal(m * m), gradient(m), step;
    for (int iteration = 0; iteration < kMaxIterations && error > 0.0; ++iteration) {
        for (std::size_t b = 0; b < bands.size(); ++b) {
            x[3 * b] = std::log(bands[b].freqHz);
            x[3 * b + 1] = bands[b].gainDb;
            x[3 * b + 2] = std::log(bands[b].q);
        }
        const auto applied = p.sum(bands);
        for (std::size_t k = 0; k < m; ++k) {
            // Only band k / 3 moves, and just past a bound is still a valid filter.
            const double h = k % 3 == 1 ? 1e-3 : 1e-4;
            ParametricBand up = bands[k / 3], down = bands[k / 3];
            if (k % 3 == 0) { up.freqHz *= std::exp(h); down.freqHz *= std::exp(-h); }
            else if (k % 3 == 1) { up.gainDb += h; down.gainDb -= h; }
            else { up.q *= std::exp(h); down.q *= std::exp(-h); }
            const auto cu = p.curve(up), cd = p.curve(down);
            for (std::size_t i = 0; i < points; ++i) jacobian[i * m + k] = (cu[i] - cd[i]) / (2.0 * h);
        }
        std::fill(normal.begin(), normal.end(), 0.0);
        std::fill(gradient.begin(), gradient.end(), 0.0);
        for (std::size_t i = 0; i < points; ++i) {
            const double residual = p.target[i] - applied[i];
            for (std::size_t a = 0; a < m; ++a) {
                gradient[a] += jacobian[i * m + a] * residual;
                for (std::size_t c = a; c < m; ++c) normal[a * m + c] += jacobian[i * m + a] * jacobian[i * m + c];
            }
        }
        for (std::size_t a = 0; a < m; ++a)
            for (std::size_t c = 0; c < a; ++c) normal[a * m + c] = normal[c * m + a];

        bool accepted = false;
        double drop = 0.0;
        while (!accepted && lambda < 1e12) {
            std::vector<double> damped = normal;
            for (std::size_t a = 0; a < m; ++a) damped[a * m + a] += lambda * std::max(normal[a * m + a], 1e-9);
            if (solve(damped, gradient, m, step)) {
                std::vector<double> next = x;
                for (std::size_t a = 0; a < m; ++a) next[a] += step[a];
                auto candidate = withParameters(next);
                const double e = p.squaredError(candidate);
                if (e < error) {
                    drop = error - e;
                    error = e;
                    bands = std::move(candidate);
                    lambda = std::max(lambda / 3.0, 1e-12);
                    accepted = true;
                    continue;
                }
            }
            lambda *= 4.0;
        }
        if (!accepted || drop < kConvergence * error) break;
    }
}

} // namespace parametric_fit

inline ParametricFitResult fitParametricCorrection(const std::vector<ResponsePoint>& input,
                                                   const ParametricFitSettings& settings = {}) {
    using namespace parametric_fit;
    ParametricFitResult result;
    std::vector<ResponsePoint> points;
    for (const auto& point : input)
        if (std::isfinite(point.freqHz) && std::isfinite(point.db) && point.freqHz > 0.0) points.push_back(point);
    std::sort(points.begin(), points.end(), [](const ResponsePoint& a, const ResponsePoint& b) { return a.freqHz < b.freqHz; });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const ResponsePoint& a, const ResponsePoint& b) { return a.freqHz == b.freqHz; }),
                 points.end());
    const std::size_t maxBands = std::min(settings.maxBands, result.bands.size());
    const double nyquistGuard = 0.45 * settings.sampleRate;
    if (points.size() < 2 || maxBands == 0) return result;

    Problem p;
    p.fs = settings.sampleRate;
    const double lo = std::max(settings.minHz, points.front().freqHz);
    const double hi = std::min({settings.maxHz, points.back().freqHz, nyquistGuard});
    if (!(hi > lo * 1.1)) return result;
    const std::size_t steps = static_cast<std::size_t>(std::floor(std::log2(hi / lo) * kStepsPerOctave));
    std::vector<double> raw;
    std::size_t j = 0;
    for (std::size_t i = 0; i <= steps; ++i) {
        const double f = lo * std::pow(2.0, static_cast<double>(i) / kStepsPerOctave);
        while (j + 1 < points.size() && points[j + 1].freqHz < f) ++j;
        const auto& a = points[j];
        const auto& b = points[std::min(j + 1, points.size() - 1)];
        const double t = b.freqHz > a.freqHz ? std::log(f / a.freqHz) / std::log(b.freqHz / a.freqHz) : 0.0;
        p.freqs.push_back(f);
        raw.push_back(a.db + std::clamp(t, 0.0, 1.0) * (b.db - a.db));
    }
    double mean = 0.0;
    for (double v : raw) mean += v;
    mean /= static_cast<double>(raw.size());
    const long half = std::lround(kSmoothingOctaves * kStepsPerOctave * 0.5);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::size_t from = i >= static_cast<std::size_t>(half) ? i - half : 0;
        const std::size_t to = std::min(raw.size() - 1, i + half);
        double sum = 0.0;
        for (std::size_t k = from; k <= to; ++k) sum += raw[k];
        const double smoothed = sum / static_cast<double>(to - from + 1);
        p.target.push_back(std::clamp(-(smoothed - mean), -settings.maxCutDb, settings.maxBoostDb));
    }
    result.gridPoints = p.freqs.size();
    result.rmsBeforeDb = p.rms({});

    ParametricFitSettings limits = settings;
    limits.maxBands = maxBands;
    std::vector<ParametricBand> bands = bellsOnExtrema(p, limits);
    fitTogether(p, bands, limits);
    while (!bands.empty()) {
        const double now = p.rms(bands);
        double cheapest = std::numeric_limits<double>::infinity();
        std::vector<ParametricBand> without;
        for (std::size_t b = 0; b < bands.size(); ++b) {
            std::vector<ParametricBand> trial = bands;
            trial.erase(trial.begin() + static_cast<std::ptrdiff_t>(b));
            const double cost = p.rms(trial) - now;
            if (cost < cheapest) { cheapest = cost; without = std::move(trial); }
        }
        fitTogether(p, without, limits);
        if (p.rms(without) - now >= kMinImprovementDb) break;
        bands = std::move(without);
    }

    const auto applied = p.sum(bands);
    result.rmsAfterDb = p.rms(bands);
    for (std::size_t i = 0; i < p.freqs.size(); ++i)
        result.worstAfterDb = std::max(result.worstAfterDb, std::fabs(p.target[i] - applied[i]));
    result.bandsUsed = bands.size();
    for (std::size_t b = 0; b < bands.size(); ++b) result.bands[b] = bands[b];
    return result;
}

} // namespace roomcut

#endif // ROOMCUT_PARAMETRIC_FIT_HPP
