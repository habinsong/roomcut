#include "DeviceWatcher.hpp"

namespace roomcut {
void DeviceWatcher::install() {
    const auto changed = changed_;
    auto callback = ^(UInt32, const AudioObjectPropertyAddress*) { changed->store(true, std::memory_order_relaxed); };
    if (!defaultOutput_.registered()) defaultOutput_.observe(kAudioObjectSystemObject,
        {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain}, nullptr, callback);
    if (!devices_.registered()) devices_.observe(kAudioObjectSystemObject,
        {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain}, nullptr, callback);
}
void DeviceWatcher::watchSR(AudioDeviceID device) {
    if (device == rateDevice_ && sampleRate_.registered()) return;
    rateDevice_ = device;
    const auto changed = changed_;
    sampleRate_.observe(device,
        {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain}, nullptr,
        ^(UInt32, const AudioObjectPropertyAddress*) { changed->store(true, std::memory_order_relaxed); });
}
void DeviceWatcher::remove() {
    defaultOutput_.reset(); devices_.reset(); sampleRate_.reset();
    rateDevice_ = kAudioObjectUnknown;
    changed_->store(false, std::memory_order_relaxed);
}
} // namespace roomcut
