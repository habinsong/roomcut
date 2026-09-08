#include "CoreAudioDevices.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cmath>

namespace roomcut::devices {
namespace {
constexpr char kRoomcutUIDPrefix[] = "RoomcutOutput:";

std::string cfToStd(CFStringRef s) {
    if (s == nullptr) return {};
    char buf[256] = {0};
    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
    return buf;
}

std::string deviceStringProp(AudioDeviceID dev, AudioObjectPropertySelector sel) {
    AudioObjectPropertyAddress addr{sel, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    CFStringRef s = nullptr;
    UInt32 z = sizeof(s);
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &z, &s) != noErr || s == nullptr) {
        return {};
    }
    std::string out = cfToStd(s);
    CFRelease(s);
    return out;
}

bool deviceHasOutputStreams(AudioDeviceID dev) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyStreams,
                                    kAudioObjectPropertyScopeOutput,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &addr, 0, nullptr, &size) != noErr) {
        return false;
    }
    return size > 0;
}

template <typename T>
bool audioObjectNumericProperty(AudioObjectID object,
                                AudioObjectPropertySelector selector,
                                AudioObjectPropertyScope scope,
                                T* out) {
    if (out == nullptr) return false;
    AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
    if (!AudioObjectHasProperty(object, &address)) return false;
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, out) == noErr;
}

UInt32 outputStreamLatencyFrames(AudioDeviceID device) {
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams,
                                       kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr
        || size == 0) {
        return 0;
    }
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, streams.data()) != noErr) {
        return 0;
    }
    streams.resize(size / sizeof(AudioStreamID));
    UInt32 maximum = 0;
    for (AudioStreamID stream : streams) {
        UInt32 latency = 0;
        if (audioObjectNumericProperty(stream, kAudioStreamPropertyLatency,
                                       kAudioObjectPropertyScopeGlobal, &latency)) {
            maximum = std::max(maximum, latency);
        }
    }
    return maximum;
}

AudioStreamID firstOutputStream(AudioDeviceID device) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyStreams,
                                    kAudioObjectPropertyScopeOutput,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &addr, 0, nullptr, &size) != noErr || size == 0) {
        return kAudioObjectUnknown;
    }
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, streams.data()) != noErr
        || streams.empty()) {
        return kAudioObjectUnknown;
    }
    return streams[0];
}

std::vector<AudioStreamRangedDescription> availablePhysicalFormats(AudioStreamID stream) {
    std::vector<AudioStreamRangedDescription> out;
    AudioObjectPropertyAddress addr{kAudioStreamPropertyAvailablePhysicalFormats,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(stream, &addr, 0, nullptr, &size) != noErr || size == 0) {
        return out;
    }
    out.resize(size / sizeof(AudioStreamRangedDescription));
    if (AudioObjectGetPropertyData(stream, &addr, 0, nullptr, &size, out.data()) != noErr) {
        out.clear();
        return out;
    }
    out.resize(size / sizeof(AudioStreamRangedDescription));
    return out;
}

bool currentPhysicalFormat(AudioStreamID stream, AudioStreamBasicDescription* out) {
    AudioObjectPropertyAddress addr{kAudioStreamPropertyPhysicalFormat,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*out);
    return AudioObjectGetPropertyData(stream, &addr, 0, nullptr, &size, out) == noErr;
}

bool formatCarriesRate(const AudioStreamRangedDescription& d, double sampleRate) {
    if (d.mFormat.mSampleRate > 0.0) {
        return std::fabs(d.mFormat.mSampleRate - sampleRate) < 1.0;
    }
    return sampleRate >= d.mSampleRateRange.mMinimum - 1.0
        && sampleRate <= d.mSampleRateRange.mMaximum + 1.0;
}

bool readChannelVolumeScalar(AudioDeviceID device, UInt32 channel, double* out) {
    if (device == kAudioObjectUnknown || out == nullptr) return false;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyVolumeScalar,
                                    kAudioObjectPropertyScopeOutput, channel};
    if (!AudioObjectHasProperty(device, &addr)) return false;
    Float32 v = 0.0f;
    UInt32 size = sizeof(v);
    if (AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, &v) != noErr
        || size != sizeof(v) || !std::isfinite(v)) return false;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    *out = (double)v;
    return true;
}

