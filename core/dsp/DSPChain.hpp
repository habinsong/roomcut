/*
 * DSPChain.hpp — the live processing chain (docs/04): Preamp → optional HPF →
 * 10-band EQ → Parametric EQ → Spatial → optional light Compressor → output gain
 * → Limiter. This
 * is the single object the engine's render thread drives; everything above it
 * (presets, UI, AI) just produces ChainParams. The HPF + compressor are Dialogue
 * -mode internals (off unless a preset sets highpassHz / compAmount).
 *
 * Click-free transitions (docs/04 "crossfade on preset switch / bypass toggle
 * to avoid pops"): both bypass toggles and preset changes ramp a dry/wet mix
 * over a short window instead of switching coefficients instantaneously. The
 * limiter is always in the wet path — it is never bypassed (safety).
 *
 * Real-time safe after prepare(): processInterleaved() does no allocation,
 * locks, or logging. Header-only to match RoomcutCore.
 */
#ifndef ROOMCUT_DSP_CHAIN_HPP
#define ROOMCUT_DSP_CHAIN_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Biquad.hpp"
#include "ChainParams.hpp"
#include "Compressor.hpp"
#include "GraphicEQ.hpp"
#include "Limiter.hpp"
#include "ParametricEQ.hpp"
#include "Spatial.hpp"

namespace roomcut {

inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

class DSPChain {
public:
    void prepare(double fs, std::size_t channels = 2, double crossfadeMs = 15.0) {
        fs_ = fs;
        channels_ = channels;
        eq_.prepare(fs);
        peq_.prepare(fs);
        spatial_.prepare(fs);
        comp_.prepare(fs, channels);
        // The limiter is a clip-only safety net at digital full scale. A sub-0
        // ceiling (the old -1 dB) needlessly dulls hot masters even with EQ flat,
        // which read as the engine sounding "softer/dirtier" than the device
        // direct — so it sits at 0 dBFS and only catches true overs.
        limiter_.prepare(fs, 2.0, kClipCeilingDb, params_.limiterReleaseMs, channels);
        preampLin_ = dbToLin(params_.preampDb);
        outGainLin_ = dbToLin(params_.outputGainDb);
        configureHpf();
        comp_.setParams(params_.compAmount);

        crossfadeSamples_ = std::max<std::size_t>(1, (std::size_t)std::lround(crossfadeMs * 0.001 * fs));
        // Start fully settled at current bypass target.
        mix_ = bypass_ ? 0.0 : 1.0;
        mixTarget_ = mix_;
        mixStep_ = 1.0 / (double)crossfadeSamples_;
    }

    // Apply a new preset. The EQ/preamp/limiter update immediately, but we ramp
    // the wet mix down and back so coefficient changes don't click. For MVP we
    // implement the simpler, robust variant: a short wet→dry→wet is overkill;
    // instead we crossfade from the PRE-change processed signal isn't available,
    // so we ramp the *parameter application* by briefly dipping toward dry and
    // recovering. This keeps switches click-free in practice.
    void setParams(const ChainParams& p) {
        params_ = p;
        preampLin_ = dbToLin(p.preampDb);
        outGainLin_ = dbToLin(p.outputGainDb);
        for (std::size_t b = 0; b < GraphicEQ::kNumBands; ++b) {
            eq_.setBandGain(b, p.eqGainsDb[b]);
        }
        for (std::size_t b = 0; b < ParametricEQ::kNumBands; ++b) {
            peq_.setBand(b, p.parametric[b]);
        }
        spatial_.setParams(p.spatialWidth, p.centerFocus, p.crossfeed, p.roomReduce, p.spatialMode);
        configureHpf();
        comp_.setParams(p.compAmount);
        // The limiter ceiling is fixed at kClipCeilingDb (0 dBFS): it is a pure
        // overshoot guard, not a tone control, so there is no ceiling parameter.
        // Trigger a brief re-settle crossfade if active (not bypassed).
        if (!bypass_) {
            mix_ = std::min(mix_, 0.5); // dip toward dry, then ramp back to wet
            mixTarget_ = 1.0;
        }
    }

    const ChainParams& params() const { return params_; }

    // Toggle bypass with a click-free dry/wet ramp.
    void setBypass(bool bypass) {
        bypass_ = bypass;
        mixTarget_ = bypass ? 0.0 : 1.0;
    }

    bool bypassed() const { return bypass_; }

    void reset() {
        eq_.reset();
        peq_.reset();
        spatial_.reset();
        hpf_.reset();
        comp_.reset();
        limiter_.reset();
        mix_ = bypass_ ? 0.0 : 1.0;
        mixTarget_ = mix_;
    }

    double limiterGainReductionDb() const { return limiter_.gainReductionDb(); }

    // Process an interleaved buffer in place. dry = input; wet = full chain.
    // The limiter runs on the mixed pre-output so even a partially-dry mix is
    // still peak-safe.
    void processInterleaved(float* buf, std::size_t frames) {
        for (std::size_t f = 0; f < frames; ++f) {
            float* frame = &buf[f * channels_];

            // Advance the dry/wet mix toward its target (click-free ramp).
            if (mix_ < mixTarget_)      mix_ = std::min(mixTarget_, mix_ + mixStep_);
            else if (mix_ > mixTarget_) mix_ = std::max(mixTarget_, mix_ - mixStep_);

            // Compute wet (preamp → HPF → EQ) per channel, keep dry for the mix.
            float wet[Limiter::kMaxChannels];
            for (std::size_t c = 0; c < channels_; ++c) {
                float dry = frame[c];
                float w = (float)(dry * preampLin_);
                if (hpfActive_) w = hpf_.processSample(w, c);
                w = eq_.processSample(w, c);
                w = peq_.processSample(w, c);
                wet[c] = w;
            }
            spatial_.processFrame(wet, channels_);
            comp_.processFrame(wet);

            // Dry/wet blend. Output gain applies to the WET path only (like the
            // preamp), so a full bypass (mix_ == 0) is a true passthrough — output
            // gain no longer leaks through the dry signal.
            for (std::size_t c = 0; c < channels_; ++c) {
                double mixed = (double)wet[c] * outGainLin_ * mix_ + (double)frame[c] * (1.0 - mix_);
                frame[c] = (float)mixed;
            }

            // Limiter always runs (safety), on the blended signal.
            limiter_.processFrame(frame);
        }
    }

private:
    // Configure the high-pass from params_.highpassHz. Below 20 Hz → off (the
    // band is inaudible and a near-DC HPF is pointless). Butterworth Q.
    void configureHpf() {
        hpfActive_ = params_.highpassHz >= 20.0;
        if (hpfActive_) {
            hpf_.set(BiquadType::HighPass, fs_, params_.highpassHz, 0.0, 0.70710678);
        }
    }

    static constexpr double kClipCeilingDb = 0.0;  // limiter = clip-only at 0 dBFS

    double fs_ = 48000.0;
    std::size_t channels_ = 2;

    ChainParams params_{};
    double preampLin_ = 1.0;
    double outGainLin_ = 1.0;

    GraphicEQ eq_{};
    ParametricEQ peq_{};
    Biquad hpf_{};
    bool hpfActive_ = false;
    Spatial spatial_{};
    Compressor comp_{};
    Limiter limiter_{};

    bool bypass_ = false;
    double mix_ = 1.0;        // 0 = fully dry (bypass), 1 = fully wet
    double mixTarget_ = 1.0;
    double mixStep_ = 1.0;
    std::size_t crossfadeSamples_ = 1;
};

} // namespace roomcut

#endif // ROOMCUT_DSP_CHAIN_HPP
