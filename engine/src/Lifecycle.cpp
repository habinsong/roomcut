/*
 * Lifecycle.cpp — see Lifecycle.hpp. Pure (state, event) -> state tables.
 */
#include "Lifecycle.hpp"

namespace roomcut {

RoomcutDriverLifecycle driverNext(RoomcutDriverLifecycle state, DriverEvent ev) {
    switch (state) {
        case ROOMCUT_DRIVER_UNINITIALIZED:
            if (ev == DriverEvent::Load) return ROOMCUT_DRIVER_LOADED;
            break;

        case ROOMCUT_DRIVER_LOADED:
            if (ev == DriverEvent::ServiceFound)   return ROOMCUT_DRIVER_HANDSHAKING;
            if (ev == DriverEvent::ServiceMissing) return ROOMCUT_DRIVER_WAITING_ENGINE;
            if (ev == DriverEvent::Unload)         return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_WAITING_ENGINE:
            if (ev == DriverEvent::ServiceFound) return ROOMCUT_DRIVER_HANDSHAKING;
            if (ev == DriverEvent::Unload)       return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_HANDSHAKING:
            if (ev == DriverEvent::HandoffMapped) return ROOMCUT_DRIVER_SHARED_READY;
            // A failed handoff is not fatal: degrade to safe bypass and retry.
            if (ev == DriverEvent::HandoffFailed) return ROOMCUT_DRIVER_SAFE_BYPASS;
            if (ev == DriverEvent::ServiceMissing) return ROOMCUT_DRIVER_WAITING_ENGINE;
            if (ev == DriverEvent::Unload)        return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_SHARED_READY:
            if (ev == DriverEvent::StartWriting)     return ROOMCUT_DRIVER_STREAMING;
            if (ev == DriverEvent::HeartbeatTimeout) return ROOMCUT_DRIVER_ENGINE_LOST;
            if (ev == DriverEvent::Unload)           return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_STREAMING:
            if (ev == DriverEvent::HeartbeatTimeout) return ROOMCUT_DRIVER_ENGINE_LOST;
            if (ev == DriverEvent::Unload)           return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_ENGINE_LOST:
            // Stop touching the (possibly stale) mapping; drop frames safely
            // while we wait for the engine to come back.
            if (ev == DriverEvent::HeartbeatTimeout) return ROOMCUT_DRIVER_SAFE_BYPASS;
            if (ev == DriverEvent::Reconnected)      return ROOMCUT_DRIVER_HANDSHAKING;
            if (ev == DriverEvent::Unload)           return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_SAFE_BYPASS:
            // Keep retrying the handoff; a found service restarts the handshake.
            if (ev == DriverEvent::ServiceFound) return ROOMCUT_DRIVER_HANDSHAKING;
            if (ev == DriverEvent::Reconnected)  return ROOMCUT_DRIVER_HANDSHAKING;
            if (ev == DriverEvent::Unload)       return ROOMCUT_DRIVER_UNLOADING;
            break;

        case ROOMCUT_DRIVER_UNLOADING:
            break;
    }
    return state; // total function: unhandled pairs are no-ops
}

RoomcutEngineLifecycle engineNext(RoomcutEngineLifecycle state, EngineEvent ev) {
    // Stop is accepted from any running state.
    if (ev == EngineEvent::Stop) return ROOMCUT_ENGINE_STOPPING;

    switch (state) {
        case ROOMCUT_ENGINE_STARTING:
            if (ev == EngineEvent::Start)            return ROOMCUT_ENGINE_STARTING;
            if (ev == EngineEvent::ServicePublished) return ROOMCUT_ENGINE_WAITING_DRIVER;
            break;

        case ROOMCUT_ENGINE_WAITING_DRIVER:
            if (ev == EngineEvent::HelloReceived) return ROOMCUT_ENGINE_CONNECTED;
            break;

        case ROOMCUT_ENGINE_CONNECTED:
            if (ev == EngineEvent::RegionCreated) return ROOMCUT_ENGINE_BUFFER_MAPPED;
            if (ev == EngineEvent::DriverLost)    return ROOMCUT_ENGINE_WAITING_DRIVER;
            break;

        case ROOMCUT_ENGINE_BUFFER_MAPPED:
            if (ev == EngineEvent::OutputOpened) return ROOMCUT_ENGINE_OUTPUT_READY;
            if (ev == EngineEvent::DriverLost)   return ROOMCUT_ENGINE_WAITING_DRIVER;
            break;

        case ROOMCUT_ENGINE_OUTPUT_READY:
            // Streaming begins once both the region and the output are live.
            if (ev == EngineEvent::OutputLost)  return ROOMCUT_ENGINE_RECOVERING;
            if (ev == EngineEvent::DriverLost)  return ROOMCUT_ENGINE_WAITING_DRIVER;
            // Treat a redundant OutputOpened as "start streaming".
            if (ev == EngineEvent::OutputReopened) return ROOMCUT_ENGINE_STREAMING;
            break;

        case ROOMCUT_ENGINE_STREAMING:
            if (ev == EngineEvent::OutputLost) return ROOMCUT_ENGINE_RECOVERING;
            if (ev == EngineEvent::DriverLost) return ROOMCUT_ENGINE_RECOVERING;
            break;

        case ROOMCUT_ENGINE_RECOVERING:
            if (ev == EngineEvent::OutputReopened) return ROOMCUT_ENGINE_STREAMING;
            if (ev == EngineEvent::OutputOpened)   return ROOMCUT_ENGINE_OUTPUT_READY;
            // DriverReturned means the stall watchdog saw writeIndex advance
            // again: the feed is live and the output stayed open through the
            // stall, so resume STREAMING directly (mirrors OutputReopened).
            // OUTPUT_READY would be a dead end here — its only exit to
            // STREAMING is an OutputReopened that nobody sends mid-session.
            if (ev == EngineEvent::DriverReturned) return ROOMCUT_ENGINE_STREAMING;
            if (ev == EngineEvent::DriverLost)     return ROOMCUT_ENGINE_WAITING_DRIVER;
            break;

        case ROOMCUT_ENGINE_STOPPING:
            break;
    }
    return state;
}

RoomcutEngineState engineWireState(RoomcutEngineLifecycle state) {
    switch (state) {
        case ROOMCUT_ENGINE_STREAMING:
            return ROOMCUT_STATE_RUNNING;
        case ROOMCUT_ENGINE_RECOVERING:
            return ROOMCUT_STATE_RECOVER;
        case ROOMCUT_ENGINE_STOPPING:
            return ROOMCUT_STATE_STOPPED;
        // Starting / WaitingDriver / Connected / BufferMapped / OutputReady are
        // all "not yet streaming" → STOPPED from the app's point of view.
        default:
            return ROOMCUT_STATE_STOPPED;
    }
}

RoomcutEngineState driverWireState(RoomcutDriverLifecycle state) {
    switch (state) {
        case ROOMCUT_DRIVER_STREAMING:
            return ROOMCUT_STATE_RUNNING;
        // §4.5: EngineLost and SafeBypass both surface as RECOVER — the app does
        // not need to tell them apart; the driver keeps the distinction itself.
        case ROOMCUT_DRIVER_ENGINE_LOST:
        case ROOMCUT_DRIVER_SAFE_BYPASS:
            return ROOMCUT_STATE_RECOVER;
        case ROOMCUT_DRIVER_UNLOADING:
            return ROOMCUT_STATE_STOPPED;
        default:
            return ROOMCUT_STATE_STOPPED;
    }
}

} // namespace roomcut
