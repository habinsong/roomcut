/*
 * Heartbeat.hpp — liveness probing between engine and driver
 * (DEVELOPMENT_PLAN.md §4.1 "heartbeat both ways", §4.2 timeout → EngineLost).
 *
 * Direction-agnostic: whichever side is probing calls heartbeatProbe() against
 * the peer's health port and gets back the peer's state, or a timeout error
 * meaning the peer is gone. The responding side calls heartbeatRespond() once
 * it has received a request on its health port.
 *
 * Kept dependency-light (Mach + protocol headers); no CoreAudio.
 */
#ifndef ROOMCUT_HEARTBEAT_HPP
#define ROOMCUT_HEARTBEAT_HPP

#include <cstdint>

#include <mach/mach.h>

extern "C" {
#include "roomcut_handshake.h"
}

namespace roomcut {

// Prober side: send a HEALTH_CHECK with `sequence` to `peerHealthPort` and wait
// up to `timeoutMs` for the matching reply. On success returns KERN_SUCCESS and
// fills *outPeerState (a RoomcutEngineState). On a missed reply returns
// MACH_RCV_TIMED_OUT — the caller treats that as "peer lost".
kern_return_t heartbeatProbe(mach_port_t peerHealthPort,
                             uint32_t sequence,
                             uint32_t timeoutMs,
                             uint32_t* outPeerState);

// Responder side: given a received HEALTH_CHECK `request`, reply with `state`
// and the echoed sequence to the request's reply port. `availableRates`/`count`
// (optional) carry the real output device's supported rates so the driver can
// correct its nominal rate after a live device switch (capped at ROOMCUT_MAX_RATES).
kern_return_t heartbeatRespond(const RoomcutHealthRequest& request,
                               uint32_t state,
                               const uint32_t* availableRates = nullptr,
                               uint32_t availableRateCount = 0);

// Convenience for the responder: block up to `timeoutMs` to receive one
// HEALTH_CHECK on `healthPort` into `outRequest`. Returns MACH_RCV_TIMED_OUT if
// none arrived. (A real engine runs this on its control thread in a loop.)
kern_return_t heartbeatReceive(mach_port_t healthPort,
                               uint32_t timeoutMs,
                               RoomcutHealthRequest* outRequest);

} // namespace roomcut

#endif // ROOMCUT_HEARTBEAT_HPP
