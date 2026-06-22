/*
 * Heartbeat.cpp — see Heartbeat.hpp.
 */
#include "Heartbeat.hpp"

#include <cstring>

namespace roomcut {

kern_return_t heartbeatProbe(mach_port_t peerHealthPort,
                             uint32_t sequence,
                             uint32_t timeoutMs,
                             uint32_t* outPeerState) {
    mach_port_t self = mach_task_self();

    mach_port_t replyPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    RoomcutHealthRequest req;
    std::memset(&req, 0, sizeof(req));
    req.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                                 MACH_MSG_TYPE_MAKE_SEND_ONCE);
    req.header.msgh_size        = sizeof(req);
    req.header.msgh_remote_port = peerHealthPort;
    req.header.msgh_local_port  = replyPort;
    req.header.msgh_id          = ROOMCUT_MSG_HEALTH_CHECK;
    req.msgType                 = ROOMCUT_MSG_HEALTH_CHECK;
    req.sequence                = sequence;

    // Send with a timeout too: if the peer's port is full/dead, don't block.
    kr = mach_msg(&req.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(req), 0,
                  MACH_PORT_NULL, timeoutMs, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return kr;
    }

    RoomcutHealthMsgBuffer buf;
    std::memset(&buf, 0, sizeof(buf));
    kr = mach_msg(&buf.reply.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(buf),
                  replyPort, timeoutMs, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return kr;   // MACH_RCV_TIMED_OUT here == peer lost
    }

    kern_return_t result = KERN_SUCCESS;
    if (buf.reply.msgType != ROOMCUT_MSG_HEALTH_CHECK ||
        buf.reply.sequence != sequence) {
        result = KERN_FAILURE;
    } else if (outPeerState) {
        *outPeerState = buf.reply.state;
    }

    mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
    return result;
}

kern_return_t heartbeatRespond(const RoomcutHealthRequest& request,
                               uint32_t state,
                               const uint32_t* availableRates,
                               uint32_t availableRateCount) {
    mach_port_t replyPort = request.header.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    RoomcutHealthReply reply;
    std::memset(&reply, 0, sizeof(reply));
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = ROOMCUT_MSG_HEALTH_CHECK;
    reply.msgType                 = ROOMCUT_MSG_HEALTH_CHECK;
    reply.sequence                = request.sequence;
    reply.state                   = state;

    reply.availableRateCount = 0;
    if (availableRates != nullptr && availableRateCount > 0) {
        uint32_t n = availableRateCount > ROOMCUT_MAX_RATES
                         ? (uint32_t)ROOMCUT_MAX_RATES : availableRateCount;
        for (uint32_t i = 0; i < n; ++i) reply.availableRates[i] = availableRates[i];
        reply.availableRateCount = n;
    }

    return mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

kern_return_t heartbeatReceive(mach_port_t healthPort,
                               uint32_t timeoutMs,
                               RoomcutHealthRequest* outRequest) {
    if (outRequest == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutHealthMsgBuffer buf;
    std::memset(&buf, 0, sizeof(buf));
    kern_return_t kr = mach_msg(&buf.request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                0, sizeof(buf), healthPort, timeoutMs, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.request.msgType != ROOMCUT_MSG_HEALTH_CHECK) {
        // Drain the reply port right we'd otherwise leak.
        if (buf.request.header.msgh_remote_port != MACH_PORT_NULL) {
            mach_msg_destroy(&buf.request.header);
        }
        return KERN_FAILURE;
    }
    *outRequest = buf.request;
    return KERN_SUCCESS;
}

} // namespace roomcut
