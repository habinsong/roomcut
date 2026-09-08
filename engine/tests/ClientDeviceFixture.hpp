#pragma once

#include "roomcut_client.h"
#include "Control.hpp"
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <bootstrap.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace client_fixture {
std::atomic<int> failures{0};
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
constexpr AudioDeviceID realID = 10, virtualID = 20;
constexpr AudioStreamID streamID = 100;
mach_port_t endpoint = MACH_PORT_NULL;
struct Channel {
    float volume = 0.4f;
    bool readable = true, writable = true, shortRead = false, failWrite = false;
    unsigned writes = 0;
};
struct Device {
    bool present = true, writable = true, muted = false, muteWritable = true;
    bool hasVolume = true;
    bool failQuery = false, failVolume = false, failMute = false;
    float volume = 0.4f;
    unsigned volumeWrites = 0, muteWrites = 0;
    std::array<Channel, 2> channels;
    double sampleRate = 48000;
    bool failRate = false;
    unsigned rateWrites = 0;
};
Device real, virtualDevice;
AudioDeviceID defaultOutput = realID;
unsigned defaultWrites = 0, physicalWrites = 0, totalWrites = 0;
bool failDefault = false, failPhysical = false;
AudioStreamBasicDescription physicalFormat{};
std::vector<AudioStreamRangedDescription> physicalFormats;
AudioStreamRangedDescription availableFormat(double rate) {
    AudioStreamRangedDescription value{};
    value.mFormat.mSampleRate = rate;
    value.mFormat.mFormatID = kAudioFormatLinearPCM;
    value.mFormat.mBitsPerChannel = 24;
    value.mFormat.mChannelsPerFrame = 2;
    value.mFormat.mBytesPerFrame = value.mFormat.mBytesPerPacket = 6;
    value.mFormat.mFramesPerPacket = 1;
    value.mSampleRateRange = {rate, rate};
    return value;
}
Device* device(AudioDeviceID id) {
    return id == realID ? &real : id == virtualID ? &virtualDevice : nullptr;
}
std::vector<AudioDeviceID> devices() {
    std::vector<AudioDeviceID> ids;
    if (real.present) ids.push_back(realID);
    if (virtualDevice.present) ids.push_back(virtualID);
    return ids;
}
std::mutex engineMutex;
bool supportsBoost = true, rejectState = false, selectReal = true, dropBoost = false;
uint32_t boostStatus = 0;
double appliedBoost = 1;
unsigned boostWrites = 0, stateReads = 0;
void reset() {
    real = {}; virtualDevice = {};
    defaultOutput = realID; defaultWrites = physicalWrites = totalWrites = 0;
    failDefault = failPhysical = false; physicalFormat = availableFormat(48000).mFormat;
    physicalFormats = {availableFormat(48000), availableFormat(96000)};
    std::lock_guard<std::mutex> lock(engineMutex);
    supportsBoost = selectReal = true; rejectState = dropBoost = false;
    boostStatus = 0; appliedBoost = 1; boostWrites = stateReads = 0;
}
bool unchanged() { return real.volume == 0.4f && virtualDevice.volume == 0.4f; }

class Service {
public:
    Service() {
        CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &endpoint) == KERN_SUCCESS,
              "allocate isolated service endpoint");
        worker_ = std::thread([this] { run(); });
    }
    ~Service() {
        stopped_ = true; worker_.join();
        mach_port_mod_refs(mach_task_self(), endpoint, MACH_PORT_RIGHT_RECEIVE, -1);
    }
private:
    std::atomic<bool> stopped_{false};
    std::thread worker_;
    void run() {
        while (!stopped_) {
            RoomcutControlMsgBuffer request{};
            const auto received = mach_msg(&request.raw.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                0, sizeof(request), endpoint, 20, MACH_PORT_NULL);
            if (received == MACH_RCV_TIMED_OUT) continue;
            CHECK(received == KERN_SUCCESS, "receive test request");
            if (received != KERN_SUCCESS) return;
            std::lock_guard<std::mutex> lock(engineMutex);
            if (request.raw.header.msgh_id == ROOMCUT_MSG_STATE) {
                ++stateReads;
                if (rejectState) { mach_msg_destroy(&request.raw.header); continue; }
                RoomcutStateReply reply{};
                reply.capabilities = supportsBoost ? ROOMCUT_CLIENT_CAP_VOLUME_BOOST : 0;
                reply.volumeBoost = appliedBoost;
                if (selectReal) std::strcpy(reply.outputDeviceUID, "test-real");
                CHECK(roomcut::controlReplyState(request.stateRequest, reply) == KERN_SUCCESS,
                      "reply with test engine state");
            } else if (request.raw.header.msgh_id == ROOMCUT_MSG_SET_VOLUME_BOOST) {
                ++boostWrites;
                if (dropBoost) { mach_msg_destroy(&request.raw.header); continue; }
                if (boostStatus == 0) appliedBoost = request.setVolumeBoost.boost;
                CHECK(roomcut::controlReplyAck(request.raw.header, ROOMCUT_MSG_SET_VOLUME_BOOST, boostStatus) == KERN_SUCCESS,
                      "reply with boost outcome");
            } else {
                CHECK(false, "volume API must not send another engine command");
                mach_msg_destroy(&request.raw.header);
            }
        }
    }
};
}