int writeChannelVolumeScalar(AudioDeviceID device, UInt32 channel, double scalar) {
    if (device == kAudioObjectUnknown) return -1;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyVolumeScalar,
                                    kAudioObjectPropertyScopeOutput, channel};
    if (scalar < 0.0) scalar = 0.0;
    if (scalar > 1.0) scalar = 1.0;
    Float32 v = (Float32)scalar;
    if (AudioObjectSetPropertyData(device, &addr, 0, nullptr, sizeof(v), &v) != noErr) return -2;
    return 0;
}

} // namespace

std::vector<OutputDevice> realOutputs() {
    std::vector<OutputDevice> out;
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDevices,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr
        || size == 0) {
        return out;
    }
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) != noErr) {
        return out;
    }
    ids.resize(size / sizeof(AudioDeviceID));
    for (AudioDeviceID dev : ids) {
        if (!deviceHasOutputStreams(dev)) continue;
        std::string uid = deviceStringProp(dev, kAudioDevicePropertyDeviceUID);
        if (uid.empty() || uid.rfind(kRoomcutUIDPrefix, 0) == 0) continue; // skip our own device
        out.push_back({dev, uid, deviceStringProp(dev, kAudioObjectPropertyName)});
    }
    return out;
}

AudioDeviceID realOutput(const char* uid) {
    if (uid == nullptr || uid[0] == '\0') return kAudioObjectUnknown;
    for (const auto& device : realOutputs()) {
        if (device.uid == uid) return device.id;
    }
    return kAudioObjectUnknown;
}

AudioDeviceID roomcutOutput() {
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDevices,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr
        || size == 0) {
        return kAudioObjectUnknown;
    }
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) != noErr) {
        return kAudioObjectUnknown;
    }
    ids.resize(size / sizeof(AudioDeviceID));
    for (AudioDeviceID dev : ids) {
        std::string uid = deviceStringProp(dev, kAudioDevicePropertyDeviceUID);
        if (uid.rfind(kRoomcutUIDPrefix, 0) == 0) return dev;
    }
    return kAudioObjectUnknown;
}

AudioDeviceID defaultOutput() {
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDefaultOutputDevice,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioDeviceID cur = kAudioObjectUnknown;
    UInt32 z = sizeof(cur);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &z, &cur) != noErr) {
        return kAudioObjectUnknown;
    }
    return cur;
}

int setDefaultOutput(AudioDeviceID dev) {
    if (dev == kAudioObjectUnknown) return -1;
    if (defaultOutput() == dev) return 1;
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDefaultOutputDevice,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    if (AudioObjectSetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                   sizeof(dev), &dev) != noErr) {
        return -2;
    }
    return 0;
}

