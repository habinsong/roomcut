import XCTest
import Combine
import RoomcutPresentationCore
@testable import RoomcutCore

@MainActor
final class SoundComparisonTests: XCTestCase {
    private var suite = ""
    private var defaults: UserDefaults!

    override func setUp() async throws {
        suite = "roomcut-comparison-tests-\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suite)!
    }
    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }

    private func model(_ client: ComparisonTestClient) async -> RoomcutViewModel {
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000, defaults: defaults)
        await model.refreshNow()
        return model
    }
    private func waitUntil(_ predicate: () -> Bool, file: StaticString = #filePath, line: UInt = #line) async throws {
        for _ in 0..<500 {
            if predicate() { return }
            try await Task.sleep(nanoseconds: 5_000_000)
        }
        XCTFail("operation did not complete", file: file, line: line)
    }

    func testToggleAndCopyDoNotAlterPresetValuesOrUndoHistory() async throws {
        let client = ComparisonTestClient()
        let model = await model(client)
        model.preampDb = 6
        model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertTrue(model.canUndoSound)
        let before = model.editor.snapshot
        model.toggleLevelMatch()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertTrue(model.comparison.enabled)
        XCTAssertEqual(model.editor.snapshot, before)
        XCTAssertEqual(client.value.current.preampDb, 6)
        XCTAssertEqual(client.value.reference.preampDb, 0)
        model.copyComparison()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.value.current, client.value.reference)
        model.toggleLevelMatch()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertFalse(client.value.enabled)
        XCTAssertEqual(client.legacyWrites, 0, "new engines receive complete atomic comparison updates")
        model.undoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.preampDb, 0)
        XCTAssertFalse(model.canUndoSound, "listening-utility toggles do not add undo steps")
    }

    func testActiveAndReferenceStayPairedDuringAnInFlightSwitch() async throws {
        let client = ComparisonTestClient()
        let model = await model(client)
        model.preampDb = 6; model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        client.holdNextWrite = true
        model.toggleLevelMatch()
        try await waitUntil { client.writeHeld }
        model.selectComparison(.b)
        client.releaseWrite()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.maximumWrites, 1)
        XCTAssertTrue(client.value.enabled)
        XCTAssertEqual(client.value.current.preampDb, 0)
        XCTAssertEqual(client.value.reference.preampDb, 6)
        XCTAssertEqual(model.editor.activeSlot, .b)
        XCTAssertEqual(model.editor.referenceSnapshot.parameters.preampDb, 6)
    }

    func testReconnectRestoresTheAuthoritativeComparisonPair() async throws {
        let client = ComparisonTestClient()
        client.value.current.preampDb = 6
        client.value.reference.preampDb = -6
        client.value.enabled = true
        client.value.state = .matched
        client.value.currentReductionDb = 12
        let model = await model(client)
        XCTAssertTrue(model.comparison.enabled)
        XCTAssertEqual(model.comparison.state, .matched)
        XCTAssertEqual(model.comparison.reductionDb, 12)
        XCTAssertEqual(model.preampDb, 6)
        XCTAssertEqual(model.editor.referenceSnapshot.parameters.preampDb, -6)
    }

    func testFailedUtilityTogglePreservesExistingHistoryAndRestoresEngineMode() async throws {
        let client = ComparisonTestClient()
        let model = await model(client)
        model.preampDb = 6; model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        client.failNextWrite = true
        model.toggleLevelMatch()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertFalse(model.comparison.enabled)
        XCTAssertTrue(model.canUndoSound)
        XCTAssertEqual(model.preampDb, 6)
        XCTAssertNotNil(model.errorBanner)
    }

    func testPresetUsesOneTransactionAndKeepsTheReference() async throws {
        let client = ComparisonTestClient()
        let model = await model(client)
        model.toggleLevelMatch()
        try await waitUntil { !model.isSoundWritePending }
        let count = client.comparisonWrites
        await model.applyPreset("bass")
        XCTAssertEqual(client.comparisonWrites, count + 1)
        XCTAssertEqual(client.legacyWrites, 0)
        XCTAssertTrue(client.value.enabled)
        XCTAssertEqual(client.value.reference, .flat)
        XCTAssertEqual(client.value.current.eqGainsDb[0], 6)
        XCTAssertEqual(model.activeBuiltinPresetId, "bass")
    }

    func testOldEngineUsesTheExistingParameterPath() async throws {
        let client = ComparisonTestClient()
        client.supported = false
        let model = await model(client)
        model.toggleLevelMatch()
        XCTAssertFalse(model.comparison.enabled)
        XCTAssertEqual(client.comparisonReads, 0)
        model.preampDb = -3; model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.legacyWrites, 1)
        XCTAssertEqual(client.comparisonWrites, 0)
    }

    func testLatePairedReadCannotOverwriteANewerSelection() async throws {
        let client = ComparisonTestClient()
        client.value.current.preampDb = 6
        client.value.enabled = true
        let model = await model(client)
        client.holdNextRead = true
        client.value.revision += 1
        let read = Task { await model.refreshNow() }
        try await waitUntil { client.readHeld }
        model.selectComparison(.b)
        try await waitUntil { !model.isSoundWritePending }
        client.releaseRead()
        await read.value
        XCTAssertEqual(model.editor.activeSlot, .b)
        XCTAssertEqual(model.preampDb, 0)
        XCTAssertEqual(model.editor.referenceSnapshot.parameters.preampDb, 6)
    }

    func testMeterUpdatesDoNotRepublishTheWholeEditor() async throws {
        let client = ComparisonTestClient()
        client.value.enabled = true
        let model = await model(client)
        let revision = model.editor.revision
        var rootUpdates = 0
        let subscription = model.objectWillChange.sink { rootUpdates += 1 }
        client.value.state = .matched
        client.value.currentReductionDb = 6.04
        try await Task.sleep(nanoseconds: 270_000_000)
        await model.refreshNow()
        XCTAssertEqual(model.comparison.state, .matched)
        XCTAssertEqual(model.comparison.reductionDb, 6)
        XCTAssertEqual(model.editor.revision, revision)
        XCTAssertEqual(rootUpdates, 0)
        subscription.cancel()
    }

    func testStaleReadyMetricsAreShownAsMeasuring() {
        let state = SoundComparison()
        state.setSupported(true)
        state.accept(EngineComparisonState(enabled: true, state: .matched, revision: 8, renderedRevision: 7))
        XCTAssertEqual(state.state, .measuring)
        state.accept(EngineComparisonState(enabled: true, state: .noSignal, revision: 8, renderedRevision: 7))
        XCTAssertEqual(state.state, .noSignal)
    }

    func testPendingEditInvalidatesReadyMeterState() async throws {
        let client = ComparisonTestClient()
        client.value.enabled = true; client.value.state = .matched
        let model = await model(client)
        XCTAssertEqual(model.comparison.state, .matched)
        client.holdNextWrite = true
        model.preampDb = 3
        model.schedulePushParams()
        XCTAssertEqual(model.comparison.state, .measuring, "old metrics cannot describe a newly queued edit")
        try await waitUntil { client.writeHeld }
        client.releaseWrite()
        try await waitUntil { !model.isSoundWritePending }
    }
}

