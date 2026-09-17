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
//   - Forward is where the listener looked once the head settled, and it stays
//     there. A correction that slowly adopted any still pose near forward as
//     the new forward (for a gyro drift that was assumed, not measured) moved
//     forward 5.0 degrees to the right over 12 minutes, because that listener
//     rested slightly right of where they started — the stage visibly crept
//     round. The sensor itself drifted -0.13 degrees a minute while the head was
//     still. Re-centring is the listener's call (the button) and happens on
//     every restart of the stream.
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

    public init() {}

    public var yawDegrees: Double { smoothed }

    // "Straight ahead is where I am looking now." Takes effect on the next sample.
    public mutating func recentre() {
        reference = nil
        referenceAttitude = nil
        smoothed = 0
        candidate = nil
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
        // Always the short way round, so crossing the back of the head does not
        // drag the stage through the front.
        let step = dt > 0 ? 1 - exp(-dt / Self.smoothingSeconds) : 1
        smoothed = Self.wrap(smoothed + step * Self.wrap(relative - smoothed))
        return smoothed
    }

    static func wrap(_ degrees: Double) -> Double {
        var value = degrees.truncatingRemainder(dividingBy: 360)
        if value > 180 { value -= 360 }
        if value < -180 { value += 360 }
        return value
    }
}
