//
// SpaceTab.swift — Phase 7 spatial controls.
//
// Liquid-Glass redesign: a transparent scroll over the Now Playing wash with
// `.regularMaterial` cards, a live stereo-field visualiser, and a reset action.
//
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct SpaceTab: View {
    @ObservedObject var model: RoomcutViewModel
    @Environment(\.colorScheme) private var scheme
    @Namespace private var segNS          // shared by the sliding selection pills
    private var accentColor: Color { RoomcutTokens.blue(scheme) }

    enum SpatialMode: String, CaseIterable, Identifiable {
        case off = "Off", focus = "Focus", widen = "Widen", custom = "Custom"
        var id: String { rawValue }
    }

    var body: some View {
        RoomcutTabScreen {
            // Mode / Output / Surround — Liquid-Glass segmented capsules that mirror the
            // Settings theme picker (Halo/Cover/Mesh), not the boxy native segmented look.
            RoomcutSection("") {
                VStack(spacing: 8) {
                    glassSegmented(visibleModes.map { modeLabel($0) },
                                   selected: visibleModes.firstIndex(of: inferredMode) ?? 0,
                                   group: "mode") { idx in
                        modeSelection.wrappedValue = visibleModes[idx]
                    }
                    .animation(.easeInOut(duration: 0.28), value: visibleModes)

                    glassSegmented([L("Speaker", "Speaker", "スピーカー", "Haut-parleur", "Lautsprecher"),
                                    L("Headphone", "Headphone", "ヘッドフォン", "Casque", "Kopfhörer")],
                                   selected: model.spatialOutputIsHeadphone ? 1 : 0,
                                   group: "output") { idx in
                        model.setSpatialOutput(headphone: idx == 1)
                    }

                    glassSegmented([L("Surround Off", "Surround Off", "サラウンド オフ", "Surround désactivé", "Surround aus"),
                                    L("Surround On", "Surround On", "サラウンド オン", "Surround activé", "Surround an")],
                                   selected: model.spatialSurroundOn ? 1 : 0,
                                   group: "surround") { idx in
                        model.setSpatialSurround(idx == 1)
                    }
                }
                .padding(.horizontal, 12).padding(.vertical, 10)
                .disabled(!model.spatialAvailable)
                .opacity(model.spatialAvailable ? 1 : 0.4)
            }

            RoomcutSection("") {
                SpatialFieldView(width: model.spatialWidth,
                                 center: model.centerFocus,
                                 crossfeed: model.crossfeed,
                                 room: model.roomReduce,
                                 headphone: model.spatialOutputIsHeadphone,
                                 accent: accentColor)
                    .padding(.horizontal, 10).padding(.vertical, 10)
                    .opacity(model.spatialAvailable ? 1 : 0.4)
                    .overlay(alignment: .topTrailing) { resetButton.padding(12) }
            }

            RoomcutSection("") {
                slider(L("Damping", "Damping", "ダンピング", "Amortissement", "Dämpfung"),
                       "house", model.roomReduce, 0...100, tint: accentColor) { model.setRoomReduce($0) }
                RoomcutDivider()
                slider(L("Space", "Space", "空間", "Espace", "Raum"),
                       "arrow.left.and.right", model.spatialWidth, -100...100, tint: accentColor) { model.setSpatialWidth($0) }
                RoomcutDivider()
                slider(L("Center", "Center", "センター", "Centre", "Mitte"),
                       "dot.scope", model.centerFocus, 0...100, tint: accentColor) { model.setCenterFocus($0) }
                RoomcutDivider()
                slider(model.spatialOutputIsHeadphone ? "Crossfeed" : "Crosstalk 3D",
                       model.spatialOutputIsHeadphone ? "headphones" : "hifispeaker.2",
                       model.crossfeed, 0...100, tint: accentColor) { model.setCrossfeed($0) }
            }
            .disabled(!model.spatialAvailable)

            if model.status.reachable && !model.spatialAvailable {
                Text(L("현재 실행 중인 엔진이 Spatial을 지원하지 않습니다.",
                       "The running engine does not support Spatial.",
                       "実行中のエンジンは Spatial に対応していません。",
                       "Le moteur en cours d'exécution ne prend pas en charge Spatial.",
                       "Die laufende Engine unterstützt Spatial nicht."))
                    .font(.system(size: 12)).foregroundStyle(.secondary).padding(.leading, 6)
            }
        }
    }

    // Small circular glass reset, tucked into the field's top-right corner.
    private var resetButton: some View {
        Button { model.setSpatialValues(width: 0, centerFocus: 0, crossfeed: 0, roomReduce: 0) } label: {
            Image(systemName: "arrow.counterclockwise")
                .font(.system(size: 12, weight: .semibold))
                .foregroundStyle(accentColor)
                .frame(width: 30, height: 30)
                .glassEffect(.regular, in: Circle())
                .clipShape(Circle())          // trim the glass drop shadow
                .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .disabled(!model.spatialAvailable)
        .help(L("Spatial 초기화", "Reset Spatial", "Spatial をリセット", "Réinitialiser Spatial", "Spatial zurücksetzen"))
        .accessibilityLabel(L("Spatial 초기화", "Reset Spatial", "Spatial をリセット", "Réinitialiser Spatial", "Spatial zurücksetzen"))
    }

    // Display label for a spatial mode (English base + ja/fr/de translations); the
    // SpatialMode rawValue stays the identity used for selection/inference.
    private func modeLabel(_ m: SpatialMode) -> String {
        switch m {
        case .off:    return L("Off", "Off", "オフ", "Désactivé", "Aus")
        case .focus:  return L("Focus", "Focus", "フォーカス", "Focus", "Fokus")
        case .widen:  return L("Widen", "Widen", "ワイド", "Élargir", "Verbreitern")
        case .custom: return L("Custom", "Custom", "カスタム", "Personnalisé", "Benutzerdef.")
        }
    }

    private func slider(_ title: String, _ icon: String, _ value: Double,
                        _ range: ClosedRange<Double>, tint: Color,
                        _ set: @escaping (Double) -> Void) -> some View {
        let amount = normalizedAmount(value, range)
        let activeTint = tint.opacity(0.28 + amount * 0.72)
        return VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 10) {
                Image(systemName: icon).font(.system(size: 13, weight: .medium)).frame(width: 22)
                    .foregroundStyle(activeTint)
                Text(title).font(.system(size: 13)).foregroundStyle(RoomcutTokens.textPrimary(scheme))
                Spacer()
                Text("\(Int(value.rounded()))")
                    .font(.system(size: 12, weight: .semibold).monospacedDigit()).foregroundStyle(activeTint)
            }
            Slider(value: Binding(get: { value }, set: set), in: range)
                .tint(activeTint)
                .padding(.leading, 32)
        }
        .padding(.horizontal, 16).padding(.vertical, 10)
    }

    // Liquid-Glass segmented control (same recipe as the Settings theme picker): a glass
    // capsule with a subtle monochrome pill that SLIDES between options (matchedGeometry).
    // `group` keeps each control's pill independent; `.clipShape` trims the glass drop
    // shadow so it sits flat on the card.
    private func glassSegmented(_ labels: [String], selected: Int, group: String,
                                _ select: @escaping (Int) -> Void) -> some View {
        GlassEffectContainer(spacing: 5) {
            HStack(spacing: 4) {
                ForEach(Array(labels.enumerated()), id: \.offset) { idx, label in
                    let isSel = idx == selected
                    Button { select(idx) } label: {
                        Text(label)
                            .font(.system(size: 13, weight: isSel ? .semibold : .regular))
                            .foregroundStyle(isSel ? RoomcutTokens.textPrimary(scheme)
                                             : RoomcutTokens.textSecondary(scheme))
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 7)
                            .background {
                                if isSel {
                                    Capsule()
                                        .fill(accentColor.opacity(0.22))
                                        .matchedGeometryEffect(id: group, in: segNS)
                                }
                            }
                            .contentShape(Capsule())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(4)
            .animation(.spring(response: 0.32, dampingFraction: 0.86), value: selected)
        }
        .glassEffect(.regular, in: Capsule())
        .clipShape(Capsule())
    }

    private func normalizedAmount(_ value: Double, _ range: ClosedRange<Double>) -> Double {
        if range.lowerBound < 0 {
            return min(1, abs(value) / max(abs(range.lowerBound), abs(range.upperBound)))
        }
        return min(1, max(0, value / max(1, range.upperBound)))
    }

    // Custom is hidden while a named mode (Off/Focus/Widen) is active; it fades in
    // only once the sliders have moved the state off a preset.
    private var visibleModes: [SpatialMode] {
        inferredMode == .custom ? SpatialMode.allCases : [.off, .focus, .widen]
    }

    private var modeSelection: Binding<SpatialMode> {
        Binding(get: { inferredMode }, set: { mode in
            guard model.spatialAvailable else { return }
            switch mode {
            case .off:    model.setSpatialValues(width: 0, centerFocus: 0, crossfeed: 0, roomReduce: 0)
            case .focus:  model.setSpatialValues(width: -35, centerFocus: 28, crossfeed: 12, roomReduce: 55)
            case .widen:  model.setSpatialValues(width: 35, centerFocus: 0, crossfeed: 4, roomReduce: 0)
            case .custom: break
            }
        })
    }

    private var inferredMode: SpatialMode {
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 0.5 }
        if near(model.spatialWidth, 0), near(model.centerFocus, 0), near(model.crossfeed, 0), near(model.roomReduce, 0) { return .off }
        if near(model.spatialWidth, -35), near(model.centerFocus, 28), near(model.crossfeed, 12), near(model.roomReduce, 55) { return .focus }
        if near(model.spatialWidth, 35), near(model.centerFocus, 0), near(model.crossfeed, 4), near(model.roomReduce, 0) { return .widen }
        return .custom
    }
}

