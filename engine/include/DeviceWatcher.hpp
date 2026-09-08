#ifndef ROOMCUT_DEVICE_WATCHER_HPP
#define ROOMCUT_DEVICE_WATCHER_HPP

#include "AudioPropertyListener.hpp"

namespace roomcut {
class DeviceWatcher {
public:
    void install();
    void remove();
    void watchSR(AudioDeviceID device);
    void markChanged() { changed_->store(true, std::memory_order_relaxed); }
    bool takeChanges() { return changed_->exchange(false, std::memory_order_relaxed); }
private:
    std::shared_ptr<std::atomic<bool>> changed_ = std::make_shared<std::atomic<bool>>(false);
    AudioPropertyListener defaultOutput_, devices_, sampleRate_;
    AudioDeviceID rateDevice_ = kAudioObjectUnknown;
};
} // namespace roomcut
#endif
