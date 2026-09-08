#include "DeviceWatcher.hpp"
#include "VolumeController.hpp"
#include <Block.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {
std::atomic<int> failures{0};
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
struct Subscription {
    AudioObjectID object;
    AudioObjectPropertyAddress address;
    dispatch_queue_t queue;
    AudioObjectPropertyListenerBlock block;
    Subscription(AudioObjectID id, AudioObjectPropertyAddress a, dispatch_queue_t q, AudioObjectPropertyListenerBlock b)
        : object(id), address(a), queue(q), block(Block_copy(b)) { if (queue) dispatch_retain(queue); }
    ~Subscription() { Block_release(block); if (queue) dispatch_release(queue); }
    void fire() {
        const auto callback = block;
        const auto a = address;
        if (queue) dispatch_sync(queue, ^{ callback(1, &a); });
        else callback(1, &a);
    }
};
std::mutex mutex;
std::vector<std::shared_ptr<Subscription>> subscriptions;
std::map<AudioObjectID, float> sourceLevels{{10, 0.4f}, {11, 0.7f}};
std::map<AudioObjectID, float> outputLevels;
unsigned writes = 0, reads = 0;
std::atomic<unsigned> inWrite{0}, maxWriters{0};
bool failReads = false, failRemove = false, slowWrites = false;
AudioObjectPropertySelector failAdd = 0;

std::shared_ptr<Subscription> event(AudioObjectID object, AudioObjectPropertySelector selector) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& subscription : subscriptions)
        if (subscription->object == object && subscription->address.mSelector == selector) return subscription;
    return {};
}
void setSource(AudioObjectID object, float level) { std::lock_guard<std::mutex> lock(mutex); sourceLevels[object] = level; }
float output(AudioObjectID object) { std::lock_guard<std::mutex> lock(mutex); return outputLevels[object]; }
unsigned writeCount() { std::lock_guard<std::mutex> lock(mutex); return writes; }
void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    subscriptions.clear(); sourceLevels = {{10, 0.4f}, {11, 0.7f}}; outputLevels.clear();
    writes = reads = 0; failReads = failRemove = slowWrites = false; failAdd = 0;
    inWrite = maxWriters = 0;
}
}

extern "C" {
OSStatus AudioObjectAddPropertyListenerBlock(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                            dispatch_queue_t queue, AudioObjectPropertyListenerBlock callback) {
    std::lock_guard<std::mutex> lock(mutex);
    if (address->mSelector == failAdd) return kAudioHardwareUnspecifiedError;
    subscriptions.push_back(std::make_shared<Subscription>(object, *address, queue, callback));
    return noErr;
}
OSStatus AudioObjectRemovePropertyListenerBlock(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                               dispatch_queue_t queue, AudioObjectPropertyListenerBlock callback) {
    std::lock_guard<std::mutex> lock(mutex);
    if (failRemove) return kAudioHardwareUnspecifiedError;
    const auto found = std::find_if(subscriptions.begin(), subscriptions.end(), [&](const auto& s) {
        return s->object == object && s->address.mSelector == address->mSelector &&
            s->address.mScope == address->mScope && s->address.mElement == address->mElement &&
            s->queue == queue && s->block == callback;
    });
    CHECK(found != subscriptions.end(), "removal uses the registered object/address/queue/block identity");
    if (found == subscriptions.end()) return kAudioHardwareUnknownPropertyError;
    subscriptions.erase(found);
    return noErr;
}
Boolean AudioObjectHasProperty(AudioObjectID object, const AudioObjectPropertyAddress* address) {
    return object == 10 || object == 11 || (object == 20 && address->mSelector == kAudioDevicePropertyVolumeScalar);
}
OSStatus AudioObjectIsPropertySettable(AudioObjectID object, const AudioObjectPropertyAddress* address, Boolean* settable) {
    *settable = object == 20 && address->mSelector == kAudioDevicePropertyVolumeScalar && address->mElement == 0;
    return noErr;
}
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*) {
    return kAudioHardwareUnknownPropertyError;
}
OSStatus AudioObjectGetPropertyData(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                   UInt32, const void*, UInt32* size, void* data) {
    std::lock_guard<std::mutex> lock(mutex);
    ++reads;
    if (failReads) return kAudioHardwareUnspecifiedError;
    if (address->mSelector == kAudioDevicePropertyMute) {
        *static_cast<UInt32*>(data) = 0; *size = sizeof(UInt32);
    } else {
        *static_cast<float*>(data) = sourceLevels[object]; *size = sizeof(float);
    }
    return noErr;
}
OSStatus AudioObjectSetPropertyData(AudioObjectID object, const AudioObjectPropertyAddress*,
                                   UInt32, const void*, UInt32, const void* data) {
    const auto active = ++inWrite;
    auto maximum = maxWriters.load();
    while (active > maximum && !maxWriters.compare_exchange_weak(maximum, active)) {}
    if (slowWrites) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    { std::lock_guard<std::mutex> lock(mutex); outputLevels[object] = *static_cast<const float*>(data); ++writes; }
    --inWrite;
    return noErr;
}
}

