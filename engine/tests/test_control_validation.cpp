#include "Control.hpp"
#include "ControlValidation.hpp"
#include <cmath>
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
    changed = valid; changed.comparisonRequest.version = ROOMCUT_COMPARISON_VERSION + 1;
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

    // A version-2 app sends the comparison pair without the trailing room.
    auto preRoomPair = request(ROOMCUT_MSG_SET_COMPARISON,
                               offsetof(RoomcutComparisonRequest, currentRoomType));
    preRoomPair.comparisonRequest.version = 2;
    preRoomPair.comparisonRequest.enabled = 1;
    CHECK(roomcut::normalizeControlRequest(preRoomPair), "a pre-room comparison pair remains supported");
    CHECK(preRoomPair.comparisonRequest.currentRoomType == 0 && preRoomPair.comparisonRequest.referenceRoomType == 0,
          "an absent comparison room reads as zero, and the engine keeps the live one");
    auto futurePair = valid; futurePair.comparisonRequest.version = ROOMCUT_COMPARISON_VERSION + 1;
    CHECK(!roomcut::normalizeControlRequest(futurePair), "a newer comparison payload is still rejected");
    auto ancientPair = valid; ancientPair.comparisonRequest.version = ROOMCUT_COMPARISON_MIN_VERSION - 1;
    CHECK(!roomcut::normalizeControlRequest(ancientPair), "a payload older than the supported range is rejected");

    // Live head pose: a small fixed message, and a bad one must not reach the
    // render thread — a NaN angle there would poison the delay interpolation.
    auto pose = request(ROOMCUT_MSG_SET_HEAD_POSE, sizeof(RoomcutSetHeadPoseRequest));
    pose.setHeadPose.active = 1;
    pose.setHeadPose.yawDeg = 30.0;
    CHECK(roomcut::normalizeControlRequest(pose), "a well-formed head pose is accepted");
    auto badPose = pose; badPose.setHeadPose.yawDeg = std::nan("");
    CHECK(!roomcut::normalizeControlRequest(badPose), "a non-finite head angle is rejected");
    badPose = pose; badPose.setHeadPose.active = 7;
    CHECK(!roomcut::normalizeControlRequest(badPose), "the tracker flag is validated");
    badPose = pose; badPose.raw.header.msgh_size -= 8;
    CHECK(!roomcut::normalizeControlRequest(badPose), "a truncated head pose is rejected");

    // Listening-test burst: the length and level reach the render thread as
    // they are, so both are held to their ranges here. Any channel is allowed —
    // one outside 0-6 is how a burst is stopped.
    auto probe = request(ROOMCUT_MSG_PROBE_CHANNEL, sizeof(RoomcutProbeChannelRequest));
    probe.probeChannel.channel = 3;
    probe.probeChannel.seconds = 0.5;
    probe.probeChannel.levelDb = -20.0;
    CHECK(roomcut::normalizeControlRequest(probe), "a well-formed probe is accepted");
    auto stop = probe; stop.probeChannel.channel = -1;
    CHECK(roomcut::normalizeControlRequest(stop), "a stop (channel outside 0-6) is accepted");
    for (double seconds : {0.0, -1.0, 10.5, std::nan("")}) {
        auto bad = probe; bad.probeChannel.seconds = seconds;
        CHECK(!roomcut::normalizeControlRequest(bad), "a probe length outside (0, 10] s is rejected");
    }
    for (double level : {0.5, -60.5, std::nan("")}) {
        auto bad = probe; bad.probeChannel.levelDb = level;
        CHECK(!roomcut::normalizeControlRequest(bad), "a probe level outside [-60, 0] dB is rejected");
    }
    auto shortProbe = probe; shortProbe.raw.header.msgh_size -= 8;
    CHECK(!roomcut::normalizeControlRequest(shortProbe), "a truncated probe is rejected");

    auto legacy = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, highpassHz));
    mach_msg_trailer_t trailer{MACH_MSG_TRAILER_FORMAT_0, sizeof(mach_msg_trailer_t)};
    std::memcpy(reinterpret_cast<char*>(&legacy) + legacy.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(legacy.setParams.highpassHz != 0, "a received trailer would contaminate an absent dynamics field");
    CHECK(roomcut::normalizeControlRequest(legacy), "a complete legacy parameter prefix remains supported");
    CHECK(legacy.setParams.highpassHz == 0 && legacy.setParams.compAmount == 0,
          "omitted fields are zero, not trailer bytes");
    legacy.raw.header.msgh_size = offsetof(RoomcutSetParamsRequest, spatialWidth) - 4;
    CHECK(!roomcut::normalizeControlRequest(legacy), "partial mandatory values are rejected");

    // An app built before the virtual room stops right after the dynamics. The
    // engine has to keep taking it, or updating one half of the pair goes silent.
    auto preRoom = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, roomType));
    std::memcpy(reinterpret_cast<char*>(&preRoom) + preRoom.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlRequest(preRoom), "a pre-room parameter message remains supported");
    CHECK(preRoom.setParams.roomType == 0 && preRoom.setParams.roomAmount == 0,
          "an absent room reads as off, not as trailer bytes");

    // Same again one field group later: an app built before the upmix stops
    // right after the room.
    auto preUpmix = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, surroundType));
    std::memcpy(reinterpret_cast<char*>(&preUpmix) + preUpmix.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlRequest(preUpmix), "a pre-upmix parameter message remains supported");
    CHECK(preUpmix.setParams.surroundType == 0 && preUpmix.setParams.centerWidth == 0
          && preUpmix.setParams.surroundDepth == 0,
          "an absent upmix reads as off, not as trailer bytes");

    // And a version-3 comparison, which stops right before the appended upmix.
    auto preUpmixPair = request(ROOMCUT_MSG_SET_COMPARISON,
                                offsetof(RoomcutComparisonRequest, currentSurroundType));
    preUpmixPair.comparisonRequest.version = 3;
    std::memcpy(reinterpret_cast<char*>(&preUpmixPair) + preUpmixPair.raw.header.msgh_size,
                &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlRequest(preUpmixPair), "a version-3 comparison remains supported");
    CHECK(preUpmixPair.comparisonRequest.currentSurroundType == 0
          && preUpmixPair.comparisonRequest.referenceSurroundType == 0,
          "an absent upmix in a comparison reads as off");

    // An app built before the bed renderer stops right after the upmix; its
    // absent renderer reads 0, which is the system renderer — the default.
    auto preBed = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, bedRenderer));
    std::memcpy(reinterpret_cast<char*>(&preBed) + preBed.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlRequest(preBed), "a pre-renderer parameter message remains supported");
    CHECK(preBed.setParams.bedRenderer == 0, "an absent bed renderer reads as the default, not as trailer bytes");
    auto preBedPair = request(ROOMCUT_MSG_SET_COMPARISON, offsetof(RoomcutComparisonRequest, currentBedRenderer));
    preBedPair.comparisonRequest.version = 4;
    std::memcpy(reinterpret_cast<char*>(&preBedPair) + preBedPair.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlRequest(preBedPair), "a version-4 comparison remains supported");
    CHECK(preBedPair.comparisonRequest.currentBedRenderer == 0 && preBedPair.comparisonRequest.referenceBedRenderer == 0,
          "an absent renderer in a comparison reads as zero, and the engine keeps the live one");
    auto cutBed = request(ROOMCUT_MSG_SET_PARAMS, offsetof(RoomcutSetParamsRequest, bedRenderer) + 4);
    CHECK(!roomcut::normalizeControlRequest(cutBed), "half a bed renderer field is rejected");
}

