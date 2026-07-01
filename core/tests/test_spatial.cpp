#include "Spatial.hpp"

#include <cmath>
#include <cstdio>
#include <random>

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

static void test_flat_passthrough() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 0.0, 0.0);

    float frame[2] = {0.25f, -0.125f};
    spatial.processFrame(frame, 2);

    CHECK_NEAR(frame[0], 0.25, 1e-7, "flat spatial preserves left");
    CHECK_NEAR(frame[1], -0.125, 1e-7, "flat spatial preserves right");
}

static void test_width_increases_side() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(50.0, 0.0, 0.0, 0.0);

    float frame[2] = {0.25f, -0.25f};
    spatial.processFrame(frame, 2);

    CHECK(frame[0] > 0.25f, "positive width raises left side sample");
    CHECK(frame[1] < -0.25f, "positive width lowers right side sample");
}

static void test_room_reduce_attenuates_side() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 0.0, 80.0);

    float frame[2] = {0.30f, -0.30f};
    spatial.processFrame(frame, 2);

    CHECK(std::fabs(frame[0]) < 0.30f, "room reduce attenuates side left");
    CHECK(std::fabs(frame[1]) < 0.30f, "room reduce attenuates side right");
}

static void test_center_focus_boosts_mid() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 80.0, 0.0, 0.0);

    float frame[2] = {0.20f, 0.20f};
    spatial.processFrame(frame, 2);

    CHECK(frame[0] > 0.20f, "center focus boosts mono left");
    CHECK(frame[1] > 0.20f, "center focus boosts mono right");
}

static void test_headphone_crossfeed_feeds_into_right() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 100.0, 0.0, 1.0);   // headphone mode → crossfeed ADDS
    // Crossfeed is now DELAY-based (ITD): drive a sustained left-only signal so the
    // delayed, head-shadowed left settles into the right output.
    float frame[2] = {0.0f, 0.0f};
    for (int i = 0; i < 4000; ++i) { frame[0] = 0.40f; frame[1] = 0.0f; spatial.processFrame(frame, 2); }
    CHECK(frame[1] > 0.0f, "headphone crossfeed feeds left into right");
}

// Headphone crossfeed must leave a CENTRED (mono) signal untouched — only the sides
// get the binaural reshape, so a vocal in the middle isn't pushed down or smeared.
static void test_headphone_crossfeed_preserves_centre() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 100.0, 0.0, 1.0);   // headphone, full crossfeed
    double errSq = 0.0, refSq = 0.0;
    const int n = 8000;
    for (int i = 0; i < n; ++i) {
        const float m = static_cast<float>(0.30 * std::sin(2.0 * M_PI * 700.0 * i / 48000.0));
        float frame[2] = {m, m};   // pure centre (L == R)
        spatial.processFrame(frame, 2);
        if (i >= 2000) {
            errSq += (double)(frame[0] - frame[1]) * (frame[0] - frame[1]);
            refSq += (double)m * m;
        }
    }
    CHECK(std::sqrt(errSq / refSq) < 1e-6, "headphone crossfeed keeps a centred signal centred");
}

static void test_speaker_xtc_drives_opposite_antiphase() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 100.0, 0.0, 0.0);   // speaker mode → RACE XTC SUBTRACTS
    // RACE is delay-based + band-limited (DC removed), so feed a tone on the left
    // only and check the right output it injects is anti-phase (XTC pushes the
    // opposite channel out of phase → image spreads beyond the speakers).
    double lr = 0.0, rr = 0.0;
    for (int i = 0; i < 8000; ++i) {
        const float s = static_cast<float>(0.40 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
        float frame[2] = {s, 0.0f};
        spatial.processFrame(frame, 2);
        if (i >= 2000) { lr += (double)s * frame[1]; rr += (double)frame[1] * frame[1]; }
    }
    CHECK(rr > 1e-6, "speaker XTC injects signal into the opposite (silent) channel");
    CHECK(lr < 0.0, "speaker XTC drives opposite channel anti-phase");
}

