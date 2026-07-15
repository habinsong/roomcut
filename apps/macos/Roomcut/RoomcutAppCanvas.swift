//
// RoomcutAppCanvas.swift — the iOS-style app canvas inside a macOS window.
//
// macOS's stock TabView does NOT render an iOS-style bottom tab bar (it falls
// back to an overflow control), so the bottom tab bar is hand-built here while
// everything that the system DOES provide natively (Form sections, pickers,
// glassEffect, etc.) stays stock. Layout: a TopBar (device + ON), the selected
// tab's content, and a hand-built Liquid-Glass bottom tab bar.
//
// The existing RoomcutViewModel + NowPlayingMonitor are unchanged — View only.
//
import SwiftUI
import AppKit
import RoomcutCore
import RoomcutPresentationCore

enum RoomcutTab: String, CaseIterable, Identifiable {
    case home, space, tune, inspect, settings
    var id: String { rawValue }

    var title: String {
        switch self {
        case .home:     return L("Home", "Home", "ホーム", "Accueil", "Start")
        case .space:    return L("Space", "Space", "空間", "Espace", "Raum")
        case .tune:     return L("Tune", "Tune", "チューン", "Réglage", "Abstimmung")
        case .inspect:  return L("Inspect", "Inspect", "検査", "Inspecter", "Prüfen")
        case .settings: return L("Settings", "Settings", "設定", "Réglages", "Einstellungen")
        }
    }
    // SF Symbols.
    var icon: String {
        switch self {
        case .home:     return "house"
        case .space:    return "square.stack.3d.up"
        case .tune:     return "iphone.radiowaves.left.and.right"
        case .inspect:  return "waveform"
        case .settings: return "gearshape"
        }
    }
}

