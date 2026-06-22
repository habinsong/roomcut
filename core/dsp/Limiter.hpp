/*
 * Limiter.hpp — look-ahead brickwall limiter (docs/04-dsp.md), the final, always
 * -on safety stage of the chain. It cannot be disabled (only its ceiling moves)
 * — this backs the "sound is safe" guarantee in DEVELOPMENT_PLAN.md priority #1.
 *
 * Design:
 *   - A look-ahead delay line (a few ms) lets gain ride DOWN before a peak
 *     reaches the output, so the ceiling is never exceeded (no overshoot).
 *   - Stereo-linked gain: one gain reduction is computed from the max peak
 *     across channels and applied to all, preserving the stereo image.
 *   - Smooth attack/release on the gain so reduction is click-free.
 *   - Reports current gain reduction (dB) for the UI clipping indicator.
 *
 * Real-time safe after prepare(): process() does no allocation/locks/logging.
 * Header-only, max channels fixed so the delay lines are fixed-size.
 */
#ifndef ROOMCUT_LIMITER_HPP
#define ROOMCUT_LIMITER_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace roomcut {

class Limiter {
public:
    static constexpr std::size_t kMaxChannels = 8;

    // fs: sample rate. lookaheadMs: delay window. ceilingDb: brickwall ceiling
    // (default -1 dB). releaseMs: how fast gain recovers after a peak passes.
    void prepare(double fs, double lookaheadMs = 2.0, double ceilingDb = -1.0,
                 double releaseMs = 100.0, std::size_t channels = 2) {
        fs_ = fs;
        channels_ = std::min(channels, kMaxChannels);
        lookahead_ = std::max<std::size_t>(1, (std::size_t)std::lround(lookaheadMs * 0.001 * fs));
        setCeilingDb(ceilingDb);

        // Attack must cover the look-ahead window: gain has to reach its target
        // within `lookahead_` samples so the peak is fully tamed on arrival.
        attackCoeff_ = std::exp(-1.0 / (double)lookahead_);
        releaseCoeff_ = std::exp(-1.0 / std::max(1.0, releaseMs * 0.001 * fs));

        bufLen_ = lookahead_ + 1;
        for (std::size_t c = 0; c < kMaxChannels; ++c) {
            delay_[c].assign(bufLen_, 0.0f);
        }
        // Monotonic-deque storage for the O(1) windowed max (see processFrame).
        // Capacity bufLen_+1: at most `bufLen_` live entries plus one mid-push.
        maxVal_.assign(bufLen_ + 1, 0.0f);
        maxIdx_.assign(bufLen_ + 1, 0);
        maxHead_ = maxTail_ = maxCount_ = 0;
        writePos_ = 0;
        pos_ = 0;
        gain_ = 1.0;
        maxGrDb_ = 0.0;
    }

    void setCeilingDb(double ceilingDb) {
        ceilingDb_ = ceilingDb;
        ceilingLin_ = std::pow(10.0, ceilingDb / 20.0);
    }

    double ceilingDb() const { return ceilingDb_; }

    // Current gain reduction in dB (>= 0). For the UI clipping indicator.
    double gainReductionDb() const {
        return gain_ < 1.0 ? -20.0 * std::log10(gain_) : 0.0;
    }

    void reset() {
        for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0f);
        std::fill(maxVal_.begin(), maxVal_.end(), 0.0f);
        std::fill(maxIdx_.begin(), maxIdx_.end(), 0);
        maxHead_ = maxTail_ = maxCount_ = 0;
        writePos_ = 0;
        pos_ = 0;
        gain_ = 1.0;
        maxGrDb_ = 0.0;
    }

    // Process one interleaved frame in place (channels_ samples at `frame`).
    inline void processFrame(float* frame) {
        // 1. Find this frame's peak across channels and push into the delay.
        const std::size_t readPos = (writePos_ + 1) % bufLen_; // oldest
        float framePeak = 0.0f;
        for (std::size_t c = 0; c < channels_; ++c) {
            delay_[c][writePos_] = frame[c];
            framePeak = std::max(framePeak, std::fabs(frame[c]));
        }

        // 2. Windowed max of |x| over the look-ahead, in O(1) amortized via a
        //    monotonic (decreasing) deque. Identical result to scanning the
        //    whole ring every sample, but without the per-sample O(lookahead)
        //    cost that dominated the engine at high sample rates.
        const std::size_t cap = maxVal_.size();
        while (maxCount_ > 0) {
            const std::size_t back = (maxTail_ + cap - 1) % cap;
            if (maxVal_[back] <= framePeak) { maxTail_ = back; --maxCount_; }
            else break;
        }
        maxVal_[maxTail_] = framePeak;
        maxIdx_[maxTail_] = pos_;
        maxTail_ = (maxTail_ + 1) % cap;
        ++maxCount_;
        // Drop entries that have slid out of the bufLen_-frame window.
        while (maxCount_ > 0 && maxIdx_[maxHead_] + bufLen_ <= pos_) {
            maxHead_ = (maxHead_ + 1) % cap;
            --maxCount_;
        }
        const float windowPeak = maxVal_[maxHead_];

        double targetGain = 1.0;
        if (windowPeak > ceilingLin_) {
            targetGain = ceilingLin_ / (double)windowPeak;
        }

        // 3. Smooth the gain. Attack (gain decreasing) is fast enough to be
        //    fully applied within the look-ahead; release is slow.
        if (targetGain < gain_) {
            gain_ = targetGain + (gain_ - targetGain) * attackCoeff_;
        } else {
            gain_ = targetGain + (gain_ - targetGain) * releaseCoeff_;
        }

        // 4. Emit the delayed samples scaled by the current gain. Hard-clamp to
        //    the ceiling as a final guarantee against any residual overshoot.
        for (std::size_t c = 0; c < channels_; ++c) {
            double y = (double)delay_[c][readPos] * gain_;
            if (y > ceilingLin_) y = ceilingLin_;
            else if (y < -ceilingLin_) y = -ceilingLin_;
            frame[c] = (float)y;
        }

        const double grDb = gainReductionDb();
        if (grDb > maxGrDb_) maxGrDb_ = grDb;

        writePos_ = (writePos_ + 1) % bufLen_;
        ++pos_;
    }

    // Convenience: process an interleaved buffer in place.
    void processInterleaved(float* buf, std::size_t frames) {
        for (std::size_t f = 0; f < frames; ++f) {
            processFrame(&buf[f * channels_]);
        }
    }

    double maxGainReductionDb() const { return maxGrDb_; }

private:
    double fs_ = 48000.0;
    std::size_t channels_ = 2;
    std::size_t lookahead_ = 96;

    double ceilingDb_ = -1.0;
    double ceilingLin_ = std::pow(10.0, -1.0 / 20.0);

    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double gain_ = 1.0;
    double maxGrDb_ = 0.0;

    std::array<std::vector<float>, kMaxChannels> delay_{};
    std::size_t bufLen_ = 1;       // look-ahead delay length (lookahead_ + 1)
    std::size_t writePos_ = 0;

    // Monotonic (decreasing) deque over the look-ahead window for an O(1)
    // windowed max. maxVal_/maxIdx_ are a ring of [maxHead_, maxTail_); each
    // entry's absolute frame index (pos_) gates expiry.
    std::vector<float>    maxVal_{};
    std::vector<uint64_t> maxIdx_{};
    std::size_t maxHead_ = 0, maxTail_ = 0, maxCount_ = 0;
    std::uint64_t pos_ = 0;        // absolute frame counter for window expiry
};

} // namespace roomcut

#endif // ROOMCUT_LIMITER_HPP
