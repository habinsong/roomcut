#include "Handshake.hpp"
#include "RoomcutTransport.h"
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

using namespace roomcut;
static std::atomic<int> failures{0};
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static unsigned refs(mach_port_t port, mach_port_right_t kind = MACH_PORT_RIGHT_SEND) {
    mach_port_urefs_t count = 0;
    mach_port_get_refs(mach_task_self(), port, kind, &count);
    return count;
}

struct Port {
    mach_port_t name = MACH_PORT_NULL;
    Port() { CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &name) == KERN_SUCCESS, "allocate port"); }
    mach_port_t sendRight() {
        CHECK(mach_port_insert_right(mach_task_self(), name, name, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "create owned send right");
        return name;
    }
    void close() {
        if (MACH_PORT_VALID(name)) {
            const auto count = refs(name);
            if (count) mach_port_mod_refs(mach_task_self(), name, MACH_PORT_RIGHT_SEND, -count);
            mach_port_mod_refs(mach_task_self(), name, MACH_PORT_RIGHT_RECEIVE, -1);
            name = MACH_PORT_NULL;
        }
    }
    ~Port() { close(); }
};

static RoomcutFormatNegotiation format() {
    RoomcutFormatNegotiation value{};
    value.sampleRate = 48000;
    value.channels = ROOMCUT_MVP_CHANNELS;
    value.capacityFrames = 1024;
    return value;
}

enum class ReplyCase { current, legacy, partialRates, truncated, wrongID, wrongType,
    errorStatus, wrongChannels, wrongRate, wrongLayout, wrongFormat, wrongCapacity,
    mismatchedHeader, invalidHeader, shortMapping, extraRight, twoDescriptors, nonMemoryPort };

