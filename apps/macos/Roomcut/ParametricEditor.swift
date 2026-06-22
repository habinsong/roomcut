//
// ParametricEditor.swift — the Advanced tab's Parametric EQ editor (Phase 7+).
//
// N independent biquad bands (enable, type, frequency, gain, Q) that stack after
// the 10-band graphic EQ in the engine. The curve at the top is the summed
// magnitude response of the enabled bands (an honest |H| computed with the same
// RBJ coefficients as the engine's Biquad), not a measured spectrum.
//
// Design (per Apple HIG / Liquid Glass guidance): the curve is flat *content*;
// Liquid Glass is reserved for the active band cards. Enabled bands rise to a
// tinted glass card with full controls; disabled bands collapse to one quiet
// row, so the list stays short and scrolls smoothly. The curve and every card
// are `Equatable`, so the model's 30 Hz meter/poll updates never re-run them —
// that was the source of the scroll jank and "heavy" feel.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct ParametricEditor: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    // Which cards are expanded (showing their controls). Independent of enabled:
    // a card opens ONLY when tapped; the toggle just engages the band. Default
    // (and newly-revealed bands) stay collapsed.
    @State private var expanded: Set<Int> = []

    private var accent: Color { RoomcutTokens.blue(scheme) }

    // Progressive reveal (n+1): show the enabled bands plus one spare slot to add
    // the next — never all six at once.
    private var visibleCount: Int {
        let lastEnabled = model.parametric.lastIndex(where: { $0.enabled }) ?? -1
        return min(model.parametric.count, max(1, lastEnabled + 2))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if model.status.reachable && !model.parametricAvailable {
                Text(L("현재 실행 중인 엔진이 Parametric EQ를 지원하지 않습니다.",
                       "The running engine does not support Parametric EQ.",
                       "実行中のエンジンは Parametric EQ に対応していません。",
                       "Le moteur en cours d'exécution ne prend pas en charge l'EQ paramétrique.",
                       "Die laufende Engine unterstützt parametrischen EQ nicht."))
                    .font(.system(size: 11))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
            }

            // Magnitude response — flat content, not glass (HIG).
            ParametricCurve(bands: model.parametric, accent: accent, scheme: scheme)
                .equatable()
                .frame(height: 96)
                .frame(maxWidth: .infinity)
                .background(
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .fill(scheme == .dark ? Color.white.opacity(0.05) : Color.black.opacity(0.035)))
                .overlay(
                    RoundedRectangle(cornerRadius: 16, style: .continuous)
                        .strokeBorder(RoomcutTokens.textTertiary(scheme).opacity(0.14), lineWidth: 0.5))

            // Band cards — the control layer gets the Liquid Glass. Grouped in a
            // container so the active cards share one sampling region (consistent
            // material + cheaper to render).
            GlassEffectContainer(spacing: 9) {
                VStack(spacing: 9) {
                    ForEach(Array(0..<visibleCount), id: \.self) { i in
                        ParametricBandCard(
                            index: i,
                            band: model.parametric[i],
                            expanded: expanded.contains(i),
                            accent: accent,
                            scheme: scheme,
                            onTapHeader: { toggleExpanded(i) },
                            onEnabled:   { setEnabled(i, $0) },
                            onType:      { model.setParametricType(i, $0) },
                            onFreq:      { model.setParametricFreq(i, $0) },
                            onGain:      { model.setParametricGain(i, $0) },
                            onQ:         { model.setParametricQ(i, $0) }
                        )
                        .equatable()
                        .transition(.opacity.combined(with: .move(edge: .top)))
                    }
                }
            }
        }
        .disabled(!model.parametricAvailable)
    }

    private func toggleExpanded(_ i: Int) {
        withAnimation(.smooth(duration: 0.26)) {
            if expanded.contains(i) { expanded.remove(i) } else { expanded.insert(i) }
        }
    }

    private func setEnabled(_ i: Int, _ on: Bool) {
        // Engaging a band reveals the next slot but leaves everything collapsed —
        // cards open only on tap.
        withAnimation(.smooth(duration: 0.3)) {
            model.setParametricEnabled(i, on)
        }
    }
}

// MARK: - Magnitude curve (flat content, Equatable on the bands)

private struct ParametricCurve: View, Equatable {
    let bands: [ParametricBand]
    let accent: Color
    let scheme: ColorScheme

