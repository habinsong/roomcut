#ifndef ROOMCUT_VOLUME_CONTROLLER_HPP
#define ROOMCUT_VOLUME_CONTROLLER_HPP

#include "AudioPropertyListener.hpp"

namespace roomcut {
// Control-thread API. HAL volume notifications and hardware writes are
// serialized on a dedicated queue; render reads only the published gain.
class VolumeController {
public:
    VolumeController();
    ~VolumeController();
    VolumeController(const VolumeController&) = delete;
    VolumeController& operator=(const VolumeController&) = delete;
    void setSource(AudioDeviceID device);
    void setTarget(AudioDeviceID device);
    void setBoost(float boost);
    float boost() const;
    float renderGain() const;
    void poll();
    void refresh();
    void stop();
private:
    struct State;
    static void apply(State& state, bool force);
    std::shared_ptr<State> state_;
    dispatch_queue_t queue_;
    AudioPropertyListener volume_, mute_;
    AudioDeviceID source_ = kAudioObjectUnknown;
    bool stopped_ = false;
};
} // namespace roomcut
#endif
