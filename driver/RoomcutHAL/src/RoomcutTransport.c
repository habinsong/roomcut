/*
 * RoomcutTransport.c — the AudioServerPlugIn side of the Mach/ring transport.
 *
 * All blocking work runs on a detached worker. The real-time callback sees only
 * an atomically published RoomcutRingHeader pointer protected by a small active
 * reader count, so a dead/restarted engine can never leave it dereferencing an
 * unmapped region.
 */
#include "RoomcutTransport.h"

#include <bootstrap.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include "roomcut_audio_format.h"
#include "roomcut_handshake.h"
#include "roomcut_ring.h"

#define ROOMCUT_CONNECT_TIMEOUT_MS 1000u
#define ROOMCUT_HEARTBEAT_TIMEOUT_MS 500u
#define ROOMCUT_RETRY_DELAY_MS 250u
#define ROOMCUT_HEARTBEAT_INTERVAL_MS 1000u

typedef struct {
    bool                  started;
    uint32_t              sampleRate;
    uint32_t              generation;
    uint32_t              activeWriters;
    RoomcutRingHeader*    header;
} RoomcutTransportShared;

static RoomcutTransportShared gTransport = {
    .started = false,
    .sampleRate = ROOMCUT_SR_48000,
    .generation = 1u,
    .activeWriters = 0u,
    .header = NULL
};

/* Set once during driver Initialize (before the worker starts), read only on
 * the worker thread → no synchronization needed. */
static Roomcut_RatesChangedFn gRatesChangedFn = NULL;

void Roomcut_TransportSetRatesChangedCallback(Roomcut_RatesChangedFn fn)
{
    gRatesChangedFn = fn;
}

/* Default policy: run the chain at the device's HIGHEST supported rate (never a
 * forced 48 kHz). `current` is intentionally ignored — the caller only invokes
 * this on a rate-LIST change (device connect/switch), so a rate the user picked
 * mid-session via the app/Audio MIDI Setup is never overridden. */
uint32_t Roomcut_PickNominalRate(uint32_t current, const uint32_t* rates, uint32_t count)
{
    (void)current;
    if (rates == NULL || count == 0u) return current;
    uint32_t highest = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        if (rates[i] > highest) highest = rates[i];
    }
    return highest != 0u ? highest : current;       /* best the device offers */
}

static void roomcut_sleep_ms(uint32_t milliseconds)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    nanosleep(&delay, NULL);
}

static mach_vm_size_t roomcut_round_to_page(uint64_t bytes)
{
    const uint64_t page = (uint64_t)getpagesize();
    return (mach_vm_size_t)((bytes + page - 1u) & ~(page - 1u));
}

void Roomcut_TransportDisconnect(RoomcutTransportConnection* connection)
{
    if (connection == NULL) return;

    mach_port_t self = mach_task_self();
    if (connection->mappedAddress != 0 && connection->mappedSize != 0) {
        mach_vm_deallocate(self, connection->mappedAddress, connection->mappedSize);
    }
    if (connection->memoryEntry != MACH_PORT_NULL) {
        mach_port_deallocate(self, connection->memoryEntry);
    }
    if (connection->servicePort != MACH_PORT_NULL) {
        mach_port_deallocate(self, connection->servicePort);
    }
    memset(connection, 0, sizeof(*connection));
}

