import Foundation

public enum RoomcutPresentation {
    public enum StatusRole: Equatable {
        case normal
        case bypass
        case warning
        case offline
    }

    public struct Status: Equatable {
        public let label: String
        public let symbol: String
        public let role: StatusRole
    }

    public static func status(reachable: Bool, state: UInt32) -> Status {
        guard reachable else {
            return Status(label: "연결 끊김", symbol: "waveform.slash", role: .offline)
        }

        switch state {
        case 1:
            return Status(label: "실행 중", symbol: "waveform", role: .normal)
        case 2:
            return Status(label: "바이패스", symbol: "waveform.slash", role: .bypass)
        case 3:
            return Status(label: "복구 중", symbol: "exclamationmark.triangle", role: .warning)
        case 0:
            return Status(label: "오프라인", symbol: "waveform.slash", role: .offline)
        default:
            return Status(label: "알 수 없음", symbol: "questionmark.circle", role: .warning)
        }
    }

    public static func peakDbFS(_ peak: Float) -> Double {
        guard peak > 0 else { return -60 }
        let db = 20 * log10(Double(peak))
        if db <= -59.999 { return -60 }
        return min(0, max(-60, db))
    }

    public static func shouldShowLimiter(gainReductionDb: Float) -> Bool {
        abs(gainReductionDb) > 0.05
    }

    public static let phase7PresetIds: Set<String> = [
        "flat",
        "clean",
        "dialogue",
        "original-focus",
        "widen",
        "night",
        "soft",
        "laptop-speaker",
        "airpods",
    ]

    public static let phase6PresetIds = phase7PresetIds

    public static let phase7PresetOrder: [String] = [
        "flat", "clean", "dialogue", "original-focus", "widen", "night", "soft", "laptop-speaker", "airpods",
    ]

    public static let phase6PresetOrder = phase7PresetOrder
}

public struct RoomcutAnalysisSnapshot: Equatable, Sendable {
    public static let spectrumBinCount = 24

    public var valid: Bool
    public var sampleRate: UInt32
    public var channels: UInt32
    public var framesAnalyzed: UInt64
    public var peakDb: Float
    public var rmsDb: Float
    public var crestFactor: Float
    public var lowEnergy: Float
    public var lowMidEnergy: Float
    public var midEnergy: Float
    public var highEnergy: Float
    public var spectralCentroid: Float
    public var stereoWidth: Float
    public var midSideRatio: Float
    public var correlation: Float
    public var muddiness: Float
    public var harshness: Float
    public var sibilance: Float
    public var voicePresence: Float
    public var reverbEstimate: Float
    public var dynamicRange: Float
    public var spectrum: [Float]

    public init(
        valid: Bool = false,
        sampleRate: UInt32 = 0,
        channels: UInt32 = 0,
        framesAnalyzed: UInt64 = 0,
        peakDb: Float = -120,
        rmsDb: Float = -120,
        crestFactor: Float = 0,
        lowEnergy: Float = 0,
        lowMidEnergy: Float = 0,
        midEnergy: Float = 0,
        highEnergy: Float = 0,
        spectralCentroid: Float = 0,
        stereoWidth: Float = 0,
        midSideRatio: Float = 1,
        correlation: Float = 1,
        muddiness: Float = 0,
        harshness: Float = 0,
        sibilance: Float = 0,
        voicePresence: Float = 0,
        reverbEstimate: Float = 0,
        dynamicRange: Float = 0,
        spectrum: [Float] = Array(repeating: 0, count: spectrumBinCount)
    ) {
        self.valid = valid
        self.sampleRate = sampleRate
        self.channels = channels
        self.framesAnalyzed = framesAnalyzed
        self.peakDb = peakDb
        self.rmsDb = rmsDb
        self.crestFactor = crestFactor
        self.lowEnergy = lowEnergy
        self.lowMidEnergy = lowMidEnergy
        self.midEnergy = midEnergy
        self.highEnergy = highEnergy
        self.spectralCentroid = spectralCentroid
        self.stereoWidth = stereoWidth
        self.midSideRatio = midSideRatio
        self.correlation = correlation
        self.muddiness = muddiness
        self.harshness = harshness
        self.sibilance = sibilance
        self.voicePresence = voicePresence
        self.reverbEstimate = reverbEstimate
        self.dynamicRange = dynamicRange
        if spectrum.count == Self.spectrumBinCount {
            self.spectrum = spectrum.map { min(1, max(0, $0)) }
        } else {
            self.spectrum = Array(repeating: 0, count: Self.spectrumBinCount)
        }
    }
}

