//
// RoomcutApp.swift — the app entry. The menu bar is a hand-rolled NSStatusItem
// driving a transparent NSPanel; the MAIN window is also a custom AppKit window
// (RoomcutMainWindow) created here, because a SwiftUI Window scene can't be both
// borderless (no title-bar gap) AND key-capable (text fields focusable) at once.
// AppDelegate owns the engine model + Now Playing monitor so every surface
// shares one of each.
//
import AppKit
import Combine
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// A borderless window that can still become key/main, so text fields (e.g. the
// knob dB entry) accept focus while the window hugs its content with no gap.
final class RoomcutMainWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }

    fileprivate var allowsTransientResize = false
    fileprivate var usesCompactSizing = false
    private var isLiveResizing = false
    private let softWindowTiming = CAMediaTimingFunction(controlPoints: 0.22, 0.92, 0.26, 1)
    private let resizeEdgeBand: CGFloat = 8
    private let resizeCornerBand: CGFloat = 28

    // While the user is dragging an edge/corner, don't let AppKit clamp the window
    // to the visible screen. The layout is tall (≈2.17:1), so on a laptop display
    // the screen height is reached well before the app's own maxWidth — the screen
    // clamp froze the drag there. The only limit during resize is the app's
    // RoomcutWindowMetrics min/max (enforced in trackResize via clampedWidth).
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect {
        isLiveResizing ? frameRect : super.constrainFrameRect(frameRect, to: screen)
    }

    override func sendEvent(_ event: NSEvent) {
        if event.type == .leftMouseDown,
           let edge = resizeEdge(at: event.locationInWindow) {
            trackResize(edge: edge)
            return
        }
        super.sendEvent(event)
    }

    func collapseToNowPlaying() {
        guard isVisible else { return }
        let start = frame
        let height = RoomcutWindowMetrics.compactHeight(forWidth: start.width)
        let end = frameKeepingTop(of: start, height: height)
        allowsTransientResize = true
        applyWindowSizing(compact: true)
        hasShadow = false
        invalidateShadow()
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.42
            context.timingFunction = softWindowTiming
            animator().setFrame(end, display: true)
        }, completionHandler: { [weak self] in
            self?.allowsTransientResize = false
        })
    }

    func expandFromNowPlaying() {
        let start = frame
        let height = RoomcutWindowMetrics.height(forWidth: start.width)
        let target = frameKeepingTop(of: start, height: height)
        allowsTransientResize = true
        applyWindowSizing(compact: false)
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.36
            context.timingFunction = softWindowTiming
            animator().setFrame(target, display: true)
        }, completionHandler: { [weak self] in
            self?.restoreStandardSizing()
        })
    }

    func rollUpAndHide(completion: (() -> Void)? = nil) {
        guard isVisible else { return }
        let start = frame
        let restoreHeight = RoomcutWindowMetrics.height(forWidth: start.width)
        let restore = frameKeepingTop(of: start, height: restoreHeight)
        let end = start.insetBy(dx: start.width * 0.035, dy: start.height * 0.10)
            .offsetBy(dx: 0, dy: 8)
        let restoreShadow = hasShadow
        allowsTransientResize = true
        hasShadow = false
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.28
            context.timingFunction = softWindowTiming
            animator().setFrame(end, display: true)
            animator().alphaValue = 0
        }, completionHandler: { [weak self] in
            guard let self else { return }
            self.orderOut(nil)
            self.setFrame(restore, display: false)
            self.alphaValue = 1
            self.hasShadow = restoreShadow || start.height < restoreHeight
            self.invalidateShadow()
            self.restoreStandardSizing()
            completion?()
        })
    }

    func setAlwaysOnTop(_ enabled: Bool) {
        level = enabled ? .floating : .normal
        if enabled {
            collectionBehavior.insert([.canJoinAllSpaces, .fullScreenAuxiliary])
        } else {
            collectionBehavior.remove([.canJoinAllSpaces, .fullScreenAuxiliary])
        }
    }

    private func frameKeepingTop(of frame: NSRect, height: CGFloat) -> NSRect {
        NSRect(
            x: frame.minX,
            y: frame.maxY - height,
            width: frame.width,
            height: height
        )
    }

    private func restoreStandardSizing() {
        allowsTransientResize = false
        hasShadow = true
        invalidateShadow()
        applyWindowSizing(compact: false)
    }

    fileprivate func applyWindowSizing(compact: Bool) {
        usesCompactSizing = compact
        contentAspectRatio = NSSize(
            width: RoomcutWindowMetrics.baseWidth,
            height: compact
                ? RoomcutWindowMetrics.compactBaseHeight
                : RoomcutWindowMetrics.baseHeight
        )
        let minWidth = RoomcutWindowMetrics.minWidth
        let maxWidth = RoomcutWindowMetrics.maxWidth
        let minHeight = compact
            ? RoomcutWindowMetrics.compactHeight(forWidth: minWidth)
            : RoomcutWindowMetrics.height(forWidth: minWidth)
        let maxHeight = compact
            ? RoomcutWindowMetrics.compactHeight(forWidth: maxWidth)
            : RoomcutWindowMetrics.height(forWidth: maxWidth)
        contentMinSize = NSSize(
            width: minWidth,
            height: minHeight
        )
        contentMaxSize = NSSize(
            width: maxWidth,
            height: maxHeight
        )
        minSize = contentMinSize
        maxSize = contentMaxSize
    }

    private func resizeEdge(at point: NSPoint) -> WindowResizeEdge? {
        let windowBounds = NSRect(origin: .zero, size: frame.size)
        guard windowBounds.contains(point) else { return nil }

        let leftCorner = point.x <= resizeCornerBand
        let rightCorner = point.x >= windowBounds.width - resizeCornerBand
        let topCorner = point.y >= windowBounds.height - resizeCornerBand
        let bottomCorner = point.y <= resizeCornerBand

        if leftCorner && topCorner { return .topLeading }
        if rightCorner && topCorner { return .topTrailing }
        if leftCorner && bottomCorner { return .bottomLeading }
        if rightCorner && bottomCorner { return .bottomTrailing }
        if point.y >= windowBounds.height - resizeEdgeBand { return .top }
        if point.y <= resizeEdgeBand { return .bottom }
        if point.x <= resizeEdgeBand { return .leading }
        if point.x >= windowBounds.width - resizeEdgeBand { return .trailing }
        return nil
    }

    private func trackResize(edge: WindowResizeEdge) {
        let startFrame = frame
        let startMouse = NSEvent.mouseLocation
        NSCursor.resizeUpDown.push()
        isLiveResizing = true
        defer { isLiveResizing = false; NSCursor.pop() }

        while true {
            guard let event = nextEvent(matching: [.leftMouseDragged, .leftMouseUp]) else { break }
            guard event.type != .leftMouseUp else { break }
            let mouse = NSEvent.mouseLocation
            let width = proposedResizeWidth(
                edge: edge,
                startFrame: startFrame,
                dx: mouse.x - startMouse.x,
                dy: mouse.y - startMouse.y
            )
            let height = resizeHeight(forWidth: width)
            var nextFrame = startFrame
            nextFrame.size = NSSize(width: width, height: height)
            nextFrame.origin.x = resizeOriginX(edge: edge, startFrame: startFrame, width: width)
            nextFrame.origin.y = resizeOriginY(edge: edge, startFrame: startFrame, height: height)
            setFrame(nextFrame, display: true)
        }
    }

    private func proposedResizeWidth(
        edge: WindowResizeEdge,
        startFrame: NSRect,
        dx: CGFloat,
        dy: CGFloat
    ) -> CGFloat {
        var candidates: [CGFloat] = []
        if edge.usesTrailingDelta {
            candidates.append(startFrame.width + dx)
        }
        if edge.usesLeadingDelta {
            candidates.append(startFrame.width - dx)
        }
        if edge.usesTopDelta {
            candidates.append(resizeWidth(forHeight: startFrame.height + dy))
        }
        if edge.usesBottomDelta {
            candidates.append(resizeWidth(forHeight: startFrame.height - dy))
        }

        let proposed = candidates.max {
            abs($0 - startFrame.width) < abs($1 - startFrame.width)
        } ?? startFrame.width
        return CGFloat(RoomcutWindowMetrics.clampedWidth(Double(proposed)))
    }

    private func resizeHeight(forWidth width: CGFloat) -> CGFloat {
        let height = usesCompactSizing
            ? RoomcutWindowMetrics.compactHeight(forWidth: Double(width))
            : RoomcutWindowMetrics.height(forWidth: Double(width))
        return CGFloat(height)
    }

    private func resizeWidth(forHeight height: CGFloat) -> CGFloat {
        let baseHeight = usesCompactSizing
            ? RoomcutWindowMetrics.compactBaseHeight
            : RoomcutWindowMetrics.baseHeight
        let ratio = baseHeight / RoomcutWindowMetrics.baseWidth
        return CGFloat(RoomcutWindowMetrics.clampedWidth(Double(height) / ratio))
    }

    private func resizeOriginX(edge: WindowResizeEdge, startFrame: NSRect, width: CGFloat) -> CGFloat {
        if edge.usesLeadingDelta { return startFrame.maxX - width }
        if edge.usesTrailingDelta { return startFrame.minX }
        return startFrame.midX - width / 2
    }

    private func resizeOriginY(edge: WindowResizeEdge, startFrame: NSRect, height: CGFloat) -> CGFloat {
        if edge.usesTopDelta { return startFrame.minY }
        return startFrame.maxY - height
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let model: RoomcutViewModel
    let monitor = NowPlayingMonitor()

    private var statusItem: NSStatusItem?
    private var panel: NSPanel?
    private var clickMonitor: Any?
    private var mainWindow: RoomcutMainWindow?
    private var statusObs: AnyCancellable?

    override init() {
        if let kind = AppLaunch.fixtureKind {
            let vm = RoomcutViewModel(client: FixtureEngineClient(kind: kind))
            vm.nowPlayingFixture = kind.nowPlayingFixture
            model = vm
        } else {
            model = RoomcutViewModel()
        }
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        if let appearance = AppLaunch.appearance {
            NSApp.appearance = NSAppearance(named: appearance == .light ? .aqua : .darkAqua)
        }
        // Fixtures show the main window for QA; production is a menu-bar accessory.
        if AppLaunch.fixtureKind != nil {
            NSApp.setActivationPolicy(.regular)
            showMainWindow()
        } else {
            NSApp.setActivationPolicy(.accessory)
        }

        setupStatusItem()
        model.startPolling()
        if AppLaunch.fixtureKind == nil {
            // Bring the audio engine up with the app (off the main thread so a
            // cold launchd start never stalls the UI); quitting takes it down.
            DispatchQueue.global(qos: .userInitiated).async { EngineService.start() }
            monitor.start()
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        // Menu-bar app stays alive; the custom main window isn't tracked here.
        false
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Stop the engine when the app quits. Synchronous: `bootout` SIGTERMs the
        // engine, which restores the real default output before exiting, so we
        // wait for that to finish before the process goes away.
        guard AppLaunch.fixtureKind == nil else { return }
        EngineService.stop()
    }

    // MARK: - Main window (custom borderless, key-capable)

    func showMainWindow() {
        if let w = mainWindow {
            w.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }
        let host = NSHostingView(rootView:
            MainWindow(model: model, monitor: monitor)
                .preferredColorScheme(preferredColorScheme))
        let baseSize = NSSize(
            width: RoomcutWindowMetrics.baseWidth,
            height: RoomcutWindowMetrics.baseHeight)
        host.frame = NSRect(origin: .zero, size: baseSize)
        host.wantsLayer = true
        host.layer?.backgroundColor = NSColor.clear.cgColor

        let w = RoomcutMainWindow(
            contentRect: NSRect(origin: .zero, size: baseSize),
            styleMask: [.borderless, .fullSizeContentView],
            backing: .buffered, defer: false)
        w.isOpaque = false
        w.backgroundColor = .clear         // transparent so rounded content defines the shape
        w.hasShadow = true
        w.isMovableByWindowBackground = false   // don't let knob/slider drags move it
        w.isReleasedWhenClosed = false
        w.applyWindowSizing(compact: false)
        w.delegate = self
        w.contentView = host
        positionMainWindow(w)
        mainWindow = w
        w.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    // Open near the menu bar (top-right) but fully on-screen (#5).
    private func positionMainWindow(_ w: NSWindow) {
        guard let screen = NSScreen.main else { w.center(); return }
        let vis = screen.visibleFrame
        let x = max(vis.minX + 8, vis.maxX - w.frame.width - 16)
        let y = max(vis.minY + 8, vis.maxY - w.frame.height - 8)
        w.setFrameOrigin(NSPoint(x: x, y: y))
    }

    func windowWillResize(_ sender: NSWindow, to frameSize: NSSize) -> NSSize {
        if let window = sender as? RoomcutMainWindow, window.allowsTransientResize {
            return frameSize
        }
        let width = RoomcutWindowMetrics.clampedWidth(frameSize.width)
        let height: Double
        if let window = sender as? RoomcutMainWindow, window.usesCompactSizing {
            height = RoomcutWindowMetrics.compactHeight(forWidth: width)
        } else {
            height = RoomcutWindowMetrics.height(forWidth: width)
        }
        return NSSize(width: width, height: height)
    }

    // MARK: - Menu bar status item

    private func setupStatusItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        item.button?.action = #selector(togglePanel)
        item.button?.target = self
        statusItem = item
        updateStatusIcon()
        // Keep the menu-bar icon in sync with engine state — ON shows "waveform",
        // OFF/bypass shows "waveform.slash" (#4).
        statusObs = model.$status
            .map(\.menuBarSymbol)
            .removeDuplicates()
            .receive(on: RunLoop.main)
            .sink { [weak self] _ in self?.updateStatusIcon() }
    }

    private func updateStatusIcon() {
        let img = NSImage(systemSymbolName: model.status.menuBarSymbol,
                          accessibilityDescription: "Roomcut")
        img?.isTemplate = true
        statusItem?.button?.image = img
    }

    @objc private func togglePanel() {
        if let panel, panel.isVisible { closePanel() } else { openPanel() }
    }

    private func openPanel() {
        let host = NSHostingView(rootView:
            MenuContent(model: model, monitor: monitor,
                        onDismiss: { [weak self] in self?.closePanel() },
                        onOpenMain: { [weak self] in
                            self?.closePanel()
                            self?.showMainWindow()
                        })
                .preferredColorScheme(preferredColorScheme))
        host.sizingOptions = [.preferredContentSize]
        let size = host.fittingSize
        host.frame = NSRect(origin: .zero, size: size)

        let panel = NSPanel(contentRect: NSRect(origin: .zero, size: size),
                            styleMask: [.borderless, .nonactivatingPanel],
                            backing: .buffered, defer: false)
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.level = .popUpMenu
        panel.isReleasedWhenClosed = false
        panel.hidesOnDeactivate = false
        panel.contentView = host
        position(panel, size: size)
        panel.orderFrontRegardless()
        self.panel = panel

        clickMonitor = NSEvent.addGlobalMonitorForEvents(
            matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]
        ) { [weak self] _ in
            self?.closePanel()
        }
    }

    private func position(_ panel: NSPanel, size: NSSize) {
        guard let button = statusItem?.button, let buttonWindow = button.window else { return }
        let inWindow = button.convert(button.bounds, to: nil)
        let onScreen = buttonWindow.convertToScreen(inWindow)
        var x = onScreen.midX - size.width / 2
        var y = onScreen.minY - size.height - 6
        if let screen = buttonWindow.screen ?? NSScreen.main {
            let vis = screen.visibleFrame
            x = min(max(x, vis.minX + 8), vis.maxX - size.width - 8)
            if y < vis.minY + 8 || y > vis.maxY { y = vis.maxY - size.height - 8 }
        }
        panel.setFrameOrigin(NSPoint(x: x, y: y))
    }

    private func closePanel() {
        panel?.orderOut(nil)
        panel = nil
        if let m = clickMonitor { NSEvent.removeMonitor(m); clickMonitor = nil }
    }

    private var preferredColorScheme: ColorScheme? {
        switch AppLaunch.appearance {
        case .light: return .light
        case .dark:  return .dark
        case nil:    return nil
        }
    }
}

