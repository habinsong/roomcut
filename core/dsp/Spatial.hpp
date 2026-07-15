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
        // Four-band side crossovers for frequency-dependent width (complementary
        // one-pole = zero latency, sums back to the input exactly). The bands let the
        // width grow gradually up the spectrum so different instrument ranges separate
        // cleanly: bass stays mono-tight, low-mids open a little, presence opens more,
        // air opens most. Crossovers ~250 Hz / 1.2 kHz / 9 kHz.
        xover1Coef_ = 1.0 - std::exp(-2.0 * kPi * 250.0  / fs_);
        xover2Coef_ = 1.0 - std::exp(-2.0 * kPi * 1200.0 / fs_);
        xover3Coef_ = 1.0 - std::exp(-2.0 * kPi * 9000.0 / fs_);
        // Adaptive widening is deliberately SLOW so it can never pump/breathe on busy,
        // fast-changing material (rock, EDM): a ~0.25 s correlation estimate feeds a
        // further ~0.7 s smoother on the applied amount, so the effective width can only
        // drift over ~1-2 s (a gentle swell), far below any audible modulation rate.
        corrAlpha_ = 1.0 - std::exp(-1.0 / (fs_ * 0.25));
        adaptSmoothA_ = 1.0 - std::exp(-1.0 / (fs_ * 0.70));
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
        // Surround ambience taps in TIME, not fixed sample counts: the old
        // constexpr 252 / 393 were ~5.25 ms / ~8.19 ms only at 48 kHz and shrank
        // 8x at 384 kHz (the field collapsed at hi-res). Compute from fs so the
        // decorrelation delay is rate-independent. At 48 kHz these round back to
        // 252 / 393 exactly (bit-identical to the old behaviour). Clamp to the
        // line length for extreme rates.
        surrTap1_ = std::max<std::size_t>(1, (std::size_t)std::lround(fs_ * 5.25e-3));
        surrTap2_ = std::max<std::size_t>(1, (std::size_t)std::lround(fs_ * 8.1875e-3));
        if (surrTap1_ >= kSurrLen) surrTap1_ = kSurrLen - 1;
        if (surrTap2_ >= kSurrLen) surrTap2_ = kSurrLen - 1;
        reset();
    }

    void setParams(double width, double centerFocus, double crossfeed, double roomReduce,
                   double mode = 0.0) {
        // Every spatial effect runs at ~2x its previous strength for the same slider
        // reading. The values keep their displayed range (-100..100 / 0..100); the
        // doubling lives in the MAPPING CURVES (here and in processFrame), which are
        // multiplicative/asymptotic rather than additive-linear. That keeps every
        // control monotonic across the WHOLE slider — no value saturates into a dead
        // zone — and the side gain stays strictly positive, so narrowing approaches
        // mono instead of crossing zero into an L/R polarity flip.
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
        // Speaker XTC strength: doubled initial slope, but eased ASYMPTOTICALLY toward a
        // sub-1 ceiling instead of clamped. raceGain_ >= 1 would self-oscillate (the
        // RACE recursion is cross-coupled), so a hard cap would leave the top of the
        // slider dead; the exponential approach keeps it rising smoothly the whole way.
        const double cf01 = crossfeed_ * 0.01;
        raceGain_ = kRaceGainCeil * (1.0 - std::exp(-kRaceDrive * cf01));
        raceMakeup_ = 1.0;

        // --- Precompute the per-band side gains here (not per sample) -------------
        // All the transcendental work (exp) and the widen/narrow branch happen once
        // per parameter change instead of once per sample → the render path is just
        // four one-pole filters + multiplies. Optimisation with no behavioural change.
        w01_ = width_ * 0.01;
        const double c01 = centerFocus_ * 0.01;
        const double r01 = roomReduce_ * 0.01;
        const double centerFactor = std::exp(-kCenterDrive * c01);   // side cut from Center
        // Mid (centre) boost. The widen term is the key fix for "the vocal gets buried
        // when I widen": boosting only the side recesses the centred lead vocal, so we
        // lift the mid in proportion to how far Space is pushed (+100 → ~+2.6 dB) to keep
        // the vocal solid and forward against the now-wider, louder sides. Only on widen
        // (max(0, w01_)); narrowing/centre/damping keep their own terms.
        midGain_ = 1.0 + 0.5 * c01 + 0.3 * r01 + kWidenMidMakeup * std::max(0.0, w01_);
        // Frequency-weighted Damping: pull down the low / low-mid side hardest (that's
        // where room boom and mud live) but leave the air band far more intact, so heavy
        // damping cleans the low end without dulling the top or smearing separation.
        const double dBass   = std::exp(-kRoomDrive * r01 * 1.00);
        const double dLowMid = std::exp(-kRoomDrive * r01 * 1.00);
        const double dMid    = std::exp(-kRoomDrive * r01 * 0.70);
        const double dAir    = std::exp(-kRoomDrive * r01 * 0.45);
        // Center×Damping per band: the WIDTH factor for the upper bands is applied per
        // frame (so the correlation-adaptive lift can ride on top — see processFrame).
        cdLowMid_ = centerFactor * dLowMid;
        cdMid_    = centerFactor * dMid;
        cdAir_    = centerFactor * dAir;
        if (w01_ >= 0.0) {
            // Widen. Graduated slopes (+100 → bass ×1.4, low-mid ×1.9, presence ×2.3,
            // air ×2.9) spread brightness/air more than the muddier low-mids → cleaner
            // separation while the centre/bass stay solid. These are deliberately
            // moderate (~+3…+9 dB): the old ×4.6/×6.4 boosts pushed panned content far
            // past 0 dBFS and the limiter crushed it (audible breakup). Bass is
            // non-adaptive (kept tight); the upper bands get the correlation-adaptive lift.
            narrowMode_ = false;
            sgBass_ = (1.0 + 0.4 * w01_) * centerFactor * dBass;
            adaptDepth_ = kAdaptDepthMax;
        } else {
            // Narrow collapses the whole upper side toward mono (strictly positive →
            // never inverts); bass narrows gently. No adaptive lift while narrowing.
            narrowMode_ = true;
            adaptDepth_ = 0.0;
            sgNarrowLow_  = std::exp(kNarrowLow  * w01_) * centerFactor * dBass;
            sgNarrowHigh_ = std::exp(kNarrowHigh * w01_) * centerFactor * dMid;
        }
    }

    void reset() {
        sideLp1_ = sideLp2_ = sideLp3_ = 0.0;
        envLL_ = envRR_ = envLR_ = 0.0;
        adaptAmt_ = 0.0;
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

        const double crossfeed = crossfeed_ * 0.01;

        // Four-band frequency-dependent M/S ("shuffler"), SIDE ONLY. Complementary
        // one-pole splits → zero latency and the bands sum back to `side` EXACTLY at
        // unity (width 0 = bit-exact passthrough). Acting only on the side keeps a
        // centred vocal (side = 0) locked at 12 o'clock and makes the whole stage
        // mono-safe (the side cancels in a mono fold-down). Each band carries its own,
        // precomputed width gain (rising up the spectrum) and Damping weight, so
        // instruments in different ranges separate cleanly while the bass/centre stay
        // solid — no decorrelation/all-pass, so no comb or transient smear.
        //   bBass   ≤250 Hz      : mono-tight bass
        //   bLowMid 250 Hz–1.2 kHz: body, opens a little
        //   bMid    1.2–9 kHz    : presence, opens more
        //   bAir    >9 kHz       : air, opens most
        sideLp1_ += xover1Coef_ * (side - sideLp1_);
        const double bBass = sideLp1_;
        const double rem1 = side - sideLp1_;
        sideLp2_ += xover2Coef_ * (rem1 - sideLp2_);
        const double bLowMid = sideLp2_;
        const double rem2 = rem1 - sideLp2_;
        sideLp3_ += xover3Coef_ * (rem2 - sideLp3_);
        const double bMid = sideLp3_;
        const double bAir = rem2 - sideLp3_;

        // Running L/R correlation of the SOURCE (smoothed). High positive correlation =
        // narrow, near-mono-sounding stereo → lift the upper-band width toward (and a
        // little past) the set amount so it opens up. Low or negative correlation =
        // already wide / out-of-phase → no lift, which avoids over-widening and keeps it
        // mono-safe. A true mono source has zero side, so this can never fabricate width.
        envLL_ += corrAlpha_ * (left * left - envLL_);
        envRR_ += corrAlpha_ * (right * right - envRR_);
        envLR_ += corrAlpha_ * (left * right - envLR_);
        const double corrDen = std::sqrt(envLL_ * envRR_);
        double corrPos = corrDen > 1.0e-9 ? envLR_ / corrDen : 0.0;
        if (corrPos < 0.0) corrPos = 0.0; else if (corrPos > 1.0) corrPos = 1.0;
        // Extra slow smoothing of the APPLIED lift → the width drifts, never pumps, even
        // when the source correlation jumps around section-to-section.
        adaptAmt_ += adaptSmoothA_ * (corrPos - adaptAmt_);

        double outSide;
        if (!narrowMode_) {
            // Effective width = set width scaled up for correlated/narrow material, but
            // capped just past the +100 ceiling so the lift can never run away.
            const double effW = std::min(kAdaptWidthCeil, w01_ * (1.0 + adaptDepth_ * adaptAmt_));
            // Per-band widen slopes. Raised from 0.9/1.3/1.9 (v1.0.3-4): the
            // 4-band split had dropped the low-mid/presence side gain (~×1.9/×2.3)
            // well below the old flat ~×2.8 above 250 Hz, so +100 read narrower
            // than earlier builds. These restore and slightly exceed that width
            // where envelopment lives, bass still held tight. Measured: side
            // ~×2.6-2.9 at +100, worst-case (fully decorrelated, hot) limiter GR
            // ~3 dB — safe, real music far less.
            outSide = bBass * sgBass_
                    + bLowMid * ((1.0 + 1.6 * effW) * cdLowMid_)
                    + bMid    * ((1.0 + 2.0 * effW) * cdMid_)
                    + bAir    * ((1.0 + 2.5 * effW) * cdAir_);
        } else {
            const double bHigh = bLowMid + bMid + bAir;   // = side − bBass
            outSide = bBass * sgNarrowLow_ + bHigh * sgNarrowHigh_;
        }

        double outLeft = mid * midGain_ + outSide;
        double outRight = mid * midGain_ - outSide;

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
                const double amount = (surround_ ? 0.60 : 0.70) * crossfeed;   // doubled binaural depth
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
                // Surround ON carries its own BASE amount: it used to scale purely
                // with the Crossfeed slider, so at crossfeed 0 (e.g. the headphone
                // Widen preset) the Surround toggle was silent — "doesn't work".
                // Crossfeed still scales it up to the same full-slider strength.
                double ambL, ambR;
                surroundAmbience(ambPre, ambL, ambR);
                const double g = 1.10 * (kSurrBase + (1.0 - kSurrBase) * crossfeed);
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
                // Same base amount as the headphone path: the toggle must be audible
                // even with Crosstalk 3D at 0 (see kSurrBase).
                double ambL, ambR;
                surroundAmbience(programmeSide, ambL, ambR);
                const double g = 1.2 * (kSurrBase + (1.0 - kSurrBase) * crossfeed);
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
        const std::size_t t1 = (surrWrite_ + kSurrLen - surrTap1_) % kSurrLen;
        const std::size_t t2 = (surrWrite_ + kSurrLen - surrTap2_) % kSurrLen;
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
    // Four-band side shuffler: complementary one-pole crossover coeffs (set in
    // prepare) + their LP states. bBass/bLowMid/bMid/bAir sum back to the side.
    double xover1Coef_ = 0.031980;   // ~250 Hz
    double xover2Coef_ = 0.149003;   // ~1.2 kHz
    double xover3Coef_ = 0.690542;   // ~9 kHz
    double sideLp1_ = 0.0;
    double sideLp2_ = 0.0;
    double sideLp3_ = 0.0;
    // Precomputed in setParams (no per-sample exp). The upper-band WIDTH factor is
    // applied per frame so the correlation-adaptive lift can ride on cd* (center×damp).
    bool narrowMode_ = false;
    double midGain_ = 1.0;
    double w01_ = 0.0;                                   // set width, normalised −1..1
    double sgBass_ = 1.0;                                // bass side gain (non-adaptive)
    double cdLowMid_ = 1.0, cdMid_ = 1.0, cdAir_ = 1.0;  // center×damp per upper band
    double sgNarrowLow_ = 1.0, sgNarrowHigh_ = 1.0;      // narrow-mode gains
    double adaptDepth_ = 0.0;                            // correlation-lift depth (widen only)
    // Adaptive width: smoothed L/R correlation envelopes of the source + a slow
    // smoother on the applied amount (anti-pumping).
    double corrAlpha_ = 8.33e-5;
    double adaptSmoothA_ = 2.98e-5;
    double envLL_ = 0.0, envRR_ = 0.0, envLR_ = 0.0;
    double adaptAmt_ = 0.0;
    static constexpr double kAdaptDepthMax = 0.35;  // up to +35% effective width when fully correlated
    static constexpr double kAdaptWidthCeil = 1.15; // hard cap just past +100 → lift can't run away / clip
    static constexpr double kWidenMidMakeup = 0.25; // mid lift at +100 widen (~+1.9 dB) → vocal stays present

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
    static constexpr double kRaceGainCeil = 0.90;  // stability asymptote: the XTC drive
                                                   // approaches but never reaches 1
    static constexpr double kRaceDrive = 1.0 / kRaceGainCeil;  // → 2x initial slope (≈1.0)

    // --- "2x strength" mapping curves (multiplicative, monotonic, no dead zone) ------
    // Side attenuation (Room/Center) decays exponentially: e^(-k) at the slider top.
    static constexpr double kRoomDrive = 2.7725887;    // ln(16):  room +100 → side ×0.0625
    static constexpr double kCenterDrive = 1.3862944;  // ln(4):   center +100 → side ×0.25
    // Narrow width decays exponentially toward mono (strictly positive, never inverts).
    static constexpr double kNarrowHigh = 4.6051702;   // ln(100): −100 → treble side ×0.01
    static constexpr double kNarrowLow = 0.63;         // −100 → bass side ×0.53
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
    // Surround ON always contributes at least this fraction of the full envelopment;
    // the crossfeed amount scales the rest (full slider = unchanged old strength).
    static constexpr double kSurrBase = 0.40;
    // Line sized for the longest tap (~8.19 ms) at the highest supported rate
    // (768 kHz → ~6290 samples); 8192 (pow2) covers it with margin. The taps
    // themselves are set from fs in prepare() (surrTap1_/surrTap2_).
    static constexpr std::size_t kSurrLen = 8192;
    std::size_t surrTap1_ = 252;   // ~5.25 ms (set from fs in prepare)
    std::size_t surrTap2_ = 393;   // ~8.19 ms (set from fs in prepare)
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
