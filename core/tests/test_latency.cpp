/*
 * test_latency.cpp — what the engine reports as its own latency has to be the
 * delay you can actually measure by pushing an impulse through it. Roadmap item
 * 8: a reported number that nobody checked against a measurement is a guess.
 */
#include "DSPChain.hpp"
#include "ComparisonProcessor.hpp"
#include "SincResampler.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

// Index of the largest sample in channel 0.
static std::size_t peakFrame(const std::vector<float>& interleaved, std::size_t channels) {
    std::size_t best = 0;
    double value = 0;
    for (std::size_t f = 0; f < interleaved.size() / channels; ++f) {
        const double magnitude = std::fabs(interleaved[f * channels]);
        if (magnitude > value) { value = magnitude; best = f; }
    }
    return best;
}

static void testChainDelayMatchesWhatItReports() {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        DSPChain chain;
        chain.prepare(fs, 2);
        chain.setParams(ChainParams::flat());
        chain.reset();
        std::vector<float> signal(2048 * 2, 0.0f);
        signal[0] = signal[1] = 0.5f;   // one impulse, both channels
        chain.processInterleaved(signal.data(), signal.size() / 2);
        const auto measured = peakFrame(signal, 2);
        const auto reported = static_cast<std::size_t>(std::lround(chain.latencySeconds() * fs));
        CHECK(measured == reported, "the chain's reported delay is where the impulse comes out");
        CHECK(reported > 0, "and it is not zero — the limiter looks ahead");
    }
}

static void testComparisonReportsTheSameDelayAsOneChain() {
    ComparisonProcessor comparison;
    ComparisonSettings settings{ChainParams::flat(), ChainParams::flat(), false, 1};
    comparison.prepare(48000.0, 2, settings);
    DSPChain single;
    single.prepare(48000.0, 2);
    CHECK(std::fabs(comparison.latencySeconds() - single.latencySeconds()) < 1e-12,
          "running two chains does not add delay");
}

static void testResamplerDelayMatchesWhatItReports() {
    struct Case { double in, out; };
    for (Case c : {Case{48000, 96000}, Case{96000, 48000}, Case{44100, 48000}}) {
        SincResampler resampler;
        resampler.prepare(c.in, c.out, 2);
        // One impulse in, then silence: the output peak sits at the filter's delay.
        struct Source { std::size_t sent = 0; };
        Source source;
        auto feed = [](void* pointer, float* out, uint32_t frames, uint32_t channels) -> uint32_t {
            auto& state = *static_cast<Source*>(pointer);
            for (uint32_t f = 0; f < frames; ++f) {
                const bool first = state.sent == 0;
                for (uint32_t c = 0; c < channels; ++c) out[f * channels + c] = first ? 1.0f : 0.0f;
                ++state.sent;
            }
            return frames;
        };
        const uint32_t block = 4096;
        std::vector<float> output(block * 2), scratch(static_cast<std::size_t>(block * resampler.ratio() + 8) * 2);
        resampler.produce(output.data(), block, feed, &source, scratch.data(),
                          static_cast<uint32_t>(block * resampler.ratio()) + 8);
        const auto measured = peakFrame(output, 2);
        const auto reported = static_cast<std::size_t>(std::lround(resampler.latencySeconds() * c.out));
        // The peak lands on the output sample nearest the reported delay; a
        // fractional ratio can put it one sample either side.
        const auto difference = measured > reported ? measured - reported : reported - measured;
        CHECK(difference <= 1, "the resampler's reported delay is where the impulse comes out");
    }
}

static void testUnityRateAddsNoResamplerDelay() {
    SincResampler resampler;
    resampler.prepare(48000.0, 48000.0, 2);
    CHECK(resampler.latencySeconds() == 0.0, "matching rates are a passthrough, not a filter");
}

int main() {
    testChainDelayMatchesWhatItReports();
    testComparisonReportsTheSameDelayAsOneChain();
    testResamplerDelayMatchesWhatItReports();
    testUnityRateAddsNoResamplerDelay();
    if (failures == 0) {
        std::printf("all latency tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d latency check(s) failed\n", failures);
    return 1;
}
