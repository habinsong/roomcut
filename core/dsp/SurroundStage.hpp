/*
 * SurroundStage.hpp — renders the programme onto the speakers the listener
 * actually has, real or virtual.
 *
 * Three jobs meet here because they are the same job: place the programme in
 * space. On headphones that means virtual loudspeakers (anchored to the room
 * while the head turns, if a tracker is attached); on real speakers it means
 * folding the same decomposition back down and throwing it wide. The upmix
 * that produces the channels lives next door in Upmixer.hpp.
 *
 * On headphones the whole stage turns with you: look left and the singer goes
 * with you, which is the main reason a mix stays "inside the head". Anchoring
 * the speakers to the room instead — the head moves, the speakers do not — is
 * what the externalisation literature finds helps most for centre-front
 * sources (Best et al., Trends in Hearing 24, 2020).
 *
 * Structure: a set of virtual loudspeakers, each rendered by VirtualSpeaker. A
 * head yaw of +psi (turning right) moves every speaker to -psi relative to the
 * head. The set depends on what is being played:
 *
 *   - A stereo pair goes to speakers at -30 and +30, the stereo standard.
 *   - An upmixed programme (see Upmixer) goes to the ITU-R BS.775 layout:
 *     5.1 places C/L/R/Ls/Rs at 0, -+30 and -+110; 7.1 moves the surrounds to
 *     -+90 and adds a back pair at -+135.
 *
 * The upmix path is what makes this stage useful without a head tracker: a
 * centre channel has nowhere to go on headphones unless something renders it as
 * a speaker standing in front of the listener.
 *
 * Level: each ear now hears BOTH speakers, the way it does in a real room, so
 * a centred signal arrives hotter than in plain headphone playback and an
 * uncorrelated pair less so (the head shadow takes back part of the sum). One
 * fixed bus gain cannot hold both at unity, so kBusGain is set from measured
 * output — halfway between the two cases — leaving about +-0.9 dB at the
 * extremes and less than that for real music.
 *
 * Engaging and disengaging ramps over 20 ms, so losing the head tracker mid
 * track fades back to ordinary playback instead of clicking.
 *
 * Real-time safe after prepare(): no allocation, no locks.
 */
#ifndef ROOMCUT_SURROUND_STAGE_HPP
#define ROOMCUT_SURROUND_STAGE_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "SpeakerColour.hpp"
#include "Upmixer.hpp"
#include "VirtualSpeaker.hpp"

namespace roomcut {

class SurroundStage {
public:
    // Where the front pair stands when the listener faces forward.
    static constexpr double kBaseAngleDegrees = 30.0;
    // Measured the same way as kUpmixBusGain: halfway between the two extremes.
    // At 0.7486 a centred mix came out +0.35 dB and an uncorrelated one -1.15,
    // so the pair is centred by trimming the difference. (It moves whenever the
    // far-ear emphasis below changes — that takes level out of wide material.)
    static constexpr double kBusGain = 0.7839;
    // Head rotation is not limited. It used to stop at 60 degrees, which meant
    // the stage quietly stopped following once a listener turned further and
    // the sound no longer agreed with where their head was. A 5.1 or 7.1 layout
    // has speakers all the way round, so a full turn is exactly what it should
    // render; a stereo pair simply ends up behind the listener, which is also
    // what really happens when you turn your back on two speakers.
    // C, L, R, Ls, Rs, Lb, Rb — the widest layout this renders.
    static constexpr std::size_t kMaxSpeakers = 7;
    static constexpr double kPi = 3.14159265358979323846;

