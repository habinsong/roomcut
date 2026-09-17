/*
 * DSPChain.hpp — live processing, preset transitions, bypass and final limiting.
 * DSPPath owns the preamp → HPF → graphic/parametric EQ → spatial → compressor
 * → output gain stages. A parameter change crossfades two independent paths;
 * bypass uses a separate dry/wet ramp. The shared final limiter stays active.
 *
 * After prepare(), processing and parameter changes allocate no memory, take
 * no locks and perform no I/O. All methods belong to the render thread.
 */
#ifndef ROOMCUT_DSP_CHAIN_HPP
#define ROOMCUT_DSP_CHAIN_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "DSPPath.hpp"
#include "Limiter.hpp"
#include "SurroundStage.hpp"
#include "RoomSim.hpp"

namespace roomcut {

class DSPChain {
public:
    void prepare(double fs, std::size_t channels = 2, double crossfadeMs = 15.0) {
        fs_ = fs;
        channels_ = std::clamp(channels, std::size_t{1}, Limiter::kMaxChannels);
        activePath_ = 0;
        paths_[activePath_].prepare(fs, channels_);
        // A clip-only safety net at digital full scale leaves sub-0 dBFS masters
        // transparent. One limiter after both mixes preserves its look-ahead.
        limiter_.prepare(fs, 2.0, kClipCeilingDb, params_.limiterReleaseMs, channels_);
        // The virtual room sits beside the limiter rather than inside a path:
        // its tail must survive a preset change instead of being crossfaded,
        // and one instance keeps its delay lines out of the path copy that
        // beginTransition() performs on the render thread.
        room_.prepare(fs);
        head_.prepare(fs);
        applyRoom();
        applyHeadPose();
        crossfadeSamples_ = std::max<std::size_t>(1, std::lround(crossfadeMs * 0.001 * fs));
        mixStep_ = 1.0 / static_cast<double>(crossfadeSamples_);
        levelEnergyA_ = 1.0 - std::exp(-1.0 / (fs * kLevelMatchSeconds));
        levelGainA_ = 1.0 - std::exp(-1.0 / (fs * kLevelGainSeconds));
        reset();
    }

    // A headphone bed renderer outside the core (see ExternalBed); before prepare().
    void attachBedRenderer(BedRenderer* renderer) { head_.attachBedRenderer(renderer); }

    void setParams(const ChainParams& params) {
        if (params == params_) return;
        params_ = params;
        limiter_.setReleaseMs(params.limiterReleaseMs);
        // Release belongs to the shared limiter, not either processing path.
        transitionParams_.limiterReleaseMs = params.limiterReleaseMs;
        transitionParams_.roomType = params.roomType;
        transitionParams_.roomAmount = params.roomAmount;
        // The upmix, like the room, lives beside the paths rather than inside
        // one, so changing it must not drag the whole chain through a crossfade.
        transitionParams_.surroundType = params.surroundType;
        transitionParams_.centerWidth = params.centerWidth;
        transitionParams_.surroundDepth = params.surroundDepth;
        transitionParams_.bedRenderer = params.bedRenderer;
        applyRoom();
        applyHeadPose();
        if (!transitioning_ && (pathsDirty_ || !(params_ == transitionParams_))) beginTransition();
        // While fading, retain only the latest request. Never replace a path
        // whose output is audible or restart a fade at a different mix weight.
    }

    const ChainParams& params() const { return params_; }

    void setBypass(bool bypass) {
        bypass_ = bypass;
        mixTarget_ = bypass ? 0.0 : 1.0;
        applyRoom();
        applyHeadPose();
    }

    // Live head orientation, in degrees, positive when the listener turns right.
    // This is not a preset value: it arrives many times a second from the
    // headphones' own sensors, so it never goes through setParams() (which
    // would crossfade the whole chain on every update).
    void setHeadPose(double yawDegrees, bool active) {
        const bool wasActive = headActive_;
        headYawDeg_ = yawDegrees;
        headActive_ = active;
        applyHeadPose();
        // The angle itself arrives fifty times a second and must never touch the
        // tonal paths. The tracker APPEARING or GOING AWAY is different: it
        // changes whether the crossfeed should be rendering at all, and that is
        // a path change like any other, so it crossfades. This happens a handful
        // of times in a session, not per block.
        if (wasActive != active && params_.crossfeed != 0.0) {
            pathsDirty_ = true;
            if (!transitioning_) beginTransition();
        }
    }

