//
// RoomcutBackgroundLayer.swift — Layer 0 of the shell (the bottom-most view the
// whole window, including the glass chrome, sits over and samples).
//
// CRITICAL: this layer is pure *content* (gradients / a blurred image / a mesh)
// with NO `Material`. The floating Liquid Glass chrome samples this layer; a
// `Material` here is itself a backdrop sampler, and nesting a material under
// glass breaks the chrome's rendering (learned the hard way). Keep it material-
// free — the glass supplies the blur-through on its own.
//
import SwiftUI
import AppKit
import MetalKit
import RoomcutCore
import RoomcutPresentationCore

// Decides when the Cover/Mesh wash is dark enough that the floating chrome (Now
// Playing text/controls AND the tab bar) should flip to WHITE — shared so both use
// the SAME judgement. Only matters in LIGHT mode (dark mode is already light ink).
//
// Uses WCAG relative luminance (sRGB-linearised, Rec.709 weights) instead of a naive
// average, so a saturated-but-dark blue/red registers as dark; plus a saturation
// bias, because vivid colours read better under white text even at mid-luminance.
enum NowPlayingInk {
    static func isDarkBackdrop(theme: RoomcutNowPlayingTheme, scheme: ColorScheme,
                               artworkColor: NSColor?, artworkPalette: [NSColor]?) -> Bool {
        guard scheme == .light else { return false }
        let color: NSColor?
        switch theme {
        case .halo:
            return false
        case .cover:
            // The wash is the artwork at ~0.82 opacity over the light base.
            color = artworkColor?.usingColorSpace(.deviceRGB)?
                .blended(withFraction: 0.18, of: NSColor(white: 0.94, alpha: 1))
        case .meshGradient:
            color = meshRepresentativeColor(artworkColor: artworkColor, artworkPalette: artworkPalette)
        }
        guard let c = color?.usingColorSpace(.deviceRGB) else { return false }
        return relLuminance(c) < (0.34 + 0.12 * c.saturationComponent)
    }

    // DARK-mode counterpart: when the Cover wash is so BRIGHT (a near-white album
    // cover at ~0.92 opacity) that the light ink disappears, flip the floating
    // chrome to BLACK. Only Cover can get that bright — dark-mode Mesh keeps its
    // tones capped dark (tahoeTones body ≤ 0.40 brightness) and Halo has no wash.
    // The saturation bias runs the OTHER way here: vivid colours read better under
    // white ink, so they must be brighter still before black takes over.
    static func isBrightBackdrop(theme: RoomcutNowPlayingTheme, scheme: ColorScheme,
                                 artworkColor: NSColor?, artworkPalette: [NSColor]?) -> Bool {
        guard scheme == .dark, theme == .cover else { return false }
        // The wash is the artwork at ~0.92 opacity over the dark base.
        guard let c = artworkColor?.usingColorSpace(.deviceRGB)?
            .blended(withFraction: 0.08, of: NSColor(white: 0.07, alpha: 1))?
            .usingColorSpace(.deviceRGB) else { return false }
        return relLuminance(c) > (0.62 + 0.10 * c.saturationComponent)
    }

    // The mid/deep tone the centred card sits over in light-mode Mesh (mirrors the
    // seed pick in tahoeTones — the most vivid palette colour, not a muddy average).
    static func meshRepresentativeColor(artworkColor: NSColor?, artworkPalette: [NSColor]?) -> NSColor? {
        guard let seed = meshSeed(artworkColor: artworkColor, artworkPalette: artworkPalette) else {
            return neutralMeshRepresentative()
        }
        return NSColor(hue: seed.hueComponent,
                       saturation: min(max(seed.saturationComponent, 0.45) * 0.93, 1),
                       brightness: 0.70, alpha: 1)
    }

    static func meshSeed(artworkColor: NSColor?, artworkPalette: [NSColor]?) -> NSColor? {
        let cand = (artworkPalette ?? [])
            .compactMap { $0.usingColorSpace(.deviceRGB) }
            .filter(isChromatic)
        return cand.max(by: { $0.saturationComponent * $0.brightnessComponent
                            < $1.saturationComponent * $1.brightnessComponent })
            ?? artworkColor?.usingColorSpace(.deviceRGB).flatMap { isChromatic($0) ? $0 : nil }
    }

    static func isChromatic(_ c: NSColor) -> Bool {
        c.saturationComponent >= 0.08 && c.brightnessComponent >= 0.08
    }

    static func neutralMeshRepresentative() -> NSColor {
        NSColor(white: 0.70, alpha: 1)
    }

