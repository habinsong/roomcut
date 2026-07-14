/*
 * Control.hpp — the Phase 6 control plane between the app/CLI and the engine
 * (docs/03 "Control thread: … handles preset/device/bypass messages").
 *
 * Client side (roomcutctl, later the app): synchronous request/reply against
 * the engine's service port, same plumbing as heartbeatProbe — allocate a
 * reply port, send with a send-once reply right, receive with a timeout.
 *
 * Engine side: reply helpers given a received request. The engine's control
 * loop dispatches by msgh_id and answers on the request's reply port.
 *
 * Kept dependency-light (Mach + protocol headers); no CoreAudio.
 */
#ifndef ROOMCUT_CONTROL_HPP
#define ROOMCUT_CONTROL_HPP

#include <cstdint>

#include <mach/mach.h>

extern "C" {
#include "roomcut_handshake.h"
}

namespace roomcut {

// ---- Client side ----

// Ask the engine to apply a builtin preset. On KERN_SUCCESS, *outStatus is
// 0 (applied) or 1 (unknown preset id).
kern_return_t controlSetPreset(mach_port_t servicePort, const char* presetId,
                               uint32_t timeoutMs, uint32_t* outStatus);

// Pin the real output device by UID (empty/null = automatic policy).
kern_return_t controlSetDevice(mach_port_t servicePort, const char* deviceUID,
                               uint32_t timeoutMs, uint32_t* outStatus);

// Toggle manual bypass.
kern_return_t controlSetBypass(mach_port_t servicePort, bool bypass,
                               uint32_t timeoutMs, uint32_t* outStatus);

// Toggle "keep Roomcut as the system default output".
kern_return_t controlSetKeepDefault(mach_port_t servicePort, bool on,
                                    uint32_t timeoutMs, uint32_t* outStatus);

kern_return_t controlSetVolumeBoost(mach_port_t servicePort, double boost,
                                    uint32_t timeoutMs, uint32_t* outStatus);

// Apply a custom DSP parameter set (live EQ). Plain scalar args keep this
// header free of the core/dsp ChainParams dependency; `eqGainsDb` must point at
// ROOMCUT_EQ_BANDS gains, `parametric` at ROOMCUT_PARAM_BANDS bands (or null for
// none). On KERN_SUCCESS, *outStatus is 0 (applied).
kern_return_t controlSetParams(mach_port_t servicePort,
                               double preampDb, const double* eqGainsDb,
                               double limiterReleaseMs,
                               double outputGainDb, double spatialWidth,
                               double centerFocus, double crossfeed,
                               double roomReduce, double spatialMode,
                               double highpassHz, double compAmount,
                               const RoomcutParamBand* parametric,
                               uint32_t timeoutMs, uint32_t* outStatus);

// Fetch the engine status snapshot.
kern_return_t controlGetState(mach_port_t servicePort, uint32_t timeoutMs,
                              RoomcutStateReply* outReply);

kern_return_t controlGetParams(mach_port_t servicePort, uint32_t timeoutMs,
                               RoomcutGetParamsReply* outReply);

kern_return_t controlGetAnalysis(mach_port_t servicePort, uint32_t timeoutMs,
                                 RoomcutAnalysisReply* outReply);

// ---- Engine side ----

// Acknowledge a SET_* request (echoes msgType, carries status).
kern_return_t controlReplyAck(const mach_msg_header_t& requestHeader,
                              uint32_t msgType, uint32_t status);

// Answer a STATE request. `reply` must be fully filled by the caller except
// for the Mach header, which this sets up from the request.
kern_return_t controlReplyState(const RoomcutStateRequest& request,
                                RoomcutStateReply reply);

kern_return_t controlReplyParams(const RoomcutGetParamsRequest& request,
                                 RoomcutGetParamsReply reply);

kern_return_t controlReplyAnalysis(const RoomcutAnalysisRequest& request,
                                   RoomcutAnalysisReply reply);

} // namespace roomcut

#endif // ROOMCUT_CONTROL_HPP
