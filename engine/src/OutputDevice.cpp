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

#include <cstdio>
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

// The one render generation allowed to consume the ring (see OutputDevice.hpp).
std::atomic<RenderGate*> OutputDevice::sActiveGate_{nullptr};

OutputDevice::~OutputDevice() {
    close();
}

OSStatus OutputDevice::renderThunk(void* inRefCon,
                                   AudioUnitRenderActionFlags* /*ioActionFlags*/,
                                   const AudioTimeStamp* /*inTimeStamp*/,
                                   UInt32 /*inBusNumber*/,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
    auto* gate = static_cast<RenderGate*>(inRefCon);
    if (ioData == nullptr || ioData->mNumberBuffers == 0) {
        return noErr;
    }
    // Interleaved => a single buffer holding frames*channels floats.
    auto* dst = static_cast<float*>(ioData->mBuffers[0].mData);
    if (dst == nullptr) {
        return noErr;
    }
    // Single-consumer guard: only the ONE currently-active render generation may
    // pull the ring. A zombie unit (its IO thread outlived a noErr dispose — seen
    // on USB DACs when the device re-rates) carries a gate that is no longer the
    // active one, so it writes silence and never touches the SPSC ring. This holds
    // no matter how the zombie came to exist — the active slot is the hard
    // single-consumer invariant, independent of per-gate disarm below.
    PullFn pull = (gate == sActiveGate_.load(std::memory_order_acquire))
                      ? gate->pull.load(std::memory_order_acquire)
                      : nullptr;
    if (pull) {
        pull(gate->ctx, dst, (uint32_t)inNumberFrames, gate->channels);
    } else {
        std::memset(dst, 0, (size_t)inNumberFrames * gate->channels * sizeof(float));
    }
    return noErr;
}

OSStatus OutputDevice::open(PullFn pull, void* ctx, uint32_t channels,
                            AudioDeviceID device, double desiredRate) {
    close();
    channels_ = channels;
    gate_ = new RenderGate;
    gate_->ctx      = ctx;
    gate_->channels = channels;
    gate_->pull.store(pull, std::memory_order_release);

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
    cb.inputProcRefCon = gate_;
    err = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input, kOutputBus,
                               &cb, sizeof(cb));
    if (err != noErr) { close(); return err; }

    err = AudioUnitInitialize(unit_);
    if (err != noErr) { close(); return err; }

    // Publish this generation as the sole permitted ring consumer, just before
    // the caller starts IO. Any prior generation (including a zombie whose IO
    // thread is still alive) is now non-active and renders silence.
    sActiveGate_.store(gate_, std::memory_order_release);
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
    // Disarm the render gate FIRST — before stop / uninitialize / dispose. On a
    // device or sample-rate switch (notably a USB DAC re-rating, e.g. iFi
    // 96k->384k) AudioComponentInstanceDispose can return noErr yet leave the
    // HAL IO thread alive; that zombie unit keeps firing renderThunk. Disarming
    // up front means every callback it fires from this point on — during
    // teardown or forever after — reads pull==nullptr and writes silence, so it
    // can never pull the single-consumer ring. (Previously the disarm ran AFTER
    // dispose, leaving a window in which a surviving zombie still drained it.)
    if (gate_ != nullptr) {
        gate_->pull.store(nullptr, std::memory_order_release);
        // Retire this generation from the active slot: its unit — and any zombie
        // that outlives dispose — immediately stops being the ring's consumer.
        // compare_exchange so we never clear a newer generation's publish.
        RenderGate* expected = gate_;
        sActiveGate_.compare_exchange_strong(expected, nullptr,
                                             std::memory_order_release,
                                             std::memory_order_relaxed);
    }
    if (unit_ != nullptr) {
        OSStatus serr = noErr;
        if (running_) {
            serr = AudioOutputUnitStop(unit_);
            running_ = false;
        }
        OSStatus uerr = AudioUnitUninitialize(unit_);
        OSStatus derr = AudioComponentInstanceDispose(unit_);
        if (serr != noErr || uerr != noErr || derr != noErr) {
            std::fprintf(stderr,
                "[engine] output teardown failed (stop=%d uninit=%d dispose=%d); "
                "gate already disarmed, zombie unit renders silence\n",
                (int)serr, (int)uerr, (int)derr);
        }
        unit_ = nullptr;
    }
    // Never delete the gate. A disposed-but-alive zombie unit may still hold this
    // pointer and fire renderThunk at any later time, on ANY device — a noErr
    // dispose does not guarantee the IO thread has joined. Freeing it is thus
    // never provably safe: with the freed block still intact the callback would
    // read pull==pullRender and resume draining the ring, the exact
    // multi-consumer warble this guards against (2026-07-15 at 4x384kHz; again on
    // a Mac mini->iFi switch). A disarmed gate is ~24 bytes and renders silence
    // forever; it is intentionally leaked and reclaimed when the engine process
    // exits (the engine follows the app lifecycle, so sessions stay short).
    gate_ = nullptr;
    sampleRate_ = 0.0;
    device_ = kAudioObjectUnknown;
}

} // namespace roomcut
