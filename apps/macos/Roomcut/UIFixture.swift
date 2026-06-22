//
// UIFixture.swift — deterministic UI states for GUI verification.
//
// `Roomcut --ui-fixture <state>` launches the real menu/window surfaces backed
// by an in-memory client instead of the live engine, so each visual state
// (offline, limiting, recover, …) can be screenshotted without driving real
// audio. The fixture never sends commands to the production daemon: writes are
// accepted locally and reflected back, reads always return the seeded state.
//
import Foundation
import RoomcutCore
import RoomcutPresentationCore

public enum UIFixtureKind: String, CaseIterable, Sendable {
    case running
    case bypass
    case recover
    case offline
    case limiting
    case underrun
    case custom
    // Main-shell surfaces (additive; the engine seed stays "running"-like).
    case uiHome = "ui-home"
    case uiBasic = "ui-basic"
    case uiAdvanced = "ui-advanced"
    case uiSidebar = "ui-sidebar"
    case uiInspector = "ui-inspector"
    case uiMetadata = "ui-metadata"
    case uiAnalyzer = "ui-analyzer"

    // Sample Now Playing metadata, shown ONLY in the main-shell QA fixtures
    // (ui-*). The engine-state fixtures and production both use the signal
    // fallback so nothing fake leaks into the real app.
    var nowPlayingFixture: NowPlayingDisplayState? {
        switch self {
        case .uiHome, .uiBasic, .uiAdvanced, .uiSidebar, .uiInspector, .uiMetadata, .uiAnalyzer:
            return .fixture(title: "Midnight Drive",
                            artist: "M83",
                            source: "Apple Music",
                            progress: 0.38)
        default:
            return nil
        }
    }

    // Which sound-controls surface the shell should open to for this fixture.
    var initialSheet: SoundSheetState {
        switch self {
        case .uiBasic:    return .basic
        case .uiAdvanced, .uiAnalyzer: return .advanced
        default:          return RoomcutMainPresentation.launchSheet
        }
    }

    var initialSidebarExpanded: Bool { self == .uiSidebar }
    var initialInspectorExpanded: Bool { self == .uiInspector }
}

// Optional appearance override from `--ui-appearance light|dark`. Drives the
// preferred colour scheme so Light/Dark QA doesn't depend on System Settings.
public enum UIAppearance: String, Sendable {
    case light
    case dark
}

// Set by main.swift from `--ui-fixture <kind>` / `--ui-appearance <mode>` before
// the SwiftUI app starts. Plain Sendable statics so they can be written from
// top-level main code; the @MainActor App reads them when constructing its view.
enum AppLaunch {
    static var fixtureKind: UIFixtureKind?
    static var appearance: UIAppearance?
}

public final class FixtureEngineClient: EngineClientProtocol, @unchecked Sendable {
    private let kind: UIFixtureKind
    private var liveBypass: Bool

    public let presets: [EnginePreset] = [
        EnginePreset(id: "flat", name: "Flat"),
        EnginePreset(id: "clean", name: "Clean"),
        EnginePreset(id: "dialogue", name: "Dialogue"),
        EnginePreset(id: "night", name: "Night"),
        EnginePreset(id: "soft", name: "Soft"),
        EnginePreset(id: "laptop-speaker", name: "Laptop Speaker"),
        EnginePreset(id: "airpods", name: "AirPods"),
    ]

    public init(kind: UIFixtureKind) {
        self.kind = kind
        self.liveBypass = (kind == .bypass)
    }

    public func getState() async throws -> EngineStatus {
        if kind == .offline { throw EngineClientError.transport(-1) }

        var s = EngineStatus()
        s.reachable = true
        s.paramsRevision = 1
        s.frames = 4_000_000
        s.outputDeviceUID = "iFiUSB"
        switch kind {
        case .running:
            s.state = EngineStatus.running
            s.presetId = "soft"
            s.peak = 0.42
        case .bypass:
            s.state = liveBypass ? EngineStatus.bypass : EngineStatus.running
            s.presetId = "soft"
            s.manualBypass = liveBypass
            s.peak = 0.38
        case .recover:
            s.state = EngineStatus.recover
            s.presetId = "soft"
            s.peak = 0
        case .limiting:
            s.state = EngineStatus.running
            s.presetId = "night"
            s.peak = 0.97
            s.limiterGRDb = 3.4
        case .underrun:
            s.state = EngineStatus.running
            s.presetId = "clean"
            s.peak = 0.21
            s.underruns = 128
        case .custom:
            s.state = EngineStatus.running
            s.presetId = "custom"
            s.peak = 0.55
        case .uiHome, .uiSidebar, .uiInspector, .uiMetadata:
            s.state = EngineStatus.running
            s.presetId = "soft"
            s.peak = 0.42
        case .uiBasic:
            s.state = EngineStatus.running
            s.presetId = "clean"
            s.peak = 0.36
        case .uiAdvanced, .uiAnalyzer:
            s.state = EngineStatus.running
            s.presetId = "night"
            s.peak = 0.58
        case .offline:
            break // handled above
        }
        s.capabilities = EngineStatus.spatialParamsCapability
            | EngineStatus.parametricCapability
            | EngineStatus.analyzerCapability
        return s
    }

    public func getParams() async throws -> EngineParameters {
        if kind == .custom || kind == .uiAdvanced || kind == .uiAnalyzer {
            return EngineParameters(
                preampDb: -3,
                eqGainsDb: [4, 3, 1, 0, -2, -1, 2, 5, 6, 3],
                outputGainDb: -1
            )
        }
        return .flat
    }

    public func getAnalysis() async throws -> RoomcutAnalysisSnapshot {
        RoomcutAnalysisSnapshot(
            valid: true,
            sampleRate: 48000,
            channels: 2,
            framesAnalyzed: 256_000,
            peakDb: -2.4,
            rmsDb: -17.8,
            crestFactor: 7.1,
            lowEnergy: 0.14,
            lowMidEnergy: 0.38,
            midEnergy: 0.31,
            highEnergy: 0.17,
            spectralCentroid: 2850,
            stereoWidth: 0.68,
            midSideRatio: 0.32,
            muddiness: 0.42,
            harshness: 0.18,
            sibilance: 0.22,
            voicePresence: 0.48,
            reverbEstimate: 0.62,
            dynamicRange: 15.4,
            spectrum: [0.18, 0.22, 0.30, 0.41, 0.54, 0.63,
                       0.71, 0.67, 0.58, 0.52, 0.48, 0.55,
                       0.69, 0.82, 0.78, 0.64, 0.50, 0.43,
                       0.38, 0.31, 0.24, 0.18, 0.12, 0.08])
    }

    public func setPreset(_ presetId: String) async throws {}
    public func setBypass(_ on: Bool) async throws { liveBypass = on }
    public func setKeepDefault(_ on: Bool) async throws {}
    public func setParams(_ params: EngineParameters) async throws {}

    private var liveVolume = 0.6
    public func outputDevices() -> [OutputDeviceChoice] {
        [OutputDeviceChoice(uid: "BuiltInSpeakerDevice", name: "Mac mini 스피커"),
         OutputDeviceChoice(uid: "iFiUSB", name: "iFi USB Audio SE")]
    }
    public func setOutputDevice(_ uid: String) async throws {}
    public func audioFormat(for uid: String) -> AudioFormatInfo? {
        AudioFormatInfo(bitDepth: 32, sampleRate: 48_000, latencyMs: 12)
    }
    public func volumeGet() -> Double? { kind == .offline ? nil : liveVolume }
    public func volumeSet(_ scalar: Double) { liveVolume = scalar }
}
