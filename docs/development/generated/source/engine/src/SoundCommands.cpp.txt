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
        const auto reference = decodeParameters(value.reference);
        if (value.kind == ROOMCUT_COMPARISON_PRESET)
            return sound.applyPreset(value.presetId, &reference, value.enabled != 0) ? 0 : 1;
        sound.applyComparison(decodeParameters(value.current), reference, value.enabled != 0);
        return 0;
    }
    default:
        return 1;
    }
}
} // namespace roomcut
