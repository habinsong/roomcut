import Foundation
import CRoomcutClient
import RoomcutPresentationCore

public typealias EngineModel = RoomcutViewModel

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
    // folder. v3 adds dynamics (highpassHz/compAmount). Defaults keep older saves
    // valid.
    public var eqMacros: [String: Double]
    public var parametric: [ParametricBand]
    public var limiterReleaseMs: Double
    public var highpassHz: Double
    public var compAmount: Double
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
        self.folder = folder
        self.builtin = builtin
        self.roomTuneInfo = roomTuneInfo
    }

    enum CodingKeys: String, CodingKey {
        case name, preampDb, eqGainsDb, outputGainDb
        case spatialWidth, centerFocus, crossfeed, roomReduce, spatialMode
        case eqMacros, parametric, limiterReleaseMs, highpassHz, compAmount
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
        folder = try c.decodeIfPresent(String.self, forKey: .folder)
        builtin = try c.decodeIfPresent(Bool.self, forKey: .builtin) ?? false
        roomTuneInfo = try c.decodeIfPresent(String.self, forKey: .roomTuneInfo)
    }
}

public enum RoomcutNowPlayingTheme: String, CaseIterable, Identifiable, Codable {
    case cover
    case meshGradient
    case halo

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .cover: return "Cover"
        case .meshGradient: return "Mesh Gradient"
        case .halo: return "Halo"
        }
    }
}

// The Now Playing layout variant, a second axis alongside the theme: A is the
// current single-card layout, B shows it split into two.
public enum RoomcutNowPlayingLayout: String, CaseIterable, Identifiable, Codable {
    case b
    case a

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .b: return "B"
        case .a: return "A"
        }
    }
}

// App appearance override (Settings → Appearance). `system` follows macOS.
public enum RoomcutAppearance: String, CaseIterable, Identifiable, Codable {
    case system
    case light
    case dark

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .system: return "Auto"
        case .light: return "Light"
        case .dark: return "Dark"
        }
    }
}

// UI language override (Settings → Appearance). `auto` follows the macOS system
// language; the others force a specific language regardless of the system.
public enum AppLanguage: String, CaseIterable, Identifiable, Codable {
    case auto
    case korean
    case english
    case japanese
    case french
    case german

    public var id: String { rawValue }

    // The user's current pick, mirrored here from the view model so the L() helper
    // can resolve strings without a SwiftUI dependency. UI runs on the main thread,
    // so a plain static is safe under the v5 language mode.
    public static var preference: AppLanguage = .auto

    // The concrete language strings are rendered in: the user's pick, or the macOS
    // system language when set to auto.
    public static var effective: AppLanguage {
        preference == .auto ? system : preference
    }

    // macOS UI language, mapped onto a supported language (English is the fallback).
    public static var system: AppLanguage {
        let code = (Locale.preferredLanguages.first ?? "en").lowercased()
        if code.hasPrefix("ko") { return .korean }
        if code.hasPrefix("ja") { return .japanese }
        if code.hasPrefix("fr") { return .french }
        if code.hasPrefix("de") { return .german }
        return .english
    }

    // Native display name for the Settings language menu.
    public var displayName: String {
        switch self {
        case .auto:     return L("자동", "Automatic", "自動", "Automatique", "Automatisch")
        case .korean:   return "한국어"
        case .english:  return "English"
        case .japanese: return "日本語"
        case .french:   return "Français"
        case .german:   return "Deutsch"
        }
    }
}

// Inline 5-language string. Korean is the project's base language; English shows
// when macOS runs in English; ja/fr/de are full translations. Co-locating the
// translations at the call site keeps them next to the UI they belong to.
public func L(_ ko: String, _ en: String, _ ja: String, _ fr: String, _ de: String) -> String {
    switch AppLanguage.effective {
    case .korean:   return ko
    case .english:  return en
    case .japanese: return ja
    case .french:   return fr
    case .german:   return de
    case .auto:     return en   // unreachable: effective never returns .auto
    }
}

// App-shipped preset library — curated, folder-grouped, FULL-state presets (each a
// SavedPreset so it carries the same fields a user save does). Applied via setParams
// like any saved preset; `builtin` blocks deletion/overwrite. Curves are 10-band
// voicings informed by each device/category's known tendencies (consumer gear is
// bass-lifted & treble-lively, studio monitors near-flat with a gentle HF tilt).
public enum PresetLibrary {
    public static let folderOrder = ["Signature", "Apple", "Speakers", "Headphones", "My Presets"]
    public static let all: [SavedPreset] = signature + apple + speakers + headphones

    // bands: 31 62 125 250 500 1k 2k 4k 8k 16k
    private static func p(_ name: String, _ folder: String, _ g: [Double],
                          preamp: Double = 0, width: Double = 0,
                          crossfeed: Double = 0, roomReduce: Double = 0) -> SavedPreset {
        SavedPreset(name: name, preampDb: preamp, eqGainsDb: g, outputGainDb: 0,
                    spatialWidth: width, centerFocus: 0, crossfeed: crossfeed, roomReduce: roomReduce,
                    folder: folder, builtin: true)
    }