struct RoomcutAppCanvas: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var monitor: NowPlayingMonitor
    @Binding var compactMode: Bool
    @Binding var keepsWindowOnTop: Bool

    @State private var tab: RoomcutTab = .home
    @State private var compactClosing = false
    @Environment(\.colorScheme) private var scheme

    // Cover/Mesh wash dark enough that the tab bar should go white. The wash (Layer
    // 0) shows behind the tab bar on EVERY tab now that the Space/Inspect/Settings
    // screens are transparent — so this is no longer gated to Home.
    private var tabBarDarkBackdrop: Bool {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty else { return false }
        return NowPlayingInk.isDarkBackdrop(theme: model.nowPlayingTheme, scheme: scheme,
                                            artworkColor: monitor.artworkColor, artworkPalette: monitor.artworkPalette)
    }

    // Dark mode + near-white Cover wash → the chrome flips to BLACK ink (same
    // judgement NowPlayingView uses, so the whole Home surface stays in sync).
    private var tabBarBrightBackdrop: Bool {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty else { return false }
        return NowPlayingInk.isBrightBackdrop(theme: model.nowPlayingTheme, scheme: scheme,
                                              artworkColor: monitor.artworkColor, artworkPalette: monitor.artworkPalette)
    }

    // B layout on Home: the cover bleeds full-width, so push the mesh crest down and
    // tint the band above the cover with its top-edge colour.
    private var bLayoutActive: Bool { tab == .home && model.nowPlayingLayout == .b }
    // The B backdrop (top tint + lowered mesh crest) only applies while the cover is
    // actually on screen — once the sheet is expanded the cover morphs to a pill, so
    // drop the B backdrop and let the plain wash show (same as A).
    private var bBackdropActive: Bool { bLayoutActive && sheetModel.level != .expanded }
    private var bTopTintColor: Color? {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty,
              let c = monitor.artworkTopColor else { return nil }
        return Color(nsColor: c)
    }
    // Sound Controls sheet state — owned here so the drag handle can sit ON TOP of
    // the tab bar (below) while HomeTab draws the panel it controls.
    @StateObject private var sheetModel = SoundSheetModel(
        level: (AppLaunch.fixtureKind == .uiAdvanced || AppLaunch.fixtureKind == .uiAnalyzer)
            ? .expanded : .minimized)
    @State private var sheetKeyMonitor: Any?

    var body: some View {
        ZStack {
            if compactMode {
                compactContent
                    .opacity(compactClosing ? 0 : 1)
                    .scaleEffect(compactClosing ? 0.96 : 1, anchor: .top)
                    .offset(y: compactClosing ? -8 : 0)
                    .transition(.asymmetric(
                        insertion: .opacity.combined(with: .scale(scale: 0.985, anchor: .top)),
                        removal: .opacity.combined(with: .scale(scale: 0.985, anchor: .top))
                    ))
            } else {
                fullSurface
                    .transition(.opacity)
            }
        }
        .background(Color.clear)
        .animation(.smooth(duration: 0.38), value: compactMode)
        .animation(.smooth(duration: 0.24), value: compactClosing)
        .task { model.startPolling() }
        .overlay(alignment: .top) { errorBanner }
        // ↑ / ↓ open / close the Sound Controls sheet (Home, full window only).
        .onChange(of: tab, initial: true) { sheetModel.keyboardEnabled = (tab == .home && !compactMode) }
        .onChange(of: compactMode) { sheetModel.keyboardEnabled = (tab == .home && !compactMode) }
        .onAppear { installSheetKeyMonitor() }
        .onDisappear { removeSheetKeyMonitor() }
    }

    private func installSheetKeyMonitor() {
        guard sheetKeyMonitor == nil else { return }
        sheetKeyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            guard sheetModel.keyboardEnabled else { return event }
            // Never hijack arrows while typing in a text field.
            if NSApp.keyWindow?.firstResponder is NSText { return event }
            switch event.keyCode {
            case 126: sheetModel.stepUp();   return nil   // ↑
            case 125: sheetModel.stepDown(); return nil   // ↓
            default:  return event
            }
        }
    }

    private func removeSheetKeyMonitor() {
        if let m = sheetKeyMonitor { NSEvent.removeMonitor(m); sheetKeyMonitor = nil }
    }

    private var fullSurface: some View {
        ZStack {
            RoomcutBackgroundLayer(
                theme: model.nowPlayingTheme,
                artwork: monitor.artwork,
                artworkColor: monitor.artworkColor,
                artworkPalette: monitor.artworkPalette,
                playing: monitor.snapshot?.playing ?? true,
                meshCrestBias: bBackdropActive ? 0.22 : 0,
                topTint: bBackdropActive ? bTopTintColor : nil,
                topTintHeight: 150)
                .ignoresSafeArea()
                .contentShape(Rectangle())
                .onTapGesture { NSApp.keyWindow?.makeFirstResponder(nil) }

            fullContent
        }
    }

    private var fullContent: some View {
        ZStack {
            VStack(spacing: 0) {
                VStack(spacing: 0) {
                    WindowHandleBar(
                        keepsWindowOnTop: $keepsWindowOnTop,
                        presetName: model.currentPresetName,
                        showPreset: false,
                        capsuleWidth: 104,
                        capsuleHeight: 22,
                        capsuleOffsetX: 0,
                        barWidth: nil,
                        barHeight: 32,
                        onClick: collapseToNowPlaying,
                        onDoubleClick: collapseToNowPlaying
                    )

                    TopBar(model: model, darkBackdrop: tabBarDarkBackdrop,
                           brightBackdrop: tabBarBrightBackdrop)
                        .padding(.horizontal, 16)
                        .padding(.bottom, 6)
                }

                content
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }

            VStack(spacing: 0) {
                Spacer()
                BottomTabBar(
                    selection: $tab,
                    darkBackdrop: tabBarDarkBackdrop,
                    brightBackdrop: tabBarBrightBackdrop,
                    blendsWithOpenSheet: tab == .home && sheetModel.level != .minimized,
                    onReselect: { t in
                        // Re-tapping Home while the Sound Controls sheet is open closes it.
                        if t == .home && sheetModel.level == .expanded {
                            withAnimation(.smooth(duration: 0.3)) { sheetModel.level = .minimized }
                        }
                    }
                )
                    // A "—" affordance inside the bar's top (Home + closed only).
                    // Purely visual — the WHOLE bar is the drag target below.
                    .overlay(alignment: .top) {
                        if tab == .home && sheetModel.level == .minimized { soundSheetHandle }
                    }
                    // Drag ANYWHERE on the tab bar to open / close the sheet, while
                    // taps still switch tabs (minimumDistance lets a tap through; a
                    // drag past it cancels the button and runs the sheet snap).
                    .simultaneousGesture(
                        DragGesture(minimumDistance: 12)
                            .onChanged { if tab == .home { sheetModel.dragChanged($0.translation.height) } }
                            .onEnded {
                                if tab == .home {
                                    sheetModel.dragEnded(predictedTranslation: $0.predictedEndTranslation.height)
                                }
                            }
                    )
                    .padding(.horizontal, 16)
                    .padding(.bottom, 14)
            }
        }
    }

    private var soundSheetHandle: some View {
        // Pure visual affordance — hit testing OFF so the bar's drag + the tab
        // buttons receive every touch.
        Capsule()
            .fill(tabBarDarkBackdrop ? Color.white.opacity(0.7)
                  : tabBarBrightBackdrop ? Color.black.opacity(0.5)
                  : Color.secondary.opacity(0.5))
            .frame(width: 36, height: 3)
            .frame(maxWidth: .infinity)
            .frame(height: 11, alignment: .center)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }

    private var compactContent: some View {
        VStack(spacing: 4) {
            WindowHandleBar(
                keepsWindowOnTop: $keepsWindowOnTop,
                presetName: model.currentPresetName,
                showPreset: true,
                capsuleWidth: 136,
                capsuleHeight: 24,
                capsuleOffsetX: 0,
                barWidth: 136,
                barHeight: 24,
                onClick: hideCompactWindow,
                onDoubleClick: hideCompactWindow,
                volume: model.volume
            )
            .padding(.top, 2)   // a hair of breathing room above the pill

            NowPlayingView(
                display: model.nowPlayingDisplay,
                compact: false,
                audioFormat: model.audioFormat,
                theme: model.nowPlayingTheme,
                menuLike: true,
                themeSync: model.themeSyncEnabled,
                lyricFontSize: 17,
                monitor: monitor,
                onMenuCardTap: expandFromNowPlaying
            )
            .frame(maxWidth: .infinity)
            .padding(.bottom, 10)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
    }

    private func collapseToNowPlaying() {
        compactClosing = false
        withAnimation(.smooth(duration: 0.38)) {
            compactMode = true
        }
    }

    private func hideCompactWindow() {
        guard let window = activeMainWindow else {
            compactClosing = false
            return
        }
        compactClosing = true
        window.rollUpAndHide {
            var transaction = Transaction()
            transaction.disablesAnimations = true
            withTransaction(transaction) {
                compactMode = false
                compactClosing = false
            }
        }
    }

    private var activeMainWindow: RoomcutMainWindow? {
        (NSApp.keyWindow as? RoomcutMainWindow)
            ?? (NSApp.mainWindow as? RoomcutMainWindow)
            ?? NSApp.windows.compactMap { $0 as? RoomcutMainWindow }.first { $0.isVisible }
    }

    private func expandFromNowPlaying() {
        compactClosing = false
        withAnimation(.smooth(duration: 0.36)) {
            compactMode = false
        }
    }

    @ViewBuilder
    private var content: some View {
        switch tab {
        case .home:     HomeTab(model: model, monitor: monitor, sheet: sheetModel)
        case .space:    SpaceTab(model: model)
        case .tune:     RoomTuneTab(model: model)
        case .inspect:  InspectTab(model: model, meters: model.meters)
        case .settings: SettingsTab(model: model)
        }
    }

    @ViewBuilder
    private var errorBanner: some View {
        if let banner = model.errorBanner {
            Text(banner)
                .font(.system(size: 12, weight: .medium))
                .foregroundStyle(.white)
                .padding(.horizontal, 14).padding(.vertical, 8)
                .background(RoomcutTokens.red.opacity(0.92), in: Capsule())
                .padding(.top, 54)
                .transition(.move(edge: .top).combined(with: .opacity))
                .accessibilityLabel("오류: \(banner)")
        }
    }

}

