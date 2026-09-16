/*
 * BinauralMeasure.hpp — reading a direction off a pair of ear signals.
 *
 * Renderer-independent on purpose: it takes two ear signals and nothing else,
 * so the same maths judges the current renderer in ctest and any other
 * renderer in a measurement tool. What it reads is what a listener has:
 *
 *   - ITD: the lag of the largest interaural correlation inside the head's
 *     time window, refined below a sample on a windowed-sinc interpolation of
 *     the correlation (a three-point parabola is biased by up to a tenth of a
 *     sample on broadband material — measured 2.7 us at 44.1 kHz).
 *   - ILD: broadband and per third octave.
 *   - Lateral angle: the ITD inverted through Woodworth & Schlosberg for the
 *     reference head. ITD and ILD only fix the cone of confusion, so this is
 *     an angle from the median plane (0..90), never a front/back decision.
 */
#ifndef ROOMCUT_BINAURAL_MEASURE_HPP
#define ROOMCUT_BINAURAL_MEASURE_HPP

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace roomcut {
namespace binaural {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSpeedOfSound = 343.0;
// The reference sphere for turning an ITD into an angle, and the search window
// for the ITD itself (the largest head the renderer models). Both equal the
// VirtualSpeaker constants; test_binaural_direction checks that they still do.
constexpr double kReferenceHeadRadiusM = 8.75 * 0.01;   // written as VirtualSpeaker computes it: 0.0875 is a different double
constexpr double kSearchWindowSeconds = 0.0011;

struct Ears { std::vector<double> left, right; };

inline double wrap180(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180.0) degrees -= 360.0;
    if (degrees < -180.0) degrees += 360.0;
    return degrees;
}

// Angle from the median plane, 0..90: the part of an azimuth ITD and ILD can
// see. 30 and 150 degrees are the same lateral angle.
inline double lateralOf(double azimuthDegrees) {
    const double a = std::fabs(wrap180(azimuthDegrees));
    return a <= 90.0 ? a : 180.0 - a;
}

// Woodworth & Schlosberg for the frontal quadrant, lateral angle in degrees.
inline double woodworthItd(double lateralDegrees) {
    const double theta = lateralDegrees * kPi / 180.0;
    return kReferenceHeadRadiusM / kSpeedOfSound * (theta + std::sin(theta));
}

// The inverse, by bisection (the formula rises monotonically on 0..90).
inline double lateralFromItd(double itdSeconds) {
    const double itd = std::fabs(itdSeconds);
    if (itd >= woodworthItd(90.0)) return 90.0;
    double lo = 0.0, hi = 90.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        (woodworthItd(mid) < itd ? lo : hi) = mid;
    }
    return 0.5 * (lo + hi);
}

inline void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

inline std::vector<double> powerSpectrum(const std::vector<double>& x, std::size_t n) {
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < x.size() && i < n; ++i) a[i] = x[i];
    fft(a);
    std::vector<double> p(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k) p[k] = std::norm(a[k]);
    return p;
}

struct Direction {
    double itdSeconds = 0.0;   // > 0: the right ear hears it later (source on the left)
    double peak = 0.0;         // largest normalised interaural correlation in the window
    double trough = 0.0;       // most negative one
    double lateralDegrees = 0.0;
    double side = 0.0;         // -1 left, +1 right, 0 centre
    double ildDb = 0.0;        // broadband, left over right
    double arrivalLeftMs = -1.0, arrivalRightMs = -1.0;
    // One source through one path reaches both ears with the same polarity.
    // Opposite polarity dominating the head's time window is not a direction a
    // single source can have. (No lag bound tied to the reference sphere: a
    // measured head runs past it — AUSpatialMixer puts 90 degrees at 723 us.)
    bool singleSource = false;
};

// First sample within 60 dB of the signal's peak.
inline double arrivalMs(const std::vector<double>& x, double fs) {
    double top = 0.0;
    for (double v : x) top = std::max(top, std::fabs(v));
    if (top <= 0.0) return -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::fabs(x[i]) >= top * 1.0e-3) return 1000.0 * static_cast<double>(i) / fs;
    return -1.0;
}

