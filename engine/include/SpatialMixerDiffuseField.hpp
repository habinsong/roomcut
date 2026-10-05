/*
 * SpatialMixerDiffuseField.hpp — AUSpatialMixer's own diffuse-field response,
 * measured on the system the engine runs on, so the bed renderer can take it
 * back out.
 *
 * It used to be a table measured once (2026-09-16, macOS 26). macOS 27 moved the
 * response by up to 4.4 dB (12.8 kHz) without changing the component's version
 * (0x00010608 on both): flat -4 dB below 200 Hz with a +1.5/-4.4 dB shape above
 * on 26, close to flat at -3 dB on 27. Divided out of 27's unit, 26's table made
 * the product chain's tone worse than no correction (2.77 against 2.20 dB RMS).
 * Nothing the unit reports tells the two apart, so the engine measures it.
 *
 * The measurement: one impulse from each of 494 directions (elevation -45..90 in
 * 15-degree rings, 5-degree azimuth steps shrinking with the ring's
 * circumference), headphones, UseOutputType, AmbienceBed, no internal reverb; the
 * power of both ears averaged in 1/6-octave bands and weighted by the area each
 * direction stands for, relative to the impulse played dry. 3.8 s of one core at
 * 48 kHz and 10 s at 96 kHz (Mac mini, macOS 27), so it runs on its own
 * low-priority thread once per macOS build, unit rate and HRTF, and the result is
 * kept next to the engine state.
 *
 * Which HRTF a unit renders with is the process's state, not the OS's: on macOS 27
 * a user process measured the generic set (HRIR_Movie.ir, flat within 0.5 dB) while
 * the engine daemon, once a unit had loaded the listener's personalized set,
 * measured that one (+3 dB at 6-10 kHz against 50 Hz). The renderer's own units
 * get the personalized set from Auto mode only while the default output is lent
 * to the headphones (SpatialMixerBedRenderer), which a seconds-long measurement
 * cannot do; so it asks for the set explicitly (On / Off), records which one its
 * units actually used, and the renderer takes only a response of its own set.
 */
#ifndef ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP
#define ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP

#include "dsp/ParametricFit.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace roomcut {

// Centres of the 1/6-octave bands the response is measured in.
inline constexpr double kDiffuseFieldBandsHz[] = {
       50.0,    56.1,    63.0,    70.7,    79.4,    89.1,   100.0,   112.2,
      126.0,   141.4,   158.7,   178.2,   200.0,   224.5,   252.0,   282.8,
      317.5,   356.4,   400.0,   449.0,   504.0,   565.7,   635.0,   712.7,
      800.0,   898.0,  1007.9,  1131.4,  1269.9,  1425.4,  1600.0,  1795.9,
     2015.9,  2262.7,  2539.8,  2850.9,  3200.0,  3591.9,  4031.7,  4525.5,
     5079.7,  5701.8,  6400.0,  7183.8,  8063.5,  9051.0, 10159.4, 11403.5,
    12800.0, 14367.5, 16127.0, 18101.9,
};

// Blocking, seconds of CPU. Never on the control or the render thread. Empty if
// the unit will not open or the units did not all use one HRTF set.
// `wantPersonalized` asks for the listener's personalized set (used if there is
// one); `personalized`, when given, says which set the units rendered with.
std::vector<ResponsePoint> measureSpatialMixerDiffuseField(double unitRate, bool wantPersonalized = false,
                                                           bool* personalized = nullptr);

// One measurement per macOS build, unit rate and HRTF set: read from `directory`
// when this build already measured it, otherwise measured in the background and
// saved there. Control thread only.
class SpatialMixerCalibration {
public:
    explicit SpatialMixerCalibration(std::string directory);

    // The response for `unitRate` with the generic or the personalized HRTF, or
    // empty while it is still being measured (the call starts that measurement).
    std::vector<ResponsePoint> response(double unitRate, bool personalized);

    struct Measured { double unitRate = 0.0; bool personalized = false; std::vector<ResponsePoint> response; };
    // A measurement that finished since the last call; an empty response is one
    // that failed.
    std::optional<Measured> takeFinished();

private:
    struct Shared;
    std::string path(double unitRate, bool personalized) const;

    std::string directory_;
    std::string build_;
    std::shared_ptr<Shared> shared_;
};

} // namespace roomcut

#endif // ROOMCUT_SPATIAL_MIXER_DIFFUSE_FIELD_HPP
