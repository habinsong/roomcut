/*
 * Upmixer.hpp — takes a stereo pair apart into the channels a 5.1/7.1 mix would
 * have had: a centre for dialogue, a front pair for everything that is panned,
 * and surrounds for whatever is uncorrelated between the two sides.
 *
 * The decomposition rests on two cues that a stereo pair already carries, both
 * measured per frequency band (Avendano & Jot, JAES 2004; Merimaa, Goodwin &
 * Jot, AES 123, 2007):
 *
 *   - Coherence: how alike the two channels are. A voice recorded to the centre
 *     appears identically in both; a hall tail does not.
 *   - Balance: whether the two channels carry the same level. A hard-panned
 *     guitar is perfectly coherent with silence on the other side, so coherence
 *     alone would mistake it for a centre image.
 *
 * Centre content is what is coherent AND balanced; ambience is what is
 * incoherent AND balanced. Anything panned is neither, and stays in front where
 * the mix engineer put it.
 *
 * Two properties are structural rather than tuned, and the tests hold them:
 *
 *   - It is a PARTITION, not a synthesis. Every output channel is carved out of
 *     the input and the pieces add back up to it exactly: centre + front + side
 *     + back == the original channel, sample for sample, at unity trims. An
 *     upmix that cannot be folded back down is an upmix that will not survive a
 *     mono fold or a downmixing player.
 *   - It adds no latency. The band split is the same complementary one-pole
 *     ladder the width shuffler uses (see Spatial.hpp): zero delay, and the
 *     bands sum back to the input exactly. Nothing here looks ahead, so a film
 *     stays in sync without the driver having to report a new latency.
 *
 * Merimaa et al. report that the time constant of the recursive correlation is
 * what decides how well this performs, so the two constants below are the
 * knobs that matter: a short estimate follows dialogue but chatters, a long one
 * is stable but smears a cut. They are separated into the estimate itself and
 * an anti-pumping smoother on the applied gains, the same structure the
 * adaptive width uses.
 *
 * Real-time safe after prepare(): no allocation, no locks, no look-ahead.
 */
#ifndef ROOMCUT_UPMIXER_HPP
#define ROOMCUT_UPMIXER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace roomcut {

// One frame of the upmixed layout. Back is used by 7.1 only; 5.1 leaves it at
// zero and gives the whole ambience to the sides.
struct UpmixFrame {
    double centre = 0.0;
    double frontL = 0.0, frontR = 0.0;
    double sideL = 0.0, sideR = 0.0;
    double backL = 0.0, backR = 0.0;
};

class Upmixer {
public:
    // Matches ChainParams::surroundType: 0 = off, 1 = the existing ambience
    // surround, 2 = 5.1, 3 = 7.1. Anything below k51 is a passthrough here —
    // the older modes live in Spatial.
    static constexpr int kOff = 0;
    static constexpr int k51 = 2;
    static constexpr int k71 = 3;

    static constexpr std::size_t kBands = 4;

    void prepare(double fs) {
        fs_ = fs > 0.0 ? fs : 48000.0;
        // Same crossovers as the width shuffler: ~250 Hz / 1.2 kHz / 9 kHz.
        // Bass below the first one is left whole — steering it would only move
        // energy a listener localises by the rest of the spectrum anyway.
        xover_[0] = 1.0 - std::exp(-2.0 * kPi * 250.0 / fs_);
        xover_[1] = 1.0 - std::exp(-2.0 * kPi * 1200.0 / fs_);
        xover_[2] = 1.0 - std::exp(-2.0 * kPi * 9000.0 / fs_);
        envA_ = 1.0 - std::exp(-1.0 / (fs_ * kEnvelopeSeconds));
        gainA_ = 1.0 - std::exp(-1.0 / (fs_ * kGainSmoothSeconds));
        reset();
    }

    // 0/1 pass the pair through untouched; 2 and 3 upmix.
    void setLayout(int layout) { layout_ = layout; }
    int layout() const { return layout_; }

    // The two controls a listener actually understands, in the shape the field
    // has used since Dolby Pro Logic II: they STEER rather than trim.
    //
    // Width is how much of the centre image becomes a discrete centre channel.
    // At 0 none of it is extracted and the centre stays the phantom the front
    // pair already makes; at 100 all of it moves to the centre speaker. A level
    // trim on the extracted channel could not do this — it only made an already
    // extracted centre quieter, which is why the control felt like it did
    // nothing and nobody could say what it was for.
    //
    // Depth is the same idea for the surrounds: how much of the ambient part of
    // the programme is steered behind the listener rather than left in front.
    void setCentreWidth(double percent) { centreSteer_ = clamp01(percent); }
    void setSurroundDepth(double percent) { surroundSteer_ = clamp01(percent); }

    void reset() {
        for (auto& band : band_) band = Band{};
        for (std::size_t i = 0; i + 1 < kBands; ++i) lLp_[i] = rLp_[i] = 0.0;
    }

