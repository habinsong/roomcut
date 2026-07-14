import Foundation
import CRoomcutClient
import RoomcutPresentationCore

public func presetIdString(_ state: RoomcutClientState) -> String {
    withUnsafeBytes(of: state.presetId) { raw in
        let bytes = raw.bindMemory(to: CChar.self)
        return String(cString: bytes.baseAddress!)
    }
}

public struct EnginePreset: Identifiable, Hashable {
    public let id: String
    public let name: String

    public init(id: String, name: String) {
        self.id = id
        self.name = name
    }
}

public struct OutputDeviceChoice: Identifiable, Hashable {
    public let uid: String
    public let name: String
    public var id: String { uid }

    public init(uid: String, name: String) {
        self.uid = uid
        self.name = name
    }
}

public struct AudioFormatInfo: Equatable, Sendable {
    public let bitDepth: Int
    public let sampleRate: Double
    public let latencyMs: Double

    public init(bitDepth: Int, sampleRate: Double, latencyMs: Double) {
        self.bitDepth = bitDepth
        self.sampleRate = sampleRate
        self.latencyMs = latencyMs
    }
}

// One physical format (sample rate + bit depth) the real output device supports.
public struct DeviceFormatOption: Equatable, Hashable, Sendable {
    public let sampleRate: Double
    public let bitDepth: Int

    public init(sampleRate: Double, bitDepth: Int) {
        self.sampleRate = sampleRate
        self.bitDepth = bitDepth
    }
}

public struct EngineStatus {
    // The C header's anonymous enum imports as plain Int32 constants.
    public static let stopped = UInt32(ROOMCUT_CLIENT_STATE_STOPPED)
    public static let running = UInt32(ROOMCUT_CLIENT_STATE_RUNNING)
    public static let bypass  = UInt32(ROOMCUT_CLIENT_STATE_BYPASS)
    public static let recover = UInt32(ROOMCUT_CLIENT_STATE_RECOVER)
    public static let spatialParamsCapability = UInt32(ROOMCUT_CLIENT_CAP_SPATIAL_PARAMS)
    public static let parametricCapability = UInt32(ROOMCUT_CLIENT_CAP_PARAMETRIC)
    public static let analyzerCapability = UInt32(ROOMCUT_CLIENT_CAP_ANALYZER)
    public static let dynamicsCapability = UInt32(ROOMCUT_CLIENT_CAP_DYNAMICS)

    public var reachable = false
    public var state: UInt32 = EngineStatus.stopped
    public var presetId = "—"
    public var manualBypass = false
    public var safeBypass = false
    public var limiterGRDb: Float = 0
    public var peak: Float = 0
    public var paramsRevision: UInt32 = 0
    public var frames: UInt64 = 0
    public var underruns: UInt64 = 0
    public var outputDeviceUID = ""
    public var keepDefault = false
    public var capabilities: UInt32 = 0
    public var volumeBoost = 1.0

    public init() {}

    public var supportsSpatialParams: Bool {
        (capabilities & Self.spatialParamsCapability) != 0
    }

    public var supportsParametric: Bool {
        (capabilities & Self.parametricCapability) != 0
    }

    public var supportsAnalyzer: Bool {
        (capabilities & Self.analyzerCapability) != 0
    }

    public var supportsDynamics: Bool {
        (capabilities & Self.dynamicsCapability) != 0
    }

    public var stateName: String {
        guard reachable else { return "OFFLINE" }
        switch state {
        case Self.stopped: return "STOPPED"
        case Self.running: return "RUNNING"
        case Self.bypass:  return "BYPASS"
        case Self.recover: return "RECOVER"
        default: return "?"
        }
    }

    public var menuBarSymbol: String {
        guard reachable else { return "waveform.slash" }
        switch state {
        case Self.running: return "waveform"
        case Self.bypass:  return "waveform.slash"
        // Recovering is transient (the engine reconnects to the driver on every
        // app launch now that it follows the app's lifecycle). Don't flash an
        // alarming ⚠️ in the menu bar for it — the in-app status still shows
        // "복구 중". Treat it as active.
        case Self.recover: return "waveform"
        default: return "waveform.slash"
        }
    }