static void replyBoundaries(bool driver, bool reconnect = false) {
    for (auto kind : {ReplyCase::current, ReplyCase::legacy, ReplyCase::partialRates,
        ReplyCase::truncated, ReplyCase::wrongID, ReplyCase::wrongType, ReplyCase::errorStatus,
        ReplyCase::wrongChannels, ReplyCase::wrongRate, ReplyCase::wrongLayout,
        ReplyCase::wrongFormat, ReplyCase::wrongCapacity, ReplyCase::mismatchedHeader,
        ReplyCase::invalidHeader, ReplyCase::shortMapping, ReplyCase::extraRight,
        ReplyCase::twoDescriptors, ReplyCase::nonMemoryPort}) {
        std::printf("reply case=%d driver=%d reconnect=%d\n", static_cast<int>(kind), driver, reconnect);
        Port service, extra;
        RingRegion backing;
        CHECK(backing.create(1024, 2, 48000) == KERN_SUCCESS, "create engine ring");
        if (kind == ReplyCase::invalidHeader) backing.header()->magic = 0;
        if (kind == ReplyCase::shortMapping) backing.header()->capacityFrames = 65536;
        const auto memoryRefs = refs(backing.memoryEntry());
        const auto endpoint = service.sendRight();
        std::thread server([&] {
            RoomcutHelloMsgBuffer request{};
            const auto received = mach_msg(&request.request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0, sizeof(request), service.name, 2000, MACH_PORT_NULL);
            CHECK(received == KERN_SUCCESS, "receive HELLO");
            if (received != KERN_SUCCESS) return;
            RoomcutHelloReply reply{};
            reply.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0) | MACH_MSGH_BITS_COMPLEX;
            reply.header.msgh_remote_port = request.request.header.msgh_remote_port;
            reply.header.msgh_id = ROOMCUT_MSG_HELLO;
            reply.body.msgh_descriptor_count = 1;
            reply.memoryEntry.name = backing.memoryEntry();
            reply.memoryEntry.disposition = MACH_MSG_TYPE_COPY_SEND;
            reply.memoryEntry.type = MACH_MSG_PORT_DESCRIPTOR;
            reply.msgType = ROOMCUT_MSG_HELLO;
            reply.granted = format();
            reply.availableRateCount = 2;
            reply.availableRates[0] = 44100;
            reply.availableRates[1] = 48000;
            mach_msg_size_t size = sizeof(reply);
            switch (kind) {
            case ReplyCase::legacy: size = offsetof(RoomcutHelloReply, availableRateCount); break;
            case ReplyCase::partialRates: size = offsetof(RoomcutHelloReply, availableRates); break;
            case ReplyCase::truncated: size = offsetof(RoomcutHelloReply, granted); break;
            case ReplyCase::wrongID: reply.header.msgh_id = ROOMCUT_MSG_HEALTH_CHECK; break;
            case ReplyCase::wrongType: reply.msgType = ROOMCUT_MSG_HEALTH_CHECK; break;
            case ReplyCase::errorStatus: reply.status = 1; break;
            case ReplyCase::wrongChannels: reply.granted.channels = 6; break;
            case ReplyCase::wrongRate: reply.granted.sampleRate = 1; break;
            case ReplyCase::wrongLayout: reply.granted.channelLayout = ROOMCUT_LAYOUT_5_1; break;
            case ReplyCase::wrongFormat: reply.granted.internalFormat = 7; break;
            case ReplyCase::wrongCapacity: reply.granted.capacityFrames = 257; break;
            case ReplyCase::mismatchedHeader: reply.granted.sampleRate = 96000; break;
            case ReplyCase::shortMapping: reply.granted.capacityFrames = 65536; break;
            case ReplyCase::extraRight:
                reply.header.msgh_bits |= MACH_MSGH_BITS(0, MACH_MSG_TYPE_MAKE_SEND);
                reply.header.msgh_local_port = extra.name;
                break;
            case ReplyCase::nonMemoryPort:
                reply.memoryEntry.name = extra.name;
                reply.memoryEntry.disposition = MACH_MSG_TYPE_MAKE_SEND;
                break;
            default: break;
            }
            if (kind == ReplyCase::twoDescriptors) {
                struct { mach_msg_header_t header; mach_msg_body_t body;
                    mach_msg_port_descriptor_t descriptors[2]; uint32_t padding[32]; } message{};
                message.header = reply.header;
                message.body.msgh_descriptor_count = 2;
                message.descriptors[0] = reply.memoryEntry;
                message.descriptors[1] = reply.memoryEntry;
                message.descriptors[1].name = extra.name;
                message.descriptors[1].disposition = MACH_MSG_TYPE_MAKE_SEND;
                CHECK(mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message),
                    0, MACH_PORT_NULL, 2000, MACH_PORT_NULL) == KERN_SUCCESS, "send two descriptors");
            } else {
                CHECK(mach_msg(&reply.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, size,
                    0, MACH_PORT_NULL, 2000, MACH_PORT_NULL) == KERN_SUCCESS, "send HELLO reply");
            }
        });
        RoomcutTransportConnection connection{};
        RingRegion mapped;
        if (reconnect) CHECK(mapped.create(512, 2, 44100) == KERN_SUCCESS, "prepare previous mapping");
        auto* previousMapping = mapped.header();
        RoomcutFormatNegotiation granted{};
        granted.sampleRate = 123;
        uint32_t rates[ROOMCUT_MAX_RATES];
        for (auto& rate : rates) rate = 123;
        uint32_t rateCount = 123;
        const auto result = driver ? Roomcut_TransportConnectToPort(endpoint, 48000, 2000, &connection)
            : driverSendHelloAndReceive(endpoint, format(), mapped, &granted, rates, &rateCount);
        server.join();
        const bool valid = kind == ReplyCase::current || kind == ReplyCase::legacy || kind == ReplyCase::partialRates;
        CHECK((result == KERN_SUCCESS) == valid, "only complete valid HELLO replies succeed");
        if (valid && result == KERN_SUCCESS) {
            auto* header = driver ? connection.header : mapped.header();
            CHECK(header && header->sampleRate == 48000 && header->channels == 2, "mapping matches negotiated format");
            const auto count = driver ? connection.availableRateCount : rateCount;
            CHECK(count == (kind == ReplyCase::current ? 2u : 0u), "missing rate list never reads Mach trailer");
            if (kind == ReplyCase::current) {
                const auto* list = driver ? connection.availableRates : rates;
                CHECK(list[0] == 44100 && list[1] == 48000, "complete rates survive handoff");
            }
            float signal[4] = {0.25f, -0.25f, 0.1f, -0.1f}, output[4]{};
            CHECK(roomcut_ring_write(header, signal, 2, 0) == 2, "accepted mapping carries audio");
            CHECK(roomcut_ring_read(backing.header(), output, 2) == 2 && std::memcmp(signal, output, sizeof(signal)) == 0,
                  "engine sees exact frames from accepted mapping");
        } else if (!valid) {
            CHECK(driver ? connection.header == nullptr : mapped.header() == previousMapping,
                  "invalid reply does not replace the caller's mapping");
            if (!driver) CHECK(granted.sampleRate == 123 && rateCount == 123 && rates[0] == 123,
                               "failed handoff leaves caller format and rates untouched");
        }
        if (driver) Roomcut_TransportDisconnect(&connection);
        else { mapped.destroy(); mach_port_deallocate(mach_task_self(), endpoint); }
        CHECK(refs(backing.memoryEntry()) == memoryRefs, "failed or retired mapping leaves no memory-entry rights");
        CHECK(refs(extra.name) == 0, "unexpected descriptors and header rights are released");
        const auto leaked = refs(backing.memoryEntry()) - memoryRefs;
        if (leaked) mach_port_mod_refs(mach_task_self(), backing.memoryEntry(), MACH_PORT_RIGHT_SEND, -leaked);
    }
}

