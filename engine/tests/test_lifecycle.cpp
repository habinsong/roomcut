/*
 * test_lifecycle.cpp — exhaustive-ish checks of the driver/engine state
 * machines (DEVELOPMENT_PLAN.md §4.1 normal path, §4.2 failure paths, §4.5
 * wire projection decision).
 */
#include "Lifecycle.hpp"

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

static void test_driver_normal_path() {
    RoomcutDriverLifecycle s = ROOMCUT_DRIVER_UNINITIALIZED;
    s = driverNext(s, DriverEvent::Load);          CHECK(s == ROOMCUT_DRIVER_LOADED, "load");
    s = driverNext(s, DriverEvent::ServiceFound);  CHECK(s == ROOMCUT_DRIVER_HANDSHAKING, "service found → handshaking");
    s = driverNext(s, DriverEvent::HandoffMapped); CHECK(s == ROOMCUT_DRIVER_SHARED_READY, "handoff mapped → shared ready");
    s = driverNext(s, DriverEvent::StartWriting);  CHECK(s == ROOMCUT_DRIVER_STREAMING, "start writing → streaming");
}

static void test_driver_engine_absent() {
    // Engine missing at load: park in WaitingEngine, then recover when found.
    RoomcutDriverLifecycle s = ROOMCUT_DRIVER_LOADED;
    s = driverNext(s, DriverEvent::ServiceMissing); CHECK(s == ROOMCUT_DRIVER_WAITING_ENGINE, "missing → waiting engine");
    s = driverNext(s, DriverEvent::ServiceFound);   CHECK(s == ROOMCUT_DRIVER_HANDSHAKING, "found → handshaking");
}

static void test_driver_engine_lost_and_recover() {
    // Streaming → heartbeat timeout → EngineLost → (still gone) SafeBypass →
    // reconnect → handshaking again.
    RoomcutDriverLifecycle s = ROOMCUT_DRIVER_STREAMING;
    s = driverNext(s, DriverEvent::HeartbeatTimeout); CHECK(s == ROOMCUT_DRIVER_ENGINE_LOST, "timeout → engine lost");
    s = driverNext(s, DriverEvent::HeartbeatTimeout); CHECK(s == ROOMCUT_DRIVER_SAFE_BYPASS, "still gone → safe bypass");
    s = driverNext(s, DriverEvent::Reconnected);      CHECK(s == ROOMCUT_DRIVER_HANDSHAKING, "reconnect → handshaking");
    s = driverNext(s, DriverEvent::HandoffMapped);    CHECK(s == ROOMCUT_DRIVER_SHARED_READY, "remap → shared ready");
}

static void test_driver_handoff_failure() {
    RoomcutDriverLifecycle s = ROOMCUT_DRIVER_HANDSHAKING;
    s = driverNext(s, DriverEvent::HandoffFailed); CHECK(s == ROOMCUT_DRIVER_SAFE_BYPASS, "handoff fail → safe bypass");
    s = driverNext(s, DriverEvent::ServiceFound);  CHECK(s == ROOMCUT_DRIVER_HANDSHAKING, "retry → handshaking");
}

static void test_driver_unload_anytime() {
    RoomcutDriverLifecycle states[] = {
        ROOMCUT_DRIVER_LOADED, ROOMCUT_DRIVER_WAITING_ENGINE, ROOMCUT_DRIVER_HANDSHAKING,
        ROOMCUT_DRIVER_SHARED_READY, ROOMCUT_DRIVER_STREAMING, ROOMCUT_DRIVER_ENGINE_LOST,
        ROOMCUT_DRIVER_SAFE_BYPASS
    };
    for (RoomcutDriverLifecycle st : states) {
        CHECK(driverNext(st, DriverEvent::Unload) == ROOMCUT_DRIVER_UNLOADING, "unload → unloading from any live state");
    }
}

static void test_driver_unknown_events_noop() {
    // StartWriting from LOADED is nonsensical → state unchanged (total function).
    CHECK(driverNext(ROOMCUT_DRIVER_LOADED, DriverEvent::StartWriting) == ROOMCUT_DRIVER_LOADED, "nonsense event is a no-op");
    CHECK(driverNext(ROOMCUT_DRIVER_UNLOADING, DriverEvent::ServiceFound) == ROOMCUT_DRIVER_UNLOADING, "unloading absorbs events");
}

static void test_engine_normal_path() {
    RoomcutEngineLifecycle s = ROOMCUT_ENGINE_STARTING;
    s = engineNext(s, EngineEvent::ServicePublished); CHECK(s == ROOMCUT_ENGINE_WAITING_DRIVER, "service published");
    s = engineNext(s, EngineEvent::HelloReceived);    CHECK(s == ROOMCUT_ENGINE_CONNECTED, "hello received");
    s = engineNext(s, EngineEvent::RegionCreated);    CHECK(s == ROOMCUT_ENGINE_BUFFER_MAPPED, "region created");
    s = engineNext(s, EngineEvent::OutputOpened);     CHECK(s == ROOMCUT_ENGINE_OUTPUT_READY, "output opened");
    s = engineNext(s, EngineEvent::OutputReopened);   CHECK(s == ROOMCUT_ENGINE_STREAMING, "streaming");
}

