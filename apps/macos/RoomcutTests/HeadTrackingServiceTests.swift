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

    func testAnOutputChangeWithoutTheSwitchStillPausesAndResumes() async {
        // Undo, an A/B switch or the engine's own readback can move the output
        // between Speaker and Headphone without the tap that used to resync it.
        let source = FakeHeadMotionSource()
        let model = await trackingModel(source)
        model.setSpatialOutput(headphone: true)
        model.setHeadTracking(true)
        XCTAssertEqual(source.starts, 1)

        model.spatialMode = 0
        await model.refreshNow()
        XCTAssertEqual(source.stops, 1, "speakers again: the next poll pauses the sensor")

        model.spatialMode = 1
        await model.refreshNow()
        XCTAssertEqual(source.starts, 2, "headphones again: the next poll resumes it")
        XCTAssertTrue(model.headTrackingActive)
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

// The stream going quiet with nothing reported (head-tracking-v3.csv,
// 2026-10-05): the service has to notice by itself and bring it back.
@MainActor
final class HeadTrackingRecoveryTests: XCTestCase {
    private func running(_ source: FakeHeadMotionSource, _ clock: FakeClock, _ sends: SendLog) -> HeadTrackingService {
        let service = HeadTrackingService(send: { yaw, active in sends.add(yaw, active) }, source: source,
                                          clock: { clock.now })
        service.start()
        return service
    }

    private func deliver(_ source: FakeHeadMotionSource, at time: TimeInterval) {
        guard let queue = source.queue, let handler = source.handler else { return XCTFail("no stream") }
        let sample = HeadMotionSample(timestamp: time, attitude: .init(w: 1, x: 0, y: 0, z: 0))
        queue.addOperation { handler(sample, nil) }
        queue.waitUntilAllOperationsAreFinished()
    }

    func testAQuietStreamLetsTheStageGoAndIsRestarted() throws {
        let source = FakeHeadMotionSource(), clock = FakeClock(), sends = SendLog()
        let service = running(source, clock, sends)
        deliver(source, at: 0.02)
        clock.now = 0.5
        service.checkStream(now: clock.now)
        XCTAssertTrue(service.isDelivering, "half a second of samples is a running stream")

        clock.now = 1.1   // a second since the last sample: 50 missed
        service.checkStream(now: clock.now)
        XCTAssertFalse(service.isDelivering)
        source.queue?.waitUntilAllOperationsAreFinished()
        XCTAssertEqual(try XCTUnwrap(sends.entries.last).active, false, "the engine stops following a frozen head")
        XCTAssertTrue(service.isTracking, "the switch stays on")

        service.checkStream(now: clock.now)
        XCTAssertEqual(source.starts, 2, "the stream is restarted at once")
        XCTAssertTrue(service.isDelivering)
    }

    func testAStreamThatStaysQuietIsRetriedLessOften() {
        let source = FakeHeadMotionSource(), clock = FakeClock(), sends = SendLog()
        let service = running(source, clock, sends)
        var starts: [Int] = []
        for _ in 0..<40 {   // 20 s without a single sample
            clock.now += HeadTrackingService.watchInterval
            service.checkStream(now: clock.now)
            starts.append(source.starts)
        }
        XCTAssertGreaterThan(source.starts, 3, "it keeps trying")
        XCTAssertLessThan(source.starts, 10, "but backs off rather than restarting every half second")

        deliver(source, at: clock.now)
        clock.now += HeadTrackingService.watchInterval
        service.checkStream(now: clock.now)
        XCTAssertTrue(service.isDelivering, "samples are back and the stream stays up")
    }

    func testASensorErrorIsRetriedWithoutAReconnect() {
        let source = FakeHeadMotionSource(), clock = FakeClock(), sends = SendLog()
        let service = running(source, clock, sends)
        source.handler?(nil, NSError(domain: "CMErrorDomain", code: 109))
        let settled = expectation(description: "error handled")
        Task { @MainActor in settled.fulfill() }
        wait(for: [settled], timeout: 2)
        XCTAssertFalse(service.isDelivering)
        // A headset that never disconnected sends no reconnect.
        service.checkStream(now: clock.now)
        XCTAssertEqual(source.starts, 2)
        XCTAssertTrue(service.isDelivering)
    }

    func testSwitchingOffEndsTheRetries() {
        let source = FakeHeadMotionSource(), clock = FakeClock(), sends = SendLog()
        let service = running(source, clock, sends)
        clock.now = 2
        service.checkStream(now: clock.now)   // stalled, restart scheduled
        service.stop()
        for _ in 0..<20 {
            clock.now += 1
            service.checkStream(now: clock.now)
        }
        XCTAssertEqual(source.starts, 1, "nothing restarts a stream the listener switched off")
    }
}

final class FakeClock: @unchecked Sendable {
    private let lock = NSLock()
    private var value: TimeInterval = 0
    var now: TimeInterval {
        get { lock.lock(); defer { lock.unlock() }; return value }
        set { lock.lock(); value = newValue; lock.unlock() }
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
