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

// Run `seconds` of a 500 Hz tone through a chain and return the output.
static std::vector<float> chainTone(const ChainParams& params, bool bypass, double seconds) {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(params);
    chain.setBypass(bypass);
    const std::size_t frames = static_cast<std::size_t>(kFs * seconds);
    std::vector<float> buf(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const float s = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 500.0 * (double)f / kFs));
        buf[f * 2] = s;
        buf[f * 2 + 1] = s;
    }
    chain.processInterleaved(buf.data(), frames);
    return buf;
}

// A stereo pair with real side content. The mono tone above cannot test any of
// the width features: crossfeed, the ambience surround and the upmix all reshape
// the SIDE and leave a centred signal alone by design, so a mono probe makes
// every one of them look like a no-op.
// Returns the SETTLED half only. Every one of these changes crossfades, and a
// whole-buffer comparison would just be measuring the crossfade.
static std::vector<float> chainStereo(const ChainParams& params, double yawDeg,
                                      bool tracking, double seconds = 0.5) {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(params);
    chain.setHeadPose(yawDeg, tracking);
    const std::size_t frames = static_cast<std::size_t>(kFs * seconds);
    std::vector<float> buf(frames * 2, 0.0f);
    unsigned state = 22695477u;
    for (std::size_t f = 0; f < frames; ++f) {
        state = state * 1103515245u + 12345u;
        const double noise = (double)((state >> 8) & 0xFFFFu) / 32768.0 - 1.0;
        const double mid = 0.25 * std::sin(2.0 * M_PI * 500.0 * (double)f / kFs);
        buf[f * 2] = static_cast<float>(mid + 0.15 * noise);
        buf[f * 2 + 1] = static_cast<float>(mid - 0.15 * noise);
    }
    chain.processInterleaved(buf.data(), frames);
    return std::vector<float>(buf.begin() + (frames / 2) * 2, buf.end());
}

static void test_virtual_room_follows_the_output() {
    // Room off must leave both outputs exactly as they were before the room
    // stage existed.
    for (double mode : {0.0, 1.0}) {
        ChainParams plain = ChainParams::flat();
        plain.spatialMode = mode;
        ChainParams off = plain;
        off.roomType = 0.0;
        CHECK(chainTone(plain, false, 0.5) == chainTone(off, false, 0.5),
              "room type 0 leaves the chain bit-identical on either output");
    }

    // A room changes the sound on both outputs...
    ChainParams speaker = ChainParams::flat();
    speaker.spatialMode = 0.0;
    ChainParams speakerRoom = speaker;
    speakerRoom.roomType = 2.0;
    ChainParams headphone = ChainParams::flat();
    headphone.spatialMode = 1.0;
    ChainParams headphoneRoom = headphone;
    headphoneRoom.roomType = 2.0;
    const std::vector<float> speakerDry = chainTone(speaker, false, 1.0);
    const std::vector<float> speakerWet = chainTone(speakerRoom, false, 1.0);
    const std::vector<float> headphoneWet = chainTone(headphoneRoom, false, 1.0);
    CHECK(!(speakerDry == speakerWet), "a speaker room changes the sound");
    CHECK(!(chainTone(headphone, false, 1.0) == headphoneWet), "a headphone room changes the sound");

    // ...but not the same way: the speaker room adds nothing in the first
    // milliseconds, where the real room's own reflections live.
    const std::size_t onset = static_cast<std::size_t>(kFs * 0.015) * 2;
    double speakerEarly = 0.0, headphoneEarly = 0.0;
    const std::vector<float> headphoneDry = chainTone(headphone, false, 1.0);
    for (std::size_t i = 0; i < onset; ++i) {
        speakerEarly = std::max(speakerEarly, std::fabs((double)speakerWet[i] - speakerDry[i]));
        headphoneEarly = std::max(headphoneEarly, std::fabs((double)headphoneWet[i] - headphoneDry[i]));
    }
    CHECK(speakerEarly < 1e-6, "the speaker room leaves the first 15 ms to the real room");
    CHECK(headphoneEarly > 1e-4, "the headphone room builds its own early reflections");
}