static void requestBoundaries() {
    for (int variant = 0; variant < 9; ++variant) {
        std::printf("request case=%d\n", variant);
        Port service, response;
        RoomcutHelloRequest request{};
        request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
        request.header.msgh_remote_port = service.sendRight();
        request.header.msgh_local_port = response.name;
        request.header.msgh_id = ROOMCUT_MSG_HELLO;
        request.msgType = ROOMCUT_MSG_HELLO;
        request.protocolVersion = ROOMCUT_IPC_VERSION;
        request.requested = format();
        mach_msg_size_t size = sizeof(request);
        if (variant == 1) size = offsetof(RoomcutHelloRequest, requested);
        if (variant == 2) request.header.msgh_id = ROOMCUT_MSG_HEALTH_CHECK;
        if (variant == 3) request.msgType = ROOMCUT_MSG_HEALTH_CHECK;
        if (variant == 4) request.protocolVersion = 0;
        if (variant == 5) request.protocolVersion = ROOMCUT_IPC_VERSION + 1;
        if (variant == 6) request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND);
        if (variant == 7) {
            request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
            request.header.msgh_local_port = MACH_PORT_NULL;
        }
        if (variant == 8) {
            struct { mach_msg_header_t header; mach_msg_body_t body; uint32_t data[16]; } message{};
            message.header = request.header;
            message.header.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
            CHECK(mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message), 0,
                MACH_PORT_NULL, 2000, MACH_PORT_NULL) == KERN_SUCCESS, "send complex request");
        } else CHECK(mach_msg(&request.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, size, 0,
            MACH_PORT_NULL, 2000, MACH_PORT_NULL) == KERN_SUCCESS, "send request variant");
        RoomcutHelloMsgBuffer received{};
        CHECK(mach_msg(&received.request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
            sizeof(received), service.name, 2000, MACH_PORT_NULL) == KERN_SUCCESS, "receive request variant");
        RingRegion backing;
        CHECK(backing.create(1024, 2, 48000) == KERN_SUCCESS, "create ring for reply validation");
        RoomcutFormatNegotiation negotiated{};
        negotiated.sampleRate = 123;
        CHECK(engineNegotiateHello(received.request, negotiated) == (variant == 0),
              "production negotiation rejects invalid requests before state changes");
        CHECK(negotiated.sampleRate == (variant == 0 ? 48000u : 123u), "failed negotiation preserves prior format");
        if (variant == 0) {
            struct Case { uint32_t rate, requestedCapacity, expectedRate, expectedCapacity; };
            for (const auto sample : {Case{44100, 256, 44100, 4096}, Case{48000, 1024, 48000, 4096},
                Case{96000, 4096, 96000, 8192}, Case{192000, 4096, 192000, 16384},
                Case{768000, 4096, 768000, 65536}, Case{8000, 256, 8000, 1024},
                Case{0, 300, 48000, 4096}, Case{999999, 65536, 48000, 65536},
                Case{48000, 65537, 48000, 4096}}) {
                auto preference = received.request;
                preference.requested.sampleRate = sample.rate;
                preference.requested.capacityFrames = sample.requestedCapacity;
                preference.requested.channels = 6;
                preference.requested.channelLayout = ROOMCUT_LAYOUT_5_1;
                preference.requested.internalFormat = 7;
                preference.requested.bufferFrameSize = 256;
                preference.requested.latencyClass = ROOMCUT_LATENCY_LOW;
                CHECK(engineNegotiateHello(preference, negotiated), "valid envelope still negotiates unsupported preferences");
                CHECK(negotiated.sampleRate == sample.expectedRate && negotiated.capacityFrames == sample.expectedCapacity,
                      "sample-rate fallback and high-rate jitter cushion retain established behavior");
                CHECK(negotiated.channels == 2 && negotiated.channelLayout == ROOMCUT_LAYOUT_STEREO &&
                      negotiated.internalFormat == ROOMCUT_INTERNAL_FORMAT_F32 &&
                      negotiated.bufferFrameSize == 256 && negotiated.latencyClass == ROOMCUT_LATENCY_LOW,
                      "negotiation retains stereo float32 and scheduling hints");
            }
        }
        const auto result = engineReplyHello(received.request, backing, format());
        CHECK((result == KERN_SUCCESS) == (variant == 0), "engine rejects malformed or incompatible requests");
        if (result != KERN_SUCCESS) mach_msg_destroy(&received.request.header);
        // Closing the caller receive right also disposes any queued reply descriptor.
        response.close();
        CHECK(refs(backing.memoryEntry()) == 1, "request boundary preserves engine-owned memory right");
    }
}

