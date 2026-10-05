/*
 * OutputProbe.cpp — see OutputProbe.hpp.
 */
#include "OutputProbe.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

#include <atomic>
#include <cstring>
#include <thread>

#include <unistd.h>

namespace roomcut {

struct OutputProbe::State {
    std::atomic<bool> done{false};
    std::atomic<bool> ran{false};
    std::atomic<int> error{0};
};

namespace {

// Never freed. A HAL IO thread can outlive AudioComponentInstanceDispose on USB
// devices (the reason OutputDevice never frees its render gate either), and a
// late callback must find this flag still there. A few bytes per check.
struct Heard { std::atomic<bool> value{false}; };

OSStatus renderSilence(void* refCon, AudioUnitRenderActionFlags* flags, const AudioTimeStamp*,
                       UInt32, UInt32, AudioBufferList* io) {
    if (io != nullptr) {
        for (UInt32 i = 0; i < io->mNumberBuffers; ++i)
            if (io->mBuffers[i].mData) std::memset(io->mBuffers[i].mData, 0, io->mBuffers[i].mDataByteSize);
    }
    if (flags != nullptr) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    static_cast<Heard*>(refCon)->value.store(true, std::memory_order_relaxed);
    return noErr;
}

// noErr only when the device actually pulled a buffer.
OSStatus runOnce(AudioDeviceID device, Heard* heard) {
    AudioComponentDescription desc = {};
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (component == nullptr) return kAudioHardwareUnspecifiedError;
    AudioUnit unit = nullptr;
    OSStatus err = AudioComponentInstanceNew(component, &unit);
    if (err != noErr || unit == nullptr) return err != noErr ? err : kAudioHardwareUnspecifiedError;

    AURenderCallbackStruct callback = {&renderSilence, heard};
    err = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                               &device, sizeof(device));
    if (err == noErr)
        err = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                                   &callback, sizeof(callback));
    if (err == noErr) err = AudioUnitInitialize(unit);
    if (err == noErr) {
        err = AudioOutputUnitStart(unit);
        if (err == noErr) {
            // A Bluetooth stream can start a moment before its first buffer.
            for (int i = 0; i < 400 && !heard->value.load(std::memory_order_relaxed); ++i) usleep(5000); // <= 2 s
            AudioOutputUnitStop(unit);
            if (!heard->value.load(std::memory_order_relaxed)) err = kAudioHardwareNotRunningError;
        }
        AudioUnitUninitialize(unit);
    }
    AudioComponentInstanceDispose(unit);
    return err;
}

} // namespace

bool OutputProbe::start(AudioDeviceID device, const std::string& uid) {
    if (state_ != nullptr || device == kAudioObjectUnknown) return false;
    auto state = std::make_shared<State>();
    auto* heard = new Heard; // see Heard
    std::thread([state, device, heard] {
        const OSStatus err = runOnce(device, heard);
        state->ran.store(err == noErr, std::memory_order_relaxed);
        state->error.store(static_cast<int>(err), std::memory_order_relaxed);
        state->done.store(true, std::memory_order_release);
    }).detach();
    state_ = std::move(state);
    uid_ = uid;
    startedAt_ = Clock::now();
    return true;
}

std::optional<OutputProbe::Result> OutputProbe::poll(Clock::time_point now) {
    if (state_ == nullptr) return std::nullopt;
    Result result;
    result.uid = uid_;
    result.spent = std::chrono::duration_cast<std::chrono::milliseconds>(now - startedAt_);
    if (state_->done.load(std::memory_order_acquire)) {
        result.ran = state_->ran.load(std::memory_order_relaxed);
        result.error = state_->error.load(std::memory_order_relaxed);
    } else if (now - startedAt_ >= kTimeout) {
        result.error = kAudioHardwareNotRunningError;
    } else {
        return std::nullopt;
    }
    state_.reset();
    uid_.clear();
    return result;
}

} // namespace roomcut
