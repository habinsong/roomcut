#include "RenderPipeline.hpp"
#include "AnalysisTap.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

using namespace roomcut;
static int failures = 0;
static bool countAllocations = false;
static size_t allocations = 0;

void* operator new(size_t size) {
    if (countAllocations) ++allocations;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }

#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

struct Source {
    float value = 0.25f;
    uint32_t available = UINT32_MAX;
    uint64_t framesRead = 0;

    static uint32_t read(void* context, float* output, uint32_t frames, uint32_t channels) {
        auto& source = *static_cast<Source*>(context);
        const uint32_t got = std::min(frames, source.available);
        if (source.available != UINT32_MAX) source.available -= got;
        for (uint32_t i = 0; i < got * channels; ++i) output[i] = source.value;
        source.framesRead += got;
        return got;
    }
};

static void testSilenceAndRecovery(double inputRate, double outputRate, uint32_t blockFrames) {
    RenderPipeline pipeline;
    pipeline.prepare(inputRate, outputRate, ChainParams::flat());
    Source source;
    std::vector<float> output((size_t)blockFrames * 2);
    const uint32_t blocks = (uint32_t)std::ceil(outputRate * 0.02 / blockFrames) + 2;
    for (uint32_t i = 0; i < blocks; ++i) {
        pipeline.render(output.data(), blockFrames, &Source::read, &source, 1);
    }
    CHECK(output.back() == 0.25f, "flat render retains a safe input level");
    source.available = 7; // include a partial read before the fully empty blocks
    for (uint32_t i = 0; i < blocks; ++i) {
        pipeline.render(output.data(), blockFrames, &Source::read, &source, 1);
        for (uint32_t f = 0; f < blockFrames; ++f) {
            CHECK(output[f * 2] == output[f * 2 + 1], "dropout fade preserves stereo equality");
        }
    }
    CHECK(std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }),
          "empty source stays exactly silent after the fade and limiter delay");
    for (int i = 0; i < 3; ++i) {
        pipeline.render(output.data(), blockFrames, &Source::read, &source, 1);
        CHECK(output.back() == 0, "held sample never reappears during prolonged silence");
    }
    source.available = UINT32_MAX;
    float previous = 0;
    float maxJump = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        pipeline.render(output.data(), blockFrames, &Source::read, &source, 1);
        for (uint32_t f = 0; f < blockFrames; ++f) {
            maxJump = std::max(maxJump, std::fabs(output[f * 2] - previous));
            previous = output[f * 2];
        }
    }
    CHECK(std::fabs(output.back() - 0.25f) < 1e-6, "audio recovers after a dropout");
    CHECK(maxJump < 0.003f, "recovery is a per-frame fade, not a hard step");
}

static void testBoostIsLimited(bool bypass) {
    RenderPipeline pipeline;
    pipeline.prepare(48000, 48000, ChainParams::flat());
    pipeline.setBypass(bypass);
    Source source; source.value = 0.8f;
    std::vector<float> output(1024 * 2);
    RenderMetrics metrics;
    for (int block = 0; block < 20; ++block) {
        metrics = pipeline.render(output.data(), 1024, &Source::read, &source, 2.0f);
        CHECK(metrics.peak <= 1.0f, "200 percent output is limited, including bypass");
    }
    CHECK(metrics.peak > 0.9f, "boost still increases the usable output level");
    CHECK(metrics.limiterReductionDb > 3.5f, "limiter meter includes the final boost");
}

static void testRateConversionAndLargeBlocks() {
    for (const auto rates : {std::pair{44100.0, 48000.0}, {192000.0, 48000.0},
                             {768000.0, 44100.0}, {48000.0, 384000.0}}) {
        RenderPipeline pipeline;
        pipeline.prepare(rates.first, rates.second, ChainParams::flat());
        Source source;
        constexpr uint32_t frames = 9001; // beyond the previous 8192-frame scratch limit
        std::vector<float> output(frames * 2);
        for (int block = 0; block < 3; ++block) {
            pipeline.render(output.data(), frames, &Source::read, &source, 1);
        }
        const double expected = frames * 3 * rates.first / rates.second;
        CHECK(std::fabs((double)source.framesRead - expected) < 20,
              "rate conversion consumes all required input, including downsampling");
        CHECK(std::fabs(output.back() - 0.25f) < 1e-5,
              "large callbacks do not lose their tail or overflow scratch storage");
    }
}