    static func relLuminance(_ c: NSColor) -> CGFloat {
        func lin(_ v: CGFloat) -> CGFloat { v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * lin(c.redComponent) + 0.7152 * lin(c.greenComponent) + 0.0722 * lin(c.blueComponent)
    }
}

struct RoomcutBackgroundLayer: View {
    @Environment(\.colorScheme) private var scheme

    var theme: RoomcutNowPlayingTheme = .halo
    var artwork: NSImage? = nil
    var artworkColor: NSColor? = nil
    var artworkPalette: [NSColor]? = nil
    var playing: Bool = true
    // Shifts the mesh crest downward (B layout pushes the wave toward the lyrics).
    var meshCrestBias: CGFloat = 0
    // B layout: fill the band above the cover (incl. the top-bar area) with the
    // cover's top-edge colour so no wash shows above the album.
    var topTint: Color? = nil
    var topTintHeight: CGFloat = 0

    private var baseGradient: LinearGradient {
        let stops: [Color] = scheme == .dark
            ? [Color(hex: 0x0B0D12), Color(hex: 0x121521), Color(hex: 0x0C0E14)]
            : [Color(hex: 0xF1F1ED), Color(hex: 0xF7F7F4), Color(hex: 0xECECE7)]
        return LinearGradient(colors: stops, startPoint: .top, endPoint: .bottom)
    }

    var body: some View {
        ZStack {
            baseGradient
            // Equatable so the 30 Hz meter poll re-rendering the canvas never
            // re-creates the (animated) wash.
            ThemeWash(theme: theme, artwork: artwork, artworkColor: artworkColor,
                      artworkPalette: artworkPalette, dark: scheme == .dark, playing: playing,
                      meshCrestBias: meshCrestBias)
                .equatable()
        }
        .overlay(alignment: .top) {
            if let topTint {
                topTint
                    .frame(maxWidth: .infinity)
                    .frame(height: topTintHeight)
                    .allowsHitTesting(false)
            }
        }
        .ignoresSafeArea()
    }
}

private struct ThemeWash: View, Equatable {
    let theme: RoomcutNowPlayingTheme
    let artwork: NSImage?
    let artworkColor: NSColor?
    let artworkPalette: [NSColor]?
    let dark: Bool
    let playing: Bool
    let meshCrestBias: CGFloat

    static func == (l: ThemeWash, r: ThemeWash) -> Bool {
        l.theme == r.theme && l.dark == r.dark && l.playing == r.playing
            && l.meshCrestBias == r.meshCrestBias
            && l.artwork === r.artwork
            && l.artworkColor == r.artworkColor
            && (l.artworkPalette ?? []) == (r.artworkPalette ?? [])
    }

    var body: some View {
        switch theme {
        case .halo:
            Color.clear
        case .cover:
            coverWash
        case .meshGradient:
            meshWash
        }
    }

    // Cover: blurred artwork → colour wash. Static (one render, no per-frame cost).
    @ViewBuilder
    private var coverWash: some View {
        Group {
            if let artwork {
                // CRITICAL: Color.clear hosts the LAYOUT (it fills the window);
                // the image fills it via overlay + clipped. `scaledToFill` on a
                // bare Image reports the artwork's intrinsic size, which grows the
                // enclosing ZStack past the window and stretches the sibling chrome
                // (sheet / tab bar / top bar) horizontally. Hosting the layout on
                // Color.clear pins it to the window so the image never dictates size.
                // CPU FIX: a `blur(radius: 80)` over the full-window raster was
                // recomputed on every track change and spiked the CPU to ~100%.
                // Instead render the cover into a tiny 64×64 square and blur THAT
                // (a trivial raster), then upscale the already-blurred layer with
                // `scaleEffect` — the magnification is a free GPU transform, so the
                // expensive blur now runs on 64×64 pixels, not ~1600×3200 retina.
                GeometryReader { geo in
                    let fill = max(geo.size.width, geo.size.height) / 64 * 1.3
                    Image(nsImage: artwork)
                        .resizable()
                        .interpolation(.low)
                        .scaledToFill()
                        .frame(width: 64, height: 64)
                        .clipped()
                        .blur(radius: 7)          // cheap — only blurs the 64×64 raster
                        .scaleEffect(fill)        // upscale the blurred layer (GPU)
                        .frame(width: geo.size.width, height: geo.size.height)
                        .clipped()
                }
                // Crossfade to the new cover on a track change (item 6): a fresh id
                // makes SwiftUI insert/remove with an opacity transition.
                .id(ObjectIdentifier(artwork))
                .transition(.opacity)
            } else {
                LinearGradient(
                    colors: NowPlayingBackdropPalette.gradientColors(
                        artworkColor: artworkColor, artworkPalette: artworkPalette, dark: dark),
                    startPoint: .topLeading, endPoint: .bottomTrailing)
            }
        }
        .animation(.easeInOut(duration: 0.6), value: artwork.map(ObjectIdentifier.init))
        .opacity(dark ? 0.92 : 0.82)
        .overlay(legibilityScrim)
    }