// Block-level side RMS over a sustained anti-phase 440 Hz tone (L=+s, R=-s):
// the input is pure side, so the output side RMS is exactly the width/room
// sideGain. Mirrors the e2e harness (sim --stereo + analyze-dump.py --stereo).
static double sideRmsAntiPhase(double width, double center, double crossfeed, double room,
                               double freqHz = 440.0) {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(width, center, crossfeed, room);
    const int n = 48000;
    double sideSq = 0.0;
    for (int i = 0; i < n; ++i) {
        const float s = static_cast<float>(0.25 * std::sin(2.0 * M_PI * freqHz * i / 48000.0));
        float frame[2] = {s, -s};
        spatial.processFrame(frame, 2);
        const double side = (frame[0] - frame[1]) * 0.5;
        sideSq += side * side;
    }
    return std::sqrt(sideSq / n);
}

static void test_width_is_frequency_dependent() {
    // Pure side in, no processing → side RMS unchanged (frequency-independent).
    CHECK_NEAR(sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0), 0.25 / std::sqrt(2.0), 1e-3,
               "flat preserves side RMS");
    // Four-band shuffler: width rises GRADUALLY up the spectrum so instrument ranges
    // separate cleanly. bass (≤250 Hz) stays tight; low-mid (250 Hz–1.2 kHz), presence
    // (1.2–9 kHz) and air (>9 kHz) open progressively more. Probe one frequency per band
    // and require the spread to increase monotonically with frequency.
    const double bassRatio   = sideRmsAntiPhase(100.0, 0.0, 0.0, 0.0, 60.0)
                             / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 60.0);
    const double lowMidRatio = sideRmsAntiPhase(100.0, 0.0, 0.0, 0.0, 600.0)
                             / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 600.0);
    const double midRatio    = sideRmsAntiPhase(100.0, 0.0, 0.0, 0.0, 3000.0)
                             / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 3000.0);
    const double airRatio    = sideRmsAntiPhase(100.0, 0.0, 0.0, 0.0, 14000.0)
                             / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 14000.0);
    CHECK(bassRatio < 1.8, "width +100 keeps bass side tight (mono-ish low end)");
    CHECK(lowMidRatio > bassRatio + 0.2, "low-mid opens more than bass");
    CHECK(midRatio > lowMidRatio + 0.2, "presence opens more than low-mid");
    CHECK(airRatio > midRatio + 0.15, "air opens most (top-down expansion)");
    CHECK(midRatio > 1.8, "presence band reaches a clearly wide spread at +100");
    // Moderate, non-clipping widen: +50 in the presence band sits between unity and +100.
    const double halfMid = sideRmsAntiPhase(50.0, 0.0, 0.0, 0.0, 3000.0)
                         / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 3000.0);
    CHECK(halfMid > 1.3 && halfMid < midRatio, "+50 presence spread sits between unity and +100");
    // Narrowing decays exponentially toward mono; doubled strength drives -100 well
    // past the old ×0.1 (to ~×0.04 treble) while staying strictly positive — the side
    // approaches mono without ever inverting.
    const double narrow = sideRmsAntiPhase(-100.0, 0.0, 0.0, 0.0, 4000.0)
                        / sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0, 4000.0);
    CHECK(narrow < 0.06, "width -100 narrows treble side well past the old x0.1");
    CHECK(narrow > 0.0, "narrowing never inverts the side (stays positive)");
}

