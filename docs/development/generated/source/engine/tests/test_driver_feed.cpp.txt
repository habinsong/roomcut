// test_driver_feed.cpp — the driver-lost / driver-returned policy. Silence must
// not be mistaken for a dead driver, and a driver that comes back has to be
// announced exactly once.
#include "DriverFeedWatchdog.hpp"
#include <cstdio>

using namespace roomcut;
using Clock = DriverFeedWatchdog::Clock;
using Event = DriverFeedWatchdog::Event;

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

static void testSilenceWithHeartbeatIsNotALostDriver() {
    const auto start = Clock::now();
    DriverFeedWatchdog watchdog(start);
    // Nothing is playing for a minute; the driver keeps beating once a second.
    for (int second = 1; second <= 60; ++second) {
        const auto now = start + std::chrono::seconds(second);
        watchdog.heartbeat(now);
        CHECK(watchdog.observe(0, now) == Event::None, "a beating driver is never lost");
    }
    CHECK(!watchdog.stalled(), "silence alone does not stall the driver");
}

static void testFrozenFeedAndQuietHeartbeatIsLostOnce() {
    const auto start = Clock::now();
    DriverFeedWatchdog watchdog(start);
    watchdog.heartbeat(start);
    CHECK(watchdog.observe(128, start) == Event::None, "first advance is not an event");
    // The feed freezes. The beat has to go quiet as well before we call it lost.
    CHECK(watchdog.observe(128, start + std::chrono::seconds(3)) == Event::None,
          "a frozen feed with a recent beat is still just silence");
    const auto lost = start + std::chrono::milliseconds(3600);
    CHECK(watchdog.observe(128, lost) == Event::Lost, "frozen feed and quiet beat is a lost driver");
    CHECK(watchdog.stalled(), "the watchdog holds the stalled state");
    CHECK(watchdog.observe(128, lost + std::chrono::seconds(10)) == Event::None,
          "a driver that is already lost is not reported again");
}

static void testReturnIsReportedOnceWhenTheFeedAdvances() {
    const auto start = Clock::now();
    DriverFeedWatchdog watchdog(start);
    watchdog.heartbeat(start);
    watchdog.observe(1, start);
    const auto lost = start + std::chrono::seconds(4);
    CHECK(watchdog.observe(1, lost) == Event::Lost, "feed froze and the beat stopped");
    const auto back = lost + std::chrono::seconds(1);
    CHECK(watchdog.observe(2, back) == Event::Returned, "an advancing feed announces the return");
    CHECK(!watchdog.stalled(), "the return clears the stalled state");
    CHECK(watchdog.observe(3, back + std::chrono::seconds(1)) == Event::None,
          "the return is announced once, not on every later block");
}

static void testHeartbeatAloneDoesNotUnstallAFrozenFeed() {
    const auto start = Clock::now();
    DriverFeedWatchdog watchdog(start);
    watchdog.observe(7, start);
    const auto lost = start + std::chrono::seconds(5);
    CHECK(watchdog.observe(7, lost) == Event::Lost, "feed froze and the beat stopped");
    // A driver that beats but never writes is still not feeding audio.
    watchdog.heartbeat(lost + std::chrono::seconds(1));
    CHECK(watchdog.observe(7, lost + std::chrono::seconds(2)) == Event::None,
          "a heartbeat without audio does not clear the stall");
    CHECK(watchdog.stalled(), "only an advancing write index ends the stall");
}

static void testANewStallCanBeReportedAfterARecovery() {
    const auto start = Clock::now();
    DriverFeedWatchdog watchdog(start);
    watchdog.heartbeat(start);
    watchdog.observe(1, start);
    CHECK(watchdog.observe(1, start + std::chrono::seconds(4)) == Event::Lost, "first stall");
    const auto back = start + std::chrono::seconds(5);
    watchdog.heartbeat(back);
    CHECK(watchdog.observe(2, back) == Event::Returned, "recovered");
    CHECK(watchdog.observe(2, back + std::chrono::seconds(4)) == Event::Lost,
          "a second stall after a recovery is reported again");
}

int main() {
    testSilenceWithHeartbeatIsNotALostDriver();
    testFrozenFeedAndQuietHeartbeatIsLostOnce();
    testReturnIsReportedOnceWhenTheFeedAdvances();
    testHeartbeatAloneDoesNotUnstallAFrozenFeed();
    testANewStallCanBeReportedAfterARecovery();
    if (failures == 0) {
        std::printf("all driver feed tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d driver feed check(s) failed\n", failures);
    return 1;
}
