/*
 * ChainParams.hpp — the parameter set the engine hands the DSP chain. A plain
 * value type (no audio state) mirroring the MVP subset of preset.schema.json so
 * the control thread can build one and publish it to the audio thread via the
 * engine's double-buffering (docs/03 "swaps DSP parameters via lock-free
 * double-buffering").
 *
 * Live chain: preamp, optional HPF, 10-band EQ, spatial, optional light comp,
 * limiter, output gain. The HPF + compressor are Dialogue-mode internals (set by
 * the builtin preset, not user-editable), so they have no IPC/UI field.
 */
#ifndef ROOMCUT_CHAIN_PARAMS_HPP
#define ROOMCUT_CHAIN_PARAMS_HPP

#include <array>
#include <cstddef>

#include "GraphicEQ.hpp"
#include "ParametricEQ.hpp"

namespace roomcut {

struct ChainParams {
    double preampDb = 0.0;                               // preset.chain.preampDb
    std::array<double, GraphicEQ::kNumBands> eqGainsDb{}; // 10 band gains
    double limiterReleaseMs = 100.0;                      // preset.chain.limiter.releaseMs
    double outputGainDb = 0.0;                            // preset.chain.outputGainDb
    double spatialWidth = 0.0;
    double centerFocus = 0.0;
    double crossfeed = 0.0;
    double roomReduce = 0.0;
    double spatialMode = 0.0;                             // 0 = speaker (XTC), 1 = headphone (crossfeed)
    double highpassHz = 0.0;                              // 0 = off (Dialogue HPF)
    double compAmount = 0.0;                              // 0..100, 0 = off (light comp)
    double roomType = 0.0;                                // virtual room: 0 = off, 1 = Studio, 2 = Living, 3 = Hall
    double roomAmount = 50.0;                             // 0..100, 50 = the room's reference level
    double surroundType = 0.0;       // upmix: 0 = off, 2 = virtual 5.1, 3 = virtual 7.1
    // Upmix steering, 0..100 (see Upmixer). Width at 100 extracts the whole
    // centre image. Depth defaults to the balance point rather than the maximum:
    // measured on ordinary programme, 50 already takes the ears from 0.57
    // correlated to 0.09 — an unmistakable opening — while keeping half the
    // head-tracking cue, which the diffuse surround bus does not carry. 100 is
    // wider still (-0.43) and tracks less; the control is there to choose.
    double centerWidth = 100.0;
    double surroundDepth = 50.0;
    std::array<ParametricBand, ParametricEQ::kNumBands> parametric{}; // user EQ bands

    // Flat: unity everywhere (eqGainsDb default-initialized to 0).
    static ChainParams flat() { return ChainParams{}; }

    bool operator==(const ChainParams& other) const {
        return preampDb == other.preampDb && eqGainsDb == other.eqGainsDb
            && limiterReleaseMs == other.limiterReleaseMs && outputGainDb == other.outputGainDb
            && spatialWidth == other.spatialWidth && centerFocus == other.centerFocus
            && crossfeed == other.crossfeed && roomReduce == other.roomReduce
            && spatialMode == other.spatialMode && highpassHz == other.highpassHz
            && compAmount == other.compAmount && roomType == other.roomType
            && roomAmount == other.roomAmount && surroundType == other.surroundType
            && centerWidth == other.centerWidth && surroundDepth == other.surroundDepth
            && parametric == other.parametric;
    }
};

} // namespace roomcut

#endif // ROOMCUT_CHAIN_PARAMS_HPP
