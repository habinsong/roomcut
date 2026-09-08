/*
 * Lifecycle.hpp — the driver/engine handshake & recovery state machines
 * (DEVELOPMENT_PLAN.md §4.1 normal path, §4.2 failure paths).
 *
 * Pure transition functions over the RoomcutDriverLifecycle /
 * RoomcutEngineLifecycle enums: (state, event) -> next state, with no IPC,
 * device, or allocation dependency. The transport pieces (RingRegion,
 * Handshake, Heartbeat) drive these by translating their results into events;
 * keeping the transitions pure makes every path exhaustively host-testable.
 *
 * This file also fixes the §4.5 open questions as concrete code:
 *   - The driver never pushes STATE to the app; the engine infers driver health
 *     from the heartbeat. So driver lifecycle is surfaced to the UI only via the
 *     engine's coarse RoomcutEngineState.
 *   - EngineLost vs SafeBypass are NOT distinguished on the wire — both collapse
 *     to RECOVER. The internal enums keep the distinction for the driver's own
 *     logic; the app only needs "running / bypassed / recovering / stopped".
 */
#ifndef ROOMCUT_LIFECYCLE_HPP
#define ROOMCUT_LIFECYCLE_HPP

extern "C" {
#include "roomcut_ipc.h"
}

namespace roomcut {

// Events the driver state machine reacts to. Named for what happened, not for
// the resulting state.
enum class DriverEvent {
    Load,              // plug-in loaded by coreaudiod
    ServiceFound,      // engine Mach service became reachable
    ServiceMissing,    // look-up failed / engine absent
    HandoffMapped,     // received send-right, region mapped + header valid
    HandoffFailed,     // map/validate failed
    StartWriting,      // first frames to write (StartIO with a live region)
    HeartbeatTimeout,  // engine heartbeat missed → engine presumed gone
    Reconnected,       // engine came back, new handoff completed
    Unload             // plug-in unloading
};

// Events the engine state machine reacts to.
enum class EngineEvent {
    Start,             // process launched
    ServicePublished,  // Mach service is up, awaiting driver HELLO
    HelloReceived,     // driver sent HELLO, format negotiated
    RegionCreated,     // shared region created + send-right handed off
    OutputOpened,      // real output device opened
    OutputLost,        // output device vanished (AirPods/DAC unplug)
    OutputReopened,    // fallback/replacement output opened
    DriverLost,        // driver heartbeat missed / disconnected
    DriverReturned,    // driver feed resumed (stall watchdog saw writeIndex
                       // advance again; a full reconnect re-enters via HELLO)
    Stop               // shutting down
};

// Driver transition. Unhandled (state, event) pairs return the state unchanged
// so the machine never lands in an undefined place.
RoomcutDriverLifecycle driverNext(RoomcutDriverLifecycle state, DriverEvent ev);

// Engine transition. Same total-function contract.
RoomcutEngineLifecycle engineNext(RoomcutEngineLifecycle state, EngineEvent ev);

// §4.5 decision, in code: project the engine's fine-grained lifecycle onto the
// coarse RoomcutEngineState the app sees over ROOMCUT_MSG_STATE.
RoomcutEngineState engineWireState(RoomcutEngineLifecycle state);

// §4.5 decision, in code: the driver has no wire channel of its own, so when the
// engine knows the driver is lost/bypassed it reports RECOVER. This maps a
// driver lifecycle to the coarse state the engine would publish on its behalf.
// EngineLost and SafeBypass deliberately collapse to the same RECOVER.
RoomcutEngineState driverWireState(RoomcutDriverLifecycle state);

} // namespace roomcut

#endif // ROOMCUT_LIFECYCLE_HPP