static void test_engine_output_lost_recover() {
    RoomcutEngineLifecycle s = ROOMCUT_ENGINE_STREAMING;
    s = engineNext(s, EngineEvent::OutputLost);     CHECK(s == ROOMCUT_ENGINE_RECOVERING, "output lost → recovering");
    s = engineNext(s, EngineEvent::OutputReopened); CHECK(s == ROOMCUT_ENGINE_STREAMING, "reopened → streaming");
}

static void test_engine_driver_lost() {
    // Driver-stall round trip: the feed froze and then resumed while the
    // output stayed open, so the machine must land back in STREAMING — not
    // OUTPUT_READY, whose wire projection is STOPPED with no way forward
    // (regression: stall-resume used to report STOPPED forever).
    RoomcutEngineLifecycle s = ROOMCUT_ENGINE_STREAMING;
    s = engineNext(s, EngineEvent::DriverLost);     CHECK(s == ROOMCUT_ENGINE_RECOVERING, "driver lost while streaming → recovering");
    CHECK(engineWireState(s) == ROOMCUT_STATE_RECOVER, "stalled feed shows RECOVER on the wire");
    s = engineNext(s, EngineEvent::DriverReturned); CHECK(s == ROOMCUT_ENGINE_STREAMING, "driver feed resumed → streaming");
    CHECK(engineWireState(s) == ROOMCUT_STATE_RUNNING, "resumed feed shows RUNNING on the wire");

    // Driver lost before streaming drops back to waiting for a driver.
    RoomcutEngineLifecycle b = ROOMCUT_ENGINE_BUFFER_MAPPED;
    b = engineNext(b, EngineEvent::DriverLost); CHECK(b == ROOMCUT_ENGINE_WAITING_DRIVER, "driver lost pre-stream → waiting driver");
}

static void test_engine_stop_anytime() {
    RoomcutEngineLifecycle states[] = {
        ROOMCUT_ENGINE_STARTING, ROOMCUT_ENGINE_WAITING_DRIVER, ROOMCUT_ENGINE_CONNECTED,
        ROOMCUT_ENGINE_BUFFER_MAPPED, ROOMCUT_ENGINE_OUTPUT_READY, ROOMCUT_ENGINE_STREAMING,
        ROOMCUT_ENGINE_RECOVERING
    };
    for (RoomcutEngineLifecycle st : states) {
        CHECK(engineNext(st, EngineEvent::Stop) == ROOMCUT_ENGINE_STOPPING, "stop → stopping from any live state");
    }
}

static void test_wire_projection() {
    // Engine lifecycle → coarse RoomcutEngineState.
    CHECK(engineWireState(ROOMCUT_ENGINE_STREAMING)  == ROOMCUT_STATE_RUNNING, "streaming → RUNNING");
    CHECK(engineWireState(ROOMCUT_ENGINE_RECOVERING) == ROOMCUT_STATE_RECOVER, "recovering → RECOVER");
    CHECK(engineWireState(ROOMCUT_ENGINE_STARTING)   == ROOMCUT_STATE_STOPPED, "starting → STOPPED");
    CHECK(engineWireState(ROOMCUT_ENGINE_OUTPUT_READY) == ROOMCUT_STATE_STOPPED, "output-ready (pre-stream) → STOPPED");
    CHECK(engineWireState(ROOMCUT_ENGINE_STOPPING)   == ROOMCUT_STATE_STOPPED, "stopping → STOPPED");

    // §4.5 decision: driver EngineLost and SafeBypass BOTH collapse to RECOVER.
    CHECK(driverWireState(ROOMCUT_DRIVER_STREAMING)   == ROOMCUT_STATE_RUNNING, "driver streaming → RUNNING");
    CHECK(driverWireState(ROOMCUT_DRIVER_ENGINE_LOST) == ROOMCUT_STATE_RECOVER, "driver engine-lost → RECOVER");
    CHECK(driverWireState(ROOMCUT_DRIVER_SAFE_BYPASS) == ROOMCUT_STATE_RECOVER, "driver safe-bypass → RECOVER");
    CHECK(driverWireState(ROOMCUT_DRIVER_ENGINE_LOST) == driverWireState(ROOMCUT_DRIVER_SAFE_BYPASS),
          "§4.5: engine-lost and safe-bypass are indistinguishable on the wire");
    CHECK(driverWireState(ROOMCUT_DRIVER_UNLOADING)   == ROOMCUT_STATE_STOPPED, "driver unloading → STOPPED");
}

int main() {
    test_driver_normal_path();
    test_driver_engine_absent();
    test_driver_engine_lost_and_recover();
    test_driver_handoff_failure();
    test_driver_unload_anytime();
    test_driver_unknown_events_noop();
    test_engine_normal_path();
    test_engine_output_lost_recover();
    test_engine_driver_lost();
    test_engine_stop_anytime();
    test_wire_projection();

    if (g_failures == 0) { printf("all lifecycle tests passed\n"); return 0; }
    fprintf(stderr, "%d lifecycle check(s) failed\n", g_failures);
    return 1;
}
