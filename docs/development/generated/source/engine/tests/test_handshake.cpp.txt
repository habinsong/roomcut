/*
 * test_handshake.cpp — end-to-end verification of the HELLO handoff over a real
 * Mach message: the engine sends the ring region's memory-entry send-right as a
 * port descriptor, the driver receives it via mach_msg and maps the SAME pages.
 *
 * Unlike test_ring_region (which bumped a refcount in-process to model the
 * handoff), this drives the actual IPC path: a service port, mach_msg send/recv
 * across two threads, and a port descriptor transfer. This is the real Phase 3
 * §4.1 transport, minus only the cross-process boundary (which needs the driver
 * loaded in coreaudiod).
 *
 *   engine thread:  allocate service port → recv HELLO → create RingRegion →
 *                   engineReplyHello → wait → read frames the driver wrote
 *   driver thread:  production C transport HELLO/map → write a ramp
 */
#include "Handshake.hpp"
#include "RingRegion.hpp"
#include "RoomcutTransport.h"

#include <cstdio>
#include <cstring>
#include <pthread.h>

#include <mach/mach.h>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

static const uint32_t kCap = 1024;
static const uint32_t kCh  = ROOMCUT_MVP_CHANNELS;
static const uint32_t kFrames = 256;

// A stand-in for a real output device's advertised rate list, forwarded by the
// engine in the HELLO reply and expected verbatim on the driver side.
static const uint32_t kDevRates[] = {ROOMCUT_SR_44100, ROOMCUT_SR_48000, ROOMCUT_SR_96000};
static const uint32_t kDevRateCount = 3;

// Shared between threads: the engine publishes its service port name here once
// allocated, so the driver knows where to send. Guarded by a simple gate.
struct Shared {
    mach_port_t servicePort;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    bool serviceReady;
    bool driverDone;
    int  driverWrote;
};

static void* engineThread(void* arg) {
    Shared* s = (Shared*)arg;
    mach_port_t self = mach_task_self();

    // Allocate the service port (receive right) + a send right to it.
    mach_port_t service = MACH_PORT_NULL;
    CHECK(mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &service) == KERN_SUCCESS, "engine alloc service port");
    CHECK(mach_port_insert_right(self, service, service, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "engine insert send right");

    pthread_mutex_lock(&s->mtx);
    s->servicePort = service;
    s->serviceReady = true;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mtx);

    // Receive the HELLO request.
    RoomcutHelloMsgBuffer buf;
    std::memset(&buf, 0, sizeof(buf));
    kern_return_t kr = mach_msg(&buf.request.header, MACH_RCV_MSG, 0, sizeof(buf),
                                service, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    CHECK(kr == KERN_SUCCESS, "engine recv HELLO");
    CHECK(buf.request.msgType == ROOMCUT_MSG_HELLO, "engine got HELLO type");
    CHECK(buf.request.protocolVersion == ROOMCUT_IPC_VERSION, "engine protocol version ok");

    // Create the ring region and reply with the negotiated format + send-right.
    RingRegion region;
    CHECK(region.create(kCap, kCh, ROOMCUT_SR_48000) == KERN_SUCCESS, "engine create region");

    RoomcutFormatNegotiation granted;
    std::memset(&granted, 0, sizeof(granted));
    granted.sampleRate     = ROOMCUT_SR_48000;
    granted.channels       = kCh;
    granted.channelLayout  = ROOMCUT_LAYOUT_STEREO;
    granted.internalFormat = ROOMCUT_INTERNAL_FORMAT_F32;
    granted.capacityFrames = kCap;

    kr = engineReplyHello(buf.request, region, granted, kDevRates, kDevRateCount);
    CHECK(kr == KERN_SUCCESS, "engine reply HELLO");

    // The send-once reply right was consumed by the reply; drop the request's
    // remote port bookkeeping is handled by the kernel. Now wait for the driver
    // to write, then read it back through our own mapping.
    pthread_mutex_lock(&s->mtx);
    while (!s->driverDone) pthread_cond_wait(&s->cv, &s->mtx);
    pthread_mutex_unlock(&s->mtx);

    CHECK(s->driverWrote == (int)kFrames, "driver reported full write");
    CHECK(roomcut_ring_readable(region.header()) == kFrames, "engine sees frames via shared pages");

    roomcut_sample_t out[kFrames * ROOMCUT_MVP_CHANNELS];
    uint32_t got = roomcut_ring_read(region.header(), out, kFrames);
    CHECK(got == kFrames, "engine reads all frames");

    bool dataOk = true;
    for (uint32_t f = 0; f < kFrames && dataOk; ++f) {
        if (out[f * kCh] != (float)(7000 + f)) dataOk = false;
    }
    CHECK(dataOk, "engine sees driver's exact data across the handoff");

    region.destroy();
    mach_port_mod_refs(self, service, MACH_PORT_RIGHT_RECEIVE, -1);
    return nullptr;
}

