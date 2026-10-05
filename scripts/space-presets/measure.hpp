// measure.hpp — measurements shared by tune.cpp: interaural cross-correlation,
// K-weighted level, the mono fold and two loudspeakers heard at the ears through
// AUSpatialMixer's HRTF. Measurement tooling only; nothing here ships.
#pragma once
#include "BinauralMeasure.hpp"
#include "DSPChain.hpp"
#include "KWeightedLevel.hpp"
#include "SpatialMixerBedRenderer.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace roomcut;
static const double kFs = 48000;
struct Stereo { std::vector<float> l, r; };

static double iacc(const std::vector<double>& l, const std::vector<double>& r) {
    const int lag = (int)(kFs * 0.001);
    double el = 0, er = 0; for (size_t i = 0; i < l.size(); ++i) { el += l[i] * l[i]; er += r[i] * r[i]; }
    double best = 0;
    for (int k = -lag; k <= lag; ++k) {
        double s = 0;
        for (size_t i = (size_t)std::max(0, -k); i + std::max(0, k) < l.size(); i += 3) s += l[i] * r[i + k];
        best = std::max(best, std::abs(s * 3 / std::sqrt(el * er)));
    }
    return best;
}

static double loudness(const std::vector<float>& l, const std::vector<float>& r, double skip) {
    KWeightedLevel k; k.prepare(kFs, 2, 30);
    double p = 0; int n = 0;
    for (size_t i = 0; i < l.size(); ++i) { const float f[2] = {l[i], r[i]}; if (k.processFrame(f) && i > skip * kFs) { p += k.power(); ++n; } }
    return 10 * std::log10(p / n);
}

static Stereo mono(const Stereo& x) { Stereo m{x.l, x.r}; for (size_t i = 0; i < x.l.size(); ++i) m.l[i] = m.r[i] = 0.5f * (x.l[i] + x.r[i]); return m; }

// Ear signals of two loudspeakers at -+deg, through AUSpatialMixer's HRTF (anechoic).
static Stereo speakersToEars(const Stereo& x, double deg) {
    AudioComponentDescription desc{kAudioUnitType_Mixer, kAudioUnitSubType_SpatialMixer, kAudioUnitManufacturer_Apple, 0, 0};
    AudioUnit au = nullptr; AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &desc), &au);
    AudioStreamBasicDescription in{kFs, kAudioFormatLinearPCM, kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved, 4, 1, 4, 2, 32, 0};
    AudioStreamBasicDescription out = in;
    UInt32 one = 1; AudioUnitSetProperty(au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &one, sizeof one);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &in, sizeof in);
    AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &out, sizeof out);
    std::vector<uint8_t> bytes(offsetof(AudioChannelLayout, mChannelDescriptions) + 2 * sizeof(AudioChannelDescription), 0);
    auto* layout = reinterpret_cast<AudioChannelLayout*>(bytes.data());
    layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions; layout->mNumberChannelDescriptions = 2;
    for (int c = 0; c < 2; ++c) { auto& d = layout->mChannelDescriptions[c]; d.mChannelLabel = kAudioChannelLabel_UseCoordinates; d.mChannelFlags = kAudioChannelFlags_SphericalCoordinates; d.mCoordinates[kAudioChannelCoordinates_Azimuth] = (Float32)(c ? deg : -deg); d.mCoordinates[kAudioChannelCoordinates_Distance] = 1; }
    AudioUnitSetProperty(au, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Input, 0, layout, (UInt32)bytes.size());
    UInt32 v = kSpatializationAlgorithm_UseOutputType; AudioUnitSetProperty(au, kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input, 0, &v, sizeof v);
    v = kSpatialMixerSourceMode_AmbienceBed; AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, 0, &v, sizeof v);
    v = kSpatialMixerOutputType_Headphones; AudioUnitSetProperty(au, kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, 0, &v, sizeof v);
    v = 0; AudioUnitSetProperty(au, kAudioUnitProperty_UsesInternalReverb, kAudioUnitScope_Global, 0, &v, sizeof v);
    struct Src { const Stereo* x; size_t at; } src{&x, 0};
    AURenderCallbackStruct cb{[](void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames, AudioBufferList* io) -> OSStatus {
        auto* s = static_cast<Src*>(ctx);
        for (UInt32 i = 0; i < frames; ++i) { const bool ok = s->at + i < s->x->l.size();
            static_cast<float*>(io->mBuffers[0].mData)[i] = ok ? s->x->l[s->at + i] : 0; static_cast<float*>(io->mBuffers[1].mData)[i] = ok ? s->x->r[s->at + i] : 0; }
        s->at += frames; return noErr; }, &src};
    AudioUnitSetProperty(au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    UInt32 maxFrames = 512; AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, sizeof maxFrames);
    AudioUnitInitialize(au);
    Stereo ears{std::vector<float>(x.l.size()), std::vector<float>(x.l.size())};
    AudioBufferList* abl = (AudioBufferList*)calloc(1, sizeof(AudioBufferList) + sizeof(AudioBuffer)); abl->mNumberBuffers = 2;
    AudioTimeStamp ts{}; ts.mFlags = kAudioTimeStampSampleTimeValid;
    for (size_t i = 0; i < x.l.size(); i += 512) {
        const UInt32 k = (UInt32)std::min<size_t>(512, x.l.size() - i);
        abl->mBuffers[0] = {1, k * 4, ears.l.data() + i}; abl->mBuffers[1] = {1, k * 4, ears.r.data() + i};
        AudioUnitRenderActionFlags flags = 0; AudioUnitRender(au, &flags, &ts, 0, k, abl); ts.mSampleTime += k;
    }
    AudioUnitUninitialize(au); AudioComponentInstanceDispose(au); free(abl);
    return ears;
}