    static let signature: [SavedPreset] = [
        p("Flat",        "Signature", [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
        p("Warm",        "Signature", [2, 2.5, 1.5, 0.5, 0, -0.5, -1, -1.5, -1, -0.5]),
        p("Bright",      "Signature", [-1, -0.5, 0, 0, 0, 0.5, 1, 2, 2.5, 2]),
        p("Bass Boost",  "Signature", [5, 5.5, 3, 1, 0, 0, 0, 0, 0, 0], preamp: -3),
        p("Vocal Boost", "Signature", [0, 0, -0.5, 0, 1, 2.5, 2.5, 1.5, 0, 0]),
        p("Loudness",    "Signature", [4, 4, 2, 0, -0.5, -1, -0.5, 0, 2, 3], preamp: -2.5),
        p("Speech",      "Signature", [-4, -3, -1, 1, 2, 3, 2.5, 1.5, 0, -1]),
        p("Soft",        "Signature", [-1, -1, -0.5, 0, 0, -0.5, -1, -1.5, -1.5, -1]),
    ]

    static let apple: [SavedPreset] = [
        p("MacBook Pro Speakers", "Apple", [4, 4, 2, 0.5, 0, -0.5, -1, -1, 0.5, 1], preamp: -2),
        p("MacBook Air Speakers", "Apple", [5, 5, 3, 1, 0, -0.5, -1, -1, 1, 1.5], preamp: -2.5),
        p("AirPods Pro",          "Apple", [1.5, 1, 0.5, 0, 0, 0, -0.5, -1, -1.5, -0.5]),
        p("AirPods Max",          "Apple", [1, 0.5, 0, 0, 0, 0, 0.5, 1, 1, 0.5]),
        p("AirPods (3rd gen)",    "Apple", [5, 4, 2, 0.5, 0, 0, 0, 0, 1, 1], preamp: -2.5),
        p("Beats",                "Apple", [-2, -1, 0, 0, 0.5, 1, 1, 0.5, 0, 0]),
    ]

    static let speakers: [SavedPreset] = [
        p("Studio Monitors",   "Speakers", [0, 0, 0, 0, 0, 0, -0.5, -1, -1.5, -2]),
        p("Bookshelf",         "Speakers", [1.5, 2, 1, 0, 0, 0, 0.5, 1, 1, 0.5]),
        p("TV Speakers",       "Speakers", [2, 2, 1, 1, 1.5, 2.5, 2, 0.5, -0.5, -1], preamp: -1),
        p("Soundbar",          "Speakers", [3, 3, 1.5, 0, 0, 1, 1.5, 1, 1.5, 2], preamp: -1.5),
        p("Bluetooth Speaker", "Speakers", [3, 2, -1, -1, 0, 1, 1.5, 1, 1.5, 1], preamp: -1.5),
        p("Car",               "Speakers", [4, 3, 1, 0, -1, 0, 1, 2, 2, 1], preamp: -2),
        p("Cinema",            "Speakers", [3, 3, 1.5, 0, 0, 1.5, 1.5, 0.5, 1, 1.5],
          preamp: -1.5, width: 40, crossfeed: 0, roomReduce: 20),
    ]

    static let headphones: [SavedPreset] = [
        p("Harman Target", "Headphones", [4, 3.5, 1.5, 0, 0, 0, 1, 0.5, -1, -2], preamp: -2),
        p("Open-Back",     "Headphones", [2, 2, 1, 0, 0, 0, 0.5, 1, 1.5, 1]),
        p("Closed-Back",   "Headphones", [3, 3, 1, -0.5, 0, 0.5, 1, 0.5, -0.5, -1], preamp: -1),
        p("In-Ear (IEM)",  "Headphones", [4, 3, 1, 0, 0, 0.5, 1.5, 1, 0, -1], preamp: -1.5),
    ]
}

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

@MainActor
public final class RoomcutViewModel: ObservableObject {
    @Published public var status = EngineStatus()
    @Published public var preampDb = 0.0
    @Published public var eqGainsDb = [Double](repeating: 0, count: EngineParameters.bandCount)
    @Published public var outputGainDb = 0.0
    // Limiter (true-peak ceiling + release). Tracked from the engine so presets can
    // capture/restore it; the engine re-clamps to its own safe range.
    @Published public var limiterReleaseMs = 100.0
    @Published public var spatialWidth = 0.0
    @Published public var centerFocus = 0.0
    @Published public var crossfeed = 0.0
    @Published public var roomReduce = 0.0
    // 0 = Speaker (crosstalk cancellation), 1 = Headphone (crossfeed). The "crossfeed"
    // control means opposite things on the two systems, so it follows this mode.
    @Published public var spatialMode = 0.0
    // Dynamics: the light compressor's single knob (0..100, 0 = off) surfaced as
    // "볼륨 평준화" — evens out loud/quiet passages for night listening. The HPF
    // has no UI (Dialogue-preset internal) but must round-trip so a hand-tweak
    // after Dialogue doesn't silently drop it.
    @Published public var compAmount = 0.0
    @Published public var highpassHz = 0.0
    @Published public var parametric: [ParametricBand] =
        Array(repeating: ParametricBand(), count: EngineParameters.paramBandCount)
    // Basic-tab macro positions in [-1, 1]. Held on the model (not a transient
    // view @State) so they survive the Home view being recreated on tab switches
    // — the bug where a raised knob snapped back to 0 while the EQ stayed changed.
    @Published public var macroValues: [EqMacro: Double] = [:]
    // User-saved custom presets, persisted in UserDefaults.
    @Published public var savedPresets: [SavedPreset] = []
    // Name of the saved preset currently applied (nil = a builtin or an unsaved
    // custom curve). The engine reports every custom curve as "custom", so this
    // is how the UI shows the actual saved name instead of "Custom".
    @Published public private(set) var activeSavedPreset: String?
    @Published public private(set) var activeBuiltinPresetId: String?
    @Published public var errorBanner: String?
    // True only when underruns are actively climbing during playback — a real
    // dropout the user might hear. The engine's `underruns` is a lifetime
    // counter that also ticks up during idle silence (empty ring), so the raw
    // value being > 0 is not a useful signal on a long-running daemon.
    // Meters live in their own observable (RoomcutMeters) so their ~12 Hz updates
    // invalidate only the meter views, not the whole window.
    public let meters = RoomcutMeters()
    // Real output device selection + volume (the Roomcut device volume, which
    // the engine mirrors to the real device's hardware volume — full range).
    @Published public var outputDevices: [OutputDeviceChoice] = []
    @Published public var audioFormat: AudioFormatInfo?
    @Published public var deviceFormatOptions: [DeviceFormatOption] = []
    @Published public var volume = 1.0
    @Published public var hasVolumeControl = true
    // Output L/R balance: -1 (left) … 0 (centre) … +1 (right). Backed by the
    // device's per-channel volume (Audio MIDI Setup "Front Left/Right"), so it
    // stays in sync with the macOS sliders. hasBalanceControl is false when the
    // device exposes no independent per-channel volume.
    @Published public var balance = 0.0
    @Published public var hasBalanceControl = true
    @Published public private(set) var nowPlayingTheme: RoomcutNowPlayingTheme = .cover
    // Now Playing layout variant (A current / B split), picked in Settings → Appearance.
    @Published public private(set) var nowPlayingLayout: RoomcutNowPlayingLayout = .b
    // App light/dark/auto override, picked in Settings → Appearance.
    @Published public private(set) var appearance: RoomcutAppearance = .system
    // When on, the compact/Sound-Controls background uses the Now Playing theme wash
    // instead of the plain light/dark gradient (Settings → Appearance toggle).
    @Published public private(set) var themeSyncEnabled: Bool = false
    // UI language override (Settings → Appearance). `auto` follows the system.
    @Published public private(set) var language: AppLanguage = .auto
    @Published public private(set) var analysis: RoomcutAnalysisSnapshot?
    // Per-device presets (Settings → behavior): when on, applying a builtin or
    // saved preset remembers it for the CURRENT output device, and switching the
    // output device re-applies that device's remembered preset. The map persists
    // in UserDefaults keyed by device UID; values are picker tokens ("night",
    // "saved:<name>").
    @Published public private(set) var deviceAutoPresetEnabled = false
    private var devicePresetMap: [String: String] = [:]

    // Sample Now Playing metadata, set only by a `--ui-fixture` launch. nil in
    // production → the UI uses the engine-signal fallback (System Audio).
    public var nowPlayingFixture: NowPlayingDisplayState?

    // Production-vs-fixture Now Playing display. Fixtures win when present;
    // otherwise the engine signal decides Signal active / No metadata.
    public var nowPlayingDisplay: NowPlayingDisplayState {
        if let fixture = nowPlayingFixture { return fixture }
        return RoomcutMainPresentation.fallbackDisplay(
            reachable: status.reachable, state: status.state, peak: meters.displayPeak)
    }

    public let presets: [EnginePreset]

    private let client: EngineClientProtocol
    private let debounceNanoseconds: UInt64
    private var pollTimer: Timer?
    private var pendingPushTask: Task<Void, Never>?
    private var isEditingParams = false
    private var isEditingVolume = false
    private var isEditingBalance = false
    private var lastSeenPresetId: String?
    private var lastSeenRevision: UInt32?
    private var lastUnderruns: UInt64?
    private var lastDeviceListRefresh = Date.distantPast
    private var lastControlStateRefresh = Date.distantPast
    private var lastAnalysisRefresh = Date.distantPast
    private var analyzerVisible = false
    private var didClaimDefaultOutput = false

    private static let pollInterval: TimeInterval = 1.0 / 12.0
    private static let deviceListRefreshInterval: TimeInterval = 5
    private static let controlStateRefreshInterval: TimeInterval = 0.5
    private static let analysisRefreshInterval: TimeInterval = 0.50
    private static let volumeEpsilon = 0.001
    public static let maxVolume = 2.0

    public convenience init() {
        self.init(client: LiveEngineClient())
    }

    public init(client: EngineClientProtocol, debounceNanoseconds: UInt64 = 150_000_000) {
        self.client = client
        self.debounceNanoseconds = debounceNanoseconds
        self.presets = client.presets
        loadSavedPresets()
        // Restore the active saved-preset name (best-effort) if it still exists.
        if let active = UserDefaults.standard.string(forKey: Self.activePresetKey),
           savedPresets.contains(where: { $0.name == active }) {
            activeSavedPreset = active
        }
        if let builtin = UserDefaults.standard.string(forKey: Self.activeBuiltinPresetKey),
           client.presets.contains(where: { $0.id == builtin }) {
            activeBuiltinPresetId = builtin
        }
        if let rawTheme = UserDefaults.standard.string(forKey: Self.nowPlayingThemeKey),
           let theme = RoomcutNowPlayingTheme(rawValue: rawTheme) {
            nowPlayingTheme = theme
        }
        if let rawLayout = UserDefaults.standard.string(forKey: Self.nowPlayingLayoutKey),
           let layout = RoomcutNowPlayingLayout(rawValue: rawLayout) {
            nowPlayingLayout = layout
        }
        if let rawApp = UserDefaults.standard.string(forKey: Self.appearanceKey),
           let a = RoomcutAppearance(rawValue: rawApp) {
            appearance = a
        }
        themeSyncEnabled = UserDefaults.standard.bool(forKey: Self.themeSyncKey)
        if let rawLang = UserDefaults.standard.string(forKey: Self.languageKey),
           let l = AppLanguage(rawValue: rawLang) {
            language = l
        }
        AppLanguage.preference = language
        deviceAutoPresetEnabled = UserDefaults.standard.bool(forKey: Self.deviceAutoPresetKey)
        if let map = UserDefaults.standard.dictionary(forKey: Self.devicePresetMapKey) as? [String: String] {
            devicePresetMap = map
        }
    }

    deinit {
        pendingPushTask?.cancel()
        pollTimer?.invalidate()
    }

    public func startPolling() {
        guard pollTimer == nil else { return }
        Task { await refreshNow() }
        pollTimer = Timer.scheduledTimer(withTimeInterval: Self.pollInterval, repeats: true) { [weak self] _ in
            Task { @MainActor in
                await self?.refreshNow()
            }
        }
    }

    public func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
        pendingPushTask?.cancel()
        pendingPushTask = nil
    }

    public func refresh() {
        Task { await refreshNow() }
    }

    public func refreshNow() async {
        let nextStatus: EngineStatus
        do {
            nextStatus = try await client.getState()
        } catch {
            var offline = status
            offline.reachable = false
            if Self.shouldPublishStatus(previous: status, next: offline) {
                status = offline
            }
            resetMeterDisplay()
            if audioFormat != nil { audioFormat = nil }
            if !deviceFormatOptions.isEmpty { deviceFormatOptions = [] }
            if analysis != nil { analysis = nil }
            setErrorBanner("연결 끊김")
            return
        }

        let refreshDate = Date()
        let wasOffline = !status.reachable
        let previousDeviceUID = status.outputDeviceUID
        let outputDeviceChanged = status.outputDeviceUID != nextStatus.outputDeviceUID
        let needsParams = wasOffline
            || lastSeenPresetId != nextStatus.presetId
            || lastSeenRevision != nextStatus.paramsRevision
        let shouldRefreshDeviceList = wasOffline || outputDeviceChanged
            || refreshDate.timeIntervalSince(lastDeviceListRefresh) >= Self.deviceListRefreshInterval
        let shouldRefreshControlState = wasOffline || outputDeviceChanged
            || refreshDate.timeIntervalSince(lastControlStateRefresh) >= Self.controlStateRefreshInterval

        // A dropout worth surfacing = the lifetime counter climbed since the
        // last poll AND audio is actually flowing (peak above the engine's
        // silence floor). Idle silence also ticks underruns, so the peak gate
        // is what keeps the warning from being permanently on.
        let underrunActiveNow = Self.underrunsActive(
            previous: lastUnderruns, current: nextStatus.underruns, peak: nextStatus.peak)
        lastUnderruns = nextStatus.underruns
        updateMeterDisplay(with: nextStatus, underrunActiveNow: underrunActiveNow)

        // Only republish `status` on a MEANINGFUL change. peak/framesRendered/
        // underruns tick every poll; publishing those would fire objectWillChange
        // at the poll rate and re-render the whole window (incl. the expensive
        // full-window background) — a major idle-CPU sink.
        if Self.shouldPublishStatus(previous: status, next: nextStatus) {
            status = nextStatus
        }
        lastSeenPresetId = nextStatus.presetId
        lastSeenRevision = nextStatus.paramsRevision

        // Per-device presets: react only to a REAL device switch while connected
        // (both UIDs non-empty, engine previously reachable). First connect is
        // left alone — the engine already resumed the user's last state, and
        // stomping it with a mapped preset would surprise.
        if deviceAutoPresetEnabled && outputDeviceChanged && !wasOffline
            && !previousDeviceUID.isEmpty && !nextStatus.outputDeviceUID.isEmpty
            && !isEditingParams,
           let token = devicePresetMap[nextStatus.outputDeviceUID],
           token != presetPickerSelection {
            applyPickerSelection(token)
        }

        // On the first healthy poll, make Roomcut the macOS default output so app
        // audio actually flows through the engine — otherwise the meters sit at
        // silence (−60 dBFS) and the EQ is inaudible whenever macOS has a real
        // device selected as default. One-shot; the user can still switch away.
        if !didClaimDefaultOutput && nextStatus.state == EngineStatus.running {
            didClaimDefaultOutput = true
            client.makeRoomcutDefaultOutput()
        }

        if shouldRefreshDeviceList {
            refreshOutputDevices()
            lastDeviceListRefresh = refreshDate
        }
        if shouldRefreshControlState {
            refreshControlState(with: nextStatus)
            lastControlStateRefresh = refreshDate
        }
        if analyzerVisible
            && nextStatus.supportsAnalyzer
            && refreshDate.timeIntervalSince(lastAnalysisRefresh) >= Self.analysisRefreshInterval {
            await refreshAnalysis()
            lastAnalysisRefresh = refreshDate
        } else if (!nextStatus.supportsAnalyzer || !analyzerVisible) && analysis != nil {
            analysis = nil
        }

        guard needsParams && !isEditingParams else {
            clearErrorBanner()
            return
        }

        do {
            try await loadParams()
            clearErrorBanner()
        } catch {
            setErrorBanner("엔진 값을 불러오지 못했습니다")
        }
    }

    public func apply(presetId: String) {
        Task { await applyPreset(presetId) }
    }

    public func applyPreset(_ presetId: String) async {
        do {
            try await client.setPreset(presetId)
            await refreshNow()
            try await loadParams()
            macroValues = [:]   // the preset is the new base; macro deltas reset
            setActivePreset(savedName: nil, builtinId: presetId)
            errorBanner = nil
        } catch {
            errorBanner = "프리셋을 적용하지 못했습니다"
        }
    }

    // MARK: Basic-tab macros (additive layer over the 10-band EQ)

    // Move one macro to `normalized` ∈ [-1, 1]. Only that macro's target bands
    // shift, by the DELTA from its previous position — so re-editing never
    // double-counts and the engine EQ always matches the knob.
    public func setMacro(_ macro: EqMacro, normalized: Double) {
        let clamped = max(-1.0, min(1.0, normalized))
        let previous = macroValues[macro] ?? 0
        macroValues[macro] = clamped
        eqGainsDb = RoomcutMacros.applyDelta(macro, delta: clamped - previous, to: eqGainsDb)
        schedulePushParams()
    }

    // MARK: Saved presets (name → params, UserDefaults-backed)

    public func presetNameExists(_ name: String) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return savedPresets.contains { $0.name.caseInsensitiveCompare(trimmed) == .orderedSame }
    }

