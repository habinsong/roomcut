/*
 * PresetValidator.hpp — enforces the bounds in shared/schemas/preset.schema.json
 * before any preset reaches the audio engine (docs/04 "Validated by
 * PresetValidator before it is ever applied"; DEVELOPMENT_PLAN.md priority #1).
 *
 * This is a safety gate, not a convenience: a preset from disk, the UI, or —
 * critically — the AI recommender (Phase 9) must pass here or be clamped before
 * it can change what the user hears. Out-of-range gains could blow speakers or
 * ears; the schema limits are the contract.
 *
 * Two modes:
 *   validate() — reject (report what's wrong) without mutating.
 *   clamp()    — coerce into range and report what was changed; the engine uses
 *                this so a slightly-bad preset degrades gracefully rather than
 *                being dropped (the limiter is still the last-resort guarantee).
 *
 * Header-only; mirrors ChainParams (the MVP subset). Full parametric-EQ /
 * spatial validation expands here as those land.
 */
#ifndef ROOMCUT_PRESET_VALIDATOR_HPP
#define ROOMCUT_PRESET_VALIDATOR_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "ChainParams.hpp"

namespace roomcut {

// Bounds copied verbatim from preset.schema.json. Keep in sync.
struct PresetBounds {
    static constexpr double kPreampMinDb = -24.0;
    static constexpr double kPreampMaxDb =  12.0;
    static constexpr double kEqGainMinDb = -24.0;
    static constexpr double kEqGainMaxDb =  24.0;
    static constexpr double kLimiterReleaseMinMs =   5.0;
    static constexpr double kLimiterReleaseMaxMs = 500.0;
    static constexpr double kOutputGainMinDb = -24.0;
    static constexpr double kOutputGainMaxDb =  12.0;
    static constexpr double kSpatialWidthMin = -100.0;
    static constexpr double kSpatialWidthMax =  100.0;
    static constexpr double kSpatialAmountMin =   0.0;
    static constexpr double kSpatialAmountMax = 100.0;
    static constexpr double kHighpassMinHz =   0.0;   // 0 = off
    static constexpr double kHighpassMaxHz = 400.0;
    static constexpr double kCompAmountMin =   0.0;
    static constexpr double kCompAmountMax = 100.0;
    static constexpr double kParamFreqMinHz =    20.0;
    static constexpr double kParamFreqMaxHz = 20000.0;
    static constexpr double kParamGainMinDb = -24.0;
    static constexpr double kParamGainMaxDb =  24.0;
    static constexpr double kParamQMin = 0.1;
    static constexpr double kParamQMax = 12.0;
    static constexpr int    kParamTypeMin = 0;        // BiquadType range
    static constexpr int    kParamTypeMax = 5;
};

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> issues; // empty when ok
    void fail(const std::string& m) { ok = false; issues.push_back(m); }
};

class PresetValidator {
public:
    // Non-mutating check against the schema bounds + finiteness.
    static ValidationResult validate(const ChainParams& p) {
        ValidationResult r;
        checkRange(r, p.preampDb, PresetBounds::kPreampMinDb, PresetBounds::kPreampMaxDb, "preampDb");
        for (std::size_t b = 0; b < p.eqGainsDb.size(); ++b) {
            checkRange(r, p.eqGainsDb[b], PresetBounds::kEqGainMinDb, PresetBounds::kEqGainMaxDb, "eqGainsDb[" + std::to_string(b) + "]");
        }
        checkRange(r, p.limiterReleaseMs, PresetBounds::kLimiterReleaseMinMs, PresetBounds::kLimiterReleaseMaxMs, "limiterReleaseMs");
        checkRange(r, p.outputGainDb, PresetBounds::kOutputGainMinDb, PresetBounds::kOutputGainMaxDb, "outputGainDb");
        checkRange(r, p.spatialWidth, PresetBounds::kSpatialWidthMin, PresetBounds::kSpatialWidthMax, "spatialWidth");
        checkRange(r, p.centerFocus, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "centerFocus");
        checkRange(r, p.crossfeed, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "crossfeed");
        checkRange(r, p.roomReduce, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "roomReduce");
        checkRange(r, p.highpassHz, PresetBounds::kHighpassMinHz, PresetBounds::kHighpassMaxHz, "highpassHz");
        checkRange(r, p.compAmount, PresetBounds::kCompAmountMin, PresetBounds::kCompAmountMax, "compAmount");
        for (std::size_t i = 0; i < p.parametric.size(); ++i) {
            const auto& band = p.parametric[i];
            const std::string tag = "parametric[" + std::to_string(i) + "]";
            if (band.type < PresetBounds::kParamTypeMin || band.type > PresetBounds::kParamTypeMax) r.fail(tag + ".type");
            checkRange(r, band.freqHz, PresetBounds::kParamFreqMinHz, PresetBounds::kParamFreqMaxHz, tag + ".freqHz");
            checkRange(r, band.gainDb, PresetBounds::kParamGainMinDb, PresetBounds::kParamGainMaxDb, tag + ".gainDb");
            checkRange(r, band.q, PresetBounds::kParamQMin, PresetBounds::kParamQMax, tag + ".q");
        }
        return r;
    }