kern_return_t Roomcut_TransportConnectToPort(
    mach_port_t servicePort,
    uint32_t sampleRate,
    uint32_t timeoutMs,
    RoomcutTransportConnection* outConnection)
{
    if (servicePort == MACH_PORT_NULL ||
        outConnection == NULL ||
        !roomcut_sr_supported(sampleRate)) {
        if (servicePort != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), servicePort);
        }
        return KERN_INVALID_ARGUMENT;
    }

    memset(outConnection, 0, sizeof(*outConnection));
    outConnection->servicePort = servicePort;

    mach_port_t self = mach_task_self();
    mach_port_t replyPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) {
        Roomcut_TransportDisconnect(outConnection);
        return kr;
    }

    RoomcutHelloRequest request;
    memset(&request, 0, sizeof(request));
    request.header.msgh_bits = MACH_MSGH_BITS(
        MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    request.header.msgh_size = sizeof(request);
    request.header.msgh_remote_port = servicePort;
    request.header.msgh_local_port = replyPort;
    request.header.msgh_id = ROOMCUT_MSG_HELLO;
    request.msgType = ROOMCUT_MSG_HELLO;
    request.protocolVersion = ROOMCUT_IPC_VERSION;
    request.requested.sampleRate = sampleRate;
    request.requested.channels = ROOMCUT_MVP_CHANNELS;
    request.requested.channelLayout = ROOMCUT_LAYOUT_STEREO;
    request.requested.internalFormat = ROOMCUT_INTERNAL_FORMAT_F32;
    request.requested.bufferFrameSize = 0u;
    request.requested.latencyClass = ROOMCUT_LATENCY_NORMAL;
    request.requested.capacityFrames = ROOMCUT_DEFAULT_CAPACITY_FRAMES;

    kr = mach_msg(
        &request.header,
        MACH_SEND_MSG | MACH_SEND_TIMEOUT,
        sizeof(request),
        0,
        MACH_PORT_NULL,
        timeoutMs,
        MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
        Roomcut_TransportDisconnect(outConnection);
        return kr;
    }

    RoomcutHelloMsgBuffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    kr = mach_msg(
        &buffer.reply.header,
        MACH_RCV_MSG | MACH_RCV_TIMEOUT,
        0,
        sizeof(buffer),
        replyPort,
        timeoutMs,
        MACH_PORT_NULL);
    mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
    if (kr != KERN_SUCCESS) {
        Roomcut_TransportDisconnect(outConnection);
        return kr;
    }

    /* Accept any reply that carries at least the fields up to (but not
     * including) the appended rate list, so a newer driver still maps a ring
     * from an older engine that doesn't send rates — rejecting it would loop
     * reconnect → silence. The rate list is read only when fully present. */
    const mach_msg_size_t baseReplySize =
        (mach_msg_size_t)offsetof(RoomcutHelloReply, availableRateCount);
    const bool validReply =
        buffer.reply.header.msgh_size >= baseReplySize &&
        (buffer.reply.header.msgh_bits & MACH_MSGH_BITS_COMPLEX) != 0 &&
        buffer.reply.body.msgh_descriptor_count == 1 &&
        buffer.reply.memoryEntry.type == MACH_MSG_PORT_DESCRIPTOR &&
        buffer.reply.memoryEntry.name != MACH_PORT_NULL &&
        buffer.reply.msgType == ROOMCUT_MSG_HELLO &&
        buffer.reply.status == 0 &&
        buffer.reply.granted.channels == ROOMCUT_MVP_CHANNELS &&
        roomcut_sr_supported(buffer.reply.granted.sampleRate) &&
        buffer.reply.granted.channelLayout == ROOMCUT_LAYOUT_STEREO &&
        buffer.reply.granted.internalFormat == ROOMCUT_INTERNAL_FORMAT_F32 &&
        roomcut_is_pow2(buffer.reply.granted.capacityFrames) &&
        buffer.reply.granted.capacityFrames >= 256u &&
        buffer.reply.granted.capacityFrames <= 65536u;
    if (!validReply) {
        mach_msg_destroy(&buffer.reply.header);
        Roomcut_TransportDisconnect(outConnection);
        return KERN_FAILURE;
    }

    outConnection->memoryEntry = buffer.reply.memoryEntry.name;
    outConnection->granted = buffer.reply.granted;

    /* Forwarded real-device rate list (only when the engine actually sent it). */
    outConnection->availableRateCount = 0u;
    if (buffer.reply.header.msgh_size >= (mach_msg_size_t)sizeof(RoomcutHelloReply)) {
        uint32_t rateCount = buffer.reply.availableRateCount;
        if (rateCount > ROOMCUT_MAX_RATES) rateCount = ROOMCUT_MAX_RATES;
        for (uint32_t i = 0; i < rateCount; ++i) {
            outConnection->availableRates[i] = buffer.reply.availableRates[i];
        }
        outConnection->availableRateCount = rateCount;
    }

    const uint64_t logicalBytes = roomcut_ring_region_bytes(
        outConnection->granted.capacityFrames,
        outConnection->granted.channels);
    const mach_vm_size_t mappedSize = roomcut_round_to_page(logicalBytes);
    mach_vm_address_t mappedAddress = 0;
    kr = mach_vm_map(
        self,
        &mappedAddress,
        mappedSize,
        0,
        VM_FLAGS_ANYWHERE,
        outConnection->memoryEntry,
        0,
        FALSE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS) {
        Roomcut_TransportDisconnect(outConnection);
        return kr;
    }

    outConnection->mappedAddress = mappedAddress;
    outConnection->mappedSize = mappedSize;
    outConnection->header = (RoomcutRingHeader*)mappedAddress;
    if (!roomcut_ring_validate(outConnection->header) ||
        outConnection->header->sampleRate != outConnection->granted.sampleRate ||
        outConnection->header->channels != outConnection->granted.channels ||
        outConnection->header->capacityFrames != outConnection->granted.capacityFrames) {
        Roomcut_TransportDisconnect(outConnection);
        return KERN_INVALID_ARGUMENT;
    }

    return KERN_SUCCESS;
}

