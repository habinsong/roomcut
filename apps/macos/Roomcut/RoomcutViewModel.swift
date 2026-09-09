import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

public typealias EngineModel = RoomcutViewModel

@MainActor
public final class RoomcutViewModel: ObservableObject {
    @Published public var status = EngineStatus()
    // Master switch: is Roomcut the live macOS default output (audio routed
    // through the engine)? Reflected from CoreAudio each poll; drives the top
    // ON/OFF toggle. OFF means the system default is a real device and Roomcut
    // is fully out of the path.
    @Published public private(set) var roomcutIsDefault = false
    public let editor = SoundEditor()
    public let comparison = SoundComparison()
    public var preampDb: Double {
        get { editor.snapshot.parameters.preampDb }
        set { editor.update { $0.parameters.preampDb = newValue } }
    }
    public var eqGainsDb: [Double] {
        get { editor.snapshot.parameters.eqGainsDb }
        set { editor.update { $0.parameters.eqGainsDb = newValue } }
    }
    public var outputGainDb: Double {
        get { editor.snapshot.parameters.outputGainDb }
        set { editor.update { $0.parameters.outputGainDb = newValue } }
    }
    public var limiterReleaseMs: Double {
        get { editor.snapshot.parameters.limiterReleaseMs }
        set { editor.update { $0.parameters.limiterReleaseMs = newValue } }
    }
    public var spatialWidth: Double {
        get { editor.snapshot.parameters.spatialWidth }
        set { editor.update { $0.parameters.spatialWidth = newValue } }
    }
    public var centerFocus: Double {
        get { editor.snapshot.parameters.centerFocus }
        set { editor.update { $0.parameters.centerFocus = newValue } }
    }
    public var crossfeed: Double {
        get { editor.snapshot.parameters.crossfeed }
        set { editor.update { $0.parameters.crossfeed = newValue } }
    }
    public var roomReduce: Double {
        get { editor.snapshot.parameters.roomReduce }
        set { editor.update { $0.parameters.roomReduce = newValue } }
    }
    public var spatialMode: Double {
        get { editor.snapshot.parameters.spatialMode }
        set { editor.update { $0.parameters.spatialMode = newValue } }
    }
    public var compAmount: Double {
        get { editor.snapshot.parameters.compAmount }
        set { editor.update { $0.parameters.compAmount = newValue } }
    }
    public var highpassHz: Double {
        get { editor.snapshot.parameters.highpassHz }
        set { editor.update { $0.parameters.highpassHz = newValue } }
    }
    public var parametric: [ParametricBand] {
        get { editor.snapshot.parameters.parametric }
        set { editor.update { $0.parameters.parametric = newValue } }
    }
    public var macroValues: [EqMacro: Double] {
        get { editor.snapshot.macros }
        set { editor.update { $0.macros = newValue } }
    }
    public var savedPresets: [SavedPreset] {
        get { presetStore.presets }
        set { presetStore.presets = newValue }
    }
    public var activeSavedPreset: String? { editor.snapshot.savedPresetName }
    public var activeBuiltinPresetId: String? { editor.snapshot.builtinPresetID }
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
    public var nowPlayingTheme: RoomcutNowPlayingTheme { preferences.theme }
    public var nowPlayingLayout: RoomcutNowPlayingLayout { preferences.layout }
    public var appearance: RoomcutAppearance { preferences.appearance }
    public var themeSyncEnabled: Bool { preferences.themeSync }
    public var language: AppLanguage { preferences.language }
    @Published public private(set) var analysis: RoomcutAnalysisSnapshot?
    public var deviceAutoPresetEnabled: Bool { presetStore.autoApply }

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
    private let writer: ParameterWriter
    private let deviceWriter: DeviceCommandWriter
    private let bypassWriter: BypassWriter
    private let deviceReader: DeviceReadCoordinator
    private let presetStore: PresetStore
    private let preferences: AppPreferences
    private var observations: Set<AnyCancellable> = []
    private let poller = EnginePoller()
    private var writeErrorActive = false
    private var paramsReadFailed = false
    private var paramsRetryAfter = Date.distantPast
    private var isEditingParams: Bool { writer.isBusy || editor.isEditing }
    private var isEditingVolume = false
    private var isEditingBalance = false
    private var lastSeenPresetId: String?
    private var lastSeenRevision: UInt32?
    private var lastUnderruns: UInt64?
    private var analyzerVisible = false
    private var didClaimDefaultOutput = false

