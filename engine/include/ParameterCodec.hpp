#ifndef ROOMCUT_PARAMETER_CODEC_HPP
#define ROOMCUT_PARAMETER_CODEC_HPP

#include "dsp/ChainParams.hpp"

#include <type_traits>

namespace roomcut {

// The legacy messages, comparison payload and C shim share named fields but
// different layouts. Keep field conversion explicit rather than memcpying them.
// The comparison payload carries no virtual room (see RoomcutParameterValues),
// so the room is copied only for the wire types that actually have it.
template<class Wire, class = void> struct HasVirtualRoom : std::false_type {};
template<class Wire> struct HasVirtualRoom<Wire, std::void_t<decltype(Wire::roomType)>> : std::true_type {};
template<class Wire> inline constexpr bool hasVirtualRoom = HasVirtualRoom<Wire>::value;
template<class Wire, class = void> struct HasUpmix : std::false_type {};
template<class Wire> struct HasUpmix<Wire, std::void_t<decltype(Wire::surroundType)>> : std::true_type {};
template<class Wire> inline constexpr bool hasUpmix = HasUpmix<Wire>::value;
template<class Wire, class = void> struct HasBedRenderer : std::false_type {};
template<class Wire> struct HasBedRenderer<Wire, std::void_t<decltype(Wire::bedRenderer)>> : std::true_type {};
template<class Wire> inline constexpr bool hasBedRenderer = HasBedRenderer<Wire>::value;
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
    if constexpr (hasVirtualRoom<Wire>) {
        result.roomType = value.roomType;
        result.roomAmount = value.roomAmount;
    }
    if constexpr (hasUpmix<Wire>) {
        result.surroundType = value.surroundType;
        result.centerWidth = value.centerWidth;
        result.surroundDepth = value.surroundDepth;
    }
    if constexpr (hasBedRenderer<Wire>) result.bedRenderer = value.bedRenderer;
    for (std::size_t b = 0; b < result.parametric.size(); ++b) {
        const auto& band = value.parametric[b];
        const auto& dyn = value.dynamics[b];
        result.parametric[b] = {band.enabled != 0, static_cast<int>(band.type), band.freqHz,
                                band.gainDb, band.q, dyn.enabled != 0, dyn.thresholdDb,
                                dyn.rangeDb, dyn.attackMs, dyn.releaseMs};
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
    if constexpr (hasVirtualRoom<Wire>) {
        result.roomType = value.roomType;
        result.roomAmount = value.roomAmount;
    }
    if constexpr (hasUpmix<Wire>) {
        result.surroundType = value.surroundType;
        result.centerWidth = value.centerWidth;
        result.surroundDepth = value.surroundDepth;
    }
    if constexpr (hasBedRenderer<Wire>) result.bedRenderer = value.bedRenderer;
    for (std::size_t b = 0; b < value.parametric.size(); ++b) {
        const auto& band = value.parametric[b];
        result.parametric[b].enabled = band.enabled ? 1 : 0;
        result.parametric[b].type = static_cast<uint32_t>(band.type);
        result.parametric[b].freqHz = band.freqHz;
        result.parametric[b].gainDb = band.gainDb;
        result.parametric[b].q = band.q;
        result.dynamics[b].enabled = band.dynamic ? 1 : 0;
        result.dynamics[b]._pad0 = 0;
        result.dynamics[b].thresholdDb = band.thresholdDb;
        result.dynamics[b].rangeDb = band.rangeDb;
        result.dynamics[b].attackMs = band.attackMs;
        result.dynamics[b].releaseMs = band.releaseMs;
    }
}
} // namespace roomcut
#endif
