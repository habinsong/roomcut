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
        crossfadeSamples_ = std::max<std::size_t>(1, std::lround(crossfadeMs * 0.001 * fs));
        mixStep_ = 1.0 / static_cast<double>(crossfadeSamples_);
        reset();
    }

    void setParams(const ChainParams& params) {
        if (params == params_) return;
        params_ = params;
        limiter_.setReleaseMs(params.limiterReleaseMs);
        // Release belongs to the shared limiter, not either processing path.
        transitionParams_.limiterReleaseMs = params.limiterReleaseMs;
        if (!transitioning_ && !(params_ == transitionParams_)) beginTransition();
        // While fading, retain only the latest request. Never replace a path
        // whose output is audible or restart a fade at a different mix weight.
    }

    const ChainParams& params() const { return params_; }

    void setBypass(bool bypass) {
        bypass_ = bypass;
        mixTarget_ = bypass ? 0.0 : 1.0;
    }

    bool bypassed() const { return bypass_; }

    void reset() {
        paths_[activePath_].setParams(params_);
        paths_[activePath_].reset();
        limiter_.reset();
        transitionParams_ = params_;
        transitioning_ = false;
        transitionFrame_ = 0;
        safeBypass_ = false;
        mix_ = bypass_ ? 0.0 : 1.0;
        mixTarget_ = mix_;
    }

    double limiterGainReductionDb() const { return limiter_.gainReductionDb(); }

    // Delay the chain adds on purpose. Only the limiter's look-ahead is a bulk
    // delay of the whole signal; the filters are IIR, so their group delay varies
    // with frequency and is not part of a single number.
    double latencySeconds() const {
        return fs_ > 0 ? static_cast<double>(limiter_.lookaheadFrames()) / fs_ : 0.0;
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
                if (!(params_ == transitionParams_)) beginTransition();
            }
        }
    }

private:
    void beginTransition() {
        paths_[1 - activePath_] = paths_[activePath_];
        paths_[1 - activePath_].setParams(params_);
        transitionParams_ = params_;
        transitionFrame_ = 0;
        transitioning_ = true;
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
    bool bypass_ = false;
    bool safeBypass_ = false;
    double mix_ = 1.0;
    double mixTarget_ = 1.0;
    double mixStep_ = 1.0;
    std::size_t crossfadeSamples_ = 1;
};

} // namespace roomcut
#endif
