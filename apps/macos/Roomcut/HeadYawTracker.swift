import Foundation

// The arithmetic behind head tracking, with no sensor attached: raw yaw from the
// headphones goes in, the angle the engine should render comes out.
//
// It is a separate type because the hard parts are not the sensor — they are
// what "straight ahead" means and what happens when the listener turns a long
// way. Both were wrong before, and neither could be tested while this logic
// lived inside a CoreMotion callback.
//
// What it has to get right:
//
//   - "Forward" is wherever the listener was looking when tracking started or
//     was last re-centred, NOT the sensor's own zero.
//   - A gyro drifts. Over minutes, forward creeps away from where the listener
//     is actually facing, so the reference has to follow — but only while the
//     listener is looking more or less forward and holding still. Chasing a head
//     that is deliberately turned is what made the origin walk off: a pose held
//     for a few seconds was read as a new forward, so returning to the screen
//     left the stage pointing somewhere else. Measured before this: a 40 degree
//     turn collapsed to 10 degrees after one second and to nothing after four,
//     and coming back to centre reported -2.9 degrees instead of zero.
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
    // Re-centring only happens while the listener is looking roughly forward.
    // Outside this window a held pose is a held pose, not a new forward.
    public static let recentreWindowDegrees = 8.0
    // ...and only while they are actually still.
    public static let stillDegreesPerSecond = 3.0
    public static let stillSeconds = 2.0
    // How fast forward creeps once both conditions hold. This corrects a gyro,
    // which wanders over minutes, so it is deliberately far slower than any
    // movement a listener makes on purpose.
    public static let recentreSeconds = 20.0

    private var reference: Double?
    private var smoothed: Double = 0
    private var previous: Double = 0
    private var lastTime: TimeInterval?
    private var stillSince: TimeInterval?

    public init() {}

    public var yawDegrees: Double { smoothed }

    // "Straight ahead is where I am looking now."
    public mutating func recentre() {
        reference = nil
        smoothed = 0
        previous = 0
        stillSince = nil
    }

    public mutating func reset() {
        recentre()
        lastTime = nil
    }

    // One sensor sample in, the angle to send to the engine out. `time` is the
    // sample's own timestamp, so a dropped or delayed batch cannot change the
    // result.
    public mutating func update(rawDegrees raw: Double, at time: TimeInterval) -> Double {
        guard raw.isFinite, time.isFinite else { return smoothed }
        // A gap longer than this means the stream stalled; treat it as one
        // ordinary step rather than letting a huge dt jump the smoother.
        let dt = lastTime.map { max(0, min(0.25, time - $0)) } ?? 0
        lastTime = time

        if reference == nil { reference = raw }
        let relative = Self.wrap(raw - (reference ?? raw))
        // Always the short way round, so crossing the back of the head does not
        // drag the stage through the front.
        let step = dt > 0 ? 1 - exp(-dt / Self.smoothingSeconds) : 1
        smoothed = Self.wrap(smoothed + step * Self.wrap(relative - smoothed))

        if dt > 0 { followDrift(dt: dt, at: time) }
        previous = smoothed
        return smoothed
    }

    private mutating func followDrift(dt: Double, at time: TimeInterval) {
        let speed = abs(Self.wrap(smoothed - previous)) / dt
        let lookingForward = abs(smoothed) < Self.recentreWindowDegrees
        guard lookingForward, speed < Self.stillDegreesPerSecond else {
            stillSince = nil
            return
        }
        guard let since = stillSince else {
            stillSince = time
            return
        }
        guard time - since > Self.stillSeconds else { return }
        let amount = 1 - exp(-dt / Self.recentreSeconds)
        reference = Self.wrap((reference ?? 0) + smoothed * amount)
        smoothed -= smoothed * amount
    }

    static func wrap(_ degrees: Double) -> Double {
        var value = degrees.truncatingRemainder(dividingBy: 360)
        if value > 180 { value -= 360 }
        if value < -180 { value += 360 }
        return value
    }
}