bool volume(AudioDeviceID device, double* out) {
    if (device == kAudioObjectUnknown || out == nullptr) return false;
    AudioObjectPropertyAddress muteAddr{kAudioDevicePropertyMute,
                                        kAudioObjectPropertyScopeOutput,
                                        kAudioObjectPropertyElementMain};
    UInt32 muted = 0;
    UInt32 muteSize = sizeof(muted);
    if (AudioObjectHasProperty(device, &muteAddr)
        && AudioObjectGetPropertyData(device, &muteAddr, 0, nullptr, &muteSize, &muted) == noErr
        && muted != 0) {
        *out = 0.0;
        return true;
    }

    AudioObjectPropertyAddress volAddr{kAudioDevicePropertyVolumeScalar,
                                       kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    if (!AudioObjectHasProperty(device, &volAddr)) return false;
    Float32 v = 0.0f;
    UInt32 volSize = sizeof(v);
    if (AudioObjectGetPropertyData(device, &volAddr, 0, nullptr, &volSize, &v) != noErr) {
        return false;
    }
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    *out = (double)v;
    return true;
}

int setVolume(AudioDeviceID device, double scalar) {
    if (device == kAudioObjectUnknown) return -1;
    AudioObjectPropertyAddress volAddr{kAudioDevicePropertyVolumeScalar,
                                       kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    if (!AudioObjectHasProperty(device, &volAddr)) return 1;
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(device, &volAddr, &settable) != noErr) return -2;
    if (!settable) return 1;
    if (scalar < 0.0) scalar = 0.0;
    if (scalar > 1.0) scalar = 1.0;
    Float32 v = (Float32)scalar;
    if (AudioObjectSetPropertyData(device, &volAddr, 0, nullptr, sizeof(v), &v) != noErr) {
        return -2;
    }
    if (scalar > 0.0) {
        AudioObjectPropertyAddress muteAddr{kAudioDevicePropertyMute,
                                            kAudioObjectPropertyScopeOutput,
                                            kAudioObjectPropertyElementMain};
        UInt32 muted = 0;
        if (AudioObjectHasProperty(device, &muteAddr)) {
            UInt32 size = sizeof(muted);
            if (AudioObjectGetPropertyData(device, &muteAddr, 0, nullptr, &size, &muted) != noErr
                || size != sizeof(muted)) return -2;
            if (muted != 0) {
                muted = 0;
                if (AudioObjectSetPropertyData(device, &muteAddr, 0, nullptr, sizeof(muted), &muted) != noErr)
                    return -2;
            }
        }
    }
    return 0;
}

int audioFormat(AudioDeviceID device, AudioFormat* out) {
    if (out == nullptr) return -3;
    if (device == kAudioObjectUnknown) return -1;

    Float64 sampleRate = 0;
    UInt32 deviceLatencyFrames = 0;
    UInt32 streamLatencyFrames = 0;
    UInt32 safetyOffsetFrames = 0;
    UInt32 bufferFrames = 0;
    if (!audioObjectNumericProperty(device, kAudioDevicePropertyNominalSampleRate,
                                    kAudioObjectPropertyScopeGlobal, &sampleRate)
        || sampleRate <= 0) {
        return -2;
    }
    audioObjectNumericProperty(device, kAudioDevicePropertyLatency,
                               kAudioObjectPropertyScopeOutput, &deviceLatencyFrames);
    streamLatencyFrames = outputStreamLatencyFrames(device);
    audioObjectNumericProperty(device, kAudioDevicePropertySafetyOffset,
                               kAudioObjectPropertyScopeOutput, &safetyOffsetFrames);
    audioObjectNumericProperty(device, kAudioDevicePropertyBufferFrameSize,
                               kAudioObjectPropertyScopeGlobal, &bufferFrames);

    out->bitDepth = 32;   // fallback if the stream's format can't be read
    const AudioStreamID stream = firstOutputStream(device);
    if (stream != kAudioObjectUnknown) {
        AudioStreamBasicDescription pf{};
        if (currentPhysicalFormat(stream, &pf) && pf.mBitsPerChannel > 0) {
            out->bitDepth = pf.mBitsPerChannel;
        }
    }
    out->sampleRate = sampleRate;
    out->latencyMs = 1000.0
        * (double)(deviceLatencyFrames + streamLatencyFrames
                   + safetyOffsetFrames + bufferFrames)
        / sampleRate;
    return 0;
}

int formatOptions(AudioDeviceID device, std::vector<FormatOption>& pairs) {
    if (device == kAudioObjectUnknown) return -1;
    const AudioStreamID stream = firstOutputStream(device);
    if (stream == kAudioObjectUnknown) return -1;

    // Expand sample-rate ranges against the standard PCM rates so a device that
    // advertises a continuous range still yields concrete pickable rates.
    static const double kStdRates[] = {
        44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0,
        352800.0, 384000.0, 705600.0, 768000.0};

    pairs.clear();
    auto addPair = [&](double sr, uint32_t bits) {
        if (sr <= 0.0 || bits == 0) return;
        for (const auto& p : pairs) {
            if (std::fabs(p.sampleRate - sr) < 1.0 && p.bitDepth == bits) return;
        }
        pairs.push_back({sr, bits});
    };
    for (const auto& d : availablePhysicalFormats(stream)) {
        const uint32_t bits = d.mFormat.mBitsPerChannel;
        if (d.mFormat.mSampleRate > 0.0) {
            addPair(d.mFormat.mSampleRate, bits);
        } else {
            for (double sr : kStdRates) {
                if (sr >= d.mSampleRateRange.mMinimum - 1.0
                    && sr <= d.mSampleRateRange.mMaximum + 1.0) {
                    addPair(sr, bits);
                }
            }
        }
    }

    return (int)pairs.size();
}

int setFormat(AudioDeviceID device, double sampleRate, unsigned bitDepth) {
    if (device == kAudioObjectUnknown) return -1;
    const AudioStreamID stream = firstOutputStream(device);
    if (stream == kAudioObjectUnknown) return -1;

    // Pick an available physical format matching the requested bit depth that can
    // carry the requested rate; only available formats are ever applied.
    AudioStreamBasicDescription chosen{};
    bool found = false;
    for (const auto& d : availablePhysicalFormats(stream)) {
        if (d.mFormat.mBitsPerChannel != bitDepth) continue;
        if (formatCarriesRate(d, sampleRate)) { chosen = d.mFormat; found = true; break; }
    }
    if (!found) return -2;

    chosen.mSampleRate = sampleRate;
    AudioObjectPropertyAddress pfAddr{kAudioStreamPropertyPhysicalFormat,
                                      kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMain};
    if (AudioObjectSetPropertyData(stream, &pfAddr, 0, nullptr, sizeof(chosen), &chosen) != noErr) {
        return -3;
    }
    // Match the nominal rate so the engine (which polls it) re-opens its output.
    AudioObjectPropertyAddress srAddr{kAudioDevicePropertyNominalSampleRate,
                                      kAudioObjectPropertyScopeGlobal,
                                      kAudioObjectPropertyElementMain};
    Float64 sr = sampleRate;
    if (AudioObjectSetPropertyData(device, &srAddr, 0, nullptr, sizeof(sr), &sr) != noErr) return -4;
    // Drive the WHOLE chain to this rate: set the Roomcut virtual device's
    // nominal rate too. coreaudiod then feeds the ring at this rate and the
    // engine matches the real device to it. Setting the real device
    // alone is reverted by the engine (it follows the ring = Roomcut device
    // rate), so this is the line that actually makes a user pick stick.
    AudioDeviceID roomcutDev = roomcutOutput();
    if (roomcutDev != kAudioObjectUnknown) {
        if (AudioObjectSetPropertyData(roomcutDev, &srAddr, 0, nullptr, sizeof(sr), &sr) != noErr) return -5;
    }
    return 0;
}

int balance(AudioDeviceID dev, double* outPan) {
    if (outPan == nullptr) return -3;
    if (dev == kAudioObjectUnknown) return -1;
    double l = 0.0, r = 0.0;
    if (!readChannelVolumeScalar(dev, 1, &l) || !readChannelVolumeScalar(dev, 2, &r)) {
        return 1;   // device has no independent per-channel volume control
    }
    // Ratio-based so it's independent of the master level: whichever channel is
    // louder is the "near" side at unity; the other's attenuation is the pan.
    double pan;
    if (l < r)      pan =  (1.0 - (r > 0.0 ? l / r : 1.0));   // right louder → panned right (+)
    else if (r < l) pan = -(1.0 - (l > 0.0 ? r / l : 1.0));   // left louder  → panned left (−)
    else            pan = 0.0;
    if (pan < -1.0) pan = -1.0;
    if (pan > 1.0) pan = 1.0;
    *outPan = pan;
    return 0;
}

int setBalance(AudioDeviceID dev, double pan) {
    if (!std::isfinite(pan)) return -3;
    if (pan < -1.0) pan = -1.0;
    if (pan > 1.0) pan = 1.0;
    if (dev == kAudioObjectUnknown) return -1;
    for (UInt32 channel : {1u, 2u}) {
        AudioObjectPropertyAddress address{kAudioDevicePropertyVolumeScalar,
                                           kAudioObjectPropertyScopeOutput, channel};
        if (!AudioObjectHasProperty(dev, &address)) return 1;
        Boolean writable = false;
        if (AudioObjectIsPropertySettable(dev, &address, &writable) != noErr) return -2;
        if (!writable) return 1;
    }
    // Level-preserving balance: keep the "near" channel at its CURRENT level (the
    // reference the volume slider set) and attenuate the opposite channel by |pan|.
    // Using the current max as the reference — rather than forcing unity — means
    // touching balance never jumps the overall loudness, even on devices that couple
    // the master and per-channel volumes. Centre → both back to the reference.
    double l = 0.0, r = 0.0;
    if (!readChannelVolumeScalar(dev, 1, &l) || !readChannelVolumeScalar(dev, 2, &r)) return -2;
    const double ref = std::max(l, r);
    const double leftGain  = pan > 0.0 ? ref * (1.0 - pan) : ref;
    const double rightGain = pan < 0.0 ? ref * (1.0 + pan) : ref;
    const int rl = writeChannelVolumeScalar(dev, 1, leftGain);
    if (rl != 0) return rl;
    return writeChannelVolumeScalar(dev, 2, rightGain);
}

} // namespace roomcut::devices
