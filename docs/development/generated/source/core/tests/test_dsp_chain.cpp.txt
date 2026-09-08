/*
 * test_dsp_chain.cpp — the MVP chain integration (docs/04 "Flat mode: output ≈
 * input", "Preset switch / bypass produces no sample discontinuity").
 *
 * Notes:
 *   - The limiter adds look-ahead delay, so we compare LEVELS (RMS), not
 *     sample-aligned values, for the flat/bypass level checks.
 *   - Inputs are kept below the -1 dB ceiling so a flat chain does no limiting
 *     and is genuinely transparent in level.
 */
#include "DSPChain.hpp"

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

static double rms(const std::vector<float>& v, std::size_t fromFrame, std::size_t channels) {
    double sum = 0.0; std::size_t n = 0;
    for (std::size_t i = fromFrame * channels; i < v.size(); ++i) { sum += (double)v[i] * v[i]; ++n; }
    return std::sqrt(sum / (double)n);
}

static void test_flat_level_transparent() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(ChainParams::flat());
    chain.setBypass(false);
    chain.reset();

    // -12 dB sine, safely under the -1 dB ceiling.
    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> in, out;
    const int N = 48000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        in.push_back(s); in.push_back(s);
    }
    out = in;
    chain.processInterleaved(out.data(), N);

    // Compare RMS after settle window — flat chain must not change level.
    double inRms = rms(in, 8000, 2);
    double outRms = rms(out, 8000, 2);
    double gainDb = 20.0 * std::log10(outRms / inRms);
    CHECK_NEAR(gainDb, 0.0, 0.2, "flat preset is level-transparent");
    CHECK_NEAR(chain.limiterGainReductionDb(), 0.0, 1e-6, "flat preset: no limiting on safe signal");
}

static void test_bypass_passthrough() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    // Non-flat params, but bypassed → dry path dominates (limiter still on).
    ChainParams p = ChainParams::flat();
    p.preampDb = 6.0;
    p.eqGainsDb[5] = 12.0;
    chain.setParams(p);
    chain.setBypass(true);
    chain.reset(); // settle to fully-dry immediately

    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> in, out;
    const int N = 24000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        in.push_back(s); in.push_back(s);
    }
    out = in;
    chain.processInterleaved(out.data(), N);

    // Bypassed: despite +6dB preamp & +12dB EQ in params, level ≈ input.
    double inRms = rms(in, 4000, 2);
    double outRms = rms(out, 4000, 2);
    double gainDb = 20.0 * std::log10(outRms / inRms);
    CHECK_NEAR(gainDb, 0.0, 0.2, "bypass ignores preamp/EQ (level ≈ input)");
}

// OFF on a hot master (peak above the old -1 dB ceiling) must be transparent:
// the clip-only limiter (0 dBFS) leaves a sub-0 dBFS signal untouched, so the
// engine path with EQ off matches the device direct (bar the harmless look-ahead
// delay). The old -1 dB ceiling dulled every loud track — the "dirtier" report.
static void test_bypass_transparent_on_hot_signal() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(ChainParams::flat());
    chain.setBypass(true);
    chain.reset(); // settle fully dry

    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -0.3 / 20.0); // -0.3 dBFS, peak 0.966
    std::vector<float> in, out;
    const int N = 24000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        in.push_back(s); in.push_back(s);
    }
    out = in;
    chain.processInterleaved(out.data(), N);

    double gainDb = 20.0 * std::log10(rms(out, 4000, 2) / rms(in, 4000, 2));
    CHECK_NEAR(gainDb, 0.0, 0.05, "bypass is level-transparent on a hot signal");
    CHECK_NEAR(chain.limiterGainReductionDb(), 0.0, 1e-6, "bypass: no limiting below 0 dBFS");
}

