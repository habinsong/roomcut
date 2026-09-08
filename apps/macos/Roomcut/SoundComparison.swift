import Combine
import Foundation
import CRoomcutClient

public enum LevelMatchState: UInt32 {
    case disabled, measuring, matched, noSignal, bypassed, unavailable
}

public enum EngineComparisonTarget {
    case parameters(EngineParameters)
    case preset(String)
}

struct SoundComparisonWrite {
    let reference: EngineParameters
    let enabled: Bool
}

public struct EngineComparisonState: Equatable {
    public var current: EngineParameters
    public var reference: EngineParameters
    public var presetID: String
    public var enabled: Bool
    public var state: LevelMatchState
    public var revision: UInt64
    public var renderedRevision: UInt64
    public var currentReductionDb: Float
    public var referenceReductionDb: Float

    public init(current: EngineParameters = .flat, reference: EngineParameters = .flat,
                presetID: String = "custom", enabled: Bool = false, state: LevelMatchState = .disabled,
                revision: UInt64 = 0, renderedRevision: UInt64 = 0,
                currentReductionDb: Float = 0, referenceReductionDb: Float = 0) {
        self.current = current; self.reference = reference; self.presetID = presetID
        self.enabled = enabled; self.state = state; self.revision = revision
        self.renderedRevision = renderedRevision; self.currentReductionDb = currentReductionDb
        self.referenceReductionDb = referenceReductionDb
    }

    init(native: RoomcutClientComparison) {
        let presetID = withUnsafeBytes(of: native.presetId) { bytes in
            String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self)
        }
        self.init(current: EngineParameters(native: native.current), reference: EngineParameters(native: native.reference),
                  presetID: presetID, enabled: native.enabled != 0,
                  state: LevelMatchState(rawValue: native.state) ?? .unavailable,
                  revision: native.revision, renderedRevision: native.renderedRevision,
                  currentReductionDb: native.currentReductionDb, referenceReductionDb: native.referenceReductionDb)
    }
}

// Listening utility state is separate from preset values and undo history.
@MainActor
public final class SoundComparison: ObservableObject {
    @Published public private(set) var supported = false
    @Published public private(set) var enabled = false
    @Published public private(set) var state: LevelMatchState = .disabled
    @Published public private(set) var reductionDb: Float = 0

    public init() {}
    func setSupported(_ value: Bool) {
        if supported != value { supported = value }
        if !value { requestEnabled(false) }
    }
    func requestEnabled(_ value: Bool) {
        if enabled != value { enabled = value }
        let next: LevelMatchState = value ? .measuring : .disabled
        if state != next { state = next }
    }
    func accept(_ snapshot: EngineComparisonState) {
        if enabled != snapshot.enabled { enabled = snapshot.enabled }
        let next: LevelMatchState = !snapshot.enabled ? .disabled
            : snapshot.state == .matched && snapshot.revision != snapshot.renderedRevision ? .measuring : snapshot.state
        if state != next { state = next }
        let gain = snapshot.currentReductionDb.isFinite ? max(0, (snapshot.currentReductionDb * 10).rounded() / 10) : 0
        if reductionDb != gain { reductionDb = gain }
    }
    func failed() { if state != .unavailable { state = .unavailable } }
}

extension EngineParameters {
    init(native: RoomcutClientParams) {
        let gains = withUnsafeBytes(of: native.eqGainsDb) { Array($0.bindMemory(to: Double.self)) }
        let bands = withUnsafeBytes(of: native.parametric) { bytes in
            bytes.bindMemory(to: RoomcutClientParamBand.self).map {
                ParametricBand(enabled: $0.enabled != 0, type: Int($0.type), freqHz: $0.freqHz, gainDb: $0.gainDb, q: $0.q)
            }
        }
        self.init(preampDb: native.preampDb, eqGainsDb: gains, limiterReleaseMs: native.limiterReleaseMs,
                  outputGainDb: native.outputGainDb, spatialWidth: native.spatialWidth, centerFocus: native.centerFocus,
                  crossfeed: native.crossfeed, roomReduce: native.roomReduce, spatialMode: native.spatialMode,
                  highpassHz: native.highpassHz, compAmount: native.compAmount, parametric: bands)
    }

    func nativeValues() -> RoomcutClientParams {
        let value = normalized()
        var result = RoomcutClientParams()
        result.preampDb = value.preampDb; result.limiterReleaseMs = value.limiterReleaseMs
        result.outputGainDb = value.outputGainDb; result.spatialWidth = value.spatialWidth
        result.centerFocus = value.centerFocus; result.crossfeed = value.crossfeed
        result.roomReduce = value.roomReduce; result.spatialMode = value.spatialMode
        result.highpassHz = value.highpassHz; result.compAmount = value.compAmount
        withUnsafeMutableBytes(of: &result.eqGainsDb) { bytes in
            let gains = bytes.bindMemory(to: Double.self)
            for b in 0..<Self.bandCount { gains[b] = value.eqGainsDb[b] }
        }
        withUnsafeMutableBytes(of: &result.parametric) { bytes in
            let bands = bytes.bindMemory(to: RoomcutClientParamBand.self)
            for b in 0..<Self.paramBandCount {
                let band = value.parametric[b]
                bands[b].enabled = band.enabled ? 1 : 0; bands[b].type = UInt32(band.type)
                bands[b].freqHz = band.freqHz; bands[b].gainDb = band.gainDb; bands[b].q = band.q
            }
        }
        return result
    }
}
