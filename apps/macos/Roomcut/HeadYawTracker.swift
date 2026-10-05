import Foundation

// The arithmetic behind head tracking, with no sensor attached: the headphones'
// attitude goes in, the angle the engine should render comes out.
//
// It is a separate type because the hard parts are not the sensor — they are
// what "straight ahead" means and what counts as turning. Each was wrong once,
// and none could be tested while this logic lived inside a CoreMotion callback.
//
// What it has to get right, each measured on AirPods Pro (13 minutes of
// 50 Hz samples, ~/Library/Logs/Roomcut/head-tracking.csv, 2026-09-17):
//
//   - Turning is rotation about the vertical, and nothing else. The angle used
//     to be the Z-Y-X Euler yaw of the earbud, whose axes sit about 18 degrees
//     rolled and 9 degrees pitched against the head. Nodding then leaked into
//     the yaw: its difference from Core Motion's own yaw moved between -1.3
//     and -4.2 degrees with posture alone. The angle is now the twist about the
//     vertical of the rotation from the reference attitude to the current one,
//     which a nod or a tilt about any horizontal axis leaves at exactly zero.
//   - Forward is where the listener looked once the head settled. Re-centring
//     is the listener's call (the button) and happens on every restart of the
//     stream.
//   - The sensor's heading is not trustworthy over minutes: AirPods have no
//     compass, and every small movement leaves a little error behind, almost
//     always the same way. Measured 2026-10-06 (head-tracking-v3.csv, macOS 27):
//     a listener facing the screen read -12.7 to -18.7 degrees within 50 s
//     (about -7 a minute) while the head only made small adjustments; in every
//     long stream that day the resting direction walked negative, the stage
//     turning clockwise. Fully still, the drift was near zero. So while the head
//     rests near forward, forward is drawn slowly back to it — never past
//     forwardConeDegrees, faintly at its edge, and not at all while the head
//     moves — which holds the measured drift to about 2 degrees. (An earlier
//     version adopted any still pose at a fixed rate, with no cone, and was
//     removed when a pose 5 degrees right crept in over 12 minutes; the
//     measured drift is far larger than that creep, and the cone keeps a
//     deliberate look to the side where it is.)
//   - The first samples arrive while the earbuds are going in (64 degrees of
//     roll in the log). Forward is taken only after the angle has held steady
//     for a moment; until then the angle reads zero.
//   - A turn can go all the way round. Angles are wrapped, and the smoother
//     always takes the short way, so passing behind the listener does not sweep
//     the stage the long way through the front.
//
// Every constant below is per second, not per sample, so the behaviour does not
// change with the sensor's rate (AirPods deliver about 50 Hz).
public struct HeadYawTracker {
    // How quickly the reported angle follows the sensor. A light smoother: head
    // rotation is slow next to audio, so this only removes sensor jitter.
    public static let smoothingSeconds = 0.045
    // Forward is taken once the angle has moved less than this...
    public static let settledDegreesPerSecond = 20.0
    // ...for this long.
    public static let settledSeconds = 0.3
    // Drift: how fast forward is drawn back to a head resting near it...
    public static let driftSeconds = 20.0
    // ...only within this far of forward, more faintly towards the edge...
    public static let forwardConeDegrees = 20.0
    // ...and only while the head turns slower than this (averaged over
    // speedSeconds), so a turn itself is never taken for drift.
    public static let restingDegreesPerSecond = 8.0
    public static let speedSeconds = 0.25

    public struct Quaternion: Equatable, Sendable {
        public var w, x, y, z: Double
        public init(w: Double, x: Double, y: Double, z: Double) { self.w = w; self.x = x; self.y = y; self.z = z }
        var conjugate: Quaternion { Quaternion(w: w, x: -x, y: -y, z: -z) }
        static func * (a: Quaternion, b: Quaternion) -> Quaternion {
            Quaternion(w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                       x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                       y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w)
        }
    }

    private var reference: Double?
    private var referenceAttitude: Quaternion?
    private var smoothed: Double = 0
    private var lastTime: TimeInterval?
    private var candidate: (angle: Double, since: TimeInterval)?
    // Degrees taken off the sensor's heading for its drift (see above).
    private var correction: Double = 0
    private var speed: Double = 0
    private var lastTarget: Double?

