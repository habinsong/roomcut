import Foundation
import AppKit
import Combine
import ImageIO
import RoomcutCore
import RoomcutPresentationCore

// Transport commands and the one-shot helper invocations behind them.
extension NowPlayingMonitor {
    // MARK: Controls

    func command(_ cmd: Command) {
        guard available, let paths = Self.helperPaths() else { return }
        if cmd == .previous {
            pendingNavigation = (.previous, Date().addingTimeInterval(5))
        } else if cmd == .next {
            pendingNavigation = (.next, Date().addingTimeInterval(5))
        }
        runOneShotAsync(paths: paths, function: "np_send",
                        env: ["ROOMCUT_NP_COMMAND": String(cmd.rawValue)])
    }

    func seek(toSeconds seconds: Double) {
        guard available, let paths = Self.helperPaths() else { return }
        let clamped = max(0, seconds)
        // Optimistic update: reflect the new position immediately so the bar
        // doesn't snap back while we wait for the next stream notification.
        if var snap = snapshot {
            let moment = Date()
            clock.rebaseline(elapsed: clamped, at: moment)
            snap.elapsedTime = clamped
            snap.timestamp = moment
            snapshot = snap
            elapsedNow = clamped
        }
        let micros = Int((clamped * 1_000_000).rounded())
        runOneShotAsync(paths: paths, function: "np_seek",
                        env: ["ROOMCUT_NP_POSITION_US": String(micros)])
    }

    // MARK: One-shot perl helpers

    // Synchronous: used only for the self-test gate (fast, blocks start()).
    func runOneShot(paths: (dylib: String, launcher: String),
                            function: String, env: [String: String]) -> Int32 {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, function]
        proc.standardOutput = FileHandle.nullDevice
        proc.standardError = FileHandle.nullDevice
        if !env.isEmpty {
            var merged = ProcessInfo.processInfo.environment
            for (k, v) in env { merged[k] = v }
            proc.environment = merged
        }
        do {
            try proc.run()
            proc.waitUntilExit()
            return proc.terminationStatus
        } catch {
            return -1
        }
    }

    // Fire-and-forget control command; never blocks the UI.
    func runOneShotAsync(paths: (dylib: String, launcher: String),
                                 function: String, env: [String: String]) {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, function]
        proc.standardOutput = FileHandle.nullDevice
        proc.standardError = FileHandle.nullDevice
        if !env.isEmpty {
            var merged = ProcessInfo.processInfo.environment
            for (k, v) in env { merged[k] = v }
            proc.environment = merged
        }
        try? proc.run()
    }
}
