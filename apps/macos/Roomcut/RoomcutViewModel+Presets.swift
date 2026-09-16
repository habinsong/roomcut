import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

// Presets: the built-in list, the Basic tab's macro layer, and the user's own
// saved curves. Which preset is "active" is a UI fact rather than an engine
// one — the engine only ever holds parameters — so it is tracked here.
extension RoomcutViewModel {
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

    func setActivePreset(savedName: String?, builtinId: String?) {
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
}