// Correlation-adaptive width: narrow-but-genuine stereo (highly correlated channels)
// must be lifted MORE than already-wide (decorrelated/anti-phase) material at the same
// Space setting — and a TRUE mono source must stay mono (no fabricated width).
static void test_adaptive_width_lifts_correlated_stereo() {
    // side-out / side-in ratio for a 2 kHz tone after the correlation envelope settles.
    auto settledSideGain = [](double rScale) {
        Spatial s;
        s.prepare(48000.0);
        s.setParams(50.0, 0.0, 0.0, 0.0);   // widen +50, speaker
        double inSq = 0.0, outSq = 0.0;
        const int n = 144000;   // ~3 s so the slow (anti-pump) adaptive smoother settles
        for (int i = 0; i < n; ++i) {
            const double m = 0.3 * std::sin(2.0 * M_PI * 2000.0 * i / 48000.0);
            float frame[2] = {(float)m, (float)(rScale * m)};
            const double inSide = (frame[0] - frame[1]) * 0.5;
            s.processFrame(frame, 2);
            const double outSide = (frame[0] - frame[1]) * 0.5;
            if (i >= 120000) { inSq += inSide * inSide; outSq += outSide * outSide; }
        }
        return inSq > 1e-12 ? std::sqrt(outSq / inSq) : 0.0;
    };
    const double correlated = settledSideGain(0.6);    // R = 0.6L → corr +1, narrow stereo
    const double antiphase  = settledSideGain(-1.0);   // R = −L  → corr −1, already wide
    CHECK(correlated > antiphase + 0.1,
          "adaptive width lifts correlated/narrow stereo more than decorrelated");

    // True mono (L == R): side is zero, so the adaptive lift cannot invent any width.
    Spatial mono;
    mono.prepare(48000.0);
    mono.setParams(100.0, 0.0, 0.0, 0.0);
    double sideSq = 0.0;
    const int n = 8000;
    for (int i = 0; i < n; ++i) {
        const float m = (float)(0.3 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
        float frame[2] = {m, m};
        mono.processFrame(frame, 2);
        sideSq += (double)(frame[0] - frame[1]) * (frame[0] - frame[1]);
    }
    CHECK(sideSq < 1e-9, "adaptive width invents no side from a mono source");
}

// Widening the side recesses the centred vocal, so the mid gets a makeup boost tied to
// the Space amount: a centred (mono) source must come out LOUDER at full widen than at
// width 0 — that's the "keep the vocal solid" compensation. (It stays centred: L == R.)
static void test_widen_lifts_centre_to_keep_vocal_present() {
    auto centreRms = [](double width) {
        Spatial s;
        s.prepare(48000.0);
        s.setParams(width, 0.0, 0.0, 0.0);
        double sq = 0.0;
        const int n = 8000;
        for (int i = 0; i < n; ++i) {
            const float m = (float)(0.3 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
            float frame[2] = {m, m};   // pure centre (vocal)
            s.processFrame(frame, 2);
            // Centred → L == R; measure the mono level.
            sq += (double)frame[0] * frame[0];
        }
        return std::sqrt(sq / n);
    };
    const double flat = centreRms(0.0);
    const double wide = centreRms(100.0);
    CHECK(wide > flat * 1.15, "widening lifts the centre (vocal makeup) so it isn't buried");
    CHECK(wide < flat * 1.6, "centre makeup stays moderate (no over-boost)");
}

static void test_room_attenuates_side_block() {
    const double flat = sideRmsAntiPhase(0.0, 0.0, 0.0, 0.0);
    // roomReduce cuts the side exponentially (e^(-ln16 * room01)) at twice the previous
    // strength: +100 → ×0.0625 (~-24 dB), with the old +100 cut (×0.25) now at +50. The
    // curve keeps dropping the whole way — no hard floor / dead zone.
    CHECK_NEAR(sideRmsAntiPhase(0.0, 0.0, 0.0, 100.0) / flat, 0.0625, 0.01, "room 100 cuts side to ~x0.06");
    CHECK_NEAR(sideRmsAntiPhase(0.0, 0.0, 0.0, 50.0) / flat, 0.25, 0.02, "room 50 cuts side to x0.25");
}

// A centred (mono) source must stay a centred POINT image at any width: width acts on
// the SIDE only, and a mono source has no side. (The old all-pass "synthesise width
// from the centre" stage was removed — added anti-symmetrically it is mono-safe but not
// image-safe, giving the centre a frequency-dependent lateral tilt: the vocal drifted
// left as width rose.)
static void test_mono_source_stays_centred() {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(100.0, 0.0, 0.0, 0.0);   // full widen, speaker mode
    double sideSq = 0.0, refSq = 0.0;
    const int n = 4096;
    for (int i = 0; i < n; ++i) {
        const float m = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
        float frame[2] = {m, m};                 // identical L/R = pure mono
        spatial.processFrame(frame, 2);
        const double side = (frame[0] - frame[1]) * 0.5;
        sideSq += side * side;
        refSq += (double)m * m;
    }
    CHECK(std::sqrt(sideSq / refSq) < 1e-6,
          "a mono source stays centred at full width (no synthetic side)");
}

// Surround must stay LEFT/RIGHT symmetric: equal-energy (decorrelated) L/R in →
// near-equal energy L/R out. The old single-decorrelator surround added the
// low-passed ambience as L+ / R−, which let it pile into one channel (the left
// went muffled while the right stayed clear).
static void test_surround_keeps_lr_balanced(double mode, const char* what) {
    Spatial spatial;
    spatial.prepare(48000.0);
    spatial.setParams(0.0, 0.0, 100.0, 0.0, mode);   // full surround amount
    // Decorrelated, equal-energy broadband L/R (stands in for real music). The old
    // single-decorrelator surround pushed the low-passed ambience into one channel;
    // the delay-tap ambience must keep both channels within a few % of each other.
    // (A single sine pair is an artificial worst case — fixed delay/base phase — and
    // is not representative of program material.)
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-0.30f, 0.30f);
    double lSq = 0.0, rSq = 0.0;
    const int n = 48000;
    for (int i = 0; i < n; ++i) {
        float frame[2] = {dist(rng), dist(rng)};
        spatial.processFrame(frame, 2);
        if (i >= 8000) { lSq += (double)frame[0] * frame[0]; rSq += (double)frame[1] * frame[1]; }
    }
    const double lr = std::sqrt(lSq), rr = std::sqrt(rSq);
    const double imbalance = std::fabs(lr - rr) / (0.5 * (lr + rr) + 1e-12);
    CHECK(imbalance < 0.03, what);
}

// Speaker surround (mode 3) layers an ambience field on top of the pure XTC
// (mode 0) — the two outputs must differ for the same input.
static void test_speaker_surround_adds_field() {
    Spatial xtc, surr;
    xtc.prepare(48000.0);
    surr.prepare(48000.0);
    xtc.setParams(0.0, 0.0, 100.0, 0.0, 0.0);    // speaker, no surround
    surr.setParams(0.0, 0.0, 100.0, 0.0, 3.0);   // speaker + surround
    double diffSq = 0.0;
    const int n = 8000;
    for (int i = 0; i < n; ++i) {
        const float l = static_cast<float>(0.30 * std::sin(2.0 * M_PI * 440.0 * i / 48000.0));
        const float r = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 557.0 * i / 48000.0));
        float fx[2] = {l, r}, fs[2] = {l, r};
        xtc.processFrame(fx, 2);
        surr.processFrame(fs, 2);
        if (i >= 2000) diffSq += (fx[0] - fs[0]) * (fx[0] - fs[0]) + (fx[1] - fs[1]) * (fx[1] - fs[1]);
    }
    CHECK(diffSq > 1e-3, "speaker surround adds an ambience field on top of XTC");
}

