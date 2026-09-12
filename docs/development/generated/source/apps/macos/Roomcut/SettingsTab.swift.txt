//
// SettingsTab.swift — output device, volume, behavior, theme, and app actions.
//
// Liquid-Glass redesign: a transparent scroll over the Now Playing wash with
// `.regularMaterial` content cards (RoomcutSection/Row), native controls inside.
//
import SwiftUI
import ServiceManagement
import UniformTypeIdentifiers
import RoomcutCore
import RoomcutPresentationCore

struct SettingsTab: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @State private var launchAtLogin = SMAppService.mainApp.status == .enabled
    @State private var lyricsCacheCount = 0
    @State private var lyricsCacheCleared = false
    @State private var presetTransferNote: String?
    @State private var maintenanceBusy = false
    @State private var driverReinstalled = false
    private let lyricsCachePath = "~/Library/Caches/com.habinsong.roomcut/lyrics.json"

    private var enabledBinding: Binding<Bool> {
        Binding(
            get: { RoomcutMainPresentation.roomcutEnabled(manualBypass: model.status.manualBypass) },
            set: {
                let bypass = RoomcutMainPresentation.manualBypass(forEnabled: $0)
                AppLaunch.traceRoomTune("user request bypass=\(bypass)")
                model.setBypass(bypass)
            })
    }

    var body: some View {
        // Tighter than the other tabs (smaller section gaps + denser rows) so the
        // whole list — including Quit — fits above the tab bar. Appearance, output
        // and behaviour share ONE card; full-width rules mark the group breaks that
        // used to be separate titled sections.
        RoomcutTabScreen(spacing: 10, bottomPadding: 80) {
            RoomcutSection("") {
                appearanceGroup
                groupRule
                outputGroup
                groupRule
                behaviorGroup
            }

            RoomcutSection("") {
                RoomcutRow(L("프리셋", "Presets", "プリセット", "Préréglages", "Presets"),
                           systemImage: "slider.horizontal.3") {
                    HStack(spacing: 8) {
                        if let note = presetTransferNote {
                            Text(note)
                                .font(.system(size: 11, weight: .medium))
                                .foregroundStyle(.secondary)
                        }
                        glassActionButton(systemImage: "square.and.arrow.up",
                                          accessibilityLabel: L("프리셋 내보내기", "Export Presets", "プリセットを書き出す",
                                                                "Exporter les préréglages", "Presets exportieren"),
                                          disabled: model.savedPresets.isEmpty) { exportPresets() }
                            .help(L("프리셋 내보내기", "Export Presets", "プリセットを書き出す",
                                    "Exporter les préréglages", "Presets exportieren"))
                        glassActionButton(systemImage: "square.and.arrow.down",
                                          accessibilityLabel: L("프리셋 가져오기", "Import Presets", "プリセットを読み込む",
                                                                "Importer des préréglages", "Presets importieren"),
                                          disabled: false) { importPresets() }
                            .help(L("프리셋 가져오기", "Import Presets", "プリセットを読み込む",
                                    "Importer des préréglages", "Presets importieren"))
                    }
                }
            }

            RoomcutSection("") {
                lyricsCacheControl
            }

            Button(role: .destructive) { NSApp.terminate(nil) } label: {
                RoomcutCard {
                    HStack {
                        Spacer()
                        Label(L("Roomcut 종료", "Quit Roomcut", "Roomcut を終了",
                                "Quitter Roomcut", "Roomcut beenden"), systemImage: "power")
                            .font(.system(size: 13, weight: .medium))
                            .foregroundStyle(RoomcutTokens.red)
                        Spacer()
                    }
                    .padding(.vertical, 12)
                }
            }
            .buttonStyle(.plain)

            maintenanceCard
        }
        .environment(\.roomcutRowVPadding, 8)
    }

    // The merged card's three groups live in their own properties: each stays
    // inside ViewBuilder's 10-child limit, and `Group` flattens into the card's
    // stack so the rows still sit flush against each other.
    private var appearanceGroup: some View {
        Group {
            appearanceSelector
            themeSelector
            layoutSelector
            languageSelector
            RoomcutRow(L("축소 모드에도 테마 적용", "Apply Theme in Compact Mode", "コンパクト表示でもテーマを適用",
                         "Appliquer le thème en mode compact", "Thema auch im Kompaktmodus"),
                       systemImage: "rectangle.on.rectangle") {
                settingsSwitch(Binding(get: { model.themeSyncEnabled },
                                       set: { model.setThemeSync($0) }))
            }
        }
    }

    private var outputGroup: some View {
        Group {
            RoomcutRow(L("출력 장치", "Output Device", "出力デバイス", "Périphérique de sortie", "Ausgabegerät"),
                       systemImage: "hifispeaker") {
                glassMenu(title: selectedDeviceName, maxTitleWidth: 184,
                          disabled: !model.status.reachable) {
                    ForEach(model.outputDevices) { device in
                        Button { model.selectDevice(device.uid) } label: {
                            checkmarkLabel(device.name, on: device.uid == model.selectedDeviceUID)
                        }
                    }
                }
            }
            if !model.availableSampleRates.isEmpty || !model.availableBitDepths.isEmpty {
                RoomcutRow(L("포맷", "Format", "フォーマット", "Format", "Format"),
                           systemImage: "dot.radiowaves.left.and.right") {
                    HStack(spacing: 8) {
                        if !model.availableBitDepths.isEmpty {
                            glassMenu(title: bitDepthLabel(model.audioFormat?.bitDepth),
                                      disabled: !model.status.reachable) {
                                ForEach(model.availableBitDepths, id: \.self) { bits in
                                    Button { model.selectBitDepth(bits) } label: {
                                        checkmarkLabel(bitDepthLabel(bits),
                                                       on: bits == model.audioFormat?.bitDepth)
                                    }
                                }
                            }
                        }
                        if !model.availableSampleRates.isEmpty {
                            glassMenu(title: rateLabel(model.audioFormat?.sampleRate),
                                      disabled: !model.status.reachable) {
                                ForEach(model.availableSampleRates, id: \.self) { sr in
                                    Button { model.selectSampleRate(sr) } label: {
                                        checkmarkLabel(rateLabel(sr),
                                                       on: sr == model.audioFormat?.sampleRate)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if model.hasVolumeControl {
                RoomcutRow(L("볼륨", "Volume", "音量", "Volume", "Lautstärke"),
                           systemImage: "speaker.wave.2") {
                    HStack(spacing: 8) {
                        Slider(value: Binding(get: { model.volume }, set: { model.setVolume($0) }),
                               in: 0...RoomcutViewModel.maxVolume,
                               onEditingChanged: { $0 ? model.beginVolumeEdit() : model.endVolumeEdit() })
                            .tint(RoomcutTokens.blue(scheme))
                            .frame(width: 130)
                        Text("\(Int((model.volume * 100).rounded()))%")
                            .font(.system(size: 12, weight: .medium).monospacedDigit())
                            .foregroundStyle(.secondary).frame(width: 38, alignment: .trailing)
                    }
                }
            }
        }
    }

    private var behaviorGroup: some View {
        Group {
            RoomcutRow(L("Roomcut 처리 켜기", "Enable Roomcut", "Roomcut 処理を有効化",
                         "Activer Roomcut", "Roomcut aktivieren"), systemImage: "power") {
                settingsSwitch(enabledBinding)
                    .disabled(!model.status.reachable)
            }
            RoomcutRow(L("기본 출력으로 유지", "Keep as Default Output", "デフォルト出力に保持",
                         "Conserver comme sortie par défaut", "Als Standardausgabe behalten"),
                       systemImage: "pin") {
                settingsSwitch(Binding(get: { model.keepDefault },
                                       set: { model.setKeepDefault($0) }))
                    .disabled(!model.status.reachable)
            }
            RoomcutRow(L("기기별 프리셋 기억", "Per-Device Presets", "デバイス別プリセット",
                         "Préréglages par appareil", "Presets pro Gerät"),
                       systemImage: "arrow.triangle.2.circlepath") {
                settingsSwitch(Binding(get: { model.deviceAutoPresetEnabled },
                                       set: { model.setDeviceAutoPreset($0) }))
            }
            RoomcutRow(L("로그인 시 자동 실행", "Launch at Login", "ログイン時に起動",
                         "Lancer à la connexion", "Beim Anmelden starten"),
                       systemImage: "arrow.right.circle") {
                settingsSwitch(Binding(get: { launchAtLogin }, set: { setLaunchAtLogin($0) }))
            }
        }
    }

    // A group break inside the merged card: full width, unlike RoomcutDivider's
    // inset row separator, so it reads as "these used to be separate cards".
    private var groupRule: some View { Divider().opacity(0.4) }

    // Native Liquid-Glass segmented theme selector (glass capsule + a morphing
    // selection pill), instead of the boxy menu dropdown.
    private var themeSelector: some View {
        segmentedPicker(RoomcutNowPlayingTheme.allCases,
                        selected: model.nowPlayingTheme,
                        title: { $0.title },
                        select: { model.setNowPlayingTheme($0) })
    }

    // Same glass segmented control for the Now Playing layout variant (Card / Poster).
    private var layoutSelector: some View {
        segmentedPicker(RoomcutNowPlayingLayout.allCases,
                        selected: model.nowPlayingLayout,
                        title: { layoutTitle($0) },
                        select: { model.setNowPlayingLayout($0) })
    }

    // Card (single-card A) / Poster (split B). English & Korean keep the English
    // words; ja/fr/de are translated.
    private func layoutTitle(_ l: RoomcutNowPlayingLayout) -> String {
        switch l {
        case .b: return L("Poster", "Poster", "ポスター", "Affiche", "Poster")
        case .a: return L("Card", "Card", "カード", "Carte", "Karte")
        }
    }

    // Same glass segmented control for the app appearance (Auto / Light / Dark).
    private var appearanceSelector: some View {
        segmentedPicker(RoomcutAppearance.allCases,
                        selected: model.appearance,
                        title: { appearanceTitle($0) },
                        select: { model.setAppearance($0) })
    }

    private func appearanceTitle(_ a: RoomcutAppearance) -> String {
        switch a {
        case .system: return L("Auto", "Auto", "自動", "Auto", "Auto")
        case .light:  return L("Light", "Light", "ライト", "Clair", "Hell")
        case .dark:   return L("Dark", "Dark", "ダーク", "Sombre", "Dunkel")
        }
    }

    // Language menu, using the exact Liquid-Glass capsule recipe as the output-device
    // picker. `auto` follows the macOS system language; the rest force a language.
    private var languageSelector: some View {
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
    private func segmentedPicker<T: Identifiable & Equatable>(
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
    private func checkmarkLabel(_ title: String, on selected: Bool) -> some View {
        if selected { Label(title, systemImage: "checkmark") } else { Text(title) }
    }

    private func settingsSwitch(_ binding: Binding<Bool>) -> some View {
        Toggle("", isOn: binding)
            .labelsHidden()
            .toggleStyle(.switch)
            .controlSize(.small)
            .tint(RoomcutTokens.blue(scheme))
    }

    private var selectedDeviceName: String {
        model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name ?? "—"
    }

    // Same Liquid-Glass recipe as EqPresetPicker: one `.glassEffect(.regular, in:
    // Capsule())` capsule (NOT `.interactive` — that lenses into a blobby pill),
    // value in semibold + a small `chevron.up.chevron.down`. A plain button menu so
    // the glass is the only surface. Sizes to content; `maxTitleWidth` only caps a
    // long device name so it truncates instead of stretching the row.
    private func glassMenu<Items: View>(title: String,
                                        maxTitleWidth: CGFloat? = nil,
                                        disabled: Bool,
                                        @ViewBuilder items: () -> Items) -> some View {
        Menu {
            items()
        } label: {
            HStack(spacing: 6) {
                Text(title)
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundStyle(disabled ? RoomcutTokens.textTertiary(scheme)
                                     : RoomcutTokens.textPrimary(scheme))
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .frame(maxWidth: maxTitleWidth)
                Image(systemName: "chevron.up.chevron.down")
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(RoomcutTokens.textTertiary(scheme))
            }
            .padding(.horizontal, 11)
            .padding(.vertical, 7)
            .contentShape(Capsule())
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .menuIndicator(.hidden)
        .fixedSize()
        // A flat fill instead of glass: no drop shadow, and one layer (not stacked on
        // the card's material) so it reads less white.
        .background(Capsule().fill(.quaternary))
        .clipShape(Capsule())
        .disabled(disabled)
    }

    // Small icon-only flat-capsule action button, the same recipe as glassMenu's
    // label so the preset export/import rows match the pickers around them.
    private func glassActionButton(systemImage: String, accessibilityLabel: String,
                                   disabled: Bool,
                                   action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: systemImage)
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(disabled ? RoomcutTokens.textTertiary(scheme)
                                 : RoomcutTokens.textPrimary(scheme))
                .padding(.horizontal, 11)
                .padding(.vertical, 7)
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .fixedSize()
        .background(Capsule().fill(.quaternary))
        .clipShape(Capsule())
        .disabled(disabled)
        .accessibilityLabel(accessibilityLabel)
    }

    private func exportPresets() {
        guard let data = model.exportPresetsData() else { return }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "Roomcut Presets.json"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try data.write(to: url)
            showPresetTransferNote(L("내보내기 완료", "Exported", "書き出し完了", "Exporté", "Exportiert"))
        } catch {
            showPresetTransferNote(L("내보내기 실패", "Export failed", "書き出し失敗",
                                     "Échec de l'export", "Export fehlgeschlagen"))
        }
    }

    private func importPresets() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.json]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url,
              let data = try? Data(contentsOf: url) else { return }
        if let count = model.importPresets(from: data) {
            showPresetTransferNote(presetCountLabel(count))
        } else {
            showPresetTransferNote(L("파일을 읽을 수 없음", "Unreadable file", "読み込めないファイル",
                                     "Fichier illisible", "Datei nicht lesbar"))
        }
    }

    private func showPresetTransferNote(_ note: String) {
        withAnimation { presetTransferNote = note }
        Task {
            try? await Task.sleep(nanoseconds: 2_200_000_000)
            withAnimation { presetTransferNote = nil }
        }
    }

    private func presetCountLabel(_ n: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "\(n)개 가져옴"
        case .japanese: return "\(n) 件を読み込み"
        case .french:   return "\(n) importés"
        case .german:   return "\(n) importiert"
        default:        return "\(n) imported"
        }
    }

    // Native Liquid-Glass action button that clears the on-disk synced-lyrics cache.
    private var lyricsCacheControl: some View {
        GlassEffectContainer(spacing: 5) {
            Button { clearLyricsCache() } label: {
                HStack(spacing: 10) {
                    Image(systemName: lyricsCacheCleared ? "checkmark.circle.fill" : "trash")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(lyricsCacheCleared ? RoomcutTokens.green : RoomcutTokens.amber)
                    Text(lyricsCacheCleared
                         ? L("초기화 완료", "Cleared", "クリア完了", "Effacé", "Geleert")
                         : L("가사 캐시 초기화", "Clear Lyrics Cache", "歌詞キャッシュを消去",
                             "Vider le cache des paroles", "Lyrik-Cache leeren"))
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    Spacer(minLength: 8)
                    Text(lyricsCacheCount > 0 ? songCountLabel(lyricsCacheCount)
                         : L("비어 있음", "Empty", "空", "Vide", "Leer"))
                        .font(.system(size: 12, weight: .medium).monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity)
                .padding(.horizontal, 14).padding(.vertical, 11)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(lyricsCacheCount == 0)
        }
        .glassEffect(.clear, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
        .padding(.horizontal, 12).padding(.vertical, 6)
        .help(lyricsCachePath)
        .task { lyricsCacheCount = await LRCLIBClient.cachedTrackCount() }
    }

    private func clearLyricsCache() {
        Task {
            await LRCLIBClient.clearCache()
            lyricsCacheCount = 0
            withAnimation { lyricsCacheCleared = true }
            try? await Task.sleep(nanoseconds: 1_400_000_000)
            withAnimation { lyricsCacheCleared = false }
        }
    }

    private func songCountLabel(_ n: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "\(n)곡"
        case .japanese: return "\(n) 曲"
        case .french:   return "\(n) titres"
        case .german:   return "\(n) Titel"
        default:        return "\(n) songs"
        }
    }

    private func rateLabel(_ sr: Double?) -> String {
        guard let sr else { return "—" }
        let khz = sr / 1000
        return khz == khz.rounded() ? "\(Int(khz)) kHz" : String(format: "%.1f kHz", khz)
    }

    private func bitDepthLabel(_ bits: Int?) -> String {
        bits.map { "\($0)-bit" } ?? "—"
    }

    // Driver maintenance, in the same card shape as Quit but split down the middle:
    // reinstall on the left, full removal on the right, one hairline between them.
    // Both halves need root, so each runs a single privileged shell command.
    private var maintenanceCard: some View {
        RoomcutCard {
            HStack(spacing: 0) {
                maintenanceButton(
                    systemImage: driverReinstalled ? "checkmark.circle.fill" : "arrow.counterclockwise",
                    title: driverReinstalled
                        ? L("재설치 완료", "Reinstalled", "再インストール完了", "Réinstallé", "Neu installiert")
                        : L("드라이버 재설치", "Reinstall Driver", "ドライバ再インストール",
                            "Réinstaller", "Neu installieren"),
                    tint: driverReinstalled ? RoomcutTokens.green : RoomcutTokens.blue(scheme),
                    action: reinstallDriver)
                Divider().frame(height: 26).opacity(0.5)
                maintenanceButton(
                    systemImage: "trash",
                    title: L("Roomcut 제거", "Remove Roomcut", "Roomcut を削除",
                             "Supprimer Roomcut", "Roomcut entfernen"),
                    tint: RoomcutTokens.red,
                    action: removeRoomcut)
            }
        }
        .disabled(maintenanceBusy)
    }

    // Half-width button: the padding lives inside so each half is tappable across
    // the whole row height. `minimumScaleFactor` keeps the longest translations on
    // one line instead of truncating them.
    private func maintenanceButton(systemImage: String, title: String, tint: Color,
                                   action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .font(.system(size: 13, weight: .bold))
                .foregroundStyle(tint)
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12).padding(.horizontal, 10)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(title)
    }

    private func reinstallDriver() {
        guard !maintenanceBusy else { return }
        maintenanceBusy = true
        Task {
            let outcome = await Task.detached { DriverMaintenance.reinstallDriver() }.value
            maintenanceBusy = false
            switch outcome {
            case .cancelled:
                break
            case .done:
                withAnimation { driverReinstalled = true }
                try? await Task.sleep(nanoseconds: 2_000_000_000)
                withAnimation { driverReinstalled = false }
            default:
                showMaintenanceFailure(
                    L("드라이버 재설치 실패", "Reinstall failed", "再インストールに失敗",
                      "Échec de la réinstallation", "Neuinstallation fehlgeschlagen"),
                    unavailable: L("이 복사본에는 드라이버가 들어 있지 않습니다. 설치 패키지로 다시 설치해 주세요.",
                                   "This copy of the app carries no driver. Reinstall from the installer package.",
                                   "このアプリにはドライバが含まれていません。インストーラから入れ直してください。",
                                   "Cette copie de l'app ne contient aucun pilote. Réinstallez depuis le programme d'installation.",
                                   "Diese App-Kopie enthält keinen Treiber. Bitte über das Installationspaket neu installieren."),
                    outcome: outcome)
            }
        }
    }

    private func removeRoomcut() {
        guard !maintenanceBusy else { return }
        let confirm = NSAlert()
        confirm.alertStyle = .critical
        confirm.messageText = L("Roomcut을 제거할까요?", "Remove Roomcut?", "Roomcut を削除しますか？",
                                "Supprimer Roomcut ?", "Roomcut entfernen?")
        confirm.informativeText = L(
            "드라이버, 엔진, 앱이 모두 삭제되고 Roomcut이 종료됩니다.",
            "The driver, the engine and the app are all removed, then Roomcut quits.",
            "ドライバ・エンジン・アプリをすべて削除し、Roomcut を終了します。",
            "Le pilote, le moteur et l'app sont supprimés, puis Roomcut quitte.",
            "Treiber, Engine und App werden entfernt, danach beendet sich Roomcut.")
        confirm.addButton(withTitle: L("제거", "Remove", "削除", "Supprimer", "Entfernen"))
        confirm.addButton(withTitle: L("취소", "Cancel", "キャンセル", "Annuler", "Abbrechen"))
        guard confirm.runModal() == .alertFirstButtonReturn else { return }

        maintenanceBusy = true
        Task {
            let outcome = await Task.detached { DriverMaintenance.removeRoomcut() }.value
            maintenanceBusy = false
            switch outcome {
            case .done:
                NSApp.terminate(nil)
            case .cancelled:
                break
            default:
                showMaintenanceFailure(
                    L("제거 실패", "Removal failed", "削除に失敗", "Échec de la suppression",
                      "Entfernen fehlgeschlagen"),
                    unavailable: L("제거 스크립트를 찾을 수 없습니다: \(DriverMaintenance.uninstaller)",
                                   "Uninstaller not found: \(DriverMaintenance.uninstaller)",
                                   "アンインストーラが見つかりません: \(DriverMaintenance.uninstaller)",
                                   "Programme de désinstallation introuvable : \(DriverMaintenance.uninstaller)",
                                   "Deinstallationsprogramm nicht gefunden: \(DriverMaintenance.uninstaller)"),
                    outcome: outcome)
            }
        }
    }

    private func showMaintenanceFailure(_ title: String, unavailable: String,
                                        outcome: DriverMaintenance.Outcome) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = title
        switch outcome {
        case .unavailable:      alert.informativeText = unavailable
        case .failed(let why):  alert.informativeText = why
        default:                return
        }
        alert.runModal()
    }

    private func setLaunchAtLogin(_ on: Bool) {
        do {
            if on { try SMAppService.mainApp.register() }
            else { try SMAppService.mainApp.unregister() }
            launchAtLogin = on
        } catch {
            launchAtLogin = SMAppService.mainApp.status == .enabled
        }
    }

}

// Driver-level maintenance: reinstalling the HAL plug-in and removing Roomcut
// both write to /Library, so each runs ONE privileged shell command through
// `osascript … with administrator privileges` — a single system auth prompt, and
// no sudoers rule to install (unlike EngineService's launchctl verbs).
enum DriverMaintenance {
    enum Outcome {
        case done
        case cancelled          // user dismissed the authorization prompt
        case unavailable        // the payload this action needs isn't on disk
        case failed(String)     // anything else; carries osascript's stderr
    }

    static let halDriver = "/Library/Audio/Plug-Ins/HAL/Roomcut.driver"
    static let uninstaller = "/Library/Application Support/Roomcut/uninstall.sh"

    // The driver copy shipped inside the app (Contents/PlugIns, where codesign
    // seals a nested bundle), so a reinstall needs nothing but the app itself.
    static var bundledDriver: URL? {
        guard let url = Bundle.main.builtInPlugInsURL?
            .appendingPathComponent("Roomcut.driver"),
              FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url
    }

    // Replace the installed plug-in with the bundled one and restart coreaudiod so
    // it reloads — the same steps as scripts/install-driver.sh, minus the engine
    // (this action is scoped to the driver; the daemon keeps running).
    static func reinstallDriver() -> Outcome {
        guard let src = bundledDriver else { return .unavailable }
        let dest = quoted(halDriver)
        let command = [
            "/bin/rm -rf \(dest)",
            "/bin/cp -R \(quoted(src.path)) \(dest)",
            "/usr/sbin/chown -R root:wheel \(dest)",
            "(/usr/bin/xattr -dr com.apple.quarantine \(dest) 2>/dev/null || true)",
            "(/usr/bin/killall -9 coreaudiod 2>/dev/null || true)",
        ].joined(separator: " && ")
        return runPrivileged(command)
    }

    // The uninstaller dropped next to the install by install.sh / the pkg. It
    // removes the driver, the engine daemon and /Applications/Roomcut.app.
    static func removeRoomcut() -> Outcome {
        guard FileManager.default.isReadableFile(atPath: uninstaller) else { return .unavailable }
        return runPrivileged("/bin/bash \(quoted(uninstaller))")
    }

    private static func quoted(_ path: String) -> String {
        "'" + path.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    // Call off the main thread: the authorization prompt blocks until dismissed.
    private static func runPrivileged(_ command: String) -> Outcome {
        let escaped = command
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        p.arguments = ["-e", "do shell script \"\(escaped)\" with administrator privileges"]
        let errPipe = Pipe()
        p.standardOutput = FileHandle.nullDevice
        p.standardError = errPipe
        do {
            try p.run()
            let stderr = String(decoding: errPipe.fileHandleForReading.readDataToEndOfFile(),
                                as: UTF8.self)
            p.waitUntilExit()
            if p.terminationStatus == 0 { return .done }
            // -128 is AppleScript's "user cancelled" — not a failure worth alerting.
            if stderr.contains("-128") { return .cancelled }
            return .failed(stderr.trimmingCharacters(in: .whitespacesAndNewlines))
        } catch {
            return .failed(error.localizedDescription)
        }
    }
}
