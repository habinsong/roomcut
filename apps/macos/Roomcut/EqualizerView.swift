//
// EqualizerView.swift — the Advanced tab's 10-band graphic EQ, styled to match
// the design mockup: frequency labels across the top, a draggable fader bank
// with a connecting control curve, and per-band gain values along the bottom.
//
// The curve is an *editable control curve* (the 10 band gains joined by line
// segments), not a measured spectrum. Dragging a band column sets that band's
// gain directly from the pointer height; the engine write is debounced into a
// single SET_PARAMS (see RoomcutViewModel.schedulePushParams).
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct EqualizerView: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme

    private let faderHeight: CGFloat = 132
    private var accent: Color { RoomcutTokens.blue(scheme) }

    var body: some View {
        VStack(spacing: 8) {
            frequencyLabels
            faderBank
            valueLabels
        }
        .frame(maxWidth: .infinity)
        .disabled(!model.status.reachable)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(L("10밴드 EQ (편집 가능한 제어 곡선)", "10-band EQ (editable control curve)",
                              "10バンド EQ(編集可能な制御カーブ)", "Égaliseur 10 bandes (courbe de contrôle modifiable)",
                              "10-Band-EQ (bearbeitbare Steuerkurve)"))
    }

    private var frequencyLabels: some View {
        HStack(spacing: 0) {
            ForEach(0..<EqBands.count, id: \.self) { i in
                Text(EqBands.labels[i])
                    .font(.system(size: 10, weight: .medium))
                    .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                    .frame(maxWidth: .infinity)
            }
        }
    }

    private var valueLabels: some View {
        HStack(spacing: 0) {
            ForEach(0..<EqBands.count, id: \.self) { i in
                Text(gainLabel(model.eqGainsDb[i]))
                    .font(.system(size: 10).monospacedDigit())
                    .foregroundStyle(abs(model.eqGainsDb[i]) < 0.05
                                     ? RoomcutTokens.textTertiary(scheme)
                                     : RoomcutTokens.textSecondary(scheme))
                    .frame(maxWidth: .infinity)
            }
        }
    }

    private var faderBank: some View {
        GeometryReader { geo in
            let h = geo.size.height
            ZStack {
                // Grid + editable control curve + handles, in one Equatable Canvas
                // so the model's 30 Hz ticks don't redraw it (only gain edits do).
                EqControlCurve(gains: model.eqGainsDb, accent: accent, scheme: scheme)
                    .equatable()
                    .allowsHitTesting(false)

                // Per-band transparent drag columns (direct-position editing).
                HStack(spacing: 0) {
                    ForEach(0..<EqBands.count, id: \.self) { i in
                        bandColumn(i, height: h)
                    }
                }
            }
        }
        .frame(height: faderHeight)
    }

    private func bandColumn(_ i: Int, height: CGFloat) -> some View {
        Rectangle()
            .fill(Color.clear)
            .contentShape(Rectangle())
            .frame(maxWidth: .infinity)
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { g in setGain(i, fromY: g.location.y, height: height) }
                    .onEnded { _ in model.schedulePushParams() }
            )
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("\(EqBands.labels[i]) "
                                + L("헤르츠 밴드", "hertz band", "ヘルツ バンド", "bande hertz", "Hertz-Band"))
            .accessibilityValue(gainAccessibilityLabel(model.eqGainsDb[i]))
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment:
                    model.eqGainsDb[i] = min(EqBands.gainRange.upperBound,
                                             model.eqGainsDb[i] + EqBands.gainStep)
                case .decrement:
                    model.eqGainsDb[i] = max(EqBands.gainRange.lowerBound,
                                             model.eqGainsDb[i] - EqBands.gainStep)
                default: break
                }
                model.schedulePushParams()
            }
    }

    // MARK: Geometry helpers (even column spacing, ±24 dB range)

    private func setGain(_ i: Int, fromY y: CGFloat, height: CGFloat) {
        let n = max(0, min(1, 1 - y / height))
        let raw = EqBands.gainRange.lowerBound
                + Double(n) * (EqBands.gainRange.upperBound - EqBands.gainRange.lowerBound)
        let snapped = (raw / EqBands.gainStep).rounded() * EqBands.gainStep
        model.eqGainsDb[i] = min(EqBands.gainRange.upperBound,
                                 max(EqBands.gainRange.lowerBound, snapped))
    }

    private func gainLabel(_ value: Double) -> String {
        abs(value) < 0.05 ? "0.0" : String(format: "%+.1f", value)
    }

    private func gainAccessibilityLabel(_ value: Double) -> String {
        let unit = L("데시벨", "decibels", "デシベル", "décibels", "Dezibel")
        return abs(value) < 0.05 ? "0.0 \(unit)" : "\(String(format: "%+.1f", value)) \(unit)"
    }
}

// Grid + control curve + handles for the 10-band EQ, drawn in a single Canvas.
// Equatable on the gains/theme so SwiftUI skips the redraw on the model's 30 Hz
// meter/poll ticks — only an actual gain edit re-runs it.
private struct EqControlCurve: View, Equatable {
    let gains: [Double]
    let accent: Color
    let scheme: ColorScheme

    static func == (l: EqControlCurve, r: EqControlCurve) -> Bool {
        l.gains == r.gains && l.accent == r.accent && l.scheme == r.scheme
    }

    var body: some View {
        Canvas { ctx, size in
            // +12 / 0 / −12 reference lines.
            for g in [24.0, 0.0, -24.0] {
                let y = yFor(g, size.height)
                var p = Path()
                p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: size.width, y: y))
                ctx.stroke(p, with: .color(RoomcutTokens.textTertiary(scheme).opacity(g == 0 ? 0.3 : 0.14)),
                           lineWidth: 1)
            }

            var path = Path()
            for i in 0..<EqBands.count {
                let p = point(i, size)
                if i == 0 { path.move(to: p) } else { path.addLine(to: p) }
            }
            ctx.stroke(path, with: .color(accent.opacity(0.85)),
                       style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))

            for i in 0..<EqBands.count {
                let p = point(i, size)
                let r: CGFloat = 4.5
                let dot = CGRect(x: p.x - r, y: p.y - r, width: 2 * r, height: 2 * r)
                ctx.fill(Path(ellipseIn: dot), with: .color(accent))
                ctx.stroke(Path(ellipseIn: dot),
                           with: .color(scheme == .dark ? .black.opacity(0.4) : .white), lineWidth: 1)
            }
        }
    }

    private func point(_ i: Int, _ size: CGSize) -> CGPoint {
        let x = (CGFloat(i) + 0.5) / CGFloat(EqBands.count) * size.width
        return CGPoint(x: x, y: yFor(gains[i], size.height))
    }

    private func yFor(_ gain: Double, _ height: CGFloat) -> CGFloat {
        let n = (gain - EqBands.gainRange.lowerBound)
              / (EqBands.gainRange.upperBound - EqBands.gainRange.lowerBound)
        return height * (1 - CGFloat(n))
    }
}
