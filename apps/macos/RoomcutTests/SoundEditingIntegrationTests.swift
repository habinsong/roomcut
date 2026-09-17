import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class SoundEditingIntegrationTests: XCTestCase {
    private var suite = ""
    private var defaults: UserDefaults!

    override func setUp() async throws {
        (suite, defaults) = makeTestDefaults(name)
    }

    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }

    private func model(_ client: EditingTestClient, debounce: UInt64 = 1_000_000) async -> RoomcutViewModel {
        let model = RoomcutViewModel(client: client, debounceNanoseconds: debounce, defaults: defaults)
        await model.refreshNow()
        return model
    }

    private func waitUntil(_ predicate: () -> Bool, file: StaticString = #filePath, line: UInt = #line) async throws {
        for _ in 0..<600 {
            if predicate() { return }
            try await Task.sleep(nanoseconds: 5_000_000)
        }
        XCTFail("asynchronous operation did not finish", file: file, line: line)
    }

    // A measurement import replaces all six bands; one undo has to bring every
    // one of them back, not the last band it happened to write.
    func testMeasurementImportIsOneUndoStep() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        model.setParametricBand(0, ParametricBand(enabled: true, type: 0, freqHz: 500, gainDb: -2, q: 1))
        try await waitUntil { !model.isSoundWritePending }
        let before = model.parametric
        var lines: [String] = []
        var f = 20.0
        while f <= 20000 {
            lines.append(String(format: "%.2f,%.3f", f, 4 * exp(-pow(log2(f / 100), 2)) - 6 * exp(-pow(log2(f / 3000) / 0.5, 2))))
            f *= pow(2, 1.0 / 24)
        }
        let correction = try XCTUnwrap(model.importMeasurement(Data(lines.joined(separator: "\n").utf8)))
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.parametric, correction.bands)
        XCTAssertNotEqual(model.parametric, before)

        model.undoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.parametric, before, "one undo restores all six bands")
        XCTAssertEqual(client.params.parametric, before, "and the engine gets them back")
        model.redoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.parametric, correction.bands)
    }

    func testGestureUndoRedoUpdatesEngineAndMacroState() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        model.beginParameterEdit()
        model.setMacro(.bass, normalized: 0.3)
        try await waitUntil { !model.isSoundWritePending }
        model.setMacro(.bass, normalized: 0.7)
        try await waitUntil { !model.isSoundWritePending }
        model.endParameterEdit()
        try await waitUntil { !model.isSoundWritePending }
        let edited = client.params
        XCTAssertEqual(model.macroValues[.bass], 0.7)
        model.undoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params, .flat)
        XCTAssertTrue(model.macroValues.isEmpty)
        XCTAssertFalse(model.canUndoSound)
        XCTAssertTrue(model.canRedoSound)
        model.redoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params, edited)
        XCTAssertEqual(model.macroValues[.bass], 0.7)
    }

    func testContinuousGestureSendsIntermediateValuesAndStillUndoesOnce() async throws {
        let client = EditingTestClient()
        let model = await model(client, debounce: 50_000_000)
        model.beginParameterEdit()
        for index in 1...12 {
            model.preampDb = -Double(index) / 2
            model.schedulePushParams()
            try await Task.sleep(nanoseconds: 15_000_000)
        }
        XCTAssertFalse(client.parameterWrites.isEmpty, "continuous input must not postpone every write until mouse-up")
        model.endParameterEdit()
        try await waitUntil { !model.isSoundWritePending }
        model.undoSound()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params.preampDb, 0)
        XCTAssertFalse(model.canUndoSound)
    }

    func testLibraryPresetIdentityDoesNotCollideWithUserPresetName() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        let library = try XCTUnwrap(PresetLibrary.all.first { $0.name == "Bass Boost" })
        model.savedPresets = [SavedPreset(name: library.name, preampDb: -12, eqGainsDb: [0], outputGainDb: 0)]
        model.setDeviceAutoPreset(true)
        model.applySavedPreset(library)
        try await waitUntil { !model.isSoundWritePending }
        let token = model.presetPickerSelection
        XCTAssertEqual(token, PresetLibrary.token(for: library))
        XCTAssertEqual((defaults.dictionary(forKey: "com.roomcut.devicePresetMap") as? [String: String])?["test-device"], token)
        await model.applyPreset("flat")
        model.applyPickerSelection(token)
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params.preampDb, library.preampDb)
        XCTAssertEqual(model.currentPresetName, library.name)
    }

    func testSavedPresetNameSurvivesInitialParameterReadback() async throws {
        let client = EditingTestClient()
        let first = await model(client)
        first.preampDb = -4
        first.schedulePushParams()
        try await waitUntil { !first.isSoundWritePending }
        first.saveCurrentAsPreset(name: "Saved")
        let reopened = await model(client)
        XCTAssertEqual(reopened.currentPresetName, "Saved")
        XCTAssertEqual(reopened.preampDb, -4)
    }

    func testABSwitchSerializesBehindInFlightWrite() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        client.holdNextWrite = true
        model.preampDb = -3
        model.schedulePushParams()
        try await waitUntil { client.pendingWrite != nil }
        model.selectComparison(.b)
        XCTAssertEqual(model.preampDb, 0)
        client.releaseWrite()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params.preampDb, 0)
        XCTAssertEqual(model.editor.activeSlot, .b)
        XCTAssertEqual(client.maxConcurrentWrites, 1)
        model.selectComparison(.a)
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params.preampDb, -3)
    }

    func testLateReadbackCannotOverwriteNewComparisonSlot() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        client.holdNextRead = true
        model.preampDb = -3
        model.schedulePushParams()
        try await waitUntil { client.pendingRead != nil }
        model.selectComparison(.b)
        client.releaseRead()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.preampDb, 0)
        XCTAssertEqual(client.params.preampDb, 0)
    }

    func testStalePollCannotOverwriteAnEditStartedDuringRead() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        client.params.preampDb = 8
        client.status.paramsRevision += 1
        client.status.presetId = "custom"
        client.holdNextRead = true
        let poll = Task { await model.refreshNow() }
        try await waitUntil { client.pendingRead != nil }
        model.preampDb = 3
        model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        client.releaseRead()
        await poll.value
        XCTAssertEqual(model.preampDb, 3)
        XCTAssertEqual(client.params.preampDb, 3)
    }

    func testFailedLatestWriteRestoresEngineAndPreservesOtherSlot() async throws {
        let client = EditingTestClient()
        let model = await model(client)
        model.preampDb = -2
        model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        client.failNextWrite = true
        model.preampDb = -4
        model.schedulePushParams()
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.preampDb, -2)
        XCTAssertFalse(model.canUndoSound)
        XCTAssertNotNil(model.errorBanner)
        await model.refreshNow()
        XCTAssertNotNil(model.errorBanner, "a healthy status poll must not hide an unresolved write failure")
        model.selectComparison(.b)
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(client.params.preampDb, 0)
        XCTAssertNil(model.errorBanner)
    }

    func testPresetSelectionCancelsUnsentSliderWrite() async throws {
        let client = EditingTestClient()
        let model = await model(client, debounce: 100_000_000)
        model.preampDb = 4
        model.schedulePushParams()
        await model.applyPreset("bass")
        try await Task.sleep(nanoseconds: 150_000_000)
        XCTAssertEqual(client.parameterWrites.count, 0)
        XCTAssertEqual(client.presetWrites, ["bass"])
        XCTAssertEqual(model.eqGainsDb[0], 5)
        XCTAssertEqual(model.activeBuiltinPresetId, "bass")
    }

    func testLegacyPresetClearsPreviousParametricBands() async throws {
        let client = EditingTestClient()
        client.params.parametric[0] = ParametricBand(enabled: true, type: 5, freqHz: 1000, gainDb: 0, q: 3)
        let model = await model(client)
        model.applySavedPreset(SavedPreset(name: "Legacy", preampDb: 0, eqGainsDb: [0], outputGainDb: 0))
        try await waitUntil { !model.isSoundWritePending }
        XCTAssertEqual(model.eqGainsDb.count, 10)
        XCTAssertTrue(client.params.parametric.allSatisfy { !$0.enabled })
        XCTAssertEqual(model.currentPresetName, "Legacy")
    }

    func testParameterReadRetriesAfterBackoffWithoutLosingConnection() async throws {
        let client = EditingTestClient()
        client.failReads = true
        let model = await model(client)
        await model.refreshNow()
        XCTAssertEqual(client.readCount, 1)
        XCTAssertTrue(model.status.reachable)
        client.failReads = false
        client.params.preampDb = -5
        try await Task.sleep(nanoseconds: 550_000_000)
        await model.refreshNow()
        XCTAssertEqual(model.preampDb, -5)
        XCTAssertTrue(model.editor.hasBaseline)
        XCTAssertNil(model.errorBanner)
    }
}

