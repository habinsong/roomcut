//
// AdvancedControls.swift — the Sound Controls sheet's Advanced body.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct AdvancedControls: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var meters: RoomcutMeters
    @Environment(\.colorScheme) private var scheme
    @State private var tab: AdvTab = .bands

    init(model: RoomcutViewModel, meters: RoomcutMeters) {
        self.model = model
        self.meters = meters
        _tab = State(initialValue: AppLaunch.fixtureKind == .uiAnalyzer ? .analyzer : .bands)
    }

    enum AdvTab: String, CaseIterable {
        case eq = "graph"
        case bands = "10-Band"
        case parametric = "Parametric"
        case limiter = "Limiter"
        case analyzer = "Analyzer"

        var displayName: String {
            switch self {
            case .eq:         return L("graph", "graph", "グラフ", "Graphe", "Graph")
            case .bands:      return L("10-Band", "10-Band", "10バンド", "10 bandes", "10 Bänder")
            case .parametric: return L("Parametric", "Parametric", "パラメトリック", "Paramétrique", "Parametrisch")
            case .limiter:    return L("Limiter", "Limiter", "リミッター", "Limiteur", "Limiter")
            case .analyzer:   return L("Analyzer", "Analyzer", "アナライザー", "Analyseur", "Analyzer")
            }
        }
    }

    private var limiterActive: Bool {
        RoomcutPresentation.shouldShowLimiter(gainReductionDb: meters.displayLimiterGRDb)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            subTabStrip

            switch tab {
            case .eq:         eqOverview
            case .bands:      EqualizerView(model: model)
            case .parametric: ParametricEditor(model: model)
            case .limiter:    limiterAndGain
            case .analyzer:   analyzerBody
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .disabled(!model.status.reachable)
    }

    // MARK: Sub-tab strip

    private var subTabStrip: some View {
        // Spacers between (not fixed spacing) so the first/last tabs sit flush to
        // the edges — equal left/right margins instead of a trailing gap.
        HStack(spacing: 0) {
            ForEach(AdvTab.allCases, id: \.self) { t in
                if t != .eq { Spacer(minLength: 0) }
                subTab(t)
            }
        }
    }

    private func subTab(_ t: AdvTab) -> some View {
        let selected = tab == t
        return Button {
            withAnimation(.smooth(duration: 0.2)) { tab = t }
        } label: {
            Text(t.displayName)
                .font(.system(size: 11, weight: selected ? .semibold : .regular))
                .lineLimit(1)
                .minimumScaleFactor(0.72)   // long labels (Parametric) shrink to fit, no wrap/truncation
                .foregroundStyle(selected ? RoomcutTokens.blue(scheme)
                                 : RoomcutTokens.textSecondary(scheme))
                .padding(.horizontal, 6).padding(.vertical, 5)
                .background {
                    if selected {
                        Capsule().fill(RoomcutTokens.blue(scheme).opacity(0.12))
                    }
                }
                .contentShape(Capsule())
        }
        .buttonStyle(.plain)
        .help(t.displayName)
        .accessibilityLabel(t.displayName)
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
    }

    // MARK: Bodies

    private var eqOverview: some View {
        EqResponseCurve(eqGains: model.eqGainsDb, parametric: model.parametric,
                        preampDb: model.preampDb, accent: RoomcutTokens.blue(scheme), scheme: scheme)
            .equatable()
            .frame(height: 120)
    }

    private var limiterAndGain: some View {
        VStack(alignment: .leading, spacing: 14) {
            limiterRow
            gainSlider(title: L("Preamp", "Preamp", "プリアンプ", "Préampli", "Vorverstärker"),
                       value: $model.preampDb, range: -24...12)
            gainSlider(title: L("Output", "Output", "出力", "Sortie", "Ausgabe"),
                       value: $model.outputGainDb, range: -24...12)
            if model.dynamicsAvailable {
                levelingSlider
                lowCutSlider
            }
        }
    }

    // Low Cut (HPF): trims rumble below the set frequency. The engine treats
    // anything under 20 Hz as off, so the bottom of the slider reads "Off".
    private var lowCutSlider: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(L("저음 컷", "Low Cut", "ローカット", "Coupe-bas", "Low Cut"))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                Spacer()
                Text(model.highpassHz < 20
                     ? L("꺼짐", "Off", "オフ", "Désactivé", "Aus")
                     : "\(Int(model.highpassHz.rounded())) Hz")
                    .monospacedDigit()
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            }
            .font(.callout)
            Slider(value: Binding(
                get: { model.highpassHz },
                set: { model.setHighpassHz($0) }
            ), in: 0...400)
            .tint(RoomcutTokens.blue(scheme))
            .accessibilityLabel(L("저음 컷", "Low Cut", "ローカット", "Coupe-bas", "Low Cut"))
            .accessibilityValue(model.highpassHz < 20
                ? L("꺼짐", "Off", "オフ", "Désactivé", "Aus")
                : "\(Int(model.highpassHz.rounded())) Hz")
        }
    }

    // 볼륨 평준화: the light compressor's single knob (0 = off). Evens out
    // loud/quiet passages — night listening without riding the volume.
    private var levelingSlider: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(L("볼륨 평준화", "Volume Leveling", "音量の平準化", "Nivellement du volume", "Lautstärkeangleichung"))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                Spacer()
                Text(model.compAmount < 0.5
                     ? L("꺼짐", "Off", "オフ", "Désactivé", "Aus")
                     : "\(Int(model.compAmount.rounded()))%")
                    .monospacedDigit()
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            }
            .font(.callout)
            Slider(value: Binding(
                get: { model.compAmount },
                set: { model.setCompAmount($0) }
            ), in: 0...100)
            .tint(RoomcutTokens.blue(scheme))
            .accessibilityLabel(L("볼륨 평준화", "Volume Leveling", "音量の平準化",
                                  "Nivellement du volume", "Lautstärkeangleichung"))
            .accessibilityValue(model.compAmount < 0.5
                ? L("꺼짐", "Off", "オフ", "Désactivé", "Aus")
                : "\(Int(model.compAmount.rounded())) %")
        }
    }

    private var analyzerBody: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let analysis = model.analysis, analysis.valid {
                VStack(alignment: .leading, spacing: 6) {
                    Text(L("Current Sound", "Current Sound", "現在のサウンド", "Son actuel", "Aktueller Klang"))
                        .font(.system(size: 11, weight: .medium))
                        .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                    Text(RoomcutAnalysisPresentation.currentSound(for: analysis))
                        .font(.system(size: 17, weight: .semibold))
                        .lineLimit(1)
                        .minimumScaleFactor(0.7)
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                }

                SpectrumBars(values: analysis.spectrum,
                             accent: RoomcutTokens.blue(scheme),
                             scheme: scheme)

                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())],
                          alignment: .leading, spacing: 8) {
                    analyzerMetric(L("Peak", "Peak", "ピーク", "Crête", "Spitze"),
                                   RoomcutAnalysisPresentation.db(analysis.peakDb))
                    analyzerMetric("RMS", RoomcutAnalysisPresentation.db(analysis.rmsDb))
                    analyzerMetric(L("Width", "Width", "幅", "Largeur", "Breite"),
                                   RoomcutAnalysisPresentation.percent(analysis.stereoWidth))
                    analyzerMetric(L("Centroid", "Centroid", "重心", "Centroïde", "Schwerpunkt"),
                                   RoomcutAnalysisPresentation.hz(analysis.spectralCentroid))
                }
            } else if model.analyzerAvailable {
                analyzerEmpty
            } else {
                analyzerUnavailable
            }
        }
        .onAppear {
            model.setAnalyzerVisible(true)
            model.refresh()
        }
        .onDisappear {
            model.setAnalyzerVisible(false)
        }
    }

    private var analyzerUnavailable: some View {
        VStack(spacing: 8) {
            Image(systemName: "waveform.path.ecg")
                .font(.title3)
                .foregroundStyle(RoomcutTokens.textTertiary(scheme))
            Text(model.status.reachable
                 ? L("현재 실행 중인 엔진이 Analyzer를 지원하지 않습니다.",
                     "The running engine does not support Analyzer.",
                     "実行中のエンジンは Analyzer に対応していません。",
                     "Le moteur en cours d'exécution ne prend pas en charge Analyzer.",
                     "Die laufende Engine unterstützt Analyzer nicht.")
                 : L("엔진 연결 후 Analyzer 상태를 확인할 수 있습니다.",
                     "Connect the engine to view Analyzer status.",
                     "エンジン接続後に Analyzer の状態を確認できます。",
                     "Connectez le moteur pour voir l'état de l'Analyzer.",
                     "Verbinden Sie die Engine, um den Analyzer-Status zu sehen."))
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                .multilineTextAlignment(.center)
            if model.status.reachable {
                Text(L("최신 엔진 설치가 필요합니다.", "A newer engine install is required.",
                       "最新のエンジンのインストールが必要です。", "Une installation plus récente du moteur est requise.",
                       "Eine neuere Engine-Installation ist erforderlich."))
                    .font(.system(size: 11))
                    .foregroundStyle(RoomcutTokens.textTertiary(scheme))
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: 150)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(model.status.reachable
            ? L("현재 실행 중인 엔진이 Analyzer를 지원하지 않습니다. 최신 엔진 설치가 필요합니다.",
                "The running engine does not support Analyzer. A newer engine install is required.",
                "実行中のエンジンは Analyzer に対応していません。最新のエンジンのインストールが必要です。",
                "Le moteur en cours d'exécution ne prend pas en charge Analyzer. Une installation plus récente du moteur est requise.",
                "Die laufende Engine unterstützt Analyzer nicht. Eine neuere Engine-Installation ist erforderlich.")
            : L("엔진 연결 후 Analyzer 상태를 확인할 수 있습니다.",
                "Connect the engine to view Analyzer status.",
                "エンジン接続後に Analyzer の状態を確認できます。",
                "Connectez le moteur pour voir l'état de l'Analyzer.",
                "Verbinden Sie die Engine, um den Analyzer-Status zu sehen."))
    }

    private var analyzerEmpty: some View {
        VStack(spacing: 8) {
            Image(systemName: "waveform.path.ecg")
                .font(.title3)
                .foregroundStyle(RoomcutTokens.textTertiary(scheme))
            Text(L("Waiting for signal", "Waiting for signal", "信号待ち", "En attente de signal", "Warte auf Signal"))
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
            Text(L("No Signal", "No Signal", "信号なし", "Aucun signal", "Kein Signal"))
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
        }
        .frame(maxWidth: .infinity)
        .frame(height: 150)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(L("Analyzer waiting for signal", "Analyzer waiting for signal", "Analyzer は信号待ちです",
                              "L'Analyzer attend un signal", "Analyzer wartet auf Signal"))
    }

    private func analyzerMetric(_ title: String, _ value: String) -> some View {
        HStack {
            Text(title)
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
            Spacer(minLength: 6)
            Text(value)
                .font(.system(size: 11, weight: .semibold).monospacedDigit())
                .foregroundStyle(RoomcutTokens.textPrimary(scheme))
        }
        .font(.system(size: 11))
        .padding(.horizontal, 9)
        .padding(.vertical, 6)
        .background(RoomcutTokens.opaqueGlass(scheme).opacity(0.45), in: RoundedRectangle(cornerRadius: 7))
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(title) \(value)")
    }

    private var limiterRow: some View {
        HStack {
            Text(L("Limiter", "Limiter", "リミッター", "Limiteur", "Limiter"))
                .foregroundStyle(RoomcutTokens.textSecondary(scheme))
            Spacer()
            if limiterActive {
                Text(limiterDbLabel(meters.displayLimiterGRDb))
                    .monospacedDigit()
                    .foregroundStyle(RoomcutTokens.amber)
            } else {
                Text(L("Safe", "Safe", "安全", "Sûr", "Sicher")).foregroundStyle(RoomcutTokens.green)
            }
        }
        .font(.callout)
        .accessibilityElement(children: .combine)
        .accessibilityLabel(L("리미터", "Limiter", "リミッター", "Limiteur", "Limiter"))
        .accessibilityValue(limiterActive
            ? L("게인 리덕션", "Gain reduction", "ゲインリダクション", "Réduction de gain", "Pegelreduktion")
              + " \(limiterDbAccessibilityLabel(meters.displayLimiterGRDb))"
            : L("안전, 관여 없음", "Safe, not engaged", "安全、未作動", "Sûr, non activé", "Sicher, nicht aktiv"))
    }

    private func gainSlider(title: String, value: Binding<Double>,
                            range: ClosedRange<Double>) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).foregroundStyle(RoomcutTokens.textSecondary(scheme))
                Spacer()
                Text(signedDbLabel(value.wrappedValue)).monospacedDigit()
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            }
            .font(.callout)
            Slider(value: Binding(
                get: { value.wrappedValue },
                set: { value.wrappedValue = $0; model.schedulePushParams() }
            ), in: range)
            .tint(RoomcutTokens.blue(scheme))
            .accessibilityLabel(title)
            .accessibilityValue(signedDbAccessibilityLabel(value.wrappedValue))
        }
    }

    private func signedDbLabel(_ value: Double) -> String {
        abs(value) < 0.05 ? "0.0 dB" : String(format: "%+.1f dB", value)
    }

    private func signedDbAccessibilityLabel(_ value: Double) -> String {
        let unit = L("데시벨", "decibels", "デシベル", "décibels", "Dezibel")
        return abs(value) < 0.05 ? "0.0 \(unit)" : "\(String(format: "%+.1f", value)) \(unit)"
    }

    private func limiterDbLabel(_ value: Float) -> String {
        abs(value) < 0.05 ? "0.0 dB" : String(format: "−%.1f dB", abs(Double(value)))
    }

    private func limiterDbAccessibilityLabel(_ value: Float) -> String {
        let unit = L("데시벨", "decibels", "デシベル", "décibels", "Dezibel")
        return abs(value) < 0.05 ? "0.0 \(unit)" : "\(String(format: "%.1f", abs(Double(value)))) \(unit)"
    }

}

