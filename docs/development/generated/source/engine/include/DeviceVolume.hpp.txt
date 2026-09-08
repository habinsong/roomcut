#ifndef ROOMCUT_DEVICE_VOLUME_HPP
#define ROOMCUT_DEVICE_VOLUME_HPP

#include <CoreAudio/CoreAudio.h>
#include <vector>

namespace roomcut {

float roomcutMasterGain(AudioDeviceID device, float fallback = 1.0f);

// Hardware volume and per-channel balance belong to one physical device.
// Non-realtime; callers serialize access with their volume-control queue.
class DeviceVolume {
public:
    void setDevice(AudioDeviceID device) {
        if (device_ != device) { device_ = device; ratios_.clear(); }
    }
    bool apply(float scalar);
private:
    AudioDeviceID device_ = kAudioObjectUnknown;
    std::vector<float> ratios_;
};

} // namespace roomcut
#endif
