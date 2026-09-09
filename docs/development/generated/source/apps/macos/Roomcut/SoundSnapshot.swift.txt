import Foundation
import RoomcutPresentationCore

public struct SoundSnapshot: Equatable {
    public var parameters: EngineParameters
    public var macros: [EqMacro: Double]
    public var savedPresetName: String?
    public var builtinPresetID: String?

    public init(parameters: EngineParameters = .flat, macros: [EqMacro: Double] = [:],
                savedPresetName: String? = nil, builtinPresetID: String? = nil) {
        self.parameters = parameters.normalized()
        self.macros = macros.mapValues { Self.clamp($0, -1, 1) }
        self.savedPresetName = savedPresetName
        self.builtinPresetID = builtinPresetID
    }

    init(preset: SavedPreset) {
        self.init(parameters: EngineParameters(
            preampDb: preset.preampDb, eqGainsDb: preset.eqGainsDb,
            limiterReleaseMs: preset.limiterReleaseMs, outputGainDb: preset.outputGainDb,
            spatialWidth: preset.spatialWidth, centerFocus: preset.centerFocus,
            crossfeed: preset.crossfeed, roomReduce: preset.roomReduce, spatialMode: preset.spatialMode,
            highpassHz: preset.highpassHz, compAmount: preset.compAmount, parametric: preset.parametric),
            macros: Dictionary(uniqueKeysWithValues: preset.eqMacros.compactMap { key, value in
                EqMacro(rawValue: key).map { ($0, value) }
            }), savedPresetName: preset.builtin ? nil : preset.name,
            builtinPresetID: preset.builtin ? PresetLibrary.token(for: preset) : nil)
    }

    func preset(name: String, folder: String?, roomTuneInfo: String?) -> SavedPreset {
        let p = parameters
        return SavedPreset(name: name, preampDb: p.preampDb, eqGainsDb: p.eqGainsDb,
            outputGainDb: p.outputGainDb, spatialWidth: p.spatialWidth, centerFocus: p.centerFocus,
            crossfeed: p.crossfeed, roomReduce: p.roomReduce, spatialMode: p.spatialMode,
            eqMacros: Dictionary(uniqueKeysWithValues: macros.map { ($0.key.rawValue, $0.value) }),
            parametric: p.parametric, limiterReleaseMs: p.limiterReleaseMs,
            highpassHz: p.highpassHz, compAmount: p.compAmount, folder: folder, roomTuneInfo: roomTuneInfo)
    }

    static func clamp(_ value: Double, _ minimum: Double, _ maximum: Double) -> Double {
        value.isFinite ? min(maximum, max(minimum, value)) : minimum
    }
}

extension EngineParameters {
    // Same ranges as the native PresetValidator. Normalize array sizes before
    // exposing imported or restored values to indexed UI controls.
    func normalized() -> EngineParameters {
        var p = EngineParameters(preampDb: preampDb, eqGainsDb: eqGainsDb,
            limiterReleaseMs: limiterReleaseMs, outputGainDb: outputGainDb,
            spatialWidth: spatialWidth, centerFocus: centerFocus, crossfeed: crossfeed,
            roomReduce: roomReduce, spatialMode: spatialMode, highpassHz: highpassHz,
            compAmount: compAmount, parametric: parametric)
        let clamp = SoundSnapshot.clamp
        p.preampDb = clamp(p.preampDb, -24, 12)
        p.outputGainDb = clamp(p.outputGainDb, -24, 12)
        p.eqGainsDb = p.eqGainsDb.map { clamp($0, -24, 24) }
        p.limiterReleaseMs = clamp(p.limiterReleaseMs, 5, 500)
        p.spatialWidth = clamp(p.spatialWidth, -200, 200)
        p.centerFocus = clamp(p.centerFocus, 0, 200)
        p.crossfeed = clamp(p.crossfeed, 0, 100)
        p.roomReduce = clamp(p.roomReduce, 0, 200)
        p.spatialMode = clamp(p.spatialMode, 0, 3).rounded()
        p.highpassHz = clamp(p.highpassHz, 0, 400)
        p.compAmount = clamp(p.compAmount, 0, 100)
        p.parametric = p.parametric.map { band in
            let tonal = (0...2).contains(band.type)
            return ParametricBand(enabled: band.enabled, type: (0...5).contains(band.type) ? band.type : 0,
                freqHz: clamp(band.freqHz, 20, 20000), gainDb: clamp(band.gainDb, -24, 24),
                q: clamp(band.q, 0.1, 12),
                // Only a bell or shelf has a gain to take away.
                dynamic: band.dynamic && tonal,
                thresholdDb: clamp(band.thresholdDb, -60, 0), rangeDb: clamp(band.rangeDb, 0, 24),
                attackMs: clamp(band.attackMs, 1, 200), releaseMs: clamp(band.releaseMs, 10, 2000))
        }
        return p
    }

    func supported(by status: EngineStatus) -> EngineParameters {
        var p = normalized()
        if !status.supportsSpatialParams {
            p.spatialWidth = 0; p.centerFocus = 0; p.crossfeed = 0; p.roomReduce = 0; p.spatialMode = 0
        }
        if !status.supportsDynamics { p.highpassHz = 0; p.compAmount = 0 }
        if !status.supportsDynamicEq {
            p.parametric = p.parametric.map {
                var band = $0; band.dynamic = false; return band
            }
        }
        if !status.supportsParametric { p.parametric = Array(repeating: ParametricBand(), count: Self.paramBandCount) }
        return p
    }
}
