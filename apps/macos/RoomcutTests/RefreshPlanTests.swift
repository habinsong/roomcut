import XCTest
@testable import RoomcutCore

// ARC-02: the decisions one poll makes, on their own. These used to be reachable
// only by driving the whole view model against a fake engine, which is why the
// interesting combinations — a late reply landing on a newer edit, a device swap
// during an edit — were never pinned down.
final class RefreshPlanTests: XCTestCase {
    private func status(reachable: Bool = true, device: String = "device-A", preset: String = "flat",
                        revision: UInt32 = 1, state: UInt32 = EngineStatus.running,
                        capabilities: UInt32 = EngineStatus.analyzerCapability | EngineStatus.levelMatchCapability,
                        latencyMs: Double = 0) -> EngineStatus {
        var value = EngineStatus()
        value.reachable = reachable
        value.outputDeviceUID = device
        value.presetId = preset
        value.paramsRevision = revision
        value.state = state
        value.capabilities = capabilities
        value.engineLatencyMs = latencyMs
        return value
    }

    private func inputs(previous: EngineStatus, next: EngineStatus,
                        editing: Bool = false, editsUnchanged: Bool = true,
                        lastPreset: String? = "flat", lastRevision: UInt32? = 1,
                        autoPreset: Bool = false, mapped: String? = nil, picker: String = "",
                        claimed: Bool = true, analyzer: Bool = false, hasAnalysis: Bool = false,
                        comparison: Bool = false, retryDue: Bool = true) -> RefreshInputs {
        .init(previous: previous, next: next, isEditingParams: editing, editsUnchanged: editsUnchanged,
              lastSeenPresetId: lastPreset, lastSeenRevision: lastRevision, deviceAutoPreset: autoPreset,
              mappedPresetToken: mapped, pickerSelection: picker, didClaimDefault: claimed,
              analyzerVisible: analyzer, hasAnalysis: hasAnalysis, comparisonEnabled: comparison,
              retryDue: retryDue)
    }

    func testAQuietPollChangesNothing() {
        let plan = RefreshPlanner.plan(inputs(previous: status(), next: status()))
        XCTAssertFalse(plan.publishStatus)
        XCTAssertFalse(plan.needsParams)
        XCTAssertFalse(plan.wantsParams)
        XCTAssertNil(plan.devicePresetToken)
        XCTAssertFalse(plan.forceDeviceRefresh)
    }

    func testMetersAloneDoNotRepublishTheStatus() {
        var next = status()
        next.peak = 0.8
        next.frames = 999_999
        next.underruns = 42
        XCTAssertFalse(RefreshPlanner.plan(inputs(previous: status(), next: next)).publishStatus,
                       "publishing on every meter tick would re-render the window at the poll rate")
    }

    func testAChangedProcessingLatencyIsPublished() {
        let next = status(latencyMs: 2.6)
        XCTAssertTrue(RefreshPlanner.plan(inputs(previous: status(latencyMs: 0), next: next)).publishStatus,
                      "Inspect shows this number, so a change has to reach it")
    }

    func testComingBackOnlineForcesTheDeviceQueriesAndParams() {
        let plan = RefreshPlanner.plan(inputs(previous: status(reachable: false), next: status()))
        XCTAssertTrue(plan.wasOffline)
        XCTAssertTrue(plan.forceDeviceRefresh)
        XCTAssertTrue(plan.needsParams)
        XCTAssertTrue(plan.wantsParams)
        XCTAssertTrue(plan.publishStatus)
    }

    func testANewRevisionAsksForParametersButNotWhileEditing() {
        let next = status(revision: 9)
        XCTAssertTrue(RefreshPlanner.plan(inputs(previous: status(), next: next)).wantsParams)
        let editing = RefreshPlanner.plan(inputs(previous: status(), next: next, editing: true))
        XCTAssertTrue(editing.needsParams)
        XCTAssertFalse(editing.wantsParams, "reading back over a gesture in progress is the overwrite to avoid")
    }

