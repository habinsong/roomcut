/*
 * BuiltinPresets.hpp — the factory presets shipped with Roomcut, expressed in
 * the live chain (preamp + 10-band EQ + spatial + limiter). Bands are the
 * GraphicEQ centers: 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k.
 *
 * Every preset is run through PresetValidator::clamp() by the engine before use;
 * these are authored within bounds, but that gate is the guarantee.
 */
#ifndef ROOMCUT_BUILTIN_PRESETS_HPP
#define ROOMCUT_BUILTIN_PRESETS_HPP

#include <array>
#include <string>
#include <vector>

#include "ChainParams.hpp"

namespace roomcut {

struct BuiltinPreset {
    std::string id;
    std::string name;
    ChainParams params;
};

// Band index reference (GraphicEQ::kCenters):
//   0:31  1:62  2:125  3:250  4:500  5:1k  6:2k  7:4k  8:8k  9:16k
inline std::vector<BuiltinPreset> builtinPresets() {
    std::vector<BuiltinPreset> out;

    auto make = [](const char* id, const char* name) {
        BuiltinPreset p;
        p.id = id;
        p.name = name;
        p.params = ChainParams::flat();
        return p;
    };

    // Flat — true reference: chain is transparent, only the safety limiter runs.
    out.push_back(make("flat", "Flat"));

    // Clean — gentle tidy: trim low-mid mud, a touch of air. Subtle.
    {
        auto p = make("clean", "Clean");
        p.params.eqGainsDb[2] = -1.5; // 125
        p.params.eqGainsDb[3] = -2.0; // 250 (mud)
        p.params.eqGainsDb[8] =  1.5; // 8k (air)
        p.params.eqGainsDb[9] =  1.0; // 16k
        out.push_back(p);
    }

    // Dialogue — speech intelligibility (docs/09 Phase 7 spec): 90 Hz high-pass
    // for rumble, dip 250 (boxiness), lift 2–4 kHz presence, light compression to
    // even out speech levels, slight width reduction to pull the voice forward.
    {
        auto p = make("dialogue", "Dialogue");
        p.params.highpassHz = 90.0;   // rumble removal (was an EQ-band approximation)
        p.params.eqGainsDb[1] = -2.0; // 62 (gentle low cleanup above the HPF)
        p.params.eqGainsDb[3] = -3.0; // 250 (boxiness)
        p.params.eqGainsDb[6] =  3.0; // 2k (presence)
        p.params.eqGainsDb[7] =  4.0; // 4k (consonants)
        p.params.spatialWidth = -10.0;
        p.params.centerFocus = 12.0;
        p.params.crossfeed = 5.0;
        p.params.roomReduce = 10.0;
        p.params.compAmount = 60.0;   // light leveling (≈1.9:1 @ −12 dB, ~5 dB GR on peaks)
        out.push_back(p);
    }

    {
        auto p = make("original-focus", "Original Focus");
        p.params.preampDb = -1.0;
        p.params.eqGainsDb[3] = -2.5;
        p.params.eqGainsDb[4] = -1.5;
        p.params.eqGainsDb[6] =  1.5;
        p.params.eqGainsDb[7] =  2.0;
        p.params.spatialWidth = -35.0;
        p.params.centerFocus = 28.0;
        p.params.crossfeed = 12.0;
        p.params.roomReduce = 55.0;
        out.push_back(p);
    }

    {
        auto p = make("widen", "Widen");
        p.params.preampDb = -1.0;
        p.params.eqGainsDb[3] = -1.5;
        p.params.eqGainsDb[8] =  1.5;
        p.params.eqGainsDb[9] =  2.0;
        p.params.spatialWidth = 35.0;
        p.params.crossfeed = 4.0;
        out.push_back(p);
    }

    // Night — tame extremes for quiet listening: cut deep bass + very high,
    // faster limiter release so loud transients don't startle.
    {
        auto p = make("night", "Night");
        p.params.eqGainsDb[0] = -8.0; // 31
        p.params.eqGainsDb[1] = -5.0; // 62
        p.params.eqGainsDb[9] = -3.0; // 16k
        p.params.limiterReleaseMs = 60.0;
        out.push_back(p);
    }

    // Soft — warm, relaxed: roll off harsh upper-mid/treble slightly.
    {
        auto p = make("soft", "Soft");
        p.params.eqGainsDb[6] = -2.0; // 2k
        p.params.eqGainsDb[7] = -3.0; // 4k (harshness)
        p.params.eqGainsDb[8] = -2.0; // 8k
        out.push_back(p);
    }

    // Laptop Speaker — compensate tiny speakers: boost low-mid body the driver
    // can't make, lift presence, cut the shrill top. Preamp down to leave
    // headroom for the boosts.
    {
        auto p = make("laptop-speaker", "Laptop Speaker");
        p.params.preampDb = -3.0;
        p.params.eqGainsDb[2] =  4.0; // 125 (fake some low end)
        p.params.eqGainsDb[3] =  3.0; // 250
        p.params.eqGainsDb[6] =  2.0; // 2k
        p.params.eqGainsDb[8] = -2.0; // 8k (reduce shrillness)
        out.push_back(p);
    }

    // AirPods — gentle correction for the common AirPods tuning: slight low-mid
    // cut, modest presence/air to counter their mid-forward voicing.
    {
        auto p = make("airpods", "AirPods");
        p.params.eqGainsDb[3] = -2.0; // 250
        p.params.eqGainsDb[4] = -1.5; // 500
        p.params.eqGainsDb[7] =  2.0; // 4k
        p.params.eqGainsDb[9] =  2.0; // 16k (air)
        out.push_back(p);
    }

    return out;
}

} // namespace roomcut

#endif // ROOMCUT_BUILTIN_PRESETS_HPP
