import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The preset library popover: built-in presets grouped by folder, then the
// listener's own saved curves.
// MARK: - Preset tree (folder-grouped library + My Presets), Liquid Glass popover

struct PresetTreeView: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var expanded: Set<String> = ["Signature"]

    private var activeName: String? { model.activeSavedPreset }

    // "Engine" surfaces the engine's own built-in presets (e.g. Dialogue, whose
    // internal compressor the app library can't reproduce); everything else is the
    // app library / user presets.
    private var folderOrder: [String] { ["Engine"] + PresetLibrary.folderOrder }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "slider.horizontal.3").font(.system(size: 12, weight: .semibold))
                Text(L("EQ Presets", "EQ Presets", "EQ プリセット", "Préréglages EQ", "EQ-Presets"))
                    .font(.system(size: 13, weight: .semibold))
                Spacer()
                Text(model.currentPresetName)
                    .font(.system(size: 11, weight: .medium)).foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .padding(.horizontal, 14).padding(.vertical, 11)

            ScrollView {
                VStack(spacing: 8) {
                    ForEach(folderOrder, id: \.self) { folder in
                        folderSection(folder)
                    }
                }
                .padding(.horizontal, 10).padding(.bottom, 12)
            }
        }
        .frame(width: 320, height: 460)
    }

    private func presets(in folder: String) -> [SavedPreset] {
        folder == "My Presets" ? model.savedPresets : PresetLibrary.all.filter { $0.folder == folder }
    }

    private func count(in folder: String) -> Int {
        folder == "Engine" ? model.presets.count : presets(in: folder).count
    }

    // Display name only — `folder` itself stays the identity key used for grouping.
    // Brand/proper folders (Engine/Signature/Apple) keep their English names.
    private func folderTitle(_ folder: String) -> String {
        switch folder {
        case "Speakers":   return L("Speakers", "Speakers", "スピーカー", "Haut-parleurs", "Lautsprecher")
        case "Headphones": return L("Headphones", "Headphones", "ヘッドフォン", "Casques", "Kopfhörer")
        case "My Presets": return L("My Presets", "My Presets", "マイプリセット", "Mes préréglages", "Meine Presets")
        default:           return folder
        }
    }

    private func folderIcon(_ folder: String) -> String {
        switch folder {
        case "Engine":     return "cpu"
        case "Signature":  return "waveform"
        case "Apple":      return "apple.logo"
        case "Speakers":   return "hifispeaker"
        case "Headphones": return "headphones"
        default:           return "star"
        }
    }

    @ViewBuilder
    private func folderSection(_ folder: String) -> some View {
        let open = expanded.contains(folder)
        VStack(spacing: 0) {
            Button {
                withAnimation(.snappy(duration: 0.2)) {
                    if open { expanded.remove(folder) } else { expanded.insert(folder) }
                }
            } label: {
                HStack(spacing: 9) {
                    Image(systemName: folderIcon(folder))
                        .font(.system(size: 12)).frame(width: 18)
                        .foregroundStyle(RoomcutTokens.blue(scheme))
                    Text(folderTitle(folder)).font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    Spacer()
                    Text("\(count(in: folder))").font(.system(size: 11))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                    Image(systemName: open ? "chevron.down" : "chevron.right")
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)

            if open {
                VStack(spacing: 2) {
                    if folder == "Engine" {
                        ForEach(model.presets) { engineRow($0) }
                    } else {
                        ForEach(presets(in: folder)) { presetRow($0, folder: folder) }
                        if presets(in: folder).isEmpty && folder == "My Presets" {
                            Text(L("EQ Preset 아래 저장으로 추가됩니다.", "Added via Save under EQ Preset.",
                                   "EQ プリセットの下の保存で追加されます。", "Ajouté via Enregistrer sous Préréglage EQ.",
                                   "Über Speichern unter EQ-Preset hinzugefügt."))
                                .font(.system(size: 11)).foregroundStyle(RoomcutTokens.textTertiary(scheme))
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding(.horizontal, 14).padding(.vertical, 6)
                        }
                    }
                }
                .padding(.bottom, 6)
            }
        }
        .glassEffect(.regular, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private func engineRow(_ preset: EnginePreset) -> some View {
        let active = model.presetPickerSelection == preset.id
        return HStack(spacing: 9) {
            Image(systemName: active ? "checkmark.circle.fill" : "circle")
                .font(.system(size: 12))
                .foregroundStyle(active ? RoomcutTokens.blue(scheme) : RoomcutTokens.textTertiary(scheme).opacity(0.5))
                .frame(width: 18)
            Text(preset.name)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer()
        }
        .padding(.horizontal, 14).padding(.vertical, 7)
        .background(active ? RoomcutTokens.blue(scheme).opacity(0.12) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .contentShape(Rectangle())
        .onTapGesture { model.applyPickerSelection(preset.id) }
        .padding(.horizontal, 6)
    }

    private func presetRow(_ preset: SavedPreset, folder: String) -> some View {
        let active = preset.builtin
            ? model.presetPickerSelection == PresetLibrary.token(for: preset)
            : preset.name == activeName
        return HStack(spacing: 9) {
            Image(systemName: active ? "checkmark.circle.fill" : "circle")
                .font(.system(size: 12))
                .foregroundStyle(active ? RoomcutTokens.blue(scheme) : RoomcutTokens.textTertiary(scheme).opacity(0.5))
                .frame(width: 18)
            Text(preset.name)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer()
            if folder == "My Presets" {
                Button { model.deleteSavedPreset(preset) } label: {
                    Image(systemName: "trash").font(.system(size: 10))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 14).padding(.vertical, 7)
        .background(active ? RoomcutTokens.blue(scheme).opacity(0.12) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .contentShape(Rectangle())
        .onTapGesture { model.applySavedPreset(preset) }
        .padding(.horizontal, 6)
    }
}