    // Angles live on (-180, 180]. A speaker turned past the rear centre line
    // comes round the other side rather than sticking against a limit.
    static double wrap180(double degrees) {
        degrees = std::fmod(degrees, 360.0);
        if (degrees > 180.0) degrees -= 360.0;
        if (degrees < -180.0) degrees += 360.0;
        return degrees;
    }
    // The upmixed path has no fixed bus gain: matchLevel() hands back exactly
    // the level that came in, whatever the programme is, which is the only way
    // "how wide" and "how loud" stay separate controls.
    // Speakers: how much wider than the boxes themselves the surround channels
    // are thrown. At 1.0 the fold is an identity — centre plus front plus
    // surround is exactly the channel they were carved from — so this value IS
    // the whole effect. Kept modest on purpose: two speakers in front of you
    // cannot put a source behind you, and pushing harder only smears the front.
    static constexpr double kSpeakerSurroundWiden = 1.8;
    // Headphones: extra attenuation of the far ear, scaled by how far to the
    // side the speaker currently stands (nothing at all straight ahead).
    //
    // The sphere model's own ILD is already realistic — measured 15.4 dB at 90
    // degrees, inside the 10-15 dB the literature reports — so this is NOT a
    // correction to the model. It is a deliberate step past it, and the reason
    // is that headphones are not a room: raw headphone stereo gives every source
    // perfect channel separation, so an accurate virtual stage lands NARROWER
    // than the thing it replaces (measured IACC 0.71 against 0.56 dry) and reads
    // as "nothing happened". This buys the contrast back. Measured at this
    // value, a 40-degree head turn moves the image 8.5 dB instead of 3.6.
    //
    // It also buys back what Surround Depth costs. Material steered to the
    // surrounds sits at -+110, where the ear difference is already near its
    // maximum, so rotating it changes almost nothing — turning Depth up used to
    // take the head-tracking cue from 7.2 dB down to 5.8. At this emphasis the
    // same setting reads 6.8 dB, so the two controls stop fighting each other.
    static constexpr double kIldEmphasisDb = 9.0;

    // --- How the surround channels are delivered -----------------------------
    //
    // NOT as two more virtual loudspeakers. That was the whole problem with the
    // first attempt: on headphones every virtual speaker reaches both ears, and
    // raw headphone stereo already has perfect channel separation, so rendering
    // 5.1 that way came out NARROWER than doing nothing (measured IACC 0.671
    // against 0.584 dry). Pushing the far-ear emphasis to 30 dB only reached
    // parity; decorrelating the surrounds made it worse (0.737); widening the
    // angles made it worse. The approach could not win, because it was trying to
    // beat infinite channel separation with a model that adds crosstalk.
    //
    // What envelopment actually needs is the opposite: energy at one ear that
    // the other ear does NOT get. So the surround bus is delivered the way a
    // room delivers late sound — a little later, a little darker, and weighted
    // towards the difference between the ears rather than their sum. Measured
    // with these values, 5.1 lands at IACC -0.03 against 0.584 dry: clearly
    // wider than no processing at all, which is what the feature promises.
    //
    // The front three (centre and the L/R pair) still go through the virtual
    // speakers, so the image in front stays where it is and keeps following the
    // listener's head. Diffuse sound does not localise anyway, which is why
    // turning Surround Depth up no longer costs the head-tracking cue.
    // The side pair and the back pair arrive at different times, which is the
    // only thing that makes 7.1 different from 5.1 here: a second, later and
    // wider layer rather than two more speakers in a place headphones cannot
    // really put them.
    static constexpr double kSurroundDelayMs = 13.0;
    static constexpr double kSurroundSkewMs = 6.5;     // right ear later than left
    static constexpr double kBackDelayMs = 23.0;
    static constexpr double kBackSkewMs = 9.0;
    static constexpr double kSurroundLowpassHz = 6000.0;
    // How hard the two ears are pushed apart. This is what buys the
    // envelopment, and on its own it would also multiply uncorrelated energy by
    // (1 + B*B)/2 — nearly 8 dB, so a decorrelated passage would jump in
    // loudness while a centred one did not. It does not, because the bus is
    // level-matched below: the same energy goes out as came in, just arranged
    // so the ears disagree about it.
    static constexpr double kSurroundSideBias = 2.2;
    static constexpr double kSurroundGain = 1.5;
    // How wide the result is and how loud it is have to be separate controls,
    // and with a fixed gain they are not: pushing the ears apart adds energy in
    // exact proportion to how much side content the programme already had, so a
    // wide passage came out 8 dB hotter than a narrow one. The stage therefore
    // hands back the level it was given — one scalar on both ears, which cannot
    // touch the spatial arrangement at all, only how loud it is. Slow enough not
    // to pump on programme, fast enough to follow a cut.
    static constexpr double kSurroundMatchSeconds = 0.25;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        rampStep_ = 1.0 / std::max(1.0, fs_ * 0.020);
        emphasisStep_ = 1.0 - std::exp(-1.0 / (fs_ * 0.012));
        for (auto& speaker : speaker_) speaker.prepare(fs_);
        upmix_.prepare(fs_);
        for (auto& colour : colour_) colour.prepare(fs_);
        delay_ = std::min<std::size_t>(kSurroundLine - 2,
                    static_cast<std::size_t>(std::lround(fs_ * kSurroundDelayMs * 0.001)));
        delaySkew_ = std::min<std::size_t>(kSurroundLine - 2,
                    static_cast<std::size_t>(std::lround(fs_ * (kSurroundDelayMs + kSurroundSkewMs) * 0.001)));
        backDelay_ = std::min<std::size_t>(kSurroundLine - 2,
                    static_cast<std::size_t>(std::lround(fs_ * kBackDelayMs * 0.001)));
        backSkew_ = std::min<std::size_t>(kSurroundLine - 2,
                    static_cast<std::size_t>(std::lround(fs_ * (kBackDelayMs + kBackSkewMs) * 0.001)));
        lowA_ = 1.0 - std::exp(-2.0 * kPi * kSurroundLowpassHz / fs_);
        matchA_ = 1.0 - std::exp(-1.0 / (fs_ * kSurroundMatchSeconds));
        matchStep_ = 1.0 - std::exp(-1.0 / (fs_ * 0.05));
        computeNormalisation();
        applyYaw();
        reset();
    }