    private let gainSpan = 24.0
    private let fMin = 20.0, fMax = 20000.0

    static func == (l: ParametricCurve, r: ParametricCurve) -> Bool {
        l.bands == r.bands && l.accent == r.accent && l.scheme == r.scheme
    }

    var body: some View {
        Canvas { ctx, size in
            let grid = RoomcutTokens.textTertiary(scheme)
            for g in [gainSpan, gainSpan / 2, 0, -gainSpan / 2, -gainSpan] {
                let y = yFor(g, size.height)
                var p = Path()
                p.move(to: CGPoint(x: 0, y: y)); p.addLine(to: CGPoint(x: size.width, y: y))
                ctx.stroke(p, with: .color(grid.opacity(g == 0 ? 0.28 : 0.10)),
                           lineWidth: g == 0 ? 1 : 0.5)
            }

            var path = Path()
            let n = 128
            for i in 0...n {
                let t = Double(i) / Double(n)
                let f = fMin * pow(fMax / fMin, t)
                let pt = CGPoint(x: t * size.width, y: yFor(totalMagnitudeDb(at: f), size.height))
                if i == 0 { path.move(to: pt) } else { path.addLine(to: pt) }
            }
            ctx.stroke(path, with: .color(accent.opacity(0.92)),
                       style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))

            for b in bands where b.enabled {
                let t = log(b.freqHz / fMin) / log(fMax / fMin)
                let g = b.kind.usesGain ? b.gainDb : 0
                let pt = CGPoint(x: max(0, min(1, t)) * size.width, y: yFor(g, size.height))
                let r: CGFloat = 4
                let dot = CGRect(x: pt.x - r, y: pt.y - r, width: 2 * r, height: 2 * r)
                ctx.fill(Path(ellipseIn: dot), with: .color(accent))
                ctx.stroke(Path(ellipseIn: dot),
                           with: .color(scheme == .dark ? .black.opacity(0.45) : .white), lineWidth: 1)
            }
        }
        .accessibilityHidden(true)
    }

    private func yFor(_ db: Double, _ h: CGFloat) -> CGFloat {
        let n = (db + gainSpan) / (2 * gainSpan)
        return h * (1 - CGFloat(max(0, min(1, n))))
    }

    private func totalMagnitudeDb(at f: Double) -> Double {
        var sum = 0.0
        for b in bands where b.enabled {
            sum += BiquadResponse.magnitudeDb(band: b, freqHz: f, fs: 48000)
        }
        return sum
    }
}

// MARK: - Band card (Liquid Glass when active; Equatable on the band)

private struct ParametricBandCard: View, Equatable {
    let index: Int
    let band: ParametricBand
    let expanded: Bool
    let accent: Color
    let scheme: ColorScheme
    let onTapHeader: () -> Void
    let onEnabled: (Bool) -> Void
    let onType: (Int) -> Void
    let onFreq: (Double) -> Void
    let onGain: (Double) -> Void
    let onQ: (Double) -> Void

    // Closures are stable (they re-capture the same model/index every render);
    // identity is the band + expand/theme, so the 30 Hz model ticks don't re-render us.
    static func == (l: ParametricBandCard, r: ParametricBandCard) -> Bool {
        l.index == r.index && l.band == r.band && l.expanded == r.expanded
            && l.accent == r.accent && l.scheme == r.scheme
    }

    private let fMin = 20.0, fMax = 20000.0
    private var ink: Color { band.enabled ? accent : RoomcutTokens.textTertiary(scheme) }

