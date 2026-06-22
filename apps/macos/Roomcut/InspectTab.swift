//
// InspectTab.swift — live engine + signal diagnostics (read-only).
//
// Liquid-Glass redesign: a transparent scroll over the Now Playing wash with
// `.regularMaterial` content cards. Real values only — no fake spectrum.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct InspectTab: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var meters: RoomcutMeters
    @Environment(\.colorScheme) private var scheme

    private var limiterActive: Bool {
        RoomcutPresentation.shouldShowLimiter(gainReductionDb: meters.displayLimiterGRDb)
    }
    private var deviceName: String {
        model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name ?? "—"
    }

    var body: some View {
        RoomcutTabScreen {
            RoomcutSection(L("Signal", "Signal", "シグナル", "Signal", "Signal")) {
                RoomcutRow(L("Peak", "Peak", "ピーク", "Crête", "Spitze"), systemImage: "waveform.path") {
                    PeakMeter(peak: meters.displayPeak,
                              limiterActive: limiterActive,
                              underrunsVisible: meters.underrunsActive)
                        .frame(width: 150)
                }
                RoomcutDivider()
                RoomcutRow(L("Limiter", "Limiter", "リミッター", "Limiteur", "Limiter"), systemImage: "shield") {
                    valueText(limiterActive
                              ? String(format: "−%.1f dB", abs(Double(meters.displayLimiterGRDb)))
                              : L("Safe", "Safe", "安全", "Sûr", "Sicher"),
                              tint: RoomcutTokens.blue(scheme))
                }
                RoomcutDivider()
                RoomcutRow(L("Dropouts", "Dropouts", "ドロップアウト", "Pertes audio", "Aussetzer"),
                           systemImage: "exclamationmark.triangle") {
                    valueText(meters.underrunsActive
                              ? L("감지됨", "Detected", "検出", "Détecté", "Erkannt") : "0",
                              tint: meters.underrunsActive ? RoomcutTokens.blue(scheme) : .secondary)
                }
            }

            RoomcutSection(L("Stereo", "Stereo", "ステレオ", "Stéréo", "Stereo")) {
                RoomcutRow(L("상관도", "Correlation", "相関", "Corrélation", "Korrelation"),
                           systemImage: "antenna.radiowaves.left.and.right") {
                    if let a = model.analysis, a.valid {
                        HStack(spacing: 8) {
                            CorrelationMeter(value: a.correlation, accent: RoomcutTokens.blue(scheme))
                            valueText(String(format: "%+.2f", a.correlation),
                                      tint: correlationTint(a.correlation))
                                .lineLimit(1)
                                .fixedSize(horizontal: true, vertical: false)
                                .frame(width: 42, alignment: .trailing)
                        }
                        .frame(width: 152, alignment: .trailing)
                    } else { valueText("—") }
                }
                RoomcutDivider()
                RoomcutRow(L("폭", "Width", "幅", "Largeur", "Breite"), systemImage: "arrow.left.and.right") {
                    if let a = model.analysis, a.valid {
                        valueText("\(Int((a.stereoWidth * 100).rounded()))%",
                                  tint: widthTint(a.stereoWidth))
                    } else { valueText("—") }
                }
            }

            RoomcutSection(L("Format", "Format", "フォーマット", "Format", "Format")) {
                RoomcutRow(L("샘플레이트", "Sample Rate", "サンプルレート", "Fréquence d'échantillonnage", "Abtastrate"),
                           systemImage: "dot.radiowaves.left.and.right") {
                    valueText(model.audioFormat.map { "\(Int($0.sampleRate / 1000)) kHz" } ?? "—",
                              tint: model.audioFormat == nil ? .secondary : RoomcutTokens.blue(scheme))
                }
                RoomcutDivider()
                RoomcutRow(L("비트 뎁스", "Bit Depth", "ビット深度", "Profondeur de bits", "Bittiefe"),
                           systemImage: "number") {
                    valueText(model.audioFormat.map { "\($0.bitDepth)-bit" } ?? "32-bit Float",
                              tint: model.audioFormat == nil ? .secondary : RoomcutTokens.blue(scheme))
                }
                RoomcutDivider()
                RoomcutRow(L("지연", "Latency", "レイテンシ", "Latence", "Latenz"), systemImage: "timer") {
                    valueText(model.audioFormat.map { String(format: "%.1f ms", $0.latencyMs) } ?? "—",
                              tint: latencyTint(model.audioFormat?.latencyMs))
                }
            }

            RoomcutSection(L("Output", "Output", "出力", "Sortie", "Ausgabe")) {
                RoomcutRow(L("장치", "Device", "デバイス", "Périphérique", "Gerät"), systemImage: "hifispeaker") {
                    valueText(deviceName, tint: model.status.reachable ? RoomcutTokens.blue(scheme) : .secondary).lineLimit(1)
                }
                RoomcutDivider()
                RoomcutRow(L("볼륨", "Volume", "音量", "Volume", "Lautstärke"), systemImage: "speaker.wave.2") {
                    valueText(model.hasVolumeControl ? "\(Int((model.volume * 100).rounded()))%" : "—",
                              tint: model.hasVolumeControl ? RoomcutTokens.blue(scheme) : .secondary)
                }
            }

            RoomcutSection(L("Engine", "Engine", "エンジン", "Moteur", "Engine")) {
                RoomcutRow(L("상태", "Status", "状態", "État", "Status"), systemImage: "bolt") {
                    valueText(stateLabel, tint: stateTint)
                }
                RoomcutDivider()
                RoomcutRow(L("Preamp", "Preamp", "プリアンプ", "Préampli", "Vorverstärker"), systemImage: "dial.min") {
                    valueText(dbLabel(model.preampDb), tint: gainTint(model.preampDb))
                }
                RoomcutDivider()
                RoomcutRow(L("Output gain", "Output gain", "出力ゲイン", "Gain de sortie", "Ausgangsverstärkung"),
                           systemImage: "dial.max") {
                    valueText(dbLabel(model.outputGainDb), tint: gainTint(model.outputGainDb))
                }
            }
        }
        .onAppear { model.setAnalyzerVisible(true) }
        .onDisappear { model.setAnalyzerVisible(false) }
    }

    private func valueText(_ s: String, tint: Color = .secondary) -> some View {
        Text(s).font(.system(size: 12, weight: .medium).monospacedDigit()).foregroundStyle(tint)
    }

    private func correlationTint(_ v: Float) -> Color {
        v < 0 ? RoomcutTokens.amber : RoomcutTokens.blue(scheme)
    }

    private func widthTint(_ v: Float) -> Color {
        v < 0.15 ? .secondary : RoomcutTokens.blue(scheme)
    }

    private func latencyTint(_ ms: Double?) -> Color {
        guard let ms else { return .secondary }
        return ms <= 12 ? RoomcutTokens.blue(scheme) : .secondary
    }

    private func gainTint(_ db: Double) -> Color {
        RoomcutTokens.blue(scheme)
    }

    private func dbLabel(_ db: Double) -> String {
        abs(db) < 0.05 ? "0.0 dB" : String(format: "%+.1f dB", db)
    }

    private var stateLabel: String {
        if !model.status.reachable { return L("오프라인", "Offline", "オフライン", "Hors ligne", "Offline") }
        switch model.status.state {
        case EngineStatus.bypass: return L("바이패스", "Bypass", "バイパス", "Contournement", "Bypass")
        case EngineStatus.stopped: return L("정지", "Stopped", "停止", "Arrêté", "Gestoppt")
        default: return L("실행 중", "Running", "実行中", "En cours", "Aktiv")
        }
    }
    private var stateTint: Color {
        model.status.reachable ? RoomcutTokens.blue(scheme) : .secondary
    }
}

private struct CorrelationMeter: View {
    let value: Float   // −1…+1
    let accent: Color

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            let clamped = CGFloat(min(1, max(-1, value)))
            let x = (clamped + 1) / 2 * w
            ZStack(alignment: .leading) {
                Capsule().fill(.secondary.opacity(0.18)).frame(height: 4)
                Capsule()
                    .fill(accent.opacity(0.20))
                    .frame(width: w * 0.35, height: 4)
                    .offset(x: w * 0.65)
                Circle().fill(.secondary.opacity(0.35))     // unity-correlation tick
                    .frame(width: 2, height: 10).offset(x: w / 2 - 1)
                Circle().fill(tint)
                    .frame(width: 9, height: 9)
                    .position(x: min(w - 4.5, max(4.5, x)), y: h / 2)
            }
        }
        .frame(width: 110, height: 14)
    }

    private var tint: Color {
        value < 0 ? RoomcutTokens.amber : accent
    }
}