    // Listening tests: see SurroundStage::startProbe.
    void startChannelProbe(int channel, double seconds, double levelDb) { head_.startProbe(channel, seconds, levelDb); }

    bool headTracking() const { return head_.enabled(); }
    // 0..1: how much of the headphone bed the attached renderer renders now.
    double externalBedGain() const { return head_.externalBedGain(); }

    bool bypassed() const { return bypass_; }

    void reset() {
        paths_[activePath_].setParams(pathParams());
        paths_[activePath_].reset();
        limiter_.reset();
        room_.reset();
        head_.reset();
        transitionParams_ = params_;
        transitioning_ = false;
        transitionFrame_ = 0;
        pathsDirty_ = false;
        safeBypass_ = false;
        mix_ = bypass_ ? 0.0 : 1.0;
        mixTarget_ = mix_;
        levelIn_ = levelOut_ = 0.0;
        levelGain_ = levelTarget_ = 1.0;
        levelTick_ = 0;
    }

    double limiterGainReductionDb() const { return limiter_.gainReductionDb(); }

    // Delay the chain adds on purpose. The limiter's look-ahead and, while a bed
    // renderer is attached, its block are bulk delays of the whole signal; the
    // filters are IIR, so their group delay varies with frequency and is not
    // part of a single number.
    double latencySeconds() const {
        return fs_ > 0 ? static_cast<double>(limiter_.lookaheadFrames() + head_.latencyFrames()) / fs_ : 0.0;
    }
    bool safeBypassed() const { return safeBypass_; }

    void processInterleaved(float* buf, std::size_t frames, const float* finalGains = nullptr) {
        for (std::size_t f = 0; f < frames; ++f) {
            float* frame = &buf[f * channels_];
            if (mix_ < mixTarget_)      mix_ = std::min(mixTarget_, mix_ + mixStep_);
            else if (mix_ > mixTarget_) mix_ = std::max(mixTarget_, mix_ - mixStep_);

            float dry[Limiter::kMaxChannels];
            for (std::size_t c = 0; c < channels_; ++c) {
                dry[c] = std::isfinite(frame[c]) ? frame[c] : 0.0f;
                frame[c] = dry[c];
            }
            if (!safeBypass_) {
                paths_[activePath_].processFrame(frame);
                if (transitioning_) {
                    float incoming[Limiter::kMaxChannels];
                    std::copy_n(dry, channels_, incoming);
                    paths_[1 - activePath_].processFrame(incoming);
                    // The first sample is entirely the old path. A linear mix
                    // preserves unity when the paths carry the same signal.
                    const double amount = transitionFrame_ * mixStep_;
                    for (std::size_t c = 0; c < channels_; ++c)
                        frame[c] = static_cast<float>(frame[c] * (1.0 - amount) + incoming[c] * amount);
                }
                if (mix_ != 1.0) {
                    for (std::size_t c = 0; c < channels_; ++c)
                        frame[c] = static_cast<float>(frame[c] * mix_ + dry[c] * (1.0 - mix_));
                }
                // Head tracking first — it decides where the speakers stand —
                // then the room those speakers stand in. Both run on the blended
                // result and before the limiter, so their extra energy still
                // meets the brickwall.
                head_.processFrame(frame, channels_);
                room_.processFrame(frame, channels_);
                matchSurroundLevel(frame);
                for (std::size_t c = 0; c < channels_; ++c) {
                    if (!std::isfinite(frame[c])) safeBypass_ = true;
                }
            }

            // Both the bypass and preset blends pass through the same final
            // gain and limiter, including the latched non-finite fallback.
            for (std::size_t c = 0; c < channels_; ++c) {
                if (safeBypass_) frame[c] = dry[c];
                if (finalGains) {
                    frame[c] *= finalGains[f];
                    if (!std::isfinite(frame[c])) frame[c] = 0.0f;
                }
            }
            limiter_.processFrame(frame);

            if (transitioning_ && ++transitionFrame_ >= crossfadeSamples_) {
                activePath_ = 1 - activePath_;
                transitioning_ = false;
                if (pathsDirty_ || !(params_ == transitionParams_)) beginTransition();
            }
        }
    }

private:
    // Both outputs get a room, but not the same one: headphones need the whole
    // space rebuilt, speakers already sit in a real room and get only its late
    // field (see RoomSim). Bypass silences it like everything else.
    void applyRoom() {
        const int mode = static_cast<int>(std::lround(params_.spatialMode));
        const bool headphone = (mode == 1 || mode == 2);
        int type = bypass_ ? 0 : static_cast<int>(std::lround(params_.roomType));
        // A virtual loudspeaker with no reflections stays inside the listener's
        // head. The literature is blunt about it — virtualisers built on the
        // direct-path HRTF alone do not externalise, because the head gives no
        // distance cue past about a metre; what carries distance and room is the
        // reflected sound. So an upmix on headphones always gets a room, and
        // the Room control chooses WHICH one rather than whether there is one.
        // Without this, picking 5.1 changed the sound so little that it read as
        // nothing happening at all.
        // Speakers are already in a room. Adding a second one at the same level
        // as the headphone profile stacks reverb on reverb, so the slider keeps
        // its full 0-100 travel and the amount it asks for is halved on the way
        // in — same control, half the reverb, which is what a real room leaves
        // space for.
        double amount = headphone ? params_.roomAmount : params_.roomAmount * 0.5;
        // Every surround choice carries its own room while the Room control is
        // Off, so choosing one changes the space as well as the layout (see
        // surroundRoom); a room the listener picked always wins.
        bool roomProfileHeadphone = headphone;
        if (!bypass_ && type == 0) {
            const SurroundRoom room = surroundRoom(mode, static_cast<int>(std::lround(params_.surroundType)));
            type = room.type;
            amount = room.amount;
            roomProfileHeadphone = room.withReflections;
        }
        room_.setParams(type, amount, roomProfileHeadphone);
    }

