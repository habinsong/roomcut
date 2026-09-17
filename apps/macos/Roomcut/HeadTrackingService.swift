import CoreMotion
import Foundation
import os

// Reads the listener's head orientation from AirPods (or Beats) and feeds it to
// the engine, which uses it to keep the virtual speakers pointed at the screen
// while the head turns.
//
// Four things make this different from every other control the app sends:
//
//   - It is live, not a setting. Measured on this Mac, AirPods Pro deliver
//     about 50 updates a second; those must never travel the parameter path,
//     which crossfades the whole DSP chain on each change.
//   - It must not wait for the interface. Samples used to be delivered to the
//     main queue, and the log (head-tracking-v2.csv, 2026-09-17) showed them
//     held back 80–481 ms whenever the window was busy — a tab switch cost
//     445–481 ms — while the sensor itself stepped every 20 ms. For that long
//     the stage stood still while the head turned. Samples are now handled on
//     their own queue (HeadPosePipeline) and only a coarse angle is handed to
//     the main thread for the picture.
//   - "Forward" is wherever the listener was looking once the head settled,
//     not where the sensor's zero happens to be (see HeadYawTracker).
//   - The listener's choice outlives the output. Switching Speaker on used to
//     stop tracking for good: the same log shows the stream ending with no
//     switch touched and coming back only when the switch was turned on again.
//     Now the sensor pauses while the output cannot use it and resumes on its
//     own when headphones are back. Losing the tracker is still graceful: the
//     engine is told it is gone so the render fades back to ordinary playback
//     instead of freezing at the last angle.
//
// Requires NSMotionUsageDescription in the app bundle; without it macOS kills
// the process the moment updates start.
@MainActor
public final class HeadTrackingService: ObservableObject {
    // The engine only needs the angle; the rest is the app's business.
    public typealias Sender = @Sendable (_ yawDegrees: Double, _ active: Bool) -> Void

    // What the listener asked for. Kept separate from whether samples are
    // actually arriving: AirPods drop out, wake up, get taken out of an ear,
    // CoreMotion reports an error and stops, and the output switches to
    // speakers. None of those is a decision to stop tracking.
    @Published public private(set) var isTracking = false
    // Whether the sensor is delivering right now. The engine is told to fade
    // back to ordinary playback whenever this is false.
    @Published public private(set) var isDelivering = false
    @Published public private(set) var deviceAvailable = false
    @Published public private(set) var permissionDenied = false
    // For the picture only, so it moves in whole degrees rather than fifty
    // times a second; the engine gets every change (HeadPosePipeline).
    @Published public private(set) var yawDegrees: Double = 0

    private let source: HeadMotionSource
    private let pipeline: HeadPosePipeline
    // Whether the current output can use tracking (headphones). False pauses the
    // sensor without touching the listener's choice.
    private var allowed = true
    private let events = Logger(subsystem: "com.roomcut.app", category: "HeadTracking")

    public convenience init(send: @escaping Sender) {
        self.init(send: send, source: CoreMotionHeadSource())
    }

    init(send: @escaping Sender, source: HeadMotionSource) {
        self.source = source
        pipeline = HeadPosePipeline(send: send, log: HeadTrackingLog.openIfEnabled())
        pipeline.onAngle = { [weak self] yaw in
            DispatchQueue.main.async {
                guard let self, self.isDelivering else { return }
                self.yawDegrees = yaw
            }
        }
    }

    public var isSupported: Bool { source.isAvailable }

    public func start() {
        guard !isTracking else { return }
        guard source.isAvailable else {
            deviceAvailable = false
            events.notice("start refused: headphone motion unavailable")
            return
        }
        if source.isDenied {
            permissionDenied = true
            events.notice("start refused: motion permission denied")
            return
        }
        permissionDenied = false
        isTracking = true
        events.notice("start (output allows it: \(self.allowed, privacy: .public))")
        source.startConnectionUpdates { [weak self] connected in
            Task { @MainActor in self?.connectionChanged(connected) }
        }
        beginUpdates()
    }

    public func stop() {
        guard isTracking else { return }
        isTracking = false
        source.stopConnectionUpdates()
        suspend(reason: "switched off")
    }

    // Whether the output can use tracking right now. Speakers do not move with
    // the listener, so the sensor pauses there and picks up again on headphones
    // — the switch keeps what the listener chose.
    public func setAllowed(_ allowed: Bool) {
        guard self.allowed != allowed else { return }
        self.allowed = allowed
        events.notice("output \(allowed ? "allows" : "does not allow", privacy: .public) tracking (switch on: \(self.isTracking, privacy: .public))")
        if allowed {
            beginUpdates()
        } else if isDelivering {
            suspend(reason: "output cannot use it")
        }
    }

    // The listener declares where "straight ahead" is. Also the way out of a
    // stage that has crept off centre, so it must work even between samples.
    public func recentre() {
        yawDegrees = 0
        pipeline.recentre()
    }

