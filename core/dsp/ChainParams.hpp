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
    std::array<ParametricBand, ParametricEQ::kNumBands> parametric{}; // user EQ bands

    // Flat: unity everywhere (eqGainsDb default-initialized to 0).
    static ChainParams flat() { return ChainParams{}; }
};

} // namespace roomcut

#endif // ROOMCUT_CHAIN_PARAMS_HPP
