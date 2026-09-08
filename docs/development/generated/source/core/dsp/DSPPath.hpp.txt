#ifndef ROOMCUT_DSP_PATH_HPP
#define ROOMCUT_DSP_PATH_HPP

#include <cmath>
#include <cstddef>

#include "ChainParams.hpp"
#include "Compressor.hpp"
#include "Spatial.hpp"

namespace roomcut {

inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

// One processing state, without bypass or the final limiter. Fixed-size filter
// and delay storage lets DSPChain copy a live path without allocating on render.
class DSPPath {
public:
    void prepare(double fs, std::size_t channels) {
        fs_ = fs;
        channels_ = channels;
        eq_.prepare(fs);
        peq_.prepare(fs);
        spatial_.prepare(fs);
        comp_.prepare(fs, channels);
    }

    void setParams(const ChainParams& params) {
        preampLin_ = dbToLin(params.preampDb);
        outGainLin_ = dbToLin(params.outputGainDb);
        for (std::size_t b = 0; b < GraphicEQ::kNumBands; ++b)
            eq_.setBandGain(b, params.eqGainsDb[b]);
        for (std::size_t b = 0; b < ParametricEQ::kNumBands; ++b)
            peq_.setBand(b, params.parametric[b]);
        spatial_.setParams(params.spatialWidth, params.centerFocus, params.crossfeed,
                           params.roomReduce, params.spatialMode);
        hpfActive_ = params.highpassHz >= 20.0;
        if (hpfActive_)
            hpf_.set(BiquadType::HighPass, fs_, params.highpassHz, 0.0, 0.70710678);
        else
            hpf_.reset();
        comp_.setParams(params.compAmount);
    }

    void reset() {
        eq_.reset();
        peq_.reset();
        spatial_.reset();
        hpf_.reset();
        comp_.reset();
    }

    void processFrame(float* frame) {
        for (std::size_t c = 0; c < channels_; ++c) {
            float sample = static_cast<float>(frame[c] * preampLin_);
            if (hpfActive_) sample = hpf_.processSample(sample, c);
            frame[c] = peq_.processSample(eq_.processSample(sample, c), c);
        }
        spatial_.processFrame(frame, channels_);
        comp_.processFrame(frame);
        for (std::size_t c = 0; c < channels_; ++c)
            frame[c] = static_cast<float>(frame[c] * outGainLin_);
    }

private:
    double fs_ = 48000;
    std::size_t channels_ = 2;
    double preampLin_ = 1;
    double outGainLin_ = 1;
    GraphicEQ eq_;
    ParametricEQ peq_;
    Biquad hpf_;
    bool hpfActive_ = false;
    Spatial spatial_;
    Compressor comp_;
};

} // namespace roomcut
#endif