    private static let volumeEpsilon = 0.001
    public static let maxVolume = 2.0

    public convenience init() {
        self.init(client: LiveEngineClient())
    }

    public init(client: EngineClientProtocol, debounceNanoseconds: UInt64 = 150_000_000,
                defaults: UserDefaults = .standard) {
        self.client = client
        self.presets = client.presets
        self.presetStore = PresetStore(defaults: defaults)
        self.preferences = AppPreferences(defaults: defaults)
        self.writer = ParameterWriter(client: client, debounceNanoseconds: debounceNanoseconds)
        self.deviceWriter = DeviceCommandWriter(client: client)
        self.bypassWriter = BypassWriter(client: client)
        self.deviceReader = DeviceReadCoordinator(client: client)
        editor.relabel(saved: presetStore.activeSavedName.flatMap { presetStore.contains($0) ? $0 : nil },
                       builtin: presetStore.activeBuiltinID.flatMap { id in
                           client.presets.contains { $0.id == id } || PresetLibrary.preset(for: id) != nil ? id : nil
                       })
        for publisher in [editor.objectWillChange, writer.objectWillChange, presetStore.objectWillChange, preferences.objectWillChange] {
            publisher.sink { [weak self] in self?.objectWillChange.send() }.store(in: &observations)
        }
        writer.didComplete = { [weak self] id, command, result in
            await self?.completeWrite(id: id, command: command, result: result)
        }
        deviceWriter.didComplete = { [weak self] id, command, result in
            self?.completeDeviceWrite(id: id, command: command, result: result)
        }
        deviceReader.didRead = { [weak self] request, readback in
            self?.acceptDeviceReadback(readback, request: request)
        }
        bypassWriter.didComplete = { [weak self] id, result in
            guard let self else { return }
            if case .success = result { await self.refreshNow(waitForDevices: false) }
            guard self.bypassWriter.isCurrent(id) else { return }
            self.clearErrorBanner()
        }
    }

    public func startPolling() {
        poller.start { [weak self] in await self?.refreshNow(waitForDevices: false) }
    }

    public func stopPolling() {
        poller.stop()
        deviceReader.cancel()
        writer.cancel()
        deviceWriter.cancel()
        editor.finishEditing()
        lastSeenRevision = nil
    }

    public func refresh() {
        Task { await refreshNow(waitForDevices: false) }
    }

    public func refreshNow() async { await refreshNow(waitForDevices: true) }

