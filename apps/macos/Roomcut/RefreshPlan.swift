import Foundation

// What one poll should do, decided before anything is applied. The view model
// owns the timers and the IO; this owns the reasoning, so "a late reply must not
// overwrite a newer edit" and friends can be checked without an engine.
public struct RefreshPlan: Equatable {
    public var publishStatus = false
    public var deviceChanged = false
    public var wasOffline = false
    public var needsParams = false
    public var wantsParams = false        // still subject to the retry timer's force
    public var devicePresetToken: String?
    public var claimDefault = false
    public var wantsAnalysis = false
    public var clearAnalysis = false
    public var wantsComparison = false

    // A first connection and a device switch both force the device queries that
    // are otherwise on a timer.
    public var forceDeviceRefresh: Bool { wasOffline || deviceChanged }
}

public struct RefreshInputs {
    public var previous: EngineStatus
    public var next: EngineStatus
    public var isEditingParams: Bool
    public var editsUnchanged: Bool
    public var lastSeenPresetId: String?
    public var lastSeenRevision: UInt32?
    public var deviceAutoPreset: Bool
    public var mappedPresetToken: String?
    public var pickerSelection: String
    public var didClaimDefault: Bool
    public var analyzerVisible: Bool
    public var hasAnalysis: Bool
    public var comparisonEnabled: Bool
    public var retryDue: Bool

    public init(previous: EngineStatus, next: EngineStatus, isEditingParams: Bool, editsUnchanged: Bool,
                lastSeenPresetId: String?, lastSeenRevision: UInt32?, deviceAutoPreset: Bool,
                mappedPresetToken: String?, pickerSelection: String, didClaimDefault: Bool,
                analyzerVisible: Bool, hasAnalysis: Bool, comparisonEnabled: Bool, retryDue: Bool) {
        self.previous = previous; self.next = next
        self.isEditingParams = isEditingParams; self.editsUnchanged = editsUnchanged
        self.lastSeenPresetId = lastSeenPresetId; self.lastSeenRevision = lastSeenRevision
        self.deviceAutoPreset = deviceAutoPreset; self.mappedPresetToken = mappedPresetToken
        self.pickerSelection = pickerSelection; self.didClaimDefault = didClaimDefault
        self.analyzerVisible = analyzerVisible; self.hasAnalysis = hasAnalysis
        self.comparisonEnabled = comparisonEnabled; self.retryDue = retryDue
    }
}

public enum RefreshPlanner {
    public static func plan(_ input: RefreshInputs) -> RefreshPlan {
        var plan = RefreshPlan()
        plan.wasOffline = !input.previous.reachable
        plan.deviceChanged = input.previous.outputDeviceUID != input.next.outputDeviceUID
        plan.publishStatus = shouldPublish(previous: input.previous, next: input.next)
        plan.needsParams = plan.wasOffline
            || input.previous.capabilities != input.next.capabilities
            || input.lastSeenPresetId != input.next.presetId
            || input.lastSeenRevision != input.next.paramsRevision

        // Per-device presets react only to a REAL switch while connected. The
        // first connection is left alone: the engine already resumed the user's
        // last state, and replacing it with a mapped preset would surprise.
        if input.deviceAutoPreset && plan.deviceChanged && !plan.wasOffline
            && !input.previous.outputDeviceUID.isEmpty && !input.next.outputDeviceUID.isEmpty
            && !input.isEditingParams,
           let token = input.mappedPresetToken, token != input.pickerSelection {
            plan.devicePresetToken = token
        }

        plan.claimDefault = !input.didClaimDefault && input.next.state == EngineStatus.running
        plan.wantsAnalysis = input.analyzerVisible && input.next.supportsAnalyzer
        plan.clearAnalysis = !plan.wantsAnalysis && input.hasAnalysis
        plan.wantsComparison = !plan.needsParams && input.comparisonEnabled
            && input.next.supportsLevelMatch && !input.isEditingParams
        // A reply from before the current edit is stale by definition, so reading
        // parameters back over it is exactly the overwrite to avoid.
        plan.wantsParams = plan.needsParams && !input.isEditingParams && input.editsUnchanged
            && (plan.wasOffline || input.retryDue)
        return plan
    }

    // Peak, framesRendered and underruns tick every poll; publishing those would
    // fire objectWillChange at the poll rate and re-render the window, which is a
    // real idle-CPU sink. Only publish what the UI actually shows as state.
    static func shouldPublish(previous: EngineStatus, next: EngineStatus) -> Bool {
        previous.reachable != next.reachable
            || previous.state != next.state
            || previous.presetId != next.presetId
            || previous.manualBypass != next.manualBypass
            || previous.safeBypass != next.safeBypass
            || previous.paramsRevision != next.paramsRevision
            || previous.outputDeviceUID != next.outputDeviceUID
            || previous.keepDefault != next.keepDefault
            || previous.capabilities != next.capabilities
            || previous.volumeBoost != next.volumeBoost
            || previous.engineLatencyMs != next.engineLatencyMs
            // Whether Apple's renderer is attached decides which controls exist.
            // How much of the bed it renders at this instant is not published:
            // it moves through every 20 ms hand-over and would redraw the tab.
            || previous.systemBedRenderer != next.systemBedRenderer
            || previous.bedPersonalizedHrtf != next.bedPersonalizedHrtf
            || previous.bedUnitRate != next.bedUnitRate
    }

    // A dropout worth surfacing: the lifetime counter climbed since the last poll
    // AND audio is actually flowing. Idle silence also ticks underruns, so the
    // peak gate is what keeps the warning from being permanently on.
    public static func underrunsActive(previous: UInt64?, current: UInt64, peak: Float) -> Bool {
        guard let previous else { return false }
        return current > previous && peak > 1e-4
    }
}