// Positions for the 3D listening-room scene, derived once per layout pass.
private struct SpatialFieldMetrics {
    let w: CGFloat, h: CGFloat
    let widthAmount: CGFloat   // 0…1 magnitude of Space
    let center01: CGFloat
    let cross01: CGFloat
    let room01: CGFloat
    let lSpeaker: CGPoint
    let rSpeaker: CGPoint
    let head: CGPoint
    let earL: CGPoint
    let earR: CGPoint
    let orb: CGPoint
    let crossPoint: CGPoint     // where the two crosstalk paths intersect
    let leftWall: CGPoint       // side-wall reflection point
    let rightWall: CGPoint

    init(width: Double, center: Double, crossfeed: Double, room: Double, w: CGFloat, h: CGFloat) {
        self.w = w; self.h = h
        self.widthAmount = CGFloat(min(1, abs(width) / 100))
        let tw = CGFloat((width + 100) / 200)                 // 0 narrow … 1 wide
        self.center01 = CGFloat(min(1, max(0, center / 100)))
        self.cross01 = CGFloat(min(1, max(0, crossfeed / 100)))
        self.room01 = CGFloat(min(1, max(0, room / 100)))
        let spreadFrac = 0.13 + 0.25 * tw
        let ty = h * 0.33
        let hy = h * 0.85
        self.lSpeaker = CGPoint(x: w * 0.5 - w * spreadFrac, y: ty)
        self.rSpeaker = CGPoint(x: w * 0.5 + w * spreadFrac, y: ty)
        self.head = CGPoint(x: w * 0.5, y: hy)
        self.earL = CGPoint(x: w * 0.5 - 14, y: hy)
        self.earR = CGPoint(x: w * 0.5 + 14, y: hy)
        self.orb = CGPoint(x: w * 0.5, y: ty)
        self.crossPoint = CGPoint(x: w * 0.5, y: ty + (hy - ty) * 0.5)
        let wallY = ty + (hy - ty) * 0.40
        self.leftWall = CGPoint(x: w * 0.05, y: wallY)
        self.rightWall = CGPoint(x: w * 0.95, y: wallY)
    }
}