    func refreshNow(waitForDevices: Bool) async {
        guard let cycle = poller.begin() else { return }
        var deviceTicket: DeviceReadCoordinator.Ticket?
        defer {
            poller.finish(cycle)
            if Task.isCancelled { deviceTicket?.cancel() }
        }
        let editRevision = editor.revision
        let writeRevision = writer.revision
        let nextStatus: EngineStatus
        do {
            nextStatus = try await client.getState()
        } catch {
            guard poller.isCurrent(cycle) else { return }
            deviceReader.cancel()
            poller.invalidateDevices()
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
            comparison.failed()
            return
        }

        guard poller.isCurrent(cycle) else { return }
        let refreshDate = Date()
        let wasOffline = !status.reachable
        let previousDeviceUID = status.outputDeviceUID
        let outputDeviceChanged = status.outputDeviceUID != nextStatus.outputDeviceUID
        if outputDeviceChanged { poller.invalidateDevices() }
        let needsParams = wasOffline
            || status.capabilities != nextStatus.capabilities
            || lastSeenPresetId != nextStatus.presetId
            || lastSeenRevision != nextStatus.paramsRevision
        let shouldRefreshDeviceList = poller.isDue(.devices, at: refreshDate, force: wasOffline || outputDeviceChanged)
        let shouldRefreshControlState = poller.isDue(.controls, at: refreshDate, force: wasOffline || outputDeviceChanged)

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
        comparison.setSupported(nextStatus.supportsLevelMatch)

        // Per-device presets: react only to a REAL device switch while connected
        // (both UIDs non-empty, engine previously reachable). First connect is
        // left alone — the engine already resumed the user's last state, and
        // stomping it with a mapped preset would surprise.
        if deviceAutoPresetEnabled && outputDeviceChanged && !wasOffline
            && !previousDeviceUID.isEmpty && !nextStatus.outputDeviceUID.isEmpty
            && !isEditingParams,
           let token = presetStore.deviceMap[nextStatus.outputDeviceUID],
           token != presetPickerSelection {
            applyPickerSelection(token)
        }

        // On the first healthy poll, make Roomcut the macOS default output so app
        // audio actually flows through the engine — otherwise the meters sit at
        // silence (−60 dBFS) and the EQ is inaudible whenever macOS has a real
        // device selected as default. One-shot; the user can still switch away.
        if !didClaimDefaultOutput && nextStatus.state == EngineStatus.running {
            didClaimDefaultOutput = true
            deviceWriter.submit(.claimDefault)
        }

        deviceTicket = deviceReader.request(.init(uid: nextStatus.outputDeviceUID, revision: poller.deviceRevision,
            devices: shouldRefreshDeviceList, controls: shouldRefreshControlState && !deviceWriter.blocksControls))
        if analyzerVisible
            && nextStatus.supportsAnalyzer
            && poller.isDue(.analysis, at: refreshDate) {
            await refreshAnalysis(in: cycle)
            guard poller.isCurrent(cycle) else { return }
            poller.didRead(.analysis, in: cycle)
        } else if (!nextStatus.supportsAnalyzer || !analyzerVisible) && analysis != nil {
            analysis = nil
        }

        if !needsParams && comparison.enabled && nextStatus.supportsLevelMatch && !isEditingParams
            && poller.isDue(.comparison, at: refreshDate) {
            await refreshComparisonState(in: cycle)
            guard poller.isCurrent(cycle) else { return }
            poller.didRead(.comparison, in: cycle)
        }

        if needsParams && !isEditingParams && editRevision == editor.revision && writeRevision == writer.revision
            && (wasOffline || refreshDate >= paramsRetryAfter) {
            do {
                try await loadParams(in: cycle)
                guard poller.isCurrent(cycle) else { return }
                paramsReadFailed = false
                clearErrorBanner()
            } catch {
                guard poller.isCurrent(cycle) else { return }
                paramsReadFailed = true
                paramsRetryAfter = Date().addingTimeInterval(0.5)
                setErrorBanner("엔진 값을 불러오지 못했습니다")
            }
        } else { clearErrorBanner() }

        // Explicit refresh callers may await device data; periodic status and
        // meters release their slot immediately and never wait for that queue.
        poller.finish(cycle)
        if waitForDevices { await deviceTicket?.wait() }
    }

    private func acceptDeviceReadback(_ readback: DeviceReadback, request: DeviceReadCoordinator.Request) {
        guard status.reachable, status.outputDeviceUID == request.uid, poller.deviceRevision == request.revision else { return }
        if !deviceWriter.isBusy && roomcutIsDefault != readback.roomcutIsDefault { roomcutIsDefault = readback.roomcutIsDefault }
        if let devices = readback.devices {
            if outputDevices != devices { outputDevices = devices }
            poller.didReadDevice(.devices, revision: request.revision)
        }
        if let controls = readback.controls, !deviceWriter.blocksControls {
            applyDeviceReadback(controls)
            poller.didReadDevice(.controls, revision: request.revision)
        }
    }

    public func apply(presetId: String) {
        Task { await applyPreset(presetId) }
    }