@MainActor
private final class ComparisonTestClient: @preconcurrency EngineClientProtocol {
    let presets = [EnginePreset(id: "flat", name: "Flat"), EnginePreset(id: "bass", name: "Bass")]
    var value = EngineComparisonState(presetID: "flat", revision: 1, renderedRevision: 1)
    var supported = true
    var comparisonReads = 0, comparisonWrites = 0, legacyWrites = 0
    var activeWrites = 0, maximumWrites = 0
    var holdNextWrite = false, holdNextRead = false, failNextWrite = false
    var writeHeld: Bool { writeContinuation != nil }
    var readHeld: Bool { readContinuation != nil }
    private var writeContinuation: CheckedContinuation<Void, Never>?
    private var readContinuation: CheckedContinuation<Void, Never>?

    func getState() async throws -> EngineStatus {
        var state = EngineStatus()
        state.reachable = true; state.state = EngineStatus.running
        state.presetId = value.presetID; state.paramsRevision = UInt32(truncatingIfNeeded: value.revision)
        state.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.parametricCapability | EngineStatus.dynamicsCapability
        if supported { state.capabilities |= EngineStatus.levelMatchCapability }
        return state
    }
    func getParams() async throws -> EngineParameters { value.current }
    func getComparison() async throws -> EngineComparisonState {
        comparisonReads += 1
        let snapshot = value
        if holdNextRead {
            holdNextRead = false
            await withCheckedContinuation { readContinuation = $0 }
        }
        return snapshot
    }
    func setComparison(_ target: EngineComparisonTarget, reference: EngineParameters, enabled: Bool) async throws {
        comparisonWrites += 1; activeWrites += 1; maximumWrites = max(maximumWrites, activeWrites)
        defer { activeWrites -= 1 }
        if holdNextWrite {
            holdNextWrite = false
            await withCheckedContinuation { writeContinuation = $0 }
        }
        if failNextWrite { failNextWrite = false; throw EngineClientError.transport(-1) }
        switch target {
        case .parameters(let parameters):
            if value.current != parameters { value.presetID = "custom" }
            value.current = parameters
        case .preset(let id):
            guard presets.contains(where: { $0.id == id }) else { throw EngineClientError.transport(1) }
            value.current = .flat
            if id == "bass" { value.current.eqGainsDb[0] = 6 }
            value.presetID = id
        }
        value.reference = reference; value.enabled = enabled; value.revision += 1
        value.state = enabled ? .measuring : .disabled
    }
    func setParams(_ parameters: EngineParameters) async throws {
        legacyWrites += 1; value.current = parameters; value.enabled = false; value.presetID = "custom"; value.revision += 1
    }
    func setPreset(_ id: String) async throws { legacyWrites += 1; value.current = .flat; value.presetID = id; value.enabled = false; value.revision += 1 }
    func releaseWrite() { writeContinuation?.resume(); writeContinuation = nil }
    func releaseRead() { readContinuation?.resume(); readContinuation = nil }
    func setBypass(_ on: Bool) async throws {}
    func setKeepDefault(_ on: Bool) async throws {}
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot { throw EngineClientError.transport(-1) }
    func outputDevices() -> [OutputDeviceChoice] { [] }
    func setOutputDevice(_ uid: String) async throws {}
    func volumeGet() -> Double? { 0.5 }
    func volumeSet(_ scalar: Double) {}
    func balanceGet() -> Double? { 0 }
    func balanceSet(_ pan: Double) {}
}