    // Mesh: macOS-Tahoe-style flowing liquid-glass wash. The animation is driven
    // by CoreAnimation (CALayer / WindowServer), NOT a per-frame SwiftUI redraw —
    // SwiftUI's MeshGradient is rasterised on the CPU every frame, so animating it
    // (plus the glass chrome re-sampling it) burned 20–40% CPU. CoreAnimation runs
    // the flow on the compositor/GPU, so the app's CPU stays near zero. No Material.
    private var meshWash: some View {
        MeshWaveMetalView(colors: tahoeTones(), dark: dark, playing: playing, crestBias: meshCrestBias)
            .overlay(legibilityScrim)
    }

    // A 4-stop TONAL ramp of the album's SINGLE dominant hue — [light/sky, mid,
    // deep, crest-highlight]. One hue only → similar colours blend, never muddy
    // cross-colour mixes. Light mode = bright tints, dark mode = dark shades, both
    // keyed off the album colour (per the Tahoe light/dark wallpaper pair).
    private func tahoeTones() -> [NSColor] {
        guard let seed = NowPlayingInk.meshSeed(artworkColor: artworkColor, artworkPalette: artworkPalette) else {
            return neutralTahoeTones()
        }
        let h = seed.hueComponent
        let s = max(seed.saturationComponent, 0.45)
        func c(_ sat: CGFloat, _ bri: CGFloat) -> NSColor {
            NSColor(hue: h, saturation: min(sat, 1), brightness: min(bri, 1), alpha: 1)
        }
        if dark {
            return [c(s * 0.80, 0.40), c(s * 0.98, 0.23), c(s * 1.00, 0.10), c(s * 0.62, 0.74)]
        } else {
            return [c(s * 0.52, 0.97), c(s * 0.86, 0.80), c(s * 1.00, 0.60), c(s * 0.32, 1.00)]
        }
    }

    private func neutralTahoeTones() -> [NSColor] {
        if dark {
            return [
                NSColor(white: 0.34, alpha: 1),
                NSColor(white: 0.22, alpha: 1),
                NSColor(white: 0.10, alpha: 1),
                NSColor(white: 0.58, alpha: 1),
            ]
        } else {
            return [
                NSColor(white: 0.96, alpha: 1),
                NSColor(white: 0.84, alpha: 1),
                NSColor(white: 0.68, alpha: 1),
                NSColor(white: 1.00, alpha: 1),
            ]
        }
    }

    // Plain-gradient dim for legibility of floating content (NOT a material).
    private var legibilityScrim: some View {
        LinearGradient(
            stops: [
                .init(color: .black.opacity(dark ? 0.22 : 0.05), location: 0.0),
                .init(color: .clear, location: 0.34),
                .init(color: .clear, location: 0.66),
                .init(color: .black.opacity(dark ? 0.28 : 0.08), location: 1.0),
            ],
            startPoint: .top, endPoint: .bottom)
    }
}

// MARK: - GPU mesh wave (Metal fragment shader → near-zero app CPU)
//
// The Tahoe wave is computed PER PIXEL in a Metal fragment shader on the GPU, so
// the app does almost no per-frame work (just encode one full-screen draw). This
// keeps the exact flowing-wave look that a SwiftUI MeshGradient gives, without its
// per-frame CPU rasterisation (the 20–40% hog). The shader is compiled from source
// at runtime, so there's no .metal file / metallib to bundle. The glass chrome
// refracts the Metal layer via the compositor as usual.

private struct MeshWaveMetalView: NSViewRepresentable {
    let colors: [NSColor]
    let dark: Bool
    let playing: Bool
    let crestBias: CGFloat

    func makeCoordinator() -> MeshWaveRenderer { MeshWaveRenderer() }

    func makeNSView(context: Context) -> MTKView {
        let view = MTKView()
        view.device = MTLCreateSystemDefaultDevice()
        view.delegate = context.coordinator
        view.framebufferOnly = true
        view.preferredFramesPerSecond = 24   // slow wave looks fine at 24
        view.layerContentsRedrawPolicy = .duringViewResize
        context.coordinator.configure(view: view)
        context.coordinator.update(colors: colors, dark: dark, crestBias: crestBias, animate: false)
        context.coordinator.setPaused(!playing)
        Self.applyRunLoop(playing: playing, to: view)
        return view
    }

