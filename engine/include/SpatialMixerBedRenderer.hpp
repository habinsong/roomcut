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
 *   - above 192 kHz the interaural delay shrinks (248 us -> 142 us at 768 kHz),
 *     so higher rates are refused and the stage keeps its built-in render
 *   - one render call costs about 2.5 us, which is why this renders blocks
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

namespace roomcut {

class SpatialMixerBedRenderer final : public BedRenderer {
public:
    static constexpr std::size_t kBlockFrames = 64;
    static constexpr double kMaxSampleRate = 192000.0;

    SpatialMixerBedRenderer() = default;
    SpatialMixerBedRenderer(const SpatialMixerBedRenderer&) = delete;
    SpatialMixerBedRenderer& operator=(const SpatialMixerBedRenderer&) = delete;
    ~SpatialMixerBedRenderer() override { release(); }

    // Control thread, with IO stopped. False, with `error` set, when the rate is
    // out of range or a unit will not open; canRender() is then false for every
    // layout and the stage renders the bed itself.
    bool prepare(double sampleRate, std::string& error);
    void release();

    std::size_t blockFrames() const override { return kBlockFrames; }
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
};

} // namespace roomcut

#endif // ROOMCUT_SPATIAL_MIXER_BED_RENDERER_HPP
