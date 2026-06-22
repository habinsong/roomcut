/*
 * Control.cpp — see Control.hpp.
 */
#include "Control.hpp"

#include <cstring>

namespace roomcut {

namespace {

// Shared client plumbing: send `req` (already filled except the Mach header)
// to `servicePort` with a reply send-once right, then receive the reply into
// `buf`. Mirrors heartbeatProbe.
kern_return_t requestReply(mach_port_t servicePort,
                           mach_msg_header_t* req, mach_msg_size_t reqSize,
                           uint32_t msgId,
                           RoomcutControlMsgBuffer* buf,
                           uint32_t timeoutMs) {
    mach_port_t self = mach_task_self();

    mach_port_t replyPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    req->msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,
                                           MACH_MSG_TYPE_MAKE_SEND_ONCE);
    req->msgh_size        = reqSize;
    req->msgh_remote_port = servicePort;
    req->msgh_local_port  = replyPort;
    req->msgh_id          = (mach_msg_id_t)msgId;

    kr = mach_msg(req, MACH_SEND_MSG | MACH_SEND_TIMEOUT, reqSize, 0,
                  MACH_PORT_NULL, timeoutMs, MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        return kr;
    }

    std::memset(buf, 0, sizeof(*buf));
    kr = mach_msg(&buf->raw.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(*buf),
                  replyPort, timeoutMs, MACH_PORT_NULL);
    mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
    return kr;
}

} // namespace

