#include "Control.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <pthread.h>

#include <mach/mach.h>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

namespace {

struct Shared {
    mach_port_t servicePort;
    pthread_mutex_t mtx;
    pthread_cond_t cv;
    bool serviceReady;
};

RoomcutGetParamsReply makeParamsReply(const mach_msg_header_t& requestHeader) {
    RoomcutGetParamsReply reply;
    std::memset(&reply, 0, sizeof(reply));
    reply.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    reply.header.msgh_size = sizeof(reply);
    reply.header.msgh_remote_port = requestHeader.msgh_remote_port;
    reply.header.msgh_local_port = MACH_PORT_NULL;
    reply.header.msgh_id = ROOMCUT_MSG_GET_PARAMS;
    reply.msgType = ROOMCUT_MSG_GET_PARAMS;
    std::snprintf(reply.presetId, sizeof(reply.presetId), "%s", "custom");
    reply.paramsRevision = 7;
    reply.preampDb = -3.0;
    for (int i = 0; i < ROOMCUT_EQ_BANDS; ++i) {
        reply.eqGainsDb[i] = (double)i - 4.0;
    }
    reply.limiterReleaseMs = 100.0;
    reply.outputGainDb = 2.5;
    reply.spatialWidth = -35.0;
    reply.centerFocus = 28.0;
    reply.crossfeed = 12.0;
    reply.roomReduce = 55.0;
    reply.parametric[2].enabled = 1;
    reply.parametric[2].type = 2; // HighShelf
    reply.parametric[2].freqHz = 8000.0;
    reply.parametric[2].gainDb = 3.5;
    reply.parametric[2].q = 0.9;
    return reply;
}

void* serverThread(void* arg) {
    Shared* shared = static_cast<Shared*>(arg);
    mach_port_t self = mach_task_self();

    mach_port_t service = MACH_PORT_NULL;
    CHECK(mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &service) == KERN_SUCCESS,
          "server alloc service port");
    CHECK(mach_port_insert_right(self, service, service, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS,
          "server insert send right");

    pthread_mutex_lock(&shared->mtx);
    shared->servicePort = service;
    shared->serviceReady = true;
    pthread_cond_signal(&shared->cv);
    pthread_mutex_unlock(&shared->mtx);

    for (int i = 0; i < 5; ++i) {
        RoomcutControlMsgBuffer buf;
        std::memset(&buf, 0, sizeof(buf));
        kern_return_t kr = mach_msg(&buf.raw.header, MACH_RCV_MSG, 0, sizeof(buf),
                                    service, 2000, MACH_PORT_NULL);
        CHECK(kr == KERN_SUCCESS, "server received control message");
        switch (buf.raw.header.msgh_id) {
            case ROOMCUT_MSG_STATE: {
                RoomcutStateReply reply;
                std::memset(&reply, 0, sizeof(reply));
                reply.state = ROOMCUT_STATE_RUNNING;
                std::snprintf(reply.presetId, sizeof(reply.presetId), "%s", "custom");
                reply.paramsRevision = 7;
                reply.capabilities = ROOMCUT_CAP_SPATIAL_PARAMS;
                kr = roomcut::controlReplyState(buf.stateRequest, reply);
                CHECK(kr == KERN_SUCCESS, "server replied state");
                break;
            }
            case ROOMCUT_MSG_GET_PARAMS: {
                RoomcutGetParamsReply reply = makeParamsReply(buf.raw.header);
                kr = mach_msg(&reply.header, MACH_SEND_MSG, sizeof(reply), 0,
                              MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
                CHECK(kr == KERN_SUCCESS, "server replied params");
                break;
            }
            case ROOMCUT_MSG_GET_ANALYSIS: {
                RoomcutAnalysisReply reply;
                std::memset(&reply, 0, sizeof(reply));
                reply.valid = 1;
                reply.sampleRate = 48000;
                reply.channels = 2;
                reply.framesAnalyzed = 4096;
                reply.peakDb = -2.0f;
                reply.rmsDb = -18.0f;
                reply.stereoWidth = 0.75f;
                reply.spectrum[3] = 0.5f;
                kr = roomcut::controlReplyAnalysis(buf.analysisRequest, reply);
                CHECK(kr == KERN_SUCCESS, "server replied analysis");
                break;
            }
            case ROOMCUT_MSG_SET_PARAMS: {
                CHECK(std::fabs(buf.setParams.preampDb - -6.0) < 0.001, "server received preamp");
                CHECK(std::fabs(buf.setParams.eqGainsDb[9] - 9.0) < 0.001, "server received eq");
                CHECK(std::fabs(buf.setParams.spatialWidth - -25.0) < 0.001, "server received width");
                CHECK(std::fabs(buf.setParams.centerFocus - 30.0) < 0.001, "server received center");
                CHECK(std::fabs(buf.setParams.crossfeed - 10.0) < 0.001, "server received crossfeed");
                CHECK(std::fabs(buf.setParams.roomReduce - 45.0) < 0.001, "server received room");
                CHECK(buf.setParams.parametric[1].enabled == 1, "server received parametric enabled");
                CHECK(buf.setParams.parametric[1].type == 0, "server received parametric type");
                CHECK(std::fabs(buf.setParams.parametric[1].freqHz - 1200.0) < 0.001, "server received parametric freq");
                CHECK(std::fabs(buf.setParams.parametric[1].gainDb - 5.0) < 0.001, "server received parametric gain");
                kr = roomcut::controlReplyAck(buf.setParams.header, ROOMCUT_MSG_SET_PARAMS, 0);
                CHECK(kr == KERN_SUCCESS, "server replied set params");
                break;
            }
            case ROOMCUT_MSG_SET_VOLUME_BOOST: {
                CHECK(std::fabs(buf.setVolumeBoost.boost - 1.5) < 0.001, "server received volume boost");
                kr = roomcut::controlReplyAck(buf.setVolumeBoost.header, ROOMCUT_MSG_SET_VOLUME_BOOST, 0);
                CHECK(kr == KERN_SUCCESS, "server replied volume boost");
                break;
            }
            default:
                CHECK(false, "unexpected control message");
                mach_msg_destroy(&buf.raw.header);
                break;
        }
    }

    mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
    return nullptr;
}

}

int main() {
    Shared shared;
    std::memset(&shared, 0, sizeof(shared));
    pthread_mutex_init(&shared.mtx, nullptr);
    pthread_cond_init(&shared.cv, nullptr);

    pthread_t server;
    pthread_create(&server, nullptr, serverThread, &shared);

    pthread_mutex_lock(&shared.mtx);
    while (!shared.serviceReady) {
        pthread_cond_wait(&shared.cv, &shared.mtx);
    }
    mach_port_t service = shared.servicePort;
    pthread_mutex_unlock(&shared.mtx);

    RoomcutStateReply state;
    std::memset(&state, 0, sizeof(state));
    kern_return_t kr = roomcut::controlGetState(service, 2000, &state);
    CHECK(kr == KERN_SUCCESS, "client got state");
    CHECK(state.paramsRevision == 7, "state carries params revision");
    CHECK((state.capabilities & ROOMCUT_CAP_SPATIAL_PARAMS) != 0, "state carries spatial capability");

    uint32_t status = 1;
    kr = roomcut::controlSetVolumeBoost(service, 1.5, 2000, &status);
    CHECK(kr == KERN_SUCCESS, "client set volume boost");
    CHECK(status == 0, "set volume boost status");

    RoomcutGetParamsReply params;
    std::memset(&params, 0, sizeof(params));
    kr = roomcut::controlGetParams(service, 2000, &params);
    CHECK(kr == KERN_SUCCESS, "client got params");
    CHECK(std::strcmp(params.presetId, "custom") == 0, "params preset id");
    CHECK(params.paramsRevision == 7, "params revision");
    CHECK(std::fabs(params.preampDb - -3.0) < 0.001, "preamp readback");
    CHECK(std::fabs(params.eqGainsDb[9] - 5.0) < 0.001, "eq readback");
    CHECK(std::fabs(params.outputGainDb - 2.5) < 0.001, "output gain readback");
    CHECK(std::fabs(params.spatialWidth - -35.0) < 0.001, "width readback");
    CHECK(std::fabs(params.centerFocus - 28.0) < 0.001, "center readback");
    CHECK(std::fabs(params.crossfeed - 12.0) < 0.001, "crossfeed readback");
    CHECK(std::fabs(params.roomReduce - 55.0) < 0.001, "room readback");
    CHECK(params.parametric[2].enabled == 1, "parametric enabled readback");
    CHECK(params.parametric[2].type == 2, "parametric type readback");
    CHECK(std::fabs(params.parametric[2].freqHz - 8000.0) < 0.001, "parametric freq readback");
    CHECK(std::fabs(params.parametric[2].gainDb - 3.5) < 0.001, "parametric gain readback");

    RoomcutAnalysisReply analysis;
    std::memset(&analysis, 0, sizeof(analysis));
    kr = roomcut::controlGetAnalysis(service, 2000, &analysis);
    CHECK(kr == KERN_SUCCESS, "client got analysis");
    CHECK(analysis.valid == 1, "analysis valid");
    CHECK(analysis.sampleRate == 48000, "analysis sample rate");
    CHECK(std::fabs(analysis.stereoWidth - 0.75f) < 0.001f, "analysis width");
    CHECK(std::fabs(analysis.spectrum[3] - 0.5f) < 0.001f, "analysis spectrum");

    double gains[ROOMCUT_EQ_BANDS];
    for (int i = 0; i < ROOMCUT_EQ_BANDS; ++i) {
        gains[i] = (double)i;
    }
    RoomcutParamBand sendBands[ROOMCUT_PARAM_BANDS];
    std::memset(sendBands, 0, sizeof(sendBands));
    sendBands[1].enabled = 1;
    sendBands[1].type = 0; // Bell
    sendBands[1].freqHz = 1200.0;
    sendBands[1].gainDb = 5.0;
    sendBands[1].q = 1.4;
    kr = roomcut::controlSetParams(service, -6.0, gains, 75.0, 1.0,
                                   -25.0, 30.0, 10.0, 45.0, 1.0 /* mode */, sendBands, 2000, &status);
    CHECK(kr == KERN_SUCCESS, "client set params");
    CHECK(status == 0, "set params status");

    pthread_join(server, nullptr);
    mach_port_deallocate(mach_task_self(), service);
    pthread_mutex_destroy(&shared.mtx);
    pthread_cond_destroy(&shared.cv);

    if (g_failures == 0) {
        std::printf("all control tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d control check(s) failed\n", g_failures);
    return 1;
}