    public init() {}

    public var yawDegrees: Double { smoothed }

    // "Straight ahead is where I am looking now." Takes effect on the next sample.
    public mutating func recentre() {
        reference = nil
        referenceAttitude = nil
        smoothed = 0
        candidate = nil
        correction = 0
        speed = 0
        lastTarget = nil
    }

    public mutating func reset() {
        recentre()
        lastTime = nil
    }

    // Twist about the vertical (+Z of Core Motion's reference frame) of the
    // rotation from `reference` to `attitude`, in the engine's convention:
    // positive when the listener turned to the right. Core Motion's angles run
    // counter-clockwise seen from above, hence the sign.
    public static func turnDegrees(from reference: Quaternion, to attitude: Quaternion) -> Double {
        let relative = attitude * reference.conjugate
        let angle = 2 * atan2(relative.z, relative.w) * 180 / .pi
        return -wrap(angle)
    }

    // One attitude sample in, the angle to send to the engine out. `time` is the
    // sample's own timestamp, so a dropped or delayed batch cannot change the
    // result.
    public mutating func update(attitude: Quaternion, at time: TimeInterval) -> Double {
        guard attitude.w.isFinite, attitude.x.isFinite, attitude.y.isFinite, attitude.z.isFinite else { return smoothed }
        if let base = referenceAttitude {
            return advance(relative: Self.turnDegrees(from: base, to: attitude), at: time)
        }
        // Not settled yet: measure steadiness on the absolute twist, which is
        // only ever compared with itself a moment later.
        let absolute = Self.turnDegrees(from: Quaternion(w: 1, x: 0, y: 0, z: 0), to: attitude)
        if settle(absolute, at: time) {
            referenceAttitude = attitude
            reference = 0
        }
        lastTime = time
        return smoothed
    }

    // The same from a bare angle (degrees, engine convention), for callers and
    // tests that already have one.
    public mutating func update(rawDegrees raw: Double, at time: TimeInterval) -> Double {
        guard raw.isFinite, time.isFinite else { return smoothed }
        if let base = reference {
            return advance(relative: Self.wrap(raw - base), at: time)
        }
        if settle(raw, at: time) { reference = raw }
        lastTime = time
        return smoothed
    }

    private mutating func settle(_ angle: Double, at time: TimeInterval) -> Bool {
        guard let held = candidate else {
            candidate = (angle, time)
            return false
        }
        let elapsed = time - held.since
        if abs(Self.wrap(angle - held.angle)) > Self.settledDegreesPerSecond * max(elapsed, 1.0 / 60) {
            candidate = (angle, time)
            return false
        }
        return elapsed >= Self.settledSeconds
    }

    private mutating func advance(relative: Double, at time: TimeInterval) -> Double {
        // A gap longer than this means the stream stalled; treat it as one
        // ordinary step rather than letting a huge dt jump the smoother.
        let dt = lastTime.map { max(0, min(0.25, time - $0)) } ?? 0
        lastTime = time
        var target = Self.wrap(relative - correction)
        if let previous = lastTarget, dt > 0 {
            let instant = abs(Self.wrap(target - previous)) / dt
            speed += (1 - exp(-dt / Self.speedSeconds)) * (instant - speed)
        }
        if dt > 0, speed < Self.restingDegreesPerSecond, abs(target) < Self.forwardConeDegrees {
            let weight = 1 - abs(target) / Self.forwardConeDegrees
            let pull = target * (1 - exp(-dt * weight / Self.driftSeconds))
            correction = Self.wrap(correction + pull)
            target -= pull
        }
        lastTarget = target
        // Always the short way round, so crossing the back of the head does not
        // drag the stage through the front.
        let step = dt > 0 ? 1 - exp(-dt / Self.smoothingSeconds) : 1
        smoothed = Self.wrap(smoothed + step * Self.wrap(target - smoothed))
        return smoothed
    }

    static func wrap(_ degrees: Double) -> Double {
        var value = degrees.truncatingRemainder(dividingBy: 360)
        if value > 180 { value -= 360 }
        if value < -180 { value += 360 }
        return value
    }
}
