/*
 * CubicResampler.hpp — 4-point Catmull-Rom (cubic Hermite) interleaved
 * resampler for the engine's render path.
 *
 * Replaces LinearResampler (2-point linear) for higher quality when the ring SR
 * differs from the device SR. The Catmull-Rom kernel has much better stopband
 * rejection than linear (~-18 dB vs ~-6 dB first sidelobe) and preserves high
 * frequencies without the harsh rolloff of a triangular kernel.
 *
 * The pull interface is identical to LinearResampler so swapping is mechanical.
 * ratio == 1 still takes a bit-exact fast path (no interpolation).
 *
 * Streaming correctness: the resampler holds the FOUR most recent input frames
 * (h[0..3]) and a fractional position pos_ between h[1] and h[2]. Input is
 * pulled only as pos_ crosses whole frames. inputFramesFor() is deterministic
 * from pos_+ratio (same contract as the linear version).
 *
 * Reference: Catmull-Rom uniform spline, tau = 0.5 (standard).
 *   q(t) = 0.5 * ((2*y1) + (-y0+y2)*t + (2*y0-5*y1+4*y2-y3)*t^2
 *                  + (-y0+3*y1-3*y2+y3)*t^3)
 * where y0..y3 are four consecutive samples and t ∈ [0,1).
 */
#ifndef ROOMCUT_CUBIC_RESAMPLER_HPP
#define ROOMCUT_CUBIC_RESAMPLER_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace roomcut {

class CubicResampler {
public:
    using InputFn = uint32_t (*)(void* ctx, float* dst, uint32_t inFrames, uint32_t channels);

    static constexpr uint32_t kMaxChannels = 8;
    // History depth: Catmull-Rom needs 4 points (y0..y3).
    static constexpr uint32_t kHistory = 4;

    void prepare(double inRate, double outRate, uint32_t channels) {
        ratio_ = (outRate > 0.0) ? (inRate / outRate) : 1.0;
        channels_ = channels;
        reset();
    }

    void reset() {
        pos_ = 1.0;  // force a pull of history on first frame
        histCount_ = 0;
        for (auto& row : h_) {
            for (uint32_t c = 0; c < kMaxChannels; ++c) row[c] = 0.0f;
        }
    }

    bool   passthrough() const { return ratio_ == 1.0; }
    double ratio()       const { return ratio_; }

    // Deterministic input-frame count for `outFrames` outputs.
    uint32_t inputFramesFor(uint32_t outFrames) const {
        if (ratio_ == 1.0) return outFrames;
        double p = pos_;
        uint32_t n = 0;
        for (uint32_t f = 0; f < outFrames; ++f) {
            while (p >= 1.0) { ++n; p -= 1.0; }
            p += ratio_;
        }
        return n;
    }

    // Produce `outFrames` interleaved frames into `out`, pulling input via `in`.
    // `scratch` must hold at least inputFramesFor(outFrames)*channels floats.
    uint32_t produce(float* out, uint32_t outFrames,
                     InputFn in, void* inCtx,
                     float* scratch, uint32_t scratchFrames) {
        const uint32_t ch = channels_;

        if (ratio_ == 1.0) {
            uint32_t got = in(inCtx, out, outFrames, ch);
            for (uint32_t i = got * ch; i < outFrames * ch; ++i) out[i] = 0.0f;
            return got;
        }

        uint32_t need = inputFramesFor(outFrames);
        if (need > scratchFrames) need = scratchFrames;
        uint32_t got = in(inCtx, scratch, need, ch);
        for (uint32_t i = got * ch; i < need * ch; ++i) scratch[i] = 0.0f;

        uint32_t si = 0;
        for (uint32_t f = 0; f < outFrames; ++f) {
            while (pos_ >= 1.0) {
                // Shift history: h[0] ← h[1] ← h[2] ← h[3] ← new
                for (uint32_t c = 0; c < ch; ++c) {
                    h_[0][c] = h_[1][c];
                    h_[1][c] = h_[2][c];
                    h_[2][c] = h_[3][c];
                }
                if (si < need) {
                    for (uint32_t c = 0; c < ch; ++c) h_[3][c] = scratch[si * ch + c];
                    ++si;
                } else {
                    for (uint32_t c = 0; c < ch; ++c) h_[3][c] = 0.0f;
                }
                if (histCount_ < kHistory) ++histCount_;
                pos_ -= 1.0;
            }

            // Interpolate between h[1] and h[2] at fractional position pos_.
            // If history isn't fully primed yet, fall back to linear between
            // h[2] and h[3] (same as old LinearResampler until 4 frames seen).
            const float t = (float)pos_;
            if (histCount_ >= kHistory) {
                // Catmull-Rom: 4-point cubic Hermite (tau=0.5).
                const float t2 = t * t;
                const float t3 = t2 * t;
                for (uint32_t c = 0; c < ch; ++c) {
                    float y0 = h_[0][c], y1 = h_[1][c];
                    float y2 = h_[2][c], y3 = h_[3][c];
                    out[f * ch + c] = 0.5f * (
                        (2.0f * y1) +
                        (-y0 + y2) * t +
                        (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
                        (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3
                    );
                }
            } else {
                // Fallback: linear between h_[1] and h_[2] (priming phase).
                for (uint32_t c = 0; c < ch; ++c) {
                    float a = h_[1][c], b = h_[2][c];
                    out[f * ch + c] = a + (b - a) * t;
                }
            }
            pos_ += ratio_;
        }
        return got;
    }

private:
    double   ratio_ = 1.0;
    uint32_t channels_ = 2;
    double   pos_ = 1.0;
    uint32_t histCount_ = 0;  // how many frames have been shifted in (0..4)
    // h_[0]=y(n-2), h_[1]=y(n-1), h_[2]=y(n), h_[3]=y(n+1)
    // Interpolation is between h_[1] and h_[2] at fractional `pos_`.
    float    h_[kHistory][kMaxChannels] = {};
};

} // namespace roomcut

#endif // ROOMCUT_CUBIC_RESAMPLER_HPP
