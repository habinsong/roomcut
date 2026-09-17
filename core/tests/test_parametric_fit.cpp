// ParametricFit: the bands that flatten a measured response. Judged on what the
// engine's own biquads do with the bands it returns.
#include "ParametricFit.hpp"
#include "presets/PresetValidator.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

namespace {

constexpr double kFs = 48000.0;

// A measurement whose flattening is known: the negative of these bands.
std::vector<ResponsePoint> responseOf(const std::vector<ParametricBand>& bands, double lo = 20.0, double hi = 20000.0,
                                      double offsetDb = 0.0) {
    std::vector<ResponsePoint> out;
    for (double f = lo; f <= hi; f *= std::pow(2.0, 1.0 / 48.0)) {
        double db = offsetDb;
        for (const auto& band : bands) db -= parametric_fit::bandDb(band, f, kFs);
        out.push_back({f, db});
    }
    return out;
}

// What the fitted bands do at f, against what flattening needs there.
double worstResidual(const ParametricFitResult& fit, const std::vector<ResponsePoint>& measured, double lo, double hi) {
    double mean = 0.0;
    int count = 0;
    for (const auto& p : measured) if (p.freqHz >= lo && p.freqHz <= hi) { mean += p.db; ++count; }
    mean /= count;
    double worst = 0.0;
    for (const auto& p : measured) {
        if (p.freqHz < lo || p.freqHz > hi) continue;
        double applied = 0.0;
        for (std::size_t b = 0; b < fit.bandsUsed; ++b) applied += parametric_fit::bandDb(fit.bands[b], p.freqHz, kFs);
        worst = std::max(worst, std::fabs(-(p.db - mean) - applied));
    }
    return worst;
}

} // namespace

static void test_known_bands_are_recovered() {
    const std::vector<ParametricBand> truth = {
        {true, 0, 120.0, 4.0, 1.2}, {true, 0, 2500.0, -5.0, 2.0}, {true, 2, 9000.0, 3.0, 0.707}};
    const auto measured = responseOf(truth);
    const auto started = std::chrono::steady_clock::now();
    const auto fit = fitParametricCorrection(measured);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("  three known bands: %zu bands, rms %.2f -> %.3f dB, worst %.3f dB on the grid, %.0f ms\n",
                fit.bandsUsed, fit.rmsBeforeDb, fit.rmsAfterDb, fit.worstAfterDb, ms);
    for (std::size_t b = 0; b < fit.bandsUsed; ++b)
        std::printf("    band %zu: type %d %.0f Hz %+.2f dB Q %.2f\n", b, fit.bands[b].type, fit.bands[b].freqHz,
                    fit.bands[b].gainDb, fit.bands[b].q);
    CHECK(fit.rmsBeforeDb > 1.0, "the test curve is far from flat to begin with");
    CHECK(fit.rmsAfterDb < 0.3 && fit.rmsAfterDb < 0.2 * fit.rmsBeforeDb,
          "the fitted bands take at least 80 % of the error away, to within 0.3 dB RMS");
    // The target was smoothed over 1/6 octave, so the raw curve is checked with room for that.
    CHECK(worstResidual(fit, measured, 20.0, 16000.0) < 1.0, "and the raw measurement to within 1 dB everywhere");
}

static void test_a_flat_measurement_needs_nothing() {
    const auto fit = fitParametricCorrection(responseOf({}, 20.0, 20000.0, -7.5));
    CHECK(fit.bandsUsed == 0 && fit.rmsBeforeDb < 1e-9 && fit.rmsAfterDb < 1e-9,
          "a flat curve at any level asks for no band");
}

