import SwiftUI
import ServiceManagement
import UniformTypeIdentifiers
import RoomcutCore
import RoomcutPresentationCore

// The bottom of Settings: the lyrics cache control, driver reinstall/removal,
// and quit. Everything here acts on the machine rather than on the sound.
extension SettingsTab {

    // Native Liquid-Glass action button that clears the on-disk synced-lyrics cache.
    var lyricsCacheControl: some View {
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

    func clearLyricsCache() {
        Task {
            await LRCLIBClient.clearCache()
            lyricsCacheCount = 0
            withAnimation { lyricsCacheCleared = true }
            try? await Task.sleep(nanoseconds: 1_400_000_000)
            withAnimation { lyricsCacheCleared = false }
        }
    }

    func songCountLabel(_ n: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "\(n)곡"
        case .japanese: return "\(n) 曲"
        case .french:   return "\(n) titres"
        case .german:   return "\(n) Titel"
        default:        return "\(n) songs"
        }
    }

    func rateLabel(_ sr: Double?) -> String {
        guard let sr else { return "—" }
        let khz = sr / 1000
        return khz == khz.rounded() ? "\(Int(khz)) kHz" : String(format: "%.1f kHz", khz)
    }

    func bitDepthLabel(_ bits: Int?) -> String {
        bits.map { "\($0)-bit" } ?? "—"
    }

    // Driver maintenance, in the same card shape as Quit but split down the middle:
    // reinstall on the left, full removal on the right, one hairline between them.
    // Both halves need root, so each runs a single privileged shell command.
    var maintenanceCard: some View {
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
    func maintenanceButton(systemImage: String, title: String, tint: Color,
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

    func reinstallDriver() {
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

    func removeRoomcut() {
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

    func showMaintenanceFailure(_ title: String, unavailable: String,
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

    func setLaunchAtLogin(_ on: Bool) {
        do {
            if on { try SMAppService.mainApp.register() }
            else { try SMAppService.mainApp.unregister() }
            launchAtLogin = on
        } catch {
            launchAtLogin = SMAppService.mainApp.status == .enabled
        }
    }

}
