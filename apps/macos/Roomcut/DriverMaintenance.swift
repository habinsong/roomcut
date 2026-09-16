import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// Driver install/uninstall helpers surfaced in Settings. These shell out to the
// installer scripts, which need the user's own authorisation — the app never
// elevates by itself.
// Driver-level maintenance: reinstalling the HAL plug-in and removing Roomcut
// both write to /Library, so each runs ONE privileged shell command through
// `osascript … with administrator privileges` — a single system auth prompt, and
// no sudoers rule to install (unlike EngineService's launchctl verbs).
enum DriverMaintenance {
    enum Outcome {
        case done
        case cancelled          // user dismissed the authorization prompt
        case unavailable        // the payload this action needs isn't on disk
        case failed(String)     // anything else; carries osascript's stderr
    }

    static let halDriver = "/Library/Audio/Plug-Ins/HAL/Roomcut.driver"
    static let uninstaller = "/Library/Application Support/Roomcut/uninstall.sh"

    // The driver copy shipped inside the app (Contents/PlugIns, where codesign
    // seals a nested bundle), so a reinstall needs nothing but the app itself.
    static var bundledDriver: URL? {
        guard let url = Bundle.main.builtInPlugInsURL?
            .appendingPathComponent("Roomcut.driver"),
              FileManager.default.fileExists(atPath: url.path) else { return nil }
        return url
    }

    // Replace the installed plug-in with the bundled one and restart coreaudiod so
    // it reloads — the same steps as scripts/install-driver.sh, minus the engine
    // (this action is scoped to the driver; the daemon keeps running).
    static func reinstallDriver() -> Outcome {
        guard let src = bundledDriver else { return .unavailable }
        let dest = quoted(halDriver)
        let command = [
            "/bin/rm -rf \(dest)",
            "/bin/cp -R \(quoted(src.path)) \(dest)",
            "/usr/sbin/chown -R root:wheel \(dest)",
            "(/usr/bin/xattr -dr com.apple.quarantine \(dest) 2>/dev/null || true)",
            "(/usr/bin/killall -9 coreaudiod 2>/dev/null || true)",
        ].joined(separator: " && ")
        return runPrivileged(command)
    }

    // The uninstaller dropped next to the install by install.sh / the pkg. It
    // removes the driver, the engine daemon and /Applications/Roomcut.app.
    static func removeRoomcut() -> Outcome {
        guard FileManager.default.isReadableFile(atPath: uninstaller) else { return .unavailable }
        return runPrivileged("/bin/bash \(quoted(uninstaller))")
    }

    private static func quoted(_ path: String) -> String {
        "'" + path.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    // Call off the main thread: the authorization prompt blocks until dismissed.
    private static func runPrivileged(_ command: String) -> Outcome {
        let escaped = command
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        p.arguments = ["-e", "do shell script \"\(escaped)\" with administrator privileges"]
        let errPipe = Pipe()
        p.standardOutput = FileHandle.nullDevice
        p.standardError = errPipe
        do {
            try p.run()
            let stderr = String(decoding: errPipe.fileHandleForReading.readDataToEndOfFile(),
                                as: UTF8.self)
            p.waitUntilExit()
            if p.terminationStatus == 0 { return .done }
            // -128 is AppleScript's "user cancelled" — not a failure worth alerting.
            if stderr.contains("-128") { return .cancelled }
            return .failed(stderr.trimmingCharacters(in: .whitespacesAndNewlines))
        } catch {
            return .failed(error.localizedDescription)
        }
    }
}
