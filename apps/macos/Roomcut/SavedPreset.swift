import Foundation

// A user-saved EQ preset — the FULL editable state (10-band, Basic macros,
// Parametric, Spatial, Limiter), persisted in UserDefaults. `folder` groups it in
// the preset tree. All post-v1 fields decode-if-present so old saves still load.
public struct SavedPreset: Codable, Identifiable, Equatable {
    public var name: String
    public var preampDb: Double
    public var eqGainsDb: [Double]
    public var outputGainDb: Double
    public var spatialWidth: Double
    public var centerFocus: Double
    public var crossfeed: Double
    public var roomReduce: Double
    public var spatialMode: Double
    // v2: also capture Basic macros, Parametric bands and the Limiter, plus a tree
    // folder. v3 adds dynamics (highpassHz/compAmount). v4 adds the virtual room
    // (roomType/roomAmount) and the upmix (surroundType and its two trims). v5
    // adds the headphone bed renderer. Defaults keep older saves valid.
    public var eqMacros: [String: Double]
    public var parametric: [ParametricBand]
    public var limiterReleaseMs: Double
    public var highpassHz: Double
    public var compAmount: Double
    public var roomType: Double
    public var roomAmount: Double
    public var surroundType: Double
    public var centerWidth: Double
    public var surroundDepth: Double
    public var bedRenderer: Double
    public var folder: String?
    public var builtin: Bool   // app-shipped library entry (not user-created)
    public var roomTuneInfo: String?   // Room Tune measurement summary (date · devices · bands)
    public var id: String { folder.map { "\($0)/\(name)" } ?? name }

    public init(name: String,
                preampDb: Double,
                eqGainsDb: [Double],
                outputGainDb: Double,
                spatialWidth: Double = 0,
                centerFocus: Double = 0,
                crossfeed: Double = 0,
                roomReduce: Double = 0,
                spatialMode: Double = 0,
                eqMacros: [String: Double] = [:],
                parametric: [ParametricBand] = [],
                limiterReleaseMs: Double = 100.0,
                highpassHz: Double = 0,
                compAmount: Double = 0,
                roomType: Double = 0,
                roomAmount: Double = 50,
                surroundType: Double = 0,
                centerWidth: Double = 100,
                surroundDepth: Double = 50,
                bedRenderer: Double = 0,
                folder: String? = nil,
                builtin: Bool = false,
                roomTuneInfo: String? = nil) {
        self.name = name
        self.preampDb = preampDb
        self.eqGainsDb = eqGainsDb
        self.outputGainDb = outputGainDb
        self.spatialWidth = spatialWidth
        self.centerFocus = centerFocus
        self.crossfeed = crossfeed
        self.roomReduce = roomReduce
        self.spatialMode = spatialMode
        self.eqMacros = eqMacros
        self.parametric = parametric
        self.limiterReleaseMs = limiterReleaseMs
        self.highpassHz = highpassHz
        self.compAmount = compAmount
        self.roomType = roomType
        self.roomAmount = roomAmount
        self.surroundType = surroundType
        self.centerWidth = centerWidth
        self.surroundDepth = surroundDepth
        self.bedRenderer = bedRenderer
        self.folder = folder
        self.builtin = builtin
        self.roomTuneInfo = roomTuneInfo
    }

    enum CodingKeys: String, CodingKey {
        case name, preampDb, eqGainsDb, outputGainDb
        case spatialWidth, centerFocus, crossfeed, roomReduce, spatialMode
        case eqMacros, parametric, limiterReleaseMs, highpassHz, compAmount
        case roomType, roomAmount
        case surroundType, centerWidth, surroundDepth, bedRenderer
        case folder, builtin, roomTuneInfo
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decode(String.self, forKey: .name)
        preampDb = try c.decode(Double.self, forKey: .preampDb)
        eqGainsDb = try c.decode([Double].self, forKey: .eqGainsDb)
        outputGainDb = try c.decode(Double.self, forKey: .outputGainDb)
        spatialWidth = try c.decodeIfPresent(Double.self, forKey: .spatialWidth) ?? 0
        centerFocus = try c.decodeIfPresent(Double.self, forKey: .centerFocus) ?? 0
        crossfeed = try c.decodeIfPresent(Double.self, forKey: .crossfeed) ?? 0
        roomReduce = try c.decodeIfPresent(Double.self, forKey: .roomReduce) ?? 0
        spatialMode = try c.decodeIfPresent(Double.self, forKey: .spatialMode) ?? 0
        eqMacros = try c.decodeIfPresent([String: Double].self, forKey: .eqMacros) ?? [:]
        parametric = try c.decodeIfPresent([ParametricBand].self, forKey: .parametric) ?? []
        limiterReleaseMs = try c.decodeIfPresent(Double.self, forKey: .limiterReleaseMs) ?? 100.0
        highpassHz = try c.decodeIfPresent(Double.self, forKey: .highpassHz) ?? 0
        compAmount = try c.decodeIfPresent(Double.self, forKey: .compAmount) ?? 0
        roomType = try c.decodeIfPresent(Double.self, forKey: .roomType) ?? 0
        roomAmount = try c.decodeIfPresent(Double.self, forKey: .roomAmount) ?? 50
        surroundType = try c.decodeIfPresent(Double.self, forKey: .surroundType) ?? 0
        centerWidth = try c.decodeIfPresent(Double.self, forKey: .centerWidth) ?? 100
        surroundDepth = try c.decodeIfPresent(Double.self, forKey: .surroundDepth) ?? 50
        bedRenderer = try c.decodeIfPresent(Double.self, forKey: .bedRenderer) ?? 0
        folder = try c.decodeIfPresent(String.self, forKey: .folder)
        builtin = try c.decodeIfPresent(Bool.self, forKey: .builtin) ?? false
        roomTuneInfo = try c.decodeIfPresent(String.self, forKey: .roomTuneInfo)
    }
}

