#include "SpatialMixerBedRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "dsp/Upmixer.hpp"

namespace roomcut {

namespace {

// ITU-R BS.775 angles in Upmixer order (C, L, R, Ls, Rs, Lb, Rb), + right.
constexpr double k51Azimuths[5] = {0.0, -30.0, 30.0, -110.0, 110.0};
constexpr double k71Azimuths[7] = {0.0, -30.0, 30.0, -90.0, 90.0, -135.0, 135.0};
constexpr double kFlushSeconds = 0.008;   // longer than the 5 ms impulse response
constexpr double kFadeSeconds = 0.020;

AudioStreamBasicDescription floatFormat(double sampleRate, UInt32 channels) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = sampleRate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBitsPerChannel = 32;
    f.mChannelsPerFrame = channels;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = f.mBytesPerPacket = sizeof(float);
    return f;
}

std::string failure(const char* step, OSStatus status) {
    return std::string("AUSpatialMixer ") + step + " failed (" + std::to_string(static_cast<int>(status)) + ")";
}

} // namespace

bool SpatialMixerBedRenderer::prepare(double sampleRate, std::string& error) {
    release();
    if (!(sampleRate > 0.0) || sampleRate > kMaxSampleRate) {
        error = "sample rate " + std::to_string(static_cast<long>(sampleRate)) + " Hz is above the 192 kHz the unit renders correctly";
        return false;
    }
    if (!openUnit(units_[0], k51Azimuths, 5, sampleRate, error) || !openUnit(units_[1], k71Azimuths, 7, sampleRate, error)) {
        release();
        return false;
    }
    for (std::size_t c = 0; c < silence_.size(); ++c) silencePointers_[c] = silence_[c].data();
    flushBlocks_ = static_cast<std::size_t>(std::ceil(kFlushSeconds * sampleRate / kBlockFrames));
    fadeBlocks_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(kFadeSeconds * sampleRate / kBlockFrames)));
    fadeBlocksLeft_ = 0;
    activeLayout_ = fadingLayout_ = Upmixer::kOff;
    ready_ = true;
    return true;
}

void SpatialMixerBedRenderer::release() {
    ready_ = false;
    for (Unit& unit : units_) {
        if (!unit.au) continue;
        AudioUnitUninitialize(unit.au);
        AudioComponentInstanceDispose(unit.au);
        unit = Unit{};
    }
}

bool SpatialMixerBedRenderer::openUnit(Unit& unit, const double* azimuths, UInt32 channels, double sampleRate, std::string& error) {
    AudioComponentDescription description{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer, kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (!component) { error = "AUSpatialMixer is not available"; return false; }
    OSStatus s = AudioComponentInstanceNew(component, &unit.au);
    if (s != noErr) { unit.au = nullptr; error = failure("instance", s); return false; }
    unit.channels = channels;

    auto set = [&](AudioUnitPropertyID id, AudioUnitScope scope, const void* data, UInt32 size, const char* step) {
        s = AudioUnitSetProperty(unit.au, id, scope, 0, data, size);
        if (s != noErr) error = failure(step, s);
        return s == noErr;
    };
    const UInt32 one = 1;
    const auto input = floatFormat(sampleRate, channels), output = floatFormat(sampleRate, 2);
    const UInt32 algorithm = kSpatializationAlgorithm_UseOutputType;
    const UInt32 sourceMode = kSpatialMixerSourceMode_AmbienceBed;
    const UInt32 outputType = kSpatialMixerOutputType_Headphones;
    const UInt32 noReverb = 0, genericHrtf = kSpatialMixerPersonalizedHRTFMode_Off;
    const UInt32 maxFrames = kBlockFrames;

    std::vector<uint8_t> layout(offsetof(AudioChannelLayout, mChannelDescriptions) + channels * sizeof(AudioChannelDescription), 0);
    auto* channelLayout = reinterpret_cast<AudioChannelLayout*>(layout.data());
    channelLayout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
    channelLayout->mNumberChannelDescriptions = channels;
    for (UInt32 c = 0; c < channels; ++c) {
        AudioChannelDescription& d = channelLayout->mChannelDescriptions[c];
        d.mChannelLabel = kAudioChannelLabel_UseCoordinates;
        d.mChannelFlags = kAudioChannelFlags_SphericalCoordinates;
        d.mCoordinates[kAudioChannelCoordinates_Azimuth] = static_cast<Float32>(azimuths[c]);
        d.mCoordinates[kAudioChannelCoordinates_Elevation] = 0.0f;
        d.mCoordinates[kAudioChannelCoordinates_Distance] = 1.0f;
    }
    AudioChannelLayout stereo{};
    stereo.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
    AURenderCallbackStruct callback{&SpatialMixerBedRenderer::pull, this};

    const bool configured =
        set(kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, &one, sizeof one, "input bus count")
        && set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, &input, sizeof input, "input format")
        && set(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, &output, sizeof output, "output format")
        && set(kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, layout.data(), static_cast<UInt32>(layout.size()), "channel layout")
        && set(kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, &stereo, sizeof stereo, "stereo layout")
        && set(kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input, &algorithm, sizeof algorithm, "algorithm")
        && set(kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, &sourceMode, sizeof sourceMode, "source mode")
        && set(kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, &outputType, sizeof outputType, "output type")
        && set(kAudioUnitProperty_UsesInternalReverb, kAudioUnitScope_Global, &noReverb, sizeof noReverb, "internal reverb")
        && set(kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode, kAudioUnitScope_Global, &genericHrtf, sizeof genericHrtf, "HRTF mode")
        && set(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, &maxFrames, sizeof maxFrames, "slice size")
        && set(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, &callback, sizeof callback, "render callback");
    if (!configured) return false;
    if ((s = AudioUnitInitialize(unit.au)) != noErr) { error = failure("initialize", s); return false; }
    if ((s = AudioUnitSetParameter(unit.au, kSpatialMixerParam_ReverbBlend, kAudioUnitScope_Input, 0, 0.0f, 0)) != noErr) {
        error = failure("reverb blend", s);
        return false;
    }
    unit.yaw = 0.0;
    unit.sampleTime = 0.0;
    return true;
}

