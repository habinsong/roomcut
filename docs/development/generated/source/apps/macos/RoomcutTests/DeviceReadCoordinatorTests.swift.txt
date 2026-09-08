import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class DeviceReadCoordinatorTests: XCTestCase {
    private func waitUntil(_ predicate: () -> Bool) async throws {
        for _ in 0..<500 {
            if predicate() { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("device read did not reach the expected state")
    }

    func testRepeatedPollsShareOneReadAndOnlyLatestDeviceWaits() async throws {
        let client = SuspendedDeviceReader()
        let reader = DeviceReadCoordinator(client: client)
        var accepted: [String] = []
        reader.didRead = { request, _ in accepted.append(request.uid) }
        let first = reader.request(.init(uid: "A", revision: 1, devices: true, controls: true))
        try await waitUntil { client.requests.count == 1 }
        for _ in 0..<100 {
            XCTAssertTrue(reader.request(.init(uid: "A", revision: 1, devices: true, controls: true)) === first)
        }
        let replaced = reader.request(.init(uid: "B", revision: 2, devices: true, controls: true))
        let latest = reader.request(.init(uid: "C", revision: 3, devices: true, controls: true))
        XCTAssertTrue(first.isCancelled)
        XCTAssertTrue(replaced.isCancelled)
        XCTAssertEqual(client.requests.map(\.uid), ["A"])
        client.finish()
        try await waitUntil { client.requests.count == 2 }
        XCTAssertEqual(client.requests.map(\.uid), ["A", "C"])
        XCTAssertTrue(accepted.isEmpty)
        client.finish()
        await latest.wait()
        XCTAssertEqual(accepted, ["C"])
    }

    func testStopAndRestartDoNotOverlapTheNativeReads() async throws {
        let client = SuspendedDeviceReader()
        let reader = DeviceReadCoordinator(client: client)
        var accepted: [String] = []
        reader.didRead = { request, _ in accepted.append(request.uid) }
        let old = reader.request(.init(uid: "A", revision: 0, devices: true, controls: true))
        try await waitUntil { client.requests.count == 1 }
        reader.cancel()
        await old.wait()
        let new = reader.request(.init(uid: "A", revision: 1, devices: true, controls: true))
        XCTAssertEqual(client.requests.count, 1)
        client.finish()
        try await waitUntil { client.requests.count == 2 }
        XCTAssertTrue(accepted.isEmpty)
        client.finish()
        await new.wait()
        XCTAssertEqual(accepted, ["A"])
        XCTAssertEqual(client.maximumActive, 1)
    }

    func testAdditionalSectionsMergeIntoOnePendingRead() async throws {
        let client = SuspendedDeviceReader()
        let reader = DeviceReadCoordinator(client: client)
        var completions = 0
        reader.didRead = { _, _ in completions += 1 }
        let first = reader.request(.init(uid: "A", revision: 1, devices: false, controls: false))
        try await waitUntil { client.requests.count == 1 }
        let controls = reader.request(.init(uid: "A", revision: 1, devices: false, controls: true))
        let both = reader.request(.init(uid: "A", revision: 1, devices: true, controls: true))
        XCTAssertTrue(controls === both)
        client.finish()
        await first.wait()
        try await waitUntil { client.requests.count == 2 }
        XCTAssertTrue(client.requests[1].devices && client.requests[1].controls)
        client.finish()
        await both.wait()
        XCTAssertEqual(completions, 2)
    }

    func testCancelledWaitersFinishAndNativeCompletionIsIgnored() async throws {
        let client = SuspendedDeviceReader()
        let reader = DeviceReadCoordinator(client: client)
        var completions = 0
        reader.didRead = { _, _ in completions += 1 }
        let ticket = reader.request(.init(uid: "A", revision: 1, devices: true, controls: true))
        try await waitUntil { client.requests.count == 1 }
        let first = Task { await ticket.wait() }
        let second = Task.detached { await ticket.wait() }
        first.cancel()
        await first.value
        await second.value
        XCTAssertTrue(ticket.isCancelled)
        reader.cancel()
        client.finish()
        try await waitUntil { client.active == 0 }
        XCTAssertEqual(completions, 0)
    }
}

@MainActor
private final class SuspendedDeviceReader: @preconcurrency EngineClientProtocol {
    struct Request { let uid: String; let devices: Bool; let controls: Bool }
    let presets: [EnginePreset] = []
    var requests: [Request] = []
    var active = 0, maximumActive = 0
    private var pending: CheckedContinuation<DeviceReadback, Never>?
    func finish() {
        let next = pending
        pending = nil
        next?.resume(returning: DeviceReadback(roomcutIsDefault: false))
    }
    func deviceReadback(for uid: String, includeDevices: Bool, includeControls: Bool) async -> DeviceReadback {
        active += 1; maximumActive = max(maximumActive, active)
        defer { active -= 1 }
        requests.append(Request(uid: uid, devices: includeDevices, controls: includeControls))
        return await withCheckedContinuation { pending = $0 }
    }
    func getState() async throws -> EngineStatus { EngineStatus() }
    func getParams() async throws -> EngineParameters { .flat }
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot { throw EngineClientError.transport(-1) }
    func setPreset(_ presetId: String) async throws {}
    func setBypass(_ on: Bool) async throws {}
    func setKeepDefault(_ on: Bool) async throws {}
    func setParams(_ params: EngineParameters) async throws {}
    func outputDevices() -> [OutputDeviceChoice] { [] }
    func setOutputDevice(_ uid: String) async throws {}
    func volumeGet() -> Double? { nil }
    func volumeSet(_ scalar: Double) {}
    func balanceGet() -> Double? { nil }
    func balanceSet(_ pan: Double) {}
}