// Same as chainTone, with a head orientation pushed in before rendering.
static std::vector<float> chainToneWithHead(const ChainParams& params, double yawDeg,
                                            bool active, bool bypass = false) {
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(params);
    chain.setBypass(bypass);
    chain.setHeadPose(yawDeg, active);
    const std::size_t frames = static_cast<std::size_t>(kFs * 0.5);
    std::vector<float> buf(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const float s = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 500.0 * (double)f / kFs));
        buf[f * 2] = s;
        buf[f * 2 + 1] = s;
    }
    chain.processInterleaved(buf.data(), frames);
    return buf;
}

static void test_head_tracking_only_runs_when_it_should() {
    ChainParams headphone = ChainParams::flat();
    headphone.spatialMode = 1.0;
    ChainParams speaker = ChainParams::flat();
    speaker.spatialMode = 0.0;

    // No tracker: the chain is exactly what it was before head tracking existed.
    CHECK(chainToneWithHead(headphone, 0.0, false) == chainTone(headphone, false, 0.5),
          "an inactive tracker leaves the chain bit-identical");
    CHECK(chainToneWithHead(headphone, 45.0, false) == chainTone(headphone, false, 0.5),
          "a stale angle with no tracker changes nothing");

    // Tracking on headphones changes the sound, and the angle matters.
    CHECK(!(chainToneWithHead(headphone, 0.0, true) == chainTone(headphone, false, 0.5)),
          "an active tracker engages the head-tracked renderer");
    CHECK(!(chainToneWithHead(headphone, 30.0, true) == chainToneWithHead(headphone, 0.0, true)),
          "turning the head changes what the ears get");

    // Speakers are already in a room: head tracking must not touch them.
    CHECK(chainToneWithHead(speaker, 30.0, true) == chainTone(speaker, false, 0.5),
          "head tracking never applies on speakers");

    // Bypass silences it like everything else.
    const std::vector<float> bypassed = chainToneWithHead(headphone, 40.0, true, true);
    const std::vector<float> dry = chainTone(ChainParams::flat(), true, 0.5);
    double worst = 0.0;
    for (std::size_t i = static_cast<std::size_t>(kFs * 0.2) * 2; i < bypassed.size(); ++i)
        worst = std::max(worst, std::fabs((double)bypassed[i] - dry[i]));
    CHECK(worst < 1e-6, "bypass silences the head-tracked renderer");
}

static void test_virtual_room_respects_bypass() {
    ChainParams params = ChainParams::flat();
    params.spatialMode = 1.0;
    params.roomType = 3.0;
    params.roomAmount = 100.0;
    // Bypass must be a true passthrough: after the bypass ramp the output is
    // the dry tone, with no reverb tail riding on top.
    const std::vector<float> bypassed = chainTone(params, true, 1.0);
    const std::vector<float> dry = chainTone(ChainParams::flat(), true, 1.0);
    double diff = 0.0;
    for (std::size_t i = static_cast<std::size_t>(kFs * 0.5) * 2; i < bypassed.size(); ++i)
        diff = std::max(diff, std::fabs((double)bypassed[i] - (double)dry[i]));
    CHECK(diff < 1e-6, "bypass silences the virtual room");
}

static void test_virtual_room_stays_inside_the_ceiling() {
    // A hot signal plus the largest room must still leave the limiter's
    // brickwall intact — the room can never be the thing that clips.
    ChainParams params = ChainParams::flat();
    params.spatialMode = 1.0;
    params.roomType = 3.0;
    params.roomAmount = 100.0;
    DSPChain chain;
    chain.prepare(kFs, 2);
    chain.setParams(params);
    chain.setBypass(false);
    const std::size_t frames = static_cast<std::size_t>(kFs * 2.0);
    std::vector<float> buf(frames * 2, 0.0f);
    for (std::size_t f = 0; f < frames; ++f) {
        const float s = static_cast<float>(0.95 * std::sin(2.0 * M_PI * 440.0 * (double)f / kFs));
        buf[f * 2] = s;
        buf[f * 2 + 1] = s;
    }
    chain.processInterleaved(buf.data(), frames);
    double peak = 0.0;
    bool finite = true;
    for (float v : buf) { peak = std::max(peak, std::fabs((double)v)); finite = finite && std::isfinite(v); }
    CHECK(finite, "hot signal through the room stays finite");
    CHECK(peak <= 1.0, "the room never pushes the chain past full scale");
}