    func updateNSView(_ nsView: MTKView, context: Context) {
        // Animate the colour cross-fade only while playing (continuous redraws);
        // paused snaps so a track change still repaints with one event-driven draw.
        context.coordinator.update(colors: colors, dark: dark, crestBias: crestBias, animate: playing)
        context.coordinator.setPaused(!playing)
        Self.applyRunLoop(playing: playing, to: nsView)
    }

    // Playing → CONTINUOUS GPU animation. Paused → EVENT-DRIVEN (on-demand): the
    // render timer stops so the GPU does no per-frame work while the song is paused
    // (energy drops — the optimisation), but `enableSetNeedsDisplay = true` keeps the
    // view repainting the frozen frame whenever it (re)appears, resizes, or we mark
    // it dirty. That avoids the blank-to-grey pitfall — which was `isPaused = true`
    // WITH `enableSetNeedsDisplay = false` (mode 3): nothing triggers a draw after a
    // tab / compact↔full layer recreate. Forcing `needsDisplay` also repaints when
    // the album colours change while paused. (Apple dev forums 689320 / 105252.)
    private static func applyRunLoop(playing: Bool, to view: MTKView) {
        if playing {
            view.isPaused = false
            view.enableSetNeedsDisplay = false
        } else {
            view.enableSetNeedsDisplay = true
            view.isPaused = true
            view.needsDisplay = true
        }
    }
}

private struct MeshWaveUniforms {
    var time: Float = 0
    var resolution: SIMD2<Float> = .init(1, 1)
    var colorCount: Int32 = 1
    var dark: Float = 1
    var crestBias: Float = 0
}

final class MeshWaveRenderer: NSObject, MTKViewDelegate {
    private var queue: MTLCommandQueue?
    private var pipeline: MTLRenderPipelineState?
    private var start = CACurrentMediaTime()
    private var pausedAt: CFTimeInterval?
    private var colorVecs: [SIMD4<Float>] = [SIMD4(0.5, 0.5, 0.5, 1)]   // target tones
    private var colorVecsFrom: [SIMD4<Float>] = [SIMD4(0.5, 0.5, 0.5, 1)]
    private var colorStart: CFTimeInterval = 0
    private let colorDur: CFTimeInterval = 0.7
    private var darkFlag: Float = 1
    private var crestBias: Float = 0

    // Smoothly cross-fade the wave's tones to the new album colours (item 6). The
    // lerp is driven by the render loop, so it animates while playing; when paused
    // we snap (no continuous redraws to animate it).
    private func currentColors() -> [SIMD4<Float>] {
        guard colorVecsFrom.count == colorVecs.count, colorStart > 0 else { return colorVecs }
        let t = min(1, max(0, (CACurrentMediaTime() - colorStart) / colorDur))
        if t >= 1 { return colorVecs }
        let tf = Float(t)
        return zip(colorVecsFrom, colorVecs).map { $0 + ($1 - $0) * tf }
    }

    // Freeze/resume the wave clock so pausing holds the exact frame and resuming
    // continues from it (no jump): on resume, push `start` forward by the paused
    // span. While paused the view is event-driven (see applyRunLoop) and any forced
    // repaint uses this frozen time, so the wave holds still — and never blanks.
    func setPaused(_ paused: Bool) {
        if paused {
            if pausedAt == nil { pausedAt = CACurrentMediaTime() }
        } else if let p = pausedAt {
            start += CACurrentMediaTime() - p
            pausedAt = nil
        }
    }

    func configure(view: MTKView) {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice() else { return }
        queue = device.makeCommandQueue()
        guard let lib = try? device.makeLibrary(source: Self.shaderSource, options: nil) else { return }
        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = lib.makeFunction(name: "mw_vertex")
        desc.fragmentFunction = lib.makeFunction(name: "mw_fragment")
        desc.colorAttachments[0].pixelFormat = view.colorPixelFormat
        pipeline = try? device.makeRenderPipelineState(descriptor: desc)
    }

