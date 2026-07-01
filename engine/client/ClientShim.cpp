/*
 * ClientShim.cpp — see include/roomcut_client.h.
 *
 * Bridges the plain-C client API onto the C++ control plane (Control.cpp) and
 * the builtin preset table. Compiled into the SwiftPM target CRoomcutClient
 * together with Control.cpp/Heartbeat.cpp (see Package.swift).
 */
#include "roomcut_client.h"

#include "Control.hpp"

#include "presets/BuiltinPresets.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <bootstrap.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

namespace {

constexpr uint32_t kTimeoutMs = 500;

// Cached send right to the engine's Mach service port. Instead of
// bootstrap_look_up on every call (ENGINE_AUDIT.md #5), we cache the port and
// only re-lookup on send failure (engine restarted → stale port). This removes
// tens of µs of launchd round-trip from every control-plane call — meaningful
// at 60 Hz analyzer polling or rapid slider drags.
//
// Thread safety: the app's non-realtime threads (main + SwiftUI async) can race
// here. A simple atomic + deallocate-on-invalidate is sufficient — the worst
// case is two threads both lookup simultaneously on first call (harmless
// duplication; one port leaks one ref until process exit).
static mach_port_t g_cachedPort = MACH_PORT_NULL;

mach_port_t acquireEngine() {
    mach_port_t cached = __atomic_load_n(&g_cachedPort, __ATOMIC_ACQUIRE);
    if (cached != MACH_PORT_NULL) {
        return cached;
    }
    // Cold path: bootstrap lookup.
    mach_port_t bp = MACH_PORT_NULL;
    if (task_get_bootstrap_port(mach_task_self(), &bp) != KERN_SUCCESS) {
        return MACH_PORT_NULL;
    }
    mach_port_t service = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(bp, ROOMCUT_MACH_SERVICE_NAME, &service);
    if (kr != KERN_SUCCESS) {
        return MACH_PORT_NULL;
    }
    // Store; if another thread raced and stored first, we leak one send ref
    // (acceptable for a process-lifetime singleton; no correctness issue).
    __atomic_store_n(&g_cachedPort, service, __ATOMIC_RELEASE);
    return service;
}

// Invalidate the cached port (engine died / send failed). The next call will
// re-lookup. We deallocate the stale ref so the kernel can clean up.
void invalidateEngine() {
    mach_port_t old = __atomic_exchange_n(&g_cachedPort, MACH_PORT_NULL, __ATOMIC_ACQ_REL);
    if (old != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), old);
    }
}

// Shared wrapper: run `fn(service, &status)`, map to the C return convention.
// On send/recv failure, invalidates the cached port and retries once (handles
// the engine-restart case transparently).
template <typename Fn>
int withEngine(Fn&& fn) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        mach_port_t service = acquireEngine();
        if (service == MACH_PORT_NULL) {
            return -1;
        }
        uint32_t status = 0;
        kern_return_t kr = fn(service, &status);
        if (kr == KERN_SUCCESS) {
            return (int)status;
        }
        // Send/recv failed — port is likely stale (engine restarted).
        invalidateEngine();
    }
    return -2;
}

// ---- CoreAudio (in-process): device enumeration + Roomcut volume ----

const char* kRoomcutUIDPrefix = "RoomcutOutput:";

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

struct ShimDevice { AudioDeviceID id; std::string uid; std::string name; };

std::vector<ShimDevice> realOutputDevices() {
    std::vector<ShimDevice> out;
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

AudioDeviceID findRealOutputDevice(const char* uid) {
    if (uid == nullptr || uid[0] == '\0') return kAudioObjectUnknown;
    for (const auto& device : realOutputDevices()) {
        if (device.uid == uid) return device.id;
    }
    return kAudioObjectUnknown;
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

AudioDeviceID findRoomcutDeviceID() {
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

// Does this ranged physical format carry `sampleRate` — either as a concrete
// rate or inside its advertised min/max range?
bool formatCarriesRate(const AudioStreamRangedDescription& d, double sampleRate) {
    if (d.mFormat.mSampleRate > 0.0) {
        return std::fabs(d.mFormat.mSampleRate - sampleRate) < 1.0;
    }
    return sampleRate >= d.mSampleRateRange.mMinimum - 1.0
        && sampleRate <= d.mSampleRateRange.mMaximum + 1.0;
}

double clampEffectiveVolume(double scalar) {
    if (!std::isfinite(scalar)) return 1.0;
    if (scalar < 0.0) return 0.0;
    if (scalar > 2.0) return 2.0;
    return scalar;
}

double clampVolumeBoost(double boost) {
    if (!std::isfinite(boost)) return 1.0;
    if (boost < 1.0) return 1.0;
    if (boost > 2.0) return 2.0;
    return boost;
}

bool readDeviceVolumeScalar(AudioDeviceID device, double* out) {
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

int writeDeviceVolumeScalar(AudioDeviceID device, double scalar) {
    if (device == kAudioObjectUnknown) return -1;
    AudioObjectPropertyAddress volAddr{kAudioDevicePropertyVolumeScalar,
                                       kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(device, &volAddr, &settable) != noErr || !settable) {
        return 1;
    }
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
            AudioObjectSetPropertyData(device, &muteAddr, 0, nullptr, sizeof(muted), &muted);
        }
    }
    return 0;
}

int setEngineVolumeBoost(double boost) {
    boost = clampVolumeBoost(boost);
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetVolumeBoost(svc, boost, kTimeoutMs, status);
    });
}