@MainActor
private final class EditingTestClient: @preconcurrency EngineClientProtocol {
    let presets = [EnginePreset(id: "flat", name: "Flat"), EnginePreset(id: "bass", name: "Bass")]
    var params = EngineParameters.flat
    var status = EngineStatus()
    var parameterWrites: [EngineParameters] = []
    var presetWrites: [String] = []
    var holdNextWrite = false
    var failNextWrite = false
    var holdNextRead = false
    var failReads = false
    var readCount = 0
    var pendingWrite: CheckedContinuation<Void, Never>?
    var pendingRead: CheckedContinuation<Void, Never>?
    private var concurrentWrites = 0
    var maxConcurrentWrites = 0

    init() {
        status.reachable = true
        status.state = EngineStatus.running
        status.presetId = "flat"
        status.outputDeviceUID = "test-device"
        status.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.parametricCapability | EngineStatus.dynamicsCapability
    }
    func getState() async throws -> EngineStatus { status }
    func getParams() async throws -> EngineParameters {
        readCount += 1
        if failReads { throw EngineClientError.transport(-1) }
        let captured = params
        if holdNextRead {
            holdNextRead = false
            await withCheckedContinuation { pendingRead = $0 }
        }
        return captured
    }
    func setParams(_ params: EngineParameters) async throws {
        concurrentWrites += 1
        maxConcurrentWrites = max(maxConcurrentWrites, concurrentWrites)
        defer { concurrentWrites -= 1 }
        parameterWrites.append(params)
        if holdNextWrite {
            holdNextWrite = false
            await withCheckedContinuation { pendingWrite = $0 }
        }
        if failNextWrite { failNextWrite = false; throw EngineClientError.transport(-1) }
        self.params = params
        status.presetId = "custom"
        status.paramsRevision += 1
    }
    func setPreset(_ id: String) async throws {
        presetWrites.append(id)
        params = .flat
        if id == "bass" { params.eqGainsDb[0] = 5 }
        status.presetId = id
        status.paramsRevision += 1
    }
    func releaseWrite() { pendingWrite?.resume(); pendingWrite = nil }
    func releaseRead() { pendingRead?.resume(); pendingRead = nil }
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot { throw EngineClientError.transport(-1) }
    func setBypass(_ on: Bool) async throws { status.manualBypass = on }
    func setKeepDefault(_ on: Bool) async throws { status.keepDefault = on }
    func outputDevices() -> [OutputDeviceChoice] { [] }
    func setOutputDevice(_ uid: String) async throws { status.outputDeviceUID = uid }
    func volumeGet() -> Double? { 0.5 }
    func volumeSet(_ scalar: Double) {}
    func balanceGet() -> Double? { 0 }
    func balanceSet(_ pan: Double) {}
}
