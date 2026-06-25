#ifndef ROOMCUT_SPATIAL_HPP
#define ROOMCUT_SPATIAL_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

class Spatial {
public:
    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        // Bass/side crossover for frequency-dependent width: below this the side
        // is kept centred (mono bass = a tight, phase-stable low end on speakers).
        const double bassHz = 250.0;
        bassAlpha_ = 1.0 - std::exp(-2.0 * kPi * bassHz / fs_);
        // RACE: ~90 us cancellation delay; headphone crossfeed ITD ~0.3 ms.
        raceDelay_ = std::max<std::size_t>(1, (std::size_t)std::lround(fs_ * 90.0e-6));
        if (raceDelay_ >= kMaxDelay) raceDelay_ = kMaxDelay - 1;
        cfDelay_ = std::max<std::size_t>(1, (std::size_t)std::lround(fs_ * 260.0e-6)); // ITD ~260us @ ±30°
        if (cfDelay_ >= kMaxDelay) cfDelay_ = kMaxDelay - 1;
        binShadowA_ = 1.0 - std::exp(-2.0 * kPi * 1500.0 / fs_);  // head-shadow LPF ~1.5 kHz
        pinnaDelay_ = std::max<std::size_t>(1, (std::size_t)std::lround(fs_ / (2.0 * 7500.0))); // ~7.5 kHz notch
        if (pinnaDelay_ >= kMaxDelay) pinnaDelay_ = kMaxDelay - 1;
        raceHpA_ = 1.0 / (1.0 + 2.0 * kPi * 250.0 / fs_);     // 1st-order HP ~250 Hz
        raceLpA_ = 1.0 - std::exp(-2.0 * kPi * 4500.0 / fs_); // 1st-order LP ~4.5 kHz
        surrLpA_ = 1.0 - std::exp(-2.0 * kPi * 4000.0 / fs_); // surround behind-cue roll-off ~4 kHz
        reset();
    }

    void setParams(double width, double centerFocus, double crossfeed, double roomReduce,
                   double mode = 0.0) {
        width_ = clamp(width, -100.0, 100.0);
        centerFocus_ = clamp(centerFocus, 0.0, 100.0);
        crossfeed_ = clamp(crossfeed, 0.0, 100.0);
        roomReduce_ = clamp(roomReduce, 0.0, 100.0);
        // mode: 0 = speaker (XTC), 1 = headphone, 2 = headphone+surround,
        //       3 = speaker+surround. Output device and surround are independent.
        const int m = (int)std::lround(mode);
        headphone_ = (m == 1 || m == 2);   // binaural path (in-head virtual speakers)
        surround_ = (m == 2 || m == 3);    // virtual surround (ambience field) added
        speakerSurround_ = (m == 3);       // surround layered on the speaker (XTC) path
        // Speaker XTC strength comes from the crossfeed/"Crosstalk 3D" amount.
        // No broadband makeup: RACE only cancels the 250 Hz–6 kHz band, so a
        // broadband boost would inflate the (uncancelled) bass — the opposite of
        // what we want. A mild center dip in-band is the inherent XTC tradeoff.
        raceGain_ = (crossfeed_ * 0.01) * kMaxRaceGain;
        raceMakeup_ = 1.0;
    }

    void reset() {
        sideLow_ = 0.0;
        for (std::size_t i = 0; i < kMaxDelay; ++i) {
            raceL_[i] = raceR_[i] = cfL_[i] = cfR_[i] = pinnaL_[i] = pinnaR_[i] = 0.0;
        }
        raceWrite_ = cfWrite_ = pinnaWrite_ = 0;
        hpXL_ = hpYL_ = hpXR_ = hpYR_ = lpL_ = lpR_ = 0.0;
        cfShadowL_ = cfShadowR_ = 0.0;
        surrLpL_ = surrLpR_ = 0.0;
        for (std::size_t i = 0; i < kSurrLen; ++i) surrLine_[i] = 0.0;
        surrWrite_ = 0;
    }

    inline void processFrame(float* frame, std::size_t channels) {
        if (channels < 2) {
            return;
        }

        const double left = frame[0];
        const double right = frame[1];
        const double mid = (left + right) * 0.5;
        const double side = (left - right) * 0.5;

        const double width = width_ * 0.01;
        const double center = centerFocus_ * 0.01;
        const double room = roomReduce_ * 0.01;
        const double crossfeed = crossfeed_ * 0.01;

        // Stronger, clearly audible ranges. Widen now uses about twice the previous
        // slope so the Space control reaches a much wider stage: the old +100 width
        // reads at roughly +50 now. Narrowing is left as before (it can only go down
        // to near-mono).
        //   width  +100 → treble side ×2.8 (was ×1.9), −100 → ×0.1 (near-mono)
        //   room   +100 → side ×0.25,  center +100 → side ×0.5 + mid +0.25
        const double commonGain = (1.0 - 0.75 * room) * (1.0 - 0.5 * center);
        const double midGain = 1.0 + 0.25 * center + 0.15 * room;

        // Frequency-dependent M/S ("shuffler"): split the side into a bass part
        // (≤250 Hz) kept tight (lows stay centred for phase/mono stability) and a
        // treble part that takes the full, widened width.
        sideLow_ += bassAlpha_ * (side - sideLow_);
        const double sideHigh = side - sideLow_;
        // Widen (width ≥ 0) uses the stronger slope; narrow (width < 0) keeps the old
        // mapping so the side bottoms out at near-mono (×0.1) instead of phase-
        // inverting — a negative side gain would flip L/R polarity, not narrow.
        double sideLowGain, sideHighGain;
        if (width >= 0.0) {
            sideLowGain  = (1.0 + 0.45 * width) * commonGain;   // bass stays tight
            sideHighGain = (1.0 + 1.8  * width) * commonGain;   // treble ×2.8 at +100
        } else {
            sideLowGain  = (1.0 + 0.27 * width) * commonGain;
            sideHighGain = (1.0 + 0.9  * width) * commonGain;   // −100 → ×0.1
        }
        const double outSide = sideLow_ * sideLowGain + sideHigh * sideHighGain;

        double outLeft = mid * midGain + outSide;
        double outRight = mid * midGain - outSide;

        // Width is applied to the SIDE only (mid is untouched), so a centred vocal —
        // which has no side — stays exactly centred at 12 o'clock no matter how far the
        // Space control is pushed. There is deliberately NO all-pass "synthesise width
        // from the centre" stage: a first-order all-pass added anti-symmetrically is
        // mono-safe but NOT image-safe — at each frequency it adds vs subtracts a
        // phase-rotated copy of the mid, which gave the centre a frequency-dependent
        // lateral tilt (the vocal drifted left as width rose, worst on the speaker/RACE
        // path). All the requested extra width now comes from the side gain above,
        // which only acts on genuine stereo content.
        //
        // The virtual surround field is built from this genuine programme side only, so
        // a pure-centre vocal (programmeSide == 0) drives zero ambience and the surround
        // stage adds nothing — decorrelate the surround, not the centre.
        const double programmeSide = outSide;

        // The "crossfeed" control means opposite things on the two playback systems
        // (per the spatial methodology): a binaural reshape on headphones, RACE
        // crosstalk cancellation on speakers.
        if (headphone_) {
            // Parametric binaural: render L/R as virtual speakers at ±30°. Each
            // channel reaches the OPPOSITE ear delayed (ITD), head-shadowed (LPF),
            // pinna-notched and ILD-attenuated → pulls the in-head image toward an
            // out-of-head stage. crossfeed = effect amount.
            const double ambPre = programmeSide;   // genuine side only (no centre-derived decorrelation)
            if (crossfeed > 0.0) {
                cfL_[cfWrite_] = outLeft;
                cfR_[cfWrite_] = outRight;
                const std::size_t rp = (cfWrite_ + kMaxDelay - cfDelay_) % kMaxDelay;
                const double delL = cfL_[rp];                        // ITD-delayed L
                const double delR = cfR_[rp];                        // ITD-delayed R
                cfWrite_ = (cfWrite_ + 1) % kMaxDelay;
                cfShadowR_ += binShadowA_ * (delR - cfShadowR_);     // head shadow (contra loses highs)
                cfShadowL_ += binShadowA_ * (delL - cfShadowL_);
                const std::size_t pp = (pinnaWrite_ + kMaxDelay - pinnaDelay_) % kMaxDelay;
                const double pinR = cfShadowR_ - pinnaGain_ * pinnaR_[pp];   // pinna notch (comb)
                const double pinL = cfShadowL_ - pinnaGain_ * pinnaL_[pp];
                pinnaR_[pinnaWrite_] = cfShadowR_;
                pinnaL_[pinnaWrite_] = cfShadowL_;
                pinnaWrite_ = (pinnaWrite_ + 1) % kMaxDelay;
                const double contraToL = pinR * ildGain_;            // RIGHT speaker → left ear
                const double contraToR = pinL * ildGain_;            // LEFT speaker → right ear
                // Surround keeps a slightly lighter binaural than plain headphone, but
                // still strong enough that the Crossfeed slider audibly externalises
                // (0.12 was so light the slider felt dead in surround mode).
                const double amount = (surround_ ? 0.30 : 0.35) * crossfeed;
                const double binL = outLeft  * (1.0 - amount) + contraToL * amount;
                const double binR = outRight * (1.0 - amount) + contraToR * amount;
                // Keep the CENTRE dry: crossfeed reshapes only the SIDE, so a centred
                // vocal isn't pushed down or combed. mid = the untouched (L+R)/2; the
                // binaural pass supplies only the new side — a pure-centre signal exits
                // unchanged.
                const double midDry = (outLeft + outRight) * 0.5;
                const double sideBin = (binL - binR) * 0.5;
                outLeft  = midDry + sideBin;
                outRight = midDry - sideBin;
            }
            if (surround_) {
                // Virtual surround: decorrelate the ORIGINAL (pre-binaural) ambience
                // into TWO different-phase copies and add one to EACH side (both with
                // a + sign) → a wide, enveloping field that stays left/right symmetric.
                // The old single decorrelator added L+ / R− with g=1.2, which piled the
                // (low-passed) ambience into the left channel — a fixed asymmetry that
                // made the left sound muffled while the right stayed clear.
                double ambL, ambR;
                surroundAmbience(ambPre, ambL, ambR);
                const double g = 0.55 * crossfeed;                // envelopment amount
                outLeft  += g * ambL;
                outRight += g * ambR;
            }
        } else {
            // Speaker crosstalk cancellation (XTC) via RACE: subtract a delayed,
            // band-limited copy of the opposite output and feed the result back
            // (recursive higher-order cancellation). Image spreads beyond the
            // speakers; bass stays clean (band-limited, ~250 Hz–6 kHz). Runs even
            // at zero strength (raceGain_ = 0 → transparent) so the recursion stays
            // warm and toggling is click-free.
            processRace(outLeft, outRight);
            if (speakerSurround_) {
                // Speaker surround: no binaural (speakers aren't in-head). RACE already
                // spreads the image beyond the speakers; layer a light, left/right-
                // symmetric decorrelated ambience on top for envelopment. Gentler than
                // headphones because the room itself already supplies reflections.
                // Built from the genuine programme side (not the decorrelated/RACE
                // output) so a centred vocal stays centred — see programmeSide above.
                double ambL, ambR;
                surroundAmbience(programmeSide, ambL, ambR);
                const double g = 0.6 * crossfeed;
                outLeft  += g * ambL;
                outRight += g * ambR;
            }
        }

        frame[0] = static_cast<float>(outLeft);
        frame[1] = static_cast<float>(outRight);
    }

