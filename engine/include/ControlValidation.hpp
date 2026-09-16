#ifndef ROOMCUT_CONTROL_VALIDATION_HPP
#define ROOMCUT_CONTROL_VALIDATION_HPP

#include "roomcut_handshake.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>

namespace roomcut {
namespace control_validation {
inline bool sizeMatches(std::size_t size, std::size_t current, std::initializer_list<std::size_t> legacy = {}) {
    if (size >= current) return true;
    for (auto value : legacy) if (size == value) return true;
    return false;
}
inline bool envelope(const RoomcutControlMsgBuffer& buffer) {
    const auto& header = buffer.raw.header;
    return !(header.msgh_bits & MACH_MSGH_BITS_COMPLEX) && header.msgh_voucher_port == MACH_PORT_NULL
        && header.msgh_size >= sizeof(mach_msg_header_t) + sizeof(uint32_t)
        && header.msgh_size <= sizeof(buffer) - sizeof(mach_msg_max_trailer_t)
        && buffer.reply.msgType == static_cast<uint32_t>(header.msgh_id);
}
inline void clearAbsentFields(RoomcutControlMsgBuffer& buffer) {
    // A Mach trailer starts after the received bytes. It must not masquerade
    // as optional fields in a reply/request from an older peer.
    auto* bytes = reinterpret_cast<unsigned char*>(&buffer);
    std::memset(bytes + buffer.raw.header.msgh_size, 0, sizeof(buffer) - buffer.raw.header.msgh_size);
}
template<std::size_t N> bool terminated(const char (&text)[N]) { return std::memchr(text, 0, N) != nullptr; }
} // namespace control_validation

inline bool normalizeControlRequest(RoomcutControlMsgBuffer& buffer) {
    using namespace control_validation;
    if (!envelope(buffer) || buffer.raw.header.msgh_remote_port == MACH_PORT_NULL
        || MACH_MSGH_BITS_REMOTE(buffer.raw.header.msgh_bits) != MACH_MSG_TYPE_PORT_SEND_ONCE) return false;
    const auto size = buffer.raw.header.msgh_size;
    bool valid = false;
    switch (buffer.raw.header.msgh_id) {
    case ROOMCUT_MSG_SET_PRESET: valid = sizeMatches(size, sizeof(RoomcutSetPresetRequest)); break;
    case ROOMCUT_MSG_SET_OUTPUT_DEV: valid = sizeMatches(size, sizeof(RoomcutSetDeviceRequest)); break;
    case ROOMCUT_MSG_SET_BYPASS: valid = sizeMatches(size, sizeof(RoomcutSetBypassRequest)); break;
    case ROOMCUT_MSG_SET_KEEP_DEFAULT: valid = sizeMatches(size, sizeof(RoomcutSetKeepDefaultRequest)); break;
    case ROOMCUT_MSG_SET_VOLUME_BOOST: valid = sizeMatches(size, sizeof(RoomcutSetVolumeBoostRequest)); break;
    case ROOMCUT_MSG_SET_HEAD_POSE:
        valid = sizeMatches(size, sizeof(RoomcutSetHeadPoseRequest))
            && buffer.setHeadPose.active <= 1 && std::isfinite(buffer.setHeadPose.yawDeg);
        break;
    case ROOMCUT_MSG_SET_PARAMS:
        valid = sizeMatches(size, sizeof(RoomcutSetParamsRequest), {
            offsetof(RoomcutSetParamsRequest, spatialWidth), offsetof(RoomcutSetParamsRequest, spatialMode),
            offsetof(RoomcutSetParamsRequest, parametric), offsetof(RoomcutSetParamsRequest, highpassHz),
            offsetof(RoomcutSetParamsRequest, dynamics), offsetof(RoomcutSetParamsRequest, roomType),
            offsetof(RoomcutSetParamsRequest, surroundType)});
        break;
    case ROOMCUT_MSG_STATE: case ROOMCUT_MSG_GET_PARAMS: case ROOMCUT_MSG_GET_ANALYSIS:
        valid = sizeMatches(size, sizeof(RoomcutStateRequest), {sizeof(mach_msg_header_t) + sizeof(uint32_t)});
        break;
    case ROOMCUT_MSG_SET_COMPARISON:
        // A version-2 sender stops right before the appended virtual room, a
        // version-3 one right before the appended upmix.
        valid = sizeMatches(size, sizeof(RoomcutComparisonRequest),
                            {offsetof(RoomcutComparisonRequest, currentRoomType),
                             offsetof(RoomcutComparisonRequest, currentSurroundType)})
            && buffer.comparisonRequest.version >= ROOMCUT_COMPARISON_MIN_VERSION
            && buffer.comparisonRequest.version <= ROOMCUT_COMPARISON_VERSION
            && buffer.comparisonRequest.enabled <= 1 && buffer.comparisonRequest.kind <= ROOMCUT_COMPARISON_PRESET;
        break;
    case ROOMCUT_MSG_GET_COMPARISON:
        valid = size == sizeof(RoomcutGetComparisonRequest)
            && buffer.getComparison.version >= ROOMCUT_COMPARISON_MIN_VERSION
            && buffer.getComparison.version <= ROOMCUT_COMPARISON_VERSION;
        break;
    }
    if (!valid) return false;
    clearAbsentFields(buffer);
    if (buffer.raw.header.msgh_id == ROOMCUT_MSG_SET_PRESET) return terminated(buffer.setPreset.presetId);
    if (buffer.raw.header.msgh_id == ROOMCUT_MSG_SET_OUTPUT_DEV) return terminated(buffer.setDevice.deviceUID);
    if (buffer.raw.header.msgh_id == ROOMCUT_MSG_SET_COMPARISON) return terminated(buffer.comparisonRequest.presetId);
    return true;
}

inline bool normalizeControlReply(RoomcutControlMsgBuffer& buffer, uint32_t expected) {
    using namespace control_validation;
    if (!envelope(buffer) || buffer.raw.header.msgh_id != static_cast<mach_msg_id_t>(expected)
        || buffer.raw.header.msgh_remote_port != MACH_PORT_NULL) return false;
    const auto size = buffer.raw.header.msgh_size;
    bool valid = false;
    switch (expected) {
    case ROOMCUT_MSG_STATE:
        valid = sizeMatches(size, sizeof(RoomcutStateReply), {
            offsetof(RoomcutStateReply, outputDeviceUID), offsetof(RoomcutStateReply, keepDefault),
            offsetof(RoomcutStateReply, capabilities), offsetof(RoomcutStateReply, volumeBoost),
            offsetof(RoomcutStateReply, engineLatencyMs)});
        break;
    case ROOMCUT_MSG_GET_PARAMS:
        valid = sizeMatches(size, sizeof(RoomcutGetParamsReply), {
            offsetof(RoomcutGetParamsReply, spatialWidth), offsetof(RoomcutGetParamsReply, spatialMode),
            offsetof(RoomcutGetParamsReply, parametric), offsetof(RoomcutGetParamsReply, highpassHz),
            offsetof(RoomcutGetParamsReply, dynamics), offsetof(RoomcutGetParamsReply, roomType),
            offsetof(RoomcutGetParamsReply, surroundType)});
        break;
    case ROOMCUT_MSG_GET_ANALYSIS: valid = sizeMatches(size, sizeof(RoomcutAnalysisReply)); break;
    case ROOMCUT_MSG_GET_COMPARISON:
        valid = sizeMatches(size, sizeof(RoomcutComparisonReply),
                            {offsetof(RoomcutComparisonReply, currentRoomType),
                             offsetof(RoomcutComparisonReply, currentSurroundType)})
            && buffer.comparisonReply.version >= ROOMCUT_COMPARISON_MIN_VERSION
            && buffer.comparisonReply.version <= ROOMCUT_COMPARISON_VERSION
            && buffer.comparisonReply.enabled <= 1 && buffer.comparisonReply.state <= 5
            && std::isfinite(buffer.comparisonReply.currentReductionDb)
            && std::isfinite(buffer.comparisonReply.referenceReductionDb)
            && buffer.comparisonReply.currentReductionDb >= 0 && buffer.comparisonReply.referenceReductionDb >= 0;
        break;
    default: valid = sizeMatches(size, sizeof(RoomcutControlReply)); break;
    }
    if (!valid) return false;
    clearAbsentFields(buffer);
    if (expected == ROOMCUT_MSG_STATE) return terminated(buffer.stateReply.presetId) && terminated(buffer.stateReply.outputDeviceUID);
    if (expected == ROOMCUT_MSG_GET_PARAMS) return terminated(buffer.paramsReply.presetId);
    if (expected == ROOMCUT_MSG_GET_COMPARISON) return terminated(buffer.comparisonReply.presetId);
    return true;
}
} // namespace roomcut
#endif
