#include "SoundCommands.hpp"
#include "EngineSoundState.hpp"
#include "ParameterCodec.hpp"

namespace roomcut {
uint32_t applySoundCommand(const RoomcutControlMsgBuffer& request, EngineSoundState& sound) {
    switch (request.raw.header.msgh_id) {
    case ROOMCUT_MSG_SET_PRESET:
        return sound.applyPreset(request.setPreset.presetId) ? 0 : 1;
    case ROOMCUT_MSG_SET_PARAMS:
        sound.apply(decodeParameters(request.setParams), "custom");
        return 0;
    case ROOMCUT_MSG_SET_COMPARISON: {
        const auto& value = request.comparisonRequest;
        // The comparison payload carries neither the virtual room nor the upmix,
        // so both sides inherit what is playing right now: A/B swaps the sound,
        // not the space. Both ride in the trailing fields, so an older sender
        // carries none — keep what is already playing rather than silencing it
        // behind the user's back.
        const auto& live = sound.parameters();
        const bool carriesRoom = value.version >= 3;
        const bool carriesUpmix = value.version >= 4;
        const bool carriesBedRenderer = value.version >= 5;
        struct Space { double roomType, roomAmount, surroundType, centerWidth, surroundDepth, bedRenderer; };
        auto withSpace = [&](ChainParams p, const Space& space) {
            p.roomType = carriesRoom ? space.roomType : live.roomType;
            p.roomAmount = carriesRoom ? space.roomAmount : live.roomAmount;
            p.surroundType = carriesUpmix ? space.surroundType : live.surroundType;
            p.centerWidth = carriesUpmix ? space.centerWidth : live.centerWidth;
            p.surroundDepth = carriesUpmix ? space.surroundDepth : live.surroundDepth;
            p.bedRenderer = carriesBedRenderer ? space.bedRenderer : live.bedRenderer;
            return p;
        };
        const Space referenceSpace{value.referenceRoomType, value.referenceRoomAmount,
                                   value.referenceSurroundType, value.referenceCenterWidth,
                                   value.referenceSurroundDepth, value.referenceBedRenderer};
        const Space currentSpace{value.currentRoomType, value.currentRoomAmount,
                                 value.currentSurroundType, value.currentCenterWidth,
                                 value.currentSurroundDepth, value.currentBedRenderer};
        const auto reference = withSpace(decodeParameters(value.reference), referenceSpace);
        if (value.kind == ROOMCUT_COMPARISON_PRESET)
            return sound.applyPreset(value.presetId, &reference, value.enabled != 0) ? 0 : 1;
        sound.applyComparison(withSpace(decodeParameters(value.current), currentSpace),
                              reference, value.enabled != 0);
        return 0;
    }
    default:
        return 1;
    }
}
} // namespace roomcut