    public var presentation: RoomcutPresentation.Status {
        RoomcutPresentation.status(reachable: reachable, state: state)
    }
}

// One parametric-EQ band, mirroring RoomcutClientParamBand. `type` indexes the
// filter kinds below (also the engine's BiquadType order).
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

    public init(enabled: Bool = false, type: Int = 0,
                freqHz: Double = 1000, gainDb: Double = 0, q: Double = 1.0) {
        self.enabled = enabled
        self.type = type
        self.freqHz = freqHz
        self.gainDb = gainDb
        self.q = q
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
        self.parametric = Array(parametric.prefix(Self.paramBandCount))
        if self.parametric.count < Self.paramBandCount {
            self.parametric.append(contentsOf:
                repeatElement(ParametricBand(), count: Self.paramBandCount - self.parametric.count))
        }
    }
}

public enum EngineClientError: Error, Equatable {
    case transport(Int32)
}

public protocol EngineClientProtocol: AnyObject {
    var presets: [EnginePreset] { get }
    func getState() async throws -> EngineStatus
    func getParams() async throws -> EngineParameters
    func getAnalysis() async throws -> RoomcutAnalysisSnapshot
    func setPreset(_ presetId: String) async throws
    func setBypass(_ on: Bool) async throws
    func setKeepDefault(_ on: Bool) async throws
    func setParams(_ params: EngineParameters) async throws

    // Output device selection (engine round-trip) + enumeration / volume
    // (in-process CoreAudio, fast). volumeGet returns nil when the device has no
    // controllable volume.
    func outputDevices() -> [OutputDeviceChoice]
    func setOutputDevice(_ uid: String) async throws
    func audioFormat(for uid: String) -> AudioFormatInfo?
    func deviceFormatOptions(for uid: String) -> [DeviceFormatOption]
    func setDeviceFormat(uid: String, sampleRate: Double, bitDepth: Int) async throws
    func volumeGet() -> Double?
    func volumeSet(_ scalar: Double)
    // Output L/R balance (pan), -1 (left) … 0 (centre) … +1 (right), shared with
    // Audio MIDI Setup. balanceGet returns nil when the device has no per-channel
    // volume control.
    func balanceGet() -> Double?
    func balanceSet(_ pan: Double)

    // Make the Roomcut virtual device the macOS default output, so app audio is
    // routed through the engine (where EQ is applied) without the user changing
    // the system output by hand.
    func makeRoomcutDefaultOutput()
}

public extension EngineClientProtocol {
    func audioFormat(for uid: String) -> AudioFormatInfo? { nil }
    func deviceFormatOptions(for uid: String) -> [DeviceFormatOption] { [] }
    func setDeviceFormat(uid: String, sampleRate: Double, bitDepth: Int) async throws {}
    func makeRoomcutDefaultOutput() {}
}

public final class LiveEngineClient: EngineClientProtocol {
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

    public static func phase7Presets(from presets: [EnginePreset]) -> [EnginePreset] {
        presets.filter { phase7PresetIds.contains($0.id) }
    }

    public static func phase6Presets(from presets: [EnginePreset]) -> [EnginePreset] {
        phase7Presets(from: presets)
    }

    public let presets: [EnginePreset]

    private let queue = DispatchQueue(label: "com.roomcut.app.client")

    public init() {
        var out: [EnginePreset] = []
        for i in 0..<roomcutClientPresetCount() {
            var idBuf = [CChar](repeating: 0, count: 32)
            var nameBuf = [CChar](repeating: 0, count: 64)
            if roomcutClientPresetInfo(i, &idBuf, 32, &nameBuf, 64) == 0 {
                out.append(EnginePreset(id: String(cString: idBuf),
                                        name: String(cString: nameBuf)))
            }
        }
        presets = Self.phase7Presets(from: out)
    }