    public func applyPreset(_ presetId: String) async {
        editor.finishEditing()
        _ = await writer.preset(presetId, editorRevision: editor.revision, comparison: prepareComparisonWrite())
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

    public func presetNameExists(_ name: String) -> Bool { presetStore.contains(name) }
    public static let customPresetName = "Custom"

    public func makeCurrentPreset(name: String, folder: String? = nil, roomTuneInfo: String? = nil) -> SavedPreset {
        var value = editor.snapshot
        value.parameters = currentParameters()
        var preset = value.preset(name: name, folder: folder, roomTuneInfo: roomTuneInfo)
        if !parametricAvailable { preset.parametric = [] }
        return preset
    }

    @discardableResult
    public func saveCurrentAsPreset(name: String, roomTuneInfo: String? = nil) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        let name = trimmed.isEmpty ? Self.customPresetName : trimmed
        presetStore.save(makeCurrentPreset(name: name, roomTuneInfo: roomTuneInfo))
        setActivePreset(savedName: name, builtinId: nil)
        return true
    }

    public func applySavedPreset(_ preset: SavedPreset) {
        editor.finishEditing()
        var value = SoundSnapshot(preset: preset)
        value.parameters = value.parameters.supported(by: status)
        editor.update { $0 = value }
        setActivePreset(savedName: value.savedPresetName, builtinId: value.builtinPresetID)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func deleteSavedPreset(_ preset: SavedPreset) {
        presetStore.delete(preset)
        if activeSavedPreset?.caseInsensitiveCompare(preset.name) == .orderedSame {
            setActivePreset(savedName: nil, builtinId: nil)
        }
    }

    // The name to show wherever the preset surfaces (menu bar, Settings, the EQ
    // summaries): the active saved preset, else the builtin name, else "Custom".
    public var currentPresetName: String {
        if editor.hasBaseline || editor.isEditing {
            if let name = activeSavedPreset { return name }
            if let id = activeBuiltinPresetId, let preset = PresetLibrary.preset(for: id) { return preset.name }
            if let id = activeBuiltinPresetId, let preset = presets.first(where: { $0.id == id }) { return preset.name }
            return "Custom"
        }
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
        status.reachable && (editor.hasBaseline || status.presetId == "custom") && activeSavedPreset == nil
            && activeBuiltinPresetId == nil
    }

    // Picker token: builtin preset id, "saved:<name>", or "custom".
    public var presetPickerSelection: String {
        if editor.hasBaseline {
            if let name = activeSavedPreset { return "saved:\(name)" }
            return activeBuiltinPresetId ?? "custom"
        }
        if status.presetId == "custom", let name = activeSavedPreset { return "saved:\(name)" }
        if status.presetId == "custom", let builtin = activeBuiltinPresetId { return builtin }
        return status.presetId
    }

    public func applyPickerSelection(_ token: String) {
        if let preset = PresetLibrary.preset(for: token) {
            applySavedPreset(preset)
        } else if token.hasPrefix("saved:") {
            let name = String(token.dropFirst("saved:".count))
            if let sp = (savedPresets + PresetLibrary.all).first(where: { $0.name.caseInsensitiveCompare(name) == .orderedSame }) { applySavedPreset(sp) }
        } else if token != "custom" {
            apply(presetId: token)
        }
    }

    private func setActivePreset(savedName: String?, builtinId: String?) {
        editor.relabel(saved: savedName, builtin: builtinId)
        presetStore.recordSelection(saved: savedName, builtin: builtinId)
        recordDevicePreset()
    }

    public func setDeviceAutoPreset(_ on: Bool) {
        guard deviceAutoPresetEnabled != on else { return }
        presetStore.setAutoApply(on)
        if on { recordDevicePreset() }
    }

    private func recordDevicePreset() {
        let token = activeSavedPreset.map { "saved:\($0)" } ?? activeBuiltinPresetId
        presetStore.remember(token, device: status.outputDeviceUID)
    }

    public typealias PresetExportFile = PresetArchive
    public func exportPresetsData() -> Data? { presetStore.exportData() }
    @discardableResult
    public func importPresets(from data: Data) -> Int? { presetStore.importData(data) }

    public var selectedDeviceUID: String { status.outputDeviceUID }

    public func selectDevice(_ uid: String) {
        sendDeviceCommand(.output(uid))
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
        sendDeviceCommand(.format(uid: uid, sampleRate: sampleRate, bitDepth: bitDepth))
    }

    // UI edits are immediate; DeviceCommandWriter owns asynchronous hardware
    // completion. Readback cannot snap the slider back during a pending write.
    public func beginVolumeEdit() { poller.invalidateDevices(); isEditingVolume = true }
    public func endVolumeEdit() { poller.invalidateDevices(); isEditingVolume = false }

    public func setVolume(_ v: Double) {
        volume = min(Self.maxVolume, max(0.0, v))
        sendDeviceCommand(.volume(volume))
    }

    // Balance uses the same optimistic UI and serialized write/readback guard.
    public func beginBalanceEdit() { poller.invalidateDevices(); isEditingBalance = true }
    public func endBalanceEdit() { poller.invalidateDevices(); isEditingBalance = false }

    public func setBalance(_ pan: Double) {
        balance = min(1.0, max(-1.0, pan))
        sendDeviceCommand(.balance(balance))
    }

    private func applyDeviceReadback(_ readback: DeviceControlReadback) {
        if audioFormat != readback.audioFormat { audioFormat = readback.audioFormat }
        if deviceFormatOptions != readback.formatOptions { deviceFormatOptions = readback.formatOptions }

        guard !isEditingVolume else { return }
        if let v = readback.volume {
            if !hasVolumeControl { hasVolumeControl = true }
            if abs(volume - v) > Self.volumeEpsilon { volume = v }
        } else if hasVolumeControl {
            hasVolumeControl = false
        }

        // Balance mirrors the device's per-channel volume — poll it back so an
        // external change (Audio MIDI Setup / System Settings) is reflected.
        guard !isEditingBalance else { return }
        if let p = readback.balance {
            if !hasBalanceControl { hasBalanceControl = true }
            if abs(balance - p) > Self.volumeEpsilon { balance = p }
        } else if hasBalanceControl {
            hasBalanceControl = false
        }
    }

    public func setNowPlayingTheme(_ theme: RoomcutNowPlayingTheme) { preferences.setTheme(theme) }
    public func setNowPlayingLayout(_ layout: RoomcutNowPlayingLayout) { preferences.setLayout(layout) }
    public func setAppearance(_ appearance: RoomcutAppearance) { preferences.setAppearance(appearance) }
    public func setThemeSync(_ on: Bool) { preferences.setThemeSync(on) }
    public func setLanguage(_ language: AppLanguage) { preferences.setLanguage(language) }

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
        sendDeviceCommand(.keepDefault(on))
    }

