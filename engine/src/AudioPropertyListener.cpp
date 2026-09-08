#include "AudioPropertyListener.hpp"
#include <Block.h>
#include <cstdio>

namespace roomcut {
AudioPropertyListener::~AudioPropertyListener() { reset(); release(); }
OSStatus AudioPropertyListener::observe(AudioObjectID object, AudioObjectPropertyAddress address,
                                        dispatch_queue_t queue, AudioObjectPropertyListenerBlock callback) {
    const auto removed = reset();
    if (removed != noErr) return removed; // Keep the disabled registration identity for retry.
    if (object == kAudioObjectUnknown) return noErr;
    object_ = object;
    address_ = address;
    queue_ = queue;
    if (queue_) dispatch_retain(queue_);
    enabled_ = std::make_shared<std::atomic<bool>>(true);
    const auto enabled = enabled_;
    block_ = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress* addresses) {
        if (enabled->load(std::memory_order_acquire)) callback(count, addresses);
    });
    const auto result = AudioObjectAddPropertyListenerBlock(object_, &address_, queue_, block_);
    registered_ = result == noErr;
    if (!registered_) {
        std::fprintf(stderr, "[engine] property listener registration failed: %d\n", static_cast<int>(result));
        reset();
    }
    return result;
}

OSStatus AudioPropertyListener::reset() {
    if (enabled_) enabled_->store(false, std::memory_order_release);
    if (registered_) {
        const auto result = AudioObjectRemovePropertyListenerBlock(object_, &address_, queue_, block_);
        if (result != noErr && result != kAudioHardwareBadObjectError) {
            std::fprintf(stderr, "[engine] property listener removal failed: %d\n", static_cast<int>(result));
            return result;
        }
    }
    release();
    return noErr;
}

void AudioPropertyListener::release() {
    registered_ = false;
    if (block_) Block_release(block_);
    if (queue_) dispatch_release(queue_);
    block_ = nullptr;
    queue_ = nullptr;
    enabled_.reset();
    object_ = kAudioObjectUnknown;
}
} // namespace roomcut
