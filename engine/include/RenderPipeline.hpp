#ifndef ROOMCUT_RENDER_PIPELINE_HPP
#define ROOMCUT_RENDER_PIPELINE_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "SincResampler.hpp"
#include "dsp/ComparisonProcessor.hpp"
#include "dsp/GainRamp.hpp"

namespace roomcut {

struct RenderMetrics {
    float peak = 0.0f;
    float limiterReductionDb = 0.0f;
    uint64_t inputFrames = 0;
    uint64_t shortfallFrames = 0;
    bool safeBypass = false;
};

// The engine's complete sample path, independent of Mach, CoreAudio and disk.
// prepare() runs with IO stopped. All other methods belong to the render thread.
class RenderPipeline {
public:
    using InputFn = SincResampler::InputFn;
    static constexpr uint32_t kBlockFrames = 1024;
    static constexpr uint32_t kChannels = 2;

    void prepare(double inputRate, double outputRate, const ChainParams& params) {
        prepare(inputRate, outputRate, ComparisonSettings{params, params, false, 0});
    }

    void prepare(double inputRate, double outputRate, const ComparisonSettings& settings) {
        resampler_.prepare(inputRate, outputRate, kChannels);
        const double scratchFrames = std::ceil(kBlockFrames * resampler_.ratio()) + 4;
        if (scratchFrames > UINT32_MAX) throw std::length_error("resampler scratch exceeds addressable frames");
        scratchFrames_ = static_cast<uint32_t>(scratchFrames);
        scratch_.assign((size_t)scratchFrames_ * kChannels, 0.0f);
        dsp_.prepare(outputRate, kChannels, settings);
        gain_.prepare(outputRate, gain_.current());
        fadeStep_ = 1.0f / (float)std::max(1.0, inputRate * 0.003);
        fadeGain_ = 1.0f;
        hold_.fill(0.0f);
    }

    void setParams(const ChainParams& params) { dsp_.setParams(params); }
    void setComparison(const ComparisonSettings& settings) { dsp_.setComparison(settings); }
    ComparisonMetrics comparisonMetrics() const { return dsp_.metrics(); }
    void setBypass(bool bypass) { dsp_.setBypass(bypass); }
    double ratio() const { return resampler_.ratio(); }
    double resamplerLatencySeconds() const { return resampler_.latencySeconds(); }
    // What the engine adds on purpose, end to end: rate conversion plus the
    // limiter's look-ahead. Ring occupancy and the device's own buffers are not
    // in here — they are not ours to report.
    double latencySeconds() const { return resampler_.latencySeconds() + dsp_.latencySeconds(); }

    RenderMetrics render(float* output, uint32_t frames, InputFn input,
                         void* inputContext, float outputGain) {
        input_ = input;
        inputContext_ = inputContext;
        metrics_ = {};
        gain_.setTarget(outputGain);
        // Chunking bounds scratch usage even when HAL asks for a large block.
        for (uint32_t at = 0; at < frames;) {
            const uint32_t count = std::min(kBlockFrames, frames - at);
            float* block = output + (size_t)at * kChannels;
            resampler_.produce(block, count, &readInput, this, scratch_.data(), scratchFrames_);
            if (gain_.isUnity()) {
                dsp_.processInterleaved(block, count);
            } else {
                for (uint32_t f = 0; f < count; ++f) gains_[f] = gain_.next();
                dsp_.processInterleaved(block, count, gains_.data());
            }
            for (uint32_t i = 0; i < count * kChannels; ++i) {
                metrics_.peak = std::max(metrics_.peak, std::fabs(block[i]));
            }
            at += count;
        }
        metrics_.limiterReductionDb = (float)dsp_.limiterGainReductionDb();
        metrics_.safeBypass = dsp_.safeBypassed();
        return metrics_;
    }

private:
    static uint32_t readInput(void* context, float* output, uint32_t frames, uint32_t channels) {
        auto& self = *static_cast<RenderPipeline*>(context);
        const uint32_t got = self.input_(self.inputContext_, output, frames, channels);
        self.metrics_.inputFrames += got;
        // Preserve the existing meter contract: a wholly stopped producer is
        // silence; a partial read is an active-stream shortfall.
        if (got > 0) self.metrics_.shortfallFrames += frames - got;
        if (got == frames && self.fadeGain_ == 1.0f) {
            // Normal playback needs no fade pass. DSP sanitizes its input before
            // filters; only the last frame must be retained for a later shortfall.
            if (got > 0) for (uint32_t c = 0; c < channels; ++c) {
                const float sample = output[(got - 1) * channels + c];
                self.hold_[c] = std::isfinite(sample) ? sample : 0.0f;
            }
            return frames;
        }
        for (uint32_t f = 0; f < frames; ++f) {
            if (f < got) {
                self.fadeGain_ = std::min(1.0f, self.fadeGain_ + self.fadeStep_);
                for (uint32_t c = 0; c < channels; ++c) {
                    const float sample = output[f * channels + c];
                    self.hold_[c] = std::isfinite(sample) ? sample : 0.0f;
                }
            } else {
                self.fadeGain_ = std::max(0.0f, self.fadeGain_ - self.fadeStep_);
            }
            // One gain per FRAME: stereo stays aligned, including partial reads.
            // Continue multiplying at zero so the held sample cannot reappear.
            for (uint32_t c = 0; c < channels; ++c) {
                output[f * channels + c] = self.hold_[c] * self.fadeGain_;
            }
        }
        return frames; // shortfalls have already been concealed
    }

    ComparisonProcessor dsp_;
    SincResampler resampler_;
    GainRamp gain_;
    std::vector<float> scratch_;
    std::array<float, kBlockFrames> gains_{};
    std::array<float, kChannels> hold_{};
    uint32_t scratchFrames_ = 0;
    float fadeStep_ = 1.0f;
    float fadeGain_ = 1.0f;
    InputFn input_ = nullptr;
    void* inputContext_ = nullptr;
    RenderMetrics metrics_;
};

} // namespace roomcut
#endif