static kern_return_t roomcut_probe_engine(
    mach_port_t servicePort,
    uint32_t sequence,
    uint32_t timeoutMs)
{
    mach_port_t self = mach_task_self();
    mach_port_t replyPort = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) return kr;

    RoomcutHealthRequest request;
    memset(&request, 0, sizeof(request));
    request.header.msgh_bits = MACH_MSGH_BITS(
        MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    request.header.msgh_size = sizeof(request);
    request.header.msgh_remote_port = servicePort;
    request.header.msgh_local_port = replyPort;
    request.header.msgh_id = ROOMCUT_MSG_HEALTH_CHECK;
    request.msgType = ROOMCUT_MSG_HEALTH_CHECK;
    request.sequence = sequence;

    kr = mach_msg(
        &request.header,
        MACH_SEND_MSG | MACH_SEND_TIMEOUT,
        sizeof(request),
        0,
        MACH_PORT_NULL,
        timeoutMs,
        MACH_PORT_NULL);
    if (kr == KERN_SUCCESS) {
        RoomcutHealthMsgBuffer buffer;
        memset(&buffer, 0, sizeof(buffer));
        kr = mach_msg(
            &buffer.reply.header,
            MACH_RCV_MSG | MACH_RCV_TIMEOUT,
            0,
            sizeof(buffer),
            replyPort,
            timeoutMs,
            MACH_PORT_NULL);
        if (kr == KERN_SUCCESS) {
            if (buffer.reply.msgType != ROOMCUT_MSG_HEALTH_CHECK ||
                buffer.reply.sequence != sequence) {
                kr = KERN_FAILURE;
            } else if (gRatesChangedFn != NULL &&
                       buffer.reply.header.msgh_size >= (mach_msg_size_t)sizeof(RoomcutHealthReply) &&
                       buffer.reply.availableRateCount > 0u) {
                /* The engine reports the real output device's rates each beat;
                 * re-run the rate policy so a live device switch (no re-HELLO)
                 * still corrects the nominal rate. The callback is a no-op unless
                 * the list actually changed. */
                uint32_t rc = buffer.reply.availableRateCount;
                if (rc > ROOMCUT_MAX_RATES) rc = ROOMCUT_MAX_RATES;
                gRatesChangedFn(buffer.reply.availableRates, rc);
            }
        }
    }

    mach_port_mod_refs(self, replyPort, MACH_PORT_RIGHT_RECEIVE, -1);
    return kr;
}

static mach_port_t roomcut_lookup_engine(void)
{
    mach_port_t bootstrapPort = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bootstrapPort) != KERN_SUCCESS ||
        bootstrapPort == MACH_PORT_NULL) {
        return MACH_PORT_NULL;
    }

    mach_port_t servicePort = MACH_PORT_NULL;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    kern_return_t kr = bootstrap_look_up(
        bootstrapPort,
        (char*)ROOMCUT_MACH_SERVICE_NAME,
        &servicePort);
#pragma clang diagnostic pop
    mach_port_deallocate(mach_task_self(), bootstrapPort);
    return kr == KERN_SUCCESS ? servicePort : MACH_PORT_NULL;
}

static void roomcut_retire_connection(RoomcutTransportConnection* connection)
{
    __atomic_store_n(&gTransport.header, NULL, __ATOMIC_RELEASE);
    while (__atomic_load_n(&gTransport.activeWriters, __ATOMIC_ACQUIRE) != 0u) {
        roomcut_sleep_ms(1u);
    }
    Roomcut_TransportDisconnect(connection);
}