// A reusable window-chrome handle: a centred "——" grab area (drag the window +
// double-click to collapse/hide, optional single-click) flanked by hover-revealed
// controls — the always-on-top toggle (trailing, both modes) and the EQ-preset
// badge (leading, compact only). The AppKit grab area is a NARROW centred strip
// so the SwiftUI toggle beside it actually receives its clicks; a full-width
// AppKit view used to swallow them, which is why the toggle "did nothing".
private struct WindowHandleBar: View {
    @Binding var keepsWindowOnTop: Bool
    let presetName: String
    let showPreset: Bool
    // Hover capsule behind the "——"/toggle cluster (hidden until hover), sized and
    // shifted so it wraps the cluster: full bar in compact, a snug pill in expanded.
    // capsuleHeight < barHeight leaves an equal top/bottom margin (separation).
    let capsuleWidth: CGFloat
    let capsuleHeight: CGFloat
    let capsuleOffsetX: CGFloat
    let barWidth: CGFloat?       // nil = full width (expanded); fixed in compact
    let barHeight: CGFloat
    let onClick: (() -> Void)?
    let onDoubleClick: () -> Void
    // Roomcut output volume (0…1) for the little ring right of the toggle. nil =
    // no ring (expanded mode); the value drives a clockwise battery-style fill.
    var volume: Double? = nil

