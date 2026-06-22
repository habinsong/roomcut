/*
 * test_limiter.cpp — the limiter's core guarantee: output NEVER exceeds the
 * ceiling, on any signal (docs/04 "Limiter never exceeds ceiling on a known
 * over-level signal"). Plus: low-level audio passes ~untouched, no NaNs.
 */
#include "Limiter.hpp"

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
static const double kCeilingDb = -1.0;

static double ceilingLin() { return std::pow(10.0, kCeilingDb / 20.0); }

static void test_never_exceeds_on_loud_sine() {
    Limiter lim;
    lim.prepare(kFs, 2.0, kCeilingDb, 100.0, 2);
    // +6 dB sine (amplitude ~2.0) — way over the ceiling.
    const double w = 2.0 * M_PI * 440.0 / kFs;
    const double ceil = ceilingLin();
    double maxOut = 0.0;
    for (int n = 0; n < 96000; ++n) {
        float s = (float)(2.0 * std::sin(w * n));
        float frame[2] = {s, s};
        lim.processFrame(frame);
        maxOut = std::max(maxOut, (double)std::fabs(frame[0]));
        maxOut = std::max(maxOut, (double)std::fabs(frame[1]));
    }
    // Allow a hair of float slop above the linear ceiling.
    CHECK(maxOut <= ceil + 1e-4, "loud sine output stays at/under ceiling");
    CHECK(lim.gainReductionDb() > 3.0, "limiter is actively reducing on +6dB sine");
}

static void test_never_exceeds_on_impulses() {
    Limiter lim;
    lim.prepare(kFs, 2.0, kCeilingDb, 100.0, 2);
    const double ceil = ceilingLin();
    double maxOut = 0.0;
    for (int n = 0; n < 48000; ++n) {
        // Periodic full-scale-plus impulses with quiet between.
        float s = (n % 500 == 0) ? 1.5f : 0.02f;
        float frame[2] = {s, s};
        lim.processFrame(frame);
        maxOut = std::max(maxOut, (double)std::fabs(frame[0]));
    }
    CHECK(maxOut <= ceil + 1e-4, "impulse peaks tamed under ceiling (look-ahead works)");
}

static void test_low_level_mostly_untouched() {
    Limiter lim;
    lim.prepare(kFs, 2.0, kCeilingDb, 100.0, 2);
    // A -12 dB sine (amp ~0.25) is well under the -1 dB ceiling → no reduction.
    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> in, out;
    const int warm = 4096;
    double sumIn = 0, sumOut = 0;
    for (int n = 0; n < warm + 16384; ++n) {
        float s = (float)(amp * std::sin(w * n));
        float frame[2] = {s, s};
        lim.processFrame(frame);
        if (n >= warm) {
            // compare delayed input vs output level (RMS) — should match.
            sumIn  += (double)s * s;
            sumOut += (double)frame[0] * frame[0];
        }
    }
    double gainDb = 10.0 * std::log10(sumOut / sumIn);
    CHECK_NEAR(gainDb, 0.0, 0.2, "low-level signal passes ~unchanged in level");
    CHECK_NEAR(lim.gainReductionDb(), 0.0, 1e-6, "no gain reduction on safe signal");
}

static void test_no_nans_random_overdrive() {
    Limiter lim;
    lim.prepare(kFs, 2.0, kCeilingDb, 80.0, 2);
    const double ceil = ceilingLin();
    unsigned int lcg = 7u;
    double maxOut = 0.0;
    bool clean = true;
    for (int n = 0; n < 500000; ++n) {
        lcg = lcg * 1103515245u + 12345u;
        float r = ((float)(lcg >> 9) / (float)(1u << 23)) * 2.0f - 1.0f; // [-1,1)
        float s = r * 3.0f; // heavy overdrive
        float frame[2] = {s, s * 0.9f};
        lim.processFrame(frame);
        if (std::isnan(frame[0]) || std::isinf(frame[0])) { clean = false; break; }
        maxOut = std::max({maxOut, (double)std::fabs(frame[0]), (double)std::fabs(frame[1])});
    }
    CHECK(clean, "no NaN/Inf under heavy random overdrive");
    CHECK(maxOut <= ceil + 1e-4, "random overdrive never breaches ceiling");
}

static void test_ceiling_adjustable_not_disablable() {
    Limiter lim;
    lim.prepare(kFs, 2.0, 0.0, 100.0, 2); // ceiling at 0 dBFS
    lim.setCeilingDb(-6.0);
    const double ceil = std::pow(10.0, -6.0 / 20.0);
    const double w = 2.0 * M_PI * 440.0 / kFs;
    double maxOut = 0.0;
    for (int n = 0; n < 48000; ++n) {
        float s = (float)std::sin(w * n); // 0 dB sine, over the -6 dB ceiling
        float frame[2] = {s, s};
        lim.processFrame(frame);
        maxOut = std::max(maxOut, (double)std::fabs(frame[0]));
    }
    CHECK(maxOut <= ceil + 1e-4, "adjusted -6dB ceiling is respected");
}

int main() {
    test_never_exceeds_on_loud_sine();
    test_never_exceeds_on_impulses();
    test_low_level_mostly_untouched();
    test_no_nans_random_overdrive();
    test_ceiling_adjustable_not_disablable();

    if (g_failures == 0) { printf("all limiter tests passed\n"); return 0; }
    fprintf(stderr, "%d limiter check(s) failed\n", g_failures);
    return 1;
}
