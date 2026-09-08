#include "DSPChain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace roomcut;

static int failures = 0;
#define CHECK(condition, message) do { \
    if (!(condition)) { std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } \
} while (0)

static float dcFrame(DSPChain& chain) {
    float frame[2] = {0.25f, 0.25f};
    chain.processInterleaved(frame, 1);
    CHECK(frame[0] == frame[1], "transition gain is identical in both channels");
    return frame[0];
}

static void gainChangesStayBetweenEndpoints() {
    for (double fs : {44100.0, 48000.0, 192000.0, 768000.0}) {
        const auto rampFrames = static_cast<std::size_t>(std::lround(fs * 0.015));
        for (bool outputGain : {false, true}) {
            for (bool increasing : {false, true}) {
                DSPChain chain;
                chain.prepare(fs);
                ChainParams params;
                double& gain = outputGain ? params.outputGainDb : params.preampDb;
                gain = increasing ? -12.0 : -6.0;
                chain.setParams(params);
                chain.reset();
                for (std::size_t n = 0; n < rampFrames * 2; ++n) dcFrame(chain);
                const double from = 0.25 * dbToLin(gain);
                gain = increasing ? 6.0 : -12.0;
                const double to = 0.25 * dbToLin(gain);
                chain.setParams(params);
                double previous = from, jump = 0, low = from, high = from;
                for (std::size_t n = 0; n < rampFrames * 3; ++n) {
                    const double sample = dcFrame(chain);
                    jump = std::max(jump, std::abs(sample - previous));
                    low = std::min(low, sample);
                    high = std::max(high, sample);
                    previous = sample;
                }
                std::printf("gain fs=%.0f output=%d up=%d max_step=%.9f\n",
                            fs, outputGain, increasing, jump);
                CHECK(low >= std::min(from, to) - 1e-6 && high <= std::max(from, to) + 1e-6,
                      "preset gain transition stays between its two endpoints");
                CHECK(jump <= std::abs(to - from) * 2.0 / rampFrames + 1e-6,
                      "preset gain transition has no instantaneous level step");
                CHECK(std::abs(previous - to) < 1e-6, "new gain settles within the transition window");
            }
        }
    }
}

static void limiterReleaseDoesNotDipTheSignal() {
    DSPChain changed, reference;
    changed.prepare(48000);
    reference.prepare(48000);
    ChainParams params;
    params.preampDb = -12;
    changed.setParams(params);
    reference.setParams(params);
    changed.reset();
    reference.reset();
    for (int n = 0; n < 2000; ++n) { dcFrame(changed); dcFrame(reference); }
    params.limiterReleaseMs = 5;
    changed.setParams(params);
    double difference = 0;
    for (int n = 0; n < 2000; ++n) {
        difference = std::max(difference, std::abs(double(dcFrame(changed)) - dcFrame(reference)));
    }
    CHECK(difference < 1e-7, "changing limiter release on an unlimited signal does not change its level");
}

static void rapidChangesConvergeWithoutRestartingTheRamp() {
    DSPChain chain;
    chain.prepare(48000);
    ChainParams params;
    params.preampDb = -12;
    chain.setParams(params);
    chain.reset();
    for (int n = 0; n < 2000; ++n) dcFrame(chain);
    double previous = dcFrame(chain), largestStep = 0;
    for (int n = 0; n < 5000; ++n) {
        if (n < 3000 && n % 17 == 0) {
            params.preampDb = n % 34 ? -6 : 6;
            chain.setParams(params);
        }
        if (n == 3000) {
            params.preampDb = -3;
            chain.setParams(params);
        }
        const double sample = dcFrame(chain);
        largestStep = std::max(largestStep, std::abs(sample - previous));
        previous = sample;
    }
    std::printf("rapid retarget max_step=%.9f\n", largestStep);
    CHECK(largestStep < 0.002, "retargeting during a transition cannot jump the current gain");
    CHECK(std::abs(previous - 0.25 * dbToLin(-3)) < 1e-6,
          "latest queued request settles within two transition windows plus limiter latency");
}

static void disabledStagesCannotReplayOldAudio() {
    for (int stage = 0; stage < 4; ++stage) {
        DSPChain chain;
        chain.prepare(48000);
        ChainParams active;
        if (stage == 0) active.eqGainsDb[0] = 12;
        if (stage == 1) active.parametric[0] = {true, 0, 31, 12, 12};
        if (stage == 2) active.highpassHz = 40;
        if (stage == 3) { active.crossfeed = 100; active.spatialMode = 2; }
        chain.setParams(active);
        chain.reset();
        for (int n = 0; n < 10000; ++n) {
            const float sample = 0.05f * std::cos(2 * M_PI * 31 * n / 48000);
            float frame[2] = {sample, -sample};
            chain.processInterleaved(frame, 1);
        }
        chain.setParams(ChainParams::flat());
        for (int n = 0; n < 48000; ++n) {
            float frame[2] = {};
            chain.processInterleaved(frame, 1);
        }
        chain.setParams(active);
        double peak = 0;
        for (int n = 0; n < 4800; ++n) {
            float frame[2] = {};
            chain.processInterleaved(frame, 1);
            for (float sample : frame) peak = std::max(peak, std::abs(double(sample)));
        }
        std::printf("re-enable silent stage=%d peak=%.9f\n", stage, peak);
        CHECK(peak < 1e-7, "enabling a stage after silence must not replay its frozen filter history");
    }
}

