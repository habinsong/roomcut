import Foundation
import Combine

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

@MainActor
final class AppPreferences: ObservableObject {
    @Published private(set) var theme: RoomcutNowPlayingTheme
    @Published private(set) var layout: RoomcutNowPlayingLayout
    @Published private(set) var appearance: RoomcutAppearance
    @Published private(set) var themeSync: Bool
    @Published private(set) var language: AppLanguage
    private let defaults: UserDefaults
    private enum Key {
        static let theme = "com.roomcut.nowPlayingTheme"
        static let layout = "com.roomcut.nowPlayingLayout"
        static let appearance = "com.roomcut.appearance"
        static let themeSync = "com.roomcut.themeSync"
        static let language = "com.roomcut.language"
        static let speakerSpacePreset = "com.roomcut.space.preset.speaker"
        static let headphoneSpacePreset = "com.roomcut.space.preset.headphone"
    }

    init(defaults: UserDefaults) {
        self.defaults = defaults
        theme = defaults.string(forKey: Key.theme).flatMap(RoomcutNowPlayingTheme.init(rawValue:)) ?? .cover
        layout = defaults.string(forKey: Key.layout).flatMap(RoomcutNowPlayingLayout.init(rawValue:)) ?? .b
        appearance = defaults.string(forKey: Key.appearance).flatMap(RoomcutAppearance.init(rawValue:)) ?? .system
        themeSync = defaults.bool(forKey: Key.themeSync)
        language = defaults.string(forKey: Key.language).flatMap(AppLanguage.init(rawValue:)) ?? .auto
        AppLanguage.preference = language
    }

    func setTheme(_ value: RoomcutNowPlayingTheme) { theme = value; defaults.set(value.rawValue, forKey: Key.theme) }
    func setLayout(_ value: RoomcutNowPlayingLayout) { layout = value; defaults.set(value.rawValue, forKey: Key.layout) }
    func setAppearance(_ value: RoomcutAppearance) { appearance = value; defaults.set(value.rawValue, forKey: Key.appearance) }
    func setThemeSync(_ value: Bool) { themeSync = value; defaults.set(value, forKey: Key.themeSync) }
    // The Space preset each output was last left on; nil once it was adjusted.
    func spacePresetID(headphone: Bool) -> String? {
        defaults.string(forKey: headphone ? Key.headphoneSpacePreset : Key.speakerSpacePreset)
    }
    func setSpacePresetID(_ id: String?, headphone: Bool) {
        defaults.set(id, forKey: headphone ? Key.headphoneSpacePreset : Key.speakerSpacePreset)
    }
    func setLanguage(_ value: AppLanguage) {
        language = value
        AppLanguage.preference = value
        defaults.set(value.rawValue, forKey: Key.language)
    }
}
