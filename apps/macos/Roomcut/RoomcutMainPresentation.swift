//
// RoomcutMainPresentation.swift — pure state + presentation for the Now Playing
// main shell. Lives in RoomcutPresentationCore: no SwiftUI/AppKit, no
// RoomcutCore types. Every helper takes primitives so it stays unit-testable
// and the app target owns the SwiftUI @State / Binding wiring.
//
import Foundation

// MARK: - UI-only state (owned by the app target's RoomcutMainView)

public enum PanelState: Equatable, Sendable {
    case collapsed
    case expanded

    public mutating func toggle() {
        self = (self == .collapsed) ? .expanded : .collapsed
    }
}

public enum SoundSheetState: Equatable, Sendable {
    case collapsed
    case basic
    case advanced

    // One step toward fully open (collapsed → basic → advanced; advanced stays).
    public func expanded() -> SoundSheetState {
        switch self {
        case .collapsed: return .basic
        case .basic:     return .advanced
        case .advanced:  return .advanced
        }
    }

    // One step toward closed (advanced → basic → collapsed; collapsed stays).
    public func collapsedOneStep() -> SoundSheetState {
        switch self {
        case .advanced:  return .basic
        case .basic:     return .collapsed
        case .collapsed: return .collapsed
        }
    }
}

public enum SoundControlsLevel: Equatable, Sendable {
    case minimized
    case controls
    case expanded

    public func expandedOneStep() -> SoundControlsLevel {
        switch self {
        case .minimized: return .controls
        case .controls:  return .expanded
        case .expanded:  return .expanded
        }
    }

    public func collapsedOneStep() -> SoundControlsLevel {
        switch self {
        case .expanded:  return .controls
        case .controls:  return .minimized
        case .minimized: return .minimized
        }
    }
}

// Now Playing is fallback-first in this MVP: production shows only the engine
// signal state; sample metadata appears solely in fixtures.
public enum NowPlayingDisplayState: Equatable, Sendable {
    case fallback(signalActive: Bool)
    case fixture(title: String, artist: String, source: String, progress: Double)
}

// Health drives the single top status dot. Distinct from RoomcutPresentation's
// state-name labelling: this folds limiter/underrun/bypass into one tri-state so
// the dot reads green/amber/red without colour being the only signal.
public enum EngineHealth: Equatable, Sendable {
    case normal
    case degraded
    case stopped
}

public enum RoomcutWindowMetrics {
    public struct Size: Equatable, Sendable {
        public let width: Double
        public let height: Double

        public init(width: Double, height: Double) {
            self.width = width
            self.height = height
        }
    }

    public static let baseWidth = 402.0
    public static let baseHeight = 874.0
    public static let compactBaseHeight = 260.0
    public static let minScale = 0.4
    public static let maxScale = 1.4

    public static let minWidth = baseWidth * minScale
    public static let maxWidth = baseWidth * maxScale
    public static let minHeight = baseHeight * minScale
    public static let maxHeight = baseHeight * maxScale

    public static func clampedWidth(_ width: Double) -> Double {
        min(maxWidth, max(minWidth, width))
    }

    public static func height(forWidth width: Double) -> Double {
        clampedWidth(width) * (baseHeight / baseWidth)
    }

    public static func compactHeight(forWidth width: Double) -> Double {
        clampedWidth(width) * (compactBaseHeight / baseWidth)
    }

    public static func constrainedSize(proposedWidth: Double) -> Size {
        let width = clampedWidth(proposedWidth)
        return Size(width: width, height: height(forWidth: width))
    }
}

// MARK: - Main shell presentation rules

public enum RoomcutMainPresentation {
    // Engine state codes (match EngineStatus; RoomcutPresentation uses the same
    // bare literals since this target can't see the C constants).
    static let stateStopped: UInt32 = 0
    static let stateRunning: UInt32 = 1
    static let stateBypass: UInt32 = 2
    static let stateRecover: UInt32 = 3

    /// Top status dot health. metadata availability is deliberately NOT an input
    /// — missing Now Playing metadata is normal fallback copy, never a warning.
    public static func engineHealth(
        reachable: Bool,
        state: UInt32,
        manualBypass: Bool,
        limiterActive: Bool,
        underrunActive: Bool
    ) -> EngineHealth {
        guard reachable else { return .stopped }
        switch state {
        case stateStopped:
            return .stopped
        case stateRunning:
            if manualBypass || limiterActive || underrunActive { return .degraded }
            return .normal
        default:
            // bypass / recover / unknown reachable states are all "attention".
            return .degraded
        }
    }