    struct SurroundRoom { int type; double amount; bool withReflections; };
    static SurroundRoom surroundRoom(int mode, int surround) {
        const bool headphone = (mode == 1 || mode == 2);
        if (surround >= Upmixer::k71) return headphone ? SurroundRoom{RoomSim::kHall, 45.0, true} : SurroundRoom{RoomSim::kHall, 60.0, false};
        if (surround >= Upmixer::k51) return headphone ? SurroundRoom{RoomSim::kLiving, 50.0, true} : SurroundRoom{RoomSim::kHall, 60.0, false};
        if (mode == 2) return {RoomSim::kLiving, 70.0, false};
        if (mode == 3) return {RoomSim::kLiving, 70.0, false};
        return {RoomSim::kOff, 0.0, false};
    }

    // Which of the spatial features is actually rendering, and how.
    //
    // Three of them overlap if left alone — head tracking, the upmix and the
    // crossfeed/XTC model all decide where a source appears — so the chain makes
    // the call rather than trusting whoever set the parameters. A preset, a
    // restored state file or a switch between outputs could otherwise leave two
    // of them running at once, which measurably doubles up: head tracking with
    // crossfeed still at 50 narrowed a 0.58-correlated mix to 0.93 instead of
    // 0.86, and put 3.5 dB more of it in the middle.
    //
    // The rules, in one place:
    //   - Head tracking runs on headphones only. Speakers do not move with you.
    //   - The upmix runs on both, differently: virtual loudspeakers on
    //     headphones, a folded and widened stereo pair on speakers.
    //   - While either of those is rendering on headphones, the crossfeed steps
    //     aside — it is a third, fixed-head version of the same job.
    //   - The upmix also retires the older ambience surround, which spreads the
    //     same material a cruder way.
    void applyHeadPose() {
        const int mode = static_cast<int>(std::lround(params_.spatialMode));
        const bool headphone = (mode == 1 || mode == 2);
        const int surround = static_cast<int>(std::lround(params_.surroundType));
        const bool upmixing = !bypass_ && surround >= Upmixer::k51;
        const bool tracking = headActive_ && headphone && !bypass_;
        const bool ambience = !bypass_ && !upmixing && (mode == 2 || mode == 3);
        head_.setHeadphone(headphone);
        head_.setLayout(upmixing ? surround : Upmixer::kOff);
        head_.setSurroundLevels(params_.centerWidth, params_.surroundDepth);
        head_.setAmbience(ambience);
        head_.setVirtualFront(tracking);
        head_.setPreferExternalBed(std::lround(params_.bedRenderer) != 1);
        head_.setEnabled(tracking || upmixing || ambience);
        levelMatching_ = upmixing || ambience;
        levelSpeaker_ = !headphone;
        head_.setYawDegrees(tracking ? headYawDeg_ : 0.0);
    }

