/*
 * test_graphic_eq.cpp — 10-band graphic EQ checks (docs/04).
 *
 *   - flat (all bands 0 dB) is an exact pass-through (identity biquads).
 *   - a single boosted band lifts its center frequency by ~the set gain.
 *   - a cut lowers it.
 *   - per-channel state is independent (stereo).
 *   - no NaN/Inf over a long random run with all bands engaged.
 */
#include "GraphicEQ.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)
#define CHECK_NEAR(a, b, tol, msg) do { \
    double _d = std::fabs((a) - (b)); \
    if (_d > (tol)) { fprintf(stderr, "FAIL: %s (%g vs %g, |d|=%g > %g) (%s:%d)\n", \
        (msg), (double)(a), (double)(b), _d, (double)(tol), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;
static const double kFs = 48000.0;

static double measureGainDb(GraphicEQ& eq, double freqHz, std::size_t ch) {
    eq.reset();
    const int warmup = 8192, measure = 16384;
    const double w = 2.0 * M_PI * freqHz / kFs;
    double sumIn = 0.0, sumOut = 0.0;
    for (int n = 0; n < warmup + measure; ++n) {
        float x = (float)std::sin(w * n);
        float y = eq.processSample(x, ch);
        if (n >= warmup) { sumIn += (double)x * x; sumOut += (double)y * y; }
    }
    return 20.0 * std::log10(std::sqrt(sumOut / sumIn));
}

static void test_flat_passthrough() {
    GraphicEQ eq;
    eq.prepare(kFs);
    // Analytic: every band identity → 0 dB everywhere.
    for (double f : {31.0, 250.0, 1000.0, 16000.0}) {
        CHECK_NEAR(eq.magnitudeDbAt(f), 0.0, 1e-9, "flat EQ analytic 0 dB");
    }
    // Empirical: exact unity sample-for-sample.
    eq.reset();
    bool exact = true;
    unsigned int lcg = 5u;
    for (int n = 0; n < 4096; ++n) {
        lcg = lcg * 1103515245u + 12345u;
        float x = ((float)(lcg >> 9) / (float)(1u << 23)) * 2.0f - 1.0f;
        float y = eq.processSample(x, 0);
        if (std::fabs(y - x) > 1e-6f) { exact = false; break; }
    }
    CHECK(exact, "flat EQ is sample-exact pass-through");
}

static void test_single_band_boost() {
    GraphicEQ eq;
    eq.prepare(kFs);
    // Boost the 1 kHz band (index 5) by +6 dB.
    eq.setBandGain(5, 6.0);
    CHECK_NEAR(eq.bandGain(5), 6.0, 1e-12, "band gain stored");

    // At 1 kHz the analytic cascade gain should be ~+6 dB (neighbors ~0 there).
    CHECK_NEAR(eq.magnitudeDbAt(1000.0), 6.0, 0.6, "1k boost ~+6dB at center (analytic)");
    CHECK_NEAR(measureGainDb(eq, 1000.0, 0), 6.0, 0.7, "1k boost ~+6dB at center (empirical)");

    // A distant band (31 Hz) should be barely affected.
    CHECK_NEAR(eq.magnitudeDbAt(31.0), 0.0, 0.5, "1k boost leaves 31Hz ~flat");
}

static void test_single_band_cut() {
    GraphicEQ eq;
    eq.prepare(kFs);
    eq.setBandGain(3, -8.0); // 250 Hz cut
    CHECK_NEAR(eq.magnitudeDbAt(250.0), -8.0, 0.7, "250Hz cut ~-8dB at center");
}

static void test_stereo_independent() {
    GraphicEQ eq;
    eq.prepare(kFs);
    eq.setBandGain(5, 6.0);
    // Drive channel 0 only; channel 1 state must stay zero and produce silence
    // for a zero input even after channel 0 has been excited.
    for (int n = 0; n < 1000; ++n) (void)eq.processSample(0.5f, 0);
    float y1 = eq.processSample(0.0f, 1);
    CHECK_NEAR(y1, 0.0f, 1e-6, "channel 1 unaffected by channel 0 activity");
}

static void test_no_nans() {
    GraphicEQ eq;
    eq.prepare(kFs);
    for (std::size_t b = 0; b < GraphicEQ::kNumBands; ++b)
        eq.setBandGain(b, (b % 2 == 0) ? 12.0 : -12.0); // aggressive zig-zag
    unsigned int lcg = 99u;
    bool clean = true;
    for (int n = 0; n < 1000000; ++n) {
        lcg = lcg * 1103515245u + 12345u;
        float x = ((float)(lcg >> 9) / (float)(1u << 23)) * 2.0f - 1.0f;
        float y = eq.processSample(x, n & 1);
        if (std::isnan(y) || std::isinf(y)) { clean = false; break; }
    }
    CHECK(clean, "no NaN/Inf over 1e6 samples with all bands engaged");
}

static void test_interleaved() {
    GraphicEQ eq;
    eq.prepare(kFs);
    // Flat → interleaved buffer is unchanged.
    float buf[8] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f, 0.7f, -0.8f};
    float ref[8];
    for (int i = 0; i < 8; ++i) ref[i] = buf[i];
    eq.processInterleaved(buf, 4, 2);
    bool same = true;
    for (int i = 0; i < 8; ++i) if (std::fabs(buf[i] - ref[i]) > 1e-6f) same = false;
    CHECK(same, "flat interleaved process is identity");
}

int main() {
    test_flat_passthrough();
    test_single_band_boost();
    test_single_band_cut();
    test_stereo_independent();
    test_no_nans();
    test_interleaved();

    if (g_failures == 0) { printf("all graphic-eq tests passed\n"); return 0; }
    fprintf(stderr, "%d graphic-eq check(s) failed\n", g_failures);
    return 1;
}