static void testRateConversionFiltersBeforeDSP() {
    struct Tone {
        double frequency;
        uint64_t at = 0;
        static uint32_t read(void* context, float* out, uint32_t frames, uint32_t channels) {
            auto& source = *static_cast<Tone*>(context);
            for (uint32_t f = 0; f < frames; ++f) {
                const float sample = 0.25f * std::sin(2 * M_PI * source.frequency * source.at++ / 96000);
                for (uint32_t c = 0; c < channels; ++c) out[f * channels + c] = sample;
            }
            return frames;
        }
    };
    for (double frequency : {1000., 30000.}) {
        RenderPipeline pipeline;
        pipeline.prepare(96000, 48000, ChainParams::flat());
        Tone source{frequency};
        std::array<float, 512 * 2> output{};
        RenderMetrics metrics;
        for (int block = 0; block < 20; ++block)
            metrics = pipeline.render(output.data(), 512, Tone::read, &source, 1);
        CHECK(!metrics.safeBypass, "rate conversion keeps the actual engine render path healthy");
        if (frequency == 1000)
            CHECK(std::abs(metrics.peak - 0.25f) < 0.001f, "the engine preserves audible input through sample-rate conversion");
        else
            CHECK(metrics.peak < 1e-5f, "the engine suppresses a 30 kHz input instead of aliasing it to 18 kHz");
    }
}

static void testInvalidInputDoesNotPoisonDSP() {
    RenderPipeline pipeline;
    pipeline.prepare(48000, 48000, ChainParams::flat());
    Source source; source.value = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> output(512 * 2);
    auto metrics = pipeline.render(output.data(), 512, &Source::read, &source, 1);
    CHECK(!metrics.safeBypass, "invalid source samples are removed before filters");
    source.value = 0.25f;
    for (int i = 0; i < 3; ++i) pipeline.render(output.data(), 512, &Source::read, &source, 1);
    CHECK(output.back() == 0.25f, "finite audio recovers after invalid source samples");

    auto invalid = ChainParams::flat();
    invalid.preampDb = std::numeric_limits<double>::quiet_NaN();
    pipeline.setParams(invalid); // exercise the final guard, beyond the IPC validator
    source.value = 0.8f;
    for (int i = 0; i < 5; ++i) {
        metrics = pipeline.render(output.data(), 512, &Source::read, &source, 2);
        CHECK(metrics.peak <= 1, "safe bypass retains final output protection");
        CHECK(std::all_of(output.begin(), output.end(), [](float x) { return std::isfinite(x); }),
              "non-finite filter state cannot reach the limiter or output");
    }
    CHECK(metrics.safeBypass, "invalid DSP state latches safe bypass");
    source.value = std::numeric_limits<float>::max();
    for (int i = 0; i < 10; ++i) {
        pipeline.render(output.data(), 512, &Source::read, &source, 2);
        CHECK(std::all_of(output.begin(), output.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; }),
              "even finite input that overflows after gain cannot poison the output");
    }
}

static void testLimiterReleaseReachesDSP() {
    DSPChain fast, slow;
    fast.prepare(48000, 2); slow.prepare(48000, 2);
    auto params = ChainParams::flat();
    params.limiterReleaseMs = 5; fast.setParams(params);
    params.limiterReleaseMs = 500; slow.setParams(params);
    std::vector<float> a(48000 * 2), b;
    for (int f = 0; f < 48000; ++f) a[f * 2] = a[f * 2 + 1] = f < 4800 ? 2.0f : 0.1f;
    b = a;
    fast.processInterleaved(a.data(), 48000);
    slow.processInterleaved(b.data(), 48000);
    CHECK(a[6000 * 2] > b[6000 * 2] * 1.5f,
          "5 ms release recovers audibly faster than 500 ms after live setParams");
}