// Active + flat EQ on a hot but sub-0 dBFS signal: the clip-only limiter (0 dBFS)
// must leave it untouched (the old -1 dB ceiling dulled every loud master).
static void test_active_flat_transparent_on_hot_signal() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(ChainParams::flat());
    chain.setBypass(false);
    chain.reset();

    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -0.3 / 20.0); // -0.3 dBFS
    std::vector<float> in, out;
    const int N = 48000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        in.push_back(s); in.push_back(s);
    }
    out = in;
    chain.processInterleaved(out.data(), N);

    double gainDb = 20.0 * std::log10(rms(out, 8000, 2) / rms(in, 8000, 2));
    CHECK_NEAR(gainDb, 0.0, 0.05, "active flat is transparent on a hot sub-0 dBFS signal");
    CHECK_NEAR(chain.limiterGainReductionDb(), 0.0, 1e-6, "clip-only limiter: no GR below 0 dBFS");
}

static void test_active_eq_changes_level() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    ChainParams p = ChainParams::flat();
    p.eqGainsDb[5] = 6.0; // +6 dB at 1 kHz
    chain.setParams(p);
    chain.setBypass(false);
    chain.reset();

    const double w = 2.0 * M_PI * 1000.0 / kFs;
    const double amp = std::pow(10.0, -18.0 / 20.0); // low enough to stay unlimited even after +6
    std::vector<float> in, out;
    const int N = 48000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        in.push_back(s); in.push_back(s);
    }
    out = in;
    chain.processInterleaved(out.data(), N);
    double gainDb = 20.0 * std::log10(rms(out, 8000, 2) / rms(in, 8000, 2));
    CHECK_NEAR(gainDb, 6.0, 0.7, "active +6dB EQ at 1kHz lifts level ~6dB");
}

static void test_no_discontinuity_on_bypass_toggle() {
    DSPChain chain;
    chain.prepare(kFs, 2, 15.0);
    // Use a large *preamp* (flat, all-frequency) difference between wet and dry
    // so a hard switch would jump hugely, and a LOW-frequency tone so the
    // signal's own per-sample slope is tiny — isolating any click from the
    // crossfade itself.
    ChainParams p = ChainParams::flat();
    p.preampDb = 12.0; // wet ≈ 4× dry
    chain.setParams(p);
    chain.setBypass(false);
    chain.reset();

    const double w = 2.0 * M_PI * 100.0 / kFs;   // low freq → small natural slope
    const double amp = std::pow(10.0, -16.0 / 20.0); // wet stays under ceiling
    float prev = 0.0f;
    double maxJump = 0.0;
    bool toggled = false;
    for (int n = 0; n < 48000; ++n) {
        float s = (float)(amp * std::sin(w * n));
        float frame[2] = {s, s};
        if (n == 12000) { chain.setBypass(true); toggled = true; }
        chain.processInterleaved(frame, 1);
        if (n > 100) {
            double jump = std::fabs((double)frame[0] - prev);
            maxJump = std::max(maxJump, jump);
        }
        prev = frame[0];
    }
    CHECK(toggled, "bypass toggled mid-stream");
    // Natural slope of the wet 100Hz tone is ~0.008/sample; a hard switch would
    // jump ~0.4. A correct crossfade keeps every step well under 0.02.
    CHECK(maxJump < 0.02, "bypass toggle introduces no large sample discontinuity");
}

static void test_no_nans() {
    DSPChain chain;
    chain.prepare(kFs, 2);
    ChainParams p = ChainParams::flat();
    for (std::size_t b = 0; b < GraphicEQ::kNumBands; ++b) p.eqGainsDb[b] = (b % 2 ? -12.0 : 12.0);
    p.preampDb = 6.0;
    p.spatialWidth = 75.0;
    p.centerFocus = 40.0;
    p.crossfeed = 25.0;
    p.roomReduce = 35.0;
    chain.setParams(p);
    chain.reset();
    unsigned int lcg = 3u;
    bool clean = true;
    for (int n = 0; n < 500000; ++n) {
        lcg = lcg * 1103515245u + 12345u;
        float r = ((float)(lcg >> 9) / (float)(1u << 23)) * 2.0f - 1.0f;
        float frame[2] = {r, r * 0.8f};
        chain.processInterleaved(frame, 1);
        if (std::isnan(frame[0]) || std::isinf(frame[0])) { clean = false; break; }
    }
    CHECK(clean, "no NaN/Inf over 5e5 random samples through full chain");
}