    public func setBypass(_ on: Bool) {
        bypassWriter.manual(on)
        clearErrorBanner()
    }

    public func makeBypassOverride() -> BypassOverride {
        BypassOverride(writer: bypassWriter, current: { self.status.manualBypass })
    }

    // Await an explicit write; room measurement uses an owned temporary override.
    @discardableResult
    public func updateBypass(_ on: Bool) async -> Bool {
        await bypassWriter.manualAndWait(on)
    }

    // Master switch: fully engage/disengage Roomcut by switching the system
    // default output between the Roomcut virtual device and the real device.
    // keep-default reclaim is coupled so OFF actually sticks (otherwise the
    // engine would grab the default straight back).
    public func setMasterEnabled(_ on: Bool) {
        didClaimDefaultOutput = true // An explicit choice supersedes startup's automatic claim.
        roomcutIsDefault = on
        sendDeviceCommand(.master(on))
    }

    var isDeviceWritePending: Bool { deviceWriter.isBusy }
    var isBypassWritePending: Bool { bypassWriter.isBusy }
    var isDeviceReadPending: Bool { deviceReader.isBusy }

    private func sendDeviceCommand(_ command: DeviceCommandWriter.Command) {
        poller.invalidateDevices()
        deviceWriter.submit(command)
        clearErrorBanner()
    }

