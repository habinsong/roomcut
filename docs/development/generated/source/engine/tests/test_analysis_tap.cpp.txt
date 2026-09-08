#include "AnalysisTap.hpp"
#include "AnalysisWorker.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static std::vector<float> ramp(uint64_t first, uint32_t count) {
    std::vector<float> samples((size_t)count * 2);
    for (uint32_t f = 0; f < count; ++f) {
        samples[f * 2] = (float)(first + f);
        samples[f * 2 + 1] = -samples[f * 2];
    }
    return samples;
}

static bool coherent(const AnalysisWindow& window) {
    if (window.endFrame < AnalysisWindow::kFrames) return false;
    const uint64_t first = window.endFrame - AnalysisWindow::kFrames;
    for (uint32_t f = 0; f < AnalysisWindow::kFrames; ++f) {
        if (window.samples[f * 2] != (float)(first + f)
            || window.samples[f * 2 + 1] != -(float)(first + f)) return false;
    }
    return true;
}

static void testPartialAndMultipleWindows() {
    AnalysisTap tap;
    tap.prepare(48000);
    AnalysisWindow window;
    auto input = ramp(0, 8193);
    tap.push(input.data(), 1023);
    CHECK(!tap.readLatest(window), "a partial window is never exposed");
    tap.push(input.data() + 1023 * 2, 1025);
    CHECK(tap.readLatest(window) && coherent(window), "split callbacks produce one intact window");
    CHECK(window.sampleRate == 48000 && window.endFrame == 2048, "format accompanies its samples");
    tap.push(input.data() + 2048 * 2, 6145);
    CHECK(tap.readLatest(window) && window.endFrame == 8192 && coherent(window),
          "a slow consumer receives the most recent complete window");
    tap.prepare(96000);
    input = ramp(0, 2048);
    tap.push(input.data(), 2048);
    CHECK(tap.readLatest(window) && coherent(window), "format reset drops the partial old window");
    CHECK(window.sampleRate == 96000 && window.generation == tap.generation(), "generation travels with data");
}

static void testConcurrentWrapAndFormatChanges() {
    AnalysisTap tap;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        uint64_t frame = 0;
        for (int block = 0; block < 32000; ++block) {
            if (block % 1000 == 0) {
                tap.prepare(block % 2000 == 0 ? 48000 : 96000);
                frame = 0;
            }
            auto samples = ramp(frame, 257);
            tap.push(samples.data(), 257);
            frame += 257;
        }
        done.store(true, std::memory_order_release);
    });
    AnalysisWindow window;
    bool valid = true;
    uint64_t generation = 0;
    uint32_t reads = 0;
    do {
        if (tap.readLatest(window)) {
            valid &= coherent(window) && window.generation >= generation;
            valid &= window.sampleRate == (window.generation % 2 ? 48000u : 96000u);
            generation = window.generation;
            ++reads;
            std::this_thread::yield();
        }
    } while (!done.load(std::memory_order_acquire));
    producer.join();
    if (tap.readLatest(window)) valid &= coherent(window);
    CHECK(valid && reads > 0, "concurrent overwrites cannot tear samples or their format");
    CHECK(window.generation == tap.generation(), "the final generation remains readable");
}

static void testWorkerDoesNotSmoothAcrossFormats() {
    AnalysisWorker worker;
    worker.start();
    for (const auto [rate, amplitude] : {std::pair{48000u, 0.1f}, {96000u, 0.5f},
                                         {96000u, 0.25f}, {384000u, 0.7f}}) {
        worker.prepare(rate);
        CHECK(!worker.snapshot().valid, "a format reset immediately hides old analysis");
        std::vector<float> input(AnalysisWindow::kFrames * 2, amplitude);
        worker.push(input.data(), AnalysisWindow::kFrames);
        AnalysisSnapshot snapshot;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            snapshot = worker.snapshot();
            if (snapshot.valid) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(snapshot.valid && snapshot.sampleRate == rate, "worker publishes the new format");
        CHECK(std::abs(snapshot.rmsDb - 20 * std::log10(amplitude)) < 0.01,
              "new generations do not inherit the old level's smoothing");
    }
    const auto start = std::chrono::steady_clock::now();
    worker.stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250),
          "stopping wakes the worker instead of waiting a full analysis interval");
}

int main() {
    testPartialAndMultipleWindows();
    testConcurrentWrapAndFormatChanges();
    testWorkerDoesNotSmoothAcrossFormats();
    if (failures == 0) std::puts("all analysis handoff tests passed");
    return failures == 0 ? 0 : 1;
}
