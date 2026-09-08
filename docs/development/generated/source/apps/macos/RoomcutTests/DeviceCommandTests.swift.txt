import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class DeviceCommandTests: XCTestCase {
    private var suite = ""
    private var defaults: UserDefaults!
    override func setUp() async throws {
        suite = "roomcut-device-command-tests-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suite)!
    }
    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }
    private func waitUntil(_ predicate: () -> Bool) async throws {
        for _ in 0..<1000 {
            if predicate() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("device operation did not finish")
    }
    private func initialized(_ client: DeviceCommandClient) async throws -> RoomcutViewModel {
        let model = RoomcutViewModel(client: client, defaults: defaults)
        await model.refreshNow()
        try await waitUntil { !client.defaultChanges.isEmpty }
        client.defaultChanges.removeAll()
        return model
    }

    func testLatestMasterIntentWinsAcrossSuspendedAcknowledgement() async throws {
        let client = DeviceCommandClient()
        let model = try await initialized(client)
        client.holdKeepOn = true
        model.setMasterEnabled(true)
        try await waitUntil { client.pendingKeepOn != nil }
        model.setMasterEnabled(false)
        try await Task.sleep(nanoseconds: 20_000_000)
        client.pendingKeepOn?.resume()
        try await waitUntil { client.defaultChanges.count == 2 }
        XCTAssertEqual(client.defaultChanges.last, false)
        XCTAssertFalse(client.isDefault)
        XCTAssertEqual(client.maxKeepWrites, 1)
    }

    func testOlderDeviceFailureCannotOverwriteNewerSuccess() async throws {
        let client = DeviceCommandClient()
        let model = try await initialized(client)
        client.holdOldDevice = true
        model.selectDevice("old-device")
        try await waitUntil { client.pendingOldDevice != nil }
        model.selectDevice("new-device")
        try await Task.sleep(nanoseconds: 20_000_000)
        client.pendingOldDevice?.resume(throwing: EngineClientError.transport(-1))
        try await waitUntil { client.state.outputDeviceUID == "new-device" }
        try await Task.sleep(nanoseconds: 20_000_000)
        XCTAssertNil(model.errorBanner)
    }

    func testAuthoritativeReadbackCanRemoveOldBoost() async throws {
        let client = DeviceCommandClient()
        client.volume = 1.8; client.state.volumeBoost = 1.8
        let model = try await initialized(client)
        XCTAssertEqual(model.volume, 1.8)
        client.volume = 1; client.state.volumeBoost = 1
        client.state.outputDeviceUID = "new-device"
        await model.refreshNow()
        XCTAssertEqual(model.volume, 1)
    }

    func testContinuousVolumeWritesCoalesceWithoutLosingBalance() async throws {
        let client = DeviceCommandClient()
        let model = try await initialized(client)
        client.holdVolume = true
        model.setVolume(0.1)
        try await waitUntil { client.pendingVolume != nil }
        model.setBalance(0.2)
        for index in 2...9 { model.setVolume(Double(index) / 10) }
        XCTAssertEqual(model.volume, 0.9)
        await model.refreshNow()
        XCTAssertEqual(model.volume, 0.9, "polling cannot snap back a pending optimistic edit")
        client.pendingVolume?.resume()
        try await waitUntil { !model.isDeviceWritePending }
        XCTAssertEqual(client.volumeWrites, [0.1, 0.9])
        XCTAssertEqual(client.balanceWrites, [0.2])
        XCTAssertEqual(client.maxVolumeWrites, 1)
    }

    func testCurrentFailureSurvivesPollingUntilNewSuccess() async throws {
        let client = DeviceCommandClient()
        let model = try await initialized(client)
        client.failVolume = true
        model.setVolume(0.7)
        try await waitUntil { !model.isDeviceWritePending }
        XCTAssertEqual(model.errorBanner, "볼륨을 변경하지 못했습니다")
        await model.refreshNow()
        XCTAssertEqual(model.errorBanner, "볼륨을 변경하지 못했습니다")
        model.setVolume(0.4)
        try await waitUntil { !model.isDeviceWritePending }
        await model.refreshNow()
        XCTAssertNil(model.errorBanner)
        XCTAssertEqual(model.volume, 0.4)
    }

    func testAutomaticClaimCannotHideManualFailure() async throws {
        let client = DeviceCommandClient()
        let writer = DeviceCommandWriter(client: client)
        client.failVolume = true
        writer.submit(.volume(0.4))
        writer.submit(.claimDefault)
        try await waitUntil { !writer.isBusy }
        XCTAssertEqual(writer.error, "볼륨을 변경하지 못했습니다")
    }

    func testCancelDropsPendingWritesAndUnsentCompoundStep() async throws {
        let client = DeviceCommandClient()
        let writer = DeviceCommandWriter(client: client)
        client.holdKeepOn = true
        writer.submit(.master(true))
        try await waitUntil { client.pendingKeepOn != nil }
        writer.submit(.volume(0.8))
        writer.cancel()
        client.pendingKeepOn?.resume()
        try await waitUntil { !writer.isBusy }
        XCTAssertTrue(client.state.keepDefault, "an acknowledged native step was not magically undone")
        XCTAssertTrue(client.defaultChanges.isEmpty)
        XCTAssertTrue(client.volumeWrites.isEmpty)
        XCTAssertNil(writer.error)
    }

    func testExplicitOffPreventsStartupFromTurningOnLater() async throws {
        let client = DeviceCommandClient()
        let model = RoomcutViewModel(client: client, defaults: defaults)
        model.setMasterEnabled(false)
        try await waitUntil { !model.isDeviceWritePending }
        await model.refreshNow()
        XCTAssertEqual(client.defaultChanges, [false])
        XCTAssertFalse(model.roomcutIsDefault)
    }

    func testFormatAndOutputRemainOrderedWhileFormatsCoalesce() async throws {
        let client = DeviceCommandClient()
        let writer = DeviceCommandWriter(client: client)
        client.holdKeepOn = true
        writer.submit(.master(true))
        try await waitUntil { client.pendingKeepOn != nil }
        writer.submit(.format(uid: "initial-device", sampleRate: 48000, bitDepth: 16))
        writer.submit(.format(uid: "initial-device", sampleRate: 96000, bitDepth: 24))
        writer.submit(.output("new-device"))
        client.pendingKeepOn?.resume()
        try await waitUntil { !writer.isBusy }
        XCTAssertEqual(client.formatWrites, ["initial-device:96000:24"])
        XCTAssertEqual(client.state.outputDeviceUID, "new-device")
        XCTAssertEqual(client.defaultChanges, [true, true])
    }

    func testLiveWriteQueueAllowsMainActorProgress() async throws {
        let client = LiveEngineClient()
        let entered = expectation(description: "native write entered")
        let release = DispatchSemaphore(value: 0)
        var completed = false
        let write = Task {
            try await client.writeDeviceData {
                XCTAssertFalse(Thread.isMainThread)
                entered.fulfill()
                _ = release.wait(timeout: .now() + 2)
            }
            completed = true
        }
        await fulfillment(of: [entered], timeout: 2)
        XCTAssertFalse(completed)
        release.signal()
        try await write.value
    }

    func testLiveDefaultOutputAcceptsChangedAndAlreadySelected() async throws {
        for result: Int32 in [0, 1] {
            for roomcut in [true, false] {
                let client = LiveEngineClient(changeDefaultOutput: { requested in
                    XCTAssertEqual(requested, roomcut)
                    XCTAssertFalse(Thread.isMainThread)
                    return result
                })
                do { try await client.setDefaultOutput(roomcut: roomcut) }
                catch { XCTFail("successful native result \(result) became \(error)") }
            }
        }
    }

    func testLiveDefaultOutputPreservesFailures() async throws {
        for result: Int32 in [-1, -2, 2] {
            let client = LiveEngineClient(changeDefaultOutput: { _ in result })
            do {
                try await client.setDefaultOutput(roomcut: true)
                XCTFail("failed native result was accepted")
            } catch { XCTAssertEqual(error as? EngineClientError, .transport(result)) }
        }
    }

    func testAlreadySelectedOutputDoesNotCreateAClaimError() async throws {
        let client = LiveEngineClient(changeDefaultOutput: { _ in 1 })
        let writer = DeviceCommandWriter(client: client)
        writer.submit(.claimDefault)
        try await waitUntil { !writer.isBusy }
        XCTAssertNil(writer.error)
    }

    func testBlockedHardwareWriteDoesNotHoldTheStateRequestQueue() async throws {
        let client = LiveEngineClient()
        let entered = expectation(description: "hardware write entered")
        let stateRequest = expectation(description: "state queue remains available")
        let release = DispatchSemaphore(value: 0)
        let write = Task {
            try await client.writeDeviceData {
                entered.fulfill()
                _ = release.wait(timeout: .now() + 2)
            }
        }
        await fulfillment(of: [entered], timeout: 2)
        let state = Task { try await client.runOnQueue { stateRequest.fulfill() } }
        await fulfillment(of: [stateRequest], timeout: 0.5)
        release.signal()
        try await write.value
        try await state.value
    }
}