    private func completeDeviceWrite(id: UInt64, command: DeviceCommandWriter.Command, result: Result<Void, Error>) {
        if command.affectsControls { poller.invalidateDevices() }
        if case .failure = result { setErrorBanner(command.failureMessage) }
        else { clearErrorBanner() }
        guard command.affectsControls else { return }
        Task { [weak self] in
            guard let self, self.deviceWriter.isCurrent(id) else { return }
            await self.refreshNow(waitForDevices: false)
        }
    }

    public func setSpatialWidth(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        spatialWidth = Self.clamp(value, -200, 200)
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
        spatialWidth = Self.clamp(width, -200, 200)
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
        if !preservingPresetSelection { setActivePreset(savedName: nil, builtinId: nil) }
        writeErrorActive = false
        var snapshot = editor.snapshot
        snapshot.parameters = currentParameters()
        writer.parameters(snapshot, comparison: prepareComparisonWrite())
    }

    private func loadParams(in cycle: EnginePoller.Cycle) async throws {
        let editRevision = editor.revision
        let writeRevision = writer.revision
        let read = try await readSoundParameters()
        let params = read.parameters
        guard poller.isCurrent(cycle) else { return }
        guard editRevision == editor.revision, writeRevision == writer.revision, !isEditingParams else { return }
        var value = editor.snapshot
        let normalized = params.supported(by: status)
        let changed = value.parameters != normalized
        value.parameters = normalized
        if changed && editor.hasBaseline { value.macros = [:]; value.savedPresetName = nil; value.builtinPresetID = nil }
        let presetID = read.comparison?.presetID ?? status.presetId
        if presetID != "custom" {
            value.savedPresetName = nil
            value.builtinPresetID = presets.contains { $0.id == presetID } ? presetID : nil
        }
        editor.synchronize(value, reset: changed)
        acceptComparison(read.comparison)
        lastSeenPresetId = presetID
        lastSeenRevision = read.comparison.map { UInt32(truncatingIfNeeded: $0.revision) } ?? status.paramsRevision
    }

    private func completeWrite(id: UInt64, command: ParameterWriter.Command, result: Result<Void, Error>) async {
        switch command {
        case .parameters(let value, _): guard value.parameters == currentParameters() else { return }
        case .preset(_, let revision, _): guard editor.revision == revision else { return }
        }
        do {
            await refreshNow(waitForDevices: false)
            guard writer.isCurrent(id) else { return }
            let readStatus = status
            let read = try await readSoundParameters()
            let params = read.parameters
            guard writer.isCurrent(id) else { return }
            switch result {
            case .success:
                var value: SoundSnapshot
                switch command {
                case .parameters(let sent, _):
                    guard sent.parameters == currentParameters() else { return }
                    value = editor.snapshot
                case .preset(let presetID, let revision, _):
                    guard editor.revision == revision else { return }
                    value = SoundSnapshot(parameters: params, builtinPresetID: presetID)
                    editor.update { $0 = value }
                }
                value.parameters = params.supported(by: status)
                editor.synchronize(value, reset: false)
                editor.commitEdit()
                setActivePreset(savedName: value.savedPresetName, builtinId: value.builtinPresetID)
                writeErrorActive = false
                paramsReadFailed = false
                clearErrorBanner()
            case .failure:
                let normalized = params.supported(by: status)
                let unchanged = normalized == currentParameters()
                var value = unchanged ? editor.snapshot : SoundSnapshot(parameters: normalized)
                let presetID = read.comparison?.presetID ?? status.presetId
                if !unchanged && presetID != "custom" { value.builtinPresetID = presetID }
                editor.synchronize(value, reset: !unchanged)
                if unchanged { editor.commitEdit() }
                setActivePreset(savedName: value.savedPresetName, builtinId: value.builtinPresetID)
                writeErrorActive = true
                errorBanner = "엔진 값을 다시 불러왔습니다"
            }
            acceptComparison(read.comparison)
            lastSeenPresetId = read.comparison?.presetID ?? readStatus.presetId
            lastSeenRevision = read.comparison.map { UInt32(truncatingIfNeeded: $0.revision) } ?? readStatus.paramsRevision
        } catch {
            guard writer.isCurrent(id) else { return }
            writeErrorActive = true
            errorBanner = "엔진 값을 다시 불러오지 못했습니다"
            comparison.failed()
        }
    }