// --- Per-channel output volume (balance / pan) ---------------------------
// Balance is the device's per-channel output volume scalars, the same controls
// Audio MIDI Setup exposes as "Front Left" / "Front Right" (element 1 / 2).
// Reading/writing these keeps the app slider in lock-step with the macOS UI.

// Read one output channel's volume scalar (channel: 1 = left, 2 = right).
bool readChannelVolumeScalar(AudioDeviceID device, UInt32 channel, double* out) {
    if (device == kAudioObjectUnknown || out == nullptr) return false;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyVolumeScalar,
                                    kAudioObjectPropertyScopeOutput, channel};
    if (!AudioObjectHasProperty(device, &addr)) return false;
    Float32 v = 0.0f;
    UInt32 size = sizeof(v);
    if (AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, &v) != noErr) return false;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    *out = (double)v;
    return true;
}

// Write one output channel's volume scalar. 0 ok, 1 not settable, <0 error.
int writeChannelVolumeScalar(AudioDeviceID device, UInt32 channel, double scalar) {
    if (device == kAudioObjectUnknown) return -1;
    AudioObjectPropertyAddress addr{kAudioDevicePropertyVolumeScalar,
                                    kAudioObjectPropertyScopeOutput, channel};
    Boolean settable = false;
    if (AudioObjectIsPropertySettable(device, &addr, &settable) != noErr || !settable) return 1;
    if (scalar < 0.0) scalar = 0.0;
    if (scalar > 1.0) scalar = 1.0;
    Float32 v = (Float32)scalar;
    if (AudioObjectSetPropertyData(device, &addr, 0, nullptr, sizeof(v), &v) != noErr) return -2;
    return 0;
}

// Pick the device whose per-channel balance the user hears: the real output
// device (what Audio MIDI Setup shows for e.g. "iFi USB Audio SE") when one is
// selected, else the Roomcut virtual device. Mirrors the volume device choice.
AudioDeviceID findBalanceDevice() {
    RoomcutClientState state;
    std::memset(&state, 0, sizeof(state));
    if (roomcutClientGetState(&state) == 0 && state.outputDeviceUID[0] != '\0') {
        AudioDeviceID real = findRealOutputDevice(state.outputDeviceUID);
        if (real != kAudioObjectUnknown) return real;
    }
    return findRoomcutDeviceID();
}

} // namespace

