#ifndef ROOMCUT_COMPARISON_PROCESSOR_HPP
#define ROOMCUT_COMPARISON_PROCESSOR_HPP

#include "ComparisonLevelMatch.hpp"
#include "DSPChain.hpp"
#include <stdexcept>

namespace roomcut {

struct ComparisonSettings {
    ChainParams current, reference;
    bool enabled = false;
    uint64_t revision = 0;
};

struct ComparisonMetrics {
    LevelMatchState state = LevelMatchState::Disabled;
    float currentReductionDb = 0, referenceReductionDb = 0;
    uint64_t revision = 0;
};

// Two complete, time-aligned chains let A/B loudness be measured on the same
// programme samples, including each chain's limiter. Compensation after those
// limiters only attenuates; the output blend therefore remains peak-safe.
class ComparisonProcessor {
public:
    void prepare(double fs, std::size_t channels, const ComparisonSettings& settings = {}) {
        if (channels < 1 || channels > 2) throw std::invalid_argument("comparison requires mono or stereo");
        channels_ = channels;
        current_ = 0;
        settings_ = settings;
        for (std::size_t i = 0; i < 2; ++i) {
            chains_[i].prepare(fs, channels);
            chains_[i].setParams(i == 0 ? settings.current : settings.reference);
            chains_[i].setBypass(bypassed_);
            chains_[i].reset();
        }
        selection_.prepare(fs, 0, 15);
        match_.prepare(fs, channels);
        match_.setEnabled(settings.enabled);
        match_.setBypassed(bypassed_);
    }

    void setParams(const ChainParams& params) {
        setComparison({params, settings_.reference, false, settings_.revision});
    }

    void setComparison(const ComparisonSettings& settings) {
        const bool canSwap = settings_.enabled && settings.enabled
            && settings.current == chains_[1 - current_].params()
            && settings.reference == chains_[current_].params();
        bool changed = false;
        if (canSwap && !(settings.current == chains_[current_].params())) {
            current_ = 1 - current_;
            selection_.setTarget(static_cast<float>(current_));
        } else {
            changed = !(settings.current == chains_[current_].params())
                || !(settings.reference == chains_[1 - current_].params());
            chains_[current_].setParams(settings.current);
            chains_[1 - current_].setParams(settings.reference);
        }
        if (settings.enabled && !settings_.enabled) chains_[1 - current_].reset();
        match_.setEnabled(settings.enabled);
        if (changed) match_.resetMeasurements();
        settings_ = settings;
    }

    void setBypass(bool bypassed) {
        if (bypassed_ == bypassed) return;
        bypassed_ = bypassed;
        for (auto& chain : chains_) chain.setBypass(bypassed);
        match_.setBypassed(bypassed);
    }

    void processInterleaved(float* output, std::size_t frames, const float* finalGains = nullptr) {
        if (!settings_.enabled && selection_.current() == current_ && match_.unity(current_)) {
            chains_[current_].processInterleaved(output, frames, finalGains);
            return;
        }
        for (std::size_t f = 0; f < frames; ++f) {
            float* frame = output + f * channels_;
            float paths[2][2]{};
            const bool both = settings_.enabled || selection_.current() != current_;
            for (std::size_t i = 0; i < 2; ++i) {
                if (!both && i != current_) continue;
                std::copy_n(frame, channels_, paths[i]);
                chains_[i].processInterleaved(paths[i], 1, finalGains ? finalGains + f : nullptr);
            }
            const auto gains = match_.processFrame(paths[0], paths[1],
                !chains_[0].safeBypassed() && !chains_[1].safeBypassed());
            const double mix = std::clamp(selection_.next(), 0.0f, 1.0f);
            for (std::size_t c = 0; c < channels_; ++c)
                frame[c] = static_cast<float>(paths[0][c] * gains[0] * (1 - mix) + paths[1][c] * gains[1] * mix);
        }
    }

    const ChainParams& params() const { return settings_.current; }
    bool safeBypassed() const { return chains_[current_].safeBypassed(); }
    double limiterGainReductionDb() const {
        if (selection_.current() != current_)
            return std::max(chains_[0].limiterGainReductionDb(), chains_[1].limiterGainReductionDb());
        return chains_[current_].limiterGainReductionDb();
    }
    ComparisonMetrics metrics() const {
        auto state = match_.state();
        if (state == LevelMatchState::Matched && selection_.current() != current_) state = LevelMatchState::Measuring;
        return {state, match_.reductionDb(current_), match_.reductionDb(1 - current_), settings_.revision};
    }

private:
    std::array<DSPChain, 2> chains_;
    ComparisonLevelMatch match_;
    GainRamp selection_;
    ComparisonSettings settings_;
    std::size_t channels_ = 2, current_ = 0;
    bool bypassed_ = false;
};
} // namespace roomcut
#endif
