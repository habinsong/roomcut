import XCTest
@testable import RoomcutCore

@MainActor
final class RoomTuneWorkflowTests: XCTestCase {
    private func waitUntil(_ condition: () -> Bool) async throws {
        for _ in 0..<1000 {
            if condition() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("measurement operation did not reach expected state")
    }
    func testCancelledBypassMustSettleBeforeAnotherRunStarts() async throws {
        let audio = ControlledRoomTuneAudio()
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var pending: CheckedContinuation<Bool, Never>?
        var restores = 0
        model.start(deviceID: "first", beforeStart: {
            await withCheckedContinuation { pending = $0 }
        }, onFinish: { restores += 1; return true })
        try await waitUntil { pending != nil }
        model.cancel()
        XCTAssertTrue(model.isBusy, "pending bypass may still change the engine")
        XCTAssertFalse(model.start(deviceID: "second", beforeStart: { false }, onFinish: { true }),
                       "new measurement cannot overlap old compensation")
        pending?.resume(returning: true)
        try await waitUntil { restores == 1 && !model.isBusy }
        XCTAssertEqual(audio.measured, [])
    }
    func testRestorationCompletionOwnsTheRunAndIsNotCancelled() async throws {
        let audio = ControlledRoomTuneAudio()
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var restoration: CheckedContinuation<Bool, Never>?
        var cancelledRestore = true
        model.start(deviceID: "mic", beforeStart: { true }, onFinish: {
            cancelledRestore = Task.isCancelled
            return await withCheckedContinuation { restoration = $0 }
        })
        try await waitUntil { restoration != nil }
        XCTAssertTrue(model.isBusy)
        XCTAssertNil(model.result, "result is published only after processing state is restored")
        model.cancel()
        restoration?.resume(returning: true)
        try await waitUntil { !model.isBusy }
        XCTAssertFalse(cancelledRestore)
        XCTAssertEqual(model.phase, .idle)
        XCTAssertNil(model.result)
    }
    func testUnconfirmedBypassStillRestoresThePriorState() async throws {
        let audio = ControlledRoomTuneAudio()
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var restores = 0
        model.start(deviceID: "mic", beforeStart: { false }, onFinish: { restores += 1; return true })
        try await waitUntil { !model.isBusy }
        XCTAssertEqual(restores, 1, "a missing acknowledgement does not prove the engine was unchanged")
        XCTAssertTrue(audio.measured.isEmpty)
    }

    func testPermissionDenialDoesNotTouchAudioOrBypass() async throws {
        let audio = ControlledRoomTuneAudio(); audio.granted = false
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var writes = 0
        model.start(deviceID: "mic", beforeStart: { writes += 1; return true }, onFinish: { writes += 1; return true })
        try await waitUntil { !model.isBusy }
        XCTAssertEqual(writes, 0); XCTAssertTrue(audio.measured.isEmpty)
        guard case .failed = model.phase else { return XCTFail("denial must be visible") }
    }

    func testImmediateCancellationDoesNotRequestPermission() async throws {
        let audio = ControlledRoomTuneAudio()
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        model.start(deviceID: "mic", beforeStart: { XCTFail("cancelled start entered bypass"); return true }, onFinish: { true })
        model.cancel()
        try await waitUntil { !model.isBusy }
        XCTAssertEqual(audio.permissionRequests, 0)
        XCTAssertEqual(model.phase, .idle)
    }

    func testLatePermissionAfterCancellationCannotStartAudio() async throws {
        let audio = ControlledRoomTuneAudio(); audio.holdPermission = true
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var writes = 0
        model.start(deviceID: "mic", beforeStart: { writes += 1; return true }, onFinish: { writes += 1; return true })
        try await waitUntil { audio.permission != nil }
        model.cancel(); model.cancel()
        audio.permission?.resume(returning: true)
        try await waitUntil { !model.isBusy }
        XCTAssertEqual(writes, 0); XCTAssertTrue(audio.measured.isEmpty)
        XCTAssertEqual(model.phase, .idle)
    }

    func testCancellationWaitsForInFlightAudioAndRestoresOnce() async throws {
        let audio = ControlledRoomTuneAudio(); audio.holdMeasure = true
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var restores = 0, finishedWaiting = false, restoreCancelled = true
        model.start(deviceID: "mic", beforeStart: { true }, onFinish: {
            restores += 1; restoreCancelled = Task.isCancelled; return true
        })
        try await waitUntil { audio.measurement != nil }
        let cancelled = Task { await model.cancelAndWait(); finishedWaiting = true }
        try await waitUntil { model.phase == .stopping }
        XCTAssertFalse(finishedWaiting)
        XCTAssertTrue(model.isBusy)
        model.strength = .high
        XCTAssertNil(model.result)
        audio.measurement?.resume(returning: [(100, 0), (200, 0)])
        await cancelled.value
        XCTAssertEqual(restores, 1); XCTAssertFalse(restoreCancelled)
        XCTAssertEqual(model.phase, .idle); XCTAssertNil(model.result)
        XCTAssertEqual(model.inputPeak, 0)
        XCTAssertEqual(audio.measured.count, 1)
    }

    func testSuccessfulRoundsRestoreBeforePublishingAndAllowRestart() async throws {
        let audio = ControlledRoomTuneAudio()
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var restores = 0
        for _ in 0..<2 {
            XCTAssertTrue(model.start(deviceID: "mic", beforeStart: { true }, onFinish: {
                XCTAssertNil(model.result)
                restores += 1; return true
            }))
            try await waitUntil { !model.isBusy }
            XCTAssertEqual(model.phase, .done)
            XCTAssertNotNil(model.result)
            XCTAssertEqual(model.result?.bands.count, 0)
        }
        XCTAssertEqual(audio.measured.count, 6); XCTAssertEqual(restores, 2)
    }

    func testAudioFailureAndRestoreFailureStayVisible() async throws {
        let audio = ControlledRoomTuneAudio(); audio.failMeasure = true
        let model = RoomTuneWorkflow(audio: audio, pause: {})
        var restores = 0
        model.start(deviceID: "mic", beforeStart: { true }, onFinish: { restores += 1; return true })
        try await waitUntil { !model.isBusy }
        guard case .failed(let message) = model.phase else { return XCTFail("audio failure must be visible") }
        XCTAssertEqual(message, "test capture failure")
        XCTAssertEqual(restores, 1); XCTAssertNil(model.result)
        audio.failMeasure = false
        model.start(deviceID: "mic", beforeStart: { true }, onFinish: { false })
        try await waitUntil { !model.isBusy }
        XCTAssertEqual(model.phase, .failed("Roomcut 처리 상태를 복구하지 못했습니다"))
        XCTAssertNil(model.result)
    }

    func testWorkQueueKeepsNativeStartAndCleanupOffTheMainThreadAndOrdered() async throws {
        let queue = RoomTuneWorkQueue()
        let started = expectation(description: "native start is blocked")
        let release = DispatchSemaphore(value: 0)
        var cleanupDone = false
        let start = Task {
            try await queue.perform {
                XCTAssertFalse(Thread.isMainThread)
                started.fulfill()
                XCTAssertEqual(release.wait(timeout: .now() + 3), .success)
            }
        }
        await fulfillment(of: [started], timeout: 1)
        let stop = Task {
            try await queue.perform { XCTAssertFalse(Thread.isMainThread) }
            cleanupDone = true
        }
        try await Task.sleep(nanoseconds: 20_000_000)
        XCTAssertFalse(cleanupDone, "cleanup cannot run through an unfinished native start")
        start.cancel()
        release.signal()
        try await start.value; try await stop.value
        XCTAssertTrue(cleanupDone)
    }
}

@MainActor
private final class ControlledRoomTuneAudio: RoomTuneAudio {
    var inputPeak: Float = 0.4
    var measured: [String] = []
    var granted = true, holdPermission = false, holdMeasure = false, failMeasure = false
    var permissionRequests = 0
    var permission: CheckedContinuation<Bool, Never>?
    var measurement: CheckedContinuation<[(freq: Double, db: Double)], Error>?
    func requestPermission() async -> Bool {
        permissionRequests += 1
        if holdPermission { return await withCheckedContinuation { permission = $0 } }
        return granted
    }
    func measure(deviceID: String) async throws -> [(freq: Double, db: Double)] {
        measured.append(deviceID)
        if failMeasure { throw NSError(domain: "test", code: 1, userInfo: [NSLocalizedDescriptionKey: "test capture failure"]) }
        if holdMeasure { return try await withCheckedThrowingContinuation { measurement = $0 } }
        return [(100, 0), (200, 0), (400, 0)]
    }
}
