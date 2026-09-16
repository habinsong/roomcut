import AppKit
import Combine
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// Application lifecycle: launching, the status item, window restoration, and
// shutting the engine down cleanly.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let model: RoomcutViewModel
    let monitor = NowPlayingMonitor()
    let roomTune = RoomTuneMeasurement()

    private var statusItem: NSStatusItem?
    private var panel: NSPanel?
    private var clickMonitor: Any?
    private var mainWindow: RoomcutMainWindow?
    private var statusObs: AnyCancellable?
    private var editMenu: SoundEditMenuController?

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
            if AppLaunch.attachToRunningEngine { showMainWindow() }
        }

        setupStatusItem()
        editMenu = SoundEditMenuController(model: model)
        DispatchQueue.main.async { [weak self] in self?.editMenu?.install() }
        model.startPolling()
        if AppLaunch.managesEngine {
            // Bring the audio engine up with the app (off the main thread so a
            // cold launchd start never stalls the UI); quitting takes it down.
            DispatchQueue.global(qos: .userInitiated).async { EngineService.start() }
            // A relaunch (`pkill && open`) races the OLD instance's quit-time
            // bootout against this bootstrap: when the bootout lands second it
            // silently takes the fresh engine down, and nothing brings it back
            // (boot autostart is disabled by design) — the app then sits
            // unreachable forever. Re-assert the start while the control plane
            // stays unreachable; bounded and idempotent (enable+bootstrap are
            // no-ops on a live service).
            scheduleEngineStartRetry(attempt: 1)
        }
        if AppLaunch.fixtureKind == nil { monitor.start() }
    }

    private func scheduleEngineStartRetry(attempt: Int) {
        guard AppLaunch.managesEngine, attempt <= 3 else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + (attempt == 1 ? 4.0 : 10.0)) { [weak self] in
            guard let self else { return }
            let status = self.model.status
            // "Engine up but the driver never handed off": STOPPED with zero
            // frames rendered. The driver's reconnect worker only re-engages on
            // a FRESH Mach service registration, so an engine that came up
            // while the driver was already waiting sits silent forever.
            let driverNeverConnected = status.reachable
                && status.state == EngineStatus.stopped && status.frames == 0
            if !status.reachable {
                DispatchQueue.global(qos: .userInitiated).async { EngineService.start() }
            } else if driverNeverConnected && attempt >= 2 {
                // Bounce (bootout → bootstrap) to re-register the service and
                // wake the driver — verified live; harmless while no audio is
                // flowing. Attempt 1 just waits: a slow first HELLO usually
                // lands within the first window.
                DispatchQueue.global(qos: .userInitiated).async {
                    EngineService.stop()
                    EngineService.start()
                }
            } else if !driverNeverConnected {
                return  // healthy (or genuinely streaming) — stop retrying
            }
            self.scheduleEngineStartRetry(attempt: attempt + 1)
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        // Menu-bar app stays alive; the custom main window isn't tracked here.
        false
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard roomTune.isBusy else { return .terminateNow }
        Task {
            await roomTune.cancelAndWait()
            sender.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }

    func applicationWillTerminate(_ notification: Notification) {
        // Stop the engine when the app quits. Synchronous: `bootout` SIGTERMs the
        // engine, which restores the real default output before exiting, so we
        // wait for that to finish before the process goes away.
        guard AppLaunch.managesEngine else { return }
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
            MainWindow(model: model, monitor: monitor, roomTune: roomTune)
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
