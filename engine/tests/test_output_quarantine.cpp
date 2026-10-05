/*
 * test_output_quarantine.cpp — outputs that will not start are skipped instead
 * of retried in place. The 2026-10-05 case: a nameless DisplayPort sink, pinned
 * by the listener, blocked every start for 10 s and the engine kept going back.
 */
#include "OutputQuarantine.hpp"

#include <cstdio>

using namespace roomcut;
static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); ++failures; } } while (0)

namespace {
using std::chrono::milliseconds;
using std::chrono::seconds;

void hungStartBenchesAtOnce() {
    OutputQuarantine q;
    const auto verdict = q.failed("dp:sink", seconds(10));
    CHECK(verdict.benched && verdict.hung, "a 10 s failed start benches on the first try");
    CHECK(q.benched("dp:sink"), "the hung device stays benched");
    CHECK(!q.benched("usb:dac"), "other devices are unaffected");
}

void quickFailuresGetTheNormalRetries() {
    OutputQuarantine q;
    CHECK(!q.failed("usb:dac", milliseconds(40)).benched, "first quick failure retries");
    CHECK(!q.failed("usb:dac", milliseconds(40)).benched, "second quick failure retries");
    const auto third = q.failed("usb:dac", milliseconds(40));
    CHECK(third.benched && !third.hung, "third quick failure benches without counting as hung");
}

void successClearsTheCount() {
    OutputQuarantine q;
    q.failed("usb:dac", milliseconds(40));
    q.failed("usb:dac", milliseconds(40));
    q.succeeded("usb:dac");
    CHECK(!q.failed("usb:dac", milliseconds(40)).benched, "a success starts the count over");
}

void slowButBelowTheHangIsQuick() {
    OutputQuarantine q;
    const auto verdict = q.failed("bt:airpods", milliseconds(1500));
    CHECK(!verdict.benched && !verdict.hung, "a slow Bluetooth failure is still retried");
}

void repickAndReplugRelease() {
    OutputQuarantine q;
    q.failed("dp:sink", seconds(10));
    q.release("dp:sink");
    CHECK(!q.benched("dp:sink"), "picking the device again releases it");

    q.failed("dp:sink", seconds(10));
    q.retain({"dp:sink", "usb:dac"});
    CHECK(q.benched("dp:sink"), "still present: stays benched");
    q.retain({"usb:dac"});
    q.retain({"dp:sink", "usb:dac"});
    CHECK(!q.benched("dp:sink"), "gone and back: starts clean");
}

void aFailedCheckBenchesAtOnce() {
    OutputQuarantine q;
    q.bench("dp:sink");
    CHECK(q.benched("dp:sink") && !q.verified("dp:sink"), "a check that could not start the device benches it");
    const auto names = q.benchedUIDs();
    CHECK(names.size() == 1 && names[0] == "dp:sink", "and names it among the unusable outputs");
}

void aDeviceThatRanIsVerifiedUntilItFails() {
    OutputQuarantine q;
    q.succeeded("usb:dac");
    CHECK(q.verified("usb:dac") && !q.benched("usb:dac"), "a device that ran needs no check before routing");
    q.failed("usb:dac", milliseconds(40));
    CHECK(!q.verified("usb:dac"), "a later failure takes that back");
    q.succeeded("usb:dac");
    q.retain({});
    CHECK(!q.verified("usb:dac"), "unplugged and back: it is checked again");
}

void emptyUIDIsIgnored() {
    OutputQuarantine q;
    CHECK(!q.failed("", seconds(10)).benched, "a vanished device has no UID to bench");
}
} // namespace

int main() {
    hungStartBenchesAtOnce();
    quickFailuresGetTheNormalRetries();
    successClearsTheCount();
    slowButBelowTheHangIsQuick();
    repickAndReplugRelease();
    aFailedCheckBenchesAtOnce();
    aDeviceThatRanIsVerifiedUntilItFails();
    emptyUIDIsIgnored();
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::printf("test_output_quarantine: all passed\n");
    return 0;
}
