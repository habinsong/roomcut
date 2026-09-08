#ifndef ROOMCUT_PARAMETER_CODEC_HPP
#define ROOMCUT_PARAMETER_CODEC_HPP

#include "dsp/ChainParams.hpp"

namespace roomcut {

// The legacy messages, comparison payload and C shim share named fields but
// different layouts. Keep field conversion explicit rather than memcpying them.
template<class Wire> ChainParams decodeParameters(const Wire& value) {
    ChainParams result;
    result.preampDb = value.preampDb;
    for (std::size_t b = 0; b < result.eqGainsDb.size(); ++b) result.eqGainsDb[b] = value.eqGainsDb[b];
    result.limiterReleaseMs = value.limiterReleaseMs;
    result.outputGainDb = value.outputGainDb;
    result.spatialWidth = value.spatialWidth;
    result.centerFocus = value.centerFocus;
    result.crossfeed = value.crossfeed;
    result.roomReduce = value.roomReduce;
    result.spatialMode = value.spatialMode;
    result.highpassHz = value.highpassHz;
    result.compAmount = value.compAmount;
    for (std::size_t b = 0; b < result.parametric.size(); ++b) {
        const auto& band = value.parametric[b];
        result.parametric[b] = {band.enabled != 0, static_cast<int>(band.type), band.freqHz, band.gainDb, band.q};
    }
    return result;
}

template<class Wire> void encodeParameters(const ChainParams& value, Wire& result) {
    result.preampDb = value.preampDb;
    for (std::size_t b = 0; b < value.eqGainsDb.size(); ++b) result.eqGainsDb[b] = value.eqGainsDb[b];
    result.limiterReleaseMs = value.limiterReleaseMs;
    result.outputGainDb = value.outputGainDb;
    result.spatialWidth = value.spatialWidth;
    result.centerFocus = value.centerFocus;
    result.crossfeed = value.crossfeed;
    result.roomReduce = value.roomReduce;
    result.spatialMode = value.spatialMode;
    result.highpassHz = value.highpassHz;
    result.compAmount = value.compAmount;
    for (std::size_t b = 0; b < value.parametric.size(); ++b) {
        const auto& band = value.parametric[b];
        result.parametric[b].enabled = band.enabled ? 1 : 0;
        result.parametric[b].type = static_cast<uint32_t>(band.type);
        result.parametric[b].freqHz = band.freqHz;
        result.parametric[b].gainDb = band.gainDb;
        result.parametric[b].q = band.q;
    }
}
} // namespace roomcut
#endif