extern "C" {

int roomcutClientGetState(RoomcutClientState* out) {
    if (out == nullptr) {
        return -3;
    }
    RoomcutStateReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetState(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    out->state        = rep.state;
    out->manualBypass = rep.manualBypass;
    out->safeBypass   = rep.safeBypass;
    static_assert(sizeof(out->presetId) <= sizeof(rep.presetId),
                  "presetId capacity");
    std::memcpy(out->presetId, rep.presetId, sizeof(out->presetId));
    out->presetId[sizeof(out->presetId) - 1] = '\0';
    out->limiterGainReductionDb = rep.limiterGainReductionDb;
    out->renderPeak             = rep.renderPeak;
    out->paramsRevision         = rep.paramsRevision;
    out->framesRendered         = rep.framesRendered;
    out->ringUnderruns          = rep.ringUnderruns;
    static_assert(sizeof(out->outputDeviceUID) <= sizeof(rep.outputDeviceUID),
                  "outputDeviceUID capacity");
    std::memcpy(out->outputDeviceUID, rep.outputDeviceUID, sizeof(out->outputDeviceUID));
    out->outputDeviceUID[sizeof(out->outputDeviceUID) - 1] = '\0';
    out->keepDefault = rep.keepDefault;
    out->capabilities = rep.capabilities;
    out->volumeBoost = clampVolumeBoost(rep.volumeBoost);
    return 0;
}

int roomcutClientGetParams(RoomcutClientParams* out) {
    if (out == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_EQ_BANDS == ROOMCUT_EQ_BANDS,
                  "EQ band count drifted from the wire protocol");
    RoomcutGetParamsReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetParams(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    out->preampDb         = rep.preampDb;
    std::memcpy(out->eqGainsDb, rep.eqGainsDb, sizeof(out->eqGainsDb));
    out->limiterReleaseMs = rep.limiterReleaseMs;
    out->outputGainDb     = rep.outputGainDb;
    out->spatialWidth     = rep.spatialWidth;
    out->centerFocus      = rep.centerFocus;
    out->crossfeed        = rep.crossfeed;
    out->roomReduce       = rep.roomReduce;
    out->spatialMode      = rep.spatialMode;
    static_assert(ROOMCUT_CLIENT_PARAM_BANDS == ROOMCUT_PARAM_BANDS,
                  "parametric band count drifted from the wire protocol");
    for (int b = 0; b < ROOMCUT_PARAM_BANDS; ++b) {
        out->parametric[b].enabled = rep.parametric[b].enabled;
        out->parametric[b].type    = rep.parametric[b].type;
        out->parametric[b].freqHz  = rep.parametric[b].freqHz;
        out->parametric[b].gainDb  = rep.parametric[b].gainDb;
        out->parametric[b].q       = rep.parametric[b].q;
    }
    return 0;
}

int roomcutClientGetAnalysis(RoomcutClientAnalysis* out) {
    if (out == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_ANALYSIS_SPECTRUM_BINS == ROOMCUT_ANALYSIS_SPECTRUM_BINS,
                  "analysis spectrum bin count drifted from the wire protocol");
    RoomcutAnalysisReply rep;
    std::memset(&rep, 0, sizeof(rep));
    int rc = withEngine([&](mach_port_t svc, uint32_t*) {
        return roomcut::controlGetAnalysis(svc, kTimeoutMs, &rep);
    });
    if (rc != 0) {
        return rc;
    }
    std::memset(out, 0, sizeof(*out));
    out->valid = rep.valid;
    out->sampleRate = rep.sampleRate;
    out->channels = rep.channels;
    out->framesAnalyzed = rep.framesAnalyzed;
    out->peakDb = rep.peakDb;
    out->rmsDb = rep.rmsDb;
    out->crestFactor = rep.crestFactor;
    out->lowEnergy = rep.lowEnergy;
    out->lowMidEnergy = rep.lowMidEnergy;
    out->midEnergy = rep.midEnergy;
    out->highEnergy = rep.highEnergy;
    out->spectralCentroid = rep.spectralCentroid;
    out->stereoWidth = rep.stereoWidth;
    out->midSideRatio = rep.midSideRatio;
    out->correlation = rep.correlation;
    out->muddiness = rep.muddiness;
    out->harshness = rep.harshness;
    out->sibilance = rep.sibilance;
    out->voicePresence = rep.voicePresence;
    out->reverbEstimate = rep.reverbEstimate;
    out->dynamicRange = rep.dynamicRange;
    std::memcpy(out->spectrum, rep.spectrum, sizeof(out->spectrum));
    return 0;
}

int roomcutClientSetPreset(const char* presetId) {
    if (presetId == nullptr) {
        return -3;
    }
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetPreset(svc, presetId, kTimeoutMs, status);
    });
}

int roomcutClientSetBypass(int on) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetBypass(svc, on != 0, kTimeoutMs, status);
    });
}

int roomcutClientSetOutputDevice(const char* uid) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetDevice(svc, uid, kTimeoutMs, status);
    });
}

int roomcutClientSetKeepDefault(int on) {
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetKeepDefault(svc, on != 0, kTimeoutMs, status);
    });
}

int roomcutClientOutputDeviceCount(void) {
    return (int)realOutputDevices().size();
}

int roomcutClientOutputDeviceInfo(int index, char* uidOut, int uidCap,
                                  char* nameOut, int nameCap) {
    const auto devs = realOutputDevices();
    if (index < 0 || index >= (int)devs.size()) {
        return -3;
    }
    const auto& d = devs[(size_t)index];
    if (uidOut != nullptr && uidCap > 0) {
        std::snprintf(uidOut, (size_t)uidCap, "%s", d.uid.c_str());
    }
    if (nameOut != nullptr && nameCap > 0) {
        std::snprintf(nameOut, (size_t)nameCap, "%s", d.name.c_str());
    }
    return 0;
}

