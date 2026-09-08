#include "DeviceVolume.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
float source = 0.5f;
bool muted = false, readFails = false, shortRead = false, master = true, malformedList = false;
std::array<float, 3> levels{1, 1, 1};
std::array<bool, 3> writable{true, true, true};
int failElement = -1;
unsigned writes = 0;
void reset() {
    source = 0.5f; muted = readFails = shortRead = malformedList = false; master = true;
    levels = {1, 1, 1}; writable = {true, true, true}; failElement = -1; writes = 0;
}
}

extern "C" {
Boolean AudioObjectHasProperty(AudioObjectID device, const AudioObjectPropertyAddress* address) {
    return device == 10 || address->mSelector == kAudioDevicePropertyStreamConfiguration ||
        address->mSelector == kAudioDevicePropertyVolumeScalar;
}
OSStatus AudioObjectIsPropertySettable(AudioObjectID, const AudioObjectPropertyAddress* address, Boolean* result) {
    *result = address->mElement < 3 && writable[address->mElement] && (address->mElement != 0 || master);
    return noErr;
}
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32* size) {
    *size = sizeof(AudioBufferList);
    return noErr;
}
OSStatus AudioObjectGetPropertyData(AudioObjectID device, const AudioObjectPropertyAddress* address,
                                   UInt32, const void*, UInt32* size, void* output) {
    if (device == 10) {
        if (readFails) return kAudioHardwareUnspecifiedError;
        if (address->mSelector == kAudioDevicePropertyMute) *static_cast<UInt32*>(output) = muted;
        else *static_cast<float*>(output) = source;
        if (shortRead) *size = 1;
        return noErr;
    }
    if (address->mSelector == kAudioDevicePropertyStreamConfiguration) {
        AudioBufferList list{};
        list.mNumberBuffers = malformedList ? 100 : 1;
        list.mBuffers[0].mNumberChannels = 2;
        std::memcpy(output, &list, sizeof(list));
        *size = sizeof(list);
        return noErr;
    }
    if (address->mElement >= 3) return kAudioHardwareUnknownPropertyError;
    *static_cast<float*>(output) = levels[address->mElement];
    *size = sizeof(float);
    return noErr;
}
OSStatus AudioObjectSetPropertyData(AudioObjectID, const AudioObjectPropertyAddress* address,
                                   UInt32, const void*, UInt32, const void* input) {
    ++writes;
    if (static_cast<int>(address->mElement) == failElement) return kAudioHardwareUnspecifiedError;
    if (address->mElement >= 3) return kAudioHardwareUnknownPropertyError;
    levels[address->mElement] = *static_cast<const float*>(input);
    return noErr;
}
}

int main(int argc, char**) {
    using namespace roomcut;
    reset();
    CHECK(roomcutMasterGain(kAudioObjectUnknown, 0.25f) == 0.25f, "missing source preserves last known level");
    CHECK(roomcutMasterGain(10) == 0.5f, "read source level");
    muted = true; CHECK(roomcutMasterGain(10) == 0, "mute wins over scalar");
    muted = false;
    source = std::numeric_limits<float>::quiet_NaN();
    CHECK(roomcutMasterGain(10, 0.25f) == 0.25f, "non-finite source preserves last known level");
    readFails = true;
    CHECK(roomcutMasterGain(10, 0.25f) == 0.25f, "read failure preserves last known level");
    readFails = false; shortRead = true; source = 0.8f;
    CHECK(roomcutMasterGain(10, 0.25f) == 0.25f, "short source payload is rejected");

    reset();
    DeviceVolume volume; volume.setDevice(20);
    CHECK(volume.apply(0.4f) && levels[0] == 0.4f && writes == 1, "master volume remains the preferred route");
    reset(); master = false; levels[1] = 0.8f; levels[2] = 0.4f;
    CHECK(volume.apply(0.6f), "per-channel level applies");
    CHECK(std::fabs(levels[1] - 0.6f) < 1e-6f && std::fabs(levels[2] - 0.3f) < 1e-6f, "volume preserves existing channel balance");
    CHECK(volume.apply(0.0f) && volume.apply(0.6f), "mute and restore apply");
    CHECK(std::fabs(levels[2] - 0.3f) < 1e-6f, "mute does not forget channel balance");

    reset(); master = false; failElement = 2;
    CHECK(!volume.apply(0.2f), "partial hardware write is not reported as success");
    CHECK(levels[1] == 1 && levels[2] == 1, "partial write restores prior levels before digital fallback");
    reset(); master = false; writable[2] = false;
    CHECK(!volume.apply(0.2f) && writes == 0, "preflight avoids partial writes to unsupported channels");

    if (argc > 1) {
        reset(); master = false; malformedList = true;
        CHECK(!volume.apply(0.2f) && writes == 0, "truncated channel list is rejected without reading past the buffer");
    }
    if (!failures) std::puts("all device volume tests passed (fake HAL)");
    return failures ? 1 : 0;
}
