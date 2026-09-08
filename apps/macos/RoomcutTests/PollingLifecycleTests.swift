import XCTest
import Combine
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class PollingLifecycleTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suite = ""

    override func setUp() async throws {
        suite = "roomcut-poll-tests-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suite)!
    }
    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }

    private func waitUntil(_ ready: () -> Bool) async throws {
        for _ in 0..<500 {
            if ready() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("pending read did not start")
    }

    func testStoppedPollCannotPublishOrClaimDefaultFromLateState() async throws {
        let client = PollingClient()
        client.holdState = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingState != nil }
        model.stopPolling()
        client.pendingState?.resume(returning: client.state)
        await refresh.value
        XCTAssertFalse(model.status.reachable)
        XCTAssertEqual(client.defaultClaims, 0)
    }

    func testStoppedPollCannotPublishLateConnectionError() async throws {
        let client = PollingClient()
        let model = RoomcutViewModel(client: client, defaults: defaults)
        await model.refreshNow()
        client.holdState = true
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingState != nil }
        model.stopPolling()
        client.pendingState?.resume(throwing: EngineClientError.transport(-1))
        await refresh.value
        XCTAssertTrue(model.status.reachable)
        XCTAssertNil(model.errorBanner)
    }

    func testStoppedPollCannotPublishLateParameterError() async throws {
        let client = PollingClient()
        client.holdParams = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingParams != nil }
        model.stopPolling()
        client.pendingParams?.resume(throwing: EngineClientError.transport(-2))
        await refresh.value
        XCTAssertNil(model.errorBanner)
    }

    func testHiddenAnalyzerDoesNotRepublishLateResult() async throws {
        let client = PollingClient()
        client.holdAnalysis = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setAnalyzerVisible(true)
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingAnalysis != nil }
        model.setAnalyzerVisible(false)
        client.pendingAnalysis?.resume(returning: .init(valid: true, sampleRate: 48000, channels: 2,
            framesAnalyzed: 2048, peakDb: -3, rmsDb: -12))
        await refresh.value
        XCTAssertNil(model.analysis)
    }

    func testStartThenStopDiscardsTheQueuedInitialPoll() async throws {
        let client = PollingClient()
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.startPolling()
        model.stopPolling()
        try await Task.sleep(nanoseconds: 10_000_000)
        XCTAssertEqual(client.stateReads, 0)
        XCTAssertEqual(client.defaultClaims, 0)
    }

    func testSlowDeviceReadDoesNotPauseStatusAndMeters() async throws {
        let client = PollingClient()
        client.holdDevices = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.startPolling()
        try await waitUntil { client.pendingDevices != nil }
        client.state.peak = 0.8
        try await Task.sleep(nanoseconds: 250_000_000)
        XCTAssertGreaterThanOrEqual(client.stateReads, 2)
        XCTAssertEqual(model.meters.displayPeak, 0.8, accuracy: 0.001)
        XCTAssertEqual(client.deviceReads, 1, "periodic polls must not accumulate device requests")
        print("held device read: status reads=\(client.stateReads), device reads=\(client.deviceReads), peak=\(model.meters.displayPeak)")
        model.stopPolling()
        client.holdDevices = false
        client.pendingDevices?.resume(returning: DeviceReadback(roomcutIsDefault: false))
        try await waitUntil { client.deviceReadsFinished == 1 }
    }

    func testDisconnectPreventsHeldDeviceDataFromRestoringTheUI() async throws {
        let client = PollingClient()
        client.holdDevices = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let first = Task { await model.refreshNow() }
        try await waitUntil { client.pendingDevices != nil }
        client.failState = true
        await model.refreshNow(waitForDevices: false)
        XCTAssertFalse(model.status.reachable)
        client.pendingDevices?.resume(returning: DeviceReadback(roomcutIsDefault: true,
            devices: [OutputDeviceChoice(uid: "old", name: "Old")], controls: .init(volume: 0.1)))
        await first.value
        try await waitUntil { !model.isDeviceReadPending }
        XCTAssertFalse(model.status.reachable)
        XCTAssertTrue(model.outputDevices.isEmpty)
        XCTAssertEqual(model.volume, 1)
    }

    func testDeviceSwitchRejectsOldReadbackBeforeApplyingNewValues() async throws {
        let client = PollingClient()
        client.holdDevices = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let first = Task { await model.refreshNow() }
        try await waitUntil { client.pendingDevices != nil }
        var volumes: [Double] = []
        let subscription = model.$volume.dropFirst().sink { volumes.append($0) }
        client.state.outputDeviceUID = "device-B"
        client.volume = 0.7
        await model.refreshNow(waitForDevices: false)
        XCTAssertEqual(model.status.outputDeviceUID, "device-B")
        client.pendingDevices?.resume(returning: DeviceReadback(roomcutIsDefault: false, controls: .init(volume: 0.1)))
        await first.value
        try await waitUntil { !model.isDeviceReadPending }
        XCTAssertEqual(client.deviceReads, 2)
        XCTAssertEqual(model.volume, 0.7)
        XCTAssertFalse(volumes.contains(0.1))
        subscription.cancel()
    }

    func testNewPollCanFinishBeforeTheStoppedPollReturns() async throws {
        let client = PollingClient()
        client.holdState = true
        let oldState = client.state
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let old = Task { await model.refreshNow() }
        try await waitUntil { client.pendingState != nil }
        let pending = client.pendingState
        client.pendingState = nil
        model.stopPolling()
        client.holdState = false
        client.state.outputDeviceUID = "device-B"
        await model.refreshNow()
        pending?.resume(returning: oldState)
        await old.value
        XCTAssertEqual(model.status.outputDeviceUID, "device-B")
    }

    func testVolumeEditOutlivesAnOlderDeviceReadback() async throws {
        let client = PollingClient()
        client.holdDevices = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingDevices != nil }
        model.beginVolumeEdit()
        model.setVolume(0.9)
        model.endVolumeEdit()
        client.pendingDevices?.resume(returning: DeviceReadback(roomcutIsDefault: false,
            controls: .init(volume: 0.5, balance: 0)))
        await refresh.value
        XCTAssertEqual(model.volume, 0.9)
        client.holdDevices = false
        await model.refreshNow()
        XCTAssertEqual(model.volume, 0.9)
    }

    func testCancelledManualRefreshDoesNotPublish() async throws {
        let client = PollingClient()
        client.holdState = true
        let model = RoomcutViewModel(client: client, defaults: defaults)
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingState != nil }
        refresh.cancel()
        client.pendingState?.resume(returning: client.state)
        await refresh.value
        XCTAssertFalse(model.status.reachable)
        XCTAssertEqual(client.defaultClaims, 0)
    }

    func testSlowDeviceReadCannotRelabelANewerSoundEdit() async throws {
        let client = PollingClient()
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1, defaults: defaults)
        await model.refreshNow()
        client.state.paramsRevision += 1
        client.holdDevices = true
        let refresh = Task { await model.refreshNow() }
        try await waitUntil { client.pendingDevices != nil }
        model.preampDb = -6
        model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        client.pendingDevices?.resume(returning: DeviceReadback(roomcutIsDefault: false))
        await refresh.value
        XCTAssertEqual(model.preampDb, -6)
        XCTAssertNil(model.activeBuiltinPresetId)
        XCTAssertEqual(model.currentPresetName, "Custom")
    }

    func testLiveDeviceQueueAllowsMainActorWorkDuringABlockedRead() async {
        let client = LiveEngineClient()
        let entered = expectation(description: "device read entered")
        let release = DispatchSemaphore(value: 0)
        var finished = false
        let read = Task {
            let value = await client.readDeviceData {
                XCTAssertFalse(Thread.isMainThread)
                entered.fulfill()
                _ = release.wait(timeout: .now() + 2)
                return DeviceReadback(roomcutIsDefault: false)
            }
            finished = true
            return value
        }
        await fulfillment(of: [entered], timeout: 2)
        XCTAssertFalse(finished, "main actor must run while device I/O is blocked")
        release.signal()
        _ = await read.value
    }

    func testPollerCadenceAndOldCompletionIsolation() throws {
        let poller = EnginePoller()
        let first = try XCTUnwrap(poller.begin())
        let now = Date(timeIntervalSince1970: 100)
        XCTAssertNil(poller.begin())
        XCTAssertTrue(poller.isDue(.controls, at: now))
        poller.didRead(.controls, in: first, at: now)
        XCTAssertFalse(poller.isDue(.controls, at: now.addingTimeInterval(0.499)))
        XCTAssertTrue(poller.isDue(.controls, at: now.addingTimeInterval(0.5)))
        XCTAssertTrue(poller.isDue(.controls, at: now.addingTimeInterval(-1)))
        poller.stop()
        let next = try XCTUnwrap(poller.begin())
        poller.finish(first)
        XCTAssertTrue(poller.isCurrent(next))
        XCTAssertTrue(poller.isDue(.controls, at: now))
        poller.finish(next)
    }
}

