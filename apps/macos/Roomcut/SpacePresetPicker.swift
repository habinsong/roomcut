import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The Space preset selector: the same capsule as Home's EQ Preset, opening a
// list of whole-scene presets for the current output (SpacePresetLibrary).
struct SpacePresetPicker: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var showList = false

    private var title: String { L("Space Preset", "Space Preset", "空間プリセット", "Préréglage d’espace", "Raum-Preset") }
    private var currentName: String {
        model.activeSpacePreset?.name ?? L("Custom", "Custom", "カスタム", "Personnalisé", "Benutzerdef.")
    }

    var body: some View {
        HStack(spacing: 8) {
            Button { showList = true } label: {
                HStack(spacing: 6) {
                    Text(title)
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                        .lineLimit(1)
                        .fixedSize(horizontal: true, vertical: false)
                    Spacer(minLength: 6)
                    Text(currentName)
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                        .lineLimit(1)
                        .minimumScaleFactor(0.72)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .popover(isPresented: $showList, arrowEdge: .bottom) {
                SpacePresetList(model: model)
            }
            .accessibilityLabel(title)
            .accessibilityValue(currentName)
            .frame(maxWidth: .infinity)

            Button { model.applySpacePreset(SpacePresetLibrary.reference(headphone: model.spatialOutputIsHeadphone)) } label: {
                Image(systemName: "arrow.uturn.left")
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                    .frame(width: 24, height: 24)
                    .contentShape(Circle())
            }
            .buttonStyle(.plain)
            .help(L("Reference로 초기화", "Reset to Reference", "リファレンスにリセット",
                    "Réinitialiser à Référence", "Auf Referenz zurücksetzen"))
            .accessibilityLabel(L("Space 초기화", "Reset Space", "空間をリセット", "Réinitialiser l’espace", "Raum zurücksetzen"))
        }
        .padding(.horizontal, 9)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity)
        // `.regular`, like the segmented controls on the same card: `.clear`
        // suits EQ Preset's place on the sheet glass, but on the card material
        // it turned into a pale pill the grey label could not be read against.
        .glassEffect(.regular, in: Capsule())
        .clipShape(Capsule())
    }
}

private struct SpacePresetList: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        let headphone = model.spatialOutputIsHeadphone
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: headphone ? "headphones" : "hifispeaker.2").font(.system(size: 12, weight: .semibold))
                Text(headphone ? L("Headphone Presets", "Headphone Presets", "ヘッドフォン プリセット",
                                   "Préréglages casque", "Kopfhörer-Presets")
                               : L("Speaker Presets", "Speaker Presets", "スピーカー プリセット",
                                   "Préréglages haut-parleurs", "Lautsprecher-Presets"))
                    .font(.system(size: 13, weight: .semibold))
                Spacer()
            }
            .padding(.horizontal, 14).padding(.vertical, 11)

            ScrollView {
                VStack(spacing: 8) {
                    ForEach(SpacePreset.Group.allCases, id: \.self) { group in
                        let items = model.spacePresets.filter { $0.group == group }
                        if !items.isEmpty { section(group, items) }
                    }
                }
                .padding(.horizontal, 10).padding(.bottom, 12)
            }
        }
        .frame(width: 320, height: 470)
    }

    private func groupTitle(_ group: SpacePreset.Group) -> String {
        switch group {
        case .everyday: return L("Everyday", "Everyday", "日常", "Quotidien", "Alltag")
        case .music:    return L("Music", "Music", "音楽", "Musique", "Musik")
        case .cinema:   return L("Movies & Games", "Movies & Games", "映画・ゲーム", "Films et jeux", "Filme & Spiele")
        }
    }

    private func groupIcon(_ group: SpacePreset.Group) -> String {
        switch group {
        case .everyday: return "person.wave.2"
        case .music:    return "music.note"
        case .cinema:   return "film"
        }
    }

    private func section(_ group: SpacePreset.Group, _ items: [SpacePreset]) -> some View {
        VStack(spacing: 2) {
            HStack(spacing: 9) {
                Image(systemName: groupIcon(group))
                    .font(.system(size: 12)).frame(width: 18)
                    .foregroundStyle(RoomcutTokens.blue(scheme))
                Text(groupTitle(group)).font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                Spacer()
            }
            .padding(.horizontal, 12).padding(.top, 10).padding(.bottom, 4)

            ForEach(items) { row($0) }
        }
        .padding(.bottom, 6)
        .glassEffect(.regular, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
    }

    private func row(_ preset: SpacePreset) -> some View {
        let active = model.activeSpacePreset == preset
        return HStack(spacing: 9) {
            Image(systemName: active ? "checkmark.circle.fill" : "circle")
                .font(.system(size: 12))
                .foregroundStyle(active ? RoomcutTokens.blue(scheme) : RoomcutTokens.textTertiary(scheme).opacity(0.5))
                .frame(width: 18)
            Text(preset.name)
                .font(.system(size: 12, weight: active ? .semibold : .regular))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            Spacer(minLength: 8)
            Text(summary(preset))
                .font(.system(size: 11))
                .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                .lineLimit(1)
        }
        .padding(.horizontal, 14).padding(.vertical, 7)
        .background(active ? RoomcutTokens.blue(scheme).opacity(0.12) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 9, style: .continuous))
        .contentShape(Rectangle())
        .onTapGesture { model.applySpacePreset(preset) }
        .padding(.horizontal, 6)
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(active ? [.isButton, .isSelected] : .isButton)
    }

    // What the preset turns on, in the controls' own words.
    private func summary(_ preset: SpacePreset) -> String {
        var parts: [String] = []
        switch preset.surround {
        case .off: break
        case .ambience: parts.append("Ambience")
        case .virtual51: parts.append(preset.headphone ? "5.1" : "Wide")
        case .virtual71: parts.append("7.1")
        }
        let rooms = ["", "studio", "live", "hall"]
        let room = Int(preset.roomType.rounded())
        if room >= 1, room < rooms.count { parts.append(rooms[room]) }
        switch preset.stage {
        case .off: break
        case .focus: parts.append(L("Focus", "Focus", "フォーカス", "Focus", "Fokus"))
        case .widen: parts.append(L("Widen", "Widen", "ワイド", "Élargir", "Verbreitern"))
        }
        return parts.isEmpty ? L("All off", "All off", "すべてオフ", "Tout désactivé", "Alles aus")
                             : parts.joined(separator: " · ")
    }
}