static void* roomcut_transport_worker(void* unused)
{
    (void)unused;

    RoomcutTransportConnection connection;
    memset(&connection, 0, sizeof(connection));
    uint32_t connectedGeneration = 0u;
    uint32_t heartbeatSequence = 1u;

    for (;;) {
        const uint32_t wantedGeneration =
            __atomic_load_n(&gTransport.generation, __ATOMIC_ACQUIRE);
        const uint32_t sampleRate =
            __atomic_load_n(&gTransport.sampleRate, __ATOMIC_ACQUIRE);

        if (connection.header == NULL) {
            mach_port_t servicePort = roomcut_lookup_engine();
            if (servicePort == MACH_PORT_NULL) {
                roomcut_sleep_ms(ROOMCUT_RETRY_DELAY_MS);
                continue;
            }

            kern_return_t kr = Roomcut_TransportConnectToPort(
                servicePort,
                sampleRate,
                ROOMCUT_CONNECT_TIMEOUT_MS,
                &connection);
            if (kr != KERN_SUCCESS) {
                roomcut_sleep_ms(ROOMCUT_RETRY_DELAY_MS);
                continue;
            }

            if (__atomic_load_n(&gTransport.generation, __ATOMIC_ACQUIRE) !=
                wantedGeneration) {
                Roomcut_TransportDisconnect(&connection);
                continue;
            }
            connectedGeneration = wantedGeneration;
            __atomic_store_n(&gTransport.header, connection.header, __ATOMIC_RELEASE);

            /* Re-advertise the real output device's rates the engine just
             * reported, so coreaudiod can settle on a device-native rate. */
            if (gRatesChangedFn != NULL) {
                gRatesChangedFn(connection.availableRates, connection.availableRateCount);
            }
        }

        uint32_t waited = 0u;
        while (waited < ROOMCUT_HEARTBEAT_INTERVAL_MS) {
            roomcut_sleep_ms(ROOMCUT_RETRY_DELAY_MS);
            waited += ROOMCUT_RETRY_DELAY_MS;
            if (__atomic_load_n(&gTransport.generation, __ATOMIC_ACQUIRE) !=
                connectedGeneration) {
                break;
            }
        }

        if (__atomic_load_n(&gTransport.generation, __ATOMIC_ACQUIRE) !=
            connectedGeneration ||
            roomcut_probe_engine(
                connection.servicePort,
                heartbeatSequence++,
                ROOMCUT_HEARTBEAT_TIMEOUT_MS) != KERN_SUCCESS) {
            roomcut_retire_connection(&connection);
        }
    }

    return NULL;
}

void Roomcut_TransportStart(uint32_t sampleRate)
{
    Roomcut_TransportSetSampleRate(sampleRate);
    if (__atomic_exchange_n(&gTransport.started, true, __ATOMIC_ACQ_REL)) {
        return;
    }

    pthread_t worker;
    if (pthread_create(&worker, NULL, roomcut_transport_worker, NULL) != 0) {
        __atomic_store_n(&gTransport.started, false, __ATOMIC_RELEASE);
        return;
    }
    pthread_detach(worker);
}

void Roomcut_TransportSetSampleRate(uint32_t sampleRate)
{
    if (!roomcut_sr_supported(sampleRate)) {
        return;
    }
    uint32_t previous = __atomic_exchange_n(
        &gTransport.sampleRate, sampleRate, __ATOMIC_ACQ_REL);
    if (previous != sampleRate) {
        __atomic_add_fetch(&gTransport.generation, 1u, __ATOMIC_ACQ_REL);
    }
}

RoomcutRingHeader* Roomcut_TransportAcquireRing(void)
{
    __atomic_add_fetch(&gTransport.activeWriters, 1u, __ATOMIC_ACQUIRE);
    RoomcutRingHeader* header =
        __atomic_load_n(&gTransport.header, __ATOMIC_ACQUIRE);
    if (header == NULL) {
        __atomic_sub_fetch(&gTransport.activeWriters, 1u, __ATOMIC_RELEASE);
    }
    return header;
}

void Roomcut_TransportReleaseRing(void)
{
    __atomic_sub_fetch(&gTransport.activeWriters, 1u, __ATOMIC_RELEASE);
}
