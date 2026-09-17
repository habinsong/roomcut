/*
 * ChannelProbe.hpp — a pink-noise burst on one channel of the upmix, for the
 * listening test that asks where each channel is heard (PRD P0-4).
 *
 * SurroundStage puts the burst in place of the programme's upmix on the
 * headphone bed, so it reaches the renderer (built-in or external) alone and at
 * the level asked for, with head tracking and the room still applied.
 *
 * The noise: xorshift32 white noise (Marsaglia 2003), pinked by six one-pole
 * lowpasses 1.5 octaves apart from 40 Hz, each scaled by 1/sqrt(a) so every
 * pole adds about the same power per octave. Its RMS is known in closed form —
 * for one-poles y_j driven by the same white noise of variance s^2,
 * E[y_j y_k] = a_j a_k s^2 / (1 - (1 - a_j)(1 - a_k)) — so the level needs no
 * calibration run. 10 ms raised-cosine ramps at both ends.
 *
 * Deterministic, real-time safe: fixed state, no allocation.
 */
#ifndef ROOMCUT_CHANNEL_PROBE_HPP
#define ROOMCUT_CHANNEL_PROBE_HPP

#include <array>
#include <cmath>
#include <cstdint>

#include "Upmixer.hpp"

namespace roomcut {

class ChannelProbe {
public:
    static constexpr int kChannels = 7;   // UpmixFrame order: C, L, R, Ls, Rs, Lb, Rb
    static constexpr double kMaxSeconds = 10.0;
    static constexpr double kRampSeconds = 0.010;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        for (int k = 0; k < kPoles; ++k) a_[k] = 1.0 - std::exp(-2.0 * kPi * 40.0 * std::pow(2.8, k) / fs_);
        double variance = 0.0;
        for (int j = 0; j < kPoles; ++j)
            for (int k = 0; k < kPoles; ++k)
                variance += std::sqrt(a_[j] * a_[k]) / (1.0 - (1.0 - a_[j]) * (1.0 - a_[k]));
        rms_ = std::sqrt(variance / 3.0);   // white noise uniform in [-1, 1): variance 1/3
        stop();
    }

    // A channel outside 0..6, or a length that is not positive, stops the burst.
    void start(int channel, double seconds, double levelDb) {
        if (channel < 0 || channel >= kChannels || !(seconds > 0.0) || !std::isfinite(levelDb)) {
            stop();
            return;
        }
        channel_ = channel;
        total_ = static_cast<std::uint64_t>(std::llround(std::fmin(seconds, kMaxSeconds) * fs_));
        remaining_ = total_;
        ramp_ = std::fmax(1.0, kRampSeconds * fs_);
        gain_ = std::pow(10.0, levelDb / 20.0) / rms_;
        state_ = 0x9E3779B9u;
        pole_.fill(0.0);
    }

    void stop() { remaining_ = 0; }
    bool active() const { return remaining_ > 0; }
    int channel() const { return channel_; }

    // The next frame of the burst; call once per frame while active().
    UpmixFrame next() {
        UpmixFrame out;
        if (remaining_ == 0) return out;
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        const double white = static_cast<double>(state_) / 2147483648.0 - 1.0;
        double pink = 0.0;
        for (int k = 0; k < kPoles; ++k) {
            pole_[k] += a_[k] * (white - pole_[k]);
            pink += pole_[k] / std::sqrt(a_[k]);
        }
        const double elapsed = static_cast<double>(total_ - remaining_);
        const double edge = std::fmin(elapsed, static_cast<double>(remaining_ - 1));
        const double envelope = edge < ramp_ ? 0.5 - 0.5 * std::cos(kPi * edge / ramp_) : 1.0;
        --remaining_;
        const double v = pink * gain_ * envelope;
        switch (channel_) {
        case 0: out.centre = v; break;
        case 1: out.frontL = v; break;
        case 2: out.frontR = v; break;
        case 3: out.sideL = v; break;
        case 4: out.sideR = v; break;
        case 5: out.backL = v; break;
        default: out.backR = v; break;
        }
        return out;
    }

private:
    static constexpr int kPoles = 6;
    static constexpr double kPi = 3.14159265358979323846;
    double fs_ = 48000.0;
    std::array<double, kPoles> a_{};
    std::array<double, kPoles> pole_{};
    double rms_ = 1.0, gain_ = 0.0, ramp_ = 1.0;
    std::uint64_t total_ = 0, remaining_ = 0;
    std::uint32_t state_ = 0x9E3779B9u;
    int channel_ = 0;
};

} // namespace roomcut

#endif // ROOMCUT_CHANNEL_PROBE_HPP
