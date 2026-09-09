import XCTest
@testable import RoomcutCore

@MainActor
final class RoomTuneRoundTests: XCTestCase {
    private func waitUntil(_ condition: () -> Bool) async throws {
        for _ in 0..<1000 {
            if condition() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("round did not reach expected state")
    }
    private func round(_ transport: TestRoomTuneTransport) -> RoomTuneRound {
        RoomTuneRound(transport: transport, tail: {}, analyze: { url in
            XCTAssertFalse(Thread.isMainThread)
            XCTAssertEqual(try Data(contentsOf: url), Data("test recording".utf8))
            return [(100, -12)]
        })
    }
    private func round(_ transport: TestRoomTuneTransport, timeoutNanoseconds: UInt64) -> RoomTuneRound {
        RoomTuneRound(transport: transport, tail: {}, timeoutNanoseconds: timeoutNanoseconds,
                      analyze: { _ in [(100, -12)] })
    }
    private func assertRemoved(_ transport: TestRoomTuneTransport) throws {
        let url = try XCTUnwrap(transport.recording)
        XCTAssertFalse(FileManager.default.fileExists(atPath: url.path))
    }

    func testSuccessfulRoundStopsBeforeAnalysisAndRemovesItsRecording() async throws {
        let transport = TestRoomTuneTransport()
        let response = try await round(transport).measure(deviceID: "mic")
        XCTAssertEqual(response.first?.freq, 100)
        XCTAssertEqual(transport.events, ["prepare", "play", "stop"])
        try assertRemoved(transport)
    }

    func testCancellationDuringNativeStartNeverStartsPlayback() async throws {
        let entered = expectation(description: "native prepare blocked")
        let release = DispatchSemaphore(value: 0)
        let transport = TestRoomTuneTransport(onPrepare: {
            entered.fulfill()
            XCTAssertEqual(release.wait(timeout: .now() + 3), .success)
        })
        let measurement = round(transport)
        let task = Task { try await measurement.measure(deviceID: "mic") }
        await fulfillment(of: [entered], timeout: 1)
        task.cancel()
        XCTAssertEqual(transport.events, ["prepare"])
        release.signal()
        do { _ = try await task.value; XCTFail("cancelled round returned a response") }
        catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertEqual(transport.events, ["prepare", "stop"])
        try assertRemoved(transport)
    }

    func testCancellationWhilePlayingIgnoresLateCompletion() async throws {
        let transport = TestRoomTuneTransport(autoComplete: false)
        let measurement = round(transport)
        let task = Task { try await measurement.measure(deviceID: "mic") }
        try await waitUntil { transport.events.contains("play") }
        task.cancel()
        do { _ = try await task.value; XCTFail("cancelled playback returned a response") }
        catch { XCTAssertTrue(error is CancellationError) }
        transport.complete(); transport.complete()
        XCTAssertEqual(transport.events, ["prepare", "play", "stop"])
        try assertRemoved(transport)
    }

    func testStartPlaybackAndRecordingFailuresAlwaysCleanUp() async throws {
        for failure in ["prepare", "play", "recording"] {
            let transport = TestRoomTuneTransport(failure: failure)
            do { _ = try await round(transport).measure(deviceID: "mic"); XCTFail("failed IO was accepted") }
            catch { }
            XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1)
            try assertRemoved(transport)
        }
    }

    func testAnalysisFailureDoesNotStopTheTransportTwice() async throws {
        let transport = TestRoomTuneTransport()
        let measurement = RoomTuneRound(transport: transport, tail: {}, analyze: { _ in
            throw NSError(domain: "analysis", code: 99)
        })
        do { _ = try await measurement.measure(deviceID: "mic"); XCTFail("analysis failure was accepted") }
        catch { XCTAssertEqual((error as NSError).code, 99) }
        XCTAssertEqual(transport.events, ["prepare", "play", "stop"])
        try assertRemoved(transport)
    }