    // Start (or restart) the sensor stream. Safe to call again after a dropout.
    private func beginUpdates() {
        guard isTracking, allowed, !isDelivering else { return }
        isDelivering = true
        deviceAvailable = true
        yawDegrees = 0
        // Queued before the stream starts, so the first sample finds it.
        pipeline.begin()
        events.notice("sensor stream started")
        source.startUpdates(to: pipeline.queue) { [pipeline, weak self] sample, error in
            // On the motion queue.
            if let error {
                let code = (error as NSError).code
                Task { @MainActor in self?.streamFailed(code: code) }
                return
            }
            if let sample { pipeline.accept(sample) }
        }
    }

    // Stop receiving, and make sure the engine is not left rendering a stage at
    // whatever angle the head happened to be at when the sensor went away.
    private func suspend(reason: String) {
        source.stopUpdates()
        let wasDelivering = isDelivering
        isDelivering = false
        yawDegrees = 0
        pipeline.end()
        events.notice("sensor stream stopped: \(reason, privacy: .public) (was delivering: \(wasDelivering, privacy: .public))")
    }

    private func connectionChanged(_ connected: Bool) {
        deviceAvailable = connected
        events.notice("headphones \(connected ? "connected" : "disconnected", privacy: .public)")
        // Back from a dropout: pick up where we left off rather than waiting for
        // the listener to work out that it died.
        if connected, isTracking, !isDelivering { beginUpdates() }
        if !connected, isDelivering { suspend(reason: "headphones disconnected") }
    }

    // A failure is not a decision to stop tracking. Drop the stream, keep the
    // intent, and let the connection delegate bring it back.
    private func streamFailed(code: Int) {
        guard isDelivering else { return }
        suspend(reason: "sensor error \(code)")
    }
}

// One sensor reading, copied out of CMDeviceMotion on the motion queue so it
// can be handed on (CMDeviceMotion is not Sendable).
struct HeadMotionSample: Sendable {
    // Seconds since boot: the sensor's own clock, the same one as
    // ProcessInfo.systemUptime.
    var timestamp: TimeInterval
    var attitude: HeadYawTracker.Quaternion
    // Diagnostics only (the CSV log), in degrees and g.
    var euler = SIMD3<Double>()          // yaw, pitch, roll
    var rotationRate = SIMD3<Double>()
    var gravity = SIMD3<Double>()
}

// The headphones' motion sensor, behind a seam so the service's lifecycle can be
// tested without AirPods.
protocol HeadMotionSource: AnyObject {
    var isAvailable: Bool { get }
    var isDenied: Bool { get }
    func startConnectionUpdates(_ onChange: @escaping @Sendable (Bool) -> Void)
    func stopConnectionUpdates()
    // `handler` runs on `queue`.
    func startUpdates(to queue: OperationQueue, _ handler: @escaping @Sendable (HeadMotionSample?, Error?) -> Void)
    func stopUpdates()
}

final class CoreMotionHeadSource: NSObject, HeadMotionSource, CMHeadphoneMotionManagerDelegate, @unchecked Sendable {
    private let manager = CMHeadphoneMotionManager()
    private var onChange: (@Sendable (Bool) -> Void)?

    override init() {
        super.init()
        manager.delegate = self
    }

    var isAvailable: Bool { manager.isDeviceMotionAvailable }
    var isDenied: Bool { CMHeadphoneMotionManager.authorizationStatus() == .denied }

    func startConnectionUpdates(_ onChange: @escaping @Sendable (Bool) -> Void) {
        self.onChange = onChange
        manager.startConnectionStatusUpdates()
    }

    func stopConnectionUpdates() { manager.stopConnectionStatusUpdates() }

    func startUpdates(to queue: OperationQueue, _ handler: @escaping @Sendable (HeadMotionSample?, Error?) -> Void) {
        manager.startDeviceMotionUpdates(to: queue) { motion, error in
            if let error { handler(nil, error); return }
            guard let motion else { return }
            let a = motion.attitude, q = a.quaternion, r = motion.rotationRate, g = motion.gravity
            let d = 180 / Double.pi
            handler(HeadMotionSample(timestamp: motion.timestamp,
                                     attitude: .init(w: q.w, x: q.x, y: q.y, z: q.z),
                                     euler: SIMD3(a.yaw * d, a.pitch * d, a.roll * d),
                                     rotationRate: SIMD3(r.x * d, r.y * d, r.z * d),
                                     gravity: SIMD3(g.x, g.y, g.z)), nil)
        }
    }

    func stopUpdates() { manager.stopDeviceMotionUpdates() }

    // Connect/disconnect arrives on its own delegate, not the motion handler.
    func headphoneMotionManagerDidConnect(_ manager: CMHeadphoneMotionManager) { onChange?(true) }
    func headphoneMotionManagerDidDisconnect(_ manager: CMHeadphoneMotionManager) { onChange?(false) }
}

// Everything done per sample, on the motion queue: the angle, the decision to
// send it, the send, and the diagnostics log. All of its state is touched only
// on `queue`, a serial queue — begin/end/recentre are queued behind whatever
// samples are already waiting, so a sample can never land after end() and
// re-activate a stage the service just switched off.
final class HeadPosePipeline: @unchecked Sendable {
    let queue: OperationQueue
    // The picture's angle, in whole degrees; called on `queue`.
    var onAngle: (@Sendable (Double) -> Void)?