int roomcutClientAudioFormat(const char* uid, RoomcutClientAudioFormat* out) {
    if (out == nullptr) return -3;
    const AudioDeviceID device = findRealOutputDevice(uid);
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

int roomcutClientDeviceFormatOptions(const char* uid,
                                     RoomcutClientDeviceFormat* out, int cap) {
    const AudioDeviceID device = findRealOutputDevice(uid);
    if (device == kAudioObjectUnknown) return -1;
    const AudioStreamID stream = firstOutputStream(device);
    if (stream == kAudioObjectUnknown) return -1;

    // Expand sample-rate ranges against the standard PCM rates so a device that
    // advertises a continuous range still yields concrete pickable rates.
    static const double kStdRates[] = {
        44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0,
        352800.0, 384000.0, 705600.0, 768000.0};

    std::vector<RoomcutClientDeviceFormat> pairs;
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

    const int n = (int)pairs.size();
    if (out != nullptr && cap > 0) {
        const int m = n < cap ? n : cap;
        for (int i = 0; i < m; ++i) out[i] = pairs[(size_t)i];
    }
    return n;
}

int roomcutClientSetDeviceFormat(const char* uid, double sampleRate, unsigned bitDepth) {
    const AudioDeviceID device = findRealOutputDevice(uid);
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
    AudioObjectSetPropertyData(device, &srAddr, 0, nullptr, sizeof(sr), &sr);  // best-effort
    // Drive the WHOLE chain to this rate: set the Roomcut virtual device's
    // nominal rate too. coreaudiod then feeds the ring at this rate and the
    // engine matches the real device to it (bit-exact). Setting the real device
    // alone is reverted by the engine (it follows the ring = Roomcut device
    // rate), so this is the line that actually makes a user pick stick.
    AudioDeviceID roomcutDev = findRoomcutDeviceID();
    if (roomcutDev != kAudioObjectUnknown) {
        AudioObjectSetPropertyData(roomcutDev, &srAddr, 0, nullptr, sizeof(sr), &sr);  // best-effort
    }
    return 0;
}

int roomcutClientVolumeGet(double* outScalar) {
    if (outScalar == nullptr) return -3;
    RoomcutClientState state;
    std::memset(&state, 0, sizeof(state));
    const bool haveState = roomcutClientGetState(&state) == 0;
    const double boost = haveState ? clampVolumeBoost(state.volumeBoost) : 1.0;
    if (haveState && state.outputDeviceUID[0] != '\0') {
        double realVolume = 0.0;
        if (readDeviceVolumeScalar(findRealOutputDevice(state.outputDeviceUID), &realVolume)) {
            *outScalar = clampEffectiveVolume(realVolume * boost);
            return 0;
        }
    }

    AudioDeviceID rc = findRoomcutDeviceID();
    if (rc == kAudioObjectUnknown) return -1;
    double roomcutVolume = 0.0;
    if (!readDeviceVolumeScalar(rc, &roomcutVolume)) return 1;
    *outScalar = clampEffectiveVolume(roomcutVolume * boost);
    return 0;
}

int roomcutClientVolumeSet(double scalar) {
    scalar = clampEffectiveVolume(scalar);
    const double hardwareScalar = scalar > 1.0 ? 1.0 : scalar;
    const double boost = scalar > 1.0 ? scalar : 1.0;

    int best = 1;
    RoomcutClientState state;
    std::memset(&state, 0, sizeof(state));
    const bool haveState = roomcutClientGetState(&state) == 0;
    if (haveState && (state.capabilities & ROOMCUT_CLIENT_CAP_VOLUME_BOOST) != 0) {
        (void)setEngineVolumeBoost(boost);
    }
    if (haveState && state.outputDeviceUID[0] != '\0') {
        const int rcReal = writeDeviceVolumeScalar(
            findRealOutputDevice(state.outputDeviceUID), hardwareScalar);
        if (rcReal == 0) best = 0;
        else if (rcReal < 0 && best > 0) best = rcReal;
    }

    AudioDeviceID rc = findRoomcutDeviceID();
    const int rcRoomcut = writeDeviceVolumeScalar(rc, hardwareScalar);
    if (rcRoomcut == 0) return 0;
    return best;
}

int roomcutClientBalanceGet(double* outPan) {
    if (outPan == nullptr) return -3;
    AudioDeviceID dev = findBalanceDevice();
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

int roomcutClientBalanceSet(double pan) {
    if (!std::isfinite(pan)) pan = 0.0;
    if (pan < -1.0) pan = -1.0;
    if (pan > 1.0) pan = 1.0;
    AudioDeviceID dev = findBalanceDevice();
    if (dev == kAudioObjectUnknown) return -1;
    // Level-preserving balance: keep the "near" channel at its CURRENT level (the
    // reference the volume slider set) and attenuate the opposite channel by |pan|.
    // Using the current max as the reference — rather than forcing unity — means
    // touching balance never jumps the overall loudness, even on devices that couple
    // the master and per-channel volumes. Centre → both back to the reference.
    double l = 0.0, r = 0.0;
    double ref = 1.0;
    if (readChannelVolumeScalar(dev, 1, &l) && readChannelVolumeScalar(dev, 2, &r)) {
        ref = std::max(l, r);
        if (ref <= 0.0) ref = 1.0;   // both muted — restore to unity as we pan
    }
    const double leftGain  = pan > 0.0 ? ref * (1.0 - pan) : ref;
    const double rightGain = pan < 0.0 ? ref * (1.0 + pan) : ref;
    const int rl = writeChannelVolumeScalar(dev, 1, leftGain);
    const int rr = writeChannelVolumeScalar(dev, 2, rightGain);
    if (rl == 0 && rr == 0) return 0;
    if (rl < 0) return rl;
    if (rr < 0) return rr;
    return 1;   // not settable
}

int roomcutClientMakeDefaultOutput(void) {
    AudioDeviceID rc = findRoomcutDeviceID();
    if (rc == kAudioObjectUnknown) return -1;
    AudioObjectPropertyAddress addr{kAudioHardwarePropertyDefaultOutputDevice,
                                    kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioDeviceID current = kAudioObjectUnknown;
    UInt32 z = sizeof(current);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &z, &current) == noErr
        && current == rc) {
        return 1; // already the system default
    }
    if (AudioObjectSetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                   sizeof(rc), &rc) != noErr) {
        return -2;
    }
    return 0;
}