    func testAnEditThatLandedDuringThePollWins() {
        let next = status(revision: 9)
        let plan = RefreshPlanner.plan(inputs(previous: status(), next: next, editsUnchanged: false))
        XCTAssertFalse(plan.wantsParams, "the reply describes a state older than the edit")
    }

    func testTheRetryTimerHoldsParametersBackButNotAReconnect() {
        let next = status(revision: 9)
        XCTAssertFalse(RefreshPlanner.plan(inputs(previous: status(), next: next, retryDue: false)).wantsParams)
        let reconnect = inputs(previous: status(reachable: false), next: next, retryDue: false)
        XCTAssertTrue(RefreshPlanner.plan(reconnect).wantsParams, "a fresh connection does not wait out the timer")
    }

    func testADeviceSwitchAppliesItsMappedPreset() {
        let plan = RefreshPlanner.plan(inputs(previous: status(device: "device-A"), next: status(device: "device-B"),
                                              autoPreset: true, mapped: "library:studio"))
        XCTAssertTrue(plan.deviceChanged)
        XCTAssertEqual(plan.devicePresetToken, "library:studio")
    }

    func testTheMappedPresetIsLeftAloneWhenItWouldSurprise() {
        // First connection: the engine already resumed the user's own state.
        XCTAssertNil(RefreshPlanner.plan(inputs(previous: status(reachable: false, device: ""),
                                                next: status(device: "device-B"),
                                                autoPreset: true, mapped: "library:studio")).devicePresetToken)
        // Mid-gesture.
        XCTAssertNil(RefreshPlanner.plan(inputs(previous: status(device: "device-A"), next: status(device: "device-B"),
                                                editing: true, autoPreset: true,
                                                mapped: "library:studio")).devicePresetToken)
        // Already selected.
        XCTAssertNil(RefreshPlanner.plan(inputs(previous: status(device: "device-A"), next: status(device: "device-B"),
                                                autoPreset: true, mapped: "library:studio",
                                                picker: "library:studio")).devicePresetToken)
        // Feature off.
        XCTAssertNil(RefreshPlanner.plan(inputs(previous: status(device: "device-A"), next: status(device: "device-B"),
                                                autoPreset: false, mapped: "library:studio")).devicePresetToken)
    }

    func testTheDefaultOutputIsClaimedOnceAndOnlyWhenRunning() {
        let stopped = status(state: EngineStatus.stopped)
        XCTAssertFalse(RefreshPlanner.plan(inputs(previous: stopped, next: stopped, claimed: false)).claimDefault)
        XCTAssertTrue(RefreshPlanner.plan(inputs(previous: stopped, next: status(), claimed: false)).claimDefault)
        XCTAssertFalse(RefreshPlanner.plan(inputs(previous: stopped, next: status(), claimed: true)).claimDefault)
    }

    func testAnalysisFollowsTheTabAndTheEngine() {
        let noAnalyzer = status(capabilities: 0)
        XCTAssertTrue(RefreshPlanner.plan(inputs(previous: status(), next: status(), analyzer: true)).wantsAnalysis)
        let unsupported = RefreshPlanner.plan(inputs(previous: noAnalyzer, next: noAnalyzer,
                                                     analyzer: true, hasAnalysis: true))
        XCTAssertFalse(unsupported.wantsAnalysis)
        XCTAssertTrue(unsupported.clearAnalysis, "a stale analysis panel is dropped")
        let hidden = RefreshPlanner.plan(inputs(previous: status(), next: status(),
                                                analyzer: false, hasAnalysis: true))
        XCTAssertTrue(hidden.clearAnalysis)
    }

    func testComparisonIsSkippedWhileParametersAreStale() {
        let same = status()
        XCTAssertTrue(RefreshPlanner.plan(inputs(previous: same, next: same, comparison: true)).wantsComparison)
        let changed = RefreshPlanner.plan(inputs(previous: same, next: status(revision: 9), comparison: true))
        XCTAssertFalse(changed.wantsComparison, "read the parameters first, then compare against them")
    }
}