    // Headphones get virtual loudspeakers; speakers get the upmix folded back
    // down and thrown wide. A pair of real speakers is already in a real room in
    // front of the listener — rendering a binaural stage on top of that would be
    // a second, wrong room.
    void setHeadphone(bool headphone) {
        if (headphone == headphone_) return;
        headphone_ = headphone;
        applyYaw();
    }
    bool headphone() const { return headphone_; }

    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    bool active() const { return enabled_ || mix_ > 0.0; }

    // Which speakers to render: 0/1 keep the plain stereo pair, 2 and 3 upmix
    // the programme to 5.1 / 7.1 first. Switching is safe mid-playback — the
    // upmixer's steering gains start at zero, which renders exactly like the
    // stereo pair, and they open up over the next ~100 ms.
    void setLayout(int layout) {
        const int wanted = layout >= Upmixer::k71 ? Upmixer::k71
                         : (layout >= Upmixer::k51 ? Upmixer::k51 : Upmixer::kOff);
        if (wanted == layout_) return;
        layout_ = wanted;
        upmix_.setLayout(layout_);
        computeNormalisation();
        applyYaw();
    }
    int layout() const { return layout_; }

    // The upmix's two steering controls, 0-100 (see Upmixer).
    void setSurroundLevels(double centreWidth, double surroundDepth) {
        upmix_.setCentreWidth(centreWidth);
        upmix_.setSurroundDepth(surroundDepth);
    }

    // Head yaw in degrees, positive when the listener turns to the right.
    void setYawDegrees(double yaw) {
        yaw_ = std::isfinite(yaw) ? wrap180(yaw) : 0.0;
        applyYaw();
    }
    double yawDegrees() const { return yaw_; }

    void setHeadRadiusCm(double cm) {
        for (auto& speaker : speaker_) speaker.setHeadRadiusCm(cm);
    }

    void reset() {
        for (std::size_t i = 0; i < kMaxSpeakers; ++i) {
            farL_[i] = farTargetL_[i];
            farR_[i] = farTargetR_[i];
        }
        for (auto& speaker : speaker_) speaker.reset();
        for (auto& colour : colour_) colour.reset();
        for (std::size_t i = 0; i < kSurroundLine; ++i) {
            line_[i] = lineR_[i] = backLine_[i] = backLineR_[i] = 0.0;
        }
        backL_ = backR_ = 0.0;
        write_ = 0;
        lowL_ = lowR_ = 0.0;
        dryEnergy_ = wetEnergy_ = 0.0;
        matchGain_ = 1.0;
        upmix_.reset();
        mix_ = enabled_ ? 1.0 : 0.0;
    }

