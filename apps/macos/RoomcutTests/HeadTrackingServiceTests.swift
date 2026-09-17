import XCTest
@testable import RoomcutCore

// The service around the sensor, written against two failures measured on
// AirPods Pro (head-tracking-v2.csv and the unified log, 2026-09-17):
//
//   - Tapping Speaker stopped tracking for good. The stream ended with no switch
//     touched and came back only when the switch was turned on again.
//   - Samples waited for the main thread. The sensor stepped every 20 ms, but
//     while the window was busy the angle reached the engine 80–481 ms late.
@MainActor
final class HeadTrackingServiceTests: XCTestCase {
    private func trackingModel(_ source: FakeHeadMotionSource) async -> RoomcutViewModel {
        var status = EngineStatus()
        status.reachable = true
        status.state = EngineStatus.running
        status.presetId = "flat"
        status.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.virtualRoomCapability
            | EngineStatus.upmixCapability | EngineStatus.headTrackingCapability
        let client = FakeEngineClient()
        // Every poll reads the same engine, not a default one after the first.
        client.stateReadHandler = { status }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000,
                                     defaults: makeTestDefaults(name).defaults, headMotion: source)
        await model.refreshNow()
        return model
    }

    func testATripToSpeakersPausesTrackingAndHeadphonesResumeIt() async {
        let source = FakeHeadMotionSource()
        let model = await trackingModel(source)
        model.setSpatialOutput(headphone: true)
        model.setHeadTracking(true)
        XCTAssertTrue(model.headTrackingOn)
        XCTAssertEqual(source.starts, 1)

        model.setSpatialOutput(headphone: false)
        XCTAssertEqual(source.stops, 1, "speakers do not move with the head, so the sensor stops")
        XCTAssertTrue(model.headTrackingOn, "but the switch keeps what the listener chose")
        XCTAssertFalse(model.headTrackingActive)

        // What a listener does on the speaker side: surround taps.
        model.setSurroundChoice(.ambience)
        model.setSurroundChoice(.virtual51)

        model.setSpatialOutput(headphone: true)
        XCTAssertEqual(source.starts, 2, "back on headphones it resumes without the switch")
        XCTAssertTrue(model.headTrackingActive)

        model.setHeadTracking(false)
        XCTAssertFalse(model.headTrackingOn)
        model.setSpatialOutput(headphone: false)
        model.setSpatialOutput(headphone: true)
        XCTAssertEqual(source.starts, 2, "switched off stays off")
    }

    func testSurroundTapsOnHeadphonesNeverTouchTheStream() async {
        let source = FakeHeadMotionSource()
        let model = await trackingModel(source)
        model.setSpatialOutput(headphone: true)
        model.setHeadTracking(true)
        for choice in [RoomcutViewModel.SurroundChoice.off, .ambience, .virtual51, .virtual71, .off] {
            model.setSurroundChoice(choice)
        }
        XCTAssertEqual(source.starts, 1)
        XCTAssertEqual(source.stops, 0)
    }

    func testTheAngleReachesTheEngineWhileTheMainThreadIsBusy() {
        let source = FakeHeadMotionSource()
        let sends = SendLog()
        let service = HeadTrackingService(send: { yaw, active in sends.add(yaw, active) }, source: source)
        service.start()
        guard let queue = source.queue, let handler = source.handler else {
            return XCTFail("the stream did not start")
        }

        // The head holds still long enough to set forward (0.3 s), then turns
        // 45 degrees a second — 50 Hz samples by their own clock, delivered the
        // way CoreMotion delivers them: onto the queue the service chose. No
        // sleeps, so a slow machine changes nothing but how long it takes.
        let handled = DispatchSemaphore(value: 0)
        let feeder = Thread {
            let t0 = ProcessInfo.processInfo.systemUptime
            for i in 0..<50 {
                let degrees = i < 20 ? 0 : Double(i - 20) * 0.9
                let half = -degrees * .pi / 360      // Core Motion turns counter-clockwise
                let sample = HeadMotionSample(timestamp: t0 + Double(i) * 0.02,
                                              attitude: .init(w: cos(half), x: 0, y: 0, z: sin(half)))
                queue.addOperation { handler(sample, nil) }
            }
            queue.addOperation { handled.signal() }
        }
        feeder.start()
        // The main thread stays busy until the queue has handled every sample,
        // as it was for 445–481 ms at a tab switch. Samples that needed the main
        // thread could not be handled before this times out.
        let finished = handled.wait(timeout: .now() + 5) == .success
        let busyUntil = ProcessInfo.processInfo.systemUptime

        XCTAssertTrue(finished, "every sample was handled while the main thread was busy")
        let turning = sends.entries.filter { $0.active && $0.yaw > 1 }
        XCTAssertGreaterThan(turning.count, 20, "a 26 degree turn is sent as it happens")
        XCTAssertTrue(turning.allSatisfy { $0.at < busyUntil }, "none of it waited for the main thread")
        XCTAssertGreaterThan(turning.last?.yaw ?? 0, 20)
        service.stop()
    }

    func testNothingReactivatesTheStageAfterItStops() throws {
        let source = FakeHeadMotionSource()
        let sends = SendLog()
        let service = HeadTrackingService(send: { yaw, active in sends.add(yaw, active) }, source: source)
        service.start()
        guard let queue = source.queue, let handler = source.handler else {
            return XCTFail("the stream did not start")
        }
        let t0 = ProcessInfo.processInfo.systemUptime
        func sample(_ i: Int) -> HeadMotionSample {
            HeadMotionSample(timestamp: t0 + Double(i) * 0.02, attitude: .init(w: 1, x: 0, y: 0, z: 0))
        }
        for i in 0..<30 { queue.addOperation { handler(sample(i), nil) } }
        service.stop()
        // A sample CoreMotion had already queued when the stream was stopped.
        queue.addOperation { handler(sample(30), nil) }
        queue.waitUntilAllOperationsAreFinished()

        let last = try XCTUnwrap(sends.entries.last)
        XCTAssertFalse(last.active, "the engine's last word is that the tracker is gone")
        XCTAssertEqual(last.yaw, 0)
    }

    func testASensorErrorKeepsTheSwitchAndReconnectResumes() async throws {
        let source = FakeHeadMotionSource()
        let service = HeadTrackingService(send: { _, _ in }, source: source)
        service.start()
        source.handler?(nil, NSError(domain: "CMErrorDomain", code: 109))
        for _ in 0..<1000 where service.isDelivering { try await Task.sleep(nanoseconds: 2_000_000) }
        XCTAssertFalse(service.isDelivering)
        XCTAssertTrue(service.isTracking)
        source.connection?(true)
        for _ in 0..<1000 where !service.isDelivering { try await Task.sleep(nanoseconds: 2_000_000) }
        XCTAssertTrue(service.isDelivering)
        XCTAssertEqual(source.starts, 2)
    }
}

