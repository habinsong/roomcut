/*
 * DeviceSelection.cpp — see DeviceSelection.hpp.
 */
#include "DeviceSelection.hpp"

namespace roomcut {

namespace {

AudioObjectPropertyAddress globalAddr(AudioObjectPropertySelector sel) {
    return { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
}

std::string cfToString(CFStringRef s) {
    if (s == nullptr) return {};
    char buf[256] = {0};
    if (!CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        buf[0] = '\0';
    }
    return std::string(buf);
}

std::string stringProperty(AudioDeviceID dev, AudioObjectPropertySelector sel) {
    AudioObjectPropertyAddress addr = globalAddr(sel);
    CFStringRef s = nullptr;
    UInt32 size = sizeof(s);
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &s) != noErr) {
        return {};
    }
    std::string out = cfToString(s);
    if (s != nullptr) CFRelease(s);
    return out;
}

bool hasOutputStreams(AudioDeviceID dev) {
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr ||
        size < sizeof(AudioBufferList)) {
        return false;
    }
    std::vector<unsigned char> raw(size);
    auto* abl = reinterpret_cast<AudioBufferList*>(raw.data());
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, abl) != noErr) {
        return false;
    }
    UInt32 channels = 0;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) {
        channels += abl->mBuffers[i].mNumberChannels;
    }
    return channels > 0;
}

bool isBuiltin(AudioDeviceID dev) {
    AudioObjectPropertyAddress addr = globalAddr(kAudioDevicePropertyTransportType);
    UInt32 transport = 0;
    UInt32 size = sizeof(transport);
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &transport) != noErr) {
        return false;
    }
    return transport == kAudioDeviceTransportTypeBuiltIn;
}

} // namespace

AudioDeviceID defaultOutputDevice() {
    AudioObjectPropertyAddress addr = globalAddr(kAudioHardwarePropertyDefaultOutputDevice);
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 size = sizeof(dev);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                   &size, &dev) != noErr) {
        return kAudioObjectUnknown;
    }
    return dev;
}

OSStatus setDefaultOutputDevice(AudioDeviceID dev) {
    AudioObjectPropertyAddress addr = globalAddr(kAudioHardwarePropertyDefaultOutputDevice);
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                      sizeof(dev), &dev);
}

std::string deviceUID(AudioDeviceID dev) {
    return stringProperty(dev, kAudioDevicePropertyDeviceUID);
}

std::string deviceName(AudioDeviceID dev) {
    return stringProperty(dev, kAudioObjectPropertyName);
}

double deviceNominalSampleRate(AudioDeviceID dev) {
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    Float64 sr = 0.0;
    UInt32 size = sizeof(sr);
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &size, &sr) != noErr) {
        return 0.0;
    }
    return (double)sr;
}

std::vector<OutputDeviceInfo> listOutputDevices() {
    std::vector<OutputDeviceInfo> out;

    AudioObjectPropertyAddress addr = globalAddr(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr,
                                       &size) != noErr || size == 0) {
        return out;
    }
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                   &size, ids.data()) != noErr) {
        return out;
    }
    ids.resize(size / sizeof(AudioDeviceID));

    for (AudioDeviceID dev : ids) {
        if (!hasOutputStreams(dev)) continue;
        OutputDeviceInfo info;
        info.id      = dev;
        info.uid     = deviceUID(dev);
        info.name    = deviceName(dev);
        info.builtin = isBuiltin(dev);
        out.push_back(std::move(info));
    }
    return out;
}

AudioDeviceID pickRenderDevice(const std::vector<OutputDeviceInfo>& outputs,
                               AudioDeviceID systemDefault,
                               const std::string& preferredUID) {
    const OutputDeviceInfo* def = nullptr;
    for (const auto& d : outputs) {
        if (d.id == systemDefault) { def = &d; break; }
    }
    // 1. The default itself, as long as it is a real device.
    if (def != nullptr && !isRoomcutDeviceUID(def->uid)) {
        return def->id;
    }
    // 2. The saved real device, if still present.
    if (!preferredUID.empty() && !isRoomcutDeviceUID(preferredUID)) {
        for (const auto& d : outputs) {
            if (d.uid == preferredUID) return d.id;
        }
    }
    // 3. First built-in output (never disappears on a laptop / Mac mini).
    for (const auto& d : outputs) {
        if (d.builtin && !isRoomcutDeviceUID(d.uid)) return d.id;
    }
    // 4. Any real output at all.
    for (const auto& d : outputs) {
        if (!isRoomcutDeviceUID(d.uid)) return d.id;
    }
    return kAudioObjectUnknown;
}

} // namespace roomcut
