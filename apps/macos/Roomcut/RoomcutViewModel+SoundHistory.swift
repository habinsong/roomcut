import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

// Undo/redo over sound edits, and the A/B comparison that rides alongside it.
//
// The awkward part is version skew: an engine older than a feature answers with
// zeros for it, and adopting those would switch off something the listener just
// chose. Each such field keeps the local value until the engine says it can
// actually carry it.
extension RoomcutViewModel {
    public var canUndoSound: Bool { status.reachable && editor.canUndo }
    public var canRedoSound: Bool { status.reachable && editor.canRedo }
    public var isSoundWritePending: Bool { writer.isBusy }
    public func beginParameterEdit() { editor.beginGesture() }
    public func endParameterEdit() {
        guard editor.isGestureActive else { return }
        editor.endGesture()
        schedulePushParams(preservingPresetSelection: true)
    }
    public func undoSound() { guard canUndoSound else { return }; editor.undo(); sendHistoryState() }
    public func redoSound() { guard canRedoSound else { return }; editor.redo(); sendHistoryState() }
    public func selectComparison(_ slot: SoundComparisonSlot) {
        guard status.reachable, editor.hasBaseline, editor.activeSlot != slot else { return }
        editor.select(slot)
        sendHistoryState()
    }
    public func copyComparison() {
        guard status.reachable, editor.hasBaseline else { return }
        editor.copyToOther()
        if comparison.enabled { sendHistoryState() }
    }
    public func toggleLevelMatch() {
        guard status.reachable, editor.hasBaseline, comparison.supported else { return }
        editor.finishEditing()
        comparison.requestEnabled(!comparison.enabled)
        sendHistoryState()
    }
    private func sendHistoryState() {
        writeErrorActive = false
        setActivePreset(savedName: editor.snapshot.savedPresetName, builtinId: editor.snapshot.builtinPresetID)
        var value = editor.snapshot
        value.parameters = currentParameters()
        writer.parameters(value, comparison: prepareComparisonWrite(), immediate: true)
    }

    func prepareComparisonWrite() -> SoundComparisonWrite? {
        guard comparison.supported else { return nil }
        if comparison.enabled { comparison.requestEnabled(true) }
        return SoundComparisonWrite(reference: editor.referenceSnapshot.parameters.supported(by: status), enabled: comparison.enabled)
    }

    // An engine whose comparison payload predates the virtual room (or the
    // upmix) reports none. Comparing its zeros against what the user has set
    // would look like a mismatch on every poll, so the local value stands in.
    private func withLocalRoomIfAbsent(_ parameters: EngineParameters,
                                       _ snapshot: EngineComparisonState) -> EngineParameters {
        var value = parameters.supported(by: status)
        if !snapshot.carriesVirtualRoom {
            value.roomType = roomType
            value.roomAmount = roomAmount
            value.surroundType = surroundType
            value.centerWidth = centerWidth
            value.surroundDepth = surroundDepth
        }
        if !snapshot.carriesUpmix {
            value.surroundType = surroundType
            value.centerWidth = centerWidth
            value.surroundDepth = surroundDepth
        }
        if !snapshot.carriesBedRenderer { value.bedRenderer = bedRenderer }
        return value
    }

    func readSoundParameters() async throws -> (parameters: EngineParameters, comparison: EngineComparisonState?) {
        if status.supportsLevelMatch {
            let value = try await client.getComparison()
            var parameters = value.current
            // An engine whose comparison payload predates the virtual room sends
            // no room at all. Taking its zeros would flip a room the user just
            // picked straight back to off, so keep what we have instead.
            if !value.carriesVirtualRoom {
                parameters.roomType = roomType
                parameters.roomAmount = roomAmount
            }
            if !value.carriesUpmix {
                parameters.surroundType = surroundType
                parameters.centerWidth = centerWidth
                parameters.surroundDepth = surroundDepth
            }
            if !value.carriesBedRenderer { parameters.bedRenderer = bedRenderer }
            return (parameters, value)
        }
        return (try await client.getParams(), nil)
    }

    func acceptComparison(_ snapshot: EngineComparisonState?) {
        guard let snapshot else { return }
        let reference = withLocalRoomIfAbsent(snapshot.reference, snapshot)
        if snapshot.enabled && reference != editor.referenceSnapshot.parameters { editor.restoreReference(reference) }
        comparison.accept(snapshot)
    }

    func refreshComparisonState(in cycle: EnginePoller.Cycle) async {
        let editRevision = editor.revision, writeRevision = writer.revision
        do {
            let snapshot = try await client.getComparison()
            guard poller.isCurrent(cycle) else { return }
            guard editRevision == editor.revision, writeRevision == writer.revision, !isEditingParams else { return }
            guard withLocalRoomIfAbsent(snapshot.current, snapshot) == currentParameters(),
                  !snapshot.enabled || withLocalRoomIfAbsent(snapshot.reference, snapshot)
                      == editor.referenceSnapshot.parameters.supported(by: status) else {
                lastSeenRevision = nil
                comparison.requestEnabled(snapshot.enabled)
                return
            }
            comparison.accept(snapshot)
        } catch {
            if poller.isCurrent(cycle) { comparison.failed() }
        }
    }

    func refreshAnalysis(in cycle: EnginePoller.Cycle) async {
        do {
            let next = try await client.getAnalysis()
            guard poller.isCurrent(cycle), analyzerVisible else { return }
            if analysis != next { analysis = next }
        } catch {
            guard poller.isCurrent(cycle), analyzerVisible else { return }
            if analysis != nil { analysis = nil }
        }
    }

    // Pure rule (testable): warn only when the lifetime underrun counter rose
    // AND audio is flowing. First sample (previous == nil) never warns — it just
    // establishes the baseline.
    func updateMeterDisplay(with nextStatus: EngineStatus, underrunActiveNow: Bool) {
        guard nextStatus.reachable else {
            meters.reset()
            return
        }
        meters.update(peak: nextStatus.peak,
                      limiterGRDb: nextStatus.limiterGRDb,
                      underrunActiveNow: underrunActiveNow)
    }

    func resetMeterDisplay() {
        meters.reset()
    }

    func clearErrorBanner() {
        let controlError = deviceWriter.error ?? bypassWriter.error
        if !writeErrorActive && !paramsReadFailed && errorBanner != controlError { errorBanner = controlError }
    }

    func setErrorBanner(_ message: String) {
        if errorBanner != message { errorBanner = message }
    }
}
