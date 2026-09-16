/*
 * VirtualSpeaker.hpp — renders one mono source as a loudspeaker standing at a
 * given azimuth around the listener, for headphone playback.
 *
 * This is the piece the head-tracked and multichannel work both need: once a
 * speaker can be placed at an arbitrary angle, head rotation is just "move the
 * angle the other way", and a 5.1 upmix is just several of these at the angles
 * ITU-R BS.775 specifies.
 *
 * The model is Brown & Duda's structural HRTF (IEEE TSAP 6(5), 1998), which
 * splits the azimuth cue into the two parts a sphere actually produces:
 *
 *   - Interaural time difference: Woodworth & Schlosberg's frequency-independent
 *     formula for a sphere of radius a. The far ear simply hears the sound later.
 *   - Head shadow: a one-pole/one-zero filter whose zero moves with the angle of
 *     incidence at that ear, so the far ear also loses its top end.
 *
 * Both parts are driven by the head radius, which is why this class exposes it:
 * personalising a listener's ITD later is a matter of changing one number
 * (Algazi, Avendaño & Duda, JAES 49(6), 2001 put the adult mean near 8.7 cm).
 *
 * No measured HRTF data and no third-party code — just the published models.
 * Real-time safe after prepare(): no allocation, no locks, no look-ahead.
 */
#ifndef ROOMCUT_VIRTUAL_SPEAKER_HPP
#define ROOMCUT_VIRTUAL_SPEAKER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

