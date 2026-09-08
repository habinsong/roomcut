#include "Compressor.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

// Run a sustained stereo sine at `amp`, return steady-state output RMS (dB) and
// the final gain reduction. 0.5 s lets the envelope settle past attack/release.
struct Out { double outRmsDb; double grDb; };
static Out run(double amount, double amp) {
    Compressor c;
    c.prepare(48000.0, 2);
    c.setParams(amount);
    const int n = 24000;
    double sumSq = 0.0;
    int counted = 0;
    for (int i = 0; i < n; ++i) {
        const float s = (float)(amp * std::sin(2.0 * M_PI * 200.0 * i / 48000.0));
        float frame[2] = {s, s};
        c.processFrame(frame);
        if (i > n / 2) { sumSq += (double)frame[0] * frame[0]; counted++; }
    }
    const double rms = std::sqrt(sumSq / counted);
    return { 20.0 * std::log10(std::max(rms, 1e-12)), c.gainReductionDb() };
}

static void test_bypass_is_exact() {
    Compressor c;
    c.prepare(48000.0, 2);
    c.setParams(0.0); // off
    float frame[2] = {0.7f, -0.4f};
    c.processFrame(frame);
    CHECK(frame[0] == 0.7f && frame[1] == -0.4f, "amount 0 = exact bypass");
    CHECK(c.gainReductionDb() == 0.0, "bypass reports no gain reduction");
}

static void test_loud_signal_is_compressed() {
    // amount 50 → threshold −10 dB, ratio 1.75:1. A −3 dBFS tone is well above
    // threshold, so the detector should pull gain down.
    const double amp = std::pow(10.0, -3.0 / 20.0); // −3 dBFS
    Out o = run(50.0, amp);
    CHECK(o.grDb > 0.5, "loud signal above threshold is compressed (GR > 0.5 dB)");
}

static void test_quiet_signal_passes() {
    // A −30 dBFS tone is far below the −10 dB threshold → no compression.
    const double amp = std::pow(10.0, -30.0 / 20.0);
    Out o = run(50.0, amp);
    CHECK(o.grDb < 0.05, "quiet signal below threshold is not compressed");
}

static void test_ratio_reduces_dynamic_range() {
    // Two tones 12 dB apart, both above threshold. The output spread must be
    // smaller than the input spread (that IS compression).
    const double loud = std::pow(10.0, -3.0 / 20.0);   // −3 dBFS
    const double soft = std::pow(10.0, -15.0 / 20.0);  // −15 dBFS (still > −9? no)
    // −15 is below −9 threshold, so pick −6 vs −18 straddling differently:
    Out hi = run(60.0, std::pow(10.0, -3.0 / 20.0));
    Out lo = run(60.0, std::pow(10.0, -9.0 / 20.0));
    (void)loud; (void)soft;
    const double inSpread = (-3.0) - (-9.0);            // 6 dB in
    const double outSpread = hi.outRmsDb - lo.outRmsDb; // dB out
    CHECK(outSpread < inSpread, "compressor narrows dynamic range (out spread < in spread)");
    CHECK(outSpread > 0.0, "louder input still maps to louder output (monotonic)");
}

int main() {
    test_bypass_is_exact();
    test_loud_signal_is_compressed();
    test_quiet_signal_passes();
    test_ratio_reduces_dynamic_range();

    if (g_failures == 0) {
        std::printf("all compressor tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d compressor check(s) failed\n", g_failures);
    return 1;
}
