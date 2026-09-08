#include "Control.hpp"
#include "ControlValidation.hpp"
#include <cstdio>
#include <cstring>
#include <thread>

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static RoomcutControlMsgBuffer request(uint32_t type, std::size_t size) {
    RoomcutControlMsgBuffer buffer{};
    buffer.raw.header.msgh_id = type;
    buffer.raw.header.msgh_size = static_cast<mach_msg_size_t>(size);
    buffer.raw.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_PORT_SEND_ONCE, 0);
    buffer.raw.header.msgh_remote_port = 42; // shape test only; never sent/destroyed
    buffer.reply.msgType = type;
    return buffer;
}

static void requestBoundaries() {
    static_assert(sizeof(RoomcutControlMsgBuffer) >= sizeof(RoomcutComparisonReply) + sizeof(mach_msg_max_trailer_t));
    auto valid = request(ROOMCUT_MSG_SET_COMPARISON, sizeof(RoomcutComparisonRequest));
    valid.comparisonRequest.version = ROOMCUT_COMPARISON_VERSION;
    valid.comparisonRequest.enabled = 1;
    CHECK(roomcut::normalizeControlRequest(valid), "a full comparison pair fits the receive envelope");
    auto changed = valid;
    changed.raw.header.msgh_size -= 8;
    CHECK(!roomcut::normalizeControlRequest(changed), "a partial reference cannot be accepted");
    changed = valid; changed.comparisonRequest.version = 2;
    CHECK(!roomcut::normalizeControlRequest(changed), "unknown comparison versions are rejected");
    changed = valid; changed.comparisonRequest.enabled = 2;
    CHECK(!roomcut::normalizeControlRequest(changed), "comparison flags are validated");
    changed = valid; changed.comparisonRequest.kind = 2;
    CHECK(!roomcut::normalizeControlRequest(changed), "unknown comparison operations are rejected");
    changed = valid; changed.reply.msgType = ROOMCUT_MSG_SET_PARAMS;
    CHECK(!roomcut::normalizeControlRequest(changed), "header and payload message types must agree");
    changed = valid; changed.raw.header.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
    CHECK(!roomcut::normalizeControlRequest(changed), "inline controls cannot carry Mach descriptors");
    changed = valid; std::memset(changed.comparisonRequest.presetId, 'x', ROOMCUT_PRESET_ID_MAX);
    CHECK(!roomcut::normalizeControlRequest(changed), "preset identifiers must terminate within their field");

    auto legacy = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, highpassHz));
    mach_msg_trailer_t trailer{MACH_MSG_TRAILER_FORMAT_0, sizeof(mach_msg_trailer_t)};
    std::memcpy(reinterpret_cast<char*>(&legacy) + legacy.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(legacy.setParams.highpassHz != 0, "a received trailer would contaminate an absent dynamics field");
    CHECK(roomcut::normalizeControlRequest(legacy), "a complete legacy parameter prefix remains supported");
    CHECK(legacy.setParams.highpassHz == 0 && legacy.setParams.compAmount == 0,
          "omitted fields are zero, not trailer bytes");
    legacy.raw.header.msgh_size = offsetof(RoomcutSetParamsRequest, spatialWidth) - 4;
    CHECK(!roomcut::normalizeControlRequest(legacy), "partial mandatory values are rejected");
}

static void truncatedAcknowledgementIsNotSuccess() {
    const auto task = mach_task_self();
    mach_port_t service = MACH_PORT_NULL;
    CHECK(mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &service) == KERN_SUCCESS, "allocate isolated test service");
    CHECK(mach_port_insert_right(task, service, service, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS, "create isolated service send right");
    std::thread server([&] {
        RoomcutControlMsgBuffer received{};
        const auto status = mach_msg(&received.raw.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(received), service, 2000, MACH_PORT_NULL);
        CHECK(status == KERN_SUCCESS, "receive test request");
        if (status != KERN_SUCCESS) return;
        RoomcutControlReply reply{};
        reply.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
        reply.header.msgh_size = offsetof(RoomcutControlReply, status); // missing result
        reply.header.msgh_remote_port = received.raw.header.msgh_remote_port;
        reply.header.msgh_id = ROOMCUT_MSG_SET_BYPASS;
        reply.msgType = ROOMCUT_MSG_SET_BYPASS;
        CHECK(mach_msg(&reply.header, MACH_SEND_MSG, reply.header.msgh_size, 0,
                       MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL) == KERN_SUCCESS, "send truncated reply");
    });
    uint32_t status = 99;
    const auto result = roomcut::controlSetBypass(service, true, 2000, &status);
    server.join();
    CHECK(result != KERN_SUCCESS && status == 99, "a reply without an engine result cannot report successful application");
    mach_port_deallocate(task, service);
    mach_port_mod_refs(task, service, MACH_PORT_RIGHT_RECEIVE, -1);
}

int main() {
    requestBoundaries();
    truncatedAcknowledgementIsNotSuccess();
    if (!failures) std::puts("all control validation tests passed");
    return failures ? 1 : 0;
}
