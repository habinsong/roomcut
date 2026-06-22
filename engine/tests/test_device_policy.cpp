/*
 * test_device_policy.cpp — the pure render-target policy (Phase 5,
 * docs/05-recovery): never render to Roomcut's own device; follow the system
 * default when it is real; fall back saved-real → built-in → any real device.
 */
#include "DeviceSelection.hpp"

#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

namespace {

OutputDeviceInfo dev(AudioDeviceID id, const char* uid, bool builtin = false) {
    OutputDeviceInfo d;
    d.id = id;
    d.uid = uid;
    d.name = uid;
    d.builtin = builtin;
    return d;
}

const AudioDeviceID kBuiltinID = 41;
const AudioDeviceID kDacID     = 42;
const AudioDeviceID kRoomcutID = 43;

// The installed-build UID variant (bundle id differs from the current source);
// prefix matching must treat every "RoomcutOutput:*" as Roomcut itself.
const char* kRoomcutUID = "RoomcutOutput:com.habins1.roomcut";

std::vector<OutputDeviceInfo> snapshot() {
    return {
        dev(kBuiltinID, "builtin:speakers", true),
        dev(kDacID, "usb:dac"),
        dev(kRoomcutID, kRoomcutUID),
    };
}

} // namespace

static void test_default_is_real() {
    // Default is a real device → follow it, regardless of the saved UID.
    CHECK(pickRenderDevice(snapshot(), kDacID, "builtin:speakers") == kDacID,
          "real default wins over preferred");
    CHECK(pickRenderDevice(snapshot(), kBuiltinID, "") == kBuiltinID,
          "real default with no preferred");
}

static void test_default_is_roomcut() {
    // Default is Roomcut → never render to it; use the saved real device.
    CHECK(pickRenderDevice(snapshot(), kRoomcutID, "usb:dac") == kDacID,
          "roomcut default → saved real device");
    // Saved device gone → built-in.
    CHECK(pickRenderDevice(snapshot(), kRoomcutID, "usb:gone") == kBuiltinID,
          "saved device missing → builtin");
    // No saved UID → built-in.
    CHECK(pickRenderDevice(snapshot(), kRoomcutID, "") == kBuiltinID,
          "no preferred → builtin");
}

static void test_no_builtin() {
    // Headless box: only a DAC and Roomcut. Default Roomcut, nothing saved.
    std::vector<OutputDeviceInfo> s = {
        dev(kDacID, "usb:dac"),
        dev(kRoomcutID, kRoomcutUID),
    };
    CHECK(pickRenderDevice(s, kRoomcutID, "") == kDacID,
          "no builtin → first real output");
}

static void test_only_roomcut() {
    // Pathological: Roomcut is the only output. Must refuse (no loop).
    std::vector<OutputDeviceInfo> s = { dev(kRoomcutID, kRoomcutUID) };
    CHECK(pickRenderDevice(s, kRoomcutID, "") == kAudioObjectUnknown,
          "only roomcut → no target");
}

static void test_default_missing_from_list() {
    // Default id not in the snapshot (stale/unknown) → fall through to policy.
    CHECK(pickRenderDevice(snapshot(), 999, "usb:dac") == kDacID,
          "unknown default → preferred");
    CHECK(pickRenderDevice(snapshot(), kAudioObjectUnknown, "") == kBuiltinID,
          "no default, no preferred → builtin");
}

static void test_preferred_roomcut_rejected() {
    // A corrupt state file naming Roomcut as 'preferred' must not win.
    CHECK(pickRenderDevice(snapshot(), kRoomcutID, kRoomcutUID) == kBuiltinID,
          "preferred==roomcut is ignored");
}

int main() {
    test_default_is_real();
    test_default_is_roomcut();
    test_no_builtin();
    test_only_roomcut();
    test_default_missing_from_list();
    test_preferred_roomcut_rejected();

    if (g_failures == 0) {
        printf("test_device_policy: ALL PASS\n");
        return 0;
    }
    fprintf(stderr, "test_device_policy: %d FAILURES\n", g_failures);
    return 1;
}
