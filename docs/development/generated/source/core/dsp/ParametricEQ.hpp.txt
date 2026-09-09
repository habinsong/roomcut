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

#include <algorithm>
#include <array>
#include <cmath>
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

    // Optional dynamic behaviour. Off by default, and off means the band is the
    // static filter it has always been — same coefficients, same output.
    // When on, the band's own level drives a downward move from gainDb: whatever
    // it exceeds thresholdDb by is taken off, up to rangeDb. The level is the
    // RMS of the band, so a full-scale sine sitting in it reads -3 dBFS.
    bool   dynamic     = false;
    double thresholdDb = -24.0;
    double rangeDb     = 0.0;   // 0 = nothing to take off, so nothing happens
    double attackMs    = 20.0;
    double releaseMs   = 200.0;

    bool operator==(const ParametricBand& other) const {
        return enabled == other.enabled && type == other.type && freqHz == other.freqHz
            && gainDb == other.gainDb && q == other.q
            && dynamic == other.dynamic && thresholdDb == other.thresholdDb
            && rangeDb == other.rangeDb && attackMs == other.attackMs
            && releaseMs == other.releaseMs;
    }
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
        resetDynamics();
    }

    void setBand(std::size_t b, const ParametricBand& band) {
        if (b >= kNumBands) return;
        if (bands_[b] == band) return;
        bands_[b] = band;
        rebuildBand(b);
    }

    const ParametricBand& band(std::size_t b) const { return bands_[b]; }

    void reset() {
        for (auto& bq : filters_) bq.reset();
        for (auto& bq : detectors_) bq.reset();
        resetDynamics();
    }

    // Frame entry point. Dynamic bands read every channel before any of them is
    // filtered, so left and right always get the same reduction and the image
    // does not move. A bank with no dynamic band takes the old per-channel path.
    void processFrame(float* frame, std::size_t channels) {
        if (dynamicBands_ > 0) trackDynamics(frame, channels);
        for (std::size_t c = 0; c < channels; ++c) frame[c] = processSample(frame[c], c);
    }

    // Current reduction of band `b` in dB (0 when it isn't dynamic). For meters
    // and tests, not the audio path.
    double dynamicReductionDb(std::size_t b) const {
        return b < kNumBands ? reduction_[b] : 0.0;
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
    // How often the dynamic gain is folded into the filter coefficients. Counted
    // in frames, so the result does not depend on the caller's block size.
    static constexpr unsigned kDynamicUpdateFrames = 32;
    // Below this the coefficients are left alone; smaller steps are inaudible and
    // recomputing them would only cost time.
    static constexpr double kDynamicStepDb = 0.02;

    void rebuildBand(std::size_t b) {
        const ParametricBand& p = bands_[b];
        // A disabled band, or a bell/shelf at exactly 0 dB, is a pass-through.
        const bool tonal = p.type == 0 || p.type == 1 || p.type == 2;
        // Only a tonal band has a gain to take away, and only a range above zero
        // has anything to take.
        dynamic_[b] = p.enabled && p.dynamic && tonal && p.rangeDb > 0.0;
        active_[b] = p.enabled && (!(tonal && p.gainDb == 0.0) || dynamic_[b]);
        reduction_[b] = 0.0;
        applied_[b] = 0.0;
        envelope_[b] = 0.0;
        if (dynamic_[b]) {
            detectors_[b].set(BiquadType::BandPass, fs_, p.freqHz, 0.0, p.q);
            detectors_[b].reset();
            attack_[b] = coefficientFor(p.attackMs);
            release_[b] = coefficientFor(p.releaseMs);
            // The level average is symmetric and fixed, so what the band reads is
            // its RMS and nothing else; attack and release then shape how fast the
            // gain follows that reading. Two periods of the band, never under 10 ms,
            // keeps a low band from riding its own waveform.
            average_[b] = coefficientFor(std::max(10.0, 2000.0 / std::max(20.0, p.freqHz)));
        }
        dynamicBands_ = 0;
        for (bool on : dynamic_) dynamicBands_ += on ? 1 : 0;
        if (active_[b]) {
            filters_[b].set(parametricBiquadType(p.type), fs_, p.freqHz, p.gainDb, p.q);
        } else {
            filters_[b].setIdentity();
            filters_[b].reset();
        }
    }

    double coefficientFor(double milliseconds) const {
        const double seconds = std::max(0.1, milliseconds) * 0.001;
        return 1.0 - std::exp(-1.0 / (seconds * fs_));
    }

    void resetDynamics() {
        envelope_.fill(0.0);
        reduction_.fill(0.0);
        applied_.fill(0.0);
        sinceUpdate_ = 0;
    }

    void trackDynamics(const float* frame, std::size_t channels) {
        for (std::size_t b = 0; b < kNumBands; ++b) {
            if (!dynamic_[b]) continue;
            // Mean square of the band, taken from whichever channel is louder so
            // both move together. Squares rather than peaks: the settled reading is
            // then the band's RMS, which does not depend on the attack time.
            double squared = 0.0;
            for (std::size_t c = 0; c < channels; ++c) {
                const double banded = detectors_[b].processSample(frame[c], c);
                squared = std::max(squared, banded * banded);
            }
            envelope_[b] += average_[b] * (squared - envelope_[b]);
            const double levelDb = 10.0 * std::log10(std::max(envelope_[b], 1e-18));
            const double target = std::clamp(levelDb - bands_[b].thresholdDb, 0.0, bands_[b].rangeDb);
            const double coefficient = target > reduction_[b] ? attack_[b] : release_[b];
            reduction_[b] += coefficient * (target - reduction_[b]);
        }
        if (++sinceUpdate_ < kDynamicUpdateFrames) return;
        sinceUpdate_ = 0;
        for (std::size_t b = 0; b < kNumBands; ++b) {
            if (!dynamic_[b] || std::fabs(reduction_[b] - applied_[b]) < kDynamicStepDb) continue;
            applied_[b] = reduction_[b];
            const ParametricBand& p = bands_[b];
            filters_[b].set(parametricBiquadType(p.type), fs_, p.freqHz, p.gainDb - applied_[b], p.q);
        }
    }

    double fs_ = 48000.0;
    std::array<ParametricBand, kNumBands> bands_{};
    std::array<Biquad, kNumBands> filters_{};
    std::array<bool, kNumBands> active_{};
    // Dynamic side: a band-pass detector per band, its envelope, and the gain
    // reduction it is asking for versus the one currently in the coefficients.
    std::array<Biquad, kNumBands> detectors_{};
    std::array<bool, kNumBands> dynamic_{};
    std::array<double, kNumBands> envelope_{}, reduction_{}, applied_{};
    std::array<double, kNumBands> attack_{}, release_{}, average_{};
    unsigned dynamicBands_ = 0;
    unsigned sinceUpdate_ = 0;
};

} // namespace roomcut

#endif // ROOMCUT_PARAMETRIC_EQ_HPP
