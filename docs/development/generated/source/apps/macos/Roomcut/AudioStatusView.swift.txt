//
// AudioStatusView.swift — the main window's right-hand inspector plus the small
// status pieces (status light, peak meter) shared with the menu bar.
//
// Color is never the only signal: every state carries a label and an SF Symbol,
// and the meter shows a numeric dBFS value, so the UI reads under Increase
// Contrast / color-blindness too.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// MARK: - Shared color roles (role → Color lives in the app, PresentationCore stays color-free)

func statusColor(_ role: RoomcutPresentation.StatusRole) -> Color {
    switch role {
    case .normal:  return Color(nsColor: .systemYellow)   // amber status glow
    case .bypass:  return Color(nsColor: .secondaryLabelColor)
    case .warning: return Color(nsColor: .systemOrange)
    case .offline: return Color(nsColor: .systemRed)
    }
}

func meterColor(_ role: RoomcutTheme.MeterRole) -> Color {
    switch role {
    case .peak:    return Color(nsColor: .systemBlue)     // blue precision meter
    case .limiter: return Color(nsColor: .systemYellow)   // amber
    case .warning: return Color(nsColor: .systemOrange)
    }
}

// MARK: - Status light

struct StatusLight: View {
    let role: RoomcutPresentation.StatusRole

    var body: some View {
        Circle()
            .fill(statusColor(role))
            .frame(width: 9, height: 9)
            .overlay(Circle().strokeBorder(.black.opacity(0.12)))
            .accessibilityHidden(true) // the adjacent label carries the meaning
    }
}

// MARK: - Peak meter (-60…0 dBFS)

struct PeakMeter: View {
    let peak: Float
    var limiterActive: Bool = false
    var underrunsVisible: Bool = false

    private var db: Double { RoomcutPresentation.peakDbFS(peak) }
    private var fraction: Double { max(0, min(1, (db + 60) / 60)) }
    private var role: RoomcutTheme.MeterRole {
        RoomcutTheme.meterRole(limiterActive: limiterActive, underrunsVisible: underrunsVisible)
    }

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Capsule().fill(Color(nsColor: .quaternaryLabelColor))
                Capsule()
                    .fill(meterColor(role))
                    .frame(width: max(2, geo.size.width * fraction))
            }
        }
        .frame(height: 6)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("피크 레벨")
        .accessibilityValue(String(format: "%.0f dBFS", db))
    }
}

// MARK: - Inspector (right column of the main window)

struct AudioStatusView: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var meters: RoomcutMeters

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            statusRow

            meterSection

            Divider()

            outputSection

            Divider()

            gainSlider(
                title: "Preamp",
                value: $model.preampDb,
                range: -24...12,
                onChange: { model.schedulePushParams() }
            )
            gainSlider(
                title: "Output",
                value: $model.outputGainDb,
                range: -24...12,
                onChange: { model.schedulePushParams() }
            )

            if meters.underrunsActive {
                Label("재생 끊김 감지", systemImage: "exclamationmark.triangle")
                    .font(.caption)
                    .foregroundStyle(Color(nsColor: .systemOrange))
                    .accessibilityLabel("재생 끊김이 감지되었습니다")
            }

            Spacer()
        }
        .padding(20)
        .frame(maxHeight: .infinity, alignment: .topLeading)
        .disabled(!model.status.reachable)
    }

    private var statusRow: some View {
        HStack(spacing: 8) {
            StatusLight(role: model.status.presentation.role)
            Label(model.status.presentation.label, systemImage: model.status.presentation.symbol)
                .font(.headline)
            if model.status.safeBypass {
                Text("안전 바이패스")
                    .font(.caption2.bold())
                    .padding(.horizontal, 6).padding(.vertical, 2)
                    .background(Color(nsColor: .systemOrange).opacity(0.85), in: Capsule())
                    .foregroundStyle(.white)
            }
        }
        .accessibilityElement(children: .combine)
    }

    private var meterSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Peak").foregroundStyle(.secondary)
                Spacer()
                Text(String(format: "%.1f dBFS", RoomcutPresentation.peakDbFS(meters.displayPeak)))
                    .monospacedDigit()
            }
            .font(.callout)
            PeakMeter(
                peak: meters.displayPeak,
                limiterActive: RoomcutPresentation.shouldShowLimiter(gainReductionDb: meters.displayLimiterGRDb),
                underrunsVisible: meters.underrunsActive
            )
            if RoomcutPresentation.shouldShowLimiter(gainReductionDb: meters.displayLimiterGRDb) {
                HStack {
                    Text("Limiter").foregroundStyle(.secondary)
                    Spacer()
                    Text(String(format: "−%.1f dB", meters.displayLimiterGRDb))
                        .monospacedDigit()
                        .foregroundStyle(Color(nsColor: .systemYellow))
                }
                .font(.callout)
                .accessibilityElement(children: .combine)
                .accessibilityLabel("리미터 게인 리덕션")
                .accessibilityValue(String(format: "%.1f 데시벨", meters.displayLimiterGRDb))
            }
        }
    }

    // Real output device + its volume (the engine mirrors the Roomcut device
    // volume to the selected device's hardware volume — full range).
    private var outputSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("출력 장치").font(.caption).foregroundStyle(.secondary)
            Picker("출력 장치", selection: Binding(
                get: { model.selectedDeviceUID },
                set: { model.selectDevice($0) }
            )) {
                ForEach(model.outputDevices) { d in Text(d.name).tag(d.uid) }
                if !model.outputDevices.contains(where: { $0.uid == model.selectedDeviceUID }) {
                    Text(model.selectedDeviceUID.isEmpty ? "—" : model.selectedDeviceUID)
                        .tag(model.selectedDeviceUID)
                }
            }
            .labelsHidden()
            .accessibilityLabel("출력 장치")

            HStack(spacing: 8) {
                Image(systemName: "speaker.fill").foregroundStyle(.secondary)
                Slider(value: Binding(get: { model.volume }, set: { model.setVolume($0) }),
                       in: 0...RoomcutViewModel.maxVolume) { editing in
                    editing ? model.beginVolumeEdit() : model.endVolumeEdit()
                }
                .disabled(!model.hasVolumeControl)
                .accessibilityLabel("볼륨")
                .accessibilityValue("\(Int((model.volume * 100).rounded())) 퍼센트")
                Text("\(Int((model.volume * 100).rounded()))%")
                    .font(.caption.monospacedDigit())
                    .frame(width: 36, alignment: .trailing)
            }

            Toggle("출력 고정", isOn: Binding(
                get: { model.keepDefault },
                set: { model.setKeepDefault($0) }
            ))
            .toggleStyle(.switch)
            .help("켜면 AirPods 등이 연결돼도 Roomcut을 시스템 출력으로 유지합니다")
            .accessibilityLabel("출력 고정 (AirPods 연결 시 Roomcut 유지)")
        }
    }

    private func gainSlider(title: String, value: Binding<Double>,
                            range: ClosedRange<Double>, onChange: @escaping () -> Void) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).foregroundStyle(.secondary)
                Spacer()
                Text(String(format: "%+.1f dB", value.wrappedValue))
                    .monospacedDigit()
            }
            .font(.callout)
            Slider(value: Binding(
                get: { value.wrappedValue },
                set: { value.wrappedValue = $0; onChange() }
            ), in: range)
            .accessibilityLabel(title)
            .accessibilityValue(String(format: "%+.1f 데시벨", value.wrappedValue))
        }
    }
}