    public struct StatusDot: Equatable, Sendable {
        public let health: EngineHealth
        public let accessibilityLabel: String

        public init(health: EngineHealth, accessibilityLabel: String) {
            self.health = health
            self.accessibilityLabel = accessibilityLabel
        }
    }

    // Color is never the only signal: pair the dot with this label / tooltip.
    public static func statusDot(for health: EngineHealth) -> StatusDot {
        switch health {
        case .normal:   return StatusDot(health: .normal, accessibilityLabel: "Roomcut 상태: 정상")
        case .degraded: return StatusDot(health: .degraded, accessibilityLabel: "Roomcut 상태: 주의")
        case .stopped:  return StatusDot(health: .stopped, accessibilityLabel: "Roomcut 상태: 정지")
        }
    }

    // MARK: On/Off toggle

    // The top toggle reads "On" but the engine contract is `manualBypass`:
    // On  == not bypassed (manualBypass == false)
    // Off == bypassed     (manualBypass == true)
    public static func roomcutEnabled(manualBypass: Bool) -> Bool { !manualBypass }
    public static func manualBypass(forEnabled enabled: Bool) -> Bool { !enabled }

    // MARK: Now Playing fallback

    /// Production fallback display: signal-active only when the engine is
    /// reachable, running, and audio is above the silence floor. Anything else
    /// is "no metadata", and neither branch affects engine health.
    public static func fallbackDisplay(reachable: Bool, state: UInt32, peak: Float) -> NowPlayingDisplayState {
        let active = reachable && state == stateRunning && peak > 1e-4
        return .fallback(signalActive: active)
    }

    public static let fallbackTitle = "System Audio"

    public static func subtitle(for display: NowPlayingDisplayState) -> String {
        switch display {
        case .fallback(let signalActive):
            return signalActive ? "Signal active" : "No media metadata available"
        case .fixture(_, let artist, let source, _):
            return source.isEmpty ? artist : "\(artist) · \(source)"
        }
    }

    public static func title(for display: NowPlayingDisplayState) -> String {
        switch display {
        case .fallback:
            return fallbackTitle
        case .fixture(let title, _, _, _):
            return title
        }
    }

    // Playback controls have no engine backing yet, and the fallback has no real
    // progress — both stay disabled outside fixtures so nothing looks live.
    public static func controlsEnabled(for display: NowPlayingDisplayState) -> Bool {
        if case .fixture = display { return true }
        return false
    }

    public static func audioFormatLabel(
        bitDepth: Int,
        sampleRate: Double,
        latencyMs: Double
    ) -> String {
        let sampleRateKHz = sampleRate / 1_000
        let rate = sampleRateKHz.rounded() == sampleRateKHz
            ? String(format: "%.0f", sampleRateKHz)
            : String(format: "%.1f", sampleRateKHz)
        return "\(bitDepth)-bit · \(rate) kHz · Latency \(Int(latencyMs.rounded())) ms"
    }

    // MARK: Launch defaults

    // A fresh window always opens collapsed-home; panel/sheet state isn't
    // persisted in this MVP.
    public static let launchSidebar: PanelState = .collapsed
    public static let launchInspector: PanelState = .collapsed
    public static let launchSheet: SoundSheetState = .collapsed
}

// MARK: - Basic EQ macros (Bass / Vocal / Clarity)

// A thin, predictable layer over the existing 10-band EQ. Each macro maps a
// normalized value in [-1, 1] to gain deltas on a few target bands only —
// unrelated bands are preserved, and nothing initializes the whole curve to
// flat. Room/Space are intentionally absent: they need the Phase 7 spatial
// stage, so the UI shows them disabled rather than mapping them here.
public enum EqMacro: String, CaseIterable, Sendable {
    case bass
    case warmth
    case vocal
    case clarity
    case air

    public var title: String {
        switch self {
        case .bass:    return "Bass"
        case .warmth:  return "Warmth"
        case .vocal:   return "Vocal"
        case .clarity: return "Clarity"
        case .air:     return "Air"
        }
    }

    // Per-band gain weight at value == 1.0 (dB). Indices are EqBands centers:
    // 0:31 1:62 2:125 3:250 4:500 5:1k 6:2k 7:4k 8:8k 9:16k.
    var weights: [Int: Double] {
        switch self {
        case .bass:    return [0: 3.0, 1: 6.0, 2: 6.0]
        case .warmth:  return [3: 4.0, 4: 4.0]   // low-mid body
        case .vocal:   return [5: 4.0, 6: 4.0, 7: 2.0]
        case .clarity: return [7: 4.0, 8: 4.0, 9: 1.5]
        case .air:     return [8: 2.0, 9: 5.0]   // top-end sparkle
        }
    }
}

