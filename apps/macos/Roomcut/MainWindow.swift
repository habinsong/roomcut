//
// MainWindow.swift — the single Roomcut control window.
//
// An iPhone-app-like surface: fixed iPhone 17 Pro aspect (~402×874), a large
// continuous "squircle" corner radius (matching iOS app icons / device corners),
// transparent borderless host window (traffic lights float over the content).
//
import SwiftUI
import AppKit
import RoomcutCore
import RoomcutPresentationCore

struct MainWindow: View {
    @ObservedObject var model: RoomcutViewModel
    @ObservedObject var monitor: NowPlayingMonitor
    @State private var compactMode = false
    @State private var keepsWindowOnTop = false

    // iPhone-style continuous corner. iOS device/app corners are ~22% of width
    // as a continuous (squircle) curve; for a 402-wide canvas ≈ 48pt reads right.
    private let corner: CGFloat = 46

    var body: some View {
        GeometryReader { geo in
            let baseHeight = compactMode
                ? RoomcutWindowMetrics.compactBaseHeight
                : RoomcutWindowMetrics.baseHeight
            let scale = min(
                geo.size.width / RoomcutWindowMetrics.baseWidth,
                geo.size.height / baseHeight)
            RoomcutAppCanvas(
                model: model,
                monitor: monitor,
                compactMode: $compactMode,
                keepsWindowOnTop: $keepsWindowOnTop
            )
                .focusEffectDisabled()   // no blue keyboard-focus rings (#2, #3)
                .frame(
                    width: RoomcutWindowMetrics.baseWidth,
                    height: baseHeight)
                .scaleEffect(scale)      // scale the fixed canvas to the window (#2)
                .frame(width: geo.size.width, height: geo.size.height)
                .clipShape(RoundedRectangle(cornerRadius: corner * scale, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: corner * scale, style: .continuous)
                        .strokeBorder(.white.opacity(compactMode ? 0 : 0.10), lineWidth: 0.5)
                )
                .background(MainWindowFrameBridge(
                    compact: compactMode,
                    keepsWindowOnTop: keepsWindowOnTop,
                    appearance: model.appearance
                ))
        }
        .ignoresSafeArea()
        .preferredColorScheme(preferredScheme)
    }

    private var preferredScheme: ColorScheme? {
        switch model.appearance {
        case .system: return nil
        case .light:  return .light
        case .dark:   return .dark
        }
    }
}

private struct MainWindowFrameBridge: NSViewRepresentable {
    let compact: Bool
    let keepsWindowOnTop: Bool
    let appearance: RoomcutAppearance

    func makeNSView(context: Context) -> NSView { MainWindowBridgeView() }

    func updateNSView(_ nsView: NSView, context: Context) {
        DispatchQueue.main.async {
            guard let bridge = nsView as? MainWindowBridgeView,
                  let window = nsView.window as? RoomcutMainWindow else { return }
            // Custom NSWindow doesn't pick up SwiftUI's .preferredColorScheme, so set
            // the window appearance directly (nil = follow the system in Auto).
            window.appearance = appearance.nsAppearance
            window.setAlwaysOnTop(keepsWindowOnTop)
            guard bridge.lastCompact != compact else { return }
            bridge.lastCompact = compact
            if compact {
                window.collapseToNowPlaying()
            } else {
                window.expandFromNowPlaying()
            }
        }
    }
}

private final class MainWindowBridgeView: NSView {
    var lastCompact: Bool?
}

private extension RoomcutAppearance {
    var nsAppearance: NSAppearance? {
        switch self {
        case .system: return nil
        case .light:  return NSAppearance(named: .aqua)
        case .dark:   return NSAppearance(named: .darkAqua)
        }
    }
}