kern_return_t controlSetPreset(mach_port_t servicePort, const char* presetId,
                               uint32_t timeoutMs, uint32_t* outStatus) {
    if (presetId == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutSetPresetRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_SET_PRESET;
    std::strncpy(req.presetId, presetId, ROOMCUT_PRESET_ID_MAX - 1);

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_PRESET, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_PRESET) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlSetDevice(mach_port_t servicePort, const char* deviceUID,
                               uint32_t timeoutMs, uint32_t* outStatus) {
    RoomcutSetDeviceRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_SET_OUTPUT_DEV;
    if (deviceUID != nullptr) {
        std::strncpy(req.deviceUID, deviceUID, ROOMCUT_DEVICE_UID_MAX - 1);
    }

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_OUTPUT_DEV, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_OUTPUT_DEV) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlSetBypass(mach_port_t servicePort, bool bypass,
                               uint32_t timeoutMs, uint32_t* outStatus) {
    RoomcutSetBypassRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_SET_BYPASS;
    req.bypass  = bypass ? 1u : 0u;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_BYPASS, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_BYPASS) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlSetKeepDefault(mach_port_t servicePort, bool on,
                                    uint32_t timeoutMs, uint32_t* outStatus) {
    RoomcutSetKeepDefaultRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_SET_KEEP_DEFAULT;
    req.on      = on ? 1u : 0u;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_KEEP_DEFAULT, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_KEEP_DEFAULT) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlSetVolumeBoost(mach_port_t servicePort, double boost,
                                    uint32_t timeoutMs, uint32_t* outStatus) {
    RoomcutSetVolumeBoostRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_SET_VOLUME_BOOST;
    req.boost   = boost;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_VOLUME_BOOST, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_VOLUME_BOOST) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlSetParams(mach_port_t servicePort,
                               double preampDb, const double* eqGainsDb,
                               double limiterReleaseMs,
                               double outputGainDb, double spatialWidth,
                               double centerFocus, double crossfeed,
                               double roomReduce, double spatialMode,
                               const RoomcutParamBand* parametric,
                               uint32_t timeoutMs, uint32_t* outStatus) {
    if (eqGainsDb == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutSetParamsRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType          = ROOMCUT_MSG_SET_PARAMS;
    req.preampDb         = preampDb;
    for (int b = 0; b < ROOMCUT_EQ_BANDS; ++b) {
        req.eqGainsDb[b] = eqGainsDb[b];
    }
    req.limiterReleaseMs = limiterReleaseMs;
    req.outputGainDb     = outputGainDb;
    req.spatialWidth     = spatialWidth;
    req.centerFocus      = centerFocus;
    req.crossfeed        = crossfeed;
    req.roomReduce       = roomReduce;
    req.spatialMode      = spatialMode;
    if (parametric != nullptr) {
        for (int b = 0; b < ROOMCUT_PARAM_BANDS; ++b) req.parametric[b] = parametric[b];
    }

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_SET_PARAMS, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.reply.msgType != ROOMCUT_MSG_SET_PARAMS) {
        return KERN_FAILURE;
    }
    if (outStatus) *outStatus = buf.reply.status;
    return KERN_SUCCESS;
}

kern_return_t controlGetState(mach_port_t servicePort, uint32_t timeoutMs,
                              RoomcutStateReply* outReply) {
    if (outReply == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutStateRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_STATE;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_STATE, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.stateReply.msgType != ROOMCUT_MSG_STATE) {
        return KERN_FAILURE;
    }
    *outReply = buf.stateReply;
    return KERN_SUCCESS;
}

kern_return_t controlGetParams(mach_port_t servicePort, uint32_t timeoutMs,
                               RoomcutGetParamsReply* outReply) {
    if (outReply == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutGetParamsRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_GET_PARAMS;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_GET_PARAMS, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.paramsReply.msgType != ROOMCUT_MSG_GET_PARAMS) {
        return KERN_FAILURE;
    }
    *outReply = buf.paramsReply;
    return KERN_SUCCESS;
}

kern_return_t controlGetAnalysis(mach_port_t servicePort, uint32_t timeoutMs,
                                 RoomcutAnalysisReply* outReply) {
    if (outReply == nullptr) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutAnalysisRequest req;
    std::memset(&req, 0, sizeof(req));
    req.msgType = ROOMCUT_MSG_GET_ANALYSIS;

    RoomcutControlMsgBuffer buf;
    kern_return_t kr = requestReply(servicePort, &req.header, sizeof(req),
                                    ROOMCUT_MSG_GET_ANALYSIS, &buf, timeoutMs);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (buf.analysisReply.msgType != ROOMCUT_MSG_GET_ANALYSIS) {
        return KERN_FAILURE;
    }
    *outReply = buf.analysisReply;
    return KERN_SUCCESS;
}

kern_return_t controlReplyAck(const mach_msg_header_t& requestHeader,
                              uint32_t msgType, uint32_t status) {
    mach_port_t replyPort = requestHeader.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    RoomcutControlReply reply;
    std::memset(&reply, 0, sizeof(reply));
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = (mach_msg_id_t)msgType;
    reply.msgType                 = msgType;
    reply.status                  = status;

    return mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

kern_return_t controlReplyState(const RoomcutStateRequest& request,
                                RoomcutStateReply reply) {
    mach_port_t replyPort = request.header.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = ROOMCUT_MSG_STATE;
    reply.msgType                 = ROOMCUT_MSG_STATE;

    return mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

kern_return_t controlReplyParams(const RoomcutGetParamsRequest& request,
                                 RoomcutGetParamsReply reply) {
    mach_port_t replyPort = request.header.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = ROOMCUT_MSG_GET_PARAMS;
    reply.msgType                 = ROOMCUT_MSG_GET_PARAMS;

    return mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

kern_return_t controlReplyAnalysis(const RoomcutAnalysisRequest& request,
                                   RoomcutAnalysisReply reply) {
    mach_port_t replyPort = request.header.msgh_remote_port;
    if (replyPort == MACH_PORT_NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    reply.header.msgh_bits        = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size        = sizeof(reply);
    reply.header.msgh_remote_port = replyPort;
    reply.header.msgh_local_port  = MACH_PORT_NULL;
    reply.header.msgh_id          = ROOMCUT_MSG_GET_ANALYSIS;
    reply.msgType                 = ROOMCUT_MSG_GET_ANALYSIS;

    return mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                    MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

} // namespace roomcut