    inline void process(double left, double right, UpmixFrame& out) {
        if (layout_ < k51) {
            out = UpmixFrame{};
            out.frontL = left;
            out.frontR = right;
            return;
        }

        double lb[kBands], rb[kBands];
        split(left, lLp_, lb);
        split(right, rLp_, rb);

        double centre = 0.0, frontL = 0.0, frontR = 0.0;
        double sideL = 0.0, sideR = 0.0, backL = 0.0, backR = 0.0;
        const double backShare = layout_ >= k71 ? kBackShare : 0.0;

        for (std::size_t i = 0; i < kBands; ++i) {
            Band& band = band_[i];
            band.ll += envA_ * (lb[i] * lb[i] - band.ll);
            band.rr += envA_ * (rb[i] * rb[i] - band.rr);
            band.lr += envA_ * (lb[i] * rb[i] - band.lr);

            // Coherence, clamped to 0..1: out-of-phase material is as unlike a
            // centre image as uncorrelated material, so negative counts as zero.
            const double product = band.ll * band.rr;
            const double denominator = product > 0.0 ? std::sqrt(product) : 0.0;
            double coherence = denominator > kEnergyFloor ? band.lr / denominator : 0.0;
            coherence = coherence < 0.0 ? 0.0 : (coherence > 1.0 ? 1.0 : coherence);
            // Balance is 1 when both sides carry the same level and falls to 0
            // as the image pans off to one side. Without it a hard-panned
            // source reads as perfectly coherent with the silence opposite.
            const double sum = band.ll + band.rr;
            const double balance = sum > kEnergyFloor ? 2.0 * denominator / sum : 0.0;

            band.centreGain += gainA_ * (coherence * balance * centreSteer_ - band.centreGain);
            band.ambienceGain += gainA_ * ((1.0 - coherence) * balance * surroundSteer_ - band.ambienceGain);

            // Carve the centre out of both sides, then split what is left
            // between front and surround. Each step removes exactly what it
            // hands on, which is what keeps the fold-down exact.
            const double bandCentre = (lb[i] + rb[i]) * 0.5 * band.centreGain;
            const double restL = lb[i] - bandCentre;
            const double restR = rb[i] - bandCentre;
            const double toSurround = band.ambienceGain;

            centre += bandCentre;
            frontL += restL * (1.0 - toSurround);
            frontR += restR * (1.0 - toSurround);
            const double surroundL = restL * toSurround;
            const double surroundR = restR * toSurround;
            sideL += surroundL * (1.0 - backShare);
            sideR += surroundR * (1.0 - backShare);
            backL += surroundL * backShare;
            backR += surroundR * backShare;
        }

        out.centre = centre;
        out.frontL = frontL;
        out.frontR = frontR;
        out.sideL = sideL;
        out.sideR = sideR;
        out.backL = backL;
        out.backR = backR;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    // Merimaa et al.: this estimate's time constant decides the separation.
    // 30 ms follows a line of dialogue without chattering on transients.
    static constexpr double kEnvelopeSeconds = 0.030;
    // A second, slower smoother on the applied gains, so a busy mix cannot make
    // the steering breathe (the adaptive width uses the same two-stage shape).
    static constexpr double kGainSmoothSeconds = 0.080;
    static constexpr double kEnergyFloor = 1.0e-12;
    // 7.1 splits the ambience between sides and backs. The two pairs differ by
    // where they are rendered, not by any decorrelation, so the fold-down stays
    // exact and nothing combs.
    static constexpr double kBackShare = 0.4;

    struct Band {
        double ll = 0.0, rr = 0.0, lr = 0.0;
        double centreGain = 0.0, ambienceGain = 0.0;
    };

    static double clamp01(double percent) {
        const double v = percent * 0.01;
        return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    }

    // Complementary one-pole ladder: each band is a low-pass of what the
    // previous ones left behind, and the last band is the remainder, so the
    // four sum back to the input exactly at every sample.
    inline void split(double x, double (&state)[kBands - 1], double (&band)[kBands]) {
        double remainder = x;
        for (std::size_t i = 0; i + 1 < kBands; ++i) {
            state[i] += xover_[i] * (remainder - state[i]);
            band[i] = state[i];
            remainder -= state[i];
        }
        band[kBands - 1] = remainder;
    }

    double fs_ = 48000.0;
    double xover_[kBands - 1] = {0.032195, 0.149003, 0.690542};
    double envA_ = 0.0007, gainA_ = 0.00026;
    double centreSteer_ = 1.0, surroundSteer_ = 1.0;
    int layout_ = kOff;
    Band band_[kBands]{};
    double lLp_[kBands - 1] = {0.0, 0.0, 0.0};
    double rLp_[kBands - 1] = {0.0, 0.0, 0.0};
};

} // namespace roomcut

#endif // ROOMCUT_UPMIXER_HPP