final class FakeHeadMotionSource: HeadMotionSource, @unchecked Sendable {
    var isAvailable = true
    var isDenied = false
    private(set) var starts = 0
    private(set) var stops = 0
    private(set) var queue: OperationQueue?
    private(set) var handler: (@Sendable (HeadMotionSample?, Error?) -> Void)?
    private(set) var connection: (@Sendable (Bool) -> Void)?

    func startConnectionUpdates(_ onChange: @escaping @Sendable (Bool) -> Void) { connection = onChange }
    func stopConnectionUpdates() { connection = nil }

    func startUpdates(to queue: OperationQueue, _ handler: @escaping @Sendable (HeadMotionSample?, Error?) -> Void) {
        starts += 1
        self.queue = queue
        self.handler = handler
    }

    func stopUpdates() { stops += 1 }
}

private final class SendLog: @unchecked Sendable {
    struct Entry { let yaw: Double; let active: Bool; let at: TimeInterval }
    private let lock = NSLock()
    private var stored: [Entry] = []

    func add(_ yaw: Double, _ active: Bool) {
        lock.lock(); defer { lock.unlock() }
        stored.append(Entry(yaw: yaw, active: active, at: ProcessInfo.processInfo.systemUptime))
    }

    var entries: [Entry] {
        lock.lock(); defer { lock.unlock() }
        return stored
    }
}
