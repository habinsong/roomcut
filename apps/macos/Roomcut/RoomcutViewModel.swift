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
    @Published public internal(set) var roomcutIsDefault = false
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
    public var roomType: Double {
        get { editor.snapshot.parameters.roomType }
        set { editor.update { $0.parameters.roomType = newValue } }
    }
    public var roomAmount: Double {
        get { editor.snapshot.parameters.roomAmount }
        set { editor.update { $0.parameters.roomAmount = newValue } }
    }
    public var surroundType: Double {
        get { editor.snapshot.parameters.surroundType }
        set { editor.update { $0.parameters.surroundType = newValue } }
    }
    public var centerWidth: Double {
        get { editor.snapshot.parameters.centerWidth }
        set { editor.update { $0.parameters.centerWidth = newValue } }
    }
    public var surroundDepth: Double {
        get { editor.snapshot.parameters.surroundDepth }
        set { editor.update { $0.parameters.surroundDepth = newValue } }
    }
    public var bedRenderer: Double {
        get { editor.snapshot.parameters.bedRenderer }
        set { editor.update { $0.parameters.bedRenderer = newValue } }
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
    @Published public internal(set) var analysis: RoomcutAnalysisSnapshot?
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

    let client: EngineClientProtocol
    let writer: ParameterWriter
    let deviceWriter: DeviceCommandWriter
    let bypassWriter: BypassWriter
    let deviceReader: DeviceReadCoordinator
    private var deviceListWatcher: DeviceListWatcher?
    public let headTracking: HeadTrackingService
    let presetStore: PresetStore
    let preferences: AppPreferences
    private var observations: Set<AnyCancellable> = []
    let poller = EnginePoller()
    var writeErrorActive = false
    var paramsReadFailed = false
    private var paramsRetryAfter = Date.distantPast
    var isEditingParams: Bool { writer.isBusy || editor.isEditing }
    var isEditingVolume = false
    var isEditingBalance = false
    private var lastSeenPresetId: String?
    var lastSeenRevision: UInt32?
    private var lastUnderruns: UInt64?
    var analyzerVisible = false
    var didClaimDefaultOutput = false

    static let volumeEpsilon = 0.001
    public static let maxVolume = 2.0

    public convenience init() {
        self.init(client: LiveEngineClient())
    }

    public convenience init(client: EngineClientProtocol, debounceNanoseconds: UInt64 = 150_000_000,
                            defaults: UserDefaults = .standard) {
        self.init(client: client, debounceNanoseconds: debounceNanoseconds, defaults: defaults,
                  headMotion: CoreMotionHeadSource())
    }

    init(client: EngineClientProtocol, debounceNanoseconds: UInt64 = 150_000_000,
         defaults: UserDefaults = .standard, headMotion: HeadMotionSource) {
        self.client = client
        self.presets = client.presets
        self.presetStore = PresetStore(defaults: defaults)
        self.preferences = AppPreferences(defaults: defaults)
        self.writer = ParameterWriter(client: client, debounceNanoseconds: debounceNanoseconds)
        self.deviceWriter = DeviceCommandWriter(client: client)
        self.bypassWriter = BypassWriter(client: client)
        self.deviceReader = DeviceReadCoordinator(client: client)
        self.headTracking = HeadTrackingService(send: { [client] yaw, active in
            client.sendHeadPose(yawDegrees: yaw, active: active)
        }, source: headMotion)
        editor.relabel(saved: presetStore.activeSavedName.flatMap { presetStore.contains($0) ? $0 : nil },
                       builtin: presetStore.activeBuiltinID.flatMap { id in
                           client.presets.contains { $0.id == id } || PresetLibrary.preset(for: id) != nil
                               ? PresetLibrary.currentToken(id) : nil
                       })
        for publisher in [editor.objectWillChange, writer.objectWillChange, presetStore.objectWillChange,
                          preferences.objectWillChange, headTracking.objectWillChange] {
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
        // CoreAudio announces a device appearing or disappearing right away;
        // waiting for the next poll would leave freshly connected headphones
        // missing from the list for up to five seconds.
        if deviceListWatcher == nil {
            deviceListWatcher = DeviceListWatcher { [weak self] in
                Task { @MainActor in self?.deviceListDidChange() }
            }
        }
        deviceListWatcher?.start()
    }

    // The hardware list moved: drop the cached list and read it now.
    public func deviceListDidChange() {
        poller.invalidateDeviceList()
        Task { await refreshNow(waitForDevices: true) }
    }

    public func stopPolling() {
        headTracking.stop()
        deviceListWatcher?.stop()
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
            if RefreshPlanner.shouldPublish(previous: status, next: offline) {
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
        let plan = RefreshPlanner.plan(.init(
            previous: status, next: nextStatus,
            isEditingParams: isEditingParams,
            editsUnchanged: editRevision == editor.revision && writeRevision == writer.revision,
            lastSeenPresetId: lastSeenPresetId, lastSeenRevision: lastSeenRevision,
            deviceAutoPreset: deviceAutoPresetEnabled,
            mappedPresetToken: presetStore.deviceMap[nextStatus.outputDeviceUID].map(PresetLibrary.currentToken),
            pickerSelection: presetPickerSelection,
            didClaimDefault: didClaimDefaultOutput,
            analyzerVisible: analyzerVisible, hasAnalysis: analysis != nil,
            comparisonEnabled: comparison.enabled,
            retryDue: refreshDate >= paramsRetryAfter))
        if plan.deviceChanged { poller.invalidateDevices() }
        let shouldRefreshDeviceList = poller.isDue(.devices, at: refreshDate, force: plan.forceDeviceRefresh)
        let shouldRefreshControlState = poller.isDue(.controls, at: refreshDate, force: plan.forceDeviceRefresh)

        let underrunActiveNow = RefreshPlanner.underrunsActive(
            previous: lastUnderruns, current: nextStatus.underruns, peak: nextStatus.peak)
        lastUnderruns = nextStatus.underruns
        updateMeterDisplay(with: nextStatus, underrunActiveNow: underrunActiveNow)

        if plan.publishStatus { status = nextStatus }
        comparison.setSupported(nextStatus.supportsLevelMatch)

        if let token = plan.devicePresetToken { applyPickerSelection(token) }

        // On the first healthy poll, make Roomcut the macOS default output so app
        // audio actually flows through the engine — otherwise the meters sit at
        // silence (−60 dBFS) and the EQ is inaudible whenever macOS has a real
        // device selected as default. One-shot; the user can still switch away.
        if plan.claimDefault {
            didClaimDefaultOutput = true
            deviceWriter.submit(.claimDefault)
        }

        deviceTicket = deviceReader.request(.init(uid: nextStatus.outputDeviceUID, revision: poller.deviceRevision,
            devices: shouldRefreshDeviceList, controls: shouldRefreshControlState && !deviceWriter.blocksControls))
        if plan.wantsAnalysis && poller.isDue(.analysis, at: refreshDate) {
            await refreshAnalysis(in: cycle)
            guard poller.isCurrent(cycle) else { return }
            poller.didRead(.analysis, in: cycle)
        } else if plan.clearAnalysis {
            analysis = nil
        }

        if plan.wantsComparison && poller.isDue(.comparison, at: refreshDate) {
            await refreshComparisonState(in: cycle)
            guard poller.isCurrent(cycle) else { return }
            poller.didRead(.comparison, in: cycle)
        }

        if plan.wantsParams {
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

    public var keepDefault: Bool { status.keepDefault }
    // the values, then pushes). No-op if the engine doesn't support parametric.

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

    func currentParameters() -> EngineParameters { editor.snapshot.parameters.supported(by: status) }

    func ensureSpatialAvailable() -> Bool {
        guard spatialAvailable else {
            errorBanner = status.reachable ? "현재 엔진이 Spatial을 지원하지 않습니다" : "연결 끊김"
            return false
        }
        return true
    }

    static func clamp(_ value: Double, _ lo: Double, _ hi: Double) -> Double {
        min(hi, max(lo, value))
    }
}
