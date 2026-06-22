/*
 * GraphicEQ.hpp — fixed 10-band graphic equalizer (docs/04-dsp.md), the EQ stage
 * of the MVP chain (Preamp → 10-band EQ → Limiter).
 *
 * Ten bell filters at the ISO-ish centers 31, 62, 125, 250, 500 Hz, 1, 2, 4, 8,
 * 16 kHz, cascaded. Each band is a Biquad per the shared engine. Stereo
 * (channel-indexed) processing; coefficients recomputed only when a band's gain
 * changes. Real-time safe.
 *
 * Header-only to match RoomcutCore and stay unit-testable off the audio thread.
 */
#ifndef ROOMCUT_GRAPHIC_EQ_HPP
#define ROOMCUT_GRAPHIC_EQ_HPP

#include <array>
#include <cstddef>

#include "Biquad.hpp"

namespace roomcut {

class GraphicEQ {
public:
    static constexpr std::size_t kNumBands = 10;

    // Fixed band center frequencies (Hz).
    static constexpr std::array<double, kNumBands> kCenters = {
        31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };

    // Default Q for a 10-band graphic EQ: ~1.41 gives roughly one-octave bands
    // that sum to a smooth response without large inter-band ripple.
    static constexpr double kBandQ = 1.41;

    GraphicEQ() { gains_.fill(0.0); }

    // Set the sample rate and (re)build all band coefficients. Call on init and
    // on format change. Preserves the current per-band gains.
    void prepare(double fs) {
        fs_ = fs;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            rebuildBand(b);
        }
    }

    // Set one band's gain in dB and recompute just that band. No-op if unchanged.
    void setBandGain(std::size_t band, double gainDb) {
        if (band >= kNumBands) return;
        if (gains_[band] == gainDb) return;
        gains_[band] = gainDb;
        rebuildBand(band);
    }

    double bandGain(std::size_t band) const {
        return band < kNumBands ? gains_[band] : 0.0;
    }

    // Clear filter history (preset crossfade start / format change).
    void reset() {
        for (auto& bq : bands_) bq.reset();
    }

    // Process one sample for `channel` through the active bands. A band at 0 dB
    // is an exact pass-through (identity biquad), so skipping it is bit-exact
    // and saves a biquad per flat band per sample — the common case is a mostly
    // flat curve, so most bands are skipped.
    inline float processSample(float x, std::size_t channel) {
        float y = x;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            if (active_[b]) y = bands_[b].processSample(y, channel);
        }
        return y;
    }

    // Process an interleaved stereo (or N-channel) buffer in place.
    void processInterleaved(float* buf, std::size_t frames, std::size_t channels) {
        for (std::size_t f = 0; f < frames; ++f) {
            for (std::size_t c = 0; c < channels; ++c) {
                float* s = &buf[f * channels + c];
                *s = processSample(*s, c);
            }
        }
    }

    // Analytic total magnitude (dB) of the cascade at `freqHz` — sum of each
    // band's dB contribution. For tests/UI curve, not the audio path.
    double magnitudeDbAt(double freqHz) const {
        double total = 0.0;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            total += bands_[b].magnitudeDbAt(freqHz, fs_);
        }
        return total;
    }

private:
    void rebuildBand(std::size_t b) {
        if (gains_[b] == 0.0) {
            bands_[b].setIdentity(); // flat band is a true pass-through
            active_[b] = false;      // …and skipped entirely on the audio path
        } else {
            bands_[b].set(BiquadType::Bell, fs_, kCenters[b], gains_[b], kBandQ);
            active_[b] = true;
        }
    }

    double fs_ = 48000.0;
    std::array<double, kNumBands> gains_{};
    std::array<Biquad, kNumBands> bands_{};
    std::array<bool, kNumBands> active_{};
};

} // namespace roomcut

#endif // ROOMCUT_GRAPHIC_EQ_HPP
