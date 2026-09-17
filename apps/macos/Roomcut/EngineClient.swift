import Foundation
import CRoomcutClient
import RoomcutPresentationCore

public func presetIdString(_ state: RoomcutClientState) -> String {
    withUnsafeBytes(of: state.presetId) { raw in
        let bytes = raw.bindMemory(to: CChar.self)
        return String(cString: bytes.baseAddress!)
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
    func getComparison() async throws -> EngineComparisonState
    nonisolated func sendHeadPose(yawDegrees: Double, active: Bool)
    func setComparison(_ target: EngineComparisonTarget, reference: EngineParameters, enabled: Bool) async throws

    // Native device access can block on CoreAudio or Mach. UI polling uses
    // deviceReadback instead of invoking these getters on the main actor.
    // volumeGet returns nil when the device has no controllable volume.
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
    // Master switch OFF: route the system default output back to the real device
    // so audio bypasses Roomcut entirely.
    func restoreRealDefaultOutput()
    // Is Roomcut currently the macOS default output? (drives the master toggle)
    func roomcutIsDefaultOutput() -> Bool

    @MainActor func deviceReadback(for uid: String, includeDevices: Bool, includeControls: Bool) async -> DeviceReadback
    @MainActor func writeVolume(_ scalar: Double) async throws
    @MainActor func writeBalance(_ pan: Double) async throws
    @MainActor func setDefaultOutput(roomcut: Bool) async throws
}

public extension EngineClientProtocol {
    @MainActor func writeVolume(_ scalar: Double) async throws { volumeSet(scalar) }
    @MainActor func writeBalance(_ pan: Double) async throws { balanceSet(pan) }
    @MainActor func setDefaultOutput(roomcut: Bool) async throws {
        if roomcut { makeRoomcutDefaultOutput() } else { restoreRealDefaultOutput() }
    }
    // Preserve actor-isolated fixtures/custom clients. The live implementation
    // explicitly dispatches blocking HAL work to its device-read queue.
    @MainActor func deviceReadback(for uid: String, includeDevices: Bool, includeControls: Bool) async -> DeviceReadback {
        DeviceReadback.capture(using: self, uid: uid, devices: includeDevices, controls: includeControls)
    }
    func getComparison() async throws -> EngineComparisonState { throw EngineClientError.transport(-3) }
    // Live head orientation. Fire-and-forget by design: at the tracker's rate a
    // dropped update is replaced by the next one a few milliseconds later, and
    // waiting on each would stall the motion callback.
    nonisolated func sendHeadPose(yawDegrees: Double, active: Bool) {}
    func setComparison(_ target: EngineComparisonTarget, reference: EngineParameters, enabled: Bool) async throws {
        throw EngineClientError.transport(-3)
    }
    func audioFormat(for uid: String) -> AudioFormatInfo? { nil }
    func deviceFormatOptions(for uid: String) -> [DeviceFormatOption] { [] }
    func setDeviceFormat(uid: String, sampleRate: Double, bitDepth: Int) async throws {}
    func makeRoomcutDefaultOutput() {}
    func restoreRealDefaultOutput() {}
    func roomcutIsDefaultOutput() -> Bool { false }
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
    private let deviceQueue = DispatchQueue(label: "com.roomcut.app.device-reads", qos: .userInitiated)
    private let deviceWriteQueue = DispatchQueue(label: "com.roomcut.app.device-writes", qos: .userInitiated)
    private let changeDefaultOutput: (Bool) -> Int32

    public convenience init() {
        self.init(changeDefaultOutput: { roomcut in
            roomcut ? roomcutClientMakeDefaultOutput() : roomcutClientRestoreRealDefault()
        })
    }

    init(changeDefaultOutput: @escaping (Bool) -> Int32) {
        self.changeDefaultOutput = changeDefaultOutput
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
            // Remember what this engine understands. setComparison has to send
            // "off" as an actual zero, and it can only know how much of the
            // message the engine will read from these bits.
            Self.rememberComparisonVersion(for: c.capabilities)
            s.volumeBoost = c.volumeBoost
            s.engineLatencyMs = c.engineLatencyMs
            s.systemBedRenderer = c.bedRenderer != 0
            s.bedPersonalizedHrtf = c.bedPersonalizedHrtf != 0
            s.bedExternalGain = c.bedExternalGain
            s.bedUnitRate = c.bedUnitRate
            return s
        }
    }

    public func getParams() async throws -> EngineParameters {
        try await runOnQueue {
            var c = RoomcutClientParams()
            let rc = roomcutClientGetParams(&c)
            guard rc == 0 else { throw EngineClientError.transport(rc) }
            return EngineParameters(native: c)
        }
    }

    public func getComparison() async throws -> EngineComparisonState {
        try await runOnQueue {
            var value = RoomcutClientComparison()
            let result = roomcutClientGetComparison(&value)
            guard result == 0 else { throw EngineClientError.transport(result) }
            return EngineComparisonState(native: value)
        }
    }

    // The comparison payload version the running engine speaks. Derived from its
    // capability bits on every state read, because the size of the message we
    // send must follow what the ENGINE can read — never what the values are.
    // Deciding it from the values is what once made "Room Off" and "Surround
    // Off" do nothing: a zero looked like "nothing to send".
    private static let comparisonVersionLock = NSLock()
    private nonisolated(unsafe) static var comparisonVersion: UInt32 = 2

    static func rememberComparisonVersion(for capabilities: UInt32) {
        let version: UInt32 = (capabilities & UInt32(ROOMCUT_CLIENT_CAP_BED_RENDERER)) != 0 ? 5
            : (capabilities & UInt32(ROOMCUT_CLIENT_CAP_UPMIX)) != 0 ? 4
            : ((capabilities & UInt32(ROOMCUT_CLIENT_CAP_VIRTUAL_ROOM)) != 0 ? 3 : 2)
        comparisonVersionLock.lock()
        comparisonVersion = version
        comparisonVersionLock.unlock()
    }

    static var currentComparisonVersion: UInt32 {
        comparisonVersionLock.lock()
        defer { comparisonVersionLock.unlock() }
        return comparisonVersion
    }

    public func setComparison(_ target: EngineComparisonTarget, reference: EngineParameters, enabled: Bool) async throws {
        try await runOnQueue {
            var reference = reference.nativeValues()
            let version = Self.currentComparisonVersion
            let result: Int32
            switch target {
            case .parameters(let parameters):
                var current = parameters.nativeValues()
                result = roomcutClientSetComparison(&current, &reference, enabled ? 1 : 0, nil, version)
            case .preset(let id):
                result = id.withCString {
                    roomcutClientSetComparison(nil, &reference, enabled ? 1 : 0, $0, version)
                }
            }
            guard result == 0 else { throw EngineClientError.transport(result) }
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
            let cdynamics = params.parametric.map { b -> RoomcutClientParamDynamics in
                var cd = RoomcutClientParamDynamics()
                cd.enabled = b.dynamic ? 1 : 0
                cd.thresholdDb = b.thresholdDb
                cd.rangeDb = b.rangeDb
                cd.attackMs = b.attackMs
                cd.releaseMs = b.releaseMs
                return cd
            }
            let rc = params.eqGainsDb.withUnsafeBufferPointer { buf in
                cbands.withUnsafeBufferPointer { pbuf in
                    cdynamics.withUnsafeBufferPointer { dbuf in
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
                        params.roomType,
                        params.roomAmount,
                        params.surroundType,
                        params.centerWidth,
                        params.surroundDepth,
                        params.bedRenderer,
                        pbuf.baseAddress,
                        dbuf.baseAddress
                    )
                    }
                }
            }
            guard rc == 0 else { throw EngineClientError.transport(rc) }
        }
    }

    // Sent from the head-tracking callback, so it hops to the client queue and
    // returns immediately rather than blocking the sensor stream on Mach IPC.
    nonisolated public func sendHeadPose(yawDegrees: Double, active: Bool) {
        queue.async {
            _ = roomcutClientSetHeadPose(yawDegrees, active ? 1 : 0)
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
        try await writeDeviceData {
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
        _ = changeDefaultOutput(true)
    }

    public func restoreRealDefaultOutput() {
        _ = changeDefaultOutput(false)
    }

    public func roomcutIsDefaultOutput() -> Bool {
        roomcutClientRoomcutIsDefault() == 1
    }

    nonisolated public func writeVolume(_ scalar: Double) async throws {
        try await writeDeviceData {
            let result = roomcutClientVolumeSet(scalar)
            guard result == 0 else { throw EngineClientError.transport(result) }
        }
    }
    nonisolated public func writeBalance(_ pan: Double) async throws {
        try await writeDeviceData {
            let result = roomcutClientBalanceSet(pan)
            guard result == 0 else { throw EngineClientError.transport(result) }
        }
    }
    nonisolated public func setDefaultOutput(roomcut: Bool) async throws {
        try await writeDeviceData {
            let result = self.changeDefaultOutput(roomcut)
            guard result == 0 || result == 1 else { throw EngineClientError.transport(result) }
        }
    }
    func writeDeviceData(_ write: @escaping () throws -> Void) async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            deviceWriteQueue.async {
                do { try write(); continuation.resume() }
                catch { continuation.resume(throwing: error) }
            }
        }
    }

    nonisolated public func deviceReadback(for uid: String, includeDevices: Bool, includeControls: Bool) async -> DeviceReadback {
        await readDeviceData {
            DeviceReadback.capture(using: self, uid: uid, devices: includeDevices, controls: includeControls)
        }
    }

    // A separate queue keeps slow device enumeration from holding up Mach
    // parameter writes. Kept internal so queue isolation can be exercised without hardware.
    func readDeviceData(_ read: @escaping () -> DeviceReadback) async -> DeviceReadback {
        await withCheckedContinuation { continuation in
            deviceQueue.async { continuation.resume(returning: read()) }
        }
    }

    func runOnQueue<T>(_ body: @escaping () throws -> T) async throws -> T {
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