public enum RoomcutAnalysisPresentation {
    public static func labels(for a: RoomcutAnalysisSnapshot?) -> [String] {
        guard let a, a.valid else { return ["No Signal"] }
        var out: [String] = []
        if a.stereoWidth >= 0.62 {
            out.append("Wide")
        } else if a.stereoWidth <= 0.18 {
            out.append("Centered")
        } else {
            out.append("Natural")
        }

        if a.reverbEstimate >= 0.55 {
            out.append("Live")
        } else if a.reverbEstimate <= 0.20 {
            out.append("Dry")
        }

        if a.muddiness >= 0.58 {
            out.append("Muddy")
        } else if a.muddiness >= 0.34 {
            out.append("Slightly Muddy")
        } else if a.harshness >= 0.55 {
            out.append("Harsh")
        } else if a.sibilance >= 0.55 {
            out.append("Sibilant")
        } else if a.voicePresence >= 0.42 {
            out.append("Voice Clear")
        }

        if a.peakDb >= -1.0 {
            out.append("Hot")
        } else if a.dynamicRange <= 6.0 {
            out.append("Compressed")
        } else {
            out.append("Safe")
        }
        return Array(out.prefix(4))
    }

    public static func currentSound(for a: RoomcutAnalysisSnapshot?) -> String {
        labels(for: a).joined(separator: " · ")
    }

    public static func db(_ value: Float) -> String {
        if value <= -119 { return "−∞ dB" }
        let rounded = value.rounded()
        if rounded == 0 { return "0 dB" }
        return String(format: "%+.0f dB", rounded).replacingOccurrences(of: "-", with: "−")
    }

    public static func hz(_ value: Float) -> String {
        let rounded = (value / 100).rounded() * 100
        if value >= 1000 {
            return String(format: "%.1f kHz", rounded / 1000)
        }
        return String(format: "%.0f Hz", rounded)
    }

    public static func percent(_ value: Float) -> String {
        let pct = min(1, max(0, value)) * 100
        return "\(Int((pct / 5).rounded() * 5))%"
    }
}

// Graphic-EQ band geometry, shared by the editor view and its tests. This is
// the *control* layout (band centers + log-frequency placement), NOT a measured
// spectrum — the EQ curve is an interpolation of these control points.
public enum EqBands {
    public static let count = 10
    public static let centersHz: [Double] = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    public static let labels: [String] = ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    public static let gainRange: ClosedRange<Double> = -24...24
    public static let gainStep: Double = 0.5

    // Normalized x in [0,1] on a log-frequency axis spanning the band centers.
    // Band 0 (31 Hz) maps to 0, band 9 (16 kHz) to 1; spacing is logarithmic so
    // the curve reads like an audio EQ rather than a linear-frequency plot.
    public static func normalizedX(_ index: Int) -> Double {
        let lo = log10(centersHz.first!)
        let hi = log10(centersHz.last!)
        return (log10(centersHz[index]) - lo) / (hi - lo)
    }

    // Map a gain (dB) within gainRange to a normalized y in [0,1] where 0 dB is
    // the vertical center (0.5), +range at the top (1), −range at the bottom (0).
    public static func normalizedY(gainDb: Double) -> Double {
        let span = gainRange.upperBound - gainRange.lowerBound
        return (gainDb - gainRange.lowerBound) / span
    }
}