private struct SpectrumBars: View {
    let values: [Float]
    let accent: Color
    let scheme: ColorScheme
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    private static let chartHeight: CGFloat = 84
    private static let verticalPadding: CGFloat = 6
    private static let horizontalPadding: CGFloat = 2
    private static let spacing: CGFloat = 3

    var body: some View {
        let bars = normalizedBars
        GeometryReader { geo in
            let innerWidth = max(1, geo.size.width - Self.horizontalPadding * 2)
            let innerHeight = max(1, geo.size.height - Self.verticalPadding * 2)
            let width = max(2, (innerWidth - Self.spacing * CGFloat(max(0, bars.count - 1))) / CGFloat(max(1, bars.count)))
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(RoomcutTokens.opaqueGlass(scheme).opacity(0.35))
                HStack(alignment: .bottom, spacing: Self.spacing) {
                    ForEach(Array(bars.enumerated()), id: \.offset) { _, value in
                        SpectrumBar(value: value,
                                    width: width,
                                    maxHeight: innerHeight,
                                    accent: accent)
                    }
                }
                .padding(.horizontal, Self.horizontalPadding)
                .padding(.vertical, Self.verticalPadding)
            }
            .frame(width: geo.size.width, height: geo.size.height)
        }
        .frame(height: Self.chartHeight)
        .fixedSize(horizontal: false, vertical: true)
        .animation(reduceMotion ? nil : .easeInOut(duration: 0.46), value: bars)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Spectrum")
    }

    private var normalizedBars: [Float] {
        let raw = values.isEmpty
            ? Array(repeating: Float(0), count: RoomcutAnalysisSnapshot.spectrumBinCount)
            : Array(values.prefix(RoomcutAnalysisSnapshot.spectrumBinCount))
        if raw.count == RoomcutAnalysisSnapshot.spectrumBinCount {
            return raw.map { min(1, max(0, $0)) }
        }
        return raw + Array(repeating: Float(0), count: RoomcutAnalysisSnapshot.spectrumBinCount - raw.count)
    }
}

private struct SpectrumBar: View {
    let value: Float
    let width: CGFloat
    let maxHeight: CGFloat
    let accent: Color

    var body: some View {
        let clamped = CGFloat(min(1, max(0, value)))
        let height = max(3, maxHeight * clamped)
        RoundedRectangle(cornerRadius: min(3, width / 2))
            .fill(accent.opacity(0.30 + Double(clamped) * 0.65))
            .frame(width: width, height: height)
            .frame(height: maxHeight, alignment: .bottom)
    }
}