public enum RoomcutMacros {
    public static let inputRange: ClosedRange<Double> = -1.0...1.0

    /// Apply one macro at `value` ∈ [-1, 1] on top of an existing gain snapshot.
    /// Only the macro's target bands move (delta = weight × value), each clamped
    /// to EqBands.gainRange. All other bands pass through untouched.
    public static func apply(_ macro: EqMacro, value: Double, to gains: [Double]) -> [Double] {
        let v = min(inputRange.upperBound, max(inputRange.lowerBound, value))
        var out = gains
        for (index, weight) in macro.weights where index < out.count {
            let next = out[index] + weight * v
            out[index] = min(EqBands.gainRange.upperBound,
                             max(EqBands.gainRange.lowerBound, next))
        }
        return out
    }

    /// Apply an INCREMENTAL macro change: each target band moves by
    /// `weight × delta`, clamped to the band range. Unlike `apply`, `delta` is
    /// not range-clamped, so a knob jump larger than the full range still moves
    /// the bands by the full amount. Used to keep the macro layer additive and
    /// reversible (no double-counting across edits).
    public static func applyDelta(_ macro: EqMacro, delta: Double, to gains: [Double]) -> [Double] {
        var out = gains
        for (index, weight) in macro.weights where index < out.count {
            let next = out[index] + weight * delta
            out[index] = min(EqBands.gainRange.upperBound,
                             max(EqBands.gainRange.lowerBound, next))
        }
        return out
    }
}

// MARK: - Lyrics (LRC parsing)

public struct LyricLine: Equatable {
    public let time: Double   // seconds from track start
    public let text: String
    public init(time: Double, text: String) {
        self.time = time
        self.text = text
    }
}

// Pure LRC parser (kept here so it's unit-testable). Turns synced lyrics like
// "[00:17.12] line" into time-sorted LyricLine values. Metadata tags ([ar:],
// [ti:], …), empty stamps, and blank lines are dropped.
public enum LyricsParsing {
    public static func parse(_ lrc: String) -> [LyricLine] {
        var lines: [LyricLine] = []
        for raw in lrc.components(separatedBy: .newlines) {
            var rest = Substring(raw)
            var stamps: [Double] = []
            // A line can carry several timestamps: "[t1][t2] text".
            while rest.first == "[", let close = rest.firstIndex(of: "]") {
                let tag = rest[rest.index(after: rest.startIndex)..<close]
                if let t = seconds(from: tag) { stamps.append(t) }
                rest = rest[rest.index(after: close)...]
            }
            guard !stamps.isEmpty else { continue }
            let text = String(rest).trimmingCharacters(in: .whitespaces)
            for t in stamps { lines.append(LyricLine(time: t, text: text)) }
        }
        return lines.sorted { $0.time < $1.time }
    }

    // "mm:ss.xx" / "mm:ss" → seconds. Returns nil for non-time tags (e.g. "ar:…").
    static func seconds(from tag: Substring) -> Double? {
        let parts = tag.split(separator: ":")
        guard parts.count == 2, let minutes = Double(String(parts[0])) else { return nil }
        let secText = String(parts[1]).replacingOccurrences(of: ",", with: ".")
        guard let secs = Double(secText) else { return nil }
        return minutes * 60 + secs
    }

    // The line that should be showing at `time` (last stamp ≤ time), or nil.
    public static func line(at time: Double, in lines: [LyricLine]) -> String? {
        lyricLines(at: time, in: lines).current
    }

    public static func lyricLines(
        at time: Double,
        in lines: [LyricLine]
    ) -> (current: String?, next: String?) {
        var current: String?
        var nextStart = lines.startIndex
        for index in lines.indices {
            if lines[index].time <= time {
                current = lines[index].text
                nextStart = lines.index(after: index)
            } else {
                break
            }
        }
        let next = lines[nextStart...].lazy.compactMap { visibleText($0.text) }.first
        return (visibleText(current), next)
    }

    private static func visibleText(_ text: String?) -> String? {
        let trimmed = text?.trimmingCharacters(in: .whitespaces)
        return (trimmed?.isEmpty ?? true) ? nil : trimmed
    }
}
