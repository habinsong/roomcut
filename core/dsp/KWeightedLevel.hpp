#ifndef ROOMCUT_K_WEIGHTED_LEVEL_HPP
#define ROOMCUT_K_WEIGHTED_LEVEL_HPP

#include "Biquad.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace roomcut {

// Stereo K-weighted power over the most recent four 100ms blocks. This is a
// comparison measurement, not an integrated programme-loudness meter.
class KWeightedLevel {
public:
    void prepare(double sampleRate, std::size_t channels) {
        channels_ = channels;
        blockFrames_ = std::max<uint32_t>(1, std::lround(sampleRate * 0.1));
        // ITU-R BS.1770-5, Tables 1 and 2 (48kHz). Inverse/forward bilinear
        // transforms preserve the weighting response at other sample rates.
        configure(shelf_, sampleRate, 1.53512485958697, -2.69169618940638,
                  1.19839281085285, -1.69065929318241, 0.73248077421585);
        configure(highpass_, sampleRate, 1, -2, 1, -1.99004745483398, 0.99007225036621);
        reset();
    }

    void reset() {
        shelf_.reset(); highpass_.reset();
        blocks_.fill(0);
        sum_ = power_ = 0;
        frames_ = index_ = filled_ = 0;
    }

    bool processFrame(const float* samples) {
        for (std::size_t c = 0; c < channels_; ++c) {
            const float x = std::isfinite(samples[c]) ? samples[c] : 0;
            const double y = highpass_.processSample(shelf_.processSample(x, c), c);
            sum_ += y * y;
        }
        if (++frames_ < blockFrames_) return false;
        blocks_[index_] = sum_;
        index_ = (index_ + 1) % blocks_.size();
        filled_ = std::min<std::size_t>(blocks_.size(), filled_ + 1);
        power_ = 0;
        for (double value : blocks_) power_ += value;
        power_ /= blockFrames_ * filled_;
        frames_ = 0; sum_ = 0;
        return true;
    }

    bool ready() const { return filled_ == blocks_.size(); }
    double power() const { return power_; }

private:
    static void configure(Biquad& filter, double fs, double b0, double b1, double b2,
                          double a1, double a2) {
        const double r = fs / 48000.0;
        const double n0 = b0 + b1 + b2, n1 = 2 * (b0 - b2) * r, n2 = (b0 - b1 + b2) * r * r;
        const double d0 = 1 + a1 + a2, d1 = 2 * (1 - a2) * r, d2 = (1 - a1 + a2) * r * r;
        const double denominator = d0 + d1 + d2;
        filter.setCoefficients((n0 + n1 + n2) / denominator, 2 * (n0 - n2) / denominator,
                               (n0 - n1 + n2) / denominator, 2 * (d0 - d2) / denominator,
                               (d0 - d1 + d2) / denominator);
    }

    Biquad shelf_, highpass_;
    std::array<double, 4> blocks_{};
    double sum_ = 0, power_ = 0;
    uint32_t frames_ = 0, blockFrames_ = 4800;
    std::size_t channels_ = 2, index_ = 0, filled_ = 0;
};
} // namespace roomcut
#endif
