/*
 * Biquad.hpp — second-order IIR filter, the building block for all Roomcut EQ
 * and filtering (docs/04-dsp.md).
 *
 * - RBJ "Audio EQ Cookbook" coefficients.
 * - Direct Form II transposed (better float numerical behavior than DF1/DF2).
 * - Per-channel state; coefficients recomputed only on setX(), never per-sample.
 * - Real-time safe: process() does no allocation, no locks, no logging.
 *
 * Header-first (RoomcutCore is header-only per CMakeLists) so it unit-tests off
 * the audio thread.
 */
#ifndef ROOMCUT_BIQUAD_HPP
#define ROOMCUT_BIQUAD_HPP

#include <array>
#include <cmath>
#include <cstddef>

namespace roomcut {

enum class BiquadType {
    Bell,       // peaking
    LowShelf,
    HighShelf,
    HighPass,
    LowPass,
    Notch
};

// Up to this many channels of independent filter state. MVP is stereo (2).
inline constexpr std::size_t kBiquadMaxChannels = 8;

class Biquad {
public:
    Biquad() { setIdentity(); }

    // Configure as a filter of `type` at `freq` Hz with `gainDb` (used by
    // bell/shelf; ignored by pass/notch) and quality `q`, for sample rate `fs`.
    // Recomputes coefficients; leaves per-channel state intact (call reset() to
    // clear history if you need a clean start).
    void set(BiquadType type, double fs, double freq, double gainDb, double q) {
        // Guard against degenerate inputs that would blow up the math.
        if (fs <= 0.0 || freq <= 0.0 || q <= 0.0) { setIdentity(); return; }
        // Nyquist clamp: keep the normalized frequency strictly inside (0, π).
        double w0 = 2.0 * M_PI * (freq / fs);
        if (w0 >= M_PI) w0 = M_PI * 0.999;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * q);
        const double A = std::pow(10.0, gainDb / 40.0); // amplitude for shelves/bell

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type) {
            case BiquadType::Bell: {
                b0 = 1 + alpha * A;
                b1 = -2 * cosw0;
                b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;
                a1 = -2 * cosw0;
                a2 = 1 - alpha / A;
                break;
            }
            case BiquadType::LowShelf: {
                const double sqrtA = std::sqrt(A);
                const double twoSqrtAalpha = 2.0 * sqrtA * alpha;
                b0 =      A * ((A + 1) - (A - 1) * cosw0 + twoSqrtAalpha);
                b1 =  2 * A * ((A - 1) - (A + 1) * cosw0);
                b2 =      A * ((A + 1) - (A - 1) * cosw0 - twoSqrtAalpha);
                a0 =          (A + 1) + (A - 1) * cosw0 + twoSqrtAalpha;
                a1 = -2 *     ((A - 1) + (A + 1) * cosw0);
                a2 =          (A + 1) + (A - 1) * cosw0 - twoSqrtAalpha;
                break;
            }
            case BiquadType::HighShelf: {
                const double sqrtA = std::sqrt(A);
                const double twoSqrtAalpha = 2.0 * sqrtA * alpha;
                b0 =      A * ((A + 1) + (A - 1) * cosw0 + twoSqrtAalpha);
                b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
                b2 =      A * ((A + 1) + (A - 1) * cosw0 - twoSqrtAalpha);
                a0 =          (A + 1) - (A - 1) * cosw0 + twoSqrtAalpha;
                a1 =  2 *     ((A - 1) - (A + 1) * cosw0);
                a2 =          (A + 1) - (A - 1) * cosw0 - twoSqrtAalpha;
                break;
            }
            case BiquadType::HighPass: {
                b0 =  (1 + cosw0) / 2;
                b1 = -(1 + cosw0);
                b2 =  (1 + cosw0) / 2;
                a0 =   1 + alpha;
                a1 =  -2 * cosw0;
                a2 =   1 - alpha;
                break;
            }
            case BiquadType::LowPass: {
                b0 = (1 - cosw0) / 2;
                b1 =  1 - cosw0;
                b2 = (1 - cosw0) / 2;
                a0 =  1 + alpha;
                a1 = -2 * cosw0;
                a2 =  1 - alpha;
                break;
            }
            case BiquadType::Notch: {
                b0 =  1;
                b1 = -2 * cosw0;
                b2 =  1;
                a0 =  1 + alpha;
                a1 = -2 * cosw0;
                a2 =  1 - alpha;
                break;
            }
        }

        // Normalize by a0 and store.
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0;
        a1_ = a1 / a0; a2_ = a2 / a0;
    }

    // Unity (pass-through) filter.
    void setIdentity() {
        b0_ = 1.0; b1_ = 0.0; b2_ = 0.0; a1_ = 0.0; a2_ = 0.0;
    }

    // Clear per-channel history (e.g. on format change / preset crossfade start).
    void reset() { state_.fill({0.0, 0.0}); }

    // Process one sample for `channel`. Direct Form II transposed:
    //   y    = b0*x + s1
    //   s1   = b1*x - a1*y + s2
    //   s2   = b2*x - a2*y
    inline float processSample(float x, std::size_t channel) {
        double s1 = state_[channel][0];
        double s2 = state_[channel][1];
        double y = b0_ * x + s1;
        state_[channel][0] = b1_ * x - a1_ * y + s2;
        state_[channel][1] = b2_ * x - a2_ * y;
        return static_cast<float>(y);
    }

    // Complex magnitude response |H(e^{jw})| at normalized frequency `freqHz`
    // for sample rate `fs`. For tests/analysis — not on the audio path.
    double magnitudeAt(double freqHz, double fs) const {
        const double w = 2.0 * M_PI * (freqHz / fs);
        const double cw = std::cos(w), sw = std::sin(w);
        const double cw2 = std::cos(2 * w), sw2 = std::sin(2 * w);
        // Numerator b0 + b1 e^-jw + b2 e^-2jw
        const double numRe = b0_ + b1_ * cw + b2_ * cw2;
        const double numIm = -(b1_ * sw + b2_ * sw2);
        // Denominator 1 + a1 e^-jw + a2 e^-2jw
        const double denRe = 1.0 + a1_ * cw + a2_ * cw2;
        const double denIm = -(a1_ * sw + a2_ * sw2);
        const double num = std::sqrt(numRe * numRe + numIm * numIm);
        const double den = std::sqrt(denRe * denRe + denIm * denIm);
        return den > 0.0 ? num / den : 0.0;
    }

    double magnitudeDbAt(double freqHz, double fs) const {
        const double m = magnitudeAt(freqHz, fs);
        return 20.0 * std::log10(m > 1e-12 ? m : 1e-12);
    }

private:
    // Normalized coefficients (a0 == 1).
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    std::array<std::array<double, 2>, kBiquadMaxChannels> state_{};
};

} // namespace roomcut

#endif // ROOMCUT_BIQUAD_HPP
