#include "ComparisonProcessor.hpp"
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

using namespace roomcut;
static int failures = 0;
static bool countAllocations = false;
static std::size_t allocations = 0;
void* operator new(std::size_t bytes) {
    if (countAllocations) ++allocations;
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static float tone(uint64_t at, double fs, float amplitude = 0.05f) {
    return amplitude * std::sin(2 * M_PI * 1000 * at / fs);
}

static double response(double fs, double frequency) {
    const double referenceFrequency = 48000 / M_PI * std::atan(fs / 48000 * std::tan(M_PI * frequency / fs));
    const auto z = std::polar(1.0, -2 * M_PI * referenceFrequency / 48000);
    const auto shelf = (1.53512485958697 - 2.69169618940638 * z + 1.19839281085285 * z * z)
                    / (1.0 - 1.69065929318241 * z + 0.73248077421585 * z * z);
    const auto highpass = (1.0 - 2.0 * z + z * z) / (1.0 - 1.99004745483398 * z + 0.99007225036621 * z * z);
    return std::norm(shelf * highpass);
}

static void weightingMatchesSpecifiedResponse() {
    for (double fs : {8000., 44100., 48000., 192000., 768000.}) {
        for (double frequency : {50., 1000., 4000.}) {
            if (frequency >= fs * 0.5) continue;
            KWeightedLevel level;
            level.prepare(fs, 2);
            for (uint64_t n = 0; n < static_cast<uint64_t>(fs); ++n) {
                const float x = 0.125f * std::sin(2 * M_PI * frequency * n / fs);
                float frame[2] = {x, -x};
                level.processFrame(frame);
            }
            const double expected = 0.125 * 0.125 * response(fs, frequency);
            CHECK(level.ready(), "four complete measurement blocks are available");
            CHECK(std::abs(10 * std::log10(level.power() / expected)) < 0.02,
                  "K-weighted measured power matches the specified filter response");
        }
    }
}

static void disabledModeIsTransparent() {
    ComparisonSettings settings;
    settings.current.preampDb = -6;
    settings.current.eqGainsDb[5] = 3;
    settings.current.crossfeed = 30;
    settings.current.spatialMode = 1;
    ComparisonProcessor comparison;
    comparison.prepare(48000, 2, settings);
    DSPChain original;
    original.prepare(48000, 2);
    original.setParams(settings.current); original.reset();
    bool equal = true;
    for (uint64_t n = 0; n < 24000; ++n) {
        float actual[2] = {tone(n, 48000), -tone(n, 48000)};
        float expected[2] = {actual[0], actual[1]};
        comparison.processInterleaved(actual, 1);
        original.processInterleaved(expected, 1);
        equal &= actual[0] == expected[0] && actual[1] == expected[1];
    }
    CHECK(equal, "disabled comparison is sample-identical to the original single chain");
    CHECK(comparison.metrics().state == LevelMatchState::Disabled, "disabled mode is reported honestly");
}

static void matchedSwitchPreservesLevel() {
    for (double fs : {44100., 48000., 192000.}) {
        ComparisonSettings settings;
        settings.enabled = true;
        settings.current.preampDb = 6;
        settings.reference.preampDb = -6;
        settings.revision = 10;
        ComparisonProcessor changed, reference;
        changed.prepare(fs, 2, settings);
        reference.prepare(fs, 2, settings);
        double difference = 0;
        for (uint64_t n = 0; n < static_cast<uint64_t>(fs * 2); ++n) {
            if (n == static_cast<uint64_t>(fs)) {
                CHECK(changed.metrics().state == LevelMatchState::Matched, "both live paths have matched loudness");
                CHECK(std::abs(changed.metrics().currentReductionDb - 12) < 0.02, "only the louder path is reduced by 12 dB");
                CHECK(changed.metrics().referenceReductionDb == 0, "the quieter path is never amplified");
                std::swap(settings.current, settings.reference);
                settings.revision = 11;
                changed.setComparison(settings);
            }
            float actual[2] = {tone(n, fs), tone(n, fs)};
            float expected[2] = {actual[0], actual[1]};
            changed.processInterleaved(actual, 1);
            reference.processInterleaved(expected, 1);
            if (n > fs) difference = std::max(difference, std::abs(double(actual[0]) - expected[0]));
        }
        std::printf("matched A/B fs=%.0f max sample difference=%.9f\n", fs, difference);
        CHECK(difference < 1e-6, "switching matched gain variants preserves the waveform without relearning");
        CHECK(changed.metrics().revision == 11 && changed.metrics().state == LevelMatchState::Matched,
              "rendered metrics identify the applied comparison revision");
        CHECK(changed.metrics().currentReductionDb == 0, "the selected quiet path keeps unity gain");
    }
}

static void missingSignalAndFaultsCannotBeReportedAsMatched() {
    ComparisonSettings settings;
    settings.enabled = true;
    ComparisonProcessor comparison;
    comparison.prepare(48000, 2, settings);
    CHECK(comparison.metrics().state == LevelMatchState::Measuring, "a fresh comparison starts measuring");
    for (int n = 0; n < 48000; ++n) {
        float frame[2] = {};
        comparison.processInterleaved(frame, 1);
    }
    CHECK(comparison.metrics().state == LevelMatchState::NoSignal, "silence cannot produce a ready match");
    settings.reference.preampDb = std::numeric_limits<double>::quiet_NaN();
    comparison.setComparison(settings);
    bool finite = true;
    for (int n = 0; n < 48000; ++n) {
        float frame[2] = {tone(n, 48000), tone(n, 48000)};
        comparison.processInterleaved(frame, 1);
        finite &= std::isfinite(frame[0]);
    }
    CHECK(finite && !comparison.safeBypassed(), "a failed reference cannot poison the selected chain");
    CHECK(comparison.metrics().state == LevelMatchState::Unavailable, "a failed reference is not a valid loudness comparison");
}

static void differentProcessingChainsMatchWeightedPower() {
    ComparisonSettings settings;
    settings.enabled = true;
    settings.current.eqGainsDb[2] = 6;
    settings.current.parametric[0] = {true, 2, 4000, -4, 0.8};
    settings.current.spatialWidth = 60;
    settings.current.compAmount = 30;
    settings.reference.highpassHz = 90;
    settings.reference.eqGainsDb[5] = -3;
    settings.reference.crossfeed = 40;
    settings.reference.spatialMode = 1;
    settings.reference.compAmount = 60;
    auto reversed = settings;
    std::swap(reversed.current, reversed.reference);
    ComparisonProcessor first, second;
    first.prepare(48000, 2, settings);
    second.prepare(48000, 2, reversed);
    KWeightedLevel firstLevel, secondLevel;
    firstLevel.prepare(48000, 2); secondLevel.prepare(48000, 2);
    for (uint64_t n = 0; n < 192000; ++n) {
        const double phase = 2 * M_PI * n / 48000;
        float a[2] = {static_cast<float>(0.08 * std::sin(phase * 120) + 0.07 * std::sin(phase * 1000) + 0.025 * std::sin(phase * 8000)),
                      static_cast<float>(0.06 * std::sin(phase * 150) + 0.04 * std::cos(phase * 1700) + 0.03 * std::sin(phase * 9000))};
        float b[2] = {a[0], a[1]};
        first.processInterleaved(a, 1); second.processInterleaved(b, 1);
        firstLevel.processFrame(a); secondLevel.processFrame(b);
    }
    const double difference = 10 * std::log10(firstLevel.power() / secondLevel.power());
    std::printf("EQ/spatial/compressor comparison difference=%.4f dB\n", difference);
    CHECK(std::abs(difference) < 0.1, "different EQ, spatial and dynamics chains match actual weighted output power");
    CHECK(first.metrics().state == LevelMatchState::Matched && second.metrics().state == LevelMatchState::Matched,
          "both spectral variants reach a valid comparison state");
}

static void bypassRemovesComparisonAttenuation() {
    ComparisonSettings settings;
    settings.enabled = true;
    settings.current.preampDb = 6; settings.reference.preampDb = -6;
    ComparisonProcessor processor;
    processor.prepare(48000, 2, settings);
    double error = 0;
    for (uint64_t n = 0; n < 98400; ++n) {
        if (n == 96000) processor.setBypass(true);
        float frame[2] = {tone(n, 48000), tone(n, 48000)};
        processor.processInterleaved(frame, 1);
        if (n >= 97440) error = std::max(error, std::abs(double(frame[0]) - tone(n - 96, 48000)));
    }
    CHECK(error < 1e-6, "bypass restores dry level within its ramp and limiter delay");
    CHECK(processor.metrics().state == LevelMatchState::Bypassed, "bypass is not presented as matched processing");
}

static void togglesRemainBoundedAndAllocateNothing() {
    ComparisonSettings settings;
    settings.enabled = true;
    settings.current.preampDb = 12;
    settings.reference.preampDb = -12;
    ComparisonProcessor comparison;
    comparison.prepare(768000, 2, settings);
    bool bounded = true;
    allocations = 0; countAllocations = true;
    for (uint64_t n = 0; n < 1536000; ++n) {
        if (n == 600000) { std::swap(settings.current, settings.reference); comparison.setComparison(settings); }
        if (n == 650000) { settings.enabled = false; comparison.setComparison(settings); }
        if (n == 670000) { settings.enabled = true; comparison.setComparison(settings); }
        if (n == 900000) comparison.setBypass(true);
        if (n == 1000000) comparison.setBypass(false);
        float frame[2] = {tone(n, 768000, 0.9f), -tone(n, 768000, 0.9f)};
        const float boost = 2;
        comparison.processInterleaved(frame, 1, &boost);
        bounded &= std::isfinite(frame[0]) && std::abs(frame[0]) <= 1.0f && std::abs(frame[1]) <= 1.0f;
    }
    countAllocations = false;
    CHECK(allocations == 0, "matched rendering, reference swaps and toggles allocate no C++ heap memory");
    CHECK(bounded, "all matched blends remain peak-safe even with 200 percent final gain");
}

int main() {
    weightingMatchesSpecifiedResponse();
    disabledModeIsTransparent();
    matchedSwitchPreservesLevel();
    missingSignalAndFaultsCannotBeReportedAsMatched();
    differentProcessingChainsMatchWeightedPower();
    bypassRemovesComparisonAttenuation();
    togglesRemainBoundedAndAllocateNothing();
    if (!failures) std::puts("all comparison tests passed");
    return failures ? 1 : 0;
}
