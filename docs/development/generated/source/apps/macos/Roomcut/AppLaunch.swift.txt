import Foundation

// Set by main.swift from fixture/appearance/attach options before
// the SwiftUI app starts. Plain Sendable statics so they can be written from
// top-level main code; the @MainActor App reads them when constructing its view.
enum AppLaunch {
    static var fixtureKind: UIFixtureKind?
    static var appearance: UIAppearance?
    static var attachToRunningEngine = false
    static var managesEngine: Bool { fixtureKind == nil && !attachToRunningEngine }
    static func traceRoomTune(_ message: String) {
        guard fixtureKind == .uiRoomTune else { return }
        print("ROOMTUNE_QA \(ProcessInfo.processInfo.systemUptime) \(message)")
        fflush(stdout)
    }
}
