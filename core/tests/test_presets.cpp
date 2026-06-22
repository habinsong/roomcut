/*
 * test_presets.cpp — PresetValidator bounds enforcement + built-in preset
 * sanity (docs/04 safety gate; DEVELOPMENT_PLAN.md priority #1).
 */
#include "BuiltinPresets.hpp"
#include "PresetValidator.hpp"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

using namespace roomcut;

static void test_flat_is_valid_and_unchanged() {
    ChainParams flat = ChainParams::flat();
    auto r = PresetValidator::validate(flat);
    CHECK(r.ok, "flat preset validates");

    ValidationResult cr;
    ChainParams clamped = PresetValidator::clamp(flat, &cr);
    CHECK(cr.ok, "flat preset needs no clamping");
    CHECK(clamped.preampDb == 0.0, "flat preamp unchanged");
    for (double g : clamped.eqGainsDb) CHECK(g == 0.0, "flat eq band unchanged");
}

static void test_rejects_dangerous_values() {
    ChainParams bad = ChainParams::flat();
    bad.preampDb = 48.0;          // way over +12
    bad.eqGainsDb[5] = 60.0;      // way over +24
    bad.spatialWidth = 200.0;
    auto r = PresetValidator::validate(bad);
    CHECK(!r.ok, "dangerous preset is rejected");
    CHECK(r.issues.size() >= 3, "each out-of-range field reported");
}

static void test_rejects_nan() {
    ChainParams bad = ChainParams::flat();
    bad.eqGainsDb[0] = std::nan("");
    auto r = PresetValidator::validate(bad);
    CHECK(!r.ok, "NaN gain rejected");
}

static void test_clamp_coerces_into_range() {
    ChainParams bad = ChainParams::flat();
    bad.preampDb = 48.0;
    bad.eqGainsDb[5] = 60.0;
    bad.eqGainsDb[6] = -60.0;
    bad.spatialWidth = 200.0;
    bad.centerFocus = 140.0;
    ValidationResult cr;
    ChainParams c = PresetValidator::clamp(bad, &cr);
    CHECK(!cr.ok, "clamp reports it changed things");
    CHECK(c.preampDb == PresetBounds::kPreampMaxDb, "preamp clamped to +12");
    CHECK(c.eqGainsDb[5] == PresetBounds::kEqGainMaxDb, "eq band clamped to +24");
    CHECK(c.eqGainsDb[6] == PresetBounds::kEqGainMinDb, "eq band clamped to -24");
    CHECK(c.spatialWidth == PresetBounds::kSpatialWidthMax, "width clamped to +100");
    CHECK(c.centerFocus == PresetBounds::kSpatialAmountMax, "center clamped to +100");
    // After clamping, the result must validate cleanly.
    CHECK(PresetValidator::validate(c).ok, "clamped preset validates");
}

static void test_clamp_nan_becomes_finite() {
    ChainParams bad = ChainParams::flat();
    bad.preampDb = std::nan("");
    ChainParams c = PresetValidator::clamp(bad);
    CHECK(std::isfinite(c.preampDb), "NaN field becomes finite after clamp");
    CHECK(PresetValidator::validate(c).ok, "clamped-from-NaN preset validates");
}

static void test_all_builtins_valid() {
    auto presets = builtinPresets();
    CHECK(presets.size() >= 9, "expected built-in presets present");
    bool sawFlat = false;
    bool sawOriginalFocus = false;
    bool sawWiden = false;
    for (const auto& bp : presets) {
        auto r = PresetValidator::validate(bp.params);
        if (!r.ok) {
            fprintf(stderr, "  preset '%s' invalid:\n", bp.id.c_str());
            for (auto& i : r.issues) fprintf(stderr, "    - %s\n", i.c_str());
        }
        CHECK(r.ok, ("built-in preset within bounds: " + bp.id).c_str());
        CHECK(!bp.id.empty() && !bp.name.empty(), "built-in preset has id+name");
        if (bp.id == "flat") sawFlat = true;
        if (bp.id == "original-focus") sawOriginalFocus = true;
        if (bp.id == "widen") sawWiden = true;
    }
    CHECK(sawFlat, "Flat preset is present");
    CHECK(sawOriginalFocus, "Original Focus preset is present");
    CHECK(sawWiden, "Widen preset is present");
}

int main() {
    test_flat_is_valid_and_unchanged();
    test_rejects_dangerous_values();
    test_rejects_nan();
    test_clamp_coerces_into_range();
    test_clamp_nan_becomes_finite();
    test_all_builtins_valid();

    if (g_failures == 0) { printf("all preset tests passed\n"); return 0; }
    fprintf(stderr, "%d preset check(s) failed\n", g_failures);
    return 1;
}
