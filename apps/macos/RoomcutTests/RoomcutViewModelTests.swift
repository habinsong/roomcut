import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class RoomcutViewModelTests: XCTestCase {
    func testInitialConnectLoadsStateAndParams() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()

        XCTAssertTrue(model.status.reachable)
        XCTAssertEqual(model.status.presetId, "flat")
        XCTAssertEqual(model.preampDb, 0)
        XCTAssertEqual(model.eqGainsDb, Array(repeating: 0, count: EngineParameters.bandCount))
        XCTAssertEqual(client.getParamsCount, 1)
    }

    func testPresetSuccessReadsBackAuthoritativeParams() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "night", revision: 1),
        ]
        client.params = EngineParameters(preampDb: -4, eqGainsDb: Array(repeating: 1, count: EngineParameters.bandCount), outputGainDb: -2)
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        await model.applyPreset("night")

        XCTAssertEqual(client.setPresetIds, ["night"])
        XCTAssertEqual(model.status.presetId, "night")
        XCTAssertEqual(model.preampDb, -4)
        XCTAssertEqual(model.eqGainsDb[0], 1)
        XCTAssertEqual(model.outputGainDb, -2)
    }

    func testDebounceCoalescesSliderWrites() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0), .running(presetId: "custom", revision: 1)]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 20_000_000)

        model.preampDb = 1
        model.schedulePushParams()
        model.preampDb = 2
        model.schedulePushParams()

        // The debounce fires on a real timer, so a busy CI runner can land the
        // coalesced write well after the nominal 20ms. Poll until it arrives
        // (~2s ceiling), then wait one more debounce window to confirm the two
        // edits collapsed into a single write rather than two.
        for _ in 0..<200 where client.setParamsValues.isEmpty {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        try await Task.sleep(nanoseconds: 60_000_000)

        XCTAssertEqual(client.setParamsValues.count, 1)
        XCTAssertEqual(client.setParamsValues.first?.preampDb, 2)
    }

    func testFailedWriteRestoresEngineParams() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "custom", revision: 3)]
        client.params = .flat
        client.failSetParams = true
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.preampDb = 9
        model.schedulePushParams()
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertEqual(model.preampDb, 0)
        XCTAssertEqual(model.errorBanner, "엔진 값을 다시 불러왔습니다")
    }

    func testRemoteRevisionDoesNotOverwriteActiveEdit() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "custom", revision: 1),
        ]
        client.params = EngineParameters(preampDb: -6, eqGainsDb: Array(repeating: -3, count: EngineParameters.bandCount), outputGainDb: -1)
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 5_000_000_000)

        await model.refreshNow()
        model.preampDb = 5
        model.schedulePushParams()
        await model.refreshNow()
        model.stopPolling()

        XCTAssertEqual(model.preampDb, 5)
        XCTAssertEqual(client.getParamsCount, 1)
    }

    func testReconnectClearsStaleStateAfterReadback() async {
        let client = FakeEngineClient()
        client.failNextState = true
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertFalse(model.status.reachable)
        XCTAssertEqual(model.errorBanner, "연결 끊김")

        client.states = [.running(presetId: "clean", revision: 4)]
        client.params = EngineParameters(preampDb: -1, eqGainsDb: Array(repeating: 0.5, count: EngineParameters.bandCount), outputGainDb: 1)
        await model.refreshNow()

        XCTAssertTrue(model.status.reachable)
        XCTAssertEqual(model.status.presetId, "clean")
        XCTAssertEqual(model.preampDb, -1)
        XCTAssertNil(model.errorBanner)
    }

    func testAnalysisRefreshesWhenEngineSupportsAnalyzer() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        client.analysis = RoomcutAnalysisSnapshot(
            valid: true,
            sampleRate: 48000,
            channels: 2,
            framesAnalyzed: 8192,
            peakDb: -2,
            rmsDb: -16,
            stereoWidth: 0.8,
            reverbEstimate: 0.3,
            dynamicRange: 14,
            spectrum: Array(repeating: 0.5, count: RoomcutAnalysisSnapshot.spectrumBinCount))
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        model.setAnalyzerVisible(true)

        await model.refreshNow()

        XCTAssertEqual(client.getAnalysisCount, 1)
        XCTAssertEqual(model.analysis?.stereoWidth, 0.8)
        XCTAssertEqual(RoomcutAnalysisPresentation.currentSound(for: model.analysis), "Wide · Safe")
    }

    func testAnalysisDoesNotRefreshWhenAnalyzerIsHidden() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()

        XCTAssertEqual(client.getAnalysisCount, 0)
        XCTAssertNil(model.analysis)
    }

    func testAnalysisClearsWhenAnalyzerUnsupported() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "flat", revision: 0, capabilities: 0),
        ]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        model.setAnalyzerVisible(true)

        await model.refreshNow()
        XCTAssertNotNil(model.analysis)

        await model.refreshNow()
        XCTAssertNil(model.analysis)
    }

    func testUnderrunWarningOnlyWhenClimbingDuringPlayback() {
        // First sample establishes a baseline — never warns.
        XCTAssertFalse(RoomcutViewModel.underrunsActive(previous: nil, current: 1_000_000, peak: 0.5))
        // Climbing while audio flows → real dropout.
        XCTAssertTrue(RoomcutViewModel.underrunsActive(previous: 1_000, current: 1_050, peak: 0.5))
        // Climbing during silence (idle empty ring) → not a dropout the user hears.
        XCTAssertFalse(RoomcutViewModel.underrunsActive(previous: 1_000, current: 1_050, peak: 0.0))
        // Steady counter during playback → no warning.
        XCTAssertFalse(RoomcutViewModel.underrunsActive(previous: 1_000, current: 1_000, peak: 0.5))
    }

    func testDisplayMetersHoldRecentPeakAndLimiterThroughShortSilence() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0, peak: 0.4, limiterGRDb: -1.6),
            .running(presetId: "flat", revision: 0, peak: 0, limiterGRDb: 0),
        ]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertEqual(model.meters.displayPeak, 0.4, accuracy: 0.0001)
        XCTAssertEqual(model.meters.displayLimiterGRDb, 1.6, accuracy: 0.0001)

        await model.refreshNow()
        // status.peak is intentionally NOT republished every poll (CPU fix); the
        // meter is fed the raw peak directly and holds it through short silence,
        // decaying gradually rather than snapping to 0.
        XCTAssertGreaterThan(model.meters.displayPeak, 0)
        XCTAssertLessThan(model.meters.displayPeak, 0.4)
        XCTAssertGreaterThan(model.meters.displayLimiterGRDb, 0.05)
    }

    func testDropoutDisplayHoldsAfterCounterBumpDuringPlayback() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0, peak: 0.3, underruns: 10),
            .running(presetId: "flat", revision: 0, peak: 0.3, underruns: 12),
            .running(presetId: "flat", revision: 0, peak: 0, underruns: 12),
        ]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertFalse(model.meters.underrunsActive)

        await model.refreshNow()
        XCTAssertTrue(model.meters.underrunsActive)

        await model.refreshNow()
        XCTAssertTrue(model.meters.underrunsActive)
    }

    func testRefreshLoadsDevicesAndVolume() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        client.params = .flat
        client.devices = [OutputDeviceChoice(uid: "A", name: "Speakers"),
                          OutputDeviceChoice(uid: "B", name: "DAC")]
        client.volume = 0.4
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()

        XCTAssertEqual(model.outputDevices.count, 2)
        XCTAssertTrue(model.hasVolumeControl)
        XCTAssertEqual(model.volume, 0.4, accuracy: 1e-9)
    }

    func testSetVolumeAllowsOutputBoostAboveHardwareRange() {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.setVolume(0.8)
        XCTAssertEqual(client.volume ?? -1, 0.8, accuracy: 1e-9) // pushed to the device now
        XCTAssertEqual(model.volume, 0.8, accuracy: 1e-9)

        model.setVolume(1.7)
        XCTAssertEqual(client.volume ?? -1, 1.7, accuracy: 1e-9)
        XCTAssertEqual(model.volume, 1.7, accuracy: 1e-9)

        model.setVolume(3.0)
        XCTAssertEqual(model.volume, 2.0, accuracy: 1e-9)
    }

    func testSpatialEditPreservesLastBuiltinPresetName() async throws {
        defer { UserDefaults.standard.removeObject(forKey: "com.roomcut.activeBuiltinPreset") }
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "night", revision: 1),
        ]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        await model.applyPreset("night")
        model.setSpatialValues(width: 35, centerFocus: 0, crossfeed: 4, roomReduce: 0)
        try await Task.sleep(nanoseconds: 40_000_000)
        model.status = .running(presetId: "custom", revision: 2)

        XCTAssertEqual(model.currentPresetName, "Night")
    }

    func testEqEditClearsLastBuiltinPresetName() async throws {
        defer { UserDefaults.standard.removeObject(forKey: "com.roomcut.activeBuiltinPreset") }
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "night", revision: 1),
        ]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        await model.applyPreset("night")
        model.setMacro(.bass, normalized: 0.5)
        try await Task.sleep(nanoseconds: 40_000_000)
        model.status = .running(presetId: "custom", revision: 2)

        XCTAssertEqual(model.currentPresetName, "Custom")
    }

    func testSetKeepDefaultForwards() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.setKeepDefault(true)
        try await Task.sleep(nanoseconds: 50_000_000)
        XCTAssertEqual(client.keepDefaultValues, [true])
    }

    func testSelectDeviceForwardsUID() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.selectDevice("iFiUSB")
        try await Task.sleep(nanoseconds: 50_000_000)
        XCTAssertEqual(client.setDeviceUIDs, ["iFiUSB"])
    }

    func testNoVolumeControlIsReflected() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        client.params = .flat
        client.volume = nil // device has no settable volume
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertFalse(model.hasVolumeControl)
    }

    func testParamReadFailureDoesNotMarkEngineOffline() async {
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0),
            .running(presetId: "flat", revision: 0),
        ]
        client.failGetParams = true
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()

        XCTAssertTrue(model.status.reachable)
        XCTAssertEqual(model.status.presentation.label, "실행 중")
        XCTAssertEqual(model.errorBanner, "엔진 값을 불러오지 못했습니다")

        await model.refreshNow()

        XCTAssertTrue(model.status.reachable)
        XCTAssertEqual(model.status.presentation.label, "실행 중")
        XCTAssertEqual(client.getParamsCount, 1)
    }

    // MARK: Macros (the knob-persistence bug fix) + saved presets

    // setMacro is incremental: re-applying the same value never double-counts,
    // and returning to 0 restores the EQ exactly.
    func testSetMacroIsIncrementalAndReversible() {
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.eqGainsDb = Array(repeating: 0, count: EngineParameters.bandCount)

        model.setMacro(.bass, normalized: 0.5)            // band 1 weight 6 → +3
        XCTAssertEqual(model.macroValues[.bass], 0.5)
        XCTAssertEqual(model.eqGainsDb[1], 3.0, accuracy: 1e-9)
        XCTAssertEqual(model.eqGainsDb[5], 0.0, accuracy: 1e-9) // non-target untouched

        model.setMacro(.bass, normalized: 0.5)            // same value
        XCTAssertEqual(model.eqGainsDb[1], 3.0, accuracy: 1e-9, "no double-count")

        model.setMacro(.bass, normalized: 0.0)            // back to rest
        XCTAssertEqual(model.eqGainsDb[1], 0.0, accuracy: 1e-9)
        XCTAssertEqual(model.macroValues[.bass], 0.0)
    }

    func testSavePresetOverwritesAndUsesCustomForEmpty() {
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.savedPresets = []

        XCTAssertTrue(model.saveCurrentAsPreset(name: "Mine"))
        XCTAssertTrue(model.saveCurrentAsPreset(name: "Mine"), "same name overwrites")
        XCTAssertTrue(model.saveCurrentAsPreset(name: "  mine "), "case/space same name overwrites")
        XCTAssertEqual(model.savedPresets.count, 1, "overwrites stay at one entry")

        XCTAssertTrue(model.saveCurrentAsPreset(name: "   "), "empty name saves as Custom")
        XCTAssertEqual(model.savedPresets.count, 2)
        XCTAssertTrue(model.savedPresets.contains { $0.name == "Custom" })

        UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets")
    }

    func testSavedPresetRoundTripsMacrosAndLimiter() {
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.savedPresets = []
        model.macroValues = [.bass: 0.5]
        model.eqGainsDb = Array(repeating: 1.5, count: EngineParameters.bandCount)
        model.limiterReleaseMs = 60.0

        XCTAssertTrue(model.saveCurrentAsPreset(name: "Full"))
        let saved = model.savedPresets.first { $0.name == "Full" }!

        // Mutate, then re-apply: macros + limiter + curve come back (item 2 sync).
        model.macroValues = [:]
        model.limiterReleaseMs = 100.0
        model.eqGainsDb = Array(repeating: 0, count: EngineParameters.bandCount)
        model.applySavedPreset(saved)
        XCTAssertEqual(model.macroValues[.bass], 0.5)
        XCTAssertEqual(model.limiterReleaseMs, 60.0)
        XCTAssertEqual(model.eqGainsDb[0], 1.5)

        UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets")
    }

    func testApplySavedPresetSetsParamsAndResetsMacros() {
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.status = .running(presetId: "custom", revision: 1)
        model.macroValues = [.bass: 0.4]
        model.applySavedPreset(SavedPreset(
            name: "X", preampDb: -3,
            eqGainsDb: Array(repeating: 2, count: EngineParameters.bandCount),
            outputGainDb: 1.5,
            spatialWidth: -35,
            centerFocus: 28,
            crossfeed: 12,
            roomReduce: 55))
        XCTAssertEqual(model.preampDb, -3)
        XCTAssertEqual(model.eqGainsDb[0], 2)
        XCTAssertEqual(model.outputGainDb, 1.5)
        XCTAssertEqual(model.spatialWidth, -35)
        XCTAssertEqual(model.centerFocus, 28)
        XCTAssertEqual(model.crossfeed, 12)
        XCTAssertEqual(model.roomReduce, 55)
        XCTAssertTrue(model.macroValues.isEmpty, "macro deltas reset to the new base")
    }

    func testSpatialControlsDoNotPushWhenEngineLacksSupport() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0, capabilities: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setSpatialValues(width: -35, centerFocus: 28, crossfeed: 12, roomReduce: 55)
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertFalse(model.spatialAvailable)
        XCTAssertEqual(model.spatialWidth, 0)
        XCTAssertEqual(model.centerFocus, 0)
        XCTAssertEqual(model.crossfeed, 0)
        XCTAssertEqual(model.roomReduce, 0)
        XCTAssertTrue(client.setParamsValues.isEmpty)
        XCTAssertEqual(model.errorBanner, "현재 엔진이 Spatial을 지원하지 않습니다")
    }

    func testSpatialControlsPushWhenEngineSupportsSpatial() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setSpatialValues(width: -35, centerFocus: 28, crossfeed: 12, roomReduce: 55)
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertEqual(client.setParamsValues.count, 1)
        XCTAssertEqual(client.setParamsValues.first?.spatialWidth, -35)
        XCTAssertEqual(client.setParamsValues.first?.centerFocus, 28)
        XCTAssertEqual(client.setParamsValues.first?.crossfeed, 12)
        XCTAssertEqual(client.setParamsValues.first?.roomReduce, 55)
    }

    func testParametricBandPushesAndClamps() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        // Out-of-range values must clamp to the validator bounds.
        model.setParametricBand(0, ParametricBand(enabled: true, type: 2,
                                                  freqHz: 50000, gainDb: 99, q: 0.001))
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertTrue(model.parametricAvailable)
        XCTAssertEqual(model.parametric[0].enabled, true)
        XCTAssertEqual(model.parametric[0].type, 2)
        XCTAssertEqual(model.parametric[0].freqHz, 20000)  // clamped to max
        XCTAssertEqual(model.parametric[0].gainDb, 24)     // clamped to max
        XCTAssertEqual(model.parametric[0].q, 0.1)         // clamped to min
        XCTAssertEqual(client.setParamsValues.count, 1)
        XCTAssertEqual(client.setParamsValues.first?.parametric[0].enabled, true)
        XCTAssertEqual(client.setParamsValues.first?.parametric[0].freqHz, 20000)
    }

    func testParametricDoesNotPushWhenEngineLacksSupport() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0, capabilities: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setParametricBand(0, ParametricBand(enabled: true, type: 0, freqHz: 1000, gainDb: 6, q: 1))
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertFalse(model.parametricAvailable)
        XCTAssertEqual(model.parametric[0].enabled, false)
        XCTAssertTrue(client.setParamsValues.isEmpty)
        XCTAssertEqual(model.errorBanner, "현재 엔진이 Parametric EQ를 지원하지 않습니다")
    }

    func testSavedPresetRoomTuneInfoRoundtripsAndBackCompat() throws {
        let preset = SavedPreset(name: "Room Tune", preampDb: 0,
                                 eqGainsDb: Array(repeating: 0, count: 10), outputGainDb: 0,
                                 roomTuneInfo: "2026 · iPhone → iFi · 3밴드")
        let data = try JSONEncoder().encode(preset)
        let back = try JSONDecoder().decode(SavedPreset.self, from: data)
        XCTAssertEqual(back.roomTuneInfo, "2026 · iPhone → iFi · 3밴드")
        // Older saves without the key decode safely to nil (back-compat).
        let legacy = #"{"name":"X","preampDb":0,"eqGainsDb":[0,0,0,0,0,0,0,0,0,0],"outputGainDb":0}"#
        let old = try JSONDecoder().decode(SavedPreset.self, from: Data(legacy.utf8))
        XCTAssertNil(old.roomTuneInfo)
    }
}