static void* driverThread(void* arg) {
    Shared* s = (Shared*)arg;

    pthread_mutex_lock(&s->mtx);
    while (!s->serviceReady) pthread_cond_wait(&s->cv, &s->mtx);
    mach_port_t service = s->servicePort;
    pthread_mutex_unlock(&s->mtx);

    RoomcutTransportConnection driverConnection;
    std::memset(&driverConnection, 0, sizeof(driverConnection));
    kern_return_t kr = Roomcut_TransportConnectToPort(
        service, ROOMCUT_SR_48000, 2000, &driverConnection);
    CHECK(kr == KERN_SUCCESS, "driver handshake + map");
    CHECK(driverConnection.header != nullptr, "driver region valid");
    CHECK(driverConnection.granted.sampleRate == ROOMCUT_SR_48000, "driver got granted SR");
    CHECK(driverConnection.granted.capacityFrames == kCap, "driver got granted capacity");

    // The forwarded real-device rate list must survive the round-trip verbatim.
    CHECK(driverConnection.availableRateCount == kDevRateCount, "driver got forwarded rate count");
    bool ratesOk = (driverConnection.availableRateCount == kDevRateCount);
    for (uint32_t i = 0; i < driverConnection.availableRateCount && ratesOk; ++i) {
        if (driverConnection.availableRates[i] != kDevRates[i]) ratesOk = false;
    }
    CHECK(ratesOk, "driver got exact forwarded rate list");

    int wrote = 0;
    if (driverConnection.header != nullptr) {
        roomcut_sample_t in[kFrames * ROOMCUT_MVP_CHANNELS];
        for (uint32_t f = 0; f < kFrames; ++f)
            for (uint32_t c = 0; c < kCh; ++c)
                in[f * kCh + c] = (float)(7000 + f);
        wrote = (int)roomcut_ring_write(driverConnection.header, in, kFrames, 0);
    }

    pthread_mutex_lock(&s->mtx);
    s->driverWrote = wrote;
    s->driverDone = true;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mtx);

    Roomcut_TransportDisconnect(&driverConnection);
    return nullptr;
}

// Pure rate-selection policy used by the driver to correct an unsupported
// nominal rate (the muffling fix) — verified here without coreaudiod.
static void testPickNominalRate() {
    // Default policy: always the device's HIGHEST supported rate, regardless of
    // the current rate (the caller only invokes this on a rate-LIST change, so a
    // user's mid-session pick is never overridden). `current` is only a fallback
    // when no rates are reported.
    uint32_t r1[] = {ROOMCUT_SR_44100, ROOMCUT_SR_48000, ROOMCUT_SR_96000};
    CHECK(Roomcut_PickNominalRate(ROOMCUT_SR_48000, r1, 3) == ROOMCUT_SR_96000, "upgrade to highest supported");
    CHECK(Roomcut_PickNominalRate(ROOMCUT_SR_96000, r1, 3) == ROOMCUT_SR_96000, "stay at highest supported");
    uint32_t airpods[] = {ROOMCUT_SR_48000};
    CHECK(Roomcut_PickNominalRate(ROOMCUT_SR_192000, airpods, 1) == ROOMCUT_SR_48000, "48k-only device → 48k");
    uint32_t r3[] = {ROOMCUT_SR_44100, ROOMCUT_SR_48000};
    CHECK(Roomcut_PickNominalRate(ROOMCUT_SR_192000, r3, 2) == ROOMCUT_SR_48000, "pick highest offered");
    CHECK(Roomcut_PickNominalRate(ROOMCUT_SR_96000, nullptr, 0) == ROOMCUT_SR_96000, "keep current when none reported");
}

int main() {
    testPickNominalRate();

    Shared s;
    std::memset(&s, 0, sizeof(s));
    pthread_mutex_init(&s.mtx, nullptr);
    pthread_cond_init(&s.cv, nullptr);

    pthread_t eng, drv;
    pthread_create(&eng, nullptr, engineThread, &s);
    pthread_create(&drv, nullptr, driverThread, &s);
    pthread_join(drv, nullptr);
    pthread_join(eng, nullptr);

    pthread_mutex_destroy(&s.mtx);
    pthread_cond_destroy(&s.cv);

    if (g_failures == 0) { printf("all handshake tests passed\n"); return 0; }
    fprintf(stderr, "%d handshake check(s) failed\n", g_failures);
    return 1;
}