    inline void processFrame(float* frame, std::size_t channels) {
        if (channels < 2) return;
        const double target = enabled_ ? 1.0 : 0.0;
        if (mix_ < target)      mix_ = std::min(target, mix_ + rampStep_);
        else if (mix_ > target) mix_ = std::max(target, mix_ - rampStep_);
        if (mix_ <= 0.0 && target == 0.0) return;   // idle: untouched samples

        const double dryL = frame[0];
        const double dryR = frame[1];

        if (!headphone_) {
            // Speaker fold. At a widen of 1 this is the identity — centre plus
            // front plus surround is exactly the channel they were carved from —
            // so the only thing the listener hears is the widening itself.
            UpmixFrame up;
            upmix_.process(dryL, dryR, up);
            double wideL = 0.0, wideR = 0.0;
            envelop(up.sideL, up.sideR, up.backL, up.backR, wideL, wideR);
            double outL = up.centre + up.frontL + wideL;
            double outR = up.centre + up.frontR + wideR;
            matchLevel(dryL, dryR, outL, outR);
            frame[0] = static_cast<float>(dryL + (outL - dryL) * mix_);
            frame[1] = static_cast<float>(dryR + (outR - dryR) * mix_);
            return;
        }

        double feed[kMaxSpeakers];
        double surroundL = 0.0, surroundR = 0.0;
        const std::size_t count = fillFeed(dryL, dryR, feed, surroundL, surroundR);

        const bool upmixing = layout_ >= Upmixer::k51;
        double wetL = 0.0, wetR = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            double l = 0.0, r = 0.0;
            speaker_[i].process(upmixing ? colour_[i].process(feed[i]) : feed[i], l, r);
            // Follow the emphasis in rather than stepping it: the speaker's own
            // azimuth glides over about 12 ms, and a gain that jumped while it
            // glided would be audible on a fast head turn.
            farL_[i] += emphasisStep_ * (farTargetL_[i] - farL_[i]);
            farR_[i] += emphasisStep_ * (farTargetR_[i] - farR_[i]);
            wetL += l * farL_[i];
            wetR += r * farR_[i];
        }
        if (upmixing) {
            double envL = 0.0, envR = 0.0;
            envelop(surroundL, surroundR, backL_, backR_, envL, envR);
            wetL += envL;
            wetR += envR;
            // The match owns the level on this path, so it has to be the LAST
            // thing: a fixed bus gain applied after it would simply reappear as
            // an offset (it did — a measured +1.7 dB).
            matchLevel(dryL, dryR, wetL, wetR);
        } else {
            wetL *= kBusGain;
            wetR *= kBusGain;
        }
        frame[0] = static_cast<float>(dryL + (wetL - dryL) * mix_);
        frame[1] = static_cast<float>(dryR + (wetR - dryR) * mix_);
    }