@MainActor
private final class DeviceCommandClient: @preconcurrency EngineClientProtocol {
    let presets = [EnginePreset(id: "flat", name: "Flat")]
    var state: EngineStatus = {
        var value = EngineStatus()
        value.reachable = true; value.state = EngineStatus.running; value.presetId = "flat"
        value.outputDeviceUID = "initial-device"
        return value
    }()
    var volume = 0.5, balance = 0.0
    var isDefault = false
    var defaultChanges: [Bool] = []
    var holdKeepOn = false, holdOldDevice = false
    var pendingKeepOn: CheckedContinuation<Void, Never>?
    var pendingOldDevice: CheckedContinuation<Void, Error>?
    var keepWrites = 0, maxKeepWrites = 0
    var holdVolume = false, failVolume = false
    var pendingVolume: CheckedContinuation<Void, Never>?
    var volumeWrites: [Double] = [], balanceWrites: [Double] = []
    var formatWrites: [String] = []
    var volumeWritesActive = 0, maxVolumeWrites = 0

    func getState() async throws -> EngineStatus { state }
    func getParams() async throws -> EngineParameters { .flat }
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot { throw EngineClientError.transport(-1) }
    func setPreset(_ presetId: String) async throws {}
    func setBypass(_ on: Bool) async throws {}
    func setParams(_ params: EngineParameters) async throws {}
    func setKeepDefault(_ on: Bool) async throws {
        keepWrites += 1; maxKeepWrites = max(maxKeepWrites, keepWrites)
        defer { keepWrites -= 1 }
        if on && holdKeepOn {
            holdKeepOn = false
            await withCheckedContinuation { pendingKeepOn = $0 }
        }
        state.keepDefault = on
    }
    func outputDevices() -> [OutputDeviceChoice] { [] }
    func setOutputDevice(_ uid: String) async throws {
        if uid == "old-device" && holdOldDevice {
            try await withCheckedThrowingContinuation { pendingOldDevice = $0 }
        }
        state.outputDeviceUID = uid
    }
    func volumeGet() -> Double? { volume }
    func volumeSet(_ scalar: Double) { volume = scalar }
    func balanceGet() -> Double? { balance }
    func balanceSet(_ pan: Double) { balance = pan; balanceWrites.append(pan) }
    func makeRoomcutDefaultOutput() { isDefault = true; defaultChanges.append(true) }
    func restoreRealDefaultOutput() { isDefault = false; defaultChanges.append(false) }
    func roomcutIsDefaultOutput() -> Bool { isDefault }
    func writeVolume(_ scalar: Double) async throws {
        volumeWritesActive += 1; maxVolumeWrites = max(maxVolumeWrites, volumeWritesActive)
        defer { volumeWritesActive -= 1 }
        volumeWrites.append(scalar)
        if holdVolume {
            holdVolume = false
            await withCheckedContinuation { pendingVolume = $0 }
        }
        if failVolume { failVolume = false; throw EngineClientError.transport(-1) }
        volumeSet(scalar)
    }
    func setDeviceFormat(uid: String, sampleRate: Double, bitDepth: Int) async throws {
        formatWrites.append("\(uid):\(Int(sampleRate)):\(bitDepth)")
    }
}