using namespace client_fixture;

// These symbols replace all external device IO. No CoreAudio framework is linked.
extern "C" {
kern_return_t bootstrap_look_up(mach_port_t, const name_t name, mach_port_t* service) {
    CHECK(std::strcmp(name, ROOMCUT_MACH_SERVICE_NAME) == 0, "only resolve the expected service name");
    *service = endpoint;
    return mach_port_insert_right(mach_task_self(), endpoint, endpoint, MACH_MSG_TYPE_MAKE_SEND);
}
Boolean AudioObjectHasProperty(AudioObjectID id, const AudioObjectPropertyAddress* address) {
    if (id == streamID && address->mSelector == kAudioStreamPropertyLatency) return true;
    auto* d = device(id);
    return d && d->present && (address->mSelector != kAudioDevicePropertyVolumeScalar || d->hasVolume);
}
OSStatus AudioObjectIsPropertySettable(AudioObjectID id, const AudioObjectPropertyAddress* address, Boolean* result) {
    auto* d = device(id);
    if (!d || !d->present || d->failQuery) return kAudioHardwareBadObjectError;
    if (address->mSelector == kAudioDevicePropertyVolumeScalar && !d->hasVolume)
        return kAudioHardwareUnknownPropertyError;
    if (address->mSelector == kAudioDevicePropertyVolumeScalar && address->mElement > 0) {
        if (address->mElement > 2) return kAudioHardwareUnknownPropertyError;
        *result = d->channels[address->mElement - 1].writable;
    } else *result = address->mSelector == kAudioDevicePropertyMute ? d->muteWritable : d->writable;
    return noErr;
}
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID id, const AudioObjectPropertyAddress* address,
                                       UInt32, const void*, UInt32* size) {
    if (id == kAudioObjectSystemObject && address->mSelector == kAudioHardwarePropertyDevices) {
        *size = static_cast<UInt32>(devices().size() * sizeof(AudioDeviceID)); return noErr;
    }
    if (device(id) && address->mSelector == kAudioDevicePropertyStreams) {
        *size = sizeof(AudioStreamID); return noErr;
    }
    if (id == streamID && address->mSelector == kAudioStreamPropertyAvailablePhysicalFormats) {
        *size = static_cast<UInt32>(physicalFormats.size() * sizeof(AudioStreamRangedDescription)); return noErr;
    }
    return kAudioHardwareUnknownPropertyError;
}
OSStatus AudioObjectGetPropertyData(AudioObjectID id, const AudioObjectPropertyAddress* address,
                                   UInt32, const void*, UInt32* size, void* output) {
    if (id == kAudioObjectSystemObject && address->mSelector == kAudioHardwarePropertyDefaultOutputDevice) {
        *static_cast<AudioDeviceID*>(output) = defaultOutput; *size = sizeof(defaultOutput); return noErr;
    }
    if (id == kAudioObjectSystemObject && address->mSelector == kAudioHardwarePropertyDevices) {
        const auto ids = devices();
        *size = static_cast<UInt32>(ids.size() * sizeof(AudioDeviceID));
        if (*size) std::memcpy(output, ids.data(), *size);
        return noErr;
    }
    if (id == streamID) {
        if (address->mSelector == kAudioStreamPropertyAvailablePhysicalFormats) {
            *size = static_cast<UInt32>(physicalFormats.size() * sizeof(AudioStreamRangedDescription));
            if (*size) std::memcpy(output, physicalFormats.data(), *size);
            return noErr;
        }
        if (address->mSelector == kAudioStreamPropertyPhysicalFormat) {
            *static_cast<AudioStreamBasicDescription*>(output) = physicalFormat;
            *size = sizeof(physicalFormat); return noErr;
        }
        if (address->mSelector == kAudioStreamPropertyLatency) {
            *static_cast<UInt32*>(output) = 32; *size = sizeof(UInt32); return noErr;
        }
        return kAudioHardwareUnknownPropertyError;
    }
    auto* d = device(id);
    if (!d || !d->present) return kAudioHardwareBadObjectError;
    switch (address->mSelector) {
    case kAudioDevicePropertyStreams:
        *static_cast<AudioStreamID*>(output) = streamID; *size = sizeof(streamID); return noErr;
    case kAudioDevicePropertyNominalSampleRate:
        *static_cast<double*>(output) = d->sampleRate; *size = sizeof(double); return noErr;
    case kAudioDevicePropertyLatency:
        *static_cast<UInt32*>(output) = 64; *size = sizeof(UInt32); return noErr;
    case kAudioDevicePropertySafetyOffset:
        *static_cast<UInt32*>(output) = 16; *size = sizeof(UInt32); return noErr;
    case kAudioDevicePropertyBufferFrameSize:
        *static_cast<UInt32*>(output) = 128; *size = sizeof(UInt32); return noErr;
    case kAudioDevicePropertyDeviceUID: case kAudioObjectPropertyName:
        *static_cast<CFStringRef*>(output) = CFStringCreateWithCString(nullptr,
            id == realID ? "test-real" : "RoomcutOutput:test", kCFStringEncodingUTF8);
        *size = sizeof(CFStringRef); return noErr;
    case kAudioDevicePropertyVolumeScalar:
        if (address->mElement > 0) {
            if (address->mElement > 2) return kAudioHardwareUnknownPropertyError;
            const auto& channel = d->channels[address->mElement - 1];
            if (!channel.readable) return kAudioHardwareUnspecifiedError;
            *static_cast<float*>(output) = channel.volume;
            *size = channel.shortRead ? 1 : sizeof(float); return noErr;
        }
        *static_cast<float*>(output) = d->volume; *size = sizeof(float); return noErr;
    case kAudioDevicePropertyMute:
        *static_cast<UInt32*>(output) = d->muted; *size = sizeof(UInt32); return noErr;
    default: return kAudioHardwareUnknownPropertyError;
    }
}
OSStatus AudioObjectSetPropertyData(AudioObjectID id, const AudioObjectPropertyAddress* address,
                                   UInt32, const void*, UInt32, const void* input) {
    ++totalWrites;
    if (id == kAudioObjectSystemObject && address->mSelector == kAudioHardwarePropertyDefaultOutputDevice) {
        ++defaultWrites;
        if (failDefault) return kAudioHardwareUnspecifiedError;
        defaultOutput = *static_cast<const AudioDeviceID*>(input); return noErr;
    }
    if (id == streamID && address->mSelector == kAudioStreamPropertyPhysicalFormat) {
        ++physicalWrites;
        if (failPhysical) return kAudioHardwareUnspecifiedError;
        physicalFormat = *static_cast<const AudioStreamBasicDescription*>(input); return noErr;
    }
    auto* d = device(id);
    if (!d || !d->present) return kAudioHardwareBadObjectError;
    if (address->mSelector == kAudioDevicePropertyNominalSampleRate) {
        ++d->rateWrites;
        if (d->failRate) return kAudioHardwareUnspecifiedError;
        d->sampleRate = *static_cast<const double*>(input); return noErr;
    }
    if (address->mSelector == kAudioDevicePropertyVolumeScalar) {
        if (address->mElement > 0) {
            if (address->mElement > 2) return kAudioHardwareUnknownPropertyError;
            auto& channel = d->channels[address->mElement - 1];
            ++channel.writes;
            if (!channel.writable || channel.failWrite) return kAudioHardwareUnspecifiedError;
            channel.volume = *static_cast<const float*>(input); return noErr;
        }
        ++d->volumeWrites;
        if (d->failVolume || !d->writable) return kAudioHardwareUnspecifiedError;
        d->volume = *static_cast<const float*>(input); return noErr;
    }
    if (address->mSelector == kAudioDevicePropertyMute) {
        ++d->muteWrites;
        if (d->failMute || !d->muteWritable) return kAudioHardwareUnspecifiedError;
        d->muted = *static_cast<const UInt32*>(input) != 0; return noErr;
    }
    CHECK(false, "volume test must not write an unrelated property");
    return kAudioHardwareUnknownPropertyError;
}
}
