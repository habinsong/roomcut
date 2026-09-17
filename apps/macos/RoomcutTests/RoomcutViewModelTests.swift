import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class RoomcutViewModelTests: XCTestCase {
    private func waitForDeviceWrites(_ model: RoomcutViewModel) async throws {
        for _ in 0..<1000 {
            if !model.isDeviceWritePending { return }
            try await Task.sleep(nanoseconds: 1_000_000)
        }
        XCTFail("device writes did not finish")
    }
    func testAwaitedBypassReportsAcknowledgedWrite() async {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client)
        let success = await model.updateBypass(true)
        XCTAssertTrue(success)
        XCTAssertEqual(client.bypassValues, [true])
    }

    func testAwaitedBypassRejectsFailedWrite() async {
        let client = FakeEngineClient()
        client.failSetBypass = true
        let model = RoomcutViewModel(client: client)
        let success = await model.updateBypass(true)
        XCTAssertFalse(success)
        XCTAssertNotNil(model.errorBanner)
        XCTAssertTrue(client.bypassValues.isEmpty)
    }

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
        client.setParamsDelayNanoseconds = 80_000_000
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.preampDb = 9
        model.schedulePushParams()
        XCTAssertTrue(model.isSoundWritePending)
        for _ in 0..<200 where model.isSoundWritePending {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertFalse(model.isSoundWritePending, "wait for the failed write and recovery, not a fixed debounce delay")

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
        XCTAssertFalse(RefreshPlanner.underrunsActive(previous: nil, current: 1_000_000, peak: 0.5))
        // Climbing while audio flows → real dropout.
        XCTAssertTrue(RefreshPlanner.underrunsActive(previous: 1_000, current: 1_050, peak: 0.5))
        // Climbing during silence (idle empty ring) → not a dropout the user hears.
        XCTAssertFalse(RefreshPlanner.underrunsActive(previous: 1_000, current: 1_050, peak: 0.0))
        // Steady counter during playback → no warning.
        XCTAssertFalse(RefreshPlanner.underrunsActive(previous: 1_000, current: 1_000, peak: 0.5))
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

    func testSetVolumeAllowsOutputBoostAboveHardwareRange() async throws {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.setVolume(0.8)
        XCTAssertEqual(model.volume, 0.8, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.volume ?? -1, 0.8, accuracy: 1e-9)

        model.setVolume(1.7)
        XCTAssertEqual(model.volume, 1.7, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.volume ?? -1, 1.7, accuracy: 1e-9)

        model.setVolume(3.0)
        XCTAssertEqual(model.volume, 2.0, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.volume, 2.0)
        model.stopPolling()
    }

    func testSetBalanceForwardsToClientAndClamps() async throws {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.setBalance(0.4)   // pan right
        XCTAssertEqual(model.balance, 0.4, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.balance ?? -2, 0.4, accuracy: 1e-9)

        model.setBalance(-0.7)  // pan left
        XCTAssertEqual(model.balance, -0.7, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.balance ?? -2, -0.7, accuracy: 1e-9)

        model.setBalance(2.5)   // clamps to +1 (full right)
        XCTAssertEqual(model.balance, 1.0, accuracy: 1e-9)
        model.setBalance(-2.5)  // clamps to -1 (full left)
        XCTAssertEqual(model.balance, -1.0, accuracy: 1e-9)
        try await waitForDeviceWrites(model)
        XCTAssertEqual(client.balance, -1.0)
        model.stopPolling()
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

    func testDeviceListChangeRereadsDevicesImmediately() async throws {
        // Hardware that appears between polls must not wait for the next tick:
        // connecting headphones and not finding them in the list reads as a bug.
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        client.devices = [OutputDeviceChoice(uid: "A", name: "Speakers")]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertEqual(model.outputDevices.count, 1)

        // A device arrives; the poller's device slot would otherwise be fresh
        // for another five seconds.
        client.devices = [OutputDeviceChoice(uid: "A", name: "Speakers"),
                          OutputDeviceChoice(uid: "BT", name: "AirPods Pro")]
        await model.refreshNow()
        XCTAssertEqual(model.outputDevices.count, 1, "an unexpired device cache is not re-read on its own")

        model.deviceListDidChange()
        for _ in 0..<40 where model.outputDevices.count < 2 {
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        XCTAssertEqual(model.outputDevices.map(\.uid), ["A", "BT"],
                       "a CoreAudio device-list change re-reads the list at once")
    }

    func testEngineWithoutRoomInItsComparisonPayloadDoesNotResetTheRoom() async throws {
        // Whenever the engine supports level matching, the app reads its
        // parameters from the comparison payload. An engine built before the
        // virtual room sends none, and adopting its zeros snapped a freshly
        // picked room straight back to off.
        let caps = EngineStatus.spatialParamsCapability
            | EngineStatus.virtualRoomCapability
            | EngineStatus.levelMatchCapability
        final class Revision: @unchecked Sendable { var value: UInt32 = 1 }
        let revision = Revision()
        let client = FakeEngineClient()
        client.offersComparison = true
        client.comparisonCarriesRoom = false        // pre-room engine
        client.stateReadHandler = { .running(presetId: "custom", revision: revision.value, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        XCTAssertTrue(model.virtualRoomAvailable)

        model.setRoomType(2)
        XCTAssertEqual(model.roomType, 2, "the picked room lands in the model immediately")
        for _ in 0..<40 where client.setComparisonCalls.isEmpty {
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        // The engine applied something, so the app re-reads its parameters —
        // this is the moment the room used to disappear.
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.roomType, 2, "a payload without a room leaves the chosen room alone")

        // An engine that reports its own room is authoritative again.
        client.comparisonCarriesRoom = true
        client.params.roomType = 0
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.roomType, 0, "the engine's own room wins once it reports one")
    }

    func testSurroundIsOneControlOverTwoFields() async throws {
        // Surround reads as one switch but is two fields on the wire: the older
        // ambience surround is a bit inside spatialMode, the 5.1/7.1 upmix is
        // surroundType. Letting the UI set them separately is what left them out
        // of step — both on, or neither. Every change goes through one call now.
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
        let client = FakeEngineClient()
        client.stateReadHandler = { .running(presetId: "custom", revision: 1, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setSpatialOutput(headphone: true)

        XCTAssertEqual(model.surroundChoice, .off)
        XCTAssertEqual(model.surroundChoices, [.off, .ambience, .virtual51, .virtual71],
                       "headphones can render a real layout")

        model.setSurroundChoice(.ambience)
        XCTAssertEqual(model.surroundChoice, .ambience)
        XCTAssertTrue(model.spatialSurroundOn)
        XCTAssertEqual(model.surroundType, 0, "ambience is not a layout")

        model.setSurroundChoice(.virtual71)
        XCTAssertEqual(model.surroundChoice, .virtual71)
        XCTAssertEqual(model.surroundType, 3)
        XCTAssertFalse(model.spatialSurroundOn, "a layout retires the ambience surround")

        // Off means off — both fields, not just the one the UI last touched.
        model.setSurroundChoice(.off)
        XCTAssertEqual(model.surroundChoice, .off)
        XCTAssertEqual(model.surroundType, 0)
        XCTAssertFalse(model.spatialSurroundOn)
    }

    func testSurroundFollowsTheOutputItIsPlayingOn() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
        let client = FakeEngineClient()
        client.stateReadHandler = { .running(presetId: "custom", revision: 1, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()

        model.setSpatialOutput(headphone: true)
        model.setSurroundChoice(.virtual71)
        XCTAssertEqual(model.surroundType, 3)

        // Speakers have no rear, so 7.1 is not on offer there and a carried-over
        // 7.1 must land on something the control can actually show.
        model.setSpatialOutput(headphone: false)
        XCTAssertEqual(model.surroundChoices, [.off, .ambience, .virtual51])
        XCTAssertEqual(model.surroundType, 2, "7.1 folds to the one speaker layout")
        XCTAssertNotNil(model.surroundChoices.firstIndex(of: model.surroundChoice),
                        "whatever is selected is always in the list the control renders")

        // The upmix itself still runs on speakers — folded, not rendered binaurally.
        XCTAssertTrue(model.upmixAvailable)
    }

    // What each Surround button actually sends: the reported "every choice
    // sounds the same" had to be ruled out on the app side too, so the pushed
    // parameters are checked, not just the model's own state.
    func testEverySurroundChoiceReachesTheEngine() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
        let client = FakeEngineClient()
        client.stateReadHandler = { .running(presetId: "custom", revision: 1, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        let cases: [(headphone: Bool, choice: RoomcutViewModel.SurroundChoice, mode: Double, layout: Double)] = [
            (true, .off, 1, 0), (true, .ambience, 2, 0), (true, .virtual51, 1, 2), (true, .virtual71, 1, 3),
            (false, .off, 0, 0), (false, .ambience, 3, 0), (false, .virtual51, 0, 2),
        ]
        for c in cases {
            model.setSpatialOutput(headphone: c.headphone)
            model.setSurroundChoice(c.choice)
            for _ in 0..<80 {
                if let last = client.setParamsValues.last, last.spatialMode == c.mode, last.surroundType == c.layout { break }
                try await Task.sleep(nanoseconds: 25_000_000)
            }
            let last = try XCTUnwrap(client.setParamsValues.last)
            XCTAssertEqual(last.spatialMode, c.mode, "\(c.headphone ? "headphone" : "speaker") \(c.choice) sends spatialMode \(c.mode)")
            XCTAssertEqual(last.surroundType, c.layout, "\(c.headphone ? "headphone" : "speaker") \(c.choice) sends surroundType \(c.layout)")
        }
    }

    func testCrossfeedStepsAsideWhenSomethingElsePlacesTheSpeakers() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
        let client = FakeEngineClient()
        client.stateReadHandler = { .running(presetId: "custom", revision: 1, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()

        model.setSpatialOutput(headphone: true)
        XCTAssertTrue(model.crossfeedActive, "on its own the crossfeed is the only stage")

        model.setSurroundChoice(.virtual51)
        XCTAssertFalse(model.crossfeedActive,
                       "the engine zeroes it while the upmix places speakers, so the slider hides")

        model.setSurroundChoice(.off)
        XCTAssertTrue(model.crossfeedActive)

        // On speakers `crossfeed` means crosstalk cancellation, a different job
        // that the upmix fold does not replace.
        model.setSpatialOutput(headphone: false)
        model.setSurroundChoice(.virtual51)
        XCTAssertTrue(model.crossfeedActive, "speaker XTC is unrelated to the fold")
    }

    func testSteeringControlsAreOnlyOfferedWhereTheyDoSomething() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
        let client = FakeEngineClient()
        client.stateReadHandler = { .running(presetId: "custom", revision: 1, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()

        // Both run 0...100 — they steer material, so there is no such thing as a
        // negative amount of it.
        model.setCenterWidth(-40)
        XCTAssertEqual(model.centerWidth, 0)
        model.setCenterWidth(500)
        XCTAssertEqual(model.centerWidth, 100)
        model.setSurroundDepth(-1)
        XCTAssertEqual(model.surroundDepth, 0)
        model.setSurroundDepth(140)
        XCTAssertEqual(model.surroundDepth, 100)

        model.setSpatialOutput(headphone: true)
        XCTAssertTrue(model.centerWidthApplies)
        XCTAssertTrue(model.surroundDepthApplies)
        // Two speakers have no discrete centre channel, so the control folds
        // back to where it started and measurably changes nothing. Speakers
        // widen the pair itself rather than an upmix, so depth does nothing
        // there either (measured identical at 20, 50 and 85).
        model.setSpatialOutput(headphone: false)
        XCTAssertFalse(model.centerWidthApplies)
        XCTAssertFalse(model.surroundDepthApplies)
    }

    func testUpmixNeedsAnEngineThatRendersIt() async throws {
        let client = FakeEngineClient()
        client.stateReadHandler = {
            .running(presetId: "custom", revision: 1, capabilities: EngineStatus.spatialParamsCapability)
        }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        XCTAssertFalse(model.upmixAvailable, "an engine without the upmix cannot be asked for one")
        XCTAssertEqual(model.surroundChoices, [.off, .ambience],
                       "only the ambience surround is on offer")
    }

    func testEngineWithoutUpmixInItsComparisonPayloadDoesNotResetIt() async throws {
        // Same hazard the virtual room hit: an engine one version behind sends
        // no upmix, and adopting its zeros would switch off a layout the user
        // just picked.
        let caps = EngineStatus.spatialParamsCapability
            | EngineStatus.upmixCapability
            | EngineStatus.levelMatchCapability
        final class Revision: @unchecked Sendable { var value: UInt32 = 1 }
        let revision = Revision()
        let client = FakeEngineClient()
        client.offersComparison = true
        client.comparisonCarriesUpmix = false
        client.stateReadHandler = { .running(presetId: "custom", revision: revision.value, capabilities: caps) }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setSpatialOutput(headphone: true)
        XCTAssertTrue(model.upmixAvailable)

        model.setSurroundType(2)
        XCTAssertEqual(model.surroundType, 2)
        for _ in 0..<40 where client.setComparisonCalls.isEmpty {
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.surroundType, 2, "a payload without an upmix leaves the layout alone")

        client.comparisonCarriesUpmix = true
        client.params.surroundType = 0
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.surroundType, 0, "the engine's own layout wins once it reports one")
    }

    func testHeadTrackingRefusesAnEngineThatCannotRenderIt() async throws {
        // Asking for head tracking on an engine without the renderer must say so
        // rather than silently streaming poses nobody reads.
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0,
                                  capabilities: EngineStatus.spatialParamsCapability)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setSpatialOutput(headphone: true)

        XCTAssertFalse(model.headTrackingAvailable, "the engine cannot render head tracking")
        model.setHeadTracking(true)
        XCTAssertFalse(model.headTrackingOn, "so it never starts")
        XCTAssertEqual(model.errorBanner, "이 헤드폰은 헤드 트래킹을 지원하지 않습니다")
    }

    func testVirtualRoomPushesTypeAndAmount() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertTrue(model.virtualRoomAvailable)
        model.setRoomType(2)
        model.setRoomAmount(140)          // out of range: clamps to 100
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertEqual(model.roomType, 2)
        XCTAssertEqual(model.roomAmount, 100)
        XCTAssertEqual(client.setParamsValues.last?.roomType, 2)
        XCTAssertEqual(client.setParamsValues.last?.roomAmount, 100)
    }

    func testVirtualRoomIsUnavailableOnAnEngineWithoutIt() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0,
                                  capabilities: EngineStatus.spatialParamsCapability)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        XCTAssertFalse(model.virtualRoomAvailable)
        model.setRoomType(3)
        try await Task.sleep(nanoseconds: 40_000_000)

        // The push is still allowed (spatial is supported) but the engine's
        // capability set strips the room, so nothing claims a room is playing.
        XCTAssertEqual(client.setParamsValues.last?.roomType ?? 0, 0)
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

    // MARK: Preset file export / import

    func testPresetExportImportRoundTripAndOverwrite() throws {
        defer { UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets") }
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.savedPresets = [
            SavedPreset(name: "Mine", preampDb: -2,
                        eqGainsDb: Array(repeating: 1, count: EngineParameters.bandCount),
                        outputGainDb: 0, compAmount: 40),
            SavedPreset(name: "Other", preampDb: 0,
                        eqGainsDb: Array(repeating: 0, count: EngineParameters.bandCount),
                        outputGainDb: 0),
        ]
        let data = try XCTUnwrap(model.exportPresetsData())

        // Import into a fresh model: both come back, values intact.
        let fresh = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        fresh.savedPresets = [
            SavedPreset(name: "mine", preampDb: 9,
                        eqGainsDb: Array(repeating: 9, count: EngineParameters.bandCount),
                        outputGainDb: 9),
        ]
        XCTAssertEqual(fresh.importPresets(from: data), 2)
        XCTAssertEqual(fresh.savedPresets.count, 2, "same name (case-insensitive) overwrites")
        let mine = fresh.savedPresets.first { $0.name == "Mine" }!
        XCTAssertEqual(mine.preampDb, -2)
        XCTAssertEqual(mine.compAmount, 40)
        XCTAssertFalse(mine.builtin)
    }

    func testPresetImportStripsBuiltinAndRejectsGarbage() {
        defer { UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets") }
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.savedPresets = []

        // A file claiming builtin must import as a user preset.
        let claimed = #"{"version":1,"presets":[{"name":"Sneaky","preampDb":0,"eqGainsDb":[0,0,0,0,0,0,0,0,0,0],"outputGainDb":0,"builtin":true}]}"#
        XCTAssertEqual(model.importPresets(from: Data(claimed.utf8)), 1)
        XCTAssertEqual(model.savedPresets.first?.builtin, false)

        // Garbage is rejected without touching the list.
        XCTAssertNil(model.importPresets(from: Data("not json".utf8)))
        XCTAssertEqual(model.savedPresets.count, 1)

        // Nothing saved → nothing to export.
        model.savedPresets = []
        XCTAssertNil(model.exportPresetsData())
    }

    // MARK: Dynamics (볼륨 평준화 — the light compressor's single knob)

    func testCompAmountPushesClampsAndRoundTripsInSavedPreset() async throws {
        defer {
            UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets")
            UserDefaults.standard.removeObject(forKey: "com.roomcut.activeSavedPreset")
        }
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setCompAmount(150)   // out of range → clamps to 100
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertTrue(model.dynamicsAvailable)
        XCTAssertEqual(model.compAmount, 100)
        XCTAssertEqual(client.setParamsValues.count, 1)
        XCTAssertEqual(client.setParamsValues.first?.compAmount, 100)

        // Save + re-apply round-trips the leveling amount.
        model.savedPresets = []
        model.setCompAmount(60)
        XCTAssertTrue(model.saveCurrentAsPreset(name: "Leveled"))
        let saved = model.savedPresets.first { $0.name == "Leveled" }!
        XCTAssertEqual(saved.compAmount, 60)
        model.setCompAmount(0)
        model.applySavedPreset(saved)
        XCTAssertEqual(model.compAmount, 60)
    }

    func testHighpassPushesAndClamps() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setHighpassHz(1000)   // out of range → clamps to 400 (validator max)
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertEqual(model.highpassHz, 400)
        XCTAssertEqual(client.setParamsValues.count, 1)
        XCTAssertEqual(client.setParamsValues.first?.highpassHz, 400)
    }

    func testCompAmountDoesNotPushWhenEngineLacksDynamics() async throws {
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0,
                                  capabilities: EngineStatus.spatialParamsCapability)]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setCompAmount(50)
        try await Task.sleep(nanoseconds: 40_000_000)

        XCTAssertFalse(model.dynamicsAvailable)
        XCTAssertEqual(model.compAmount, 0)
        XCTAssertTrue(client.setParamsValues.isEmpty)
        XCTAssertEqual(model.errorBanner, "현재 엔진이 볼륨 평준화를 지원하지 않습니다")
    }

    func testSavedPresetDynamicsBackCompat() throws {
        // Older saves without the dynamics keys decode safely to 0 (off).
        let legacy = #"{"name":"X","preampDb":0,"eqGainsDb":[0,0,0,0,0,0,0,0,0,0],"outputGainDb":0}"#
        let old = try JSONDecoder().decode(SavedPreset.self, from: Data(legacy.utf8))
        XCTAssertEqual(old.compAmount, 0)
        XCTAssertEqual(old.highpassHz, 0)
    }

    // MARK: Per-device presets (Settings → 기기별 프리셋 기억)

    private func clearDevicePresetDefaults() {
        UserDefaults.standard.removeObject(forKey: "com.roomcut.deviceAutoPreset")
        UserDefaults.standard.removeObject(forKey: "com.roomcut.devicePresetMap")
        UserDefaults.standard.removeObject(forKey: "com.roomcut.activeBuiltinPreset")
        UserDefaults.standard.removeObject(forKey: "com.roomcut.activeSavedPreset")
    }

    func testDeviceAutoPresetReappliesRememberedPresetOnDeviceSwitch() async throws {
        defer { clearDevicePresetDefaults() }
        clearDevicePresetDefaults()
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0, outputDeviceUID: "A"),   // connect
            .running(presetId: "night", revision: 1, outputDeviceUID: "A"),  // apply night
            .running(presetId: "night", revision: 1, outputDeviceUID: "B"),  // switch → B (no mapping yet)
            .running(presetId: "clean", revision: 2, outputDeviceUID: "B"),  // apply clean
            .running(presetId: "clean", revision: 2, outputDeviceUID: "A"),  // switch back → A
            .running(presetId: "night", revision: 3, outputDeviceUID: "A"),  // auto-apply readback
        ]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        model.setDeviceAutoPreset(true)
        await model.applyPreset("night")   // remembered for A
        await model.refreshNow()           // device A → B, B has no mapping
        XCTAssertEqual(client.setPresetIds, ["night"])
        await model.applyPreset("clean")   // remembered for B
        await model.refreshNow()           // device B → A → re-apply "night"

        // The auto-apply runs as a Task; poll until it lands (CI-safe ceiling).
        for _ in 0..<200 where client.setPresetIds.count < 3 {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertEqual(client.setPresetIds, ["night", "clean", "night"])
    }

    func testDeviceAutoPresetOffNeverAutoApplies() async {
        defer { clearDevicePresetDefaults() }
        clearDevicePresetDefaults()
        // A mapping exists on disk, but the feature is off.
        UserDefaults.standard.set(["B": "clean"], forKey: "com.roomcut.devicePresetMap")
        let client = FakeEngineClient()
        client.states = [
            .running(presetId: "flat", revision: 0, outputDeviceUID: "A"),
            .running(presetId: "flat", revision: 0, outputDeviceUID: "B"),
        ]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        await model.refreshNow()

        XCTAssertFalse(model.deviceAutoPresetEnabled)
        XCTAssertTrue(client.setPresetIds.isEmpty)
    }

    func testDeviceAutoPresetSkipsFirstConnectAndLoadsPersistedState() async throws {
        defer { clearDevicePresetDefaults() }
        clearDevicePresetDefaults()
        // Persisted from a previous session: feature on, device A remembers night.
        UserDefaults.standard.set(true, forKey: "com.roomcut.deviceAutoPreset")
        UserDefaults.standard.set(["A": "night"], forKey: "com.roomcut.devicePresetMap")
        let client = FakeEngineClient()
        client.states = [.running(presetId: "flat", revision: 0, outputDeviceUID: "A")]
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        await model.refreshNow()
        try await Task.sleep(nanoseconds: 50_000_000)

        XCTAssertTrue(model.deviceAutoPresetEnabled, "flag persists across sessions")
        // First connect resumes the engine's own state — never stomps it.
        XCTAssertTrue(client.setPresetIds.isEmpty)
    }

    func testDeleteSavedPresetDropsItsDeviceMappings() {
        defer { clearDevicePresetDefaults() }
        clearDevicePresetDefaults()
        UserDefaults.standard.set(true, forKey: "com.roomcut.deviceAutoPreset")
        UserDefaults.standard.set(["A": "saved:Mine", "B": "night"],
                                  forKey: "com.roomcut.devicePresetMap")
        let model = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        model.savedPresets = [SavedPreset(
            name: "Mine", preampDb: 0,
            eqGainsDb: Array(repeating: 0, count: EngineParameters.bandCount),
            outputGainDb: 0)]

        model.deleteSavedPreset(model.savedPresets[0])

        let map = UserDefaults.standard.dictionary(forKey: "com.roomcut.devicePresetMap") as? [String: String]
        XCTAssertEqual(map, ["B": "night"])
        UserDefaults.standard.removeObject(forKey: "com.roomcut.savedPresets")
    }

    // Apple's renderer or the built-in bed: offered only where both can play,
    // and what is picked is what gets pushed.
    func testBedRendererChoiceIsOfferedWhereBothCanPlayAndReachesTheEngine() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
            | EngineStatus.bedRendererCapability
        final class Attached: @unchecked Sendable { var value = true }
        let attached = Attached()
        let client = FakeEngineClient()
        client.stateReadHandler = {
            var status = EngineStatus.running(presetId: "custom", revision: 1, capabilities: caps)
            status.systemBedRenderer = attached.value
            return status
        }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setSpatialOutput(headphone: true)
        model.setSurroundChoice(.off)
        XCTAssertFalse(model.bedRendererChoiceAvailable, "no layout, no bed to render")
        model.setSurroundChoice(.virtual71)
        XCTAssertTrue(model.bedRendererChoiceAvailable)
        XCTAssertEqual(model.bedRendererChoice, .system, "Apple's renderer is the default")
        XCTAssertTrue(model.systemBedInUse, "and the Inspect tab says it is the one in use")

        model.setBedRendererChoice(.builtIn)
        XCTAssertFalse(model.systemBedInUse, "a sound that picks the built-in bed is not reported as Apple's")
        for _ in 0..<40 where client.setParamsValues.last?.bedRenderer != 1 {
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        XCTAssertEqual(client.setParamsValues.last?.bedRenderer, 1, "the choice is pushed")

        model.setSpatialOutput(headphone: false)
        XCTAssertFalse(model.bedRendererChoiceAvailable, "speakers never render a headphone bed")
        model.setSpatialOutput(headphone: true)
        XCTAssertTrue(model.bedRendererChoiceAvailable)

        attached.value = false
        await model.refreshNow()
        XCTAssertFalse(model.bedRendererChoiceAvailable, "an engine without the system renderer offers no choice")
        model.setBedRendererChoice(.system)
        XCTAssertFalse(model.systemBedInUse, "nor reports it in use")
    }

    func testEngineWithoutBedRendererInItsComparisonPayloadDoesNotResetIt() async throws {
        let caps = EngineStatus.spatialParamsCapability | EngineStatus.upmixCapability
            | EngineStatus.levelMatchCapability | EngineStatus.bedRendererCapability
        final class Revision: @unchecked Sendable { var value: UInt32 = 1 }
        let revision = Revision()
        let client = FakeEngineClient()
        client.offersComparison = true
        client.comparisonCarriesBedRenderer = false
        client.stateReadHandler = {
            var status = EngineStatus.running(presetId: "custom", revision: revision.value, capabilities: caps)
            status.systemBedRenderer = true
            return status
        }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setSpatialOutput(headphone: true)
        model.setSurroundChoice(.virtual51)
        model.setBedRendererChoice(.builtIn)
        for _ in 0..<40 where client.setComparisonCalls.isEmpty {
            try await Task.sleep(nanoseconds: 25_000_000)
        }
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.bedRenderer, 1, "a payload without a renderer leaves the choice alone")

        client.comparisonCarriesBedRenderer = true
        client.params.bedRenderer = 0
        revision.value += 1
        await model.refreshNow()
        XCTAssertEqual(model.bedRenderer, 0, "the engine's own renderer wins once it reports one")
    }

    func testSavedPresetKeepsItsBedRendererAndOlderSavesUseTheDefault() throws {
        let preset = SavedPreset(name: "Built-in bed", preampDb: 0,
                                 eqGainsDb: Array(repeating: 0, count: 10), outputGainDb: 0,
                                 surroundType: 3, bedRenderer: 1)
        let back = try JSONDecoder().decode(SavedPreset.self, from: try JSONEncoder().encode(preset))
        XCTAssertEqual(back.bedRenderer, 1)
        XCTAssertEqual(SoundSnapshot(preset: back).parameters.bedRenderer, 1)
        let legacy = #"{"name":"X","preampDb":0,"eqGainsDb":[0,0,0,0,0,0,0,0,0,0],"outputGainDb":0,"surroundType":2}"#
        let old = try JSONDecoder().decode(SavedPreset.self, from: Data(legacy.utf8))
        XCTAssertEqual(old.bedRenderer, 0, "a save from before the field plays through the system renderer")
        var odd = EngineParameters.flat
        odd.bedRenderer = 7
        XCTAssertEqual(odd.normalized().bedRenderer, 1, "out-of-range renderers clamp onto the two choices")
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
    var setParamsDelayNanoseconds: UInt64 = 0
    var setPresetIds: [String] = []
    var setParamsValues: [EngineParameters] = []
    var getParamsCount = 0
    var stateReadHandler: (@MainActor () -> EngineStatus)?

    func getState() async throws -> EngineStatus {
        if let stateReadHandler { return await stateReadHandler() }
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

    var failSetBypass = false
    var bypassValues: [Bool] = []
    var bypassWriteHandler: (@MainActor (Bool) async throws -> Void)?
    func setBypass(_ on: Bool) async throws {
        if let bypassWriteHandler { try await bypassWriteHandler(on); return }
        if failSetBypass { throw EngineClientError.transport(-2) }
        bypassValues.append(on)
    }
    var keepDefaultValues: [Bool] = []
    func setKeepDefault(_ on: Bool) async throws { keepDefaultValues.append(on) }

    func setParams(_ params: EngineParameters) async throws {
        if setParamsDelayNanoseconds > 0 { try await Task.sleep(nanoseconds: setParamsDelayNanoseconds) }
        if failSetParams {
            throw EngineClientError.transport(-2)
        }
        setParamsValues.append(params)
        self.params = params
    }

    // Level-match fixture: an engine that answers from whatever was last pushed,
    // and can pretend to be one whose payload predates the virtual room.
    var offersComparison = false
    var comparisonCarriesRoom = true
    var comparisonCarriesUpmix = true
    var comparisonCarriesBedRenderer = true
    var comparisonRevision: UInt64 = 1
    var setComparisonCalls: [(EngineComparisonTarget, EngineParameters, Bool)] = []
    func getComparison() async throws -> EngineComparisonState {
        guard offersComparison else { throw EngineClientError.transport(-3) }
        var state = EngineComparisonState(current: params, reference: params, presetID: "custom",
                                          revision: comparisonRevision, renderedRevision: comparisonRevision)
        state.carriesVirtualRoom = comparisonCarriesRoom
        if !comparisonCarriesRoom {
            state.current.roomType = 0; state.current.roomAmount = 0
            state.reference.roomType = 0; state.reference.roomAmount = 0
        }
        state.carriesUpmix = comparisonCarriesUpmix
        if !comparisonCarriesUpmix {
            state.current.surroundType = 0; state.reference.surroundType = 0
        }
        state.carriesBedRenderer = comparisonCarriesBedRenderer
        if !comparisonCarriesBedRenderer {
            state.current.bedRenderer = 0; state.reference.bedRenderer = 0
        }
        return state
    }
    func setComparison(_ target: EngineComparisonTarget, reference: EngineParameters, enabled: Bool) async throws {
        setComparisonCalls.append((target, reference, enabled))
        if case .parameters(let value) = target { params = value }
        comparisonRevision += 1
    }

    var devices: [OutputDeviceChoice] = []
    var setDeviceUIDs: [String] = []
    var volume: Double? = 0.5
    var balance: Double? = 0.0
    func outputDevices() -> [OutputDeviceChoice] { devices }
    func setOutputDevice(_ uid: String) async throws { setDeviceUIDs.append(uid) }
    func volumeGet() -> Double? { volume }
    func volumeSet(_ scalar: Double) { volume = scalar }
    func balanceGet() -> Double? { balance }
    func balanceSet(_ pan: Double) { balance = pan }
}

private extension EngineStatus {
    static func running(
        presetId: String,
        revision: UInt32,
        peak: Float = 0,
        limiterGRDb: Float = 0,
        underruns: UInt64 = 0,
        outputDeviceUID: String = "",
        capabilities: UInt32 = EngineStatus.spatialParamsCapability
            | EngineStatus.parametricCapability
            | EngineStatus.analyzerCapability
            | EngineStatus.dynamicsCapability
            | EngineStatus.virtualRoomCapability
    ) -> EngineStatus {
        var status = EngineStatus()
        status.reachable = true
        status.state = EngineStatus.running
        status.presetId = presetId
        status.paramsRevision = revision
        status.peak = peak
        status.limiterGRDb = limiterGRDb
        status.underruns = underruns
        status.outputDeviceUID = outputDeviceUID
        status.capabilities = capabilities
        return status
    }
}