    public static let customPresetName = "Custom"

    // Snapshot the CURRENT full state into a preset (10-band + Basic macros +
    // Parametric + Spatial + Limiter).
    public func makeCurrentPreset(name: String, folder: String? = nil, roomTuneInfo: String? = nil) -> SavedPreset {
        SavedPreset(
            name: name,
            preampDb: preampDb,
            eqGainsDb: eqGainsDb,
            outputGainDb: outputGainDb,
            spatialWidth: spatialAvailable ? spatialWidth : 0,
            centerFocus: spatialAvailable ? centerFocus : 0,
            crossfeed: spatialAvailable ? crossfeed : 0,
            roomReduce: spatialAvailable ? roomReduce : 0,
            spatialMode: spatialAvailable ? spatialMode : 0,
            eqMacros: Dictionary(uniqueKeysWithValues: macroValues.map { ($0.key.rawValue, $0.value) }),
            parametric: parametricAvailable ? parametric : [],
            limiterReleaseMs: limiterReleaseMs,
            highpassHz: dynamicsAvailable ? highpassHz : 0,
            compAmount: dynamicsAvailable ? compAmount : 0,
            folder: folder,
            roomTuneInfo: roomTuneInfo)
    }

    // Saves the current state as a named preset. An EMPTY name saves into the
    // reusable "Custom" slot; a name that already exists is OVERWRITTEN (the user
    // asked for overwrite saves). Built-in library entries are never overwritten.
    @discardableResult
    public func saveCurrentAsPreset(name: String, roomTuneInfo: String? = nil) -> Bool {
        var trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { trimmed = Self.customPresetName }
        let preset = makeCurrentPreset(name: trimmed, roomTuneInfo: roomTuneInfo)
        if let idx = savedPresets.firstIndex(where: {
            !$0.builtin && $0.name.caseInsensitiveCompare(trimmed) == .orderedSame
        }) {
            savedPresets[idx] = preset
        } else {
            savedPresets.append(preset)
        }
        persistSavedPresets()
        setActivePreset(savedName: trimmed, builtinId: nil)
        return true
    }