    // What the tonal paths get: the stored parameters with whatever the stage
    // has taken over removed, so the two can never both render it.
    ChainParams pathParams() const {
        ChainParams p = params_;
        const int mode = static_cast<int>(std::lround(p.spatialMode));
        const bool headphone = (mode == 1 || mode == 2);
        const int surround = static_cast<int>(std::lround(p.surroundType));
        const bool upmixing = surround >= Upmixer::k51;
        if (upmixing || mode == 2 || mode == 3) {
            // The surround stage renders both the upmix and the Ambience
            // surround; the tonal path's own older ambience field stays out.
            p.spatialMode = headphone ? 1.0 : 0.0;
        }
        if (headphone && (upmixing || headActive_)) {
            // Crossfeed builds a fixed virtual stage; the stage is already
            // building one, with real angles.
            p.crossfeed = 0.0;
        }
        return p;
    }

    void beginTransition() {
        paths_[1 - activePath_] = paths_[activePath_];
        paths_[1 - activePath_].setParams(pathParams());
        pathsDirty_ = false;
        transitionParams_ = params_;
        transitionFrame_ = 0;
        transitioning_ = true;
    }

    // A surround choice adds a room on top of the stage, and a room adds
    // energy the stage cannot see: measured +0.3 to +1.2 dB on the same
    // programme against Off, more on transients than on steady material. So
    // while one is active, what leaves the room is held to what entered the
    // stage — the same slow follow the stage itself uses, one scalar on both
    // outputs, weighted like the stage on speakers (a single built-in speaker
    // hears the sum).
    static constexpr double kLevelMatchSeconds = 0.25;
    static constexpr double kLevelGainSeconds = 0.05;
    inline double spatialEnergy(double l, double r) const {
        return levelSpeaker_ ? 0.5 * (l * l + r * r) + 0.25 * (l + r) * (l + r) : l * l + r * r;
    }
    inline void matchSurroundLevel(float* frame) {
        if (!levelMatching_ && levelGain_ == 1.0) return;
        if (channels_ < 2) return;
        // The stage's input at the time of this output frame: an external bed
        // delays everything by a block, and a comparison across that block would
        // follow the programme a block late.
        levelIn_ += levelEnergyA_ * (spatialEnergy(head_.alignedDryLeft(), head_.alignedDryRight()) - levelIn_);
        levelOut_ += levelEnergyA_ * (spatialEnergy(frame[0], frame[1]) - levelOut_);
        // The target moves on a 0.25 s follow; a square root every 16 frames
        // (0.33 ms at 48 kHz) is as good as one per frame at a fraction of the cost.
        if ((levelTick_++ & 15u) == 0u) {
            levelTarget_ = 1.0;
            if (levelMatching_ && levelOut_ > 1.0e-12) levelTarget_ = std::clamp(std::sqrt(levelIn_ / levelOut_), 0.25, 2.0);
        }
        levelGain_ += levelGainA_ * (levelTarget_ - levelGain_);
        if (!levelMatching_ && std::fabs(levelGain_ - 1.0) < 1.0e-6) levelGain_ = 1.0;
        for (std::size_t c = 0; c < channels_; ++c) frame[c] = static_cast<float>(frame[c] * levelGain_);
    }

    static constexpr double kClipCeilingDb = 0.0;
    std::size_t channels_ = 2;
    double fs_ = 0.0;
    ChainParams params_{};
    ChainParams transitionParams_{};
    std::array<DSPPath, 2> paths_{};
    std::size_t activePath_ = 0;
    std::size_t transitionFrame_ = 0;
    bool transitioning_ = false;
    Limiter limiter_{};
    RoomSim room_{};
    SurroundStage head_{};
    double headYawDeg_ = 0.0;
    bool headActive_ = false;
    // Set when something outside the parameter set changed what the tonal paths
    // should be doing (so far: the tracker appearing or going away). Consumed by
    // the next crossfade, so it cannot be lost while one is already running.
    bool pathsDirty_ = false;
    bool bypass_ = false;
    bool safeBypass_ = false;
    double mix_ = 1.0;
    double mixTarget_ = 1.0;
    double mixStep_ = 1.0;
    bool levelMatching_ = false, levelSpeaker_ = false;
    double levelIn_ = 0.0, levelOut_ = 0.0, levelGain_ = 1.0, levelTarget_ = 1.0;
    unsigned levelTick_ = 0;
    double levelEnergyA_ = 0.0001, levelGainA_ = 0.0004;
    std::size_t crossfadeSamples_ = 1;
};

} // namespace roomcut
#endif