// Regression: with surround ON and the Space width raised, a CENTRED (mono) vocal
// must stay centred (12 o'clock). The virtual surround field is built from the genuine
// programme side only, so a pure-centre input drives zero ambience — speaker surround
// (mode 3) must then produce exactly the same output as plain XTC (mode 0). Before the
// fix the surround read the centre-derived decorrelation and the unequal delay taps
// pulled the vocal off-centre (toward 11 o'clock), worsening as width rose.
static void test_surround_keeps_centre_speaker() {
    Spatial xtc, surr;
    xtc.prepare(48000.0);
    surr.prepare(48000.0);
    xtc.setParams(100.0, 0.0, 100.0, 0.0, 0.0);    // speaker, full width, no surround
    surr.setParams(100.0, 0.0, 100.0, 0.0, 3.0);   // speaker + surround, full width
    double diffSq = 0.0, refSq = 0.0;
    const int n = 12000;
    for (int i = 0; i < n; ++i) {
        const float m = static_cast<float>(0.30 * std::sin(2.0 * M_PI * 700.0 * i / 48000.0));
        float fx[2] = {m, m}, fs[2] = {m, m};   // pure centre (L == R)
        xtc.processFrame(fx, 2);
        surr.processFrame(fs, 2);
        if (i >= 2000) {
            diffSq += (double)(fx[0]-fs[0])*(fx[0]-fs[0]) + (double)(fx[1]-fs[1])*(fx[1]-fs[1]);
            refSq  += (double)fx[0]*fx[0] + (double)fx[1]*fx[1];
        }
    }
    CHECK(std::sqrt(diffSq / refSq) < 1e-6,
          "speaker surround leaves a centred vocal identical to plain XTC (no left pull)");
}

