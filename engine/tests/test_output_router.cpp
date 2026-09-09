/*
 * test_output_router.cpp — the order a render output is put back together. The
 * policy had tests; the sequence around it did not, so a reopen that closed the
 * device but forgot the watcher, the volume target or the lifecycle would have
 * gone unnoticed until a real device did it.
 */
#include "OutputRouter.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

namespace {
struct Recorder {
    std::vector<std::string> calls;
    int openError = 0, startError = 0;
    uint32_t openedDevice = 0;
    bool matchedRingRate = false;
    std::string reason;

    OutputRouter::Operations operations() {
        return {
            .lost = [this](const char* why) { reason = why; calls.push_back("lost"); },
            .stop = [this] { calls.push_back("stop"); },
            .unwatchRate = [this] { calls.push_back("unwatch"); },
            .close = [this] { calls.push_back("close"); },
            .open = [this](uint32_t device, bool matchRingRate) {
                openedDevice = device;
                matchedRingRate = matchRingRate;
                calls.push_back("open");
                return openError;
            },
            .start = [this] { calls.push_back("start"); return startError; },
            .failed = [this](int) { calls.push_back("failed"); },
            .restored = [this](uint32_t) { calls.push_back("restored"); },
        };
    }
};
}  // namespace

static void testNothingHappensWithoutARing() {
    Recorder recorder;
    OutputRouter router(OutputRouter::Clock::now());
    const auto result = router.reopen({7, 0, false, 0, 0}, 48000, /*ringValid*/ false,
                                      OutputRouter::Clock::now(), false, recorder.operations());
    CHECK(result == OutputRouter::Result::Idle, "no ring, no reopen");
    CHECK(recorder.calls.empty(), "and no device is touched");
}

static void testAMoveTearsDownBeforeItOpens() {
    Recorder recorder;
    OutputRouter router(OutputRouter::Clock::now());
    const auto result = router.reopen({9, 4, true, 48000, 48000}, 48000, true,
                                      OutputRouter::Clock::now(), false, recorder.operations());
    CHECK(result == OutputRouter::Result::Reopened, "a different target reopens");
    const std::vector<std::string> expected{"lost", "stop", "unwatch", "close", "open", "start", "restored"};
    CHECK(recorder.calls == expected, "teardown, open, then everything is put back");
    CHECK(recorder.openedDevice == 9, "it opens the device the policy picked");
    CHECK(recorder.reason == "target device changed", "and says why");
}

static void testAFailedOpenNeverRestoresAndRetriesLater() {
    Recorder recorder;
    recorder.openError = -1;
    OutputRouter router(OutputRouter::Clock::now());
    const auto now = OutputRouter::Clock::now();
    const auto result = router.reopen({9, 4, true, 48000, 48000}, 48000, true, now, false, recorder.operations());
    CHECK(result == OutputRouter::Result::Failed, "a failed open is reported as failed");
    const std::vector<std::string> expected{"lost", "stop", "unwatch", "close", "open", "failed"};
    CHECK(recorder.calls == expected, "start and restore are skipped");
    CHECK(!router.retryDue(now), "the retry is not immediate");
    CHECK(router.retryDue(now + std::chrono::seconds(5)), "but it is scheduled");
}

static void testAFailedStartIsAlsoAFailure() {
    Recorder recorder;
    recorder.startError = -2;
    OutputRouter router(OutputRouter::Clock::now());
    const auto result = router.reopen({9, 4, true, 48000, 48000}, 48000, true,
                                      OutputRouter::Clock::now(), false, recorder.operations());
    CHECK(result == OutputRouter::Result::Failed, "opening but not starting is a failure");
    const std::vector<std::string> expected{"lost", "stop", "unwatch", "close", "open", "start", "failed"};
    CHECK(recorder.calls == expected, "the device is closed again, nothing is restored");
}

static void testForcingAReopenKeepsTheSameDevice() {
    Recorder recorder;
    OutputRouter router(OutputRouter::Clock::now());
    // Same device, same rate: only a forced reopen (render runaway) moves.
    const auto now = OutputRouter::Clock::now();
    CHECK(router.reopen({4, 4, true, 48000, 48000}, 48000, true, now, false, recorder.operations())
          == OutputRouter::Result::Idle, "a healthy output is left alone");
    CHECK(recorder.calls.empty(), "and nothing is touched");
    const auto forced = router.reopen({4, 4, true, 48000, 48000}, 48000, true, now, true, recorder.operations());
    CHECK(forced == OutputRouter::Result::Reopened, "a runaway rebuilds the same device");
    CHECK(recorder.openedDevice == 4, "on the device it was already using");
    CHECK(recorder.reason == "render runaway", "and says so");
}

static void testAnAdoptedOutputClearsTheRetryTimer() {
    Recorder recorder;
    recorder.openError = -1;
    OutputRouter router(OutputRouter::Clock::now());
    const auto now = OutputRouter::Clock::now();
    router.reopen({9, 4, true, 48000, 48000}, 48000, true, now, false, recorder.operations());
    CHECK(router.retryDue(now + std::chrono::seconds(5)), "a retry is pending");
    router.adopt(9, now);
    CHECK(!router.retryDue(now + std::chrono::seconds(5)), "the driver's own handoff cancels it");
}

int main() {
    testNothingHappensWithoutARing();
    testAMoveTearsDownBeforeItOpens();
    testAFailedOpenNeverRestoresAndRetriesLater();
    testAFailedStartIsAlsoAFailure();
    testForcingAReopenKeepsTheSameDevice();
    testAnAdoptedOutputClearsTheRetryTimer();
    if (failures == 0) {
        std::printf("all output router tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d output router check(s) failed\n", failures);
    return 1;
}
