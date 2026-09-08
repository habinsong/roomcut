/*
 * LinearResampler.hpp — minimal interleaved linear-interpolation resampler for
 * the engine's render path (Phase 4).
 *
 * Why it exists: the shared ring carries audio at the negotiated input rate
 * (e.g. 48 kHz from the driver), but the real output device runs at its own
 * native rate (e.g. 96 kHz on many DACs). Without conversion the render
 * callback drains the ring at the wrong cadence — it empties faster than the
 * producer fills, so the output underruns to silence (observed in the Phase 4
 * smoke test). This converts ring frames → output frames so playback is
 * continuous and at the right pitch/speed.
 *
 * Linear interpolation is intentionally simple: RT-safe (no alloc/lock), cheap,
 * and enough to prove the passthrough. A polyphase/sinc converter is a later
 * quality pass (docs/03 AudioFormatManager); the pull interface is unchanged.
 *
 * Streaming correctness: the resampler holds the two most recent input frames
 * (prev_, cur_) and a fractional position pos_ between them, all persisted
 * across produce() calls. Input is pulled ONLY as the position crosses whole
 * frames, and every pulled frame is retained in state — so no frame is ever
 * read from the ring and discarded (which would skip/glitch). A counting
 * pre-pass computes exactly how many input frames the block will consume, so
 * the single batched ring read pulls precisely that many. ratio == 1 takes a
 * bit-exact fast path (no interpolation, no added latency).
 */
#ifndef ROOMCUT_LINEAR_RESAMPLER_HPP
#define ROOMCUT_LINEAR_RESAMPLER_HPP

#include <cstddef>
#include <cstdint>

namespace roomcut {

class LinearResampler {
public:
    // Pull `inFrames` interleaved input frames into `dst`; return how many were
    // actually available (a short read means the source ran dry).
    using InputFn = uint32_t (*)(void* ctx, float* dst, uint32_t inFrames, uint32_t channels);

    static constexpr uint32_t kMaxChannels = 8;

    void prepare(double inRate, double outRate, uint32_t channels) {
        ratio_ = (outRate > 0.0) ? (inRate / outRate) : 1.0;
        channels_ = channels;
        reset();
    }

    void reset() {
        pos_ = 1.0;          // force a pull of prev_+cur_ on the first frame
        primed_ = false;
        for (uint32_t c = 0; c < kMaxChannels; ++c) { prev_[c] = 0.0f; cur_[c] = 0.0f; }
    }

    bool   passthrough() const { return ratio_ == 1.0; }
    double ratio()       const { return ratio_; }

    // Input frames this block will consume for `outFrames` outputs. Deterministic
    // from pos_ + ratio (independent of sample values), so the caller can size
    // scratch and the real loop consumes exactly this many.
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
    // Returns input frames actually pulled (for underrun accounting). On a dry
    // source the held sample / silence is emitted rather than a hard gap.
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
        for (uint32_t i = got * ch; i < need * ch; ++i) scratch[i] = 0.0f; // dry → silence

        uint32_t si = 0;  // next scratch frame to consume; ends exactly at `need`
        for (uint32_t f = 0; f < outFrames; ++f) {
            while (pos_ >= 1.0) {
                for (uint32_t c = 0; c < ch; ++c) prev_[c] = cur_[c];
                if (si < need) {
                    for (uint32_t c = 0; c < ch; ++c) cur_[c] = scratch[si * ch + c];
                    ++si;
                } else {
                    for (uint32_t c = 0; c < ch; ++c) cur_[c] = 0.0f;
                }
                pos_ -= 1.0;
                primed_ = true;
            }
            for (uint32_t c = 0; c < ch; ++c) {
                float a = prev_[c], b = cur_[c];
                out[f * ch + c] = (float)(a + (b - a) * pos_);
            }
            pos_ += ratio_;
        }
        return got;
    }

private:
    double   ratio_ = 1.0;     // inRate / outRate
    uint32_t channels_ = 2;
    double   pos_ = 1.0;       // fractional position between prev_ and cur_
    bool     primed_ = false;  // whether cur_ has been loaded at least once
    float    prev_[kMaxChannels] = {0};
    float    cur_[kMaxChannels]  = {0};
};

} // namespace roomcut

#endif // ROOMCUT_LINEAR_RESAMPLER_HPP