    public func applySavedPreset(_ preset: SavedPreset) {
        preampDb = preset.preampDb
        eqGainsDb = preset.eqGainsDb
        outputGainDb = preset.outputGainDb
        limiterReleaseMs = preset.limiterReleaseMs
        spatialWidth = spatialAvailable ? preset.spatialWidth : 0
        centerFocus = spatialAvailable ? preset.centerFocus : 0
        crossfeed = spatialAvailable ? preset.crossfeed : 0
        roomReduce = spatialAvailable ? preset.roomReduce : 0
        spatialMode = spatialAvailable ? preset.spatialMode : 0
        highpassHz = dynamicsAvailable ? preset.highpassHz : 0
        compAmount = dynamicsAvailable ? preset.compAmount : 0
        if parametricAvailable, !preset.parametric.isEmpty {
            var bands = Array(preset.parametric.prefix(EngineParameters.paramBandCount))
            if bands.count < EngineParameters.paramBandCount {
                bands.append(contentsOf: repeatElement(
                    ParametricBand(), count: EngineParameters.paramBandCount - bands.count))
            }
            parametric = bands
        }
        // Restore the Basic-tab knob positions so they SYNC with the saved curve
        // (eqGainsDb already bakes in the macro deltas — this only moves the knobs).
        macroValues = Dictionary(uniqueKeysWithValues: preset.eqMacros.compactMap { key, value in
            EqMacro(rawValue: key).map { ($0, value) }
        })
        schedulePushParams()
        setActivePreset(savedName: preset.name, builtinId: nil)
    }

