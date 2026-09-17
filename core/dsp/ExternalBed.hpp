/*
 * ExternalBed.hpp — sends SurroundStage's headphone bed through a BedRenderer.
 *
 * The stage works a frame at a time; a renderer such as AUSpatialMixer works a
 * block at a time. This bridges the two with a fixed delay of one block, plus
 * whatever the renderer's own output lags its input:
 *
 *   - Every frame entering the stage is delayed by latencyFrames(), whatever
 *     the stage then does with it, so switching between the built-in render
 *     and the external one never jumps in time. The latency is constant while
 *     a renderer is attached, which is what makes it reportable.
 *   - The undelayed frame is upmixed here (a second Upmixer, so the stage's own
 *     path is untouched) and queued; each full block goes to the renderer, and
 *     its output lines up exactly with the delayed frame.
 *   - The external output replaces the built-in bed over 20 ms, both ways.
 *
 * Measuring AUSpatialMixer is why the block exists at all: one render call per
 * frame cost 2.5 us, 53 % of a core at 192 kHz, and the A/B comparison drives
 * the chain a frame at a time.
 *
 * Real-time safe after prepare(): fixed buffers, no allocation, no locks.
 */
#ifndef ROOMCUT_EXTERNAL_BED_HPP
#define ROOMCUT_EXTERNAL_BED_HPP

#include <algorithm>
#include <array>
#include <cstddef>

#include "BedRenderer.hpp"
#include "Upmixer.hpp"

namespace roomcut {

class ExternalBed {
public:
    static constexpr std::size_t kMaxBlockFrames = 512;
    static constexpr std::size_t kMaxRendererLatencyFrames = 128;
    static constexpr std::size_t kChannels = 7;
    static constexpr double kFadeSeconds = 0.020;

    // Before prepare(). A renderer whose block or latency does not fit is not
    // attached.
    void attach(BedRenderer* renderer) { renderer_ = renderer; }
    bool attached() const { return renderer_ != nullptr; }
    std::size_t latencyFrames() const { return attached() ? delay_ : 0; }

    void prepare(double fs) {
        block_ = renderer_ ? renderer_->blockFrames() : 0;
        const std::size_t lag = renderer_ ? renderer_->latencyFrames() : 0;
        if (block_ == 0 || block_ > kMaxBlockFrames || lag > kMaxRendererLatencyFrames) {
            renderer_ = nullptr;
            block_ = 0;
        }
        delay_ = renderer_ ? block_ + lag : 0;
        upmix_.prepare(fs);
        gainStep_ = 1.0 / std::max(1.0, fs * kFadeSeconds);
        for (std::size_t c = 0; c < kChannels; ++c) channelPointers_[c] = input_[c].data();
    }

    void setLayout(int layout) { upmix_.setLayout(layout); }
    void setSurroundLevels(double centreWidth, double surroundDepth) {
        upmix_.setCentreWidth(centreWidth);
        upmix_.setSurroundDepth(surroundDepth);
    }

    // `wanted`: the stage is rendering an upmixed bed for headphones.
    void reset(int layout, bool wanted) {
        delayL_.fill(0.0);
        delayR_.fill(0.0);
        for (auto& channel : input_) channel.fill(0.0f);
        outputL_.fill(0.0f);
        outputR_.fill(0.0f);
        delayWrite_ = fill_ = 0;
        upmix_.reset();
        running_ = attached() && wanted && renderer_->canRender(layout);
        renderLayout_ = layout;
        gain_ = running_ ? 1.0 : 0.0;
        externalL_ = externalR_ = 0.0;
    }

    // Call first thing for every frame the stage sees. Replaces left/right with
    // the frame from latencyFrames() ago. `replace`, when given, is what the
    // renderer gets instead of this frame's upmix (a probe burst).
    inline void input(double& left, double& right, int layout, bool wanted, double headYawDegrees,
                      const UpmixFrame* replace = nullptr) {
        const double inL = left, inR = right;
        left = delayL_[delayWrite_];
        right = delayR_[delayWrite_];
        delayL_[delayWrite_] = inL;
        delayR_[delayWrite_] = inR;
        delayWrite_ = delayWrite_ + 1 < delay_ ? delayWrite_ + 1 : 0;

        const bool target = wanted && renderer_->canRender(layout);
        if (target) {
            if (!running_) startQueue();
            renderLayout_ = layout;
        }
        if (running_) {
            externalL_ = outputL_[fill_];
            externalR_ = outputR_[fill_];
            UpmixFrame up;
            upmix_.process(inL, inR, up);
            if (replace) up = *replace;
            const double channels[kChannels] = {up.centre, up.frontL, up.frontR, up.sideL, up.sideR, up.backL, up.backR};
            for (std::size_t c = 0; c < kChannels; ++c) input_[c][fill_] = static_cast<float>(channels[c]);
            if (++fill_ == block_) {
                renderer_->render(renderLayout_, channelPointers_, headYawDegrees, outputL_.data(), outputR_.data());
                fill_ = 0;
            }
        }
        if (target) gain_ = std::min(1.0, gain_ + gainStep_);
        else gain_ = std::max(0.0, gain_ - gainStep_);
        if (!target && gain_ <= 0.0) {
            running_ = false;
            externalL_ = externalR_ = 0.0;
        }
    }

    // In place of the built-in bed render of the current (delayed) frame.
    inline void substitute(double& wetL, double& wetR) const {
        if (gain_ <= 0.0) return;
        if (gain_ >= 1.0) {
            wetL = externalL_;
            wetR = externalR_;
            return;
        }
        wetL += (externalL_ - wetL) * gain_;
        wetR += (externalR_ - wetR) * gain_;
    }

    double externalGain() const { return gain_; }

private:
    // Anything left in the queue is from before the renderer stopped; start
    // from silence instead, which the fade-in covers.
    void startQueue() {
        for (auto& channel : input_) channel.fill(0.0f);
        outputL_.fill(0.0f);
        outputR_.fill(0.0f);
        fill_ = 0;
        running_ = true;
    }

    BedRenderer* renderer_ = nullptr;
    std::size_t block_ = 0, delay_ = 0, delayWrite_ = 0, fill_ = 0;
    bool running_ = false;
    int renderLayout_ = Upmixer::kOff;
    double gain_ = 0.0, gainStep_ = 1.0;
    double externalL_ = 0.0, externalR_ = 0.0;
    Upmixer upmix_{};
    std::array<double, kMaxBlockFrames + kMaxRendererLatencyFrames> delayL_{}, delayR_{};
    std::array<std::array<float, kMaxBlockFrames>, kChannels> input_{};
    std::array<float, kMaxBlockFrames> outputL_{}, outputR_{};
    const float* channelPointers_[kChannels] = {};
};

} // namespace roomcut

#endif // ROOMCUT_EXTERNAL_BED_HPP