private:
    // ITU-R BS.775, in the order fillFeed() builds: C, L, R, then the surrounds.
    static constexpr double kStereoAngles[2] = {-kBaseAngleDegrees, kBaseAngleDegrees};
    static constexpr double k51Angles[5] = {0.0, -kBaseAngleDegrees, kBaseAngleDegrees, -110.0, 110.0};
    static constexpr double k71Angles[7] = {0.0, -kBaseAngleDegrees, kBaseAngleDegrees,
                                            -90.0, 90.0, -135.0, 135.0};

    static std::size_t speakerCount(int layout) {
        return layout >= Upmixer::k71 ? 7u : (layout >= Upmixer::k51 ? 5u : 2u);
    }

    const double* layoutAngles() const {
        return layout_ >= Upmixer::k71 ? k71Angles
             : (layout_ >= Upmixer::k51 ? k51Angles : kStereoAngles);
    }

    // The stereo pair keeps its original, separately measured balance, so head
    // tracking on a plain stereo mix renders exactly as it did before.
    void computeNormalisation() {
        const double* angles = layoutAngles();
        const std::size_t count = speakerCount(layout_);
        for (auto& colour : colour_) colour.setFlat();
        if (layout_ < Upmixer::k51) return;
        for (std::size_t i = 0; i < count; ++i) colour_[i].setAngle(angles[i]);
    }

    void applyYaw() {
        // Turning the head right moves every speaker to the left of the head.
        const double* angles = layoutAngles();
        const std::size_t count = speakerCount(layout_);
        for (std::size_t i = 0; i < count; ++i) {
            const double azimuth = wrap180(angles[i] - yaw_);
            speaker_[i].setAzimuth(azimuth);
            // How far to the side this speaker now stands: 0 dead ahead or dead
            // behind, 1 beside the listener. A centred source is never touched,
            // so a mono programme stays exactly centred.
            const double lean = std::sin(azimuth * kPi / 180.0);
            farTargetL_[i] = lean > 0.0 ? std::pow(10.0, -kIldEmphasisDb * lean / 20.0) : 1.0;
            farTargetR_[i] = lean < 0.0 ? std::pow(10.0, kIldEmphasisDb * lean / 20.0) : 1.0;
        }
    }

    inline std::size_t fillFeed(double left, double right, double (&feed)[kMaxSpeakers],
                                double& surroundL, double& surroundR) {
        surroundL = surroundR = backL_ = backR_ = 0.0;
        if (layout_ < Upmixer::k51) {
            feed[0] = left;
            feed[1] = right;
            return 2;
        }
        UpmixFrame up;
        upmix_.process(left, right, up);
        feed[0] = up.centre;
        feed[1] = up.frontL;
        feed[2] = up.frontR;
        surroundL = up.sideL;
        surroundR = up.sideR;
        backL_ = up.backL;
        backR_ = up.backR;
        return 3;
    }

    // One scalar on both ears, tracking the level that came in. A scalar cannot
    // change how alike the two ears are, so every bit of the widening survives
    // and only the loudness moves.
    inline void matchLevel(double dryL, double dryR, double& wetL, double& wetR) {
        dryEnergy_ += matchA_ * (dryL * dryL + dryR * dryR - dryEnergy_);
        wetEnergy_ += matchA_ * (wetL * wetL + wetR * wetR - wetEnergy_);
        if (wetEnergy_ <= 1.0e-12) return;
        double gain = std::sqrt(dryEnergy_ / wetEnergy_);
        if (gain > 2.0) gain = 2.0;          // never turn a quiet passage up
        matchGain_ += matchStep_ * (gain - matchGain_);
        wetL *= matchGain_;
        wetR *= matchGain_;
    }

    // Late, dark, and weighted to the difference between the ears — the three
    // things that make sound arrive from around the listener rather than from a
    // point. See the constants for why this is not another virtual speaker.
    inline void envelop(double sideInL, double sideInR, double backInL, double backInR,
                        double& outL, double& outR) {
        line_[write_] = sideInL;
        lineR_[write_] = sideInR;
        backLine_[write_] = backInL;
        backLineR_[write_] = backInR;
        const std::size_t sideL = (write_ + kSurroundLine - delay_) % kSurroundLine;
        const std::size_t sideR = (write_ + kSurroundLine - delaySkew_) % kSurroundLine;
        const std::size_t backL = (write_ + kSurroundLine - backDelay_) % kSurroundLine;
        const std::size_t backR = (write_ + kSurroundLine - backSkew_) % kSurroundLine;
        const double takenL = line_[sideL] + backLine_[backL];
        const double takenR = lineR_[sideR] + backLineR_[backR];
        write_ = write_ + 1 < kSurroundLine ? write_ + 1 : 0;
        lowL_ += lowA_ * (takenL - lowL_);
        lowR_ += lowA_ * (takenR - lowR_);
        const double mid = (lowL_ + lowR_) * 0.5;
        const double difference = (lowL_ - lowR_) * 0.5;
        const double wideL = mid + difference * kSurroundSideBias;
        const double wideR = mid - difference * kSurroundSideBias;
        outL = wideL * kSurroundGain;
        outR = wideR * kSurroundGain;
    }

    double fs_ = 48000.0;
    double rampStep_ = 1.0;
    double yaw_ = 0.0;
    double mix_ = 0.0;
    bool enabled_ = false;
    bool headphone_ = true;
    int layout_ = Upmixer::kOff;
    SpeakerColour colour_[kMaxSpeakers]{};
    // 20 ms at 768 kHz is 15360 samples; 16384 (pow2) covers the top rate.
    static constexpr std::size_t kSurroundLine = 16384;
    double line_[kSurroundLine] = {0.0};
    double lineR_[kSurroundLine] = {0.0};
    double backLine_[kSurroundLine] = {0.0};
    double backLineR_[kSurroundLine] = {0.0};
    double backL_ = 0.0, backR_ = 0.0;
    std::size_t write_ = 0, delay_ = 624, delaySkew_ = 936, backDelay_ = 1104, backSkew_ = 1536;
    double lowA_ = 0.5, lowL_ = 0.0, lowR_ = 0.0;
    double matchA_ = 0.0002, matchStep_ = 0.0002;
    double dryEnergy_ = 0.0, wetEnergy_ = 0.0, matchGain_ = 1.0;
    double farTargetL_[kMaxSpeakers] = {1,1,1,1,1,1,1};
    double farTargetR_[kMaxSpeakers] = {1,1,1,1,1,1,1};
    double farL_[kMaxSpeakers] = {1,1,1,1,1,1,1};
    double farR_[kMaxSpeakers] = {1,1,1,1,1,1,1};
    double emphasisStep_ = 0.002;
    Upmixer upmix_{};
    VirtualSpeaker speaker_[kMaxSpeakers]{};
};

} // namespace roomcut

#endif // ROOMCUT_SURROUND_STAGE_HPP