    public func getState() async throws -> EngineStatus {
        try await runOnQueue {
            var c = RoomcutClientState()
            let rc = roomcutClientGetState(&c)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
            var s = EngineStatus()
            s.reachable = true
            s.state = c.state
            s.presetId = presetIdString(c)
            s.manualBypass = c.manualBypass != 0
            s.safeBypass = c.safeBypass != 0
            s.limiterGRDb = c.limiterGainReductionDb
            s.peak = c.renderPeak
            s.paramsRevision = c.paramsRevision
            s.frames = c.framesRendered
            s.underruns = c.ringUnderruns
            s.outputDeviceUID = withUnsafeBytes(of: c.outputDeviceUID) { raw in
                String(cString: raw.bindMemory(to: CChar.self).baseAddress!)
            }
            s.keepDefault = c.keepDefault != 0
            s.capabilities = c.capabilities
            s.volumeBoost = c.volumeBoost
            return s
        }
    }

    public func getParams() async throws -> EngineParameters {
        try await runOnQueue {
            var c = RoomcutClientParams()
            let rc = roomcutClientGetParams(&c)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
            let gains = withUnsafeBytes(of: c.eqGainsDb) { raw -> [Double] in
                Array(raw.bindMemory(to: Double.self))
            }
            let bands = withUnsafeBytes(of: c.parametric) { raw -> [ParametricBand] in
                raw.bindMemory(to: RoomcutClientParamBand.self).map {
                    ParametricBand(enabled: $0.enabled != 0, type: Int($0.type),
                                   freqHz: $0.freqHz, gainDb: $0.gainDb, q: $0.q)
                }
            }
            return EngineParameters(
                preampDb: c.preampDb,
                eqGainsDb: gains,
                limiterReleaseMs: c.limiterReleaseMs,
                outputGainDb: c.outputGainDb,
                spatialWidth: c.spatialWidth,
                centerFocus: c.centerFocus,
                crossfeed: c.crossfeed,
                roomReduce: c.roomReduce,
                spatialMode: c.spatialMode,
                highpassHz: c.highpassHz,
                compAmount: c.compAmount,
                parametric: bands
            )
        }
    }

