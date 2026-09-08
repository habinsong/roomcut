#ifndef ROOMCUT_ENGINE_SOUND_STATE_HPP
#define ROOMCUT_ENGINE_SOUND_STATE_HPP

#include "dsp/ComparisonProcessor.hpp"
#include "presets/BuiltinPresets.hpp"
#include "presets/PresetValidator.hpp"
#include <array>
#include <cstdio>
#include <string_view>

namespace roomcut {

// Control-thread sound state and validation. Disk, Mach and rendering remain
// outside this type; callers publish one complete settings value after success.
class EngineSoundState {
public:
    EngineSoundState(const ChainParams& params, const char* preset) {
        settings_.current = settings_.reference = PresetValidator::clamp(params);
        std::snprintf(preset_.data(), preset_.size(), "%s", preset);
    }
    const ComparisonSettings& settings() const { return settings_; }
    const ChainParams& parameters() const { return settings_.current; }
    const char* presetID() const { return preset_.data(); }
    uint32_t revision() const { return static_cast<uint32_t>(settings_.revision); }

    void apply(const ChainParams& params, const char* preset) {
        applyComparison(params, settings_.reference, false, preset);
    }
    void applyComparison(const ChainParams& current, const ChainParams& reference,
                         bool enabled, const char* preset = nullptr) {
        ComparisonSettings next{PresetValidator::clamp(current), PresetValidator::clamp(reference),
                                enabled, settings_.revision + 1};
        std::array<char, 32> label{};
        const char* name = preset ? preset : next.current == settings_.current ? preset_.data() : "custom";
        std::snprintf(label.data(), label.size(), "%s", name);
        settings_ = next;
        preset_ = label;
    }
    bool applyPreset(std::string_view preset, const ChainParams* reference = nullptr, bool enabled = false) {
        for (const auto& candidate : builtinPresets()) {
            if (std::string_view(candidate.id) == preset) {
                applyComparison(candidate.params, reference ? *reference : settings_.reference,
                                enabled, candidate.id.c_str());
                return true;
            }
        }
        return false;
    }

private:
    ComparisonSettings settings_;
    std::array<char, 32> preset_{};
};
} // namespace roomcut
#endif