int roomcutClientSetParams(double preampDb,
                           const double eqGainsDb[ROOMCUT_CLIENT_EQ_BANDS],
                           double limiterReleaseMs,
                           double outputGainDb, double spatialWidth,
                           double centerFocus, double crossfeed,
                           double roomReduce, double spatialMode,
                           const RoomcutClientParamBand parametric[ROOMCUT_CLIENT_PARAM_BANDS]) {
    if (eqGainsDb == nullptr) {
        return -3;
    }
    static_assert(ROOMCUT_CLIENT_EQ_BANDS == ROOMCUT_EQ_BANDS,
                  "EQ band count drifted from the wire protocol");
    static_assert(ROOMCUT_CLIENT_PARAM_BANDS == ROOMCUT_PARAM_BANDS,
                  "parametric band count drifted from the wire protocol");
    RoomcutParamBand bands[ROOMCUT_PARAM_BANDS];
    std::memset(bands, 0, sizeof(bands));
    if (parametric != nullptr) {
        for (int b = 0; b < ROOMCUT_PARAM_BANDS; ++b) {
            bands[b].enabled = parametric[b].enabled;
            bands[b].type    = parametric[b].type;
            bands[b].freqHz  = parametric[b].freqHz;
            bands[b].gainDb  = parametric[b].gainDb;
            bands[b].q       = parametric[b].q;
        }
    }
    return withEngine([&](mach_port_t svc, uint32_t* status) {
        return roomcut::controlSetParams(svc, preampDb, eqGainsDb,
                                         limiterReleaseMs,
                                         outputGainDb, spatialWidth,
                                         centerFocus, crossfeed, roomReduce, spatialMode,
                                         bands, kTimeoutMs, status);
    });
}

int roomcutClientPresetCount(void) {
    return (int)roomcut::builtinPresets().size();
}

int roomcutClientPresetInfo(int index, char* idOut, int idCap,
                            char* nameOut, int nameCap) {
    const auto presets = roomcut::builtinPresets();
    if (index < 0 || index >= (int)presets.size()) {
        return -3;
    }
    const auto& p = presets[(size_t)index];
    if (idOut != nullptr && idCap > 0) {
        std::snprintf(idOut, (size_t)idCap, "%s", p.id.c_str());
    }
    if (nameOut != nullptr && nameCap > 0) {
        std::snprintf(nameOut, (size_t)nameCap, "%s", p.name.c_str());
    }
    return 0;
}

} // extern "C"
