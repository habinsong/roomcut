import Combine
import Foundation

public struct PresetArchive: Codable {
    public var version: Int
    public var presets: [SavedPreset]
}

@MainActor
final class PresetStore: ObservableObject {
    @Published var presets: [SavedPreset] = []
    @Published private(set) var autoApply = false
    private(set) var deviceMap: [String: String] = [:]
    private let defaults: UserDefaults
    private enum Key {
        static let presets = "com.roomcut.savedPresets"
        static let saved = "com.roomcut.activeSavedPreset"
        static let builtin = "com.roomcut.activeBuiltinPreset"
        static let autoApply = "com.roomcut.deviceAutoPreset"
        static let deviceMap = "com.roomcut.devicePresetMap"
    }

    init(defaults: UserDefaults) {
        self.defaults = defaults
        if let data = defaults.data(forKey: Key.presets), let saved = try? JSONDecoder().decode([SavedPreset].self, from: data) {
            presets = saved
        }
        autoApply = defaults.bool(forKey: Key.autoApply)
        deviceMap = defaults.dictionary(forKey: Key.deviceMap) as? [String: String] ?? [:]
    }

    var activeSavedName: String? { defaults.string(forKey: Key.saved) }
    var activeBuiltinID: String? { defaults.string(forKey: Key.builtin) }

    func contains(_ name: String) -> Bool {
        presets.contains { $0.name.caseInsensitiveCompare(name.trimmingCharacters(in: .whitespacesAndNewlines)) == .orderedSame }
    }

    func save(_ preset: SavedPreset) {
        merge(preset)
        persist()
    }

    func delete(_ preset: SavedPreset) {
        guard !preset.builtin else { return }
        presets.removeAll { $0.id == preset.id }
        deviceMap = deviceMap.filter {
            !$0.value.hasPrefix("saved:") || String($0.value.dropFirst(6)).caseInsensitiveCompare(preset.name) != .orderedSame
        }
        defaults.set(deviceMap, forKey: Key.deviceMap)
        persist()
    }

    func recordSelection(saved: String?, builtin: String?) {
        defaults.set(saved, forKey: Key.saved)
        defaults.set(builtin, forKey: Key.builtin)
    }

    func setAutoApply(_ on: Bool) {
        autoApply = on
        defaults.set(on, forKey: Key.autoApply)
    }

    func remember(_ token: String?, device: String) {
        guard autoApply, !device.isEmpty, let token, deviceMap[device] != token else { return }
        deviceMap[device] = token
        defaults.set(deviceMap, forKey: Key.deviceMap)
    }

    func exportData() -> Data? {
        let userPresets = presets.filter { !$0.builtin }
        guard !userPresets.isEmpty else { return nil }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try? encoder.encode(PresetArchive(version: 1, presets: userPresets))
    }

    func importData(_ data: Data) -> Int? {
        let decoder = JSONDecoder()
        let incoming: [SavedPreset]
        if let archive = try? decoder.decode(PresetArchive.self, from: data) {
            guard archive.version == 1 else { return nil }
            incoming = archive.presets
        } else if let bare = try? decoder.decode([SavedPreset].self, from: data) { incoming = bare }
        else { return nil }
        var count = 0
        for item in incoming {
            let name = item.name.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !name.isEmpty else { continue }
            let normalized = SoundSnapshot(preset: item).preset(name: name, folder: item.folder, roomTuneInfo: item.roomTuneInfo)
            merge(normalized)
            count += 1
        }
        if count > 0 { persist() }
        return count
    }

    private func merge(_ preset: SavedPreset) {
        if let index = presets.firstIndex(where: { !$0.builtin && $0.name.caseInsensitiveCompare(preset.name) == .orderedSame }) {
            presets[index] = preset
        } else { presets.append(preset) }
    }

    private func persist() {
        if let data = try? JSONEncoder().encode(presets) { defaults.set(data, forKey: Key.presets) }
    }
}
