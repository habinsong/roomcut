#ifndef ROOMCUT_SOUND_COMMANDS_HPP
#define ROOMCUT_SOUND_COMMANDS_HPP

#include "roomcut_handshake.h"

namespace roomcut {
class EngineSoundState;

// Apply a normalized sound request. 0 is accepted, 1 is an unknown preset or
// unsupported command. Rejected commands never change current/reference state.
// Publishing, persistence and Mach replies remain with the caller.
uint32_t applySoundCommand(const RoomcutControlMsgBuffer& request, EngineSoundState& sound);

} // namespace roomcut
#endif