    public func deleteSavedPreset(_ preset: SavedPreset) {
        savedPresets.removeAll { $0.id == preset.id }
        persistSavedPresets()
        if activeSavedPreset == preset.name { setActivePreset(savedName: nil, builtinId: nil) }
        // Drop per-device mappings that pointed at the deleted preset.
        let token = "saved:\(preset.name)"
        if devicePresetMap.contains(where: { $0.value == token }) {
            devicePresetMap = devicePresetMap.filter { $0.value != token }
            persistDevicePresetMap()
        }
    }

    // The name to show wherever the preset surfaces (menu bar, Settings, the EQ
    // summaries): the active saved preset, else the builtin name, else "Custom".
    public var currentPresetName: String {
        let id = status.presetId
        if id == "custom", let name = activeSavedPreset { return name }
        if id == "custom",
           let builtin = activeBuiltinPresetId,
           let p = presets.first(where: { $0.id == builtin }) { return p.name }
        if let p = presets.first(where: { $0.id == id }) { return p.name }
        return id == "custom" ? "Custom" : (id == "—" ? "—" : id.capitalized)
    }

    // True when the live curve is a hand-modified "custom" — not a saved or library
    // preset — i.e. there's something the user might want to save.
    public var isCustomCurve: Bool {
        status.reachable && status.presetId == "custom" && activeSavedPreset == nil
            && activeBuiltinPresetId == nil
    }

    // Picker token: builtin preset id, "saved:<name>", or "custom".
    public var presetPickerSelection: String {
        if status.presetId == "custom", let name = activeSavedPreset { return "saved:\(name)" }
        if status.presetId == "custom", let builtin = activeBuiltinPresetId { return builtin }
        return status.presetId
    }

    public func applyPickerSelection(_ token: String) {
        if token.hasPrefix("saved:") {
            let name = String(token.dropFirst("saved:".count))
            if let sp = savedPresets.first(where: { $0.name == name }) { applySavedPreset(sp) }
        } else if token != "custom" {
            apply(presetId: token)
        }
    }

    private static let activePresetKey = "com.roomcut.activeSavedPreset"
    private static let activeBuiltinPresetKey = "com.roomcut.activeBuiltinPreset"
    private func setActivePreset(savedName: String?, builtinId: String?) {
        if savedName != activeSavedPreset { activeSavedPreset = savedName }
        if builtinId != activeBuiltinPresetId { activeBuiltinPresetId = builtinId }
        if let savedName { UserDefaults.standard.set(savedName, forKey: Self.activePresetKey) }
        else { UserDefaults.standard.removeObject(forKey: Self.activePresetKey) }
        if let builtinId { UserDefaults.standard.set(builtinId, forKey: Self.activeBuiltinPresetKey) }
        else { UserDefaults.standard.removeObject(forKey: Self.activeBuiltinPresetKey) }
        recordDevicePreset()
    }

    // MARK: Per-device presets

    public func setDeviceAutoPreset(_ on: Bool) {
        guard deviceAutoPresetEnabled != on else { return }
        deviceAutoPresetEnabled = on
        UserDefaults.standard.set(on, forKey: Self.deviceAutoPresetKey)
        // Turning it on adopts the currently active preset for the current device
        // right away, so the very next device round-trip already restores it.
        if on { recordDevicePreset() }
    }

    // Remember an EXPLICIT preset activation (builtin or saved) for the current
    // output device. Custom hand-edits (both names nil) leave the map alone — the
    // device keeps its last known preset.
    private func recordDevicePreset() {
        guard deviceAutoPresetEnabled else { return }
        let uid = status.outputDeviceUID
        guard !uid.isEmpty else { return }
        let token = activeSavedPreset.map { "saved:\($0)" } ?? activeBuiltinPresetId
        guard let token, devicePresetMap[uid] != token else { return }
        devicePresetMap[uid] = token
        persistDevicePresetMap()
    }

    private func persistDevicePresetMap() {
        UserDefaults.standard.set(devicePresetMap, forKey: Self.devicePresetMapKey)
    }

    private static let deviceAutoPresetKey = "com.roomcut.deviceAutoPreset"
    private static let devicePresetMapKey = "com.roomcut.devicePresetMap"

    // MARK: Preset file export / import (backup & sharing)

    // On-disk JSON envelope. Versioned so the format can evolve; presets reuse
    // SavedPreset's Codable (same decode-if-present back-compat as UserDefaults).
    public struct PresetExportFile: Codable {
        public var version: Int
        public var presets: [SavedPreset]
    }

    // All user-saved presets as pretty-printed JSON. nil when there is nothing
    // to export (the UI disables the button).
    public func exportPresetsData() -> Data? {
        guard !savedPresets.isEmpty else { return nil }
        let file = PresetExportFile(version: 1, presets: savedPresets.filter { !$0.builtin })
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try? encoder.encode(file)
    }

