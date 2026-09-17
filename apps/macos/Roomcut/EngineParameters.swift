import Foundation
import CRoomcutClient
import RoomcutPresentationCore

// The parameter set the app and engine exchange, plus one parametric band.
// A mirror of the native ChainParams, kept as a Swift value type so edits,
// undo history and A/B comparison can all copy it around freely.
public struct ParametricBand: Equatable, Codable {
    public enum Kind: Int, CaseIterable, Identifiable {
        case bell = 0, lowShelf = 1, highShelf = 2, highPass = 3, lowPass = 4, notch = 5
        public var id: Int { rawValue }
        public var label: String {
            switch self {
            case .bell:      return "Bell"
            case .lowShelf:  return "Low Shelf"
            case .highShelf: return "High Shelf"
            case .highPass:  return "High Pass"
            case .lowPass:   return "Low Pass"
            case .notch:     return "Notch"
            }
        }
        // Pass/notch filters ignore gain; the UI hides the gain control for them.
        public var usesGain: Bool { self == .bell || self == .lowShelf || self == .highShelf }
    }

    public var enabled: Bool
    public var type: Int
    public var freqHz: Double
    public var gainDb: Double
    public var q: Double
    // Optional dynamic side, mirroring RoomcutClientParamDynamics. `dynamic` off
    // is the static band, and older presets decode to exactly that.
    public var dynamic: Bool
    public var thresholdDb: Double
    public var rangeDb: Double
    public var attackMs: Double
    public var releaseMs: Double

    public init(enabled: Bool = false, type: Int = 0,
                freqHz: Double = 1000, gainDb: Double = 0, q: Double = 1.0,
                dynamic: Bool = false, thresholdDb: Double = -24, rangeDb: Double = 0,
                attackMs: Double = 20, releaseMs: Double = 200) {
        self.enabled = enabled
        self.type = type
        self.freqHz = freqHz
        self.gainDb = gainDb
        self.q = q
        self.dynamic = dynamic
        self.thresholdDb = thresholdDb
        self.rangeDb = rangeDb
        self.attackMs = attackMs
        self.releaseMs = releaseMs
    }

    private enum CodingKeys: String, CodingKey {
        case enabled, type, freqHz, gainDb, q, dynamic, thresholdDb, rangeDb, attackMs, releaseMs
    }

    // A preset written before the dynamic side existed has none of those keys.
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        enabled = try c.decodeIfPresent(Bool.self, forKey: .enabled) ?? false
        type = try c.decodeIfPresent(Int.self, forKey: .type) ?? 0
        freqHz = try c.decodeIfPresent(Double.self, forKey: .freqHz) ?? 1000
        gainDb = try c.decodeIfPresent(Double.self, forKey: .gainDb) ?? 0
        q = try c.decodeIfPresent(Double.self, forKey: .q) ?? 1.0
        dynamic = try c.decodeIfPresent(Bool.self, forKey: .dynamic) ?? false
        thresholdDb = try c.decodeIfPresent(Double.self, forKey: .thresholdDb) ?? -24
        rangeDb = try c.decodeIfPresent(Double.self, forKey: .rangeDb) ?? 0
        attackMs = try c.decodeIfPresent(Double.self, forKey: .attackMs) ?? 20
        releaseMs = try c.decodeIfPresent(Double.self, forKey: .releaseMs) ?? 200
    }

    // Only write the dynamic keys when they mean something, so a static band's
    // JSON stays byte-for-byte what it used to be.
    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(enabled, forKey: .enabled)
        try c.encode(type, forKey: .type)
        try c.encode(freqHz, forKey: .freqHz)
        try c.encode(gainDb, forKey: .gainDb)
        try c.encode(q, forKey: .q)
        guard dynamic else { return }
        try c.encode(dynamic, forKey: .dynamic)
        try c.encode(thresholdDb, forKey: .thresholdDb)
        try c.encode(rangeDb, forKey: .rangeDb)
        try c.encode(attackMs, forKey: .attackMs)
        try c.encode(releaseMs, forKey: .releaseMs)
    }

    public var kind: Kind { Kind(rawValue: type) ?? .bell }
}

public struct EngineParameters: Equatable {
    public static let bandCount = Int(ROOMCUT_CLIENT_EQ_BANDS)
    public static let paramBandCount = Int(ROOMCUT_CLIENT_PARAM_BANDS)
    public static let flat = EngineParameters(
        preampDb: 0,
        eqGainsDb: Array(repeating: 0, count: bandCount),
        outputGainDb: 0,
        spatialWidth: 0,
        centerFocus: 0,
        crossfeed: 0,
        roomReduce: 0
    )

    public var preampDb: Double
    public var eqGainsDb: [Double]
    public var limiterReleaseMs: Double
    public var outputGainDb: Double
    public var spatialWidth: Double
    public var centerFocus: Double
    public var crossfeed: Double
    public var roomReduce: Double
    public var spatialMode: Double   // 0 = speaker (XTC), 1 = headphone (crossfeed)
    public var highpassHz: Double    // dynamics: 0 = off
    public var compAmount: Double    // dynamics: 0..100 leveling amount, 0 = off
    public var roomType: Double      // virtual room: 0 = off, 1 = Studio, 2 = Living Room, 3 = Hall
    public var roomAmount: Double    // virtual room: 0..100, 50 = reference level
    public var surroundType: Double    // upmix: 0 = off, 2 = virtual 5.1, 3 = virtual 7.1
    public var centerWidth: Double   // upmix centre steering, 0..100
    public var surroundDepth: Double // upmix surround steering, 0..100
    public var bedRenderer: Double   // headphone 5.1/7.1 bed: 0 = system renderer (Apple), 1 = built-in
    public var parametric: [ParametricBand]

    public init(preampDb: Double,
                eqGainsDb: [Double],
                limiterReleaseMs: Double = 100.0,
                outputGainDb: Double,
                spatialWidth: Double = 0.0,
                centerFocus: Double = 0.0,
                crossfeed: Double = 0.0,
                roomReduce: Double = 0.0,
                spatialMode: Double = 0.0,
                highpassHz: Double = 0.0,
                compAmount: Double = 0.0,
                roomType: Double = 0.0,
                roomAmount: Double = 50.0,
                surroundType: Double = 0.0,
                centerWidth: Double = 100.0,
                surroundDepth: Double = 50.0,
                bedRenderer: Double = 0.0,
                parametric: [ParametricBand] = []) {
        self.preampDb = preampDb
        self.eqGainsDb = Array(eqGainsDb.prefix(Self.bandCount))
        if self.eqGainsDb.count < Self.bandCount {
            self.eqGainsDb.append(contentsOf: repeatElement(0, count: Self.bandCount - self.eqGainsDb.count))
        }
        self.limiterReleaseMs = limiterReleaseMs
        self.outputGainDb = outputGainDb
        self.spatialWidth = spatialWidth
        self.centerFocus = centerFocus
        self.crossfeed = crossfeed
        self.roomReduce = roomReduce
        self.spatialMode = spatialMode
        self.highpassHz = highpassHz
        self.compAmount = compAmount
        self.roomType = roomType
        self.roomAmount = roomAmount
        self.surroundType = surroundType
        self.centerWidth = centerWidth
        self.surroundDepth = surroundDepth
        self.bedRenderer = bedRenderer
        self.parametric = Array(parametric.prefix(Self.paramBandCount))
        if self.parametric.count < Self.paramBandCount {
            self.parametric.append(contentsOf:
                repeatElement(ParametricBand(), count: Self.paramBandCount - self.parametric.count))
        }
    }
}
