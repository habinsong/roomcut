import XCTest
@testable import RoomcutCore

// What "straight ahead" means while the listener moves. These are written
// against a measured failure: holding a 40 degree turn used to collapse to
// 10 degrees after one second and to nothing after four, because the drift
// correction treated any steady pose as a new forward. Coming back to the
// screen then reported -2.9 degrees instead of zero, and -6.5 after a
// 90 degree turn — the further you looked, the further the origin walked.
final class HeadYawTrackerTests: XCTestCase {
    private let rate = 50.0          // AirPods deliver about 50 Hz (measured)

    // Feeds a motion profile and returns the last reported angle.
    @discardableResult
    private func play(_ tracker: inout HeadYawTracker,
                      _ profile: [Double],
                      from start: TimeInterval = 0) -> Double {
        var last = 0.0
        for (index, raw) in profile.enumerated() {
            last = tracker.update(rawDegrees: raw, at: start + Double(index) / rate)
        }
        return last
    }

    private func ramp(_ from: Double, _ to: Double, seconds: Double) -> [Double] {
        let n = max(1, Int(seconds * rate))
        return (0..<n).map { from + (to - from) * Double($0) / Double(n) }
    }
    private func hold(_ value: Double, seconds: Double) -> [Double] {
        Array(repeating: value, count: max(1, Int(seconds * rate)))
    }

    func testAHeldTurnIsNotMistakenForANewForward() {
        var tracker = HeadYawTracker()
        var profile = hold(0, seconds: 1) + ramp(0, 40, seconds: 1.5)
        profile += hold(40, seconds: 10)
        let reported = play(&tracker, profile)
        // The old behaviour reported about 0 here. The head is still turned, so
        // the stage must still be rotated.
        XCTAssertEqual(reported, 40, accuracy: 1.0,
                       "a pose held for ten seconds is a held pose, not a new forward")
    }

    func testReturningToForwardReportsForward() {
        for turn in [40.0, 90.0, 150.0] {
            var tracker = HeadYawTracker()
            var profile = hold(0, seconds: 1) + ramp(0, turn, seconds: 1.5)
            profile += hold(turn, seconds: 6) + ramp(turn, 0, seconds: 1.5) + hold(0, seconds: 2)
            let reported = play(&tracker, profile)
            XCTAssertEqual(reported, 0, accuracy: 0.5,
                           "after a \(Int(turn)) degree turn, facing forward again means zero")
        }
    }

    func testRepeatedWideSwingsKeepTheOrigin() {
        // The reported complaint: swing left and right a lot, past the old
        // 60 degree limit, and the origin ends up somewhere else.
        var tracker = HeadYawTracker()
        var profile = hold(0, seconds: 1)
        for _ in 0..<8 {
            profile += ramp(0, -100, seconds: 0.8)
            profile += ramp(-100, 100, seconds: 1.6)
            profile += ramp(100, 0, seconds: 0.8)
        }
        profile += hold(0, seconds: 2)
        let reported = play(&tracker, profile)
        XCTAssertEqual(reported, 0, accuracy: 0.5,
                       "eight wide swings leave forward where it was")
    }

    func testATurnBeyondTheOldLimitIsReportedInFull() {
        var tracker = HeadYawTracker()
        let profile = hold(0, seconds: 1) + ramp(0, 120, seconds: 2) + hold(120, seconds: 1)
        let reported = play(&tracker, profile)
        XCTAssertEqual(reported, 120, accuracy: 1.0,
                       "a 120 degree turn is reported as 120, not clipped to 60")
    }

    func testAHeadRestingOffCentreDoesNotDragForward() {
        // The measured failure: a listener who rests slightly right of where
        // they started saw the stage creep 5 degrees to the right in 12 minutes,
        // because any still pose near forward was slowly adopted as forward.
        var tracker = HeadYawTracker()
        var profile = hold(0, seconds: 1) + ramp(0, 5, seconds: 0.5)
        for _ in 0..<72 {                                   // 12 minutes
            profile += hold(5, seconds: 9) + ramp(5, 12, seconds: 0.3) + ramp(12, 5, seconds: 0.7)
        }
        profile += hold(5, seconds: 1)
        let reported = play(&tracker, profile)
        XCTAssertEqual(reported, 5, accuracy: 0.3, "a pose near forward stays that far from forward")
        let back = play(&tracker, ramp(5, 0, seconds: 0.5) + hold(0, seconds: 1), from: Double(profile.count) / rate)
        XCTAssertEqual(back, 0, accuracy: 0.3, "and looking where forward was still reads zero")
    }

    func testForwardIsTakenOnceTheHeadHasSettled() {
        // The earbuds go in with the head moving; forward is where it comes to rest.
        var tracker = HeadYawTracker()
        var profile = ramp(-30, 20, seconds: 0.4) + ramp(20, -10, seconds: 0.3) + ramp(-10, 8, seconds: 0.3)
        profile += hold(8, seconds: 1)
        play(&tracker, profile)
        let turned = play(&tracker, ramp(8, 38, seconds: 1) + hold(38, seconds: 1), from: Double(profile.count) / rate)
        XCTAssertEqual(turned, 30, accuracy: 1.0, "forward is the settled 8 degrees, not the first sample")
    }

