import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The full-window backdrop behind Now Playing: the mesh animation's control
// points and the palette drawn from the current artwork.
// Full-window mesh animation control points (3×3), shared by the Layer-0 wash.
enum NowPlayingMeshPoints {
    static func points(_ phase: TimeInterval) -> [SIMD2<Float>] {
        let t = Float(phase)
        func wave(_ base: Float, _ amplitude: Float, _ speed: Float, _ offset: Float = 0) -> Float {
            base + amplitude * sin(t * speed + offset)
        }
        return [
            SIMD2<Float>(0, 0),
            SIMD2<Float>(wave(0.50, 0.08, 0.62), 0),
            SIMD2<Float>(1, 0),
            SIMD2<Float>(0, wave(0.50, 0.07, 0.74, 0.7)),
            SIMD2<Float>(wave(0.50, 0.12, 0.45, 1.1), wave(0.50, 0.12, 0.58, 0.35)),
            SIMD2<Float>(1, wave(0.50, 0.07, 0.80, 1.7)),
            SIMD2<Float>(0, 1),
            SIMD2<Float>(wave(0.50, 0.08, 0.66, 2.1), 1),
            SIMD2<Float>(1, 1),
        ]
    }
}

enum NowPlayingBackdropPalette {
    static func tint(_ artworkColor: NSColor?, dark: Bool) -> Color {
        Color(nsColor: baseColor(artworkColor, dark: dark))
    }

    static func gradientColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [Color] {
        let colors = seedColors(artworkColor: artworkColor, artworkPalette: artworkPalette, dark: dark)
        let base = colors[0]
        let soft = colors[1].blended(withFraction: dark ? 0.18 : 0.32, of: .white) ?? colors[1]
        let deep = colors[2].blended(withFraction: dark ? 0.34 : 0.18, of: .black) ?? colors[2]
        return [Color(nsColor: soft), Color(nsColor: base), Color(nsColor: deep)]
    }

    static func meshColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [Color] {
        let colors = seedColors(artworkColor: artworkColor, artworkPalette: artworkPalette, dark: dark)
        let deep = colors[0].blended(withFraction: dark ? 0.34 : 0.20, of: .black) ?? colors[0]
        let bright = colors[1].blended(withFraction: dark ? 0.14 : 0.30, of: .white) ?? colors[1]
        let soft = colors[2].blended(withFraction: dark ? 0.10 : 0.24, of: .white) ?? colors[2]
        let shadow = colors[3].blended(withFraction: dark ? 0.28 : 0.16, of: .black) ?? colors[3]

        return [
            deep, colors[1], bright,
            colors[2], colors[0], colors[3],
            soft, colors[4], shadow,
        ].map { Color(nsColor: $0) }
    }

    private static func seedColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [NSColor] {
        var colors = (artworkPalette ?? []).compactMap { $0.usingColorSpace(.deviceRGB) }
        if let base = artworkColor?.usingColorSpace(.deviceRGB) {
            colors.insert(base, at: 0)
        }
        if colors.isEmpty {
            colors = [baseColor(nil, dark: dark)]
        }
        while colors.count < 5 {
            let nextBase = colors[colors.count % max(1, colors.count)]
            let blendTarget: NSColor = colors.count.isMultiple(of: 2) ? .white : .black
            colors.append(nextBase.blended(withFraction: dark ? 0.18 : 0.24, of: blendTarget) ?? nextBase)
        }
        return Array(colors.prefix(5))
    }

    private static func baseColor(_ color: NSColor?, dark: Bool) -> NSColor {
        if let rgb = color?.usingColorSpace(.deviceRGB) {
            return rgb
        }
        let white: CGFloat = dark ? 0.72 : 0.58
        return NSColor(deviceRed: white, green: white, blue: white, alpha: 1)
    }
}
