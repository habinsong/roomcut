//
// EqMiniCurve.swift — a small read-only EQ summary curve, drawn from the live
// 10-band gains. Used by the Advanced controls overview. No interaction.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct EqMiniCurve: View, Equatable {
    let gains: [Double]
    let accent: Color
    let scheme: ColorScheme

    // Equatable so `.equatable()` lets SwiftUI skip the Canvas redraw on the
    // model's 30 Hz meter/poll ticks (the gains don't change then).
    static func == (l: EqMiniCurve, r: EqMiniCurve) -> Bool {
        l.gains == r.gains && l.accent == r.accent && l.scheme == r.scheme
    }

    var body: some View {
        Canvas { ctx, size in
            guard gains.count == EqBands.count else { return }
            let mid = size.height / 2

            // 0 dB baseline.
            var base = Path()
            base.move(to: CGPoint(x: 0, y: mid))
            base.addLine(to: CGPoint(x: size.width, y: mid))
            ctx.stroke(base, with: .color(RoomcutTokens.textTertiary(scheme).opacity(0.22)),
                       lineWidth: 1)

            // Points across the band centers.
            let pts: [CGPoint] = (0..<EqBands.count).map { i in
                CGPoint(x: EqBands.normalizedX(i) * size.width,
                        y: size.height * (1 - EqBands.normalizedY(gainDb: gains[i])))
            }
            guard pts.count > 1 else { return }

            // Smooth Catmull-Rom → cubic Bézier curve.
            let curve = smoothPath(pts)

            // Soft fill under the curve.
            var fill = curve
            fill.addLine(to: CGPoint(x: pts.last!.x, y: size.height))
            fill.addLine(to: CGPoint(x: pts.first!.x, y: size.height))
            fill.closeSubpath()
            ctx.fill(fill, with: .linearGradient(
                Gradient(colors: [accent.opacity(0.22), accent.opacity(0.02)]),
                startPoint: CGPoint(x: 0, y: 0),
                endPoint: CGPoint(x: 0, y: size.height)))

            ctx.stroke(curve, with: .color(accent),
                       style: StrokeStyle(lineWidth: 2.5, lineCap: .round, lineJoin: .round))
        }
        .padding(.horizontal, 4)
        .background(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .fill(scheme == .dark ? Color.white.opacity(0.04) : Color.black.opacity(0.03))
        )
        .accessibilityHidden(true)
    }

    // Catmull-Rom spline through the points, emitted as cubic Bézier segments.
    private func smoothPath(_ p: [CGPoint]) -> Path {
        var path = Path()
        path.move(to: p[0])
        for i in 0..<(p.count - 1) {
            let p0 = p[max(0, i - 1)]
            let p1 = p[i]
            let p2 = p[i + 1]
            let p3 = p[min(p.count - 1, i + 2)]
            let c1 = CGPoint(x: p1.x + (p2.x - p0.x) / 6.0,
                             y: p1.y + (p2.y - p0.y) / 6.0)
            let c2 = CGPoint(x: p2.x - (p3.x - p1.x) / 6.0,
                             y: p2.y - (p3.y - p1.y) / 6.0)
            path.addCurve(to: p2, control1: c1, control2: c2)
        }
        return path
    }
}

// The full processed EQ response: 10-band + Parametric summed as real RBJ-biquad
// magnitude (same math the engine uses, via BiquadResponse), offset by preamp. The
// 0 dB baseline doubles as the limiter ceiling. (Space is M/S width, not a frequency
// magnitude, so it lives in the Field view — not on this curve.)
struct EqResponseCurve: View, Equatable {
    let eqGains: [Double]
    let parametric: [ParametricBand]
    let preampDb: Double
    let accent: Color
    let scheme: ColorScheme

    static func == (l: EqResponseCurve, r: EqResponseCurve) -> Bool {
        l.eqGains == r.eqGains && l.parametric == r.parametric
            && l.preampDb == r.preampDb && l.accent == r.accent && l.scheme == r.scheme
    }

    private let fLo = 20.0, fHi = 20_000.0, dbRange = 18.0

    var body: some View {
        Canvas { ctx, size in
            let mid = size.height / 2
            func yFor(_ db: Double) -> CGFloat {
                let clamped = max(-dbRange, min(dbRange, db))
                return size.height * (1 - CGFloat((clamped + dbRange) / (2 * dbRange)))
            }
            var base = Path()
            base.move(to: CGPoint(x: 0, y: mid)); base.addLine(to: CGPoint(x: size.width, y: mid))
            ctx.stroke(base, with: .color(RoomcutTokens.textTertiary(scheme).opacity(0.22)), lineWidth: 1)

            let n = 140
            var curve = Path()
            for i in 0...n {
                let t = Double(i) / Double(n)
                let f = fLo * pow(fHi / fLo, t)
                let pt = CGPoint(x: t * size.width, y: yFor(magnitudeDb(at: f)))
                if i == 0 { curve.move(to: pt) } else { curve.addLine(to: pt) }
            }
            var fill = curve
            fill.addLine(to: CGPoint(x: size.width, y: size.height))
            fill.addLine(to: CGPoint(x: 0, y: size.height))
            fill.closeSubpath()
            ctx.fill(fill, with: .linearGradient(
                Gradient(colors: [accent.opacity(0.22), accent.opacity(0.02)]),
                startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            ctx.stroke(curve, with: .color(accent),
                       style: StrokeStyle(lineWidth: 2.5, lineCap: .round, lineJoin: .round))
        }
        .padding(.horizontal, 4)
        .background(RoundedRectangle(cornerRadius: 12, style: .continuous)
            .fill(scheme == .dark ? Color.white.opacity(0.04) : Color.black.opacity(0.03)))
        .accessibilityHidden(true)
    }

    private func magnitudeDb(at f: Double) -> Double {
        var sum = preampDb
        for i in 0..<min(eqGains.count, EqBands.count) where eqGains[i] != 0 {
            sum += BiquadResponse.magnitudeDb(
                band: ParametricBand(enabled: true, type: 0, freqHz: EqBands.centersHz[i],
                                     gainDb: eqGains[i], q: 1.41),
                freqHz: f, fs: 48000)
        }
        for b in parametric where b.enabled {
            sum += BiquadResponse.magnitudeDb(band: b, freqHz: f, fs: 48000)
        }
        return sum
    }
}
