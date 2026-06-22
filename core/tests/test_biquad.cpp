/*
 * test_biquad.cpp — Biquad frequency-response and runtime checks (docs/04).
 *
 * Two independent angles so a bug in either the coefficients OR the Direct
 * Form II transposed implementation is caught:
 *   A. analytic: magnitudeDbAt() evaluates H(e^jw) from the stored coefficients.
 *   B. empirical: drive processSample() with a sine and measure the actual
 *      steady-state output gain. A and B must agree.
 */
#include "Biquad.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

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

// Measure steady-state gain (linear) of a configured biquad at `freqHz` by
// driving a unit sine and comparing RMS out/in after the filter settles.
static double measureGain(Biquad& bq, double freqHz) {
    bq.reset();
    const int warmup = 8192;     // let transients die
    const int measure = 16384;
    const double w = 2.0 * M_PI * freqHz / kFs;
    double sumIn = 0.0, sumOut = 0.0;
    for (int n = 0; n < warmup + measure; ++n) {
        float x = (float)std::sin(w * n);
        float y = bq.processSample(x, 0);
        if (n >= warmup) {
            sumIn  += (double)x * x;
            sumOut += (double)y * y;
        }
    }
    return std::sqrt(sumOut / sumIn);
}

static void test_flat_passthrough() {
    Biquad bq; // identity by default
    for (double f : {100.0, 1000.0, 10000.0}) {
        CHECK_NEAR(bq.magnitudeDbAt(f, kFs), 0.0, 1e-9, "identity is 0 dB (analytic)");
    }
    CHECK_NEAR(measureGain(bq, 1000.0), 1.0, 1e-4, "identity is unity (empirical)");
}

static void test_bell_center_gain() {
    // A bell at 1 kHz, +6 dB, Q=2 must give ~+6 dB at center, both ways.
    Biquad bq;
    bq.set(BiquadType::Bell, kFs, 1000.0, 6.0, 2.0);
    CHECK_NEAR(bq.magnitudeDbAt(1000.0, kFs), 6.0, 0.05, "bell +6dB at center (analytic)");
    CHECK_NEAR(20.0 * std::log10(measureGain(bq, 1000.0)), 6.0, 0.1, "bell +6dB at center (empirical)");

    // Far from center it should be ~0 dB.
    CHECK_NEAR(bq.magnitudeDbAt(60.0, kFs), 0.0, 0.5, "bell ~0dB well below center");
    CHECK_NEAR(bq.magnitudeDbAt(16000.0, kFs), 0.0, 0.5, "bell ~0dB well above center");

    // A cut must be symmetric-ish in dB sign.
    Biquad cut;
    cut.set(BiquadType::Bell, kFs, 1000.0, -6.0, 2.0);
    CHECK_NEAR(cut.magnitudeDbAt(1000.0, kFs), -6.0, 0.05, "bell -6dB at center");
}

static void test_shelves() {
    // Low shelf +6 dB: DC ≈ +6, high freq ≈ 0.
    Biquad ls;
    ls.set(BiquadType::LowShelf, kFs, 200.0, 6.0, 0.707);
    CHECK_NEAR(ls.magnitudeDbAt(20.0, kFs), 6.0, 0.4, "low shelf ~+6dB at low freq");
    CHECK_NEAR(ls.magnitudeDbAt(18000.0, kFs), 0.0, 0.4, "low shelf ~0dB at high freq");

    // High shelf +6 dB: high freq ≈ +6, DC ≈ 0.
    Biquad hs;
    hs.set(BiquadType::HighShelf, kFs, 5000.0, 6.0, 0.707);
    CHECK_NEAR(hs.magnitudeDbAt(20.0, kFs), 0.0, 0.4, "high shelf ~0dB at low freq");
    CHECK_NEAR(hs.magnitudeDbAt(20000.0, kFs), 6.0, 0.4, "high shelf ~+6dB at high freq");
}

