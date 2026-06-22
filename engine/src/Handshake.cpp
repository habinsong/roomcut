/*
 * Handshake.cpp — see Handshake.hpp.
 */
#include "Handshake.hpp"

#include <cstring>

namespace roomcut {

kern_return_t engineReplyHello(const RoomcutHelloRequest& request,
                               const RingRegion& region,
                               const RoomcutFormatNegotiation& granted,
                               const uint32_t* availableRates,
                               uint32_t availableRateCount) {
    if (!region.valid() || region.memoryEntry() == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    mach_port_t replyPort = request.header.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    RoomcutHelloReply reply;
    std::memset(&reply, 0, sizeof(reply));

    // Complex message: header bits MACH_MSGH_BITS_COMPLEX, one descriptor.
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0)
                                  | MACH_MSGH_BITS_COMPLEX;
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;          // send the reply here
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = ROOMCUT_MSG_HELLO;

    reply.body.msgh_descriptor_count = 1;
    reply.memoryEntry.name        = region.memoryEntry();
    reply.memoryEntry.disposition = MACH_MSG_TYPE_COPY_SEND; // mint a ref for receiver
    reply.memoryEntry.type        = MACH_MSG_PORT_DESCRIPTOR;

    reply.msgType = ROOMCUT_MSG_HELLO;
    reply.status  = 0;
    reply.granted = granted;

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

kern_return_t driverSendHelloAndReceive(mach_port_t servicePort,
                                        const RoomcutFormatNegotiation& requested,
                                        RingRegion& outRegion,
                                        RoomcutFormatNegotiation* outGranted,
                                        uint32_t* outRates,
                                        uint32_t* outRateCount) {
    mach_port_t self = mach_task_self();

    // Make a reply port (receive right) the engine will send its reply to.
    mach_port_t replyPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    // Send the HELLO request, carrying a send-once right to our reply port.
    RoomcutHelloRequest req;
    std::memset(&req, 0, sizeof(req));
    req.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                                 MACH_MSG_TYPE_MAKE_SEND_ONCE);
    req.header.msgh_size        = sizeof(req);
    req.header.msgh_remote_port = servicePort;   // destination: engine service
    req.header.msgh_local_port  = replyPort;     // engine replies here
    req.header.msgh_id          = ROOMCUT_MSG_HELLO;
    req.msgType                 = ROOMCUT_MSG_HELLO;
    req.protocolVersion         = ROOMCUT_IPC_VERSION;
    req.requested               = requested;

    kr = mach_msg(&req.header, MACH_SEND_MSG, sizeof(req), 0,
                  MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return kr;
    }

    // Receive the reply (with the memory-entry port descriptor).
    RoomcutHelloMsgBuffer buf;
    std::memset(&buf, 0, sizeof(buf));
    kr = mach_msg(&buf.reply.header, MACH_RCV_MSG, 0, sizeof(buf),
                  replyPort, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return kr;
    }

    // Validate it's a complex HELLO reply with our descriptor.
    if (!(buf.reply.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) ||
        buf.reply.body.msgh_descriptor_count != 1 ||
        buf.reply.memoryEntry.type != MACH_MSG_PORT_DESCRIPTOR ||
        buf.reply.msgType != ROOMCUT_MSG_HELLO ||
        buf.reply.status != 0) {
        // Drop any received right to avoid leaking it.
        if (buf.reply.body.msgh_descriptor_count == 1 &&
            buf.reply.memoryEntry.name != MACH_PORT_NULL) {
            mach_port_deallocate(self, buf.reply.memoryEntry.name);
        }
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return KERN_FAILURE;
    }

    mach_port_t receivedEntry = buf.reply.memoryEntry.name;
    if (outGranted) {
        *outGranted = buf.reply.granted;
    }
    if (outRateCount) {
        uint32_t n = buf.reply.availableRateCount;
        if (n > ROOMCUT_MAX_RATES) n = ROOMCUT_MAX_RATES;
        if (outRates) {
            for (uint32_t i = 0; i < n; ++i) outRates[i] = buf.reply.availableRates[i];
        }
        *outRateCount = n;
    }

    // Map the received entry. mapFromPort takes ownership of the right.
    kr = outRegion.mapFromPort(receivedEntry);

    mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
    return kr;
}

} // namespace roomcut
