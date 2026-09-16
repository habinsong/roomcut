import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The halo. This is the ring a listener actually sees behind the artwork — the
// background layer is a separate, much softer wash.
// MARK: - Aurora ring (the Now Playing halo)

struct AuroraRing: View {
    let diameter: CGFloat
    let dark: Bool
    let artworkColor: NSColor?

    @State private var transitionFrom: AuroraBaseColor?
    @State private var transitionTo: AuroraBaseColor?
    @State private var transitionStart = Date.distantPast
    // Animate only DURING a colour transition; pause the timeline when settled so
    // the per-frame blur stops (idle CPU → ~0). Re-armed on each track change.
    @State private var animating = false

    private let transitionDuration: TimeInterval = 1.15

    private var rimTargetKey: String {
        "\(dark):\(Self.colorKey(artworkColor))"
    }

    private var targetBaseColor: AuroraBaseColor {
        Self.baseColor(artworkColor, dark: dark)
    }

    private func rim(for base: AuroraBaseColor, phase: Double) -> AngularGradient {
        let accent = base.nsColor
        let deep = accent.blended(withFraction: 0.22, of: .black) ?? accent
        let bright = accent.blended(withFraction: 0.18, of: .white) ?? accent
        return AngularGradient(
            colors: [
                Color(nsColor: deep), Color(nsColor: accent),
                Color(nsColor: bright), Color(nsColor: accent),
                Color(nsColor: deep), Color(nsColor: accent),
            ],
            center: .center,
            angle: .degrees(132 + phase * 8)
        )
    }

    private func rimCircle(for base: AuroraBaseColor, phase: Double) -> some View {
        Circle()
            .stroke(rim(for: base, phase: phase), lineWidth: dark ? 34 : 30)
            .frame(width: diameter - 8, height: diameter - 8)
            .blur(radius: dark ? 18 : 15)
            .opacity(dark ? 0.58 : 0.44)
    }

    var body: some View {
        TimelineView(.animation(paused: !animating)) { timeline in
            let progress = transitionProgress(at: timeline.date)
            let currentBase = currentBaseColor(at: timeline.date)

            ZStack {
                Circle()
                    .fill(.ultraThinMaterial)
                    .opacity(dark ? 0.45 : 0.65)
                    .frame(width: diameter, height: diameter)

                rimCircle(for: currentBase, phase: progress)

                Circle()
                    .strokeBorder(.white.opacity(dark ? 0.07 : 0.45), lineWidth: 1)
                    .frame(width: diameter, height: diameter)
            }
        }
        .accessibilityHidden(true)
        .allowsHitTesting(false)
        .onAppear {
            transitionFrom = targetBaseColor
            transitionTo = targetBaseColor
        }
        .onChange(of: rimTargetKey) {
            let now = Date()
            transitionFrom = currentBaseColor(at: now)
            transitionTo = targetBaseColor
            transitionStart = now
            animating = true
            // Settle (pause the timeline) once this transition has elapsed — unless
            // a newer track change has re-armed it in the meantime.
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: UInt64((transitionDuration + 0.1) * 1_000_000_000))
                if Date().timeIntervalSince(transitionStart) >= transitionDuration {
                    animating = false
                }
            }
        }
    }

    private func transitionProgress(at date: Date) -> Double {
        min(1, max(0, date.timeIntervalSince(transitionStart) / transitionDuration))
    }

    private func currentBaseColor(at date: Date) -> AuroraBaseColor {
        guard let from = transitionFrom, let to = transitionTo else { return targetBaseColor }
        return from.interpolated(to: to, progress: smoothstep(transitionProgress(at: date)))
    }

    private func smoothstep(_ value: Double) -> Double {
        value * value * (3 - 2 * value)
    }

    private static func baseColor(_ color: NSColor?, dark: Bool) -> AuroraBaseColor {
        if let rgb = color?.usingColorSpace(.deviceRGB) {
            return AuroraBaseColor(
                red: rgb.redComponent,
                green: rgb.greenComponent,
                blue: rgb.blueComponent,
                alpha: rgb.alphaComponent
            )
        }
        let white: CGFloat = dark ? 0.82 : 0.52
        return AuroraBaseColor(red: white, green: white, blue: white, alpha: 1)
    }

    private static func colorKey(_ color: NSColor?) -> String {
        guard let rgb = color?.usingColorSpace(.deviceRGB) else { return "nil" }
        return String(format: "%.3f:%.3f:%.3f:%.3f",
                      rgb.redComponent,
                      rgb.greenComponent,
                      rgb.blueComponent,
                      rgb.alphaComponent)
    }
}

struct AuroraBaseColor {
    let red: CGFloat
    let green: CGFloat
    let blue: CGFloat
    let alpha: CGFloat

    var nsColor: NSColor {
        NSColor(deviceRed: red, green: green, blue: blue, alpha: alpha)
    }

    func interpolated(to other: AuroraBaseColor, progress: Double) -> AuroraBaseColor {
        let t = CGFloat(progress)
        return AuroraBaseColor(
            red: red + (other.red - red) * t,
            green: green + (other.green - green) * t,
            blue: blue + (other.blue - blue) * t,
            alpha: alpha + (other.alpha - alpha) * t
        )
    }
}

// Artwork in the square art slot, letterbox-aware. Album covers are 1:1 and
// fill the square exactly; video thumbnails (YouTube etc.) are 16:9 and used to
// be centre-cropped, losing the left/right edges. Non-square art now renders
// the FULL image (fit) and fills the top/bottom bands with a blurred, scaled
// copy of itself — the blur-extension treatment video players use. Square art
// keeps the plain fast path (the fit layer would cover the frame anyway, so
// the blur would be invisible cost). Shared by every artwork slot (Card,
// Poster, compact card, menu-bar popover), so `blurRadius` scales down for the
// tiny thumbnails.