    // The toggle sits this far right of the centred "——"; the drag view punches a
    // hit-hole of this half-width there so the toggle's clicks get through.
    private let toggleOffsetX: CGFloat = 36
    private let toggleHoleHalfWidth: CGFloat = 18

    @Environment(\.colorScheme) private var scheme
    @State private var centerHovering = false   // pointer over the AppKit grab area
    @State private var sideHovering = false     // pointer over the rest of the bar
    @State private var pressed = false

    private var visible: Bool { centerHovering || sideHovering || pressed }

    var body: some View {
        let bar = ZStack {
            // Real Liquid Glass hover capsule, rendered in AppKit (NSGlassEffectView)
            // so it never swallows the window-drag / double-click — SwiftUI's
            // `.glassEffect` does, even with allowsHitTesting(false). GlassCapsule's
            // hitTest returns nil, so the drag view below stays fully interactive.
            GlassCapsule(visible: visible)
                .frame(width: capsuleWidth, height: capsuleHeight)
                .offset(x: capsuleOffsetX)

            // Full-bar grab area (AppKit): drag the whole top strip + double/
            // single click. It declines hits only in the toggle's band so the
            // overlaid SwiftUI toggle stays clickable.
            WindowCloseButton(
                onClick: onClick,
                onDoubleClick: onDoubleClick,
                onPressChanged: { pressed = $0 },
                onHoverChanged: { centerHovering = $0 },
                hitHoleCenterOffsetX: toggleOffsetX,
                hitHoleHalfWidth: visible ? toggleHoleHalfWidth : 0
            )
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Text("——")
                .font(.system(size: 18, weight: .semibold))
                .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                .allowsHitTesting(false)

            if visible && showPreset {
                CompactPresetBadge(name: presetName, scheme: scheme)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.leading, 8)
                    .transition(.opacity.combined(with: .scale(scale: 0.92)))
            }

            if visible {
                // Just right of the centred "——", overlaid on the drag bar (which
                // opens a hit-hole here so this toggle receives the click).
                AlwaysOnTopToggle(isOn: $keepsWindowOnTop, scheme: scheme)
                    .offset(x: toggleOffsetX)
                    .transition(.opacity.combined(with: .scale(scale: 0.92)))

                // Roomcut volume, as a tiny battery-style ring just right of the
                // toggle (compact only — `volume` is nil in expanded mode).
                if let volume {
                    VolumeRing(value: volume, scheme: scheme)
                        .offset(x: toggleOffsetX + 21)
                        .transition(.opacity.combined(with: .scale(scale: 0.92)))
                }
            }
        }
        .contentShape(Rectangle())
        .onHover { sideHovering = $0 }
        .animation(.easeOut(duration: 0.12), value: visible)

        if let barWidth {
            bar.frame(width: barWidth, height: barHeight)
        } else {
            bar.frame(maxWidth: .infinity).frame(height: barHeight)
        }
    }
}