    // Merge presets from an exported file: same-name user presets are
    // OVERWRITTEN (the same rule as saving), everything imports as a user preset
    // (builtin is never trusted from disk). Returns the imported count, or nil
    // when the data isn't a Roomcut preset file.
    @discardableResult
    public func importPresets(from data: Data) -> Int? {
        let decoder = JSONDecoder()
        let incoming: [SavedPreset]
        if let file = try? decoder.decode(PresetExportFile.self, from: data) {
            incoming = file.presets
        } else if let bare = try? decoder.decode([SavedPreset].self, from: data) {
            incoming = bare
        } else {
            return nil
        }
        var imported = 0
        for preset in incoming {
            var p = preset
            p.builtin = false
            let trimmed = p.name.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else { continue }
            p.name = trimmed
            if let idx = savedPresets.firstIndex(where: {
                !$0.builtin && $0.name.caseInsensitiveCompare(trimmed) == .orderedSame
            }) {
                savedPresets[idx] = p
            } else {
                savedPresets.append(p)
            }
            imported += 1
        }
        if imported > 0 { persistSavedPresets() }
        return imported
    }

    private static let savedPresetsKey = "com.roomcut.savedPresets"
    private func loadSavedPresets() {
        guard let data = UserDefaults.standard.data(forKey: Self.savedPresetsKey),
              let list = try? JSONDecoder().decode([SavedPreset].self, from: data) else { return }
        savedPresets = list
    }
    private func persistSavedPresets() {
        if let data = try? JSONEncoder().encode(savedPresets) {
            UserDefaults.standard.set(data, forKey: Self.savedPresetsKey)
        }
    }

    public var selectedDeviceUID: String { status.outputDeviceUID }

    public func selectDevice(_ uid: String) {
        Task {
            do {
                try await client.setOutputDevice(uid)
                // Picking a real device only changes where the engine renders TO;
                // re-assert Roomcut as the system default so the input side keeps
                // flowing through the engine (and the EQ keeps applying).
                client.makeRoomcutDefaultOutput()
                await refreshNow()
                errorBanner = nil
            } catch {
                errorBanner = "출력 장치를 변경하지 못했습니다"
            }
        }
    }

    // Available (sampleRate, bitDepth) pairs for the current real device, and the
    // distinct rates / depths the pickers offer.
    public var availableSampleRates: [Double] {
        Array(Set(deviceFormatOptions.map { $0.sampleRate })).sorted()
    }
    public var availableBitDepths: [Int] {
        Array(Set(deviceFormatOptions.map { $0.bitDepth })).sorted()
    }

    // Change only the rate (keep the current depth) or only the depth (keep the
    // current rate). The engine polls the nominal rate and re-opens its output.
    public func selectSampleRate(_ sr: Double) {
        guard let fmt = audioFormat else { return }
        applyDeviceFormat(sampleRate: sr, bitDepth: fmt.bitDepth)
    }
    public func selectBitDepth(_ bits: Int) {
        guard let fmt = audioFormat else { return }
        applyDeviceFormat(sampleRate: fmt.sampleRate, bitDepth: bits)
    }

    private func applyDeviceFormat(sampleRate: Double, bitDepth: Int) {
        let uid = selectedDeviceUID
        guard !uid.isEmpty else { return }
        Task {
            do {
                try await client.setDeviceFormat(uid: uid, sampleRate: sampleRate, bitDepth: bitDepth)
                await refreshNow()
                errorBanner = nil
            } catch {
                errorBanner = "출력 포맷을 변경하지 못했습니다"
                await refreshNow()
            }
        }
    }

    // Volume edits apply instantly (in-process CoreAudio on the Roomcut device,
    // which the engine mirrors to the real device). The edit guard keeps the
    // 250 ms poll from snapping the slider back mid-drag.
    public func beginVolumeEdit() { isEditingVolume = true }
    public func endVolumeEdit() { isEditingVolume = false }

    public func setVolume(_ v: Double) {
        volume = min(Self.maxVolume, max(0.0, v))
        client.volumeSet(volume)
    }

    // Balance edits apply instantly (in-process CoreAudio on the device's
    // per-channel volume); the edit guard keeps the poll from snapping the
    // slider back mid-drag, exactly like volume.
    public func beginBalanceEdit() { isEditingBalance = true }
    public func endBalanceEdit() { isEditingBalance = false }

    public func setBalance(_ pan: Double) {
        balance = min(1.0, max(-1.0, pan))
        client.balanceSet(balance)
    }

    private func refreshOutputDevices() {
        let devices = client.outputDevices()
        if outputDevices != devices { outputDevices = devices }
    }

    private func refreshControlState(with nextStatus: EngineStatus) {
        let nextFormat = nextStatus.outputDeviceUID.isEmpty
            ? nil
            : client.audioFormat(for: nextStatus.outputDeviceUID)
        if audioFormat != nextFormat { audioFormat = nextFormat }

        let nextOptions = nextStatus.outputDeviceUID.isEmpty
            ? []
            : client.deviceFormatOptions(for: nextStatus.outputDeviceUID)
        if deviceFormatOptions != nextOptions { deviceFormatOptions = nextOptions }

        guard !isEditingVolume else { return }
        if let v = client.volumeGet() {
            if !hasVolumeControl { hasVolumeControl = true }
            // volumeGet only reports hardware volume (0…1); the boost (1…2) is a
            // digital gain the device can't read back. Don't let a full-scale
            // hardware reading snap a boosted slider back to 100%.
            let boostedAtFull = volume > 1.0 && v >= 0.999
            if !boostedAtFull && abs(volume - v) > Self.volumeEpsilon { volume = v }
        } else if hasVolumeControl {
            hasVolumeControl = false
        }

        // Balance mirrors the device's per-channel volume — poll it back so an
        // external change (Audio MIDI Setup / System Settings) is reflected.
        guard !isEditingBalance else { return }
        if let p = client.balanceGet() {
            if !hasBalanceControl { hasBalanceControl = true }
            if abs(balance - p) > Self.volumeEpsilon { balance = p }
        } else if hasBalanceControl {
            hasBalanceControl = false
        }
    }

    public func setNowPlayingTheme(_ theme: RoomcutNowPlayingTheme) {
        guard nowPlayingTheme != theme else { return }
        nowPlayingTheme = theme
        UserDefaults.standard.set(theme.rawValue, forKey: Self.nowPlayingThemeKey)
    }

