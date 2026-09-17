#include "SpatialMixerBedRenderer.hpp"
#include "SpatialMixerDiffuseField.hpp"

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
        error = "sample rate " + std::to_string(static_cast<long>(sampleRate)) + " Hz is above the 768 kHz the bed renderer takes";
        return false;
    }
    // The smallest whole fraction of the stream the units render correctly at.
    factor_ = static_cast<std::size_t>(std::ceil(sampleRate / kMaxUnitRate));
    unitRate_ = sampleRate / static_cast<double>(factor_);
    if (!openUnit(units_[0], k51Azimuths, 5, unitRate_, error) || !openUnit(units_[1], k71Azimuths, 7, unitRate_, error)) {
        release();
        return false;
    }
    personalizedHrtf_ = false;
    for (const Unit& unit : units_) {
        UInt32 inUse = 0, size = sizeof inUse;
        if (AudioUnitGetProperty(unit.au, kAudioUnitProperty_SpatialMixerAnyInputIsUsingPersonalizedHRTF,
                                 kAudioUnitScope_Global, 0, &inUse, &size) == noErr && inUse != 0)
            personalizedHrtf_ = true;
    }
    {
        ParametricFitSettings fit;
        fit.sampleRate = unitRate_;
        fit.minHz = 50.0;
        fit.maxHz = std::min(16000.0, 0.45 * unitRate_);
        const std::vector<ResponsePoint> measured(std::begin(kSpatialMixerDiffuseField48k), std::end(kSpatialMixerDiffuseField48k));
        const ParametricFitResult result = fitParametricCorrection(measured, fit);
        diffuseBands_ = result.bands;
        diffuseBandCount_ = result.bandsUsed;
        for (std::size_t b = 0; b < diffuseFilters_.size(); ++b) {
            const ParametricBand& band = diffuseBands_[b];
            if (b < diffuseBandCount_) diffuseFilters_[b].set(static_cast<BiquadType>(band.type), unitRate_, band.freqHz, band.gainDb, band.q);
            else diffuseFilters_[b].setIdentity();
            diffuseFilters_[b].reset();
        }
        diffuseCorrection_ = true;
    }
    backDelayL_ = std::min(kBackLine - 1, static_cast<std::size_t>(std::lround(unitRate_ * kBackDelayLeftSeconds)));
    backDelayR_ = std::min(kBackLine - 1, static_cast<std::size_t>(std::lround(unitRate_ * kBackDelayRightSeconds)));
    backLineL_.fill(0.0f);
    backLineR_.fill(0.0f);
    backWrite_ = 0;
    down_.prepare(sampleRate, factor_, 7);
    up_.prepare(sampleRate, factor_, 2);
    for (std::size_t c = 0; c < unitIn_.size(); ++c) unitInRead_[c] = unitInWrite_[c] = unitIn_[c].data();
    for (std::size_t c = 0; c < silence_.size(); ++c) silencePointers_[c] = silence_[c].data();
    flushBlocks_ = static_cast<std::size_t>(std::ceil(kFlushSeconds * unitRate_ / kBlockFrames));
    fadeBlocks_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(kFadeSeconds * unitRate_ / kBlockFrames)));
    fadeBlocksLeft_ = 0;
    activeLayout_ = fadingLayout_ = Upmixer::kOff;
    ready_ = true;
    return true;
}

void SpatialMixerBedRenderer::release() {
    ready_ = false;
    factor_ = 1;
    unitRate_ = 0.0;
    personalizedHrtf_ = false;
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
    const UInt32 noReverb = 0, hrtfMode = kSpatialMixerPersonalizedHRTFMode_Auto;
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
        && set(kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode, kAudioUnitScope_Global, &hrtfMode, sizeof hrtfMode, "HRTF mode")
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

void SpatialMixerBedRenderer::equalise(float* left, float* right) {
    if (!diffuseCorrection_) return;
    for (std::size_t b = 0; b < diffuseBandCount_; ++b) {
        Biquad& filter = diffuseFilters_[b];
        for (std::size_t i = 0; i < kBlockFrames; ++i) {
            left[i] = filter.processSample(left[i], 0);
            right[i] = filter.processSample(right[i], 1);
        }
    }
}

void SpatialMixerBedRenderer::render(int layout, const float* const* channels, double headYawDegrees, float* left, float* right) {
    if (factor_ == 1) {
        renderAtUnitRate(layout, channels, headYawDegrees, left, right);
        equalise(left, right);
        return;
    }
    down_.process(channels, unitInWrite_, kBlockFrames);
    renderAtUnitRate(layout, unitInRead_, headYawDegrees, unitLeft_.data(), unitRight_.data());
    equalise(unitLeft_.data(), unitRight_.data());
    const float* unitOut[2] = {unitLeft_.data(), unitRight_.data()};
    float* streamOut[2] = {left, right};
    up_.process(unitOut, streamOut, kBlockFrames);
}

void SpatialMixerBedRenderer::renderAtUnitRate(int layout, const float* const* incoming, double headYawDegrees,
                                               float* left, float* right) {
    // The back pair arrives later than the rest (see kBackDelayLeftSeconds).
    // Every block goes through the lines, whatever the layout, so a switch to
    // 7.1 starts from the programme rather than from a stale tail.
    for (std::size_t i = 0; i < kBlockFrames; ++i) {
        backLineL_[backWrite_] = incoming[5][i];
        backLineR_[backWrite_] = incoming[6][i];
        backOutL_[i] = backLineL_[backWrite_ >= backDelayL_ ? backWrite_ - backDelayL_ : backWrite_ + kBackLine - backDelayL_];
        backOutR_[i] = backLineR_[backWrite_ >= backDelayR_ ? backWrite_ - backDelayR_ : backWrite_ + kBackLine - backDelayR_];
        backWrite_ = backWrite_ + 1 < kBackLine ? backWrite_ + 1 : 0;
    }
    const float* channels[7] = {incoming[0], incoming[1], incoming[2], incoming[3], incoming[4], backOutL_.data(), backOutR_.data()};
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
