#include "ParametricEQ.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)
#define CHECK_NEAR(a, b, tol, msg) do { \
    double _d = std::fabs((a) - (b)); \
    if (_d > (tol)) { std::fprintf(stderr, "FAIL: %s (%g vs %g, |d|=%g > %g) (%s:%d)\n", \
        (msg), (double)(a), (double)(b), _d, (double)(tol), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

static void test_all_disabled_is_flat() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    for (double f : {50.0, 500.0, 5000.0}) {
        CHECK_NEAR(eq.magnitudeDbAt(f), 0.0, 1e-9, "disabled parametric bank is flat");
    }
}

static void test_bell_boost_at_center() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    ParametricBand b; b.enabled = true; b.type = 0; b.freqHz = 1000.0; b.gainDb = 6.0; b.q = 2.0;
    eq.setBand(0, b);
    CHECK_NEAR(eq.magnitudeDbAt(1000.0), 6.0, 0.2, "bell +6 dB at center");
    CHECK_NEAR(eq.magnitudeDbAt(60.0), 0.0, 0.3, "bell leaves far-low untouched");
    CHECK_NEAR(eq.magnitudeDbAt(16000.0), 0.0, 0.3, "bell leaves far-high untouched");
}

static void test_highpass_cuts_lows() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    ParametricBand b; b.enabled = true; b.type = 3; b.freqHz = 200.0; b.q = 0.707;
    eq.setBand(0, b);
    CHECK(eq.magnitudeDbAt(50.0) < -8.0, "highpass cuts 50 Hz (>8 dB below 200 Hz corner)");
    CHECK_NEAR(eq.magnitudeDbAt(4000.0), 0.0, 0.3, "highpass passes 4 kHz");
}

static void test_bands_stack() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    ParametricBand a; a.enabled = true; a.type = 0; a.freqHz = 1000.0; a.gainDb = 4.0; a.q = 1.5;
    ParametricBand b; b.enabled = true; b.type = 0; b.freqHz = 1000.0; b.gainDb = 4.0; b.q = 1.5;
    eq.setBand(0, a);
    eq.setBand(1, b);
    CHECK_NEAR(eq.magnitudeDbAt(1000.0), 8.0, 0.3, "two +4 dB bells stack to +8 dB");
}

static void test_zero_gain_bell_is_passthrough() {
    ParametricEQ eq;
    eq.prepare(48000.0);
    ParametricBand b; b.enabled = true; b.type = 0; b.freqHz = 1000.0; b.gainDb = 0.0; b.q = 2.0;
    eq.setBand(0, b);
    CHECK_NEAR(eq.magnitudeDbAt(1000.0), 0.0, 1e-9, "enabled 0 dB bell is pass-through");
}

int main() {
    test_all_disabled_is_flat();
    test_bell_boost_at_center();
    test_highpass_cuts_lows();
    test_bands_stack();
    test_zero_gain_bell_is_passthrough();

    if (g_failures == 0) {
        std::printf("all parametric tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d parametric check(s) failed\n", g_failures);
    return 1;
}