private:
    static double clamp(double v, double lo, double hi) {
        return std::max(lo, std::min(hi, v));
    }

    // Build a left/right-symmetric surround ambience from two short, unequal delay
    // taps of the side signal (see body). Each copy is added with a + sign by the
    // caller, so the field stays balanced instead of piling into one channel the way
    // the old single copy added L+ / R−.
    inline void surroundAmbience(double side, double& ambL, double& ambR) {
        // Pure side, no mid: the surround ambience must not contain the centre, or the
        // vocal/centre image leaks into the (unequal) delay taps and pulls toward one
        // side. Mono / near-mono content simply gets no surround — widening the centre
        // is the Space control's job, not surround's.
        const double src = side;
        // Read two SHORT, unequal delay taps. A delayed copy is decorrelated from the
        // dry base, so adding it back per-side adds almost no L/R energy cross-term —
        // the field stays balanced — while the two different taps decorrelate L from R.
        // (A bare all-pass keeps the same frequencies in phase with the base, which is
        // what tipped the energy toward one side.)
        surrLine_[surrWrite_] = src;
        const std::size_t t1 = (surrWrite_ + kSurrLen - kSurrTap1) % kSurrLen;
        const std::size_t t2 = (surrWrite_ + kSurrLen - kSurrTap2) % kSurrLen;
        surrWrite_ = (surrWrite_ + 1) % kSurrLen;
        surrLpL_ += surrLpA_ * (surrLine_[t1] - surrLpL_);   // ~4 kHz distance roll-off
        surrLpR_ += surrLpA_ * (surrLine_[t2] - surrLpR_);
        ambL = surrLpL_;
        ambR = surrLpR_;
    }

    static constexpr double kPi = 3.14159265358979323846;

    double fs_ = 48000.0;
    double width_ = 0.0;
    double centerFocus_ = 0.0;
    double crossfeed_ = 0.0;
    double roomReduce_ = 0.0;
    bool headphone_ = false;   // false = speaker (XTC), true = headphone (crossfeed)
    double bassAlpha_ = 0.031980;
    double sideLow_ = 0.0;

    // --- RACE crosstalk canceller (speaker mode) ----------------------------
    // Cross-coupled recursive delay+attenuation: each output subtracts a delayed,
    // band-limited copy of the OPPOSITE output. The recursion (delay lines hold
    // past outputs) builds the higher-order cancellation. Band-limited to roughly
    // 250 Hz–6 kHz so the bass and the very top stay clean (RACE keeps a tight,
    // phase-stable low end — little crosstalk exists below ~400 Hz anyway).
    // Time-domain, per-sample, no look-ahead.
    static constexpr std::size_t kMaxDelay = 256;  // 768k * ~250us, with margin
    static constexpr double kMaxRaceGain = 0.50;   // < 1 keeps the recursion stable;
                                                   // gentler = less center dip / comb
    double raceL_[kMaxDelay] = {0.0};
    double raceR_[kMaxDelay] = {0.0};
    std::size_t raceWrite_ = 0;
    std::size_t raceDelay_ = 8;
    double raceGain_ = 0.0;     // set from crossfeed amount (speaker mode)
    double raceMakeup_ = 1.0;
    double raceHpA_ = 0.95;     // 1st-order high-pass coeff (~250 Hz)
    double raceLpA_ = 0.5;      // 1st-order low-pass coeff (~6 kHz)
    double hpXL_ = 0.0, hpYL_ = 0.0, hpXR_ = 0.0, hpYR_ = 0.0;  // HP states
    double lpL_ = 0.0, lpR_ = 0.0;                              // LP states

    // --- Headphone parametric binaural (virtual speakers at ±30°) -----------
    // Each input reaches the OPPOSITE ear delayed (ITD, spherical head ~260 us),
    // low-passed (head shadow), comb-notched (pinna cue ~7.5 kHz) and attenuated
    // (ILD). No measured HRTF — all time-domain IIR + short delay lines, no
    // look-ahead. Pulls the in-head image toward an out-of-head virtual stage.
    double cfL_[kMaxDelay] = {0.0};
    double cfR_[kMaxDelay] = {0.0};
    std::size_t cfWrite_ = 0;
    std::size_t cfDelay_ = 12;          // ITD ~260 us
    double cfShadowL_ = 0.0, cfShadowR_ = 0.0;
    double binShadowA_ = 0.2;           // head-shadow LPF (~1.5 kHz)
    double pinnaL_[kMaxDelay] = {0.0};
    double pinnaR_[kMaxDelay] = {0.0};
    std::size_t pinnaWrite_ = 0;
    std::size_t pinnaDelay_ = 3;        // pinna notch ~7.5 kHz (fs / 2D)
    double pinnaGain_ = 0.5;
    double ildGain_ = 0.62;

    // --- Virtual surround (headphone, spatialMode ≥ 2) ----------------------
    // Pull the stereo ambience (side) out, decorrelate it (2nd all-pass), roll
    // off the highs (a "behind/at a distance" cue) and add it anti-phase → an
    // enveloping virtual back/side field. Not real 5.1/7.1 channels (impossible
    // self-contained on a 2ch device); an ambience-based virtual surround.
    bool surround_ = false;
    bool speakerSurround_ = false;     // mode 3: surround layered on the speaker path
    static constexpr std::size_t kSurrLen = 512;    // ambience delay line (~10.7 ms @48k)
    static constexpr std::size_t kSurrTap1 = 252;   // ~5.25 ms tap (left ambience)
    static constexpr std::size_t kSurrTap2 = 393;   // ~8.19 ms tap (right ambience)
    double surrLine_[kSurrLen] = {0.0};
    std::size_t surrWrite_ = 0;
    double surrLpL_ = 0.0, surrLpR_ = 0.0;  // per-side ~4 kHz roll-off states
    double surrLpA_ = 0.3;             // ~4 kHz roll-off (distance/behind cue)

    // Band-limit the cancellation feed: 1st-order high-pass (keep bass out of the
    // canceller) then 1st-order low-pass (tame the top). `hpX/hpY/lp` are per-side.
    inline double raceBand(double x, double& hpX, double& hpY, double& lp) {
        const double hp = raceHpA_ * (hpY + x - hpX);  // high-pass (~250 Hz)
        hpX = x; hpY = hp;
        lp += raceLpA_ * (hp - lp);                    // low-pass (~6 kHz)
        return lp;
    }

    // One RACE step on the stereo output (in/out by reference).
    inline void processRace(double& outL, double& outR) {
        const std::size_t rp = (raceWrite_ + kMaxDelay - raceDelay_) % kMaxDelay;
        const double bL = raceBand(raceL_[rp], hpXL_, hpYL_, lpL_);  // delayed L, band-limited
        const double bR = raceBand(raceR_[rp], hpXR_, hpYR_, lpR_);
        const double yL = outL - raceGain_ * bR;   // subtract opposite (cross-coupled)
        const double yR = outR - raceGain_ * bL;
        raceL_[raceWrite_] = yL;                    // feed back (recursion via delay)
        raceR_[raceWrite_] = yR;
        raceWrite_ = (raceWrite_ + 1) % kMaxDelay;
        outL = yL * raceMakeup_;
        outR = yR * raceMakeup_;
    }
};

}

#endif