static void test_boosts_stay_inside_the_limit() {
    // A 20 dB notch: filling it would need +20 dB.
    const auto measured = responseOf({{true, 0, 4000.0, 20.0, 3.0}});
    ParametricFitSettings settings;
    settings.maxBoostDb = 6.0;
    const auto fit = fitParametricCorrection(measured, settings);
    double most = -100.0;
    for (double f = 20.0; f <= 16000.0; f *= std::pow(2.0, 1.0 / 48.0)) {
        double applied = 0.0;
        for (std::size_t b = 0; b < fit.bandsUsed; ++b) applied += parametric_fit::bandDb(fit.bands[b], f, kFs);
        most = std::max(most, applied);
    }
    std::printf("  a 20 dB notch with boost limited to 6 dB: %zu bands, most boost applied %.2f dB\n", fit.bandsUsed, most);
    CHECK(most <= 6.5, "no frequency is boosted past the limit (with 0.5 dB for band overlap)");
}

static void test_bad_input_is_survived() {
    std::vector<ResponsePoint> messy = responseOf({{true, 0, 1000.0, -6.0, 1.0}}, 100.0, 10000.0);
    messy.push_back({std::numeric_limits<double>::quiet_NaN(), 3.0});
    messy.push_back({500.0, std::numeric_limits<double>::infinity()});
    messy.push_back({-20.0, 1.0});
    messy.push_back({0.0, 1.0});
    messy.push_back(messy[10]);                        // duplicate frequency
    std::reverse(messy.begin(), messy.end());          // unsorted
    const auto fit = fitParametricCorrection(messy);
    CHECK(fit.bandsUsed > 0 && std::isfinite(fit.rmsAfterDb) && fit.rmsAfterDb < 0.3,
          "non-finite, non-positive, duplicate and unsorted points are dropped, not fatal");
    CHECK(fit.gridPoints > 0, "the grid is built from what remains");
    CHECK(fitParametricCorrection({}).bandsUsed == 0 && fitParametricCorrection({{1000.0, 3.0}}).bandsUsed == 0,
          "fewer than two points gives nothing to fit");
}

static void test_only_the_measured_range_is_used() {
    const auto measured = responseOf({{true, 0, 1000.0, -6.0, 1.0}}, 200.0, 5000.0);
    const auto fit = fitParametricCorrection(measured);
    const std::size_t expected = static_cast<std::size_t>(std::floor(std::log2(5000.0 / 200.0) * 24.0)) + 1;
    CHECK(fit.gridPoints <= expected + 1 && fit.gridPoints + 2 >= expected, "the grid spans only 200 Hz - 5 kHz");
}

static void test_the_result_is_repeatable_and_valid() {
    const auto measured = responseOf({{true, 0, 80.0, 5.0, 0.9}, {true, 0, 3100.0, -4.0, 3.0},
                                      {true, 0, 6500.0, 3.0, 4.0}, {true, 1, 60.0, -3.0, 0.707}});
    const auto a = fitParametricCorrection(measured), b = fitParametricCorrection(measured);
    bool same = a.bandsUsed == b.bandsUsed && a.rmsAfterDb == b.rmsAfterDb;
    for (std::size_t i = 0; i < a.bandsUsed; ++i) same = same && a.bands[i] == b.bands[i];
    CHECK(same, "the same measurement gives the same bands, bit for bit");
    ChainParams params;
    for (std::size_t i = 0; i < a.bandsUsed; ++i) params.parametric[i] = a.bands[i];
    const auto validation = PresetValidator::validate(params);
    CHECK(validation.ok, "every band is inside the preset validator's bounds");
    for (std::size_t i = 0; i < a.bandsUsed; ++i)
        CHECK(a.bands[i].enabled && a.bands[i].type >= 0 && a.bands[i].type <= 2, "bands are enabled bells or shelves");
    std::printf("  four bands incl. a shelf: %zu bands, rms %.2f -> %.3f dB\n", a.bandsUsed, a.rmsBeforeDb, a.rmsAfterDb);
}

int main() {
    test_known_bands_are_recovered();
    test_a_flat_measurement_needs_nothing();
    test_boosts_stay_inside_the_limit();
    test_bad_input_is_survived();
    test_only_the_measured_range_is_used();
    test_the_result_is_repeatable_and_valid();
    if (g_failures == 0) std::printf("test_parametric_fit: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