// 3D listening-room visualiser: a perspective floor (Room), L/R glass speakers with
// a soundstage (Space), a phantom-centre orb + beam (Center), and the transaural
// path geometry — same-side S paths plus the alternate-side A crosstalk paths shown
// being cancelled (speaker / Crosstalk 3D) or bled (headphone / Crossfeed).
//
// PERF: parameter changes drive springs; the floor is a value-driven `Canvas` (no
// per-frame redraw); the only continuous animation is a cheap 18 fps ring pulse with
// no blur. Idle CPU stays ~0.
private struct SpatialFieldView: View {
    let width: Double      // -100…100  Space
    let center: Double     // 0…100     Center
    let crossfeed: Double  // 0…100     Crosstalk 3D / Crossfeed
    let room: Double       // 0…100     Room
    let headphone: Bool
    let accent: Color
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        GeometryReader { geo in
            let t = SpatialFieldMetrics(width: width, center: center, crossfeed: crossfeed,
                                        room: room, w: geo.size.width, h: geo.size.height)
            ZStack {
                stageBackdrop(t)
                floorCanvas()
                roomLayer(t)
                soundstage(t)
                crosstalkLayer(t)
                centerBeam(t)
                pulseLayer(t)
                orb(t)
                speaker("L", at: t.lSpeaker, amount: t.widthAmount)
                speaker("R", at: t.rSpeaker, amount: t.widthAmount)
                listener(t)
            }
            .animation(.spring(response: 0.4, dampingFraction: 0.82), value: width)
            .animation(.spring(response: 0.4, dampingFraction: 0.82), value: center)
            .animation(.spring(response: 0.4, dampingFraction: 0.82), value: crossfeed)
            .animation(.spring(response: 0.4, dampingFraction: 0.82), value: room)
            .animation(.easeInOut(duration: 0.3), value: headphone)
        }
        .frame(height: 232)
    }

    // MARK: Room — perspective floor + far-wall reflection (Canvas, value-driven)

    private func stageBackdrop(_ t: SpatialFieldMetrics) -> some View {
        RoundedRectangle(cornerRadius: 16, style: .continuous)
            .fill(RadialGradient(colors: [accent.opacity(0.06), .clear],
                                 center: .init(x: 0.5, y: 0.34), startRadius: 8, endRadius: t.w * 0.72))
    }

    private func floorCanvas() -> some View {
        let ink = (scheme == .dark ? Color.white : Color.black)
        return Canvas { ctx, size in
            let w = size.width, h = size.height, cx = w / 2
            let farY = h * 0.20, nearY = h * 0.98
            let farHalf = w * 0.17, nearHalf = w * 0.48
            var floor = Path()
            floor.move(to: CGPoint(x: cx - farHalf, y: farY))
            floor.addLine(to: CGPoint(x: cx + farHalf, y: farY))
            floor.addLine(to: CGPoint(x: cx + nearHalf, y: nearY))
            floor.addLine(to: CGPoint(x: cx - nearHalf, y: nearY))
            floor.closeSubpath()
            ctx.fill(floor, with: .linearGradient(
                Gradient(colors: [accent.opacity(0.16), accent.opacity(0.02)]),
                startPoint: CGPoint(x: cx, y: nearY), endPoint: CGPoint(x: cx, y: farY)))

            // Perspective grid — denser toward the far edge.
            for i in 0...6 {
                let pe = CGFloat(pow(Double(i) / 6.0, 1.7))
                let y = farY + (nearY - farY) * pe
                let half = farHalf + (nearHalf - farHalf) * pe
                var line = Path()
                line.move(to: CGPoint(x: cx - half, y: y))
                line.addLine(to: CGPoint(x: cx + half, y: y))
                ctx.stroke(line, with: .color(ink.opacity(0.07)), lineWidth: 0.8)
            }
            for i in 0...6 {
                let p = CGFloat(i) / 6.0
                var line = Path()
                line.move(to: CGPoint(x: cx - farHalf + 2 * farHalf * p, y: farY))
                line.addLine(to: CGPoint(x: cx - nearHalf + 2 * nearHalf * p, y: nearY))
                ctx.stroke(line, with: .color(ink.opacity(0.06)), lineWidth: 0.8)
            }

            // Faint stage outline (Room itself is drawn by roomLayer above the floor).
            ctx.stroke(floor, with: .color(accent.opacity(0.12)), lineWidth: 1)
        }
    }

    // MARK: Room — first-reflection bounce paths off the side walls

    // The classic "speaker → side wall → listener" first-reflection paths. A live room
    // shows them brightly; raising Room treats the walls (the panels fill in with
    // absorption) so the reflections fade — exactly what the control does.
    @ViewBuilder
    private func roomLayer(_ t: SpatialFieldMetrics) -> some View {
        let refl = Double(1 - t.room01)
        wallPanel(at: t.leftWall, absorb: t.room01)
        wallPanel(at: t.rightWall, absorb: t.room01)
        reflectionPath(t.lSpeaker, t.leftWall, t.head, opacity: 0.06 + 0.34 * refl)
        reflectionPath(t.rSpeaker, t.rightWall, t.head, opacity: 0.06 + 0.34 * refl)
    }

    @ViewBuilder
    private func reflectionPath(_ s: CGPoint, _ wall: CGPoint, _ head: CGPoint, opacity: Double) -> some View {
        beam(s, wall, thickness: 1.5, opacity: opacity)
        beam(wall, head, thickness: 1.5, opacity: opacity)
        Circle().fill(accent.opacity(min(1, opacity * 1.6)))
            .frame(width: 5, height: 5).position(wall)
    }

    private func wallPanel(at p: CGPoint, absorb: CGFloat) -> some View {
        RoundedRectangle(cornerRadius: 3, style: .continuous)
            .fill(accent.opacity(0.05 + 0.16 * Double(absorb)))
            .overlay(RoundedRectangle(cornerRadius: 3, style: .continuous)
                .strokeBorder(accent.opacity(0.16 + 0.24 * Double(absorb)), lineWidth: 1))
            .frame(width: 7, height: 58)
            .position(p)
    }

    // MARK: Space — soundstage glow between the speakers (widens with Crosstalk 3D)

    private func soundstage(_ t: SpatialFieldMetrics) -> some View {
        // In speaker mode, crosstalk cancellation stretches the image beyond the
        // speakers — the soundstage glow grows past them so the "3D" payoff is visible.
        let widen = headphone ? 0 : Double(t.cross01) * 0.40
        let stageW = (t.rSpeaker.x - t.lSpeaker.x) + 44 + CGFloat(widen) * t.w
        return Ellipse()
            .fill(RadialGradient(
                colors: [accent.opacity(0.10 + 0.18 * Double(t.widthAmount)), accent.opacity(0.015)],
                center: .center, startRadius: 2, endRadius: stageW / 2))
            .frame(width: max(40, stageW), height: 50)
            .position(x: t.w / 2, y: t.lSpeaker.y + 8)
    }

    // MARK: Crosstalk 3D / Crossfeed — only the alternate-side (A) paths the control acts on

    // One clear story per mode (no 4-beam soup): the opposite-channel paths the control
    // governs, plus a badge marking what it does — speaker CANCELS the crosstalk (✕,
    // paths fade, image widens); headphone BLEEDS the channels together (⇄, paths grow).
    @ViewBuilder
    private func crosstalkLayer(_ t: SpatialFieldMetrics) -> some View {
        let c = Double(t.cross01)
        if headphone {
            beam(t.lSpeaker, t.earR, thickness: 2.5, opacity: 0.10 + 0.50 * c)
            beam(t.rSpeaker, t.earL, thickness: 2.5, opacity: 0.10 + 0.50 * c)
            badge("arrow.left.arrow.right", at: t.crossPoint, strength: t.cross01)
        } else {
            beam(t.lSpeaker, t.earR, thickness: 2, opacity: 0.30 * (1 - c))
            beam(t.rSpeaker, t.earL, thickness: 2, opacity: 0.30 * (1 - c))
            badge("xmark", at: t.crossPoint, strength: t.cross01)
        }
    }

    private func badge(_ symbol: String, at p: CGPoint, strength: CGFloat) -> some View {
        let s = 16 + 7 * strength
        return Image(systemName: symbol)
            .font(.system(size: s * 0.5, weight: .bold))
            .foregroundStyle(accent.opacity(0.45 + 0.45 * Double(strength)))
            .frame(width: s, height: s)
            .background(Circle().fill(accent.opacity(0.10 + 0.18 * Double(strength))))
            .overlay(Circle().strokeBorder(accent.opacity(0.30 + 0.40 * Double(strength)), lineWidth: 1))
            .glassEffect(.regular.tint(accent.opacity(0.08 + 0.14 * Double(strength))), in: Circle())
            .position(p)
    }

    private func beam(_ a: CGPoint, _ b: CGPoint, thickness: CGFloat, opacity: Double) -> some View {
        let dx = b.x - a.x, dy = b.y - a.y
        let len = max(1, hypot(dx, dy))
        return Capsule()
            .fill(LinearGradient(colors: [accent.opacity(0), accent, accent.opacity(0)],
                                 startPoint: .leading, endPoint: .trailing))
            .frame(width: len, height: thickness)
            .opacity(opacity)
            .rotationEffect(.radians(atan2(dy, dx)))
            .position(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2)
    }

    // MARK: Center — phantom-centre beam + orb

    private func centerBeam(_ t: SpatialFieldMetrics) -> some View {
        beam(t.orb, t.head, thickness: 2 + 5 * t.center01, opacity: 0.08 + 0.5 * Double(t.center01))
    }

    private func orb(_ t: SpatialFieldMetrics) -> some View {
        let size = 16 + 26 * t.center01
        return Circle()
            .fill(RadialGradient(
                colors: [.white.opacity(0.25 + 0.6 * Double(t.center01)), accent.opacity(0.45)],
                center: .center, startRadius: 0, endRadius: size / 2))
            .overlay(Circle().strokeBorder(accent.opacity(0.3 + 0.4 * Double(t.center01)), lineWidth: 1))
            .frame(width: size, height: size)
            .glassEffect(.regular.tint(accent.opacity(0.10 + 0.20 * Double(t.center01))), in: Circle())
            .position(t.orb)
    }

    // MARK: Speakers (Space) + listener

    private func speaker(_ label: String, at p: CGPoint, amount: CGFloat) -> some View {
        let shape = RoundedRectangle(cornerRadius: 13, style: .continuous)
        let amt = Double(amount)
        return ZStack {
            shape
                .fill(accent.opacity(0.12))
                .overlay(shape.strokeBorder(accent.opacity(0.30 + 0.30 * amt), lineWidth: 1))
                .frame(width: 40, height: 52)
                .glassEffect(.regular.tint(accent.opacity(0.10 + 0.12 * amt)).interactive(), in: shape)
            VStack(spacing: 4) {
                Image(systemName: "hifispeaker.fill").font(.system(size: 13, weight: .semibold))
                Text(label).font(.system(size: 9, weight: .bold)).monospacedDigit()
            }
            .foregroundStyle(accent.opacity(0.66 + 0.28 * amt))
        }
        .position(p)
    }

    @ViewBuilder
    private func listener(_ t: SpatialFieldMetrics) -> some View {
        let head = Circle()
            .fill(accent.opacity(0.10))
            .overlay(Circle().strokeBorder(accent.opacity(0.35), lineWidth: 1))
            .frame(width: 30, height: 30)
            .glassEffect(.regular.tint(accent.opacity(0.08)), in: Circle())
            .position(t.head)
        head
        Image(systemName: "person.fill")
            .font(.system(size: 12)).foregroundStyle(accent.opacity(0.72)).position(t.head)
        if headphone {
            ForEach([-1.0, 1.0], id: \.self) { s in
                RoundedRectangle(cornerRadius: 3, style: .continuous)
                    .fill(accent.opacity(0.55))
                    .frame(width: 6, height: 15)
                    .position(x: t.head.x + CGFloat(s) * 17, y: t.head.y)
            }
            Circle().trim(from: 0.5, to: 1.0)
                .stroke(accent.opacity(0.5), lineWidth: 2)
                .frame(width: 38, height: 38)
                .position(x: t.head.x, y: t.head.y)
        } else {
            Circle().fill(accent.opacity(0.65)).frame(width: 5, height: 5).position(t.earL)
            Circle().fill(accent.opacity(0.65)).frame(width: 5, height: 5).position(t.earR)
        }
    }

    // MARK: Sound emission — cheap continuous ring pulse (18 fps, no blur)

    private func pulseLayer(_ t: SpatialFieldMetrics) -> some View {
        let intensity = Double(max(t.widthAmount, max(t.center01, max(t.cross01, t.room01)))) * 0.5 + 0.22
        return TimelineView(.periodic(from: .now, by: 1.0 / 18.0)) { ctx in
            let phase = ctx.date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: 2.4) / 2.4
            ZStack {
                ForEach(0..<3, id: \.self) { i in
                    let f = (phase + Double(i) / 3).truncatingRemainder(dividingBy: 1)
                    ring(at: t.lSpeaker, f: f, intensity: intensity)
                    ring(at: t.rSpeaker, f: f, intensity: intensity)
                }
            }
        }
    }

    private func ring(at p: CGPoint, f: Double, intensity: Double) -> some View {
        let d = 18 + f * 66
        return Circle()
            .strokeBorder(accent.opacity((1 - f) * 0.32 * intensity), lineWidth: 1.4)
            .frame(width: d, height: d)
            .position(p)
    }
}