static void renderPartitionDoesNotChangeTransitions() {
    const std::size_t total = 16000;
    const std::vector<std::size_t> events = {0, 1009, 1111, 1700, 6003, 6031, 8000, 10111};
    auto render = [&](std::size_t chunk) {
        DSPChain chain;
        chain.prepare(48000);
        std::vector<float> output(total * 2);
        for (std::size_t f = 0; f < total; ++f) {
            output[f * 2] = 0.15f * std::sin(2 * M_PI * 123 * f / 48000);
            output[f * 2 + 1] = 0.2f * std::cos(2 * M_PI * 791 * f / 48000);
        }
        std::size_t event = 0;
        for (std::size_t at = 0; at < total;) {
            if (event < events.size() && at == events[event]) {
                ChainParams params;
                params.preampDb = event % 2 ? -6 : 3;
                params.eqGainsDb[event % 10] = event % 2 ? 12 : -12;
                params.parametric[0] = {true, int(event % 6), 80.0 + event * 550, 8, 3};
                params.highpassHz = event % 2 ? 60 : 0;
                params.spatialWidth = event % 2 ? -80 : 80;
                params.crossfeed = 50;
                params.spatialMode = event % 4;
                params.compAmount = 30;
                chain.setParams(params);
                chain.setBypass(event == 3 || event == 4);
                ++event;
            }
            const auto next = event < events.size() ? events[event] : total;
            const auto count = std::min(chunk, next - at);
            chain.processInterleaved(output.data() + at * 2, count);
            at += count;
        }
        CHECK(!chain.safeBypassed(), "valid rapid changes do not trigger the emergency bypass");
        return output;
    };
    const auto singles = render(1);
    for (std::size_t chunk : {64u, 257u, 2048u}) {
        const auto blocked = render(chunk);
        CHECK(singles == blocked, "audio is identical across render block sizes and event boundaries");
        CHECK(std::all_of(blocked.begin(), blocked.end(), [](float x) {
            return std::isfinite(x) && std::abs(x) <= 1.0f;
        }), "simultaneous EQ, spatial and bypass changes stay finite and peak-limited");
    }
}

static void filterRetuningPreservesTheBoundaryAndReachesTheNewResponse() {
    for (double fs : {44100.0, 192000.0}) {
        for (int type = 0; type < 6; ++type) {
            DSPChain changed, oldReference, newReference;
            ChainParams oldParams, newParams;
            oldParams.parametric[0] = {true, 0, 600, 6, 3};
            newParams.parametric[0] = {true, type, 800, -12, 6};
            newParams.eqGainsDb[5] = 3;
            changed.prepare(fs);
            oldReference.prepare(fs);
            newReference.prepare(fs);
            changed.setParams(oldParams); changed.reset();
            oldReference.setParams(oldParams); oldReference.reset();
            newReference.setParams(newParams); newReference.reset();
            const int switchAt = static_cast<int>(fs * 0.2);
            const int lookahead = static_cast<int>(std::lround(fs * 0.002));
            double boundaryError = 0, settledError = 0;
            for (int n = 0; n < static_cast<int>(fs * 0.4); ++n) {
                const float sample = 0.01f * std::sin(2 * M_PI * 151 * n / fs)
                                   + 0.006f * std::cos(2 * M_PI * 811 * n / fs);
                float actual[2] = {sample, -sample};
                float oldExpected[2] = {sample, -sample};
                float newExpected[2] = {sample, -sample};
                if (n == switchAt) changed.setParams(newParams);
                changed.processInterleaved(actual, 1);
                oldReference.processInterleaved(oldExpected, 1);
                newReference.processInterleaved(newExpected, 1);
                if (n <= switchAt + lookahead)
                    boundaryError = std::max(boundaryError, std::abs(double(actual[0]) - oldExpected[0]));
                if (n > switchAt + fs * 0.1)
                    settledError = std::max(settledError, std::abs(double(actual[0]) - newExpected[0]));
            }
            CHECK(boundaryError < 1e-7, "filter retuning starts from the unchanged outgoing waveform");
            CHECK(settledError < 2e-6, "all six filter types converge to the independently rendered new response");
            CHECK(!changed.safeBypassed(), "filter type changes remain in normal processing");
        }
    }
}

int main() {
    gainChangesStayBetweenEndpoints();
    limiterReleaseDoesNotDipTheSignal();
    rapidChangesConvergeWithoutRestartingTheRamp();
    disabledStagesCannotReplayOldAudio();
    renderPartitionDoesNotChangeTransitions();
    filterRetuningPreservesTheBoundaryAndReachesTheNewResponse();
    if (failures) std::fprintf(stderr, "%d transition checks failed\n", failures);
    else std::puts("all DSP transition tests passed");
    return failures ? 1 : 0;
}
