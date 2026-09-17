/*
 * SideCanceller.hpp — crosstalk cancellation on the difference signal only, so
 * a pair of loudspeakers can throw the side of the image past the boxes without
 * touching the middle.
 *
 * A loudspeaker pair leaks each speaker into the far ear a fraction of a
 * millisecond later and with the head's shadow taken off. Cancelling that leak
 * is what makes sound appear outside the speakers (recursive cancellation,
 * Glasgal's RACE and Bauck & Cooper, JAES 44(9), 1996). Written out for the
 * sum and difference of the two outputs, the recursion splits in two: the sum
 * is fed back with a minus sign, which combs whatever sits in the centre, and
 * the difference with a plus sign, which is the widening. Only the second half
 * runs here:
 *
 *     side_out[n] = side[n] + g * B(side_out[n - d])
 *
 * with B a band-pass over the range where the leak matters (250 Hz - 8 kHz) and
 * d the extra path to the far ear. The middle is passed through untouched, so
 * a centred voice is not coloured and a mono fold-down (a single built-in
 * speaker) hears exactly the programme's middle.
 *
 * The listener's speaker positions are unknown, and a canceller only cancels
 * for the geometry it assumes. Chosen by measurement (2026-09-17): 48 settings
 * of d, g and the upper band edge, rendered through AUSpatialMixer's HRTF for
 * speakers at +-15 and +-30 degrees, on pop, ballad, electronic and orchestral
 * programme. Past g = 0.6 the ears heard 4-7 dB of third-octave colouration;
 * at 0.45, 90 us and 8 kHz the worst was 2.6 / 2.8 dB, the interaural
 * correlation fell by up to 0.12 for the close pair, and the mono sum moved at
 * most 0.76 dB. It is a moderate widening on purpose; no setting widened both
 * geometries without colouring one of them.
 *
 * Real-time safe: fixed-size state, no allocation.
 */
#ifndef ROOMCUT_SIDE_CANCELLER_HPP
#define ROOMCUT_SIDE_CANCELLER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

class SideCanceller {
public:
    static constexpr double kDelaySeconds = 90.0e-6;
    static constexpr double kHighpassHz = 250.0;
    static constexpr double kLowpassHz = 8000.0;
    // 768 kHz * 90 us = 69 samples, with room to spare.
    static constexpr std::size_t kLine = 128;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        delay_ = std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(fs_ * kDelaySeconds)), 1, kLine - 1);
        hpA_ = 1.0 / (1.0 + 2.0 * kPi * kHighpassHz / fs_);
        lpA_ = 1.0 - std::exp(-2.0 * kPi * kLowpassHz / fs_);
        reset();
    }

    // 0 = off (the difference passes straight through); must stay below 1.
    void setGain(double gain) { gain_ = std::clamp(gain, 0.0, 0.95); }
    double gain() const { return gain_; }

    void reset() {
        std::fill(line_, line_ + kLine, 0.0);
        write_ = 0;
        hpX_ = hpY_ = lp_ = 0.0;
    }

    inline void process(double& left, double& right) {
        const double mid = 0.5 * (left + right);
        const double side = 0.5 * (left - right);
        const std::size_t read = write_ >= delay_ ? write_ - delay_ : write_ + kLine - delay_;
        const double delayed = line_[read];
        const double hp = hpA_ * (hpY_ + delayed - hpX_);
        hpX_ = delayed;
        hpY_ = hp;
        lp_ += lpA_ * (hp - lp_);
        const double out = side + gain_ * lp_;
        line_[write_] = std::fabs(out) < 1.0e-25 ? 0.0 : out;
        write_ = write_ + 1 < kLine ? write_ + 1 : 0;
        left = mid + out;
        right = mid - out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double fs_ = 48000.0;
    double gain_ = 0.0;
    std::size_t delay_ = 6, write_ = 0;
    double hpA_ = 0.97, lpA_ = 0.48;
    double hpX_ = 0.0, hpY_ = 0.0, lp_ = 0.0;
    double line_[kLine] = {0.0};
};

} // namespace roomcut

#endif // ROOMCUT_SIDE_CANCELLER_HPP