static void watcherLifecycle() {
    using namespace roomcut;
    reset();
    std::shared_ptr<Subscription> late;
    {
        DeviceWatcher watcher;
        watcher.install(); watcher.install();
        CHECK(subscriptions.size() == 2, "duplicate install does not register twice");
        event(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice)->fire();
        CHECK(watcher.takeChanges() && !watcher.takeChanges(), "device changes coalesce until consumed");
        watcher.watchSR(20); watcher.watchSR(20);
        CHECK(subscriptions.size() == 3, "same sample-rate target does not re-register");
        late = event(20, kAudioDevicePropertyNominalSampleRate);
        watcher.watchSR(21);
        late->fire();
        CHECK(!watcher.takeChanges(), "retired device cannot publish a delayed notification");
        event(21, kAudioDevicePropertyNominalSampleRate)->fire();
        CHECK(watcher.takeChanges(), "new device sample-rate notification arrives");
        watcher.markChanged(); CHECK(watcher.takeChanges(), "recovery can explicitly re-arm device work");
        watcher.remove();
        CHECK(subscriptions.empty(), "watcher removes every successful registration");
    }
    late->fire(); // Retained HAL callback after owner destruction; must not dereference it.

    reset();
    {
        DeviceWatcher watcher;
        failAdd = kAudioHardwarePropertyDevices;
        watcher.install(); CHECK(subscriptions.size() == 1, "partial registration records only success");
        failAdd = 0;
        watcher.install(); CHECK(subscriptions.size() == 2, "failed registration can be retried");
        late = event(kAudioObjectSystemObject, kAudioHardwarePropertyDevices);
        failRemove = true;
        watcher.remove();
        late->fire();
        CHECK(!watcher.takeChanges(), "failed removal disables retained callbacks");
        failRemove = false;
        watcher.install();
        CHECK(subscriptions.size() == 2, "failed removal identity is reused for retry without leaking registrations");
        event(kAudioObjectSystemObject, kAudioHardwarePropertyDevices)->fire();
        CHECK(watcher.takeChanges(), "successful retry restores notifications");
    }
    late->fire();
    reset();
}

static void volumeEventsAndFallback() {
    using namespace roomcut;
    reset();
    std::shared_ptr<Subscription> late;
    {
        VolumeController volume;
        volume.setSource(10); volume.setTarget(20); volume.setBoost(1.8f);
        CHECK(std::fabs(output(20) - 0.4f) < 1e-6 && volume.renderGain() == 1.8f, "hardware carries volume, render carries boost");
        late = event(10, kAudioDevicePropertyVolumeScalar);
        setSource(10, 0.2f); late->fire();
        CHECK(output(20) == 0.2f, "notification updates hardware without waiting for the control-loop poll");
        setSource(10, 0); late->fire();
        CHECK(volume.renderGain() == 0, "mute remains silent if another hardware control changes the device level");
        setSource(10, 0.3f); volume.poll();
        CHECK(output(20) == 0.3f, "poll recovers a missed event");
        const auto written = writeCount();
        for (int i = 0; i < 10; ++i) volume.poll();
        CHECK(writeCount() == written, "unchanged polling does not repeat hardware writes");
        failReads = true; volume.poll(); failReads = false;
        CHECK(output(20) == 0.3f, "read error does not jump to full volume");
        volume.setTarget(21);
        CHECK(std::fabs(volume.renderGain() - 0.54f) < 1e-6, "fixed-volume output receives digital fallback with boost");
        setSource(10, 0.25f); late->fire();
        CHECK(std::fabs(volume.renderGain() - 0.45f) < 1e-6 && output(20) == 0.3f, "new target does not write the old device");
        volume.setTarget(20); volume.setSource(11);
        CHECK(output(20) == 0.7f, "source replacement applies its current level");
        setSource(10, 0.1f); late->fire();
        CHECK(output(20) == 0.7f, "retired source notification cannot overwrite replacement level");
        volume.setSource(kAudioObjectUnknown);
        CHECK(output(20) == 0.7f, "source disappearance does not jump hardware to full volume");
        volume.setSource(11);
        late = event(11, kAudioDevicePropertyMute);
        volume.stop(); volume.stop();
        CHECK(subscriptions.empty(), "volume stop releases registrations once");
    }
    const auto written = writeCount();
    unsigned readBefore = reads;
    late->fire();
    CHECK(writeCount() == written && reads == readBefore, "late callback after destruction performs no HAL I/O");
    reset();
}

static void concurrentNotificationsAreSerialized() {
    using namespace roomcut;
    reset(); slowWrites = true;
    VolumeController volume;
    volume.setSource(10); volume.setTarget(20);
    auto notification = event(10, kAudioDevicePropertyVolumeScalar);
    std::thread notifications([&] {
        for (int i = 0; i < 100; ++i) {
            setSource(10, 0.1f + (i % 8) * 0.1f);
            notification->fire();
        }
    });
    std::thread render([&] {
        for (int i = 0; i < 10000; ++i) {
            const float gain = volume.renderGain();
            CHECK(std::isfinite(gain) && gain >= 0 && gain <= 2, "render observes only a valid atomic gain");
        }
    });
    for (int i = 0; i < 100; ++i) volume.setBoost(1.0f + (i % 10) * 0.1f);
    notifications.join(); render.join();
    setSource(10, 0.42f); volume.setBoost(1.5f); volume.refresh();
    CHECK(output(20) == 0.42f && volume.renderGain() == 1.5f, "latest source and boost win after concurrent work");
    CHECK(maxWriters == 1, "HAL notifications and control commands never write hardware concurrently");
    volume.stop();
}

int main() {
    watcherLifecycle();
    volumeEventsAndFallback();
    concurrentNotificationsAreSerialized();
    if (!failures) std::puts("all device service tests passed (fake HAL, real dispatch queues)");
    return failures ? 1 : 0;
}
