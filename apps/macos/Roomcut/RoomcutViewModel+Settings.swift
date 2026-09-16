import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

// The settings a listener picks rather than tunes: which device plays, at what
// format, how loud, and how the app looks. These talk to CoreAudio and to the
// preferences store, not to the DSP chain.
extension RoomcutViewModel {
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

    func applyDeviceReadback(_ readback: DeviceControlReadback) {
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
}
