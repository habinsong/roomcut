/*
 * SpatialMixerBedRenderer.hpp — the headphone bed rendered by AUSpatialMixer.
 *
 * Apple's renderer places each upmixed channel at its ITU angle with measured
 * HRTFs and turns all of them with the head. Measured before this was written
 * (P0 M1, user process, 48 / 192 kHz):
 *   - channels placed by spherical coordinates land where asked; CoreAudio's
 *     own surround labels do not (LeftSurroundDirect sits near 110 degrees)
 *   - HeadYaw +40 equals placing every channel 40 degrees to the left
 *   - render and repeated HeadYaw sets: no allocation, no locks
 *   - AudioUnitReset allocated twice in 1000 calls, so it is never called here
 *   - the unit's interaural delay stops growing at roughly 80-90 samples, so
 *     above 96 kHz lateral channels lose ITD: at 90 degrees 723 us up to
 *     110.25 kHz, 652 us at 128 kHz, 483 us at 192 kHz, and the left speaker
 *     itself shrinks past 352.8 kHz. Above 96 kHz the units therefore run at
 *     88.2 / 96 kHz (a half, quarter or eighth of the stream) between an
 *     IntegerDecimator and an IntegerInterpolator, which keeps the bed flat to
 *     24 kHz and adds the filters' delay, reported as latencyFrames()
 *   - one render call costs about 2.5 us, which is why this renders blocks
 *   - its diffuse-field response is not flat (SpatialMixerDiffuseField.hpp), and
 *     it showed: the chain's tone moved 2.2 dB RMS from the dry programme. Both
 *     ears go through the same fitted parametric bands that undo it, so only
 *     the part every direction shares is removed and the direction cues stay
 *
 * One unit per layout (5.1 with surrounds at +-110, 7.1 at +-90 / +-135), both
 * opened on the control thread. A layout change clears the incoming unit's
 * history with silence (its impulse response is over within 5 ms) and
 * crossfades the two units over 20 ms.
 */
#ifndef ROOMCUT_SPATIAL_MIXER_BED_RENDERER_HPP
#define ROOMCUT_SPATIAL_MIXER_BED_RENDERER_HPP

#include <AudioToolbox/AudioToolbox.h>

#include <array>
#include <cstddef>
#include <string>

#include "dsp/BedRenderer.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/IntegerResampler.hpp"
#include "dsp/ParametricEQ.hpp"

namespace roomcut {

class SpatialMixerBedRenderer final : public BedRenderer {
public:
    // One unit render, at the unit's rate.
    static constexpr std::size_t kBlockFrames = 64;
    // The highest rate the units render correctly, and the highest this takes.
    static constexpr double kMaxUnitRate = 96000.0;
    static constexpr double kMaxSampleRate = 768000.0;

    SpatialMixerBedRenderer() = default;
    SpatialMixerBedRenderer(const SpatialMixerBedRenderer&) = delete;
    SpatialMixerBedRenderer& operator=(const SpatialMixerBedRenderer&) = delete;
    ~SpatialMixerBedRenderer() override { release(); }

    // Control thread, with IO stopped. False, with `error` set, when the rate is
    // out of range or a unit will not open; canRender() is then false for every
    // layout and the stage renders the bed itself.
    bool prepare(double sampleRate, std::string& error);
    void release();
    double unitRate() const { return unitRate_; }
    // What the units reported right after opening (property 3116): whether
    // either one renders with the listener's personalized HRTF. Asked for with
    // the Auto mode; the measured answer in a signed-less user process is no.
    bool personalizedHrtfInUse() const { return personalizedHrtf_; }
    // The bands that undo the unit's diffuse-field response, fitted at unitRate().
    const std::array<ParametricBand, ParametricEQ::kNumBands>& diffuseFieldBands() const { return diffuseBands_; }
    std::size_t diffuseFieldBandCount() const { return diffuseBandCount_; }
    // Off only to measure what the correction does; on after every prepare().
    void setDiffuseFieldCorrection(bool on) { diffuseCorrection_ = on; }

    std::size_t blockFrames() const override { return kBlockFrames * factor_; }
    std::size_t latencyFrames() const override { return factor_ > 1 ? down_.delayFrames() + up_.delayFrames() : 0; }
    bool canRender(int layout) const override;
    void render(int layout, const float* const* channels, double headYawDegrees,
                float* left, float* right) override;

private:
    struct Unit {
        AudioUnit au = nullptr;
        UInt32 channels = 0;
        double yaw = 0.0;
        Float64 sampleTime = 0.0;   // advances on every render, flushes included
    };

    static OSStatus pull(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*,
                         UInt32, UInt32 frames, AudioBufferList* io);
    bool openUnit(Unit& unit, const double* azimuths, UInt32 channels, double sampleRate, std::string& error);
    void renderUnit(Unit& unit, const float* const* channels, double headYawDegrees, float* left, float* right);
    void renderAtUnitRate(int layout, const float* const* channels, double headYawDegrees, float* left, float* right);
    void equalise(float* left, float* right);
    Unit* unitFor(int layout);

    std::array<Unit, 2> units_{};         // [0] 5.1, [1] 7.1
    bool ready_ = false;
    int activeLayout_ = 0;
    int fadingLayout_ = 0;
    std::size_t fadeBlocks_ = 1, fadeBlocksLeft_ = 0, flushBlocks_ = 1;
    const float* const* pending_ = nullptr;
    std::array<float, kBlockFrames> fadeL_{}, fadeR_{};
    std::array<std::array<float, kBlockFrames>, 7> silence_{};
    const float* silencePointers_[7] = {};

    // Above kMaxUnitRate only: the stream taken down to the units and back.
    std::size_t factor_ = 1;
    double unitRate_ = 0.0;
    bool personalizedHrtf_ = false;
    IntegerDecimator down_;
    IntegerInterpolator up_;
    std::array<std::array<float, kBlockFrames>, 7> unitIn_{};
    float* unitInWrite_[7] = {};
    const float* unitInRead_[7] = {};
    std::array<float, kBlockFrames> unitLeft_{}, unitRight_{};

    std::array<ParametricBand, ParametricEQ::kNumBands> diffuseBands_{};
    std::array<Biquad, ParametricEQ::kNumBands> diffuseFilters_{};
    std::size_t diffuseBandCount_ = 0;
    bool diffuseCorrection_ = true;
};

} // namespace roomcut

#endif // ROOMCUT_SPATIAL_MIXER_BED_RENDERER_HPP
