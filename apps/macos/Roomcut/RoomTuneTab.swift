//
// RoomTuneTab.swift — Phase 1 of iPhone Room Tune (replaces the old AI tab).
//
// Detects an iPhone Continuity mic and guides the wired-first setup. Recording,
// sweep playback, analysis and correction are Phase 2+ — the "측정 시작" action is
// present but disabled here, with an honest "준비 중" state (no fake measurement).
//
import SwiftUI
import AppKit
import RoomcutCore
import RoomcutPresentationCore

struct RoomTuneTab: View {
    @ObservedObject var model: RoomcutViewModel
    @StateObject private var scanner = RoomTuneInputScanner()
    @StateObject private var measurement = RoomTuneMeasurement()
    @State private var presetName = "Room Tune"
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        RoomcutTabScreen {
            switch scanner.state {
            case .detected(let name): detectedSection(name)
            case .notFound:           notFoundSection
            }

            if let result = measurement.result {
                resultSection(result)
            }
        }
        .onAppear { scanner.refresh() }
    }

    // MARK: Detected

    private func detectedSection(_ name: String) -> some View {
        VStack(alignment: .leading, spacing: 7) {
            sectionHeader(L("iPhone 마이크", "iPhone Mic", "iPhone マイク", "Micro iPhone", "iPhone-Mikrofon"))
            RoomcutCard {
                RoomcutRow(L("감지됨", "Detected", "検出", "Détecté", "Erkannt"),
                           systemImage: "checkmark.circle.fill", tint: RoomcutTokens.green) {
                    Text(name).font(.system(size: 12, weight: .medium))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                        .lineLimit(1)
                        .truncationMode(.middle)
                }
                RoomcutDivider()
                Button { startMeasurement() } label: {
                    RoomcutRow(L("측정 시작", "Start Measurement", "測定開始",
                                 "Démarrer la mesure", "Messung starten"), systemImage: "waveform.path",
                               tint: measurement.isBusy ? RoomcutTokens.textTertiary(scheme) : RoomcutTokens.blue(scheme)) {
                        Text(measurementStatusText)
                            .font(.system(size: 12, weight: .medium))
                            .foregroundStyle(measurement.isBusy ? RoomcutTokens.amber : RoomcutTokens.blue(scheme))
                    }
                }
                .buttonStyle(.plain)
                .disabled(measurement.isBusy)
                if measurement.isBusy {
                    RoomcutDivider()
                    RoomcutRow(L("입력 레벨", "Input Level", "入力レベル", "Niveau d'entrée", "Eingangspegel"),
                               systemImage: "waveform") {
                        Text(String(format: "%.0f%%", min(1, measurement.inputPeak) * 100))
                            .font(.system(size: 12, weight: .medium).monospacedDigit())
                            .foregroundStyle(measurement.inputPeak > 0.01 ? RoomcutTokens.green : .secondary)
                    }
                }
            }
        }
    }

    // MARK: Not found

    private var notFoundSection: some View {
        VStack(alignment: .leading, spacing: 7) {
            sectionHeader(L("연결 확인", "Check Connection", "接続を確認", "Vérifier la connexion", "Verbindung prüfen"))
            RoomcutCard {
                RoomcutRow(L("iPhone 마이크", "iPhone Mic", "iPhone マイク", "Micro iPhone", "iPhone-Mikrofon"),
                           systemImage: "iphone.radiowaves.left.and.right") { EmptyView() }
                RoomcutDivider()
                checkRow(L("iPhone 잠금 해제", "Unlock iPhone", "iPhone のロックを解除",
                           "Déverrouiller l'iPhone", "iPhone entsperren"), "lock.open")
                RoomcutDivider()
                checkRow(L("케이블 연결", "Connect Cable", "ケーブルを接続",
                           "Brancher le câble", "Kabel anschließen"), "cable.connector")
                RoomcutDivider()
                checkRow(L("‘이 컴퓨터를 신뢰’ 허용", "Allow ‘Trust This Computer’", "「このコンピュータを信頼」を許可",
                           "Autoriser « Faire confiance à cet ordinateur »",
                           "„Diesem Computer vertrauen“ erlauben"),
                         "checkmark.shield")
                RoomcutDivider()
                checkRow(L("Wi-Fi · Bluetooth 켜기", "Turn on Wi-Fi · Bluetooth", "Wi-Fi・Bluetooth をオン",
                           "Activer Wi-Fi · Bluetooth", "Wi-Fi · Bluetooth einschalten"), "wifi")
                RoomcutDivider()
                checkRow(L("연속성 카메라 켜기", "Enable Continuity Camera", "連係カメラをオン",
                           "Activer Appareil photo de continuité", "Integrationskamera aktivieren"),
                         "iphone.radiowaves.left.and.right")
                RoomcutDivider()
                checkRow(L("같은 Apple ID 로그인", "Sign in with same Apple ID", "同じ Apple ID でサインイン",
                           "Connexion avec le même identifiant Apple", "Mit derselben Apple-ID anmelden"),
                         "person.crop.circle")
            }
        }
    }

    private func checkRow(_ title: String, _ icon: String) -> some View {
        RoomcutRow(title, systemImage: icon) { EmptyView() }
    }

    // MARK: Actions

    private func sectionHeader(_ title: String) -> some View {
        HStack(alignment: .center, spacing: 10) {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .semibold))
                .tracking(1.1)
                .foregroundStyle(.secondary)
            Spacer()
            compactActions
        }
        .padding(.horizontal, 6)
    }

    private var compactActions: some View {
        HStack(spacing: 6) {
            iconAction(L("새로고침", "Refresh", "更新", "Actualiser", "Aktualisieren"),
                       "arrow.clockwise") { scanner.refresh() }
            iconAction(L("사운드 입력 설정", "Sound Input Settings", "サウンド入力設定",
                         "Réglages d'entrée audio", "Toneingabe-Einstellungen"),
                       "gearshape") { openSoundInputSettings() }
        }
    }

    private func iconAction(_ title: String, _ icon: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 11, weight: .semibold))
                .foregroundStyle(RoomcutTokens.blue(scheme))
                .frame(width: 22, height: 22)
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .help(title)
        .accessibilityLabel(title)
    }

    private func openSoundInputSettings() {
        // macOS 13+ System Settings → Sound. (Direct deep-link to the Input tab
        // isn't reliably supported, so open the Sound pane.)
        if let url = URL(string: "x-apple.systempreferences:com.apple.Sound-Settings.extension") {
            NSWorkspace.shared.open(url)
        }
    }

    private var measurementStatusText: String {
        switch measurement.phase {
        case .idle:                    return L("준비됨", "Ready", "準備完了", "Prêt", "Bereit")
        case .measuring(let r, let t): return measuringText(r, t)
        case .done:                    return L("측정 완료", "Done", "測定完了", "Terminé", "Fertig")
        case .failed(let m):           return m
        }
    }

    private func measuringText(_ r: Int, _ t: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "측정 중 \(r)/\(t) · Roomcut 처리 우회"
        case .japanese: return "測定中 \(r)/\(t) · Roomcut バイパス"
        case .french:   return "Mesure \(r)/\(t) · Roomcut contourné"
        case .german:   return "Messung \(r)/\(t) · Roomcut umgangen"
        default:        return "Measuring \(r)/\(t) · Roomcut bypassed"
        }
    }

    private func startMeasurement() {
        guard let device = scanner.device else { return }
        // Route the sweep clean: bypass Roomcut's DSP so the test tone isn't coloured
        // by Roomcut's own EQ (we measure the room, not our processing). Restore after.
        let wasBypassed = model.status.manualBypass
        model.setBypass(true)
        measurement.start(device: device) { model.setBypass(wasBypassed) }
    }

    private func resultSection(_ result: RoomTuneResult) -> some View {
        RoomcutSection(L("측정 결과", "Measurement Result", "測定結果", "Résultat de la mesure", "Messergebnis")) {
            if !result.response.isEmpty {
                beforeAfterChart(result)
                    .frame(height: 112)
                    .padding(.horizontal, 14).padding(.vertical, 10)
                chartLegend
                RoomcutDivider()
            }
            RoomcutRow(L("보정 강도", "Correction Strength", "補正強度", "Intensité de correction", "Korrekturstärke"),
                       systemImage: "dial.medium") {
                Picker("", selection: Binding(
                    get: { measurement.strength },
                    set: { measurement.strength = $0 })) {
                    ForEach(RoomTuneStrength.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 170)
            }
            RoomcutDivider()
            if result.bands.isEmpty {
                RoomcutRow(L("보정 불필요", "No Correction Needed", "補正不要",
                             "Aucune correction nécessaire", "Keine Korrektur nötig"),
                           systemImage: "checkmark.seal", tint: RoomcutTokens.green) {
                    EmptyView()
                }
            } else {
                ForEach(Array(result.bands.enumerated()), id: \.offset) { idx, band in
                    if idx > 0 { RoomcutDivider() }
                    RoomcutRow(bandReduceLabel(band.freqHz),
                               systemImage: "minus.circle", tint: RoomcutTokens.amber) {
                        Text("\(String(format: "%.1f", band.gainDb)) dB")
                            .font(.system(size: 12, weight: .medium).monospacedDigit())
                            .foregroundStyle(.secondary)
                    }
                }
                RoomcutDivider()
                RoomcutRow(L("프리셋 이름", "Preset Name", "プリセット名", "Nom du préréglage", "Preset-Name"),
                           systemImage: "tag") {
                    TextField("Room Tune", text: $presetName)
                        .textFieldStyle(.plain)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 150)
                        .font(.system(size: 12))
                }
                RoomcutDivider()
                Button { applyResult(result) } label: {
                    RoomcutRow(L("적용 · 프리셋 저장", "Apply · Save Preset", "適用・プリセット保存",
                                 "Appliquer · Enregistrer le préréglage", "Anwenden · Preset speichern"),
                               systemImage: "checkmark.circle.fill",
                               tint: RoomcutTokens.blue(scheme)) { EmptyView() }
                }
                .buttonStyle(.plain)
                .disabled(!model.parametricAvailable)
            }
        }
    }

    private var chartLegend: some View {
        HStack(spacing: 14) {
            label(L("측정", "Measured", "測定", "Mesuré", "Gemessen"), .secondary)
            label(L("보정 후", "Corrected", "補正後", "Corrigé", "Korrigiert"), RoomcutTokens.textPrimary(scheme))
            Spacer()
        }
        .font(.system(size: 10, weight: .medium))
        .padding(.horizontal, 16).padding(.bottom, 6)
    }

    private func label(_ text: String, _ color: Color) -> some View {
        HStack(spacing: 4) {
            RoundedRectangle(cornerRadius: 1).fill(color).frame(width: 12, height: 2)
            Text(text).foregroundStyle(.secondary)
        }
    }

    // Before (measured room response) vs After (with the correction applied), using
    // the same RBJ biquad magnitude the engine applies (BiquadResponse). Display-only.
    private func beforeAfterChart(_ result: RoomTuneResult) -> some View {
        let resp = result.response
        let after = resp.map { item in
            item.db + result.bands.reduce(0.0) {
                $0 + BiquadResponse.magnitudeDb(band: $1, freqHz: item.freq, fs: 48000)
            }
        }
        let allDb = resp.map { $0.db } + after
        let lo = (allDb.min() ?? -60) - 1, hi = (allDb.max() ?? 0) + 1
        let fLo = resp.first?.freq ?? 50, fHi = resp.last?.freq ?? 8000
        return Canvas { ctx, size in
            func pt(_ f: Double, _ db: Double) -> CGPoint {
                CGPoint(x: log(f / fLo) / log(fHi / fLo) * size.width,
                        y: size.height * (1 - (db - lo) / max(hi - lo, 1e-6)))
            }
            func curve(_ ys: [Double]) -> Path {
                var p = Path()
                for (i, item) in resp.enumerated() {
                    let q = pt(item.freq, ys[i])
                    if i == 0 { p.move(to: q) } else { p.addLine(to: q) }
                }
                return p
            }
            ctx.stroke(curve(resp.map { $0.db }), with: .color(.secondary.opacity(0.6)), lineWidth: 1.5)
            ctx.stroke(curve(after), with: .color(RoomcutTokens.textPrimary(scheme)), lineWidth: 2)
        }
    }

    private func applyResult(_ result: RoomTuneResult) {
        for i in 0..<6 {
            model.setParametricBand(i, i < result.bands.count ? result.bands[i] : ParametricBand())
        }
        // Save as a dedicated preset (don't overwrite Custom), tagged with a summary.
        let trimmed = presetName.trimmingCharacters(in: .whitespacesAndNewlines)
        model.saveCurrentAsPreset(name: trimmed.isEmpty ? "Room Tune" : trimmed,
                                  roomTuneInfo: roomTuneSummary(result))
    }

    // "21 Jun 2026 at 14:30 · 송하빈의 iPhone → iFi USB · 3밴드" — devices read live,
    // never hardcoded, so it's correct on any Mac.
    private func roomTuneSummary(_ result: RoomTuneResult) -> String {
        let date = DateFormatter.localizedString(from: Date(), dateStyle: .medium, timeStyle: .short)
        let input = scanner.device?.localizedName ?? "iPhone"
        let output = model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name
            ?? L("출력 장치", "Output Device", "出力デバイス", "Périphérique de sortie", "Ausgabegerät")
        return "\(date) · \(input) → \(output) · \(bandsCountLabel(result.bands.count))"
    }

    private func bandReduceLabel(_ hz: Double) -> String {
        let f = Int(hz.rounded())
        switch AppLanguage.effective {
        case .korean:   return "\(f) Hz 완화"
        case .japanese: return "\(f) Hz 緩和"
        case .french:   return "Atténuer \(f) Hz"
        case .german:   return "\(f) Hz absenken"
        default:        return "Reduce \(f) Hz"
        }
    }

    private func bandsCountLabel(_ n: Int) -> String {
        switch AppLanguage.effective {
        case .korean:   return "\(n)밴드"
        case .japanese: return "\(n) バンド"
        case .french:   return "\(n) bandes"
        case .german:   return "\(n) Bänder"
        default:        return "\(n) bands"
        }
    }
}