struct RoomcutApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        // The real UI is the custom NSWindow + menu-bar panel created in
        // AppDelegate; this empty Settings scene just satisfies App's scene
        // requirement (it never shows unless the user invokes ⌘,).
        Settings { EmptyView() }
    }
}

// Starts/stops the engine LaunchDaemon alongside the app. The daemon lives in
// the system Mach domain (only there can the driver inside coreaudiod reach it),
// so launchctl needs root — a tightly-scoped sudoers rule installed by
// scripts/install-engine.sh grants exactly these verbs without a password. When
// that rule is absent, `sudo -n` fails fast (no prompt) and the engine just
// keeps whatever state it had.
enum EngineService {
    private static let label = "com.roomcut.engine"
    private static let plist = "/Library/LaunchDaemons/com.roomcut.engine.plist"
    private static let launchctl = "/bin/launchctl"
    private static let sudo = "/usr/bin/sudo"
    private static var target: String { "system/\(label)" }

    @discardableResult
    private static func run(_ verb: [String]) -> Bool {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: sudo)
        p.arguments = ["-n", launchctl] + verb   // must match the sudoers rule exactly
        p.standardOutput = FileHandle.nullDevice
        p.standardError = FileHandle.nullDevice
        do {
            try p.run()
            p.waitUntilExit()
            return p.terminationStatus == 0
        } catch {
            return false
        }
    }

    // Clear the boot-time "disabled" flag, then load + start (RunAtLoad). If it's
    // already loaded, `bootstrap` is a harmless no-op error.
    static func start() {
        run(["enable", target])
        run(["bootstrap", "system", plist])
    }

    // Stop + unload (SIGTERM → engine restores the real default output and exits),
    // then keep it from auto-starting at the next boot.
    static func stop() {
        run(["bootout", target])
        run(["disable", target])
    }
}
