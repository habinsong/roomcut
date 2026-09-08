#include "DeviceVolume.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace roomcut {
namespace {
AudioObjectPropertyAddress volumeAddress(UInt32 element) {
    return {kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, element};
}
bool settable(AudioDeviceID device, UInt32 element) {
    const auto address = volumeAddress(element);
    Boolean writable = false;
    return AudioObjectHasProperty(device, &address) &&
        AudioObjectIsPropertySettable(device, &address, &writable) == noErr && writable;
}
bool write(AudioDeviceID device, UInt32 element, float scalar) {
    const auto address = volumeAddress(element);
    return AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(scalar), &scalar) == noErr;
}
UInt32 channelCount(AudioDeviceID device) {
    const AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    constexpr auto headerBytes = offsetof(AudioBufferList, mBuffers);
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size < headerBytes)
        return 0;
    std::vector<unsigned char> bytes(size);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, bytes.data()) != noErr ||
        size > bytes.size() || size < headerBytes) return 0;
    UInt32 buffers = 0;
    std::memcpy(&buffers, bytes.data(), sizeof(buffers));
    if (buffers > (size - headerBytes) / sizeof(AudioBuffer)) return 0;
    UInt32 channels = 0;
    for (UInt32 i = 0; i < buffers; ++i) {
        AudioBuffer buffer{};
        std::memcpy(&buffer, bytes.data() + headerBytes + i * sizeof(buffer), sizeof(buffer));
        // Bound non-realtime HAL work even if a device reports corrupt geometry.
        if (buffer.mNumberChannels > 256 - channels) return 0;
        channels += buffer.mNumberChannels;
    }
    return channels;
}
}

float roomcutMasterGain(AudioDeviceID device, float fallback) {
    if (device == kAudioObjectUnknown) return fallback;
    const AudioObjectPropertyAddress muteAddress{kAudioDevicePropertyMute,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain};
    UInt32 muted = 0, size = sizeof(muted);
    if (AudioObjectHasProperty(device, &muteAddress)) {
        if (AudioObjectGetPropertyData(device, &muteAddress, 0, nullptr, &size, &muted) != noErr || size != sizeof(muted))
            return fallback;
        if (muted) return 0;
    }
    const auto address = volumeAddress(kAudioObjectPropertyElementMain);
    float value = 0;
    size = sizeof(value);
    if (!AudioObjectHasProperty(device, &address) ||
        AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr ||
        size != sizeof(value) || !std::isfinite(value)) return fallback;
    return std::clamp(value, 0.0f, 1.0f);
}

bool DeviceVolume::apply(float scalar) {
    if (device_ == kAudioObjectUnknown || !std::isfinite(scalar)) return false;
    scalar = std::clamp(scalar, 0.0f, 1.0f);
    if (settable(device_, kAudioObjectPropertyElementMain) && write(device_, kAudioObjectPropertyElementMain, scalar))
        return true;
    const auto channels = channelCount(device_);
    if (!channels) return false;
    std::vector<float> previous(channels);
    for (UInt32 channel = 0; channel < channels; ++channel) {
        if (!settable(device_, channel + 1)) return false;
        const auto address = volumeAddress(channel + 1);
        UInt32 size = sizeof(float);
        if (AudioObjectGetPropertyData(device_, &address, 0, nullptr, &size, &previous[channel]) != noErr ||
            size != sizeof(float) || !std::isfinite(previous[channel])) return false;
        previous[channel] = std::clamp(previous[channel], 0.0f, 1.0f);
    }
    if (ratios_.size() != channels) ratios_.assign(channels, 1.0f);
    const auto reference = *std::max_element(previous.begin(), previous.end());
    if (reference > 0) {
        for (UInt32 channel = 0; channel < channels; ++channel) ratios_[channel] = previous[channel] / reference;
    }
    for (UInt32 channel = 0; channel < channels; ++channel) {
        if (!write(device_, channel + 1, scalar * ratios_[channel])) {
            for (UInt32 restored = 0; restored < channel; ++restored) write(device_, restored + 1, previous[restored]);
            return false;
        }
    }
    return true;
}
} // namespace roomcut
