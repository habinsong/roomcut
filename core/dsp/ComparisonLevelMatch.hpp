#ifndef ROOMCUT_COMPARISON_LEVEL_MATCH_HPP
#define ROOMCUT_COMPARISON_LEVEL_MATCH_HPP

#include "GainRamp.hpp"
#include "KWeightedLevel.hpp"

namespace roomcut {

enum class LevelMatchState : uint32_t { Disabled, Measuring, Matched, NoSignal, Bypassed, Unavailable };

class ComparisonLevelMatch {
public:
    void prepare(double fs, std::size_t channels) {
        sampleRate_ = fs;
        warmupFrames_ = std::max<uint32_t>(1, std::lround(fs * 0.5));
        for (auto& meter : meters_) meter.prepare(fs, channels);
        for (auto& gain : gains_) gain.prepare(fs, 1, 200);
        enabled_ = bypassed_ = false;
        resetMeasurements();
    }

    void setEnabled(bool enabled) {
        if (enabled_ == enabled) return;
        enabled_ = enabled;
        resetMeasurements();
        if (!enabled) restoreGain();
    }

    void setBypassed(bool bypassed) {
        if (bypassed_ == bypassed) return;
        bypassed_ = bypassed;
        resetMeasurements();
        for (auto& gain : gains_)
            gain.prepare(sampleRate_, std::clamp(gain.current(), 0.0f, 1.0f), bypassed ? 15 : 200);
        if (bypassed) restoreGain();
    }

    void resetMeasurements() {
        for (auto& meter : meters_) meter.reset();
        waiting_ = warmupFrames_;
        signal_ = false;
        healthy_ = true;
    }

    std::array<float, 2> processFrame(const float* first, const float* second, bool healthy) {
        healthy_ = healthy;
        if (enabled_ && !bypassed_ && healthy) {
            const bool updated = meters_[0].processFrame(first);
            meters_[1].processFrame(second);
            if (waiting_ > 0) --waiting_;
            if (updated && waiting_ == 0 && meters_[0].ready() && meters_[1].ready()) {
                const double a = meters_[0].power(), b = meters_[1].power();
                // The absolute BS.1770 gate (-70 LKFS) excludes silence. Only
                // attenuate the louder path; never amplify a quiet reference.
                constexpr double gate = 1.172465304582298e-7;
                signal_ = std::isfinite(a) && std::isfinite(b) && a > gate && b > gate;
                if (signal_) {
                    const double target = std::min(a, b);
                    targets_[0] = static_cast<float>(std::sqrt(target / a));
                    targets_[1] = static_cast<float>(std::sqrt(target / b));
                    for (int i = 0; i < 2; ++i) gains_[i].setTarget(targets_[i]);
                } else restoreGain();
            }
        } else if (!healthy) restoreGain();
        return {std::clamp(gains_[0].next(), 0.0f, 1.0f), std::clamp(gains_[1].next(), 0.0f, 1.0f)};
    }

    bool unity(std::size_t index) const { return gains_[index].isUnity(); }
    float reductionDb(std::size_t index) const {
        return -20.0f * std::log10(std::clamp(gains_[index].current(), 1e-12f, 1.0f));
    }
    LevelMatchState state() const {
        if (!enabled_) return LevelMatchState::Disabled;
        if (bypassed_) return LevelMatchState::Bypassed;
        if (!healthy_) return LevelMatchState::Unavailable;
        if (waiting_ > 0 || !meters_[0].ready() || !meters_[1].ready()) return LevelMatchState::Measuring;
        if (!signal_) return LevelMatchState::NoSignal;
        for (int i = 0; i < 2; ++i) {
            const double ratio = gains_[i].current() / std::max(1e-12f, targets_[i]);
            if (std::abs(20 * std::log10(std::max(1e-12, ratio))) > 0.1) return LevelMatchState::Measuring;
        }
        return LevelMatchState::Matched;
    }

private:
    void restoreGain() {
        targets_ = {1, 1};
        for (auto& gain : gains_) gain.setTarget(1);
    }
    std::array<KWeightedLevel, 2> meters_;
    std::array<GainRamp, 2> gains_;
    std::array<float, 2> targets_{1, 1};
    uint32_t warmupFrames_ = 24000, waiting_ = 24000;
    double sampleRate_ = 48000;
    bool enabled_ = false, bypassed_ = false, signal_ = false, healthy_ = true;
};
} // namespace roomcut
#endif