final class FakeEngineClient: EngineClientProtocol {
    var presets: [EnginePreset] = [
        EnginePreset(id: "flat", name: "Flat"),
        EnginePreset(id: "clean", name: "Clean"),
        EnginePreset(id: "night", name: "Night"),
    ]
    var states: [EngineStatus] = []
    var params = EngineParameters.flat
    var failNextState = false
    var failGetParams = false
    var failSetParams = false
    var setPresetIds: [String] = []
    var setParamsValues: [EngineParameters] = []
    var getParamsCount = 0

    func getState() async throws -> EngineStatus {
        if failNextState {
            failNextState = false
            throw EngineClientError.transport(-1)
        }
        if states.isEmpty {
            return .running(presetId: "flat", revision: 0)
        }
        return states.removeFirst()
    }

    func getParams() async throws -> EngineParameters {
        getParamsCount += 1
        if failGetParams {
            throw EngineClientError.transport(-3)
        }
        return params
    }

    var analysis = RoomcutAnalysisSnapshot(
        valid: true,
        sampleRate: 48000,
        channels: 2,
        framesAnalyzed: 4096,
        peakDb: -3,
        rmsDb: -18,
        stereoWidth: 0.7,
        dynamicRange: 15,
        spectrum: Array(repeating: 0.4, count: RoomcutAnalysisSnapshot.spectrumBinCount))
    var failGetAnalysis = false
    var getAnalysisCount = 0

