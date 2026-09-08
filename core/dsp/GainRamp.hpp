#ifndef ROOMCUT_GAIN_RAMP_HPP
#define ROOMCUT_GAIN_RAMP_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace roomcut {

// Frame-based gain smoothing. The duration is independent of callback size.
class GainRamp {
public:
    void prepare(double sampleRate, float initial = 1.0f, double milliseconds = 10.0) {
        length_ = std::max<uint32_t>(1, (uint32_t)std::lround(sampleRate * milliseconds * 0.001));
        current_ = target_ = initial;
        remaining_ = 0;
        step_ = 0.0f;
    }

    void setTarget(float target) {
        if (!std::isfinite(target)) target = 0.0f;
        target = std::clamp(target, 0.0f, 2.0f);
        if (target == target_) return;
        target_ = target;
        remaining_ = length_;
        step_ = (target_ - current_) / (float)length_;
    }

    float next() {
        if (remaining_ > 0) {
            if (--remaining_ == 0) current_ = target_;
            else current_ += step_;
        }
        return current_;
    }

    float current() const { return current_; }
    bool isUnity() const { return remaining_ == 0 && current_ == 1.0f; }

    void apply(float* samples, uint32_t frames, uint32_t channels) {
        for (uint32_t f = 0; f < frames; ++f) {
            const float gain = next();
            for (uint32_t c = 0; c < channels; ++c) samples[f * channels + c] *= gain;
        }
    }

private:
    float current_ = 1.0f;
    float target_ = 1.0f;
    float step_ = 0.0f;
    uint32_t length_ = 480;
    uint32_t remaining_ = 0;
};

} // namespace roomcut
#endif