    // Coerce into range. Returns the clamped params; `outResult` (if given)
    // lists every field that was out of range or non-finite.
    static ChainParams clamp(const ChainParams& in, ValidationResult* outResult = nullptr) {
        ChainParams p = in;
        ValidationResult r;
        p.preampDb = clampField(r, p.preampDb, PresetBounds::kPreampMinDb, PresetBounds::kPreampMaxDb, "preampDb");
        for (std::size_t b = 0; b < p.eqGainsDb.size(); ++b) {
            p.eqGainsDb[b] = clampField(r, p.eqGainsDb[b], PresetBounds::kEqGainMinDb, PresetBounds::kEqGainMaxDb, "eqGainsDb[" + std::to_string(b) + "]");
        }
        p.limiterReleaseMs = clampField(r, p.limiterReleaseMs, PresetBounds::kLimiterReleaseMinMs, PresetBounds::kLimiterReleaseMaxMs, "limiterReleaseMs");
        p.outputGainDb = clampField(r, p.outputGainDb, PresetBounds::kOutputGainMinDb, PresetBounds::kOutputGainMaxDb, "outputGainDb");
        p.spatialWidth = clampField(r, p.spatialWidth, PresetBounds::kSpatialWidthMin, PresetBounds::kSpatialWidthMax, "spatialWidth");
        p.centerFocus = clampField(r, p.centerFocus, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "centerFocus");
        p.crossfeed = clampField(r, p.crossfeed, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "crossfeed");
        p.roomReduce = clampField(r, p.roomReduce, PresetBounds::kSpatialAmountMin, PresetBounds::kSpatialAmountMax, "roomReduce");
        p.highpassHz = clampField(r, p.highpassHz, PresetBounds::kHighpassMinHz, PresetBounds::kHighpassMaxHz, "highpassHz");
        p.compAmount = clampField(r, p.compAmount, PresetBounds::kCompAmountMin, PresetBounds::kCompAmountMax, "compAmount");
        for (std::size_t i = 0; i < p.parametric.size(); ++i) {
            auto& band = p.parametric[i];
            const std::string tag = "parametric[" + std::to_string(i) + "]";
            if (band.type < PresetBounds::kParamTypeMin || band.type > PresetBounds::kParamTypeMax) {
                r.fail(tag + ".type"); band.type = 0;
            }
            band.freqHz = clampField(r, band.freqHz, PresetBounds::kParamFreqMinHz, PresetBounds::kParamFreqMaxHz, tag + ".freqHz");
            band.gainDb = clampField(r, band.gainDb, PresetBounds::kParamGainMinDb, PresetBounds::kParamGainMaxDb, tag + ".gainDb");
            band.q = clampField(r, band.q, PresetBounds::kParamQMin, PresetBounds::kParamQMax, tag + ".q");
        }
        if (outResult) *outResult = r;
        return p;
    }

private:
    static void checkRange(ValidationResult& r, double v, double lo, double hi, const std::string& name) {
        if (!std::isfinite(v)) { r.fail(name + " is not finite"); return; }
        if (v < lo) r.fail(name + " below minimum");
        else if (v > hi) r.fail(name + " above maximum");
    }

    static double clampField(ValidationResult& r, double v, double lo, double hi, const std::string& name) {
        if (!std::isfinite(v)) { r.fail(name + " non-finite → set to " + std::to_string(lo)); return lo; }
        if (v < lo) { r.fail(name + " clamped up"); return lo; }
        if (v > hi) { r.fail(name + " clamped down"); return hi; }
        return v;
    }
};

} // namespace roomcut

#endif // ROOMCUT_PRESET_VALIDATOR_HPP