private struct AlwaysOnTopToggle: View {
    @Binding var isOn: Bool
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        Button {
            withAnimation(.easeOut(duration: 0.14)) {
                isOn.toggle()
            }
        } label: {
            ZStack(alignment: isOn ? .trailing : .leading) {
                Capsule(style: .continuous)
                    .fill(ink.opacity(isOn ? 0.24 : 0.08))
                Circle()
                    .fill(ink.opacity(isOn ? 0.82 : 0.42))
                    .frame(width: 8, height: 8)
                    .padding(.horizontal, 3)
            }
            .frame(width: 24, height: 14)
            .contentShape(Capsule(style: .continuous))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("항상 위에 표시")
        .accessibilityValue(isOn ? "켬" : "끔")
    }
}

// Tiny battery-style ring that fills clockwise from 12 o'clock by the volume
// fraction. Display only (no hit testing) — sits in the sliver right of the toggle.
private struct VolumeRing: View {
    let value: Double
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        ZStack {
            Circle().stroke(ink.opacity(0.22), lineWidth: 1.6)
            Circle()
                .trim(from: 0, to: max(0, min(1, value)))
                .stroke(ink.opacity(0.72), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
                .rotationEffect(.degrees(-90))
        }
        .frame(width: 11, height: 11)
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

private struct CompactPresetBadge: View {
    let name: String
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        Text(name)
            .font(.system(size: 9, weight: .semibold))
            .foregroundStyle(ink.opacity(0.48))
            .lineLimit(1)
            .minimumScaleFactor(0.55)
            .frame(width: 52, alignment: .leading)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }
}

// MARK: - TopBar (device selector + ON toggle)

private struct TopBar: View {
    @ObservedObject var model: RoomcutViewModel
    // White over a dark wash, black over a bright dark-mode wash, normal tokens
    // otherwise — synced with the tab bar.
    var darkBackdrop: Bool = false
    var brightBackdrop: Bool = false
    @Environment(\.colorScheme) private var scheme
    private let chromeControlLift: CGFloat = 10

    private var ink: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    // Master switch: ON when Roomcut is the system default output (audio routed
    // through the engine), OFF when it's fully out of the path.
    private var isOn: Bool { model.roomcutIsDefault }
    private var deviceName: String {
        model.outputDevices.first { $0.uid == model.selectedDeviceUID }?.name ?? "출력 장치"
    }
    private var dotColor: Color {
        if !model.status.reachable { return RoomcutTokens.red }
        if !isOn { return RoomcutTokens.amber }
        return RoomcutTokens.green
    }

    var body: some View {
        HStack(spacing: 10) {
            // Device selector — no focus ring, background only while pressed.
            Menu {
                ForEach(model.outputDevices) { d in
                    Button {
                        model.selectDevice(d.uid)
                    } label: {
                        if d.uid == model.selectedDeviceUID {
                            Label(d.name, systemImage: "checkmark")
                        } else { Text(d.name) }
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: "hifispeaker").font(.system(size: 11, weight: .medium))
                    Text(deviceName)
                        .font(.system(size: 12, weight: .medium))
                        .lineLimit(1).truncationMode(.tail)
                    Image(systemName: "chevron.down").font(.system(size: 8, weight: .semibold))
                }
                .foregroundStyle(ink)
                .padding(.horizontal, 11).padding(.vertical, 6)
            }
            .menuStyle(.button)
            .buttonStyle(PressGlassButtonStyle())
            .menuIndicator(.hidden)
            .fixedSize()
            .focusable(false)
            .disabled(!model.status.reachable)
            .accessibilityLabel("출력 장치, 현재 \(deviceName)")
            .offset(y: -chromeControlLift)

            WindowDragHandle()
                .frame(maxWidth: .infinity)
                .frame(height: 32)

            // ON / OFF — master switch (routes the system default in/out of
            // Roomcut). Background only while pressed.
            Button {
                model.setMasterEnabled(!isOn)
            } label: {
                HStack(spacing: 7) {
                    Circle().fill(dotColor).frame(width: 7, height: 7)
                    Text(isOn ? "ON" : "OFF")
                        .font(.system(size: 12, weight: .semibold))
                        .foregroundStyle(ink)
                }
                .padding(.horizontal, 12).padding(.vertical, 6)
            }
            .buttonStyle(PressGlassButtonStyle())
            .focusable(false)
            .disabled(!model.status.reachable)
            .accessibilityLabel("Roomcut 전원")
            .accessibilityValue(isOn ? "켜짐" : "꺼짐")
            .offset(y: -chromeControlLift)
        }
        .frame(height: 32)
    }
}

// A button whose glass background appears only while pressed (no resting pill,
// no focus ring) — used for the chrome controls that should read as bare text.
struct PressGlassButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background {
                if configuration.isPressed {
                    Capsule().fill(.regularMaterial)
                }
            }
            .contentShape(Capsule())
            .opacity(configuration.isPressed ? 0.85 : 1)
    }
}

