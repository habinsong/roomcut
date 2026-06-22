/*
 * OutputDevice.hpp — render audio to the real output device (Phase 4).
 *
 * Wraps an AudioUnit (kAudioUnitSubType_HALOutput) on the system's current
 * default output device. The unit pulls audio from us via a render callback;
 * each callback we ask the supplied PullFn for `frames` of interleaved float32.
 * That pull is where the engine drains the shared ring and runs the DSP chain
 * (wired in main.cpp) — OutputDevice itself knows nothing about rings or DSP, so
 * it stays unit-testable and replaceable.
 *
 * Real-time contract: the PullFn runs on the audio render thread. It must not
 * allocate, lock, or log. OutputDevice guarantees `dst` holds room for
 * frames * channels floats and is the unit's own buffer (interleaved).
 *
 * Phase 4 scope: open the *current default* output device at its native sample
 * rate, 2ch float32. Device-change watching and fallback are Phase 5
 * (DeviceWatcher / RecoveryManager); SR conversion when device SR != ring SR is
 * a later step (docs/03 AudioFormatManager) — for now we report the device rate
 * and the caller decides.
 */
#ifndef ROOMCUT_OUTPUT_DEVICE_HPP
#define ROOMCUT_OUTPUT_DEVICE_HPP

#include <cstdint>

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

namespace roomcut {

// Pull callback: fill `dst` with `frames` of interleaved float32 (channels from
// OutputDevice::channels()). Runs on the render thread — RT-safe only. `ctx` is
// the opaque pointer passed to open().
using PullFn = void (*)(void* ctx, float* dst, uint32_t frames, uint32_t channels);

class OutputDevice {
public:
    OutputDevice() = default;
    ~OutputDevice();

    OutputDevice(const OutputDevice&) = delete;
    OutputDevice& operator=(const OutputDevice&) = delete;

    // Open an output device and install the render callback. `device` picks
    // the target; kAudioObjectUnknown means "the current default output"
    // (Phase 4 behavior — Phase 5 recovery passes an explicit, policy-picked
    // device). `channels` is the interleaved channel count (2 for MVP).
    // Returns noErr on success; on failure the object stays closed
    // (valid() == false).
    // `desiredRate` (Hz, 0 = leave as-is): when > 0 and the device supports it,
    // the device's nominal sample rate is switched to match BEFORE opening, so
    // the engine can feed it 1:1 (bit-exact) instead of resampling — the
    // difference between transparent and a soft, low-resolution sound. Falls
    // back to the device's native rate if it can't do `desiredRate`.
    OSStatus open(PullFn pull, void* ctx, uint32_t channels = 2,
                  AudioDeviceID device = kAudioObjectUnknown,
                  double desiredRate = 0.0);

    OSStatus start();
    OSStatus stop();
    void     close();

    bool valid() const { return unit_ != nullptr; }

    // True between a successful start() and stop()/close(): the unit is
    // actively pulling the render callback.
    bool running() const { return running_; }

    // The device's native sample rate (0 until open() succeeds). The caller
    // compares this to the ring's SR to decide on conversion (Phase 4+).
    double   sampleRate() const { return sampleRate_; }
    uint32_t channels()   const { return channels_; }

    // The device the unit is bound to (kAudioObjectUnknown until open()).
    AudioDeviceID deviceID() const { return device_; }

private:
    static OSStatus renderThunk(void* inRefCon,
                                AudioUnitRenderActionFlags* ioActionFlags,
                                const AudioTimeStamp* inTimeStamp,
                                UInt32 inBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList* ioData);

    AudioUnit     unit_      = nullptr;
    PullFn        pull_      = nullptr;
    void*         ctx_       = nullptr;
    double        sampleRate_ = 0.0;
    uint32_t      channels_  = 2;
    AudioDeviceID device_    = kAudioObjectUnknown;
    bool          running_   = false;
};

} // namespace roomcut

#endif // ROOMCUT_OUTPUT_DEVICE_HPP
