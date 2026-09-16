import CoreMotion
import Foundation

// Reads the listener's head orientation from AirPods (or Beats) and feeds it to
// the engine, which uses it to keep the virtual speakers pointed at the screen
// while the head turns.
//
// Three things make this different from every other control the app sends:
//
//   - It is live, not a setting. Measured on this Mac, AirPods Pro deliver
//     about 50 updates a second; those must never travel the parameter path,
//     which crossfades the whole DSP chain on each change.
//   - "Forward" is wherever the listener was looking, not where the sensor's
//     zero happens to be, so the service keeps its own reference angle and
//     drifts it back while the head is still.
//   - Losing the tracker has to be graceful: the engine is told the tracker is
//     gone so the render fades back to ordinary playback instead of freezing at
//     the last angle.
//
// Requires NSMotionUsageDescription in the app bundle; without it macOS kills
// the process the moment updates start.
@MainActor
public final class HeadTrackingService: ObservableObject {
    // The engine only needs the angle; the rest is the app's business.
    public typealias Sender = @Sendable (_ yawDegrees: Double, _ active: Bool) -> Void

    // What the listener asked for. Kept separate from whether samples are
    // actually arriving: AirPods drop out, wake up, get taken out of an ear, and
    // CoreMotion reports an error and stops. Before, any one of those ended
    // tracking for good — the switch flipped itself off and nothing brought it
    // back until the listener noticed and toggled it again.
    @Published public private(set) var isTracking = false
    // Whether the sensor is delivering right now. The engine is told to fade
    // back to ordinary playback whenever this is false.
    @Published public private(set) var isDelivering = false
    @Published public private(set) var deviceAvailable = false
    @Published public private(set) var permissionDenied = false
    @Published public private(set) var yawDegrees: Double = 0

    private let manager = CMHeadphoneMotionManager()
    private let send: Sender
    private let delegate = ConnectionDelegate()
    // Everything about what the angle MEANS lives in HeadYawTracker, which has
    // no sensor in it and is therefore testable. This class is the plumbing.
    private var tracker = HeadYawTracker()
    private var lastSentYaw: Double = 0
    private var lastSentAt: Date = .distantPast

    // The sensor's own rate is about 50 Hz; sending every one of those is
    // wasteful when the head is still, so an update goes out only when the
    // angle actually moved or enough time has passed to keep the engine warm.
    private static let minimumChangeDegrees = 0.25
    private static let heartbeat: TimeInterval = 0.5

    public init(send: @escaping Sender) {
        self.send = send
        delegate.onChange = { [weak self] connected in
            Task { @MainActor in
                guard let self else { return }
                self.deviceAvailable = connected
                // Back from a dropout: pick up where we left off rather than
                // waiting for the listener to work out that it died.
                if connected, self.isTracking, !self.isDelivering { self.beginUpdates() }
                if !connected, self.isDelivering { self.suspend() }
            }
        }
        manager.delegate = delegate
    }

    public var isSupported: Bool { manager.isDeviceMotionAvailable }

    public func start() {
        guard !isTracking else { return }
        guard manager.isDeviceMotionAvailable else {
            deviceAvailable = false
            return
        }
        if CMHeadphoneMotionManager.authorizationStatus() == .denied {
            permissionDenied = true
            return
        }
        permissionDenied = false
        isTracking = true
        manager.startConnectionStatusUpdates()
        beginUpdates()
    }

    public func stop() {
        guard isTracking else { return }
        isTracking = false
        manager.stopConnectionStatusUpdates()
        suspend()
    }

    // The listener declares where "straight ahead" is. Also the way out of a
    // stage that has crept off centre, so it must work even between samples.
    public func recentre() {
        tracker.recentre()
        yawDegrees = 0
        lastSentYaw = 0
        lastSentAt = .distantPast
        push(0, force: true)
    }

    // Start (or restart) the sensor stream. Safe to call again after a dropout.
    private func beginUpdates() {
        guard isTracking, !isDelivering else { return }
        tracker.reset()
        lastSentYaw = 0
        lastSentAt = .distantPast
        isDelivering = true
        manager.startDeviceMotionUpdates(to: .main) { [weak self] motion, error in
            guard let self else { return }
            if error != nil {
                // A failure is not a decision to stop tracking. Drop the stream,
                // keep the intent, and let the connection delegate bring it back.
                self.suspend()
                return
            }
            guard let motion else { return }
            self.accept(motion)
        }
        deviceAvailable = true
    }

    // Stop receiving, and make sure the engine is not left rendering a stage at
    // whatever angle the head happened to be at when the sensor went away.
    private func suspend() {
        manager.stopDeviceMotionUpdates()
        isDelivering = false
        yawDegrees = 0
        tracker.reset()
        lastSentYaw = 0
        lastSentAt = .distantPast
        send(0, false)
    }

    private func accept(_ motion: CMDeviceMotion) {
        let q = motion.attitude.quaternion
        // Core Motion's yaw runs counter-clockwise seen from above, so turning
        // the head to the RIGHT reads negative. The engine's convention is the
        // other way round (+ = right), hence the flip. Get this wrong and the
        // stage swings the wrong way, which is worse than no tracking at all.
        let raw = -atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 180 / .pi
        // The sample's own clock, not the wall clock: a delayed batch must not
        // read as the listener having held still.
        let yaw = tracker.update(rawDegrees: raw, at: motion.timestamp)
        yawDegrees = yaw
        push(yaw, force: false)
    }

    private func push(_ yaw: Double, force: Bool) {
        let now = Date()
        let moved = abs(yaw - lastSentYaw) >= Self.minimumChangeDegrees
        let due = now.timeIntervalSince(lastSentAt) >= Self.heartbeat
        guard force || moved || due else { return }
        lastSentYaw = yaw
        lastSentAt = now
        send(yaw, true)
    }

    // Connect/disconnect arrives on its own delegate, not the motion handler.
    private final class ConnectionDelegate: NSObject, CMHeadphoneMotionManagerDelegate, @unchecked Sendable {
        var onChange: ((Bool) -> Void)?
        func headphoneMotionManagerDidConnect(_ manager: CMHeadphoneMotionManager) { onChange?(true) }
        func headphoneMotionManagerDidDisconnect(_ manager: CMHeadphoneMotionManager) { onChange?(false) }
    }
}