    // The sensor's own rate is about 50 Hz; sending every one of those is
    // wasteful when the head is still, so an update goes out only when the
    // angle actually moved or enough time has passed to keep the engine warm.
    static let minimumChangeDegrees = 0.25
    static let heartbeatSeconds = 0.5
    static let pictureStepDegrees = 1.0

    private let send: HeadTrackingService.Sender
    private let log: HeadTrackingLog?
    private var tracker = HeadYawTracker()
    private var running = false
    private var lastSentYaw = 0.0
    private var lastSentAt = -Double.infinity
    private var lastPictureYaw = 0.0

    init(send: @escaping HeadTrackingService.Sender, log: HeadTrackingLog?) {
        self.send = send
        self.log = log
        queue = OperationQueue()
        queue.name = "com.roomcut.app.head-tracking"
        queue.maxConcurrentOperationCount = 1
        queue.qualityOfService = .userInteractive
    }

    func begin() {
        queue.addOperation { [self] in
            running = true
            restart()
        }
    }

    func end() {
        queue.addOperation { [self] in
            running = false
            restart()
            send(0, false)
        }
    }

    func recentre() {
        queue.addOperation { [self] in
            tracker.recentre()
            lastSentYaw = 0
            lastSentAt = -.infinity
            lastPictureYaw = 0
            guard running else { return }
            send(0, true)
        }
    }

    // Called on `queue` by the motion handler.
    func accept(_ sample: HeadMotionSample) {
        guard running else { return }
        let yaw = tracker.update(attitude: sample.attitude, at: sample.timestamp)
        if abs(yaw - lastSentYaw) >= Self.minimumChangeDegrees
            || sample.timestamp - lastSentAt >= Self.heartbeatSeconds {
            lastSentYaw = yaw
            lastSentAt = sample.timestamp
            send(yaw, true)
        }
        if abs(yaw - lastPictureYaw) >= Self.pictureStepDegrees {
            lastPictureYaw = yaw
            onAngle?(yaw)
        }
        log?.record(sample, reported: yaw)
    }

    private func restart() {
        tracker.reset()
        lastSentYaw = 0
        lastSentAt = -.infinity
        lastPictureYaw = 0
    }
}

// Diagnostics only, off unless `defaults write com.roomcut.app
// roomcut.debug.headTrackingLog -bool YES`: every sample as CSV in
// ~/Library/Logs/Roomcut/head-tracking-v3.csv (attitude quaternion, gravity, and
// how old the sample was when it was handled), so drift and delivery delay can
// be measured from what the sensor actually delivered instead of guessed at.
// Start/stop events go to the unified log (subsystem com.roomcut.app, category
// HeadTracking) whether or not this is on. Used only on the motion queue.
final class HeadTrackingLog {
    private let handle: FileHandle
    private var pending = ""
    private var lastFlush: TimeInterval = 0
    private var written = 0
    private static let limitBytes = 64 * 1024 * 1024

    static func openIfEnabled() -> HeadTrackingLog? {
        guard UserDefaults.standard.bool(forKey: "roomcut.debug.headTrackingLog") else { return nil }
        let directory = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/Roomcut")
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let url = directory.appendingPathComponent("head-tracking-v3.csv")
        if !FileManager.default.fileExists(atPath: url.path) {
            let header = "sampleTime,wallTime,delayMs,rawYaw,cmYaw,pitch,roll,rateX,rateY,rateZ,reportedYaw,qw,qx,qy,qz,gravX,gravY,gravZ\n"
            FileManager.default.createFile(atPath: url.path, contents: Data(header.utf8))
        }
        guard let handle = try? FileHandle(forWritingTo: url) else { return nil }
        handle.seekToEndOfFile()
        return HeadTrackingLog(handle: handle)
    }

    private init(handle: FileHandle) { self.handle = handle }

    func record(_ sample: HeadMotionSample, reported: Double) {
        guard written < Self.limitBytes else { return }
        let q = sample.attitude
        // The Euler yaw the angle used to be, kept for comparison.
        let euler = -atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 180 / .pi
        let delayMs = (ProcessInfo.processInfo.systemUptime - sample.timestamp) * 1000
        pending += String(format: "%.4f,%.3f,%.1f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.5f,%.5f,%.5f\n",
                          sample.timestamp, Date().timeIntervalSince1970, delayMs, euler,
                          sample.euler.x, sample.euler.y, sample.euler.z,
                          sample.rotationRate.x, sample.rotationRate.y, sample.rotationRate.z,
                          reported, q.w, q.x, q.y, q.z, sample.gravity.x, sample.gravity.y, sample.gravity.z)
        guard sample.timestamp - lastFlush >= 1 else { return }
        lastFlush = sample.timestamp
        let data = Data(pending.utf8)
        handle.write(data)
        written += data.count
        pending = ""
    }
}
