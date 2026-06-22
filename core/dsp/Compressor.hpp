/*
 * Compressor.hpp — light, single-knob downward compressor (docs/04-dsp.md).
 * Used by the Dialogue mode (Phase 7) to even out speech levels after the tone
 * shaping and spatial stage, before the always-on limiter.
 *
 * Design:
 *   - Feed-forward peak detector, stereo-LINKED: one gain reduction is computed
 *     from the loudest channel and applied to all, preserving the stereo image
 *     (same rule as the Limiter, so the two never fight the picture).
 *   - One "amount" knob (0..100) derives threshold / ratio / makeup. Fixed,
 *     dialogue-friendly attack/release. amount == 0 → exact bypass.
 *   - Gain reduction is smoothed in dB (attack when clamping down, release when
 *     letting go) so it is click-free.
 *
 * Real-time safe after prepare(): processFrame() does no allocation/locks/
 * logging. Header-only so it unit-tests off the audio thread.
 */
#ifndef ROOMCUT_COMPRESSOR_HPP
#define ROOMCUT_COMPRESSOR_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

class Compressor {
public:
    static constexpr std::size_t kMaxChannels = 8;

    void prepare(double fs, std::size_t channels = 2) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        channels_ = std::min(channels, kMaxChannels);
        // Dialogue-friendly, fixed: fast enough to catch syllables, slow enough
        // not to pump on speech.
        attackCoeff_  = 1.0 - std::exp(-1.0 / std::max(1.0, 0.010 * fs_)); // ~10 ms
        releaseCoeff_ = 1.0 - std::exp(-1.0 / std::max(1.0, 0.140 * fs_)); // ~140 ms
        reset();
    }

    // amount: 0..100. 0 = bypass. Higher = lower threshold, higher ratio, more
    // makeup — but kept gentle (≈2:1 max) since this is a "light" stage.
    void setParams(double amount) {
        amount_ = std::max(0.0, std::min(100.0, amount));
        active_ = amount_ > 0.0;
        thresholdDb_ = -0.20 * amount_;          // amount 100 → −20 dB
        ratio_       = 1.0 + 0.015 * amount_;    // amount 100 → 2.5 : 1
        makeupDb_    = 0.030 * amount_;          // amount 100 → +3 dB (partial)
    }

    void reset() {
        grDb_ = 0.0;
    }

    // Current gain reduction in dB (>= 0), for diagnostics/metering parity.
    double gainReductionDb() const { return grDb_; }

    // Process one interleaved frame (channels_ samples) in place.
    inline void processFrame(float* frame) {
        if (!active_) return;

        double peak = 0.0;
        for (std::size_t c = 0; c < channels_; ++c) {
            peak = std::max(peak, (double)std::fabs(frame[c]));
        }

        const double levelDb = 20.0 * std::log10(std::max(peak, 1e-9));
        const double overDb = levelDb - thresholdDb_;
        const double targetGrDb = overDb > 0.0 ? overDb * (1.0 - 1.0 / ratio_) : 0.0;

        // Smooth the gain reduction: attack while clamping further down,
        // release while letting go.
        const double coeff = targetGrDb > grDb_ ? attackCoeff_ : releaseCoeff_;
        grDb_ += coeff * (targetGrDb - grDb_);

        const double gainLin = std::pow(10.0, (makeupDb_ - grDb_) / 20.0);
        for (std::size_t c = 0; c < channels_; ++c) {
            frame[c] = (float)(frame[c] * gainLin);
        }
    }

private:
    double fs_ = 48000.0;
    std::size_t channels_ = 2;

    bool   active_ = false;
    double amount_ = 0.0;
    double thresholdDb_ = 0.0;
    double ratio_ = 1.0;
    double makeupDb_ = 0.0;

    double attackCoeff_ = 0.0;
    double releaseCoeff_ = 0.0;
    double grDb_ = 0.0;
};

} // namespace roomcut

#endif // ROOMCUT_COMPRESSOR_HPP