inline Direction measure(const Ears& e, double fs) {
    Direction d;
    const std::size_t n = e.left.size();
    double ll = 0.0, rr = 0.0;
    for (std::size_t i = 0; i < n; ++i) { ll += e.left[i] * e.left[i]; rr += e.right[i] * e.right[i]; }
    d.ildDb = 10.0 * std::log10(std::max(ll, 1e-300) / std::max(rr, 1e-300));
    d.arrivalLeftMs = arrivalMs(e.left, fs);
    d.arrivalRightMs = arrivalMs(e.right, fs);
    const double norm = std::sqrt(ll * rr);
    if (norm <= 0.0) return d;

    // The window scales with the rate; a fixed sample count saturates high.
    const long window = static_cast<long>(std::ceil(kSearchWindowSeconds * fs));
    std::vector<double> r(static_cast<std::size_t>(2 * window + 1));
    long best = 0;
    double peak = -2.0, trough = 2.0;
    for (long lag = -window; lag <= window; ++lag) {
        double sum = 0.0;
        const long from = std::max(0L, -lag), to = std::min(static_cast<long>(n), static_cast<long>(n) - lag);
        for (long i = from; i < to; ++i) sum += e.left[static_cast<std::size_t>(i)] * e.right[static_cast<std::size_t>(i + lag)];
        const double v = sum / norm;
        r[static_cast<std::size_t>(lag + window)] = v;
        if (v > peak) { peak = v; best = lag; }
        trough = std::min(trough, v);
    }
    auto interpolated = [&](double t) {
        constexpr long half = 32;
        double sum = 0.0;
        for (long k = -half; k <= half; ++k) {
            const long index = best + k + window;
            if (index < 0 || index >= static_cast<long>(r.size())) continue;
            const double x = t - static_cast<double>(k);
            if (std::fabs(x) >= static_cast<double>(half)) continue;
            const double sinc = x == 0.0 ? 1.0 : std::sin(kPi * x) / (kPi * x);
            const double u = (x + half) / (2.0 * half);
            sum += r[static_cast<std::size_t>(index)] * sinc
                 * (0.42 - 0.5 * std::cos(2.0 * kPi * u) + 0.08 * std::cos(4.0 * kPi * u));
        }
        return sum;
    };
    double lo = -1.0, hi = 1.0;
    constexpr double golden = 0.6180339887498949;
    double x1 = hi - golden * (hi - lo), x2 = lo + golden * (hi - lo);
    double f1 = interpolated(x1), f2 = interpolated(x2);
    for (int i = 0; i < 60; ++i) {
        if (f1 < f2) { lo = x1; x1 = x2; f1 = f2; x2 = lo + golden * (hi - lo); f2 = interpolated(x2); }
        else         { hi = x2; x2 = x1; f2 = f1; x1 = hi - golden * (hi - lo); f1 = interpolated(x1); }
    }
    const double fraction = 0.5 * (lo + hi);
    d.peak = peak;
    d.trough = trough;
    d.itdSeconds = (static_cast<double>(best) + fraction) / fs;
    d.lateralDegrees = lateralFromItd(d.itdSeconds);
    d.side = d.itdSeconds > 0.0 ? -1.0 : (d.itdSeconds < 0.0 ? 1.0 : 0.0);
    d.singleSource = peak > 0.0 && peak >= -trough;
    return d;
}

struct Band { double centreHz; double left; double right; std::size_t bins; };

// Third-octave energies of both ears, 250 Hz..16 kHz, for bands wholly below
// 0.45 fs. The transform is padded until the narrowest band (250 Hz, 58 Hz
// wide) holds at least four bins; a short response otherwise leaves bands with
// none at all.
inline std::vector<Band> thirdOctaves(const Ears& e, double fs) {
    const double narrowest = 250.0 * (std::pow(2.0, 1.0 / 6.0) - std::pow(2.0, -1.0 / 6.0));
    std::size_t n = 1;
    while (n < e.left.size() || fs / static_cast<double>(n) > narrowest / 4.0) n <<= 1;
    const std::vector<double> pl = powerSpectrum(e.left, n), pr = powerSpectrum(e.right, n);
    static constexpr double kCentres[] = {250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                          2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000};
    std::vector<Band> out;
    for (double fc : kCentres) {
        const double lo = fc * std::pow(2.0, -1.0 / 6.0), hi = fc * std::pow(2.0, 1.0 / 6.0);
        if (hi > 0.45 * fs) break;
        Band band{fc, 0.0, 0.0, 0};
        for (std::size_t k = 1; k < n / 2; ++k) {
            const double f = static_cast<double>(k) * fs / static_cast<double>(n);
            if (f >= lo && f < hi) { band.left += pl[k]; band.right += pr[k]; ++band.bins; }
        }
        out.push_back(band);
    }
    return out;
}

inline double ildDb(const Band& band) {
    return 10.0 * std::log10(std::max(band.left, 1e-300) / std::max(band.right, 1e-300));
}

} // namespace binaural
} // namespace roomcut

#endif // ROOMCUT_BINAURAL_MEASURE_HPP
