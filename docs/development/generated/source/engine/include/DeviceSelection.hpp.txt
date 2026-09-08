/*
 * DeviceSelection.hpp — control-thread CoreAudio device utilities + the pure
 * render-target policy (Phase 5, docs/05-recovery).
 *
 * The engine must never render to Roomcut's own virtual device: once the user
 * makes Roomcut Output the system default, rendering to the default would feed
 * our own driver (a loop). The render target is always a REAL device — by
 * default the system default, and when that is Roomcut, the real device the
 * user last used. pickRenderDevice() encodes the policy as a pure function
 * over a device snapshot so it is host-testable without CoreAudio; the
 * surrounding helpers do the actual (non-RT) HAL calls.
 */
#ifndef ROOMCUT_DEVICE_SELECTION_HPP
#define ROOMCUT_DEVICE_SELECTION_HPP

#include <string>
#include <vector>

#include <CoreAudio/CoreAudio.h>

namespace roomcut {

// Roomcut's own virtual device — keep in sync with kRoomcut_Device_UID in
// driver/RoomcutHAL/include/RoomcutDriver.h (the driver header is not on the
// engine include path by design). Matched by PREFIX: the suffix carries the
// bundle id, which has differed across installed builds
// ("RoomcutOutput:com.roomcut" vs "RoomcutOutput:com.habins1.roomcut"), and
// the engine must recognize every one of them as itself.
inline constexpr const char* kRoomcutDeviceUIDPrefix = "RoomcutOutput:";

inline bool isRoomcutDeviceUID(const std::string& uid) {
    return uid.rfind(kRoomcutDeviceUIDPrefix, 0) == 0;
}

struct OutputDeviceInfo {
    AudioDeviceID id = kAudioObjectUnknown;
    std::string   uid;
    std::string   name;
    bool          builtin = false; // built-in transport (speakers/headphone jack)
};

// ---- HAL queries (control thread only — they allocate; never on the render
// thread) ----

AudioDeviceID defaultOutputDevice();
OSStatus      setDefaultOutputDevice(AudioDeviceID dev);
std::string   deviceUID(AudioDeviceID dev);
std::string   deviceName(AudioDeviceID dev);
double        deviceNominalSampleRate(AudioDeviceID dev); // 0.0 on failure

// All alive devices with at least one output stream, in HAL order.
std::vector<OutputDeviceInfo> listOutputDevices();

// ---- Pure policy (unit-tested in engine/tests/test_device_policy.cpp) ----
//
// Choose the render target from a snapshot:
//   1. the system default, unless it is Roomcut itself (or missing);
//   2. else `preferredUID` (the saved real device), if still present;
//   3. else the first built-in output;
//   4. else the first non-Roomcut output;
//   5. else kAudioObjectUnknown (nothing usable).
AudioDeviceID pickRenderDevice(const std::vector<OutputDeviceInfo>& outputs,
                               AudioDeviceID systemDefault,
                               const std::string& preferredUID);

} // namespace roomcut

#endif // ROOMCUT_DEVICE_SELECTION_HPP