    public func setNowPlayingLayout(_ layout: RoomcutNowPlayingLayout) {
        guard nowPlayingLayout != layout else { return }
        nowPlayingLayout = layout
        UserDefaults.standard.set(layout.rawValue, forKey: Self.nowPlayingLayoutKey)
    }

    public func setAppearance(_ a: RoomcutAppearance) {
        guard appearance != a else { return }
        appearance = a
        UserDefaults.standard.set(a.rawValue, forKey: Self.appearanceKey)
    }

    public func setThemeSync(_ on: Bool) {
        guard themeSyncEnabled != on else { return }
        themeSyncEnabled = on
        UserDefaults.standard.set(on, forKey: Self.themeSyncKey)
    }

    public func setLanguage(_ l: AppLanguage) {
        guard language != l else { return }
        language = l
        AppLanguage.preference = l
        UserDefaults.standard.set(l.rawValue, forKey: Self.languageKey)
    }

    public var keepDefault: Bool { status.keepDefault }
    public var spatialAvailable: Bool { status.reachable && status.supportsSpatialParams }
    public var parametricAvailable: Bool { status.reachable && status.supportsParametric }
    public var analyzerAvailable: Bool { status.reachable && status.supportsAnalyzer }
    public var dynamicsAvailable: Bool { status.reachable && status.supportsDynamics }