    func update(colors: [NSColor], dark: Bool, crestBias: CGFloat, animate: Bool) {
        darkFlag = dark ? 1 : 0
        self.crestBias = Float(crestBias)
        let raw = colors.prefix(6).compactMap { c -> SIMD4<Float>? in
            guard let r = c.usingColorSpace(.deviceRGB) else { return nil }
            return SIMD4(Float(r.redComponent), Float(r.greenComponent), Float(r.blueComponent), 1)
        }
        let vecs = raw.isEmpty ? [SIMD4(0.5, 0.5, 0.5, 1)] : raw
        guard vecs != colorVecs else { return }
        if animate {
            colorVecsFrom = currentColors()   // continue from the current (interpolated) tones
            colorVecs = vecs
            colorStart = CACurrentMediaTime()
        } else {
            colorVecsFrom = vecs
            colorVecs = vecs
            colorStart = 0
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard let pipeline, let queue,
              let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmd = queue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else { return }

        var u = MeshWaveUniforms()
        // Frozen clock while paused (`pausedAt` holds the time) → the wave stops but
        // the view keeps drawing, so it never blanks and live colour changes paint.
        u.time = Float((pausedAt ?? CACurrentMediaTime()) - start)
        u.resolution = SIMD2(Float(view.drawableSize.width), Float(max(1, view.drawableSize.height)))
        var cols = currentColors()
        u.colorCount = Int32(cols.count)
        u.dark = darkFlag
        u.crestBias = crestBias

        enc.setRenderPipelineState(pipeline)
        enc.setFragmentBytes(&u, length: MemoryLayout<MeshWaveUniforms>.stride, index: 0)
        enc.setFragmentBytes(&cols, length: MemoryLayout<SIMD4<Float>>.stride * cols.count, index: 1)
        enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }

    private static let shaderSource = """
    #include <metal_stdlib>
    using namespace metal;

    struct MWUniforms { float time; float2 resolution; int colorCount; float dark; float crestBias; };

    vertex float4 mw_vertex(uint vid [[vertex_id]]) {
        float2 p = float2((vid << 1) & 2, vid & 2);   // full-screen triangle
        return float4(p * 2.0 - 1.0, 0.0, 1.0);
    }

    fragment float4 mw_fragment(float4 pos [[position]],
                                constant MWUniforms& u [[buffer(0)]],
                                constant float4* colors [[buffer(1)]]) {
        float2 uv = pos.xy / u.resolution;
        float t = u.time * 0.14;                  // slow, elegant (a touch quicker)

        float3 cLight = colors[0].rgb;            // sky / light tone
        float3 cMid   = colors[1].rgb;
        float3 cDeep  = colors[2].rgb;            // wave body
        float3 cCrest = colors[3].rgb;            // bright crest highlight

        // In compact mode the layer is wide & short (aspect ≈ 1.55 vs ≈ 0.46 expanded),
        // which squashes the ribbon into a near-straight line. Scale the horizontal
        // frequency + vertical amplitude up as the aspect widens so the wave keeps the
        // expanded look's curl (expanded → no change; compact → full boost).
        float aspect = u.resolution.x / max(1.0, u.resolution.y);
        float wide = clamp((aspect - 0.7) / 0.9, 0.0, 1.0);
        float freqK = 1.0 + 2.4 * wide;
        float ampK  = 1.0 + 1.1 * wide;

        // ONE smooth ribbon edge — low frequency (a single big wave, not stacked
        // bands), domain-warped so it bends organically, drifting over time.
        float warp = 0.10 * ampK * sin(uv.x * 0.9 * freqK - t * 0.6 + uv.y * 1.3);
        float x = uv.x + warp;
        float crestY = 0.46 + u.crestBias
                     + 0.17 * ampK * sin(x * 1.6 * freqK + t * 0.9)
                     + 0.07 * ampK * sin(x * 0.8 * freqK - t * 0.5);
        float dist = uv.y - crestY;              // signed distance from the ribbon

        // Tonal gradient of the ONE hue: light above the wave → deep below. Smooth,
        // so similar tones blend (no muddy cross-colour).
        float tone = smoothstep(-0.40, 0.55, dist);
        float3 col = tone < 0.5 ? mix(cLight, cMid, tone * 2.0)
                                : mix(cMid, cDeep, (tone - 0.5) * 2.0);

        // Bright crest highlight hugging the ribbon edge (the glass sheen) — a
        // tighter falloff + a touch stronger reads crisper, less hazy.
        float hl = clamp(exp(-dist * dist * 260.0), 0.0, 1.0);
        col = mix(col, cCrest, hl * 0.72);

        // Fine ribbon striations near the crest (the rippling "물결" texture) — subtle.
        float stri = 0.5 + 0.5 * sin(dist * 70.0 + x * 4.0 - t * 1.2);
        col = mix(col, col * (0.94 + 0.12 * stri), hl * 0.5);

        return float4(col, 1.0);
    }
    """
}