@MainActor
private final class PollingClient: @preconcurrency EngineClientProtocol {
    let presets = [EnginePreset(id: "flat", name: "Flat")]
    var holdState = false, holdParams = false, holdAnalysis = false, holdDevices = false, failState = false
    var pendingState: CheckedContinuation<EngineStatus, Error>?
    var pendingParams: CheckedContinuation<EngineParameters, Error>?
    var pendingAnalysis: CheckedContinuation<RoomcutAnalysisSnapshot, Error>?
    var pendingDevices: CheckedContinuation<DeviceReadback, Never>?
    var defaultClaims = 0
    var stateReads = 0
    var deviceReads = 0, deviceReadsFinished = 0
    var volume = 0.5
    var parameters = EngineParameters.flat
    var state: EngineStatus = {
        var value = EngineStatus()
        value.reachable = true; value.state = EngineStatus.running; value.presetId = "flat"
        value.outputDeviceUID = "device-A"; value.capabilities = EngineStatus.analyzerCapability
        return value
    }()
    func getState() async throws -> EngineStatus {
        stateReads += 1
        if failState { throw EngineClientError.transport(-1) }
        if holdState { return try await withCheckedThrowingContinuation { pendingState = $0 } }
        return state
    }
    func getParams() async throws -> EngineParameters {
        if holdParams { return try await withCheckedThrowingContinuation { pendingParams = $0 } }
        return parameters
    }
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot {
        if holdAnalysis { return try await withCheckedThrowingContinuation { pendingAnalysis = $0 } }
        throw EngineClientError.transport(-3)
    }
    func setPreset(_ presetId: String) async throws {}
    func setBypass(_ on: Bool) async throws {}
    func setKeepDefault(_ on: Bool) async throws {}
    func setParams(_ params: EngineParameters) async throws {
        parameters = params
        state.presetId = "custom"
        state.paramsRevision += 1
    }
    func outputDevices() -> [OutputDeviceChoice] { [] }
    func setOutputDevice(_ uid: String) async throws {}
    func volumeGet() -> Double? { volume }
    func volumeSet(_ scalar: Double) { volume = scalar }
    func balanceGet() -> Double? { 0 }
    func balanceSet(_ pan: Double) {}
    func makeRoomcutDefaultOutput() { defaultClaims += 1 }
    func deviceReadback(for uid: String, includeDevices: Bool, includeControls: Bool) async -> DeviceReadback {
        deviceReads += 1
        defer { deviceReadsFinished += 1 }
        if holdDevices {
            holdDevices = false // Delay this read only; a queued replacement must be allowed to finish.
            return await withCheckedContinuation { pendingDevices = $0 }
        }
        return DeviceReadback.capture(using: self, uid: uid, devices: includeDevices, controls: includeControls)
    }
}