    func testCancellationDuringCaptureTailDoesNotAnalyze() async throws {
        let transport = TestRoomTuneTransport()
        let entered = expectation(description: "capture tail started")
        let measurement = RoomTuneRound(transport: transport, tail: {
            entered.fulfill(); try await Task.sleep(nanoseconds: 10_000_000_000)
        }, analyze: { _ in XCTFail("cancelled recording was analyzed"); return [] })
        let task = Task { try await measurement.measure(deviceID: "mic") }
        await fulfillment(of: [entered], timeout: 1)
        task.cancel()
        do { _ = try await task.value; XCTFail("cancelled tail returned a response") }
        catch { XCTAssertTrue(error is CancellationError) }
        try assertRemoved(transport)
    }

    func testAlreadyCancelledRoundDoesNotTouchHardware() async throws {
        let transport = TestRoomTuneTransport()
        let measurement = round(transport)
        let task = Task { try await measurement.measure(deviceID: "mic") }
        task.cancel()
        do { _ = try await task.value; XCTFail("cancelled task entered IO") }
        catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertEqual(transport.events, [])
    }

    func testMissingPlaybackCompletionEndsAndCleansUpWithoutUserCancellation() async throws {
        let transport = TestRoomTuneTransport(autoComplete: false)
        let measurement = round(transport)
        var finished = false
        var failure: Error?
        let task = Task {
            do { _ = try await measurement.measure(deviceID: "mic") }
            catch { failure = error }
            finished = true
        }
        try await waitUntil { transport.events.contains("play") }
        for _ in 0..<120 where !finished { try await Task.sleep(nanoseconds: 100_000_000) }
        let automaticallyFinished = finished
        print("missing playback completion: automatically finished=\(automaticallyFinished)")
        XCTAssertTrue(automaticallyFinished, "a six-second sweep must not record indefinitely")
        if !finished { task.cancel() }
        await task.value
        XCTAssertNotNil(failure)
        XCTAssertEqual(failure as? RoomTuneRoundError, .timedOut)
        XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1)
        try assertRemoved(transport)
    }

    // RT-05. The round polls the transport's health while it waits for the playback
    // completion, so an interruption that arrives mid-round has to end it on the spot —
    // with the transport's own error, one stop, and no recording left behind — instead of
    // sitting out the sweep deadline.
    private func assertInterruptionEndsTheRound(_ failure: RoomTuneRoundError,
                                                line: UInt = #line) async throws {
        let transport = TestRoomTuneTransport(autoComplete: false)
        let measurement = round(transport, timeoutNanoseconds: 30_000_000_000)
        let started = ContinuousClock.now
        let task = Task { try await measurement.measure(deviceID: "mic") }
        try await waitUntil { transport.events.contains("play") }
        transport.interrupt(with: failure)
        do { _ = try await task.value; XCTFail("interrupted round returned a response", line: line) }
        catch { XCTAssertEqual(error as? RoomTuneRoundError, failure, line: line) }
        XCTAssertLessThan(started.duration(to: .now), .seconds(5),
                          "an interrupted round must not wait out the sweep deadline", line: line)
        XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1, line: line)
        try assertRemoved(transport)
        // The device can still call back after it was stopped; that must change nothing.
        transport.complete()
        XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1, line: line)
    }

    func testLostMicrophoneEndsTheRoundWithTheCaptureMessage() async throws {
        try await assertInterruptionEndsTheRound(.captureInterrupted)
    }

    func testLostOutputEndsTheRoundWithThePlaybackMessage() async throws {
        try await assertInterruptionEndsTheRound(.playbackInterrupted)
    }

    // A completion that arrives after the deadline has passed belongs to a round that is
    // already finished: no second stop, and no analysis of a recording that is gone.
    func testPlaybackCompletionAfterTheDeadlineChangesNothing() async throws {
        let transport = TestRoomTuneTransport(autoComplete: false)
        let measurement = RoomTuneRound(transport: transport, tail: {},
                                        timeoutNanoseconds: 200_000_000,
                                        analyze: { _ in XCTFail("timed-out round was analyzed"); return [] })
        do { _ = try await measurement.measure(deviceID: "mic"); XCTFail("timed-out round returned a response") }
        catch { XCTAssertEqual(error as? RoomTuneRoundError, .timedOut) }
        XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1)
        try assertRemoved(transport)
        transport.complete(); transport.complete()
        XCTAssertEqual(transport.events, ["prepare", "play", "stop"])
    }

    // Cleanup owns the hardware until the native stop returns. The round may not hand
    // control back early — the next round would then start on a device this one still
    // holds. Nothing here preempts a native call that has already begun.
    func testRoundReturnsOnlyAfterASlowStopHasFinished() async throws {
        let transport = TestRoomTuneTransport(autoComplete: false, stopDelay: 0.4)
        let measurement = round(transport, timeoutNanoseconds: 100_000_000)
        let started = ContinuousClock.now
        do { _ = try await measurement.measure(deviceID: "mic"); XCTFail("timed-out round returned a response") }
        catch { XCTAssertEqual(error as? RoomTuneRoundError, .timedOut) }
        XCTAssertGreaterThanOrEqual(started.duration(to: .now), .milliseconds(400),
                                    "the round returned before its own cleanup finished")
        XCTAssertEqual(transport.events.filter { $0 == "stop" }.count, 1)
        try assertRemoved(transport)
    }

    // A recording the transport could not keep must reach the user as the recording
    // message, not as an opaque native error.
    func testLostRecordingReportsTheRecordingFailure() async throws {
        let transport = TestRoomTuneTransport(failure: "recording")
        do { _ = try await round(transport).measure(deviceID: "mic"); XCTFail("empty recording was accepted") }
        catch { XCTAssertEqual(error as? RoomTuneRoundError, .recordingFailed) }
        try assertRemoved(transport)
    }

    func testUnhealthyCaptureEndsPromptlyWithoutPlaybackCompletion() async throws {
        let transport = TestRoomTuneTransport(autoComplete: false, failure: "health")
        let measurement = round(transport)
        var finished = false
        var failure: Error?
        let task = Task {
            do { _ = try await measurement.measure(deviceID: "mic") }
            catch { failure = error }
            finished = true
        }
        for _ in 0..<100 where !finished { try await Task.sleep(nanoseconds: 10_000_000) }
        XCTAssertTrue(finished, "an interrupted capture must not keep waiting for playback")
        if !finished { task.cancel() }
        await task.value
        XCTAssertNotNil(failure)
        try assertRemoved(transport)
    }
}

