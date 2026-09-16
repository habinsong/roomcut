import SwiftUI
import ServiceManagement
import UniformTypeIdentifiers
import RoomcutCore
import RoomcutPresentationCore

// The individual pickers inside Settings: theme, layout, appearance, language,
// and the two small controls they share.
extension SettingsTab {
    var themeSelector: some View {
        segmentedPicker(RoomcutNowPlayingTheme.allCases,
                        selected: model.nowPlayingTheme,
                        title: { $0.title },
                        select: { model.setNowPlayingTheme($0) })
    }

    // Same glass segmented control for the Now Playing layout variant (Card / Poster).
    var layoutSelector: some View {
        segmentedPicker(RoomcutNowPlayingLayout.allCases,
                        selected: model.nowPlayingLayout,
                        title: { layoutTitle($0) },
                        select: { model.setNowPlayingLayout($0) })
    }

    // Card (single-card A) / Poster (split B). English & Korean keep the English
    // words; ja/fr/de are translated.
    func layoutTitle(_ l: RoomcutNowPlayingLayout) -> String {
        switch l {
        case .b: return L("Poster", "Poster", "ポスター", "Affiche", "Poster")
        case .a: return L("Card", "Card", "カード", "Carte", "Karte")
        }
    }

    // Same glass segmented control for the app appearance (Auto / Light / Dark).
    var appearanceSelector: some View {
        segmentedPicker(RoomcutAppearance.allCases,
                        selected: model.appearance,
                        title: { appearanceTitle($0) },
                        select: { model.setAppearance($0) })
    }

    func appearanceTitle(_ a: RoomcutAppearance) -> String {
        switch a {
        case .system: return L("Auto", "Auto", "自動", "Auto", "Auto")
        case .light:  return L("Light", "Light", "ライト", "Clair", "Hell")
        case .dark:   return L("Dark", "Dark", "ダーク", "Sombre", "Dunkel")
        }
    }

    // Language menu, using the exact Liquid-Glass capsule recipe as the output-device
    // picker. `auto` follows the macOS system language; the rest force a language.
    var languageSelector: some View {
        RoomcutRow(L("언어", "Language", "言語", "Langue", "Sprache"), systemImage: "globe") {
            glassMenu(title: model.language.displayName, maxTitleWidth: 184, disabled: false) {
                ForEach(AppLanguage.allCases) { lang in
                    Button { model.setLanguage(lang) } label: {
                        checkmarkLabel(lang.displayName, on: lang == model.language)
                    }
                }
            }
        }
    }

    // Liquid-Glass segmented control shared by the theme and layout pickers — a
    // glass capsule with a soft selection pill behind the active segment.
    func segmentedPicker<T: Identifiable & Equatable>(
        _ cases: [T],
        selected: T,
        title: @escaping (T) -> String,
        select: @escaping (T) -> Void
    ) -> some View {
        GlassEffectContainer(spacing: 5) {
            HStack(spacing: 4) {
                ForEach(cases) { item in
                    let isSelected = selected == item
                    Button { select(item) } label: {
                        Text(title(item))
                            .font(.system(size: 12, weight: isSelected ? .semibold : .regular))
                            .foregroundStyle(isSelected ? RoomcutTokens.textPrimary(scheme)
                                             : RoomcutTokens.textSecondary(scheme))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 8)
                            .background {
                                if isSelected { Capsule().fill(RoomcutTokens.blue(scheme).opacity(0.22)) }
                            }
                            .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(4)
        }
        .glassEffect(.regular, in: Capsule())
        .clipShape(Capsule())          // trim the glass drop shadow — sit flat on the card
        .padding(.horizontal, 12).padding(.vertical, 6)
    }

    // Native menu row: a checkmark on the current selection (like a system pull-down).
    @ViewBuilder
    func checkmarkLabel(_ title: String, on selected: Bool) -> some View {
        if selected { Label(title, systemImage: "checkmark") } else { Text(title) }
    }

    func settingsSwitch(_ binding: Binding<Bool>) -> some View {
        Toggle("", isOn: binding)
            .labelsHidden()
            .toggleStyle(.switch)
            .controlSize(.small)
            .tint(RoomcutTokens.blue(scheme))
    }
}