static void testRenderDoesNotAllocate() {
    RenderPipeline pipeline;
    AnalysisTap analysis;
    analysis.prepare(48000);
    pipeline.prepare(192000, 48000, ChainParams::flat());
    Source source;
    std::array<float, 512 * 2> output{};
    ChainParams params;
    allocations = 0;
    countAllocations = true;
    for (int block = 0; block < 100; ++block) {
        params.eqGainsDb[5] = block % 2 ? 6 : -6;
        params.limiterReleaseMs = block % 2 ? 5 : 500;
        pipeline.setParams(params);
        pipeline.setBypass(block % 3 == 0);
        pipeline.render(output.data(), 512, &Source::read, &source, 2);
        analysis.push(output.data(), 512);
    }
    countAllocations = false;
    CHECK(allocations == 0, "rendering and live parameter updates perform no C++ heap allocations");
}

static void testRepeatedParamsDoNotChangeAudio() {
    RenderPipeline pipeline;
    auto params = ChainParams::flat();
    params.preampDb = -6;
    pipeline.prepare(48000, 48000, params);
    Source source;
    std::array<float, 512 * 2> output{};
    for (int i = 0; i < 10; ++i) pipeline.render(output.data(), 512, &Source::read, &source, 1);
    const float expected = output.back();
    pipeline.setParams(params);
    for (int i = 0; i < 3; ++i) {
        pipeline.render(output.data(), 512, &Source::read, &source, 1);
        CHECK(std::all_of(output.begin(), output.end(), [expected](float x) { return x == expected; }),
              "reapplying the current preset cannot dip the mix or change the audio");
    }
}

static void testMatchedComparisonReachesTheRenderPath() {
    struct Tone {
        uint64_t at = 0;
        static uint32_t read(void* context, float* out, uint32_t frames, uint32_t channels) {
            auto& source = *static_cast<Tone*>(context);
            for (uint32_t f = 0; f < frames; ++f) {
                const float value = 0.05f * std::sin(2 * M_PI * 1000 * source.at++ / 44100);
                for (uint32_t c = 0; c < channels; ++c) out[f * channels + c] = value;
            }
            return frames;
        }
    };
    ComparisonSettings settings;
    settings.current.preampDb = 6; settings.reference.preampDb = -6;
    settings.enabled = true; settings.revision = 42;
    RenderPipeline pipeline;
    pipeline.prepare(44100, 48000, settings);
    Tone source;
    std::array<float, 512 * 2> output{};
    for (int block = 0; block < 120; ++block) pipeline.render(output.data(), 512, Tone::read, &source, 1);
    const auto before = pipeline.comparisonMetrics();
    CHECK(before.state == LevelMatchState::Matched && before.revision == 42, "rendered comparison telemetry belongs to the applied settings");
    CHECK(std::abs(before.currentReductionDb - 12) < 0.05, "the live sample-rate conversion and DSP path apply comparison attenuation");
    std::swap(settings.current, settings.reference); settings.revision = 43;
    pipeline.setComparison(settings);
    for (int block = 0; block < 4; ++block) pipeline.render(output.data(), 512, Tone::read, &source, 1);
    const auto after = pipeline.comparisonMetrics();
    CHECK(after.state == LevelMatchState::Matched && after.revision == 43, "a matched A/B switch retains its learned levels in the actual render path");
    CHECK(after.currentReductionDb < 0.05 && std::abs(after.referenceReductionDb - 12) < 0.05,
          "the quieter selected preset and louder reference retain the correct gains");
}

int main() {
    for (double rate : {44100., 48000., 96000., 192000., 384000., 768000.}) {
        for (uint32_t frames : {1u, 64u, 257u, 1024u, 8193u}) testSilenceAndRecovery(rate, rate, frames);
    }
    for (const auto rates : {std::pair{44100.,48000.}, {96000.,48000.},
                             {768000.,44100.}, {48000.,384000.}})
        for (uint32_t frames : {64u, 1024u}) testSilenceAndRecovery(rates.first, rates.second, frames);
    testBoostIsLimited(false);
    testBoostIsLimited(true);
    testRateConversionAndLargeBlocks();
    testRateConversionFiltersBeforeDSP();
    testInvalidInputDoesNotPoisonDSP();
    testLimiterReleaseReachesDSP();
    testRenderDoesNotAllocate();
    testRepeatedParamsDoNotChangeAudio();
    testMatchedComparisonReachesTheRenderPath();
    if (failures == 0) std::puts("all render pipeline tests passed");
    return failures == 0 ? 0 : 1;
}