bool SpatialMixerBedRenderer::canRender(int layout) const {
    return ready_ && (layout == Upmixer::k51 || layout == Upmixer::k71);
}

SpatialMixerBedRenderer::Unit* SpatialMixerBedRenderer::unitFor(int layout) {
    if (layout == Upmixer::k51) return &units_[0];
    if (layout == Upmixer::k71) return &units_[1];
    return nullptr;
}

OSStatus SpatialMixerBedRenderer::pull(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                                       UInt32, UInt32 frames, AudioBufferList* io) {
    auto* self = static_cast<SpatialMixerBedRenderer*>(refCon);
    for (UInt32 c = 0; c < io->mNumberBuffers; ++c) {
        if (io->mBuffers[c].mData == nullptr) return kAudioUnitErr_InvalidParameter;
        std::memcpy(io->mBuffers[c].mData, self->pending_[c], frames * sizeof(float));
        io->mBuffers[c].mDataByteSize = frames * sizeof(float);
    }
    return noErr;
}

void SpatialMixerBedRenderer::renderUnit(Unit& unit, const float* const* channels, double headYawDegrees, float* left, float* right) {
    if (headYawDegrees != unit.yaw) {
        AudioUnitSetParameter(unit.au, kSpatialMixerParam_HeadYaw, kAudioUnitScope_Global, 0,
                              static_cast<AudioUnitParameterValue>(headYawDegrees), 0);
        unit.yaw = headYawDegrees;
    }
    pending_ = channels;
    struct { UInt32 count; AudioBuffer buffers[2]; } list;
    list.count = 2;
    list.buffers[0] = AudioBuffer{1, static_cast<UInt32>(kBlockFrames * sizeof(float)), left};
    list.buffers[1] = AudioBuffer{1, static_cast<UInt32>(kBlockFrames * sizeof(float)), right};
    AudioTimeStamp stamp{};
    stamp.mSampleTime = unit.sampleTime;
    stamp.mFlags = kAudioTimeStampSampleTimeValid;
    AudioUnitRenderActionFlags flags = 0;
    if (AudioUnitRender(unit.au, &flags, &stamp, 0, kBlockFrames, reinterpret_cast<AudioBufferList*>(&list)) != noErr) {
        std::fill(left, left + kBlockFrames, 0.0f);
        std::fill(right, right + kBlockFrames, 0.0f);
    }
    unit.sampleTime += kBlockFrames;
}

void SpatialMixerBedRenderer::render(int layout, const float* const* channels, double headYawDegrees, float* left, float* right) {
    Unit* unit = canRender(layout) ? unitFor(layout) : nullptr;
    if (!unit) {
        std::fill(left, left + kBlockFrames, 0.0f);
        std::fill(right, right + kBlockFrames, 0.0f);
        return;
    }
    if (layout != activeLayout_) {
        // The incoming unit last rendered whatever it heard before; clear it.
        for (std::size_t b = 0; b < flushBlocks_; ++b) renderUnit(*unit, silencePointers_, headYawDegrees, fadeL_.data(), fadeR_.data());
        if (unitFor(activeLayout_) != nullptr) {
            fadingLayout_ = activeLayout_;
            fadeBlocksLeft_ = fadeBlocks_;
        }
        activeLayout_ = layout;
    }
    renderUnit(*unit, channels, headYawDegrees, left, right);
    if (fadeBlocksLeft_ > 0) {
        renderUnit(*unitFor(fadingLayout_), channels, headYawDegrees, fadeL_.data(), fadeR_.data());
        const double total = static_cast<double>(fadeBlocks_ * kBlockFrames);
        const double done = static_cast<double>((fadeBlocks_ - fadeBlocksLeft_) * kBlockFrames);
        for (std::size_t i = 0; i < kBlockFrames; ++i) {
            const float incoming = static_cast<float>((done + static_cast<double>(i) + 1.0) / total);
            left[i] = left[i] * incoming + fadeL_[i] * (1.0f - incoming);
            right[i] = right[i] * incoming + fadeR_[i] * (1.0f - incoming);
        }
        --fadeBlocksLeft_;
    }
}

} // namespace roomcut