// MARK: - Bottom tab bar (Liquid Glass bar; subtle selection pill)

private struct BottomTabBar: View {
    @Binding var selection: RoomcutTab
    // When the Home Cover/Mesh wash is dark (light mode), the bar + its text go white
    // to match the Now Playing chrome (item 4) — linked to NowPlayingInk. A BRIGHT
    // dark-mode wash flips them to black instead (same judgement, other direction).
    var darkBackdrop: Bool = false
    var brightBackdrop: Bool = false
    var blendsWithOpenSheet: Bool = false
    var onReselect: ((RoomcutTab) -> Void)? = nil
    @Environment(\.colorScheme) private var scheme
    @Namespace private var pillNS

    // Brighter fill + white ink over a dark wash; otherwise the normal tokens.
    // Dark mode uses plain white for the unselected tabs (the tertiary grey token
    // read as washed-out) — selection still stands out via the pill + fill glyph.
    private var unselectedInk: Color {
        if darkBackdrop { return Color.white.opacity(0.66) }
        if brightBackdrop { return Color.black.opacity(0.66) }
        return scheme == .dark ? .white : RoomcutTokens.textTertiary(scheme)
    }
    private var fillColor: Color {
        if darkBackdrop { return Color.white.opacity(0.30) }
        if brightBackdrop { return Color.white.opacity(0.28) }
        return (scheme == .dark ? Color.black : Color.white).opacity(scheme == .dark ? 0.24 : 0.28)
    }
    // Selection is monochrome (HIG: no accent on a glass tab bar over rich content) —
    // a bright/dark glass-highlight capsule reads as Liquid Glass, not a blue button.
    private var selectedInk: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    private var pillFill: Color {
        if darkBackdrop { return Color.white.opacity(0.24) }
        if brightBackdrop { return Color.black.opacity(0.08) }
        return (scheme == .dark ? Color.white : Color.black).opacity(scheme == .dark ? 0.16 : 0.08)
    }

    var body: some View {
        HStack(spacing: 2) {
            ForEach(RoomcutTab.allCases) { t in
                tabButton(t)
            }
        }
        .padding(.horizontal, 6)
        .padding(.vertical, 6)
        // A plain white/black fill (NOT a glass tint, which dims when the window is
        // inactive) so the bar looks the SAME focused or not; clear glass on top.
        .background(Capsule().fill(blendsWithOpenSheet ? Color.clear : fillColor))
        .glassEffect(blendsWithOpenSheet ? .identity : .clear, in: Capsule())
    }

    private func tabButton(_ t: RoomcutTab) -> some View {
        let selected = selection == t
        return Button {
            if selection == t {
                onReselect?(t)
            } else {
                withAnimation(.smooth(duration: 0.3)) { selection = t }
            }
        } label: {
            VStack(spacing: 3) {
                Image(systemName: t.icon)
                    .font(.system(size: 16, weight: .medium))
                    .symbolVariant(selected ? .fill : .none)
                Text(t.title)
                    .font(.system(size: 9, weight: selected ? .semibold : .medium))
                    .lineLimit(1).minimumScaleFactor(0.75)
            }
            .foregroundStyle(selected ? selectedInk : unselectedInk)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 7)
            .background {
                // A flat, low-opacity selection pill UNDER the glyph (no glass
                // blur on top), so the icon/label stay crisp. It slides between
                // tabs via matchedGeometryEffect.
                if selected {
                    Capsule()
                        .fill(pillFill)
                        .matchedGeometryEffect(id: "selPill", in: pillNS)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(t.title)
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
    }
}
