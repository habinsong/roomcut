#ifndef ROOMCUT_K_WEIGHTED_LEVEL_HPP
#define ROOMCUT_K_WEIGHTED_LEVEL_HPP

#include "Biquad.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace roomcut {

// Stereo K-weighted power over the most recent `windowBlocks` 100ms blocks
// (four by default, up to kMaxBlocks). It is ready once four blocks are in and
// averages whatever part of the window is filled until then. This is a
// comparison measurement, not an integrated programme-loudness meter.
class KWeightedLevel {
public:
    static constexpr std::size_t kReadyBlocks = 4;
    static constexpr std::size_t kMaxBlocks = 30;

    void prepare(double sampleRate, std::size_t channels, std::size_t windowBlocks = kReadyBlocks) {
        channels_ = channels;
        window_ = std::clamp<std::size_t>(windowBlocks, 1, kMaxBlocks);
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
        index_ = (index_ + 1) % window_;
        filled_ = std::min(window_, filled_ + 1);
        power_ = 0;
        for (std::size_t b = 0; b < window_; ++b) power_ += blocks_[b];
        power_ /= blockFrames_ * filled_;
        frames_ = 0; sum_ = 0;
        return true;
    }

    bool ready() const { return filled_ >= std::min(kReadyBlocks, window_); }
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
    std::array<double, kMaxBlocks> blocks_{};
    std::size_t window_ = kReadyBlocks;
    double sum_ = 0, power_ = 0;
    uint32_t frames_ = 0, blockFrames_ = 4800;
    std::size_t channels_ = 2, index_ = 0, filled_ = 0;
};
} // namespace roomcut
#endif
