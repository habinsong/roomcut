#ifndef ROOMCUT_AUDIO_PROPERTY_LISTENER_HPP
#define ROOMCUT_AUDIO_PROPERTY_LISTENER_HPP

#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <memory>

namespace roomcut {

// Owns the exact block/queue identity required to remove a HAL subscription.
// A retained but disabled callback never dereferences its former owner.
class AudioPropertyListener {
public:
    AudioPropertyListener() = default;
    ~AudioPropertyListener();
    AudioPropertyListener(const AudioPropertyListener&) = delete;
    AudioPropertyListener& operator=(const AudioPropertyListener&) = delete;
    OSStatus observe(AudioObjectID object, AudioObjectPropertyAddress address,
                     dispatch_queue_t queue, AudioObjectPropertyListenerBlock callback);
    OSStatus reset();
    bool registered() const { return registered_ && enabled_->load(std::memory_order_acquire); }
private:
    void release();
    AudioObjectID object_ = kAudioObjectUnknown;
    AudioObjectPropertyAddress address_{};
    dispatch_queue_t queue_ = nullptr;
    AudioObjectPropertyListenerBlock block_ = nullptr;
    std::shared_ptr<std::atomic<bool>> enabled_;
    bool registered_ = false;
};

} // namespace roomcut
#endif