    var body: some View {
        VStack(spacing: expanded ? 12 : 0) {
            header
            if expanded {
                controls
                    .transition(.opacity.combined(with: .move(edge: .top)))
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, expanded ? 13 : 11)
        .frame(maxWidth: .infinity, alignment: .leading)
        .modifier(BandCardSurface(enabled: band.enabled, accent: accent, scheme: scheme))
    }

    private var header: some View {
        HStack(spacing: 10) {
            // Tap target for expand/collapse — everything except the toggle.
            HStack(spacing: 10) {
                Text("\(index + 1)")
                    .font(.system(size: 11, weight: .semibold, design: .rounded))
                    .foregroundStyle(ink)
                    .frame(width: 21, height: 21)
                    .background(Circle().fill(ink.opacity(0.15)))

                if expanded {
                    Menu {
                        ForEach(ParametricBand.Kind.allCases) { k in
                            Button { onType(k.rawValue) } label: {
                                if k.rawValue == band.type { Label(k.label, systemImage: "checkmark") }
                                else { Text(k.label) }
                            }
                        }
                    } label: {
                        HStack(spacing: 4) {
                            Text(band.kind.label).font(.system(size: 13, weight: .medium))
                            Image(systemName: "chevron.up.chevron.down").font(.system(size: 8, weight: .semibold))
                        }
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                    }
                    .menuStyle(.button)
                    .buttonStyle(.plain)
                    .menuIndicator(.hidden)
                    .fixedSize()
                    .accessibilityLabel(L("밴드 \(index + 1) 필터 종류", "Band \(index + 1) filter type",
                                          "バンド \(index + 1) フィルタ種類", "Bande \(index + 1) type de filtre",
                                          "Band \(index + 1) Filtertyp"))
                } else {
                    Text(band.kind.label)
                        .font(.system(size: 13, weight: .medium))
                        .foregroundStyle(band.enabled ? RoomcutTokens.textPrimary(scheme)
                                         : RoomcutTokens.textSecondary(scheme))
                }

                Spacer(minLength: 8)

                if !expanded {
                    Text(summary)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                        .lineLimit(1)
                }

                Image(systemName: "chevron.down")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                    .rotationEffect(.degrees(expanded ? 0 : -90))
            }
            .contentShape(Rectangle())
            .onTapGesture { onTapHeader() }

            Toggle("", isOn: Binding(get: { band.enabled }, set: { onEnabled($0) }))
                .labelsHidden()
                .toggleStyle(.switch)
                .controlSize(.small)
                .tint(accent)
                .accessibilityLabel(L("밴드 \(index + 1) 사용", "Band \(index + 1) enable",
                                      "バンド \(index + 1) 有効", "Bande \(index + 1) activer",
                                      "Band \(index + 1) aktivieren"))
        }
    }

    private var controls: some View {
        VStack(spacing: 11) {
            sliderRow(L("Frequency", "Frequency", "周波数", "Fréquence", "Frequenz"), value: freqLabel(band.freqHz),
                      binding: Binding(get: { freqNorm }, set: { onFreq(normToFreq($0)) }),
                      range: 0...1)
            if band.kind.usesGain {
                sliderRow(L("Gain", "Gain", "ゲイン", "Gain", "Verstärkung"), value: gainLabel(band.gainDb),
                          binding: Binding(get: { band.gainDb }, set: { onGain($0) }),
                          range: -24...24)
            }
            sliderRow("Q", value: String(format: "%.2f", band.q),
                      binding: Binding(get: { band.q }, set: { onQ($0) }),
                      range: 0.1...12)
        }
    }

    private func sliderRow(_ title: String, value: String,
                           binding: Binding<Double>, range: ClosedRange<Double>) -> some View {
        VStack(spacing: 3) {
            HStack {
                Text(title)
                    .font(.system(size: 11, weight: .medium))
                    .foregroundStyle(RoomcutTokens.textSecondary(scheme))
                Spacer()
                Text(value)
                    .font(.system(size: 11, weight: .medium, design: .monospaced))
                    .foregroundStyle(RoomcutTokens.textPrimary(scheme))
            }
            Slider(value: binding, in: range)
                .controlSize(.small)
                .tint(accent)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(title)
        .accessibilityValue(value)
    }

    private var summary: String {
        let f = freqLabel(band.freqHz)
        return band.kind.usesGain ? "\(f) · \(gainLabel(band.gainDb))" : f
    }
    private var freqNorm: Double { log(band.freqHz / fMin) / log(fMax / fMin) }
    private func normToFreq(_ t: Double) -> Double { fMin * pow(fMax / fMin, t) }
    private func freqLabel(_ f: Double) -> String {
        f >= 1000 ? String(format: "%.2f kHz", f / 1000) : String(format: "%.0f Hz", f)
    }
    private func gainLabel(_ value: Double) -> String {
        abs(value) < 0.05 ? "0.0 dB" : String(format: "%+.1f dB", value)
    }
}

// Liquid Glass for an active band; a quiet flat fill when off or under Reduce
// Transparency. Glass goes on the control card (not the curve), per the HIG.
private struct BandCardSurface: ViewModifier {
    let enabled: Bool
    let accent: Color
    let scheme: ColorScheme
    @Environment(\.accessibilityReduceTransparency) private var reduceTransparency

    func body(content: Content) -> some View {
        let shape = RoundedRectangle(cornerRadius: 16, style: .continuous)
        if enabled && !reduceTransparency {
            content.glassEffect(.regular.tint(accent.opacity(0.10)), in: shape)
        } else {
            content
                .background(shape.fill(scheme == .dark
                    ? Color.white.opacity(enabled ? 0.06 : 0.03)
                    : Color.black.opacity(enabled ? 0.05 : 0.025)))
                .overlay(shape.strokeBorder(RoomcutTokens.textTertiary(scheme).opacity(0.12), lineWidth: 0.5))
        }
    }
}

// Swift port of the engine's RBJ Biquad magnitude (core/dsp/Biquad.hpp) so the
// editor curve matches what the engine actually applies. For display only.
enum BiquadResponse {
    static func magnitudeDb(band: ParametricBand, freqHz: Double, fs: Double) -> Double {
        // A bell/shelf at 0 dB is a pass-through (matches ParametricEQ::rebuildBand).
        if band.kind.usesGain && band.gainDb == 0 { return 0 }
        var w0 = 2.0 * .pi * (band.freqHz / fs)
        if w0 >= .pi { w0 = .pi * 0.999 }
        let cosw0 = cos(w0), sinw0 = sin(w0)
        let q = max(band.q, 1e-4)
        let alpha = sinw0 / (2.0 * q)
        let A = pow(10.0, band.gainDb / 40.0)
        var b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0
        switch band.kind {
        case .bell:
            b0 = 1 + alpha * A; b1 = -2 * cosw0; b2 = 1 - alpha * A
            a0 = 1 + alpha / A; a1 = -2 * cosw0; a2 = 1 - alpha / A
        case .lowShelf:
            let s = sqrt(A), ta = 2 * s * alpha
            b0 = A * ((A + 1) - (A - 1) * cosw0 + ta); b1 = 2 * A * ((A - 1) - (A + 1) * cosw0)
            b2 = A * ((A + 1) - (A - 1) * cosw0 - ta); a0 = (A + 1) + (A - 1) * cosw0 + ta
            a1 = -2 * ((A - 1) + (A + 1) * cosw0); a2 = (A + 1) + (A - 1) * cosw0 - ta
        case .highShelf:
            let s = sqrt(A), ta = 2 * s * alpha
            b0 = A * ((A + 1) + (A - 1) * cosw0 + ta); b1 = -2 * A * ((A - 1) + (A + 1) * cosw0)
            b2 = A * ((A + 1) + (A - 1) * cosw0 - ta); a0 = (A + 1) - (A - 1) * cosw0 + ta
            a1 = 2 * ((A - 1) - (A + 1) * cosw0); a2 = (A + 1) - (A - 1) * cosw0 - ta
        case .highPass:
            b0 = (1 + cosw0) / 2; b1 = -(1 + cosw0); b2 = (1 + cosw0) / 2
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha
        case .lowPass:
            b0 = (1 - cosw0) / 2; b1 = 1 - cosw0; b2 = (1 - cosw0) / 2
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha
        case .notch:
            b0 = 1; b1 = -2 * cosw0; b2 = 1
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha
        }
        let nb0 = b0 / a0, nb1 = b1 / a0, nb2 = b2 / a0, na1 = a1 / a0, na2 = a2 / a0
        let w = 2.0 * .pi * (freqHz / fs)
        let cw = cos(w), sw = sin(w), cw2 = cos(2 * w), sw2 = sin(2 * w)
        let numRe = nb0 + nb1 * cw + nb2 * cw2, numIm = -(nb1 * sw + nb2 * sw2)
        let denRe = 1 + na1 * cw + na2 * cw2, denIm = -(na1 * sw + na2 * sw2)
        let num = sqrt(numRe * numRe + numIm * numIm)
        let den = sqrt(denRe * denRe + denIm * denIm)
        let m = den > 0 ? num / den : 0
        return 20.0 * log10(max(m, 1e-6))
    }
}
