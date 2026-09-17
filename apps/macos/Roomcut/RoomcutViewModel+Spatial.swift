import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

// Everything in the Space tab: what the engine is capable of, the stereo field
// controls, the virtual room, the 5.1/7.1 upmix, and head tracking.
//
// Several of these read as one control to the listener but are two fields on the
// wire (output x surround inside spatialMode, ambience vs a real layout across
// spatialMode and surroundType), which is why the encoding lives here and not in
// the views.
extension RoomcutViewModel {
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

    func sendDeviceCommand(_ command: DeviceCommandWriter.Command) {
        poller.invalidateDevices()
        deviceWriter.submit(command)
        clearErrorBanner()
    }

    func completeDeviceWrite(id: UInt64, command: DeviceCommandWriter.Command, result: Result<Void, Error>) {
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
        centerFocus = Self.clamp(value, 0, 200)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setCrossfeed(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        crossfeed = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setRoomReduce(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        roomReduce = Self.clamp(value, 0, 200)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setSpatialValues(width: Double, centerFocus: Double, crossfeed: Double, roomReduce: Double) {
        guard ensureSpatialAvailable() else { return }
        spatialWidth = Self.clamp(width, -200, 200)
        self.centerFocus = Self.clamp(centerFocus, 0, 200)
        self.crossfeed = Self.clamp(crossfeed, 0, 100)
        self.roomReduce = Self.clamp(roomReduce, 0, 200)
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
        syncHeadTrackingAvailability()
        schedulePushParams(preservingPresetSelection: true)
    }

    // The two independent axes over the encoded spatialMode.
    public var spatialOutputIsHeadphone: Bool { spatialMode == 1 || spatialMode == 2 }
    public var spatialSurroundOn: Bool { spatialMode == 2 || spatialMode == 3 }

    public func setSpatialOutput(headphone: Bool) {
        setSpatialMode(Self.encodeSpatialMode(headphone: headphone, surround: spatialSurroundOn))
        // 7.1 only exists on headphones. Carrying it to speakers would leave the
        // control showing a layout that is not in its own list.
        if !headphone, surroundType >= 3 { setSurroundChoice(.virtual51) }
    }
    public func setSpatialSurround(_ on: Bool) {
        setSpatialMode(Self.encodeSpatialMode(headphone: spatialOutputIsHeadphone, surround: on))
    }
    static func encodeSpatialMode(headphone: Bool, surround: Bool) -> Double {
        switch (headphone, surround) {
        case (false, false): return 0   // speaker
        case (true, false):  return 1   // headphone
        case (true, true):   return 2   // headphone + surround
        case (false, true):  return 3   // speaker + surround
        }
    }

    // MARK: Virtual room

    // Both outputs take a room; the engine picks the headphone or speaker
    // profile from the current Speaker/Headphone setting.
    public var virtualRoomAvailable: Bool { spatialAvailable && status.supportsVirtualRoom }

    // The upmix runs on both outputs, differently: virtual loudspeakers on
    // headphones, a folded and widened pair on speakers. Two boxes in front of
    // you cannot put a source behind you, so the speaker side widens rather than
    // pretending — and it stays mono-compatible while doing it.
    public var upmixAvailable: Bool { spatialAvailable && status.supportsUpmix }

    // Surround is ONE control to the listener and TWO fields on the wire: the
    // older ambience surround is a bit inside spatialMode, the 5.1/7.1 upmix is
    // surroundType. Exposing them separately is what let the UI drift out of
    // step with the engine — a tap could leave both set, or neither. This is the
    // only way the app changes either of them, and it writes both at once.
    public enum SurroundChoice: Int, CaseIterable, Sendable {
        case off = 0, ambience = 1, virtual51 = 2, virtual71 = 3
    }

    public var surroundChoice: SurroundChoice {
        if surroundType >= 3 { return .virtual71 }
        if surroundType >= 2 { return .virtual51 }
        return spatialSurroundOn ? .ambience : .off
    }

    // Speakers have no back, so 5.1 and 7.1 render identically there; offering
    // both would be a choice that changes nothing.
    public var surroundChoices: [SurroundChoice] {
        guard upmixAvailable else { return [.off, .ambience] }
        return spatialOutputIsHeadphone ? [.off, .ambience, .virtual51, .virtual71]
                                        : [.off, .ambience, .virtual51]
    }

    public func setSurroundChoice(_ choice: SurroundChoice) {
        guard ensureSpatialAvailable() else { return }
        let layout: Double
        let ambience: Bool
        switch choice {
        case .off:       layout = 0; ambience = false
        case .ambience:  layout = 0; ambience = true
        case .virtual51: layout = 2; ambience = false
        case .virtual71: layout = spatialOutputIsHeadphone ? 3 : 2; ambience = false
        }
        let mode = Self.encodeSpatialMode(headphone: spatialOutputIsHeadphone, surround: ambience)
        editor.update {
            $0.parameters.surroundType = layout
            $0.parameters.spatialMode = mode
        }
        syncHeadTrackingAvailability()
        schedulePushParams(preservingPresetSelection: true)
    }

    // Centre width decides how much of the centre image becomes a DISCRETE
    // centre channel. Two speakers in front of you have no such channel — the
    // centre is a phantom either way — so on speakers the control is folded
    // straight back where it came from and measurably does nothing at all
    // (identical output at 0 and at 100). Do not show a dead control.
    public var centerWidthApplies: Bool { spatialOutputIsHeadphone }

    // The same holds for Surround Depth since speakers stopped going through the
    // upmix (they widen the stereo pair itself): rendered through the chain on
    // six programmes, speaker Wide at depth 20, 50 and 85 measured identical in
    // level, colour and reverb (2026-09-17). Not offered where it does nothing.
    public var surroundDepthApplies: Bool { spatialOutputIsHeadphone }

    // Crossfeed builds a fixed virtual stage. While head tracking or the upmix
    // is rendering one with real angles, the engine zeroes it — so the slider
    // would move and nothing would happen. Hide it instead of lying.
    public var crossfeedActive: Bool {
        guard spatialOutputIsHeadphone else { return true }   // speakers: XTC, unrelated
        return !(headTrackingOn || surroundType >= 2)
    }

    // Kept for presets and tests that set the layout directly.
    public func setSurroundType(_ type: Double) {
        let t = type.rounded()
        setSurroundChoice(t >= 3 ? .virtual71 : (t >= 2 ? .virtual51 : .off))
    }

    public func setCenterWidth(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        centerWidth = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setSurroundDepth(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        surroundDepth = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    // Who renders a headphone 5.1/7.1 bed: Apple's spatial renderer where the
    // engine has one attached, or the built-in virtual speakers. A choice only
    // exists where both could play — headphones, an actual layout, and an engine
    // that reports the system renderer — so that A/B can compare the two.
    public enum BedRendererChoice: Int, CaseIterable, Sendable { case system = 0, builtIn = 1 }

    public var bedRendererChoice: BedRendererChoice { bedRenderer >= 1 ? .builtIn : .system }

    public var bedRendererChoiceAvailable: Bool {
        upmixAvailable && spatialOutputIsHeadphone && surroundType >= 2
            && status.supportsBedRenderer && status.systemBedRenderer
    }

    // Whether this sound's headphone bed goes to Apple's renderer: the engine has
    // one attached and the sound has not picked the built-in bed. (Whether a bed
    // renders at all is the layout and output, shown elsewhere.)
    public var systemBedInUse: Bool { status.systemBedRenderer && bedRendererChoice == .system }

    public func setBedRendererChoice(_ choice: BedRendererChoice) {
        guard ensureSpatialAvailable() else { return }
        bedRenderer = Double(choice.rawValue)
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setRoomType(_ type: Double) {
        guard ensureSpatialAvailable() else { return }
        let t = type.rounded()
        roomType = t >= 3 ? 3.0 : (t >= 2 ? 2.0 : (t >= 1 ? 1.0 : 0.0))
        // An engine that predates the room reports its level as 0. Picking a
        // room then has to start at the reference level, not at silence.
        if roomType >= 1, roomAmount <= 0 { roomAmount = 50 }
        schedulePushParams(preservingPresetSelection: true)
    }

    public func setRoomAmount(_ value: Double) {
        guard ensureSpatialAvailable() else { return }
        roomAmount = Self.clamp(value, 0, 100)
        schedulePushParams(preservingPresetSelection: true)
    }

    // MARK: Head tracking (headphones with motion sensors)

    // Availability is three things at once: the engine can render it, the
    // output is set to Headphone, and the listener's headphones actually have
    // the sensors.
    public var headTrackingAvailable: Bool {
        spatialAvailable && status.supportsHeadTracking && spatialOutputIsHeadphone
            && headTracking.isSupported
    }

    // The switch: what the listener chose. It stays on across a trip to
    // speakers, where the sensor pauses (see syncHeadTrackingAvailability).
    public var headTrackingOn: Bool { headTracking.isTracking }

    // Whether the stage is actually following the head right now.
    public var headTrackingActive: Bool { headTracking.isTracking && headTrackingAvailable }

    // The live head angle, for the stage picture. Quantised to 2 degrees: the
    // sensor delivers 50 a second and the drawing has no use for that rate, but
    // a view that redrew on every one of them would.
    public var headYawDegrees: Double {
        guard headTracking.isDelivering else { return 0 }
        return (headTracking.yawDegrees / 2).rounded() * 2
    }

    public func setHeadTracking(_ on: Bool) {
        guard ensureSpatialAvailable() else { return }
        guard on else {
            headTracking.stop()
            return
        }
        guard headTrackingAvailable else {
            setErrorBanner("이 헤드폰은 헤드 트래킹을 지원하지 않습니다")
            return
        }
        // The engine's head-tracked renderer places the speakers itself, so the
        // crossfeed model that does the same job for a fixed head would stack
        // on top of it.
        if crossfeed != 0 { setCrossfeed(0) }
        syncHeadTrackingAvailability()
        headTracking.start()
    }

    public func recentreHead() {
        guard headTracking.isTracking else { return }
        headTracking.recentre()
    }

    // The output switched away from headphones (or back): the renderer has
    // nothing to do on speakers, so the sensor pauses — but the listener's
    // choice stays. This used to switch tracking off for good, and a trip to
    // Speaker and back left it off with nothing to say why.
    private func syncHeadTrackingAvailability() {
        headTracking.setAllowed(headTrackingAvailable)
    }

    // MARK: Parametric EQ (N user-configurable biquad bands)

    // Parametric clamps mirror PresetValidator (core/presets/PresetValidator.hpp);
    // the engine re-clamps, but matching here keeps the UI honest.
    public static let parametricFreqRange = 20.0...20000.0
    public static let parametricGainRange = -24.0...24.0
    public static let parametricQRange = 0.1...12.0

    // Replace one band wholesale (the editor builds the updated band, validates
}
