/*
 * OutputDevice.cpp — see OutputDevice.hpp.
 *
 * Opens an HAL output AudioUnit on the current default output device:
 *   - find the default output device (kAudioHardwarePropertyDefaultOutputDevice)
 *   - create the HALOutput AudioUnit, bind it to that device
 *   - set an interleaved float32 stream format at the device's native SR on the
 *     input scope of the output element (bus 0) — that's the format the unit
 *     pulls FROM us and converts to the hardware
 *   - install the render callback, initialize, start
 */
#include "OutputDevice.hpp"

#include <cstring>
#include <vector>

#include <unistd.h>

namespace roomcut {

namespace {

constexpr AudioUnitElement kOutputBus = 0; // output element's input scope = our feed

OSStatus defaultOutputDeviceID(AudioDeviceID* outID) {
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = sizeof(AudioDeviceID);
    return AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                                      &size, outID);
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

// Switch the device to `desired` Hz if it supports it. Returns true when the
// device ends up at `desired`. Matching the device rate to the ring rate is
// what lets the engine feed it 1:1 (bit-exact) instead of resampling.
bool trySetNominalRate(AudioDeviceID dev, double desired) {
    if (desired <= 0.0) return false;
    if (deviceNominalSampleRate(dev) == desired) return true;

    AudioObjectPropertyAddress avail = {
        kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(dev, &avail, 0, nullptr, &size) != noErr || size == 0) {
        return false;
    }
    std::vector<AudioValueRange> ranges(size / sizeof(AudioValueRange));
    if (AudioObjectGetPropertyData(dev, &avail, 0, nullptr, &size, ranges.data()) != noErr) {
        return false;
    }
    bool supported = false;
    for (const auto& r : ranges) {
        if (desired >= r.mMinimum - 1.0 && desired <= r.mMaximum + 1.0) { supported = true; break; }
    }
    if (!supported) return false;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain
    };
    Float64 sr = desired;
    if (AudioObjectSetPropertyData(dev, &addr, 0, nullptr, sizeof(sr), &sr) != noErr) {
        return false;
    }
    for (int i = 0; i < 40; ++i) {            // the change is async; wait <= ~400 ms
        if (deviceNominalSampleRate(dev) == desired) return true;
        usleep(10000);
    }
    return deviceNominalSampleRate(dev) == desired;
}

} // namespace

OutputDevice::~OutputDevice() {
    close();
}

OSStatus OutputDevice::renderThunk(void* inRefCon,
                                   AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                   const AudioTimeStamp* /*inTimeStamp*/,
                                   UInt32 /*inBusNumber*/,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
    auto* self = static_cast<OutputDevice*>(inRefCon);
    if (ioData == nullptr || ioData->mNumberBuffers == 0) {
        return noErr;
    }
    // Interleaved => a single buffer holding frames*channels floats.
    auto* dst = static_cast<float*>(ioData->mBuffers[0].mData);
    if (dst == nullptr) {
        return noErr;
    }
    if (self->pull_) {
        self->pull_(self->ctx_, dst, (uint32_t)inNumberFrames, self->channels_);
    } else {
        std::memset(dst, 0, (size_t)inNumberFrames * self->channels_ * sizeof(float));
    }
    return noErr;
}

OSStatus OutputDevice::open(PullFn pull, void* ctx, uint32_t channels,
                            AudioDeviceID device, double desiredRate) {
    close();
    pull_ = pull;
    ctx_  = ctx;
    channels_ = channels;

    AudioDeviceID dev = device;
    if (dev == kAudioObjectUnknown) {
        OSStatus derr = defaultOutputDeviceID(&dev);
        if (derr != noErr || dev == kAudioObjectUnknown) {
            return (derr != noErr) ? derr : kAudioHardwareBadDeviceError;
        }
    }
    OSStatus err = noErr;

    // Describe + instantiate the HAL output AudioUnit.
    AudioComponentDescription desc = {};
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (comp == nullptr) {
        return kAudioHardwareUnspecifiedError;
    }
    err = AudioComponentInstanceNew(comp, &unit_);
    if (err != noErr || unit_ == nullptr) {
        unit_ = nullptr;
        return err != noErr ? err : kAudioHardwareUnspecifiedError;
    }

    // Bind the unit to the chosen device.
    err = AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice,
                               kAudioUnitScope_Global, kOutputBus,
                               &dev, sizeof(dev));
    if (err != noErr) { close(); return err; }
    device_ = dev;

    // Match the device to the ring rate when possible → bit-exact passthrough.
    trySetNominalRate(dev, desiredRate);
    sampleRate_ = deviceNominalSampleRate(dev);
    if (sampleRate_ <= 0.0) sampleRate_ = 48000.0;

    // Tell the unit what WE produce: interleaved float32 at the device rate.
    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate       = sampleRate_;
    asbd.mFormatID         = kAudioFormatLinearPCM;
    asbd.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked; // interleaved
    asbd.mFramesPerPacket  = 1;
    asbd.mChannelsPerFrame = channels_;
    asbd.mBitsPerChannel   = 32;
    asbd.mBytesPerFrame    = sizeof(float) * channels_;
    asbd.mBytesPerPacket   = asbd.mBytesPerFrame;

    err = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                               kAudioUnitScope_Input, kOutputBus,
                               &asbd, sizeof(asbd));
    if (err != noErr) { close(); return err; }

    // Install the render callback.
    AURenderCallbackStruct cb = {};
    cb.inputProc       = &OutputDevice::renderThunk;
    cb.inputProcRefCon = this;
    err = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input, kOutputBus,
                               &cb, sizeof(cb));
    if (err != noErr) { close(); return err; }

    err = AudioUnitInitialize(unit_);
    if (err != noErr) { close(); return err; }

    return noErr;
}

OSStatus OutputDevice::start() {
    if (unit_ == nullptr) return kAudioHardwareBadObjectError;
    if (running_) return noErr;
    OSStatus err = AudioOutputUnitStart(unit_);
    if (err == noErr) running_ = true;
    return err;
}

OSStatus OutputDevice::stop() {
    if (unit_ == nullptr) return kAudioHardwareBadObjectError;
    if (!running_) return noErr;
    OSStatus err = AudioOutputUnitStop(unit_);
    if (err == noErr) running_ = false;
    return err;
}

void OutputDevice::close() {
    if (unit_ != nullptr) {
        if (running_) {
            AudioOutputUnitStop(unit_);
            running_ = false;
        }
        AudioUnitUninitialize(unit_);
        AudioComponentInstanceDispose(unit_);
        unit_ = nullptr;
    }
    pull_ = nullptr;
    ctx_  = nullptr;
    sampleRate_ = 0.0;
    device_ = kAudioObjectUnknown;
}

} // namespace roomcut
