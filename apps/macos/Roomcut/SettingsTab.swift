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
    @Environment(\.colorScheme) var scheme
    @State var launchAtLogin = SMAppService.mainApp.status == .enabled
    @State var lyricsCacheCount = 0
    @State var lyricsCacheCleared = false
    @State var presetTransferNote: String?
    @State var maintenanceBusy = false
    @State var driverReinstalled = false
    let lyricsCachePath = "~/Library/Caches/com.habinsong.roomcut/lyrics.json"

    var enabledBinding: Binding<Bool> {
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
    var appearanceGroup: some View {
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

    var outputGroup: some View {
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

    var behaviorGroup: some View {
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
    var groupRule: some View { Divider().opacity(0.4) }

    // Native Liquid-Glass segmented theme selector (glass capsule + a morphing
    // selection pill), instead of the boxy menu dropdown.
    var selectedDeviceName: String {
        model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name ?? "—"
    }

    // Same Liquid-Glass recipe as EqPresetPicker: one `.glassEffect(.regular, in:
    // Capsule())` capsule (NOT `.interactive` — that lenses into a blobby pill),
    // value in semibold + a small `chevron.up.chevron.down`. A plain button menu so
    // the glass is the only surface. Sizes to content; `maxTitleWidth` only caps a
    // long device name so it truncates instead of stretching the row.
    func glassMenu<Items: View>(title: String,
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
    func glassActionButton(systemImage: String, accessibilityLabel: String,
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

    func exportPresets() {
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

    func importPresets() {
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

    func showPresetTransferNote(_ note: String) {
        withAnimation { presetTransferNote = note }
        Task {
            try? await Task.sleep(nanoseconds: 2_200_000_000)
            withAnimation { presetTransferNote = nil }
        }
    }

    func presetCountLabel(_ n: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "\(n)개 가져옴"
        case .japanese: return "\(n) 件を読み込み"
        case .french:   return "\(n) importés"
        case .german:   return "\(n) importiert"
        default:        return "\(n) imported"
        }
    }
}