    public var canUndoSound: Bool { status.reachable && editor.canUndo }
    public var canRedoSound: Bool { status.reachable && editor.canRedo }
    public var isSoundWritePending: Bool { writer.isBusy }
    public func beginParameterEdit() { editor.beginGesture() }
    public func endParameterEdit() {
        guard editor.isGestureActive else { return }
        editor.endGesture()
        schedulePushParams(preservingPresetSelection: true)
    }
    public func undoSound() { guard canUndoSound else { return }; editor.undo(); sendHistoryState() }
    public func redoSound() { guard canRedoSound else { return }; editor.redo(); sendHistoryState() }
    public func selectComparison(_ slot: SoundComparisonSlot) {
        guard status.reachable, editor.hasBaseline, editor.activeSlot != slot else { return }
        editor.select(slot)
        sendHistoryState()
    }
    public func copyComparison() {
        guard status.reachable, editor.hasBaseline else { return }
        editor.copyToOther()
        if comparison.enabled { sendHistoryState() }
    }
    public func toggleLevelMatch() {
        guard status.reachable, editor.hasBaseline, comparison.supported else { return }
        editor.finishEditing()
        comparison.requestEnabled(!comparison.enabled)
        sendHistoryState()
    }
    private func sendHistoryState() {
        writeErrorActive = false
        setActivePreset(savedName: editor.snapshot.savedPresetName, builtinId: editor.snapshot.builtinPresetID)
        var value = editor.snapshot
        value.parameters = currentParameters()
        writer.parameters(value, comparison: prepareComparisonWrite(), immediate: true)
    }

    private func prepareComparisonWrite() -> SoundComparisonWrite? {
        guard comparison.supported else { return nil }
        if comparison.enabled { comparison.requestEnabled(true) }
        return SoundComparisonWrite(reference: editor.referenceSnapshot.parameters.supported(by: status), enabled: comparison.enabled)
    }

    private func readSoundParameters() async throws -> (parameters: EngineParameters, comparison: EngineComparisonState?) {
        if status.supportsLevelMatch {
            let value = try await client.getComparison()
            return (value.current, value)
        }
        return (try await client.getParams(), nil)
    }

    private func acceptComparison(_ snapshot: EngineComparisonState?) {
        guard let snapshot else { return }
        let reference = snapshot.reference.supported(by: status)
        if snapshot.enabled && reference != editor.referenceSnapshot.parameters { editor.restoreReference(reference) }
        comparison.accept(snapshot)
    }

    private func refreshComparisonState(in cycle: EnginePoller.Cycle) async {
        let editRevision = editor.revision, writeRevision = writer.revision
        do {
            let snapshot = try await client.getComparison()
            guard poller.isCurrent(cycle) else { return }
            guard editRevision == editor.revision, writeRevision == writer.revision, !isEditingParams else { return }
            guard snapshot.current.supported(by: status) == currentParameters(),
                  !snapshot.enabled || snapshot.reference.supported(by: status) == editor.referenceSnapshot.parameters.supported(by: status) else {
                lastSeenRevision = nil
                comparison.requestEnabled(snapshot.enabled)
                return
            }
            comparison.accept(snapshot)
        } catch {
            if poller.isCurrent(cycle) { comparison.failed() }
        }
    }

    private func refreshAnalysis(in cycle: EnginePoller.Cycle) async {
        do {
            let next = try await client.getAnalysis()
            guard poller.isCurrent(cycle), analyzerVisible else { return }
            if analysis != next { analysis = next }
        } catch {
            guard poller.isCurrent(cycle), analyzerVisible else { return }
            if analysis != nil { analysis = nil }
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
        let controlError = deviceWriter.error ?? bypassWriter.error
        if !writeErrorActive && !paramsReadFailed && errorBanner != controlError { errorBanner = controlError }
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

    private func currentParameters() -> EngineParameters { editor.snapshot.parameters.supported(by: status) }

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
