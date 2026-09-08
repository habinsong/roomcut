import Combine

// Live meters (peak / limiter GR / dropouts) live in their OWN observable object
// so their ~12 Hz updates invalidate ONLY the small meter views — not the whole
// window (RoomcutViewModel) and its expensive full-window background. This is the
// fix for the idle-CPU storm: previously these were @Published on the view model,
// so every meter tick re-rendered every view observing the model.
@MainActor
public final class RoomcutMeters: ObservableObject {
    @Published public private(set) var displayPeak: Float = 0
    @Published public private(set) var displayLimiterGRDb: Float = 0
    @Published public var underrunsActive = false

    private var limiterHoldPolls = 0
    private var underrunHoldPolls = 0
    private static let meterPeakFloor: Float = 0.0001
    private static let meterPeakDecay: Float = 0.86
    private static let limiterDecay: Float = 0.92
    private static let limiterHoldPollsMax = 18
    private static let underrunHoldPollsMax = 45

    func update(peak rawPeak: Float, limiterGRDb: Float, underrunActiveNow: Bool) {
        let peak = max(0, rawPeak)
        if peak >= displayPeak {
            displayPeak = peak
        } else {
            let decayed = max(peak, displayPeak * Self.meterPeakDecay)
            displayPeak = decayed < Self.meterPeakFloor ? 0 : decayed
        }

        let limiter = abs(limiterGRDb)
        if limiter > 0.05 {
            displayLimiterGRDb = max(limiter, displayLimiterGRDb)
            limiterHoldPolls = Self.limiterHoldPollsMax
        } else if limiterHoldPolls > 0 && displayLimiterGRDb > 0.05 {
            limiterHoldPolls -= 1
            displayLimiterGRDb *= Self.limiterDecay
        } else {
            limiterHoldPolls = 0
            displayLimiterGRDb = 0
        }

        if underrunActiveNow {
            underrunHoldPolls = Self.underrunHoldPollsMax
        } else if underrunHoldPolls > 0 {
            underrunHoldPolls -= 1
        }
        let next = underrunHoldPolls > 0
        if underrunsActive != next { underrunsActive = next }
    }

    func reset() {
        if displayPeak != 0 { displayPeak = 0 }
        if displayLimiterGRDb != 0 { displayLimiterGRDb = 0 }
        if underrunsActive { underrunsActive = false }
        limiterHoldPolls = 0
        underrunHoldPolls = 0
    }
}