    func getAnalysis() async throws -> RoomcutAnalysisSnapshot {
        getAnalysisCount += 1
        if failGetAnalysis {
            throw EngineClientError.transport(-4)
        }
        return analysis
    }

    func setPreset(_ presetId: String) async throws {
        setPresetIds.append(presetId)
    }

    func setBypass(_ on: Bool) async throws {}
    var keepDefaultValues: [Bool] = []
    func setKeepDefault(_ on: Bool) async throws { keepDefaultValues.append(on) }

    func setParams(_ params: EngineParameters) async throws {
        if failSetParams {
            throw EngineClientError.transport(-2)
        }
        setParamsValues.append(params)
    }

    var devices: [OutputDeviceChoice] = []
    var setDeviceUIDs: [String] = []
    var volume: Double? = 0.5
    func outputDevices() -> [OutputDeviceChoice] { devices }
    func setOutputDevice(_ uid: String) async throws { setDeviceUIDs.append(uid) }
    func volumeGet() -> Double? { volume }
    func volumeSet(_ scalar: Double) { volume = scalar }
}

private extension EngineStatus {
    static func running(
        presetId: String,
        revision: UInt32,
        peak: Float = 0,
        limiterGRDb: Float = 0,
        underruns: UInt64 = 0,
        capabilities: UInt32 = EngineStatus.spatialParamsCapability
            | EngineStatus.parametricCapability
            | EngineStatus.analyzerCapability
    ) -> EngineStatus {
        var status = EngineStatus()
        status.reachable = true
        status.state = EngineStatus.running
        status.presetId = presetId
        status.paramsRevision = revision
        status.peak = peak
        status.limiterGRDb = limiterGRDb
        status.underruns = underruns
        status.capabilities = capabilities
        return status
    }
}
