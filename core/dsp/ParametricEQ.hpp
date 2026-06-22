/*
 * ParametricEQ.hpp — a fixed bank of N fully-parametric biquad bands (docs/04
 * "Parametric EQ: up to N bands, each {enabled,type,frequency,gainDb,q}").
 *
 * Sits after the 10-band graphic EQ in the chain (they stack). Each band is an
 * independent Biquad whose type/freq/gain/Q the user sets; a disabled band is a
 * true pass-through. Stereo (channel-indexed) state; coefficients recomputed
 * only when a band changes. Real-time safe after prepare().
 *
 * Header-only to match RoomcutCore and stay unit-testable off the audio thread.
 */
#ifndef ROOMCUT_PARAMETRIC_EQ_HPP
#define ROOMCUT_PARAMETRIC_EQ_HPP

#include <array>
#include <cstddef>

#include "Biquad.hpp"

namespace roomcut {

// One parametric band. `type` indexes BiquadType (0=Bell … 5=Notch) so the
// struct stays a plain value type that mirrors the IPC/preset representation.
struct ParametricBand {
    bool   enabled = false;
    int    type    = 0;        // BiquadType: 0 Bell,1 LowShelf,2 HighShelf,3 HighPass,4 LowPass,5 Notch
    double freqHz  = 1000.0;
    double gainDb  = 0.0;      // used by Bell/shelves; ignored by pass/notch
    double q       = 1.0;
};

inline BiquadType parametricBiquadType(int t) {
    switch (t) {
        case 1: return BiquadType::LowShelf;
        case 2: return BiquadType::HighShelf;
        case 3: return BiquadType::HighPass;
        case 4: return BiquadType::LowPass;
        case 5: return BiquadType::Notch;
        default: return BiquadType::Bell;
    }
}

class ParametricEQ {
public:
    static constexpr std::size_t kNumBands = 6;

    void prepare(double fs) {
        fs_ = fs;
        for (std::size_t b = 0; b < kNumBands; ++b) rebuildBand(b);
    }

    void setBand(std::size_t b, const ParametricBand& band) {
        if (b >= kNumBands) return;
        bands_[b] = band;
        rebuildBand(b);
    }

    const ParametricBand& band(std::size_t b) const { return bands_[b]; }

    void reset() {
        for (auto& bq : filters_) bq.reset();
    }

    // Process one sample for `channel` through the active bands in order.
    inline float processSample(float x, std::size_t channel) {
        float y = x;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            if (active_[b]) y = filters_[b].processSample(y, channel);
        }
        return y;
    }

    // Analytic total magnitude (dB) of the active cascade at `freqHz` — for the
    // UI response curve / tests, not the audio path.
    double magnitudeDbAt(double freqHz) const {
        double total = 0.0;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            if (active_[b]) total += filters_[b].magnitudeDbAt(freqHz, fs_);
        }
        return total;
    }

private:
    void rebuildBand(std::size_t b) {
        const ParametricBand& p = bands_[b];
        // A disabled band, or a bell/shelf at exactly 0 dB, is a pass-through.
        const bool tonal = p.type == 0 || p.type == 1 || p.type == 2;
        active_[b] = p.enabled && !(tonal && p.gainDb == 0.0);
        if (active_[b]) {
            filters_[b].set(parametricBiquadType(p.type), fs_, p.freqHz, p.gainDb, p.q);
        } else {
            filters_[b].setIdentity();
        }
    }

    double fs_ = 48000.0;
    std::array<ParametricBand, kNumBands> bands_{};
    std::array<Biquad, kNumBands> filters_{};
    std::array<bool, kNumBands> active_{};
};

} // namespace roomcut

#endif // ROOMCUT_PARAMETRIC_EQ_HPP