static unsigned temporaryRights() {
    mach_port_name_array_t names = nullptr;
    mach_port_type_array_t types = nullptr;
    mach_msg_type_number_t nameCount = 0, typeCount = 0;
    CHECK(mach_port_names(mach_task_self(), &names, &nameCount, &types, &typeCount) == KERN_SUCCESS, "read test task port rights");
    unsigned count = 0;
    for (unsigned i = 0; i < typeCount; ++i)
        if (types[i] & (MACH_PORT_TYPE_SEND_ONCE | MACH_PORT_TYPE_DEAD_NAME)) ++count;
    if (names) vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(names), nameCount * sizeof(*names));
    if (types) vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(types), typeCount * sizeof(*types));
    return count;
}

static void cancelledCallerReleasesReplyRight() {
    Port service, response;
    RoomcutHelloRequest request{};
    request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    request.header.msgh_remote_port = service.sendRight();
    request.header.msgh_local_port = response.name;
    request.header.msgh_id = ROOMCUT_MSG_HELLO;
    request.msgType = ROOMCUT_MSG_HELLO;
    request.protocolVersion = ROOMCUT_IPC_VERSION;
    request.requested = format();
    CHECK(mach_msg(&request.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(request),
        0, MACH_PORT_NULL, 100, MACH_PORT_NULL) == KERN_SUCCESS, "send before cancelling caller");
    RoomcutHelloMsgBuffer received{};
    CHECK(mach_msg(&received.request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(received),
        service.name, 100, MACH_PORT_NULL) == KERN_SUCCESS, "receive before cancelling caller");
    const auto replyRight = received.request.header.msgh_remote_port;
    response.close();
    RingRegion backing;
    CHECK(backing.create(1024, 2, 48000) == KERN_SUCCESS, "create cancelled caller ring");
    const auto sent = engineReplyHello(received.request, backing, format());
    std::printf("cancelled caller result=%d dead refs=%u\n", sent, refs(replyRight, MACH_PORT_RIGHT_DEAD_NAME));
    CHECK(sent == KERN_SUCCESS || sent == MACH_SEND_INVALID_DEST, "cancelled reply is delivered or reports a vanished caller");
    CHECK(refs(replyRight, MACH_PORT_RIGHT_DEAD_NAME) == 0, "failed HELLO reply releases its dead send-once right");
    CHECK(refs(backing.memoryEntry()) == 1, "failed reply preserves the engine's borrowed memory-entry right");
    if (refs(replyRight, MACH_PORT_RIGHT_DEAD_NAME)) mach_port_deallocate(mach_task_self(), replyRight);
}

static void timeoutsAreBoundedAndReleaseRights(bool driver, bool fullQueue) {
    Port service;
    const auto endpoint = service.sendRight();
    if (fullQueue) {
        mach_port_limits_t limits{1};
        CHECK(mach_port_set_attributes(mach_task_self(), service.name, MACH_PORT_LIMITS_INFO,
            reinterpret_cast<mach_port_info_t>(&limits), MACH_PORT_LIMITS_INFO_COUNT) == KERN_SUCCESS, "limit queue to one request");
        mach_msg_header_t message{};
        message.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        message.msgh_remote_port = endpoint;
        CHECK(mach_msg(&message, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message),
            0, MACH_PORT_NULL, 0, MACH_PORT_NULL) == KERN_SUCCESS, "fill service queue");
    }
    const auto before = temporaryRights();
    // C transfer takes its own right; C++ borrows the original one.
    const auto passed = driver ? service.sendRight() : endpoint;
    RoomcutTransportConnection connection{};
    RingRegion mapped;
    RoomcutFormatNegotiation granted{};
    const auto start = std::chrono::steady_clock::now();
    const auto result = driver ? Roomcut_TransportConnectToPort(passed, 48000, 50, &connection)
        : driverSendHelloAndReceive(passed, format(), mapped, &granted, nullptr, nullptr, 50);
    CHECK(result == (fullQueue ? MACH_SEND_TIMED_OUT : MACH_RCV_TIMED_OUT), "silent or busy peer times out");
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "timeout is bounded");
    CHECK(refs(endpoint) == 1, "timeout leaves only the caller-owned service right");
    CHECK(temporaryRights() == before, "timeout leaves no temporary send-once or dead-name rights");
}

int main() {
    replyBoundaries(false);
    replyBoundaries(true);
    replyBoundaries(false, true);
    requestBoundaries();
    cancelledCallerReleasesReplyRight();
    for (bool driver : {false, true}) for (bool full : {false, true})
        timeoutsAreBoundedAndReleaseRights(driver, full);
    if (failures) std::fprintf(stderr, "%d handshake boundary checks failed\n", failures.load());
    else std::puts("all handshake boundary tests passed");
    return failures ? 1 : 0;
}