// Run a stereo sine of `freq` Hz at -12 dBFS through the chain, return output
// RMS over the settled tail.
static double chainToneRms(const ChainParams& p, double freq) {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(p);
    chain.setBypass(false);
    chain.reset();
    const double w = 2.0 * M_PI * freq / kFs;
    const double amp = std::pow(10.0, -12.0 / 20.0);
    std::vector<float> buf;
    const int N = 48000;
    for (int n = 0; n < N; ++n) {
        float s = (float)(amp * std::sin(w * n));
        buf.push_back(s); buf.push_back(s);
    }
    chain.processInterleaved(buf.data(), N);
    return rms(buf, 12000, 2);
}

static void test_dialogue_highpass_attenuates_lows() {
    // Dialogue's 90 Hz HPF must cut sub-bass (40 Hz, ~1 octave below) while
    // leaving the speech band (1 kHz) essentially untouched.
    ChainParams hp = ChainParams::flat();
    hp.highpassHz = 90.0;
    ChainParams off = ChainParams::flat(); // highpassHz 0 = off

    double low_off = chainToneRms(off, 40.0);
    double low_hp  = chainToneRms(hp, 40.0);
    double mid_hp  = chainToneRms(hp, 1000.0);
    double mid_off = chainToneRms(off, 1000.0);

    double lowCutDb = 20.0 * std::log10(low_hp / low_off);
    double midDb    = 20.0 * std::log10(mid_hp / mid_off);
    CHECK(lowCutDb < -8.0, "90 Hz HPF attenuates 40 Hz by >8 dB");
    CHECK_NEAR(midDb, 0.0, 0.3, "90 Hz HPF leaves 1 kHz speech band intact");
}

static void test_parametric_band_in_chain() {
    // A parametric notch at 1 kHz (via ChainParams) must cut a 1 kHz tone while
    // leaving 250 Hz alone — proves the parametric stage is wired into the chain.
    ChainParams flat = ChainParams::flat();
    ChainParams notch = ChainParams::flat();
    notch.parametric[0].enabled = true;
    notch.parametric[0].type = 5;        // Notch
    notch.parametric[0].freqHz = 1000.0;
    notch.parametric[0].q = 4.0;

    double on1k  = chainToneRms(notch, 1000.0);
    double off1k = chainToneRms(flat, 1000.0);
    double on250 = chainToneRms(notch, 250.0);
    double off250 = chainToneRms(flat, 250.0);
    CHECK(20.0 * std::log10(on1k / off1k) < -10.0, "parametric notch cuts 1 kHz in the chain");
    CHECK_NEAR(20.0 * std::log10(on250 / off250), 0.0, 0.4, "parametric notch leaves 250 Hz intact");

    // A +6 dB bell at 2 kHz lifts a 2 kHz tone.
    ChainParams bell = ChainParams::flat();
    bell.parametric[0].enabled = true;
    bell.parametric[0].type = 0;         // Bell
    bell.parametric[0].freqHz = 2000.0;
    bell.parametric[0].gainDb = 6.0;
    bell.parametric[0].q = 2.0;
    double bell2k = chainToneRms(bell, 2000.0);
    double off2k = chainToneRms(flat, 2000.0);
    CHECK_NEAR(20.0 * std::log10(bell2k / off2k), 6.0, 0.6, "parametric +6 dB bell lifts 2 kHz in the chain");
}

int main() {
    test_flat_level_transparent();
    test_bypass_passthrough();
    test_bypass_transparent_on_hot_signal();
    test_active_flat_transparent_on_hot_signal();
    test_active_eq_changes_level();
    test_no_discontinuity_on_bypass_toggle();
    test_no_nans();
    test_dialogue_highpass_attenuates_lows();
    test_parametric_band_in_chain();

    if (g_failures == 0) { printf("all dsp-chain tests passed\n"); return 0; }
    fprintf(stderr, "%d dsp-chain check(s) failed\n", g_failures);
    return 1;
}
