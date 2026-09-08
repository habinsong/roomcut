import XCTest
@testable import RoomcutCore

@MainActor
final class BypassOwnershipTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suite = ""
    override func setUp() async throws {
        suite = "roomcut-bypass-tests-\(UUID())"
        defaults = UserDefaults(suiteName: suite)!
    }
    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }
    private func waitUntil(_ condition: () -> Bool) async throws {
        for _ in 0..<1000 {
            if condition() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("bypass operation did not finish")
    }
    private func readback(_ bypass: Bool) -> EngineStatus {
        var state = EngineStatus()
        state.reachable = true; state.presetId = "flat"
        state.state = bypass ? EngineStatus.bypass : EngineStatus.running
        state.manualBypass = bypass
        return state
    }
    private func expect(_ expected: Bool, file: StaticString = #filePath, line: UInt = #line,
                        _ operation: @MainActor () async -> Bool) async {
        let actual = await operation()
        XCTAssertEqual(actual, expected, file: file, line: line)
    }

    func testUserChoiceDuringCancelledMeasurementIsNotRestoredAway() async throws {
        let client = FakeEngineClient()
        var first: CheckedContinuation<Void, Error>?
        var writes: [Bool] = []
        var actual = false
        client.stateReadHandler = { self.readback(actual) }
        client.bypassWriteHandler = { value in
            writes.append(value)
            if writes.count == 1 { try await withCheckedThrowingContinuation { first = $0 } }
            actual = value
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let measurement = RoomTuneWorkflow(audio: NoRecordingAudio(), pause: {})
        let bypass = model.makeBypassOverride()
        measurement.start(deviceID: "mic", beforeStart: { await bypass.begin() }, onFinish: { await bypass.restore() })
        try await waitUntil { first != nil }
        measurement.cancel()
        model.setBypass(true)
        first?.resume()
        try await waitUntil { !measurement.isBusy && !model.isBypassWritePending && writes.count >= 2 }
        print("measurement cancellation + user bypass: writes=\(writes), actual=\(actual)")
        XCTAssertTrue(actual, "old measurement cleanup cannot re-enable processing after the user bypassed it")
        XCTAssertTrue(model.status.manualBypass)
        XCTAssertFalse(writes.contains(false), "superseded restoration must not send a stale value")
    }

    func testDirectUserWritesCannotCompleteOutOfOrder() async throws {
        let client = FakeEngineClient()
        var first: CheckedContinuation<Void, Error>?
        var writes: [Bool] = []
        var actual = false
        client.stateReadHandler = { self.readback(actual) }
        client.bypassWriteHandler = { value in
            writes.append(value)
            if writes.count == 1 { try await withCheckedThrowingContinuation { first = $0 } }
            actual = value
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setBypass(true)
        try await waitUntil { first != nil }
        model.setBypass(false)
        try await Task.sleep(nanoseconds: 20_000_000)
        XCTAssertEqual(writes.count, 1, "only one bypass request can be in flight")
        first?.resume()
        try await waitUntil { writes.count >= 2 && !model.isBypassWritePending }
        XCTAssertFalse(actual, "the last user choice owns the final engine state")
        XCTAssertFalse(model.status.manualBypass)
    }

    func testNormalOverrideRestoresBothPriorStatesAndRestoresOnlyOnce() async throws {
        for prior in [false, true] {
            let client = FakeEngineClient()
            var actual = prior, writes: [Bool] = []
            client.stateReadHandler = { self.readback(actual) }
            client.bypassWriteHandler = { actual = $0; writes.append($0) }
            let model = RoomcutViewModel(client: client, defaults: defaults)
            await model.refreshNow()
            let bypass = model.makeBypassOverride()
            await expect(true) { await bypass.begin() }
            XCTAssertTrue(actual)
            await expect(false) { await bypass.begin() }
            await expect(true) { await bypass.restore() }
            await expect(true) { await bypass.restore() }
            XCTAssertEqual(actual, prior)
            XCTAssertEqual(writes, [true, prior])
        }
    }

    func testPendingManualChoiceIsNewerThanThePolledSnapshot() async throws {
        let client = FakeEngineClient()
        var first: CheckedContinuation<Void, Error>?
        var actual = false, writes: [Bool] = []
        client.stateReadHandler = { self.readback(actual) }
        client.bypassWriteHandler = { value in
            writes.append(value)
            if writes.count == 1 { try await withCheckedThrowingContinuation { first = $0 } }
            actual = value
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setBypass(true)
        try await waitUntil { first != nil }
        XCTAssertFalse(model.status.manualBypass)
        let bypass = model.makeBypassOverride()
        let begin = Task { await bypass.begin() }
        await Task.yield()
        first?.resume()
        await expect(true) { await begin.value }
        await expect(true) { await bypass.restore() }
        XCTAssertTrue(actual, "restore the pending user choice, not stale polling data")
    }

    func testUserChoiceDuringInFlightRestorationRunsLast() async throws {
        let client = FakeEngineClient()
        var restoration: CheckedContinuation<Void, Error>?
        var actual = false, writes: [Bool] = []
        client.stateReadHandler = { self.readback(actual) }
        client.bypassWriteHandler = { value in
            writes.append(value)
            if writes.count == 2 { try await withCheckedThrowingContinuation { restoration = $0 } }
            actual = value
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let bypass = model.makeBypassOverride()
        await expect(true) { await bypass.begin() }
        let restore = Task { await bypass.restore() }
        try await waitUntil { restoration != nil }
        model.setBypass(true)
        restoration?.resume()
        await expect(true) { await restore.value }
        try await waitUntil { !model.isBypassWritePending }
        XCTAssertTrue(actual)
        XCTAssertEqual(writes, [true, false, true], "already sent writes finish, but the latest choice still runs last")
    }

    func testRapidManualWritesKeepOnlyTheLatestPendingValue() async throws {
        let client = FakeEngineClient()
        var first: CheckedContinuation<Void, Error>?
        var writes: [Bool] = []
        client.bypassWriteHandler = { value in
            writes.append(value)
            if writes.count == 1 { try await withCheckedThrowingContinuation { first = $0 } }
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setBypass(true)
        try await waitUntil { first != nil }
        for index in 0..<100 { model.setBypass(index.isMultiple(of: 2)) }
        first?.resume()
        try await waitUntil { !model.isBypassWritePending }
        XCTAssertEqual(writes, [true, false])
    }

    func testCurrentFailureSurvivesPollingAndOlderSuccess() async throws {
        let client = FakeEngineClient()
        var first: CheckedContinuation<Void, Error>?
        var count = 0
        client.bypassWriteHandler = { _ in
            count += 1
            if count == 1 { try await withCheckedThrowingContinuation { first = $0 } }
            if count == 2 { throw EngineClientError.transport(-2) }
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setBypass(true)
        try await waitUntil { first != nil }
        model.setBypass(false)
        first?.resume()
        try await waitUntil { !model.isBypassWritePending }
        XCTAssertNotNil(model.errorBanner)
        await model.refreshNow()
        XCTAssertNotNil(model.errorBanner)
        await expect(true) { await model.updateBypass(true) }
        XCTAssertNil(model.errorBanner)
    }

    func testAnOverrideCannotRestoreAnotherOwnersState() async throws {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let first = model.makeBypassOverride(), other = model.makeBypassOverride()
        await expect(true) { await first.begin() }
        await expect(false) { await other.begin() }
        await expect(true) { await other.restore() }
        XCTAssertEqual(client.bypassValues, [true])
        await expect(true) { await first.restore() }
        XCTAssertEqual(client.bypassValues, [true, false])
    }
    func testAlreadyCancelledOverrideDoesNotWrite() async throws {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let bypass = model.makeBypassOverride()
        let begin = Task { await bypass.begin() }
        begin.cancel()
        await expect(false) { await begin.value }
        await expect(true) { await bypass.restore() }
        XCTAssertTrue(client.bypassValues.isEmpty)
    }

    func testFailedAcknowledgementStillCompensatesAnAppliedTemporaryWrite() async throws {
        let client = FakeEngineClient()
        var actual = false, count = 0
        client.stateReadHandler = { self.readback(actual) }
        client.bypassWriteHandler = { value in
            count += 1; actual = value
            if count == 1 { throw EngineClientError.transport(-2) }
        }
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let bypass = model.makeBypassOverride()
        await expect(false) { await bypass.begin() }
        XCTAssertTrue(actual)
        await expect(true) { await bypass.restore() }
        XCTAssertFalse(actual)
        XCTAssertEqual(count, 2)
    }
}

@MainActor
private final class NoRecordingAudio: RoomTuneAudio {
    var inputPeak: Float { 0 }
    func requestPermission() async -> Bool { true }
    func measure(deviceID: String) async throws -> [(freq: Double, db: Double)] {
        XCTFail("cancelled measurement must not record"); return []
    }
}