private final class TestRoomTuneTransport: RoomTuneTransport {
    private let lock = NSLock()
    private var history: [String] = []
    private var url: URL?
    private var completion: (() -> Void)?
    private let onPrepare: () -> Void
    private let autoComplete: Bool
    private let failure: String
    private let stopDelay: TimeInterval
    private var interruption: Error?
    init(autoComplete: Bool = true, failure: String = "", stopDelay: TimeInterval = 0,
         onPrepare: @escaping () -> Void = {}) {
        self.autoComplete = autoComplete; self.failure = failure
        self.stopDelay = stopDelay; self.onPrepare = onPrepare
    }
    /// Interrupt an in-flight round the way a lost mic or output device does: the next
    /// health check the round makes fails.
    func interrupt(with error: Error) { lock.lock(); interruption = error; lock.unlock() }
    var events: [String] { lock.lock(); defer { lock.unlock() }; return history }
    var recording: URL? { lock.lock(); defer { lock.unlock() }; return url }
    func prepare(deviceID: String, url: URL) throws {
        XCTAssertFalse(Thread.isMainThread)
        lock.lock(); history.append("prepare"); self.url = url; lock.unlock()
        try Data("test recording".utf8).write(to: url)
        onPrepare()
        if failure == "prepare" { throw NSError(domain: "prepare", code: 1) }
    }
    func play(completion: @escaping () -> Void) throws {
        XCTAssertFalse(Thread.isMainThread)
        lock.lock(); history.append("play"); self.completion = completion; lock.unlock()
        if failure == "play" { throw NSError(domain: "play", code: 2) }
        if autoComplete { completion() }
    }
    func complete() {
        lock.lock(); let callback = completion; lock.unlock()
        callback?()
    }
    func stop() -> URL? {
        XCTAssertFalse(Thread.isMainThread)
        if stopDelay > 0 { Thread.sleep(forTimeInterval: stopDelay) }
        lock.lock(); defer { lock.unlock() }
        history.append("stop")
        return failure == "recording" ? nil : url
    }
    func checkHealth() throws {
        XCTAssertFalse(Thread.isMainThread)
        lock.lock(); let pending = interruption; lock.unlock()
        if let pending { throw pending }
        if failure == "health" { throw NSError(domain: "capture", code: 3) }
    }
}
