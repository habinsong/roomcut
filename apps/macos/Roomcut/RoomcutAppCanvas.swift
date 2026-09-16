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
    let roomTune: RoomTuneMeasurement
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
        roomTune.cancel()
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
        case .tune:     RoomTuneTab(model: model, measurement: roomTune)
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
