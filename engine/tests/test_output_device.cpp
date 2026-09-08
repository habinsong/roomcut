// Exercises the production OutputDevice against a fake HAL. Every CoreAudio/
// AudioUnit entry point it uses is defined here; no real device is opened.
#include "OutputDevice.hpp"
#include "OutputRecovery.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
struct Unit {
    AURenderCallbackStruct callback{};
    AudioStreamBasicDescription clientFormat{};
};
Unit* lastUnit = nullptr;
double hardwareRate = 48000;
bool failStart = false;
bool failStop = false;
bool failInitialize = false;
unsigned created = 0, disposed = 0;
int failures = 0;
constexpr OSStatus failure = kAudioHardwareUnspecifiedError;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
}

extern "C" {
AudioComponent AudioComponentFindNext(AudioComponent, const AudioComponentDescription*) {
    return reinterpret_cast<AudioComponent>(uintptr_t(1));
}
OSStatus AudioComponentInstanceNew(AudioComponent, AudioComponentInstance* output) {
    lastUnit = new Unit;
    *output = reinterpret_cast<AudioComponentInstance>(lastUnit);
    ++created;
    return noErr;
}
OSStatus AudioComponentInstanceDispose(AudioComponentInstance instance) {
    auto* unit = reinterpret_cast<Unit*>(instance);
    if (lastUnit == unit) lastUnit = nullptr;
    delete unit;
    ++disposed;
    return noErr;
}
OSStatus AudioUnitSetProperty(AudioUnit instance, AudioUnitPropertyID property,
                              AudioUnitScope, AudioUnitElement, const void* data, UInt32) {
    auto& unit = *reinterpret_cast<Unit*>(instance);
    if (property == kAudioUnitProperty_SetRenderCallback) unit.callback = *static_cast<const AURenderCallbackStruct*>(data);
    else if (property == kAudioUnitProperty_StreamFormat) unit.clientFormat = *static_cast<const AudioStreamBasicDescription*>(data);
    else if (property != kAudioOutputUnitProperty_CurrentDevice) return failure;
    return noErr;
}
OSStatus AudioUnitGetProperty(AudioUnit, AudioUnitPropertyID property,
                              AudioUnitScope, AudioUnitElement, void* data, UInt32* size) {
    if (property != kAudioUnitProperty_StreamFormat || *size < sizeof(AudioStreamBasicDescription)) return failure;
    AudioStreamBasicDescription format{};
    format.mSampleRate = hardwareRate;
    std::memcpy(data, &format, sizeof(format));
    *size = sizeof(format);
    return noErr;
}
OSStatus AudioUnitInitialize(AudioUnit) { return failInitialize ? failure : noErr; }
OSStatus AudioUnitUninitialize(AudioUnit) { return noErr; }
OSStatus AudioOutputUnitStart(AudioUnit) { return failStart ? failure : noErr; }
OSStatus AudioOutputUnitStop(AudioUnit) { return failStop ? failure : noErr; }
OSStatus AudioObjectGetPropertyData(AudioObjectID, const AudioObjectPropertyAddress* address,
                                    UInt32, const void*, UInt32* size, void* data) {
    if (address->mSelector == kAudioHardwarePropertyDefaultOutputDevice && *size >= sizeof(AudioDeviceID)) {
        *static_cast<AudioDeviceID*>(data) = 42;
    } else if (address->mSelector == kAudioDevicePropertyNominalSampleRate && *size >= sizeof(Float64)) {
        *static_cast<Float64*>(data) = hardwareRate;
    } else if (address->mSelector == kAudioDevicePropertyAvailableNominalSampleRates && *size >= sizeof(AudioValueRange)) {
        *static_cast<AudioValueRange*>(data) = AudioValueRange{44100, 192000};
    } else return failure;
    return noErr;
}
OSStatus AudioObjectGetPropertyDataSize(AudioObjectID, const AudioObjectPropertyAddress* address,
                                        UInt32, const void*, UInt32* size) {
    if (address->mSelector != kAudioDevicePropertyAvailableNominalSampleRates) return failure;
    *size = sizeof(AudioValueRange);
    return noErr;
}
OSStatus AudioObjectSetPropertyData(AudioObjectID, const AudioObjectPropertyAddress* address,
                                    UInt32, const void*, UInt32 size, const void* data) {
    if (address->mSelector != kAudioDevicePropertyNominalSampleRate || size != sizeof(Float64)) return failure;
    hardwareRate = *static_cast<const Float64*>(data);
    return noErr;
}
}