    // Attitudes as Core Motion reports them: turn about the vertical after a
    // tilt of the earbud against the head, which is how they sit in an ear.
    // The tilt matches the log (18 degrees of roll, 9 of pitch at rest); the
    // 25 degrees about the vertical is an assumption, the size at which the
    // Euler yaw's posture error matches the -1.3 to -4.2 degrees measured.
    private func attitude(turn: Double, nod: Double, mountRoll: Double = 18, mountPitch: Double = 9, mountYaw: Double = 25) -> HeadYawTracker.Quaternion {
        func axis(_ degrees: Double, _ x: Double, _ y: Double, _ z: Double) -> HeadYawTracker.Quaternion {
            let half = degrees * .pi / 360
            return .init(w: cos(half), x: x * sin(half), y: y * sin(half), z: z * sin(half))
        }
        // World frame, applied right to left: the earbud's own tilt, the nod
        // (about the head's ear-to-ear axis), then the turn about the vertical.
        // Core Motion counts a turn to the right as negative about +Z.
        let mount = axis(mountRoll, 1, 0, 0) * axis(mountPitch, 0, 1, 0) * axis(mountYaw, 0, 0, 1)
        return axis(-turn, 0, 0, 1) * axis(nod, 0, 1, 0) * mount
    }

    func testNoddingDoesNotTurnTheStage() {
        var tracker = HeadYawTracker()
        var time = 0.0
        func feed(turn: Double, nod: Double, seconds: Double) -> Double {
            var last = 0.0
            for _ in 0..<Int(seconds * rate) {
                last = tracker.update(attitude: attitude(turn: turn, nod: nod), at: time)
                time += 1 / rate
            }
            return last
        }
        _ = feed(turn: 0, nod: 0, seconds: 1)
        var worst = 0.0
        for nod in stride(from: -25.0, through: 25.0, by: 5.0) { worst = max(worst, abs(feed(turn: 0, nod: nod, seconds: 0.3))) }
        XCTAssertLessThan(worst, 0.05, "looking down at a keyboard or up again is not a turn")
        XCTAssertEqual(feed(turn: 40, nod: 15, seconds: 1), 40, accuracy: 0.5, "a turn while looking down is still the whole turn")

        // What the old Euler yaw read for the same nods, so this test can tell the two apart.
        func euler(_ q: HeadYawTracker.Quaternion) -> Double { -atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 180 / .pi }
        var eulerWorst = 0.0
        for nod in stride(from: -25.0, through: 25.0, by: 5.0) {
            eulerWorst = max(eulerWorst, abs(euler(attitude(turn: 0, nod: nod)) - euler(attitude(turn: 0, nod: 0))))
        }
        XCTAssertGreaterThan(eulerWorst, 2.0, "the Euler yaw of the same earbud moves with a nod")
    }

    func testCrossingTheBackTakesTheShortWay() {
        // Going past 180 must come round the other side, not sweep the stage
        // back through the front.
        var tracker = HeadYawTracker()
        var profile = hold(0, seconds: 1) + ramp(0, 170, seconds: 2)
        profile += hold(170, seconds: 0.5)
        play(&tracker, profile)
        var worstStep = 0.0
        var previous = tracker.yawDegrees
        var time = 3.5
        for raw in ramp(170, 200, seconds: 1) {
            let value = tracker.update(rawDegrees: raw, at: time)
            let step = abs(HeadYawTracker.wrap(value - previous))
            worstStep = max(worstStep, step)
            previous = value
            time += 1 / rate
        }
        XCTAssertLessThan(worstStep, 5.0, "passing behind the listener never jumps the stage")
        XCTAssertEqual(previous, -160, accuracy: 2.0, "200 degrees round is -160 degrees")
    }

    func testTheResultDoesNotDependOnTheSensorRate() {
        // Same movement, half the samples: the angle must land in the same place.
        func run(at hz: Double) -> Double {
            var tracker = HeadYawTracker()
            var last = 0.0
            var time = 0.0
            let profile: [(Double, Double)] = {
                var out: [(Double, Double)] = []
                var t = 0.0
                while t < 1.0 { out.append((0, t)); t += 1 / hz }
                while t < 3.0 { out.append(((t - 1) / 2 * 55, t)); t += 1 / hz }
                while t < 8.0 { out.append((55, t)); t += 1 / hz }
                return out
            }()
            for (raw, at) in profile { last = tracker.update(rawDegrees: raw, at: at); time = at }
            _ = time
            return last
        }
        XCTAssertEqual(run(at: 50), run(at: 25), accuracy: 0.5,
                       "25 Hz and 50 Hz agree, so the constants are per second")
    }

    func testRecentreMakesTheCurrentDirectionForward() {
        var tracker = HeadYawTracker()
        let profile = hold(0, seconds: 1) + ramp(0, 35, seconds: 1.5) + hold(35, seconds: 1)
        play(&tracker, profile)
        XCTAssertEqual(tracker.yawDegrees, 35, accuracy: 1.0)
        tracker.recentre()
        XCTAssertEqual(tracker.yawDegrees, 0, accuracy: 0.0001)
        let after = tracker.update(rawDegrees: 35, at: 100)
        XCTAssertEqual(after, 0, accuracy: 0.0001, "after re-centring, where you look is forward")
    }
}