    public func setCompAmount(_ value: Double) {
        guard ensureDynamicsAvailable() else { return }
        compAmount = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    // Low Cut (HPF). The engine treats < 20 Hz as off (DSPChain::configureHpf);
    // 400 Hz mirrors PresetBounds::kHighpassMaxHz.
    public func setHighpassHz(_ value: Double) {
        guard ensureDynamicsAvailable() else { return }
        highpassHz = Self.clamp(value, 0, 400)
        schedulePushParams(preservingPresetSelection: true)
    }

    private func ensureDynamicsAvailable() -> Bool {
        guard dynamicsAvailable else {
            errorBanner = status.reachable ? "현재 엔진이 볼륨 평준화를 지원하지 않습니다" : "연결 끊김"
            return false
        }
        return true
    }

    public func setAnalyzerVisible(_ visible: Bool) {
        analyzerVisible = visible
        if !visible, analysis != nil {
            analysis = nil
        }
    }

    public func setKeepDefault(_ on: Bool) {
        Task {
            do {
                try await client.setKeepDefault(on)
                await refreshNow()
                errorBanner = nil
            } catch {
                errorBanner = "출력 고정을 변경하지 못했습니다"
            }
        }
    }

    public func setBypass(_ on: Bool) {
        Task {
            do {
                try await client.setBypass(on)
                await refreshNow()
                errorBanner = nil
            } catch {
                errorBanner = "바이패스를 변경하지 못했습니다"
            }
        }
    }

    private static let nowPlayingThemeKey = "com.roomcut.nowPlayingTheme"
    private static let nowPlayingLayoutKey = "com.roomcut.nowPlayingLayout"
    private static let appearanceKey = "com.roomcut.appearance"
    private static let themeSyncKey = "com.roomcut.themeSync"
    private static let languageKey = "com.roomcut.language"

    public func setSpatialWidth(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        spatialWidth = Self.clamp(value, -100, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setCenterFocus(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        centerFocus = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setCrossfeed(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        crossfeed = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setRoomReduce(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        roomReduce = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setSpatialValues(width: Double, centerFocus: Double, crossfeed: Double, roomReduce: Double) {
        guard ensureSpatialAvailable() else { return }
        spatialWidth = Self.clamp(width, -100, 100)
        self.centerFocus = Self.clamp(centerFocus, 0, 100)
        self.crossfeed = Self.clamp(crossfeed, 0, 100)
        self.roomReduce = Self.clamp(roomReduce, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    // Output device (Speaker/Headphone) × Surround (off/on) are encoded into one
    // value the DSP reads: 0 = speaker, 1 = headphone, 2 = headphone+surround,
    // 3 = speaker+surround. Speaker uses crosstalk cancellation, headphone uses
    // binaural crossfeed; surround layers a symmetric ambience field on either path.
    public func setSpatialMode(_ mode: Double) {
        guard ensureSpatialAvailable() else { return }
        let m = mode.rounded()
        spatialMode = m >= 3 ? 3.0 : (m >= 2 ? 2.0 : (m >= 1 ? 1.0 : 0.0))
        schedulePushParams(preservingPresetSelection: true)
    }

    // The two independent axes over the encoded spatialMode.
    public var spatialOutputIsHeadphone: Bool { spatialMode == 1 || spatialMode == 2 }
    public var spatialSurroundOn: Bool { spatialMode == 2 || spatialMode == 3 }

    public func setSpatialOutput(headphone: Bool) {
        setSpatialMode(Self.encodeSpatialMode(headphone: headphone, surround: spatialSurroundOn))
    }
    public func setSpatialSurround(_ on: Bool) {
        setSpatialMode(Self.encodeSpatialMode(headphone: spatialOutputIsHeadphone, surround: on))
    }
    private static func encodeSpatialMode(headphone: Bool, surround: Bool) -> Double {
        switch (headphone, surround) {
        case (false, false): return 0   // speaker
        case (true, false):  return 1   // headphone
        case (true, true):   return 2   // headphone + surround
        case (false, true):  return 3   // speaker + surround
        }
    }

    // MARK: Parametric EQ (N user-configurable biquad bands)

    // Parametric clamps mirror PresetValidator (core/presets/PresetValidator.hpp);
    // the engine re-clamps, but matching here keeps the UI honest.
    public static let parametricFreqRange = 20.0...20000.0
    public static let parametricGainRange = -24.0...24.0
    public static let parametricQRange = 0.1...12.0

    // Replace one band wholesale (the editor builds the updated band, validates
    // the values, then pushes). No-op if the engine doesn't support parametric.
    public func setParametricBand(_ index: Int, _ band: ParametricBand) {
        guard ensureParametricAvailable() else { return }
        guard parametric.indices.contains(index) else { return }
        var b = band
        b.type = max(0, min(ParametricBand.Kind.allCases.count - 1, b.type))
        b.freqHz = Self.clamp(b.freqHz, Self.parametricFreqRange.lowerBound, Self.parametricFreqRange.upperBound)
        b.gainDb = Self.clamp(b.gainDb, Self.parametricGainRange.lowerBound, Self.parametricGainRange.upperBound)
        b.q = Self.clamp(b.q, Self.parametricQRange.lowerBound, Self.parametricQRange.upperBound)
        parametric[index] = b
        schedulePushParams()
    }

    // Convenience mutators used by the editor controls.
    public func setParametricEnabled(_ index: Int, _ on: Bool) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.enabled = on; setParametricBand(index, b)
    }
    public func setParametricType(_ index: Int, _ type: Int) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.type = type; setParametricBand(index, b)
    }
    public func setParametricFreq(_ index: Int, _ hz: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.freqHz = hz; setParametricBand(index, b)
    }
    public func setParametricGain(_ index: Int, _ db: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.gainDb = db; setParametricBand(index, b)
    }
    public func setParametricQ(_ index: Int, _ q: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.q = q; setParametricBand(index, b)
    }

    private func ensureParametricAvailable() -> Bool {
        guard parametricAvailable else {
            errorBanner = status.reachable ? "현재 엔진이 Parametric EQ를 지원하지 않습니다" : "연결 끊김"
            return false
        }
        return true
    }

    public func schedulePushParams(preservingPresetSelection: Bool = false) {
        // Any direct param edit (knob, band, gain) means we're no longer on a
        // saved preset; applySavedPreset re-sets the name after calling this.
        if !preservingPresetSelection {
            setActivePreset(savedName: nil, builtinId: nil)
        }
        isEditingParams = true
        pendingPushTask?.cancel()
        let snapshot = currentParameters()
        pendingPushTask = Task { [weak self] in
            guard let self else { return }
            do {
                try await Task.sleep(nanoseconds: debounceNanoseconds)
                try await client.setParams(snapshot)
                await MainActor.run {
                    self.isEditingParams = false
                }
                await self.refreshNow()
            } catch is CancellationError {
            } catch {
                await MainActor.run {
                    self.isEditingParams = false
                }
                await self.restoreParamsAfterFailure()
            }
        }
    }

    private func loadParams() async throws {
        let params = try await client.getParams()
        apply(params)
    }

    private func refreshAnalysis() async {
        do {
            let next = try await client.getAnalysis()
            if analysis != next { analysis = next }
        } catch {
            if analysis != nil { analysis = nil }
        }
    }

    private func restoreParamsAfterFailure() async {
        do {
            try await loadParams()
            errorBanner = "엔진 값을 다시 불러왔습니다"
        } catch {
            errorBanner = "엔진 값을 다시 불러오지 못했습니다"
        }
    }

    // Pure rule (testable): warn only when the lifetime underrun counter rose
    // AND audio is flowing. First sample (previous == nil) never warns — it just
    // establishes the baseline.
    static func underrunsActive(previous: UInt64?, current: UInt64, peak: Float) -> Bool {
        guard let previous else { return false }
        return current > previous && peak > 1e-4
    }

    private func updateMeterDisplay(with nextStatus: EngineStatus, underrunActiveNow: Bool) {
        guard nextStatus.reachable else {
            meters.reset()
            return
        }
        meters.update(peak: nextStatus.peak,
                      limiterGRDb: nextStatus.limiterGRDb,
                      underrunActiveNow: underrunActiveNow)
    }

    private func resetMeterDisplay() {
        meters.reset()
    }

    private func clearErrorBanner() {
        if errorBanner != nil { errorBanner = nil }
    }

    private func setErrorBanner(_ message: String) {
        if errorBanner != message { errorBanner = message }
    }

    private static func shouldPublishStatus(previous: EngineStatus, next: EngineStatus) -> Bool {
        previous.reachable != next.reachable
            || previous.state != next.state
            || previous.presetId != next.presetId
            || previous.manualBypass != next.manualBypass
            || previous.safeBypass != next.safeBypass
            || previous.paramsRevision != next.paramsRevision
            || previous.outputDeviceUID != next.outputDeviceUID
            || previous.keepDefault != next.keepDefault
            || previous.capabilities != next.capabilities
            || previous.volumeBoost != next.volumeBoost
    }

    private func currentParameters() -> EngineParameters {
        EngineParameters(preampDb: preampDb,
                         eqGainsDb: eqGainsDb,
                         limiterReleaseMs: limiterReleaseMs,
                         outputGainDb: outputGainDb,
                         spatialWidth: spatialAvailable ? spatialWidth : 0,
                         centerFocus: spatialAvailable ? centerFocus : 0,
                         crossfeed: spatialAvailable ? crossfeed : 0,
                         roomReduce: spatialAvailable ? roomReduce : 0,
                         spatialMode: spatialAvailable ? spatialMode : 0,
                         highpassHz: dynamicsAvailable ? highpassHz : 0,
                         compAmount: dynamicsAvailable ? compAmount : 0,
                         parametric: parametricAvailable ? parametric
                                     : Array(repeating: ParametricBand(), count: EngineParameters.paramBandCount))
    }

    private func apply(_ params: EngineParameters) {
        preampDb = params.preampDb
        eqGainsDb = params.eqGainsDb
        limiterReleaseMs = params.limiterReleaseMs
        outputGainDb = params.outputGainDb
        spatialWidth = spatialAvailable ? params.spatialWidth : 0
        centerFocus = spatialAvailable ? params.centerFocus : 0
        crossfeed = spatialAvailable ? params.crossfeed : 0
        roomReduce = spatialAvailable ? params.roomReduce : 0
        spatialMode = spatialAvailable ? params.spatialMode : 0
        highpassHz = dynamicsAvailable ? params.highpassHz : 0
        compAmount = dynamicsAvailable ? params.compAmount : 0
        parametric = params.parametric.count == EngineParameters.paramBandCount
            ? params.parametric
            : Array(repeating: ParametricBand(), count: EngineParameters.paramBandCount)
    }

    private func ensureSpatialAvailable() -> Bool {
        guard spatialAvailable else {
            errorBanner = status.reachable ? "현재 엔진이 Spatial을 지원하지 않습니다" : "연결 끊김"
            return false
        }
        return true
    }

    private static func clamp(_ value: Double, _ lo: Double, _ hi: Double) -> Double {
        min(hi, max(lo, value))
    }
}