    public func setPreset(_ presetId: String) async throws {
        try await runOnQueue {
            let rc = roomcutClientSetPreset(presetId)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func setBypass(_ on: Bool) async throws {
        try await runOnQueue {
            let rc = roomcutClientSetBypass(on ? 1 : 0)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func setKeepDefault(_ on: Bool) async throws {
        try await runOnQueue {
            let rc = roomcutClientSetKeepDefault(on ? 1 : 0)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func getAnalysis() async throws -> RoomcutAnalysisSnapshot {
        try await runOnQueue {
            var c = RoomcutClientAnalysis()
            let rc = roomcutClientGetAnalysis(&c)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
            let spectrum = withUnsafeBytes(of: c.spectrum) { raw -> [Float] in
                let floats = raw.bindMemory(to: Float.self)
                return Array(floats.prefix(RoomcutAnalysisSnapshot.spectrumBinCount))
            }
            return RoomcutAnalysisSnapshot(
                valid: c.valid != 0,
                sampleRate: c.sampleRate,
                channels: c.channels,
                framesAnalyzed: c.framesAnalyzed,
                peakDb: c.peakDb,
                rmsDb: c.rmsDb,
                crestFactor: c.crestFactor,
                lowEnergy: c.lowEnergy,
                lowMidEnergy: c.lowMidEnergy,
                midEnergy: c.midEnergy,
                highEnergy: c.highEnergy,
                spectralCentroid: c.spectralCentroid,
                stereoWidth: c.stereoWidth,
                midSideRatio: c.midSideRatio,
                correlation: c.correlation,
                muddiness: c.muddiness,
                harshness: c.harshness,
                sibilance: c.sibilance,
                voicePresence: c.voicePresence,
                reverbEstimate: c.reverbEstimate,
                dynamicRange: c.dynamicRange,
                spectrum: spectrum)
        }
    }

    public func setParams(_ params: EngineParameters) async throws {
        try await runOnQueue {
            let cbands = params.parametric.map { b -> RoomcutClientParamBand in
                var cb = RoomcutClientParamBand()
                cb.enabled = b.enabled ? 1 : 0
                cb.type = UInt32(max(0, b.type))
                cb.freqHz = b.freqHz
                cb.gainDb = b.gainDb
                cb.q = b.q
                return cb
            }
            let rc = params.eqGainsDb.withUnsafeBufferPointer { buf in
                cbands.withUnsafeBufferPointer { pbuf in
                    roomcutClientSetParams(
                        params.preampDb,
                        buf.baseAddress,
                        params.limiterReleaseMs,
                        params.outputGainDb,
                        params.spatialWidth,
                        params.centerFocus,
                        params.crossfeed,
                        params.roomReduce,
                        params.spatialMode,
                        params.highpassHz,
                        params.compAmount,
                        pbuf.baseAddress
                    )
                }
            }
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func outputDevices() -> [OutputDeviceChoice] {
        var out: [OutputDeviceChoice] = []
        let n = roomcutClientOutputDeviceCount()
        for i in 0..<n {
            var uidBuf = [CChar](repeating: 0, count: 128)
            var nameBuf = [CChar](repeating: 0, count: 128)
            if roomcutClientOutputDeviceInfo(i, &uidBuf, 128, &nameBuf, 128) == 0 {
                out.append(OutputDeviceChoice(uid: String(cString: uidBuf),
                                              name: String(cString: nameBuf)))
            }
        }
        return out
    }

    public func setOutputDevice(_ uid: String) async throws {
        try await runOnQueue {
            let rc = roomcutClientSetOutputDevice(uid)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func audioFormat(for uid: String) -> AudioFormatInfo? {
        var format = RoomcutClientAudioFormat()
        guard roomcutClientAudioFormat(uid, &format) == 0 else { return nil }
        return AudioFormatInfo(
            bitDepth: Int(format.bitDepth),
            sampleRate: format.sampleRate,
            latencyMs: format.latencyMs)
    }

    public func deviceFormatOptions(for uid: String) -> [DeviceFormatOption] {
        var buf = [RoomcutClientDeviceFormat](repeating: RoomcutClientDeviceFormat(), count: 64)
        let n = roomcutClientDeviceFormatOptions(uid, &buf, Int32(buf.count))
        guard n > 0 else { return [] }
        let count = min(Int(n), buf.count)
        return (0..<count).map {
            DeviceFormatOption(sampleRate: buf[$0].sampleRate, bitDepth: Int(buf[$0].bitDepth))
        }
    }

    public func setDeviceFormat(uid: String, sampleRate: Double, bitDepth: Int) async throws {
        try await runOnQueue {
            let rc = roomcutClientSetDeviceFormat(uid, sampleRate, UInt32(bitDepth))
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    public func volumeGet() -> Double? {
        var v: Double = 0
        return roomcutClientVolumeGet(&v) == 0 ? v : nil
    }

    public func volumeSet(_ scalar: Double) {
        _ = roomcutClientVolumeSet(scalar)
    }

    public func balanceGet() -> Double? {
        var p: Double = 0
        return roomcutClientBalanceGet(&p) == 0 ? p : nil
    }

    public func balanceSet(_ pan: Double) {
        _ = roomcutClientBalanceSet(pan)
    }

    public func makeRoomcutDefaultOutput() {
        _ = roomcutClientMakeDefaultOutput()
    }

    private func runOnQueue<T>(_ body: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            queue.async {
                do {
                    continuation.resume(returning: try body())
                } catch {
                    continuation.resume(throwing: error)
                }
            }
        }
    }
}