static void replyBoundaries() {
    // The mirror image: a reply from an engine built before the virtual room.
    // This is what breaks first when a field is appended without registering
    // its offset — the app can no longer read any parameter at all.
    auto reply = request(ROOMCUT_MSG_GET_PARAMS, offsetof(RoomcutGetParamsReply, roomType));
    reply.raw.header.msgh_bits = 0;
    reply.raw.header.msgh_remote_port = MACH_PORT_NULL;
    mach_msg_trailer_t trailer{MACH_MSG_TRAILER_FORMAT_0, sizeof(mach_msg_trailer_t)};
    std::memcpy(reinterpret_cast<char*>(&reply) + reply.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlReply(reply, ROOMCUT_MSG_GET_PARAMS),
          "a pre-room parameter reply remains readable");
    CHECK(reply.paramsReply.roomType == 0 && reply.paramsReply.roomAmount == 0,
          "an absent room reads as off in a reply too");

    auto full = request(ROOMCUT_MSG_GET_PARAMS, sizeof(RoomcutGetParamsReply));
    full.raw.header.msgh_bits = 0;
    full.raw.header.msgh_remote_port = MACH_PORT_NULL;
    full.paramsReply.roomType = 2;
    full.paramsReply.roomAmount = 65;
    CHECK(roomcut::normalizeControlReply(full, ROOMCUT_MSG_GET_PARAMS), "a current reply is accepted");
    CHECK(full.paramsReply.roomType == 2 && full.paramsReply.roomAmount == 65,
          "a current reply keeps its room fields");

    auto preUpmix = request(ROOMCUT_MSG_GET_PARAMS, offsetof(RoomcutGetParamsReply, surroundType));
    preUpmix.raw.header.msgh_bits = 0;
    preUpmix.raw.header.msgh_remote_port = MACH_PORT_NULL;
    std::memcpy(reinterpret_cast<char*>(&preUpmix) + preUpmix.raw.header.msgh_size,
                &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlReply(preUpmix, ROOMCUT_MSG_GET_PARAMS),
          "a pre-upmix parameter reply remains readable");
    CHECK(preUpmix.paramsReply.surroundType == 0 && preUpmix.paramsReply.centerWidth == 0,
          "an absent upmix reads as off in a reply too");

    auto preBed = request(ROOMCUT_MSG_GET_PARAMS, offsetof(RoomcutGetParamsReply, bedRenderer));
    preBed.raw.header.msgh_bits = 0;
    preBed.raw.header.msgh_remote_port = MACH_PORT_NULL;
    std::memcpy(reinterpret_cast<char*>(&preBed) + preBed.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlReply(preBed, ROOMCUT_MSG_GET_PARAMS), "a pre-renderer parameter reply remains readable");
    CHECK(preBed.paramsReply.bedRenderer == 0, "an absent renderer reads as the default in a reply");

    // A state reply from an engine before the renderer status: no renderer.
    auto preBedState = request(ROOMCUT_MSG_STATE, offsetof(RoomcutStateReply, bedRenderer));
    preBedState.raw.header.msgh_bits = 0;
    preBedState.raw.header.msgh_remote_port = MACH_PORT_NULL;
    std::memcpy(reinterpret_cast<char*>(&preBedState) + preBedState.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlReply(preBedState, ROOMCUT_MSG_STATE), "a pre-renderer state reply remains readable");
    CHECK(preBedState.stateReply.bedRenderer == 0 && preBedState.stateReply.bedExternalGain == 0
          && preBedState.stateReply.bedUnitRate == 0, "and reports no system renderer rather than trailer bytes");

    auto preBedPair = request(ROOMCUT_MSG_GET_COMPARISON, offsetof(RoomcutComparisonReply, currentBedRenderer));
    preBedPair.raw.header.msgh_bits = 0;
    preBedPair.raw.header.msgh_remote_port = MACH_PORT_NULL;
    preBedPair.comparisonReply.version = 4;
    std::memcpy(reinterpret_cast<char*>(&preBedPair) + preBedPair.raw.header.msgh_size, &trailer, sizeof(trailer));
    CHECK(roomcut::normalizeControlReply(preBedPair, ROOMCUT_MSG_GET_COMPARISON), "a version-4 comparison reply remains readable");
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
    replyBoundaries();
    truncatedAcknowledgementIsNotSuccess();
    if (!failures) std::puts("all control validation tests passed");
    return failures ? 1 : 0;
}