static void test_pass_filters() {
    // Low-pass at 1 kHz, Q=0.707 (Butterworth): ~ -3 dB at cutoff, attenuating above.
    Biquad lp;
    lp.set(BiquadType::LowPass, kFs, 1000.0, 0.0, 0.707);
    CHECK_NEAR(lp.magnitudeDbAt(1000.0, kFs), -3.0, 0.5, "low-pass -3dB at cutoff");
    CHECK(lp.magnitudeDbAt(8000.0, kFs) < -15.0, "low-pass strongly attenuates above cutoff");
    CHECK_NEAR(lp.magnitudeDbAt(50.0, kFs), 0.0, 0.3, "low-pass passes well below cutoff");

    // High-pass mirror.
    Biquad hp;
    hp.set(BiquadType::HighPass, kFs, 1000.0, 0.0, 0.707);
    CHECK_NEAR(hp.magnitudeDbAt(1000.0, kFs), -3.0, 0.5, "high-pass -3dB at cutoff");
    CHECK(hp.magnitudeDbAt(125.0, kFs) < -15.0, "high-pass strongly attenuates below cutoff");

    // Empirical cross-check on the low-pass cutoff.
    CHECK_NEAR(20.0 * std::log10(measureGain(lp, 1000.0)), -3.0, 0.6, "low-pass -3dB at cutoff (empirical)");
}

static void test_notch() {
    Biquad nq;
    nq.set(BiquadType::Notch, kFs, 1000.0, 0.0, 4.0);
    CHECK(nq.magnitudeDbAt(1000.0, kFs) < -30.0, "notch deep null at center");
    CHECK_NEAR(nq.magnitudeDbAt(250.0, kFs), 0.0, 0.5, "notch passes away from center");
    CHECK_NEAR(nq.magnitudeDbAt(4000.0, kFs), 0.0, 0.5, "notch passes away from center (high)");
}

static void test_no_nans_random() {
    // 1e6 random samples through an aggressive bell must never produce NaN/Inf.
    Biquad bq;
    bq.set(BiquadType::Bell, kFs, 3000.0, 18.0, 12.0);
    unsigned int lcg = 1u;
    bool clean = true;
    for (int n = 0; n < 1000000; ++n) {
        lcg = lcg * 1103515245u + 12345u;
        float x = ((float)(lcg >> 9) / (float)(1u << 23)) * 2.0f - 1.0f; // [-1,1)
        float y = bq.processSample(x, 0);
        if (std::isnan(y) || std::isinf(y)) { clean = false; break; }
    }
    CHECK(clean, "no NaN/Inf over 1e6 random samples");
}

static void test_degenerate_inputs() {
    Biquad bq;
    bq.set(BiquadType::Bell, kFs, -5.0, 6.0, 2.0);   // bad freq → identity
    CHECK_NEAR(bq.magnitudeDbAt(1000.0, kFs), 0.0, 1e-9, "bad freq falls back to identity");
    bq.set(BiquadType::Bell, kFs, 1000.0, 6.0, 0.0); // bad Q → identity
    CHECK_NEAR(bq.magnitudeDbAt(1000.0, kFs), 0.0, 1e-9, "bad Q falls back to identity");
    // Above-Nyquist request must stay finite (clamped), not blow up.
    bq.set(BiquadType::LowPass, kFs, 30000.0, 0.0, 0.707);
    CHECK(std::isfinite(bq.magnitudeDbAt(1000.0, kFs)), "above-Nyquist clamp stays finite");
}

int main() {
    test_flat_passthrough();
    test_bell_center_gain();
    test_shelves();
    test_pass_filters();
    test_notch();
    test_no_nans_random();
    test_degenerate_inputs();

    if (g_failures == 0) { printf("all biquad tests passed\n"); return 0; }
    fprintf(stderr, "%d biquad check(s) failed\n", g_failures);
    return 1;
}
