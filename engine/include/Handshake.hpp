/*
 * Handshake.hpp — the HELLO exchange that hands the ring region's memory-entry
 * send-right from the engine to the driver over a Mach service
 * (DEVELOPMENT_PLAN.md §4.1).
 *
 * Two halves, usable independently so each side lives in its own process:
 *   engineReplyHello() — engine side: given a received HELLO request (whose
 *       reply port we send to) and a created RingRegion, send the negotiated
 *       format + the memory-entry send-right as a Mach port descriptor.
 *   driverSendHelloAndReceive() — driver side: send a HELLO request to the
 *       engine's service port, receive the reply, and map the region.
 *
 * Kept dependency-light (Mach + the protocol headers); no CoreAudio.
 */
#ifndef ROOMCUT_HANDSHAKE_HPP
#define ROOMCUT_HANDSHAKE_HPP

#include <mach/mach.h>

#include "RingRegion.hpp"

extern "C" {
#include "roomcut_handshake.h"
}

namespace roomcut {

// Engine side: reply to a received HELLO `request` by sending `region`'s
// memory-entry send-right (as a port descriptor) plus the granted format to
// request.header.msgh_remote_port. Does not consume the region's right (the
// descriptor COPY_SEND makes the kernel mint a new reference for the receiver).
// `availableRates`/`count` (optional) are the real output device's supported
// nominal sample rates, forwarded so the driver can advertise them.
kern_return_t engineReplyHello(const RoomcutHelloRequest& request,
                               const RingRegion& region,
                               const RoomcutFormatNegotiation& granted,
                               const uint32_t* availableRates = nullptr,
                               uint32_t availableRateCount = 0);

// Driver side: send a HELLO to `servicePort` requesting `requested`, wait for
// the reply, and map the received region into `outRegion`. On success
// outRegion.valid() is true and *outGranted holds the engine's format.
// If outRates/outRateCount are non-null they receive the engine's advertised
// real-device rate list (outRateCount capped at ROOMCUT_MAX_RATES).
kern_return_t driverSendHelloAndReceive(mach_port_t servicePort,
                                        const RoomcutFormatNegotiation& requested,
                                        RingRegion& outRegion,
                                        RoomcutFormatNegotiation* outGranted,
                                        uint32_t* outRates = nullptr,
                                        uint32_t* outRateCount = nullptr);

} // namespace roomcut

#endif // ROOMCUT_HANDSHAKE_HPP
