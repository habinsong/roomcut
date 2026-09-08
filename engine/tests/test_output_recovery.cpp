#include "OutputRecovery.hpp"
#include "RenderProgressWatchdog.hpp"
#include <cstdio>
#include <limits>

using namespace roomcut;
using namespace std::chrono;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)
static auto at(int milliseconds) { return OutputRecovery::Clock::time_point{} + std::chrono::milliseconds(milliseconds); }

static void recoveryPolicy() {
    OutputRecovery policy;
    OutputRecovery::Snapshot output{42, 48000, 42, false, 48000, 48000};
    CHECK(policy.decide(output, at(0)).reopen, "configured rate alone does not make a stopped output healthy");
    policy.completed(at(200));
    for (int ms = 201; ms < 700; ++ms)
        CHECK(!policy.decide(output, at(ms)).reopen && !policy.retryDue(at(ms)), "busy control messages cannot shorten retry delay");
    CHECK(policy.retryDue(at(700)), "retry becomes due at the completion-based deadline");
    const auto second = policy.decide(output, at(700));
    CHECK(second.reopen && second.matchRingRate, "second attempt still requests ring rate");
    policy.completed(at(700));
    const auto third = policy.decide(output, at(1200));
    CHECK(third.reopen && !third.matchRingRate, "third failed attempt uses the device's native rate");
    CHECK(!policy.retryDue(at(3199)) && policy.retryDue(at(3200)), "third attempt uses a two-second delay");
    CHECK(policy.decide(output, at(3200)).reopen, "fourth attempt is available at deadline");
    CHECK(policy.decide(output, at(5200)).reopen, "fifth attempt is available at deadline");
    CHECK(!policy.retryDue(at(10199)) && policy.retryDue(at(10200)), "persistent failure backs off to five seconds");

    output.targetDevice = 43;
    const auto replacement = policy.decide(output, at(5300));
    CHECK(replacement.reopen && replacement.matchRingRate, "replacement device starts fresh despite old device's delay");
    output.ringRate = 96000;
    const auto formatChange = policy.decide(output, at(5301));
    CHECK(formatChange.reopen && formatChange.matchRingRate, "explicit ring format change starts a fresh attempt");

    output.openedDevice = output.targetDevice; output.running = true;
    output.hardwareRate = output.configuredRate = 96000;
    CHECK(!policy.decide(output, at(5400)).reopen, "stable running output is preserved");
    CHECK(policy.retryDue(at(5900)), "stability is confirmed after an initial healthy observation");
    CHECK(!policy.decide(output, at(5900)).reopen, "stability polling never tears down a healthy unit");
    CHECK(!policy.decide(output, at(7400)).reopen && !policy.retryDue(at(9999)), "two stable seconds clear retry work");
    CHECK(policy.decide(output, at(7500), true).reopen, "runaway detector may force a repair of matching formats");
    output.hardwareRate = std::numeric_limits<double>::quiet_NaN();
    CHECK(policy.decide(output, at(8000)).reopen, "invalid hardware rate is not treated as stable");

    output.targetDevice = 0;
    CHECK(!policy.decide(output, at(8001)).reopen && policy.retryDue(at(8501)), "missing target waits without touching current output");
    output.ringRate = 0;
    CHECK(!policy.decide(output, at(8502)).reopen && !policy.retryDue(at(20000)), "no negotiated input cancels recovery work");
}

static void progressWatchdog() {
    RenderProgressWatchdog watchdog(at(0));
    CHECK(!watchdog.observe(100, 48000, at(0)).requested, "watchdog starts with a sample");
    CHECK(!watchdog.observe(400000, 48000, at(4999)).requested, "startup grace suppresses mixed-rate measurements");
    CHECK(!watchdog.observe(496000, 48000, at(5999)).requested, "one fast interval does not rebuild output");
    const auto repair = watchdog.observe(592000, 48000, at(6999));
    CHECK(repair.requested && repair.attempt == 1 && repair.framesPerSecond == 96000, "two fast intervals request one repair");

    uint64_t frames = 592000;
    int time = 6999;
    // Assign the initial device before exercising the per-device retry budget.
    watchdog.opened(42, at(time));
    unsigned expected = 1;
    for (int cycle = 0; cycle < 4; ++cycle) {
        watchdog.opened(42, at(time));
        CHECK(!watchdog.observe(frames, 48000, at(time + 3000)).requested, "reopen starts a new measurement window");
        frames += 96000;
        CHECK(!watchdog.observe(frames, 48000, at(time + 4000)).requested, "first fast interval after reopen is only a warning");
        frames += 96000;
        const auto next = watchdog.observe(frames, 48000, at(time + 5000));
        CHECK(next.requested == (cycle < 3), "repeated repairs stop after three unsuccessful attempts");
        if (next.requested) CHECK(next.attempt == expected++, "repair attempt count is preserved across reopen");
        time += 5000;
    }
    watchdog.opened(43, at(time));
    watchdog.observe(frames, 48000, at(time + 3000));
    frames += 96000; watchdog.observe(frames, 48000, at(time + 4000));
    frames += 96000;
    CHECK(watchdog.observe(frames, 48000, at(time + 5000)).attempt == 1, "new device gets its own repair budget");
    frames += 72000;
    CHECK(!watchdog.observe(frames, 48000, at(time + 6000)).requested, "exact 1.5x threshold remains non-runaway");
    CHECK(!watchdog.observe(0, 48000, at(time + 7000)).requested, "counter reset cannot underflow into a false runaway");

    RenderProgressWatchdog interrupted(at(0));
    interrupted.opened(42, at(0));
    interrupted.observe(100, 48000, at(3000));
    interrupted.observe(96100, 48000, at(4000));
    interrupted.observe(192100, std::numeric_limits<double>::quiet_NaN(), at(5000));
    interrupted.observe(288100, 48000, at(6000));
    CHECK(!interrupted.observe(384100, 48000, at(7000)).requested, "invalid rate breaks the consecutive interval streak");
}

int main() {
    recoveryPolicy(); progressWatchdog();
    if (!failures) std::puts("all output recovery policy and watchdog tests passed");
    return failures ? 1 : 0;
}