class VirtualSpeaker {
public:
    static constexpr double kDefaultHeadRadiusCm = 8.75;
    static constexpr double kMinHeadRadiusCm = 7.0;
    static constexpr double kMaxHeadRadiusCm = 10.5;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        // Azimuth moves along a ~12 ms one-pole. Head rotation is slow compared
        // with audio, so this only exists to keep a jumped angle from stepping
        // the delay line and clicking.
        azSmoothA_ = 1.0 - std::exp(-1.0 / (fs_ * 0.012));
        setHeadRadiusCm(radiusCm_);
        reset();
    }

    // Where the speaker stands, in degrees: 0 straight ahead, + to the right.
    void setAzimuth(double degrees) {
        azTarget_ = clamp(degrees, -180.0, 180.0);
    }

    // The listener's head size. Bigger head = longer ITD and a lower shadow
    // corner; this is the hook the personalisation step will drive.
    void setHeadRadiusCm(double cm) {
        radiusCm_ = clamp(cm, kMinHeadRadiusCm, kMaxHeadRadiusCm);
        radius_ = radiusCm_ * 0.01;
        // Brown & Duda: the shadow filter's pole sits at w0 = c/a.
        w0_ = kSpeedOfSound / radius_;
        coefAz_ = 1.0e9;          // force a coefficient refresh
    }

    double headRadiusCm() const { return radiusCm_; }
    double azimuth() const { return azSmooth_; }

    // Longest delay the model can ask for, in samples — the buffer bound.
    static constexpr double kMaxItdSeconds = 0.0011;   // 10.5 cm head, worst case

    void reset() {
        for (std::size_t i = 0; i < kMaxDelay; ++i) line_[i] = 0.0;
        write_ = 0;
        shadowXL_ = shadowYL_ = shadowXR_ = shadowYR_ = 0.0;
        azSmooth_ = azTarget_;
        coefAz_ = 1.0e9;
        updateCoefficients();
    }

    // One mono sample in, one binaural pair out.
    inline void process(double in, double& outL, double& outR) {
        if (azSmooth_ != azTarget_) {
            // Move along the SHORT way round. Azimuth is a circle, and -180 is
            // one degree from +179, not three hundred and fifty nine. Smoothing
            // the raw difference sent a speaker that crossed the rear centre
            // line sweeping all the way through the front instead — measured at
            // 40 ms to travel, dragging the delay line across its whole range on
            // the way. On 7.1 the back pair does this every time the listener
            // turns past 45 degrees, which is what it sounded like: a dropout
            // and a smear rather than a head turn.
            double delta = azTarget_ - azSmooth_;
            if (delta > 180.0) delta -= 360.0;
            else if (delta < -180.0) delta += 360.0;
            azSmooth_ += azSmoothA_ * delta;
            if (azSmooth_ > 180.0) azSmooth_ -= 360.0;
            else if (azSmooth_ < -180.0) azSmooth_ += 360.0;
            // Recompute only when the angle has actually moved enough to matter;
            // the delay itself is interpolated every sample, so nothing steps.
            if (std::fabs(delta) * (1.0 - azSmoothA_) < 1.0e-4) {
                // Arriving. Land the coefficients exactly on the target: the
                // 0.25-degree threshold below can otherwise leave them up to a
                // quarter degree short FOREVER, because once the angle snaps
                // this block never runs again. Two speakers that approached
                // their angles from opposite directions kept slightly different
                // errors, which showed up as a permanent -59 dB imbalance on a
                // centred source.
                azSmooth_ = azTarget_;
                updateCoefficients();
            } else {
                double since = azSmooth_ - coefAz_;
                if (since > 180.0) since -= 360.0;
                else if (since < -180.0) since += 360.0;
                if (std::fabs(since) > 0.25) updateCoefficients();
            }
        }

        line_[write_] = in;
        const double near = readDelayed(delayNear_);
        const double far = readDelayed(delayFar_);
        write_ = write_ + 1 < kMaxDelay ? write_ + 1 : 0;

        const bool toRight = azSmooth_ >= 0.0;
        const double rightIn = toRight ? near : far;
        const double leftIn = toRight ? far : near;
        outL = shadow(leftIn, bL0_, bL1_, aL1_, shadowXL_, shadowYL_);
        outR = shadow(rightIn, bR0_, bR1_, aR1_, shadowXR_, shadowYR_);
    }

    // The ITD this model produces at an angle, in seconds — exposed so the
    // tests can check the render path against the formula it claims to follow.
    static double woodworthItd(double azDegrees, double radiusMeters) {
        const double theta = std::fabs(azDegrees) * kPi / 180.0;
        const double ratio = radiusMeters / kSpeedOfSound;
        // Woodworth & Schlosberg: the extra path around a sphere. It grows to a
        // maximum beside the head and shortens again toward the back.
        return theta <= kPi * 0.5 ? ratio * (theta + std::sin(theta))
                                  : ratio * (kPi - theta + std::sin(theta));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kSpeedOfSound = 343.0;
    // The engine advertises rates up to 768 kHz (roomcut_audio_format.h), where
    // the longest ITD this model can ask for is 845 samples. 512 was sized for
    // 192 kHz and silently truncated above 384 kHz: measured at 768 kHz, a
    // source at 90 degrees came out 664 us late instead of 787 — the delay line
    // ran out and the direction cue went with it. 1024 covers the top rate with
    // room for the interpolator.
    static constexpr std::size_t kMaxDelay = 1024;
    // Brown & Duda's shadow shape: the zero runs from a 6 dB lift facing the ear
    // down to alphaMin at the deepest shadow, which they place at 150 degrees.
    static constexpr double kAlphaMin = 0.1;
    static constexpr double kThetaMin = 150.0;
    // The ears sit a little behind the interaural axis (their value).
    static constexpr double kEarAzimuth = 100.0;

    static double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }

    inline double readDelayed(double delaySamples) const {
        const double d = std::min(delaySamples, static_cast<double>(kMaxDelay - 2));
        const std::size_t whole = static_cast<std::size_t>(d);
        const double frac = d - static_cast<double>(whole);
        const std::size_t i0 = (write_ + kMaxDelay - whole) % kMaxDelay;
        const std::size_t i1 = i0 == 0 ? kMaxDelay - 1 : i0 - 1;
        return line_[i0] * (1.0 - frac) + line_[i1] * frac;
    }

    static inline double shadow(double x, double b0, double b1, double a1,
                                double& state_x, double& state_y) {
        const double y = b0 * x + b1 * state_x - a1 * state_y;
        state_x = x;
        state_y = y;
        return y;
    }

    void updateCoefficients() {
        coefAz_ = azSmooth_;
        const double itd = woodworthItd(azSmooth_, radius_) * fs_;
        delayNear_ = 0.0;
        delayFar_ = std::min(itd, static_cast<double>(kMaxDelay - 2));

        // Angle of incidence at each ear, folded into 0..180 degrees.
        const double toRight = azSmooth_ >= 0.0 ? 1.0 : -1.0;
        const double nearEar = kEarAzimuth * toRight;
        const double farEar = -kEarAzimuth * toRight;
        const double nearIncidence = incidence(azSmooth_, nearEar);
        const double farIncidence = incidence(azSmooth_, farEar);
        double nearB0, nearB1, nearA1, farB0, farB1, farA1;
        shadowCoefficients(nearIncidence, nearB0, nearB1, nearA1);
        shadowCoefficients(farIncidence, farB0, farB1, farA1);
        if (azSmooth_ >= 0.0) {
            bR0_ = nearB0; bR1_ = nearB1; aR1_ = nearA1;
            bL0_ = farB0;  bL1_ = farB1;  aL1_ = farA1;
        } else {
            bL0_ = nearB0; bL1_ = nearB1; aL1_ = nearA1;
            bR0_ = farB0;  bR1_ = farB1;  aR1_ = farA1;
        }
    }

    static double incidence(double sourceAz, double earAz) {
        double d = std::fabs(sourceAz - earAz);
        while (d > 360.0) d -= 360.0;
        if (d > 180.0) d = 360.0 - d;
        return d;
    }

    // Bilinear transform of H(s) = (1 + a*s/(2w0)) / (1 + s/(2w0)).
    void shadowCoefficients(double incidenceDeg, double& b0, double& b1, double& a1) const {
        const double alpha = (1.0 + kAlphaMin * 0.5)
            + (1.0 - kAlphaMin * 0.5) * std::cos(incidenceDeg / kThetaMin * kPi);
        const double k = 2.0 * fs_;
        const double h = k / (2.0 * w0_);
        const double g = alpha * k / (2.0 * w0_);
        const double norm = 1.0 + h;
        b0 = (1.0 + g) / norm;
        b1 = (1.0 - g) / norm;
        a1 = (1.0 - h) / norm;
    }

    double fs_ = 48000.0;
    double radiusCm_ = kDefaultHeadRadiusCm;
    double radius_ = kDefaultHeadRadiusCm * 0.01;
    double w0_ = kSpeedOfSound / (kDefaultHeadRadiusCm * 0.01);
    double azTarget_ = 0.0, azSmooth_ = 0.0, coefAz_ = 1.0e9;
    double azSmoothA_ = 0.002;
    double delayNear_ = 0.0, delayFar_ = 0.0;
    double bL0_ = 1.0, bL1_ = 0.0, aL1_ = 0.0;
    double bR0_ = 1.0, bR1_ = 0.0, aR1_ = 0.0;
    double shadowXL_ = 0.0, shadowYL_ = 0.0, shadowXR_ = 0.0, shadowYR_ = 0.0;
    double line_[kMaxDelay] = {0.0};
    std::size_t write_ = 0;
};

} // namespace roomcut

#endif // ROOMCUT_VIRTUAL_SPEAKER_HPP
