//
// main.swift — Roomcut menu-bar app entry point.
//
// `--selftest` exercises the C client against a running engine and exits with
// 0/1 — the headless path the verification scripts drive. Anything else
// launches the SwiftUI app (RoomcutApp).
//
import AppKit

if CommandLine.arguments.contains("--selftest") {
    exit(runSelfTest())
}

// `--ui-fixture <state>`: launch the real UI backed by an in-memory client.
if let idx = CommandLine.arguments.firstIndex(of: "--ui-fixture"),
   idx + 1 < CommandLine.arguments.count {
    if let kind = UIFixtureKind(rawValue: CommandLine.arguments[idx + 1]) {
        AppLaunch.fixtureKind = kind
    } else {
        FileHandle.standardError.write(
            ("unknown --ui-fixture state; valid: "
             + UIFixtureKind.allCases.map(\.rawValue).joined(separator: ", ") + "\n")
                .data(using: .utf8)!)
        exit(2)
    }
}

// `--ui-appearance light|dark`: optional colour-scheme override for Light/Dark QA.
if let idx = CommandLine.arguments.firstIndex(of: "--ui-appearance"),
   idx + 1 < CommandLine.arguments.count {
    if let mode = UIAppearance(rawValue: CommandLine.arguments[idx + 1]) {
        AppLaunch.appearance = mode
    } else {
        FileHandle.standardError.write(
            "unknown --ui-appearance mode; valid: light, dark\n".data(using: .utf8)!)
        exit(2)
    }
}

private let isRunningTests = CommandLine.arguments.contains { $0.contains(".xctest") }

if !isRunningTests {
    RoomcutApp.main()
}