namespace {
struct Context { unsigned calls = 0; float value = 0.25f; };
void pull(void* raw, float* output, uint32_t frames, uint32_t channels) {
    auto& context = *static_cast<Context*>(raw);
    ++context.calls;
    for (uint32_t i = 0; i < frames * channels; ++i) output[i] = context.value;
}

AudioUnitRenderActionFlags invoke(AURenderCallbackStruct callback, std::array<float, 16>& samples,
                                 UInt32 bytes = 16 * sizeof(float)) {
    AudioBufferList buffers{};
    buffers.mNumberBuffers = 1;
    buffers.mBuffers[0] = AudioBuffer{2, bytes, samples.data()};
    AudioUnitRenderActionFlags flags = 0;
    AudioTimeStamp timestamp{};
    CHECK(callback.inputProc(callback.inputProcRefCon, &flags, &timestamp, 0, 8, &buffers) == noErr,
          "production callback returns success");
    return flags;
}

void testLifecycleAndLateCallbacks() {
    auto context = std::make_unique<Context>();
    roomcut::OutputDevice device;
    CHECK(device.open(&pull, context.get(), 2, 42, 96000) == noErr, "fake HAL opens through production code");
    CHECK(device.sampleRate() == 96000 && lastUnit->clientFormat.mSampleRate == 96000,
          "client format matches settled hardware rate");
    const auto callback = lastUnit->callback;
    std::array<float, 16> samples; samples.fill(1);
    CHECK(invoke(callback, samples) & kAudioUnitRenderAction_OutputIsSilence,
          "opening cannot render before the caller prepares DSP and starts IO");
    CHECK(context->calls == 0 && samples.back() == 0, "pre-start callbacks cannot touch DSP");
    CHECK(device.start() == noErr, "start activates its generation");
    CHECK(!(invoke(callback, samples) & kAudioUnitRenderAction_OutputIsSilence) && samples.back() == 0.25f,
          "active output renders through the production callback");
    CHECK(device.stop() == noErr, "stop quiesces the generation");
    CHECK(invoke(callback, samples) & kAudioUnitRenderAction_OutputIsSilence, "callbacks after stop are silent");
    CHECK(device.start() == noErr, "the same device can restart safely");
    invoke(callback, samples);
    CHECK(samples.back() == 0.25f && context->calls == 2, "restart re-arms exactly one consumer");
    device.close();
    context.reset();
    invoke(callback, samples);
    CHECK(samples.back() == 0, "callbacks after context destruction never dereference it");

    Context next; next.value = 0.5f;
    CHECK(device.open(&pull, &next) == noErr && device.start() == noErr, "another generation opens");
    const auto newCallback = lastUnit->callback;
    invoke(callback, samples);
    CHECK(next.calls == 0 && samples.back() == 0, "old callback cannot consume a new generation");
    invoke(newCallback, samples);
    CHECK(next.calls == 1 && samples.back() == 0.5f, "new callback reaches the correct context");
    samples.fill(1);
    invoke(newCallback, samples, 4 * sizeof(float));
    CHECK(samples[3] == 0 && samples[4] == 1 && next.calls == 1,
          "short HAL buffers are silenced without overwriting their boundary");
}

void testFailuresLeaveNoLiveTarget() {
    Context context;
    roomcut::OutputDevice device;
    failInitialize = true;
    CHECK(device.open(&pull, &context) != noErr && !device.valid(), "initialize failure leaves a closed device");
    CHECK(!device.running(), "failed first open cannot claim running state");
    failInitialize = false;
    CHECK(device.open(&pull, &context) == noErr, "device can open after a previous failure");
    const auto callback = lastUnit->callback;
    failStart = true;
    CHECK(device.start() != noErr, "start failure is reported");
    CHECK(!device.running(), "failed first start cannot claim running state");
    std::array<float, 16> samples;
    invoke(callback, samples);
    CHECK(context.calls == 0 && samples.back() == 0, "failed start retires its target");
    failStart = false;
    CHECK(device.start() == noErr, "start can be retried after a failure");
    CHECK(device.running(), "successful retry exposes the actual running state");
    invoke(callback, samples);
    CHECK(context.calls == 1, "retry reaches the prepared DSP context");
    failStop = true;
    CHECK(device.stop() != noErr && !device.running(), "even a failed HAL stop retires the render target");
    invoke(callback, samples);
    CHECK(context.calls == 1 && samples.back() == 0, "a HAL unit surviving stop cannot keep rendering");
    failStart = true;
    CHECK(device.start() != noErr && !device.running(), "failed restart cannot expose stale running state");
    failStart = false;
    failStop = false;
}
}

int main() {
    testLifecycleAndLateCallbacks();
    testFailuresLeaveNoLiveTarget();
    {
        roomcut::OutputDevice device;
        roomcut::OutputRecovery policy;
        Context context;
        CHECK(device.open(&pull, &context, 2, 42, 48000) == noErr, "prepare failed-start recovery");
        failStart = true;
        CHECK(device.start() != noErr && !device.running(), "HAL rejects initial start");
        CHECK(device.currentHardwareRate() == device.sampleRate(), "stopped unit may still report matching rates");
        const auto decision = policy.decide({42, 48000, device.deviceID(), device.running(),
            device.sampleRate(), device.currentHardwareRate()}, roomcut::OutputRecovery::Clock::time_point{});
        CHECK(decision.reopen, "recovery retries the actual configured-but-stopped unit");
        device.close();
        failStart = false;
        CHECK(device.open(&pull, &context, 2, 42, decision.matchRingRate ? 48000 : 0) == noErr &&
              device.start() == noErr && device.running(), "subsequent successful start restores output");
        std::array<float, 16> samples{};
        invoke(lastUnit->callback, samples);
        CHECK(context.calls == 1 && samples[0] == context.value, "recovered production callback carries samples");
    }
    CHECK(created == disposed, "every fake HAL instance is disposed");
    if (failures == 0) std::puts("all output device integration tests passed (fake HAL)");
    return failures == 0 ? 0 : 1;
}
