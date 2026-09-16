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