// The upmix has to run with no head tracker attached — 5.1 has a centre and
// surrounds that need rendering as speakers whether or not the listener's head
// is being followed — but it must stay out of the way on speakers and in bypass.
static void test_upmix_runs_without_a_tracker() {
    ChainParams headphone = ChainParams::flat();
    headphone.spatialMode = 1.0;
    const std::vector<float> plain = chainToneWithHead(headphone, 0.0, false);

    for (double layout : {2.0, 3.0}) {
        ChainParams upmixed = headphone;
        upmixed.surroundType = layout;
        CHECK(chainToneWithHead(upmixed, 0.0, false) != plain,
              "an upmix layout renders even with no tracker attached");

        ChainParams speaker = upmixed;
        speaker.spatialMode = 0.0;
        CHECK(chainToneWithHead(speaker, 0.0, false) == chainTone(speaker, false, 0.5),
              "the upmix stays off on speakers, which are already in a room");

        CHECK(chainToneWithHead(upmixed, 0.0, false, true) == chainTone(upmixed, true, 0.5),
              "bypass silences the upmix like everything else");
    }

    // Off is off: the field exists but changes nothing until it is set.
    ChainParams off = headphone;
    off.surroundType = 0.0;
    off.centerWidth = -6.0;
    off.surroundDepth = 4.0;
    CHECK(chainToneWithHead(off, 0.0, false) == plain,
          "the upmix trims do nothing while the layout is off");
}

// Head tracking, the upmix and the crossfeed all decide where a source appears.
// Left alone they stack: measured on a 0.58-correlated mix, head tracking with
// crossfeed still at 50 pulled the correlation to 0.93 instead of 0.86 and put
// 3.5 dB more energy in the middle. The chain now decides which one renders, so
// a stale preset or a restored state file cannot double them up.
static void test_only_one_virtual_stage_renders() {
    ChainParams headphone = ChainParams::flat();
    headphone.spatialMode = 1.0;

    // First prove the probe can see these features at all, or the checks below
    // would pass by doing nothing.
    ChainParams plainCrossfeed = headphone;
    plainCrossfeed.crossfeed = 50.0;
    CHECK(chainStereo(plainCrossfeed, 0.0, false) != chainStereo(headphone, 0.0, false),
          "crossfeed works on its own");
    ChainParams plainAmbience = headphone;
    plainAmbience.spatialMode = 2.0;
    CHECK(chainStereo(plainAmbience, 0.0, false) != chainStereo(headphone, 0.0, false),
          "the ambience surround works on its own");

    for (double layout : {2.0, 3.0}) {
        ChainParams upmix = headphone;
        upmix.surroundType = layout;
        const std::vector<float> alone = chainStereo(upmix, 0.0, false);

        ChainParams withCrossfeed = upmix;
        withCrossfeed.crossfeed = 50.0;
        CHECK(chainStereo(withCrossfeed, 0.0, false) == alone,
              "crossfeed adds nothing while the upmix is placing speakers");

        ChainParams withAmbience = upmix;
        withAmbience.spatialMode = 2.0;          // headphone + the older ambience surround
        CHECK(chainStereo(withAmbience, 0.0, false) == alone,
              "the upmix retires the ambience surround rather than stacking on it");
    }

    const std::vector<float> tracked = chainStereo(headphone, 20.0, true);
    ChainParams trackedWithCrossfeed = headphone;
    trackedWithCrossfeed.crossfeed = 50.0;
    CHECK(chainStereo(trackedWithCrossfeed, 20.0, true) == tracked,
          "crossfeed steps aside while head tracking is placing speakers");
}