// Headphone counterpart. The headphone surround/no-surround pair also differs by a
// deliberate, light crossfeed-amount tweak, so the surround-OFF output isn't bit-
// identical here — but for a CENTRED vocal it must stay within ~1%. Before the fix the
// centre-derived decorrelation leaked into the surround taps and pushed this well past
// 8% (the audible left drift). A single 700 Hz tone is the worst case for the leak
// (the all-pass decorrelation is concentrated at one frequency).
static void test_surround_keeps_centre_headphone() {
    Spatial s1, s2;
    s1.prepare(48000.0);
    s2.prepare(48000.0);
    s1.setParams(100.0, 0.0, 100.0, 0.0, 1.0);   // headphone, no surround
    s2.setParams(100.0, 0.0, 100.0, 0.0, 2.0);   // headphone + surround
    double diffSq = 0.0, refSq = 0.0;
    const int n = 24000;
    for (int i = 0; i < n; ++i) {
        const float m = static_cast<float>(0.30 * std::sin(2.0 * M_PI * 700.0 * i / 48000.0));
        float f1[2] = {m, m}, f2[2] = {m, m};     // pure centre (L == R)
        s1.processFrame(f1, 2);
        s2.processFrame(f2, 2);
        if (i >= 4000) {
            diffSq += (double)(f2[0]-f1[0])*(f2[0]-f1[0]) + (double)(f2[1]-f1[1])*(f2[1]-f1[1]);
            refSq  += (double)f1[0]*f1[0] + (double)f1[1]*f1[1];
        }
    }
    CHECK(std::sqrt(diffSq / refSq) < 0.03,
          "headphone surround barely changes a centred vocal (no centre leak into surround)");
}

int main() {
    test_flat_passthrough();
    test_width_increases_side();
    test_room_reduce_attenuates_side();
    test_center_focus_boosts_mid();
    test_headphone_crossfeed_feeds_into_right();
    test_headphone_crossfeed_preserves_centre();
    test_speaker_xtc_drives_opposite_antiphase();
    test_width_is_frequency_dependent();
    test_adaptive_width_lifts_correlated_stereo();
    test_widen_lifts_centre_to_keep_vocal_present();
    test_room_attenuates_side_block();
    test_mono_source_stays_centred();
    test_surround_keeps_lr_balanced(2.0, "headphone surround keeps L/R balanced");
    test_surround_keeps_lr_balanced(3.0, "speaker surround keeps L/R balanced");
    test_speaker_surround_adds_field();
    test_surround_keeps_centre_speaker();
    test_surround_keeps_centre_headphone();

    if (g_failures == 0) {
        std::printf("all spatial tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d spatial check(s) failed\n", g_failures);
    return 1;
}