// Speakers get the upmix too, but folded rather than rendered binaurally: they
// already stand in a real room in front of the listener. The fold has to stay
// mono-compatible, because a centre that cancels in mono is worse than no
// surround at all.
static void test_speaker_upmix_folds_and_stays_mono_safe() {
    ChainParams speaker = ChainParams::flat();
    speaker.spatialMode = 0.0;

    ChainParams wide = speaker;
    wide.surroundType = 2.0;
    CHECK(chainStereo(wide, 0.0, false) != chainStereo(speaker, 0.0, false),
          "a speaker layout actually changes the output");
    const std::vector<float> folded = chainToneWithHead(wide, 0.0, false);

    // A centred (mono) programme must survive: nothing may cancel in the sum
    // and the source may not lean to a side. Wide carries a room, whose tail is
    // decorrelated between the outputs on purpose, so this is judged by energy
    // on broadband noise (a single tone lands in each output wherever the tail's
    // modes put it) rather than sample by sample.
    (void)folded;
    auto centredNoise = [](const ChainParams& params) {
        DSPChain chain;
        chain.prepare(kFs, 2);
        chain.setParams(params);
        const std::size_t frames = static_cast<std::size_t>(kFs * 2);
        std::vector<float> buf(frames * 2, 0.0f);
        unsigned state = 12345u;
        for (std::size_t f = 0; f < frames; ++f) {
            state = state * 1103515245u + 12345u;
            const float s = static_cast<float>(0.2 * ((double)((state >> 8) & 0xFFFFu) / 32768.0 - 1.0));
            buf[f * 2] = s;
            buf[f * 2 + 1] = s;
        }
        chain.processInterleaved(buf.data(), frames);
        return buf;
    };
    const std::vector<float> wideNoise = centredNoise(wide), dryNoise = centredNoise(speaker);
    double left = 0.0, right = 0.0, sum = 0.0, drySum = 0.0;
    for (std::size_t f = kFs / 2; f * 2 + 1 < wideNoise.size(); ++f) {
        left += (double)wideNoise[f * 2] * wideNoise[f * 2];
        right += (double)wideNoise[f * 2 + 1] * wideNoise[f * 2 + 1];
        const double mono = 0.5 * ((double)wideNoise[f * 2] + wideNoise[f * 2 + 1]);
        const double monoDry = 0.5 * ((double)dryNoise[f * 2] + dryNoise[f * 2 + 1]);
        sum += mono * mono;
        drySum += monoDry * monoDry;
    }
    std::printf("speaker Wide on centred noise: balance %+.2f dB, mono sum %+.2f dB against plain speakers\n",
                10.0 * std::log10(left / right), 10.0 * std::log10(sum / drySum));
    CHECK(std::fabs(10.0 * std::log10(left / right)) < 0.5, "a centred source stays centred through the speaker layout");
    CHECK(std::fabs(10.0 * std::log10(sum / drySum)) < 1.0, "and its mono sum stays within 1 dB of the plain speaker output");

    // Head tracking is meaningless on speakers — they do not move with the head.
    CHECK(chainStereo(wide, 45.0, true) == chainStereo(wide, 0.0, false),
          "a head angle changes nothing on speakers");
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
    test_virtual_room_follows_the_output();
    test_head_tracking_only_runs_when_it_should();
    test_virtual_room_respects_bypass();
    test_virtual_room_stays_inside_the_ceiling();
    test_upmix_runs_without_a_tracker();
    test_only_one_virtual_stage_renders();
    test_speaker_upmix_folds_and_stays_mono_safe();

    if (g_failures == 0) { printf("all dsp-chain tests passed\n"); return 0; }
    fprintf(stderr, "%d dsp-chain check(s) failed\n", g_failures);
    return 1;
}
