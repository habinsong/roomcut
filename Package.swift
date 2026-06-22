// swift-tools-version: 6.2
//
// Package.swift — builds the native macOS app (SwiftUI + AppKit) and the C
// client library it talks through. Lives at the repo root so targets can
// reference the engine's client sources; the native driver/engine themselves
// stay on CMake (this package never compiles them).
//
//   swift build                    # CRoomcutClient + Roomcut (menu-bar app)
//   swift run Roomcut --selftest   # headless: connect to the engine, verify
//   scripts/build-app.sh           # assemble build/Roomcut.app (LSUIElement)
import PackageDescription

let package = Package(
    name: "Roomcut",
    platforms: [.macOS(.v26)],
    targets: [
        // Plain-C client API over the engine's Mach control plane. Wraps the
        // C++ transport (Control.cpp) + builtin preset table behind
        // engine/client/include/roomcut_client.h — the only header Swift sees.
        .target(
            name: "CRoomcutClient",
            path: "engine",
            sources: [
                "src/Control.cpp",
                "client/ClientShim.cpp",
            ],
            publicHeadersPath: "client/include",
            cxxSettings: [
                .headerSearchPath("include"),
                .headerSearchPath("../shared/protocol"),
                .headerSearchPath("../core"),
                .headerSearchPath("../core/dsp"),     // bare ChainParams.hpp include
                .headerSearchPath("../core/presets"), // (matches engine/CMakeLists)
            ],
            linkerSettings: [
                // ClientShim enumerates output devices + reads/writes the Roomcut
                // device volume directly via CoreAudio (in-process, no engine).
                .linkedFramework("CoreAudio"),
                .linkedFramework("CoreFoundation"),
            ]
        ),
        .target(
            name: "RoomcutPresentationCore",
            path: "apps/macos/Roomcut",
            exclude: [
                "EngineClient.swift",
                "RoomcutViewModel.swift",
                "main.swift",
                "RoomcutApp.swift",
                "MenuContent.swift",
                "MainWindow.swift",
                "WindowChrome.swift",
                "RoomcutAppCanvas.swift",
                "HomeTab.swift",
                "SpaceTab.swift",
                "RoomTuneInputScanner.swift",
                "RoomTuneAnalysis.swift",
                "RoomTuneRecorder.swift",
                "RoomTuneSweep.swift",
                "RoomTuneMeasurement.swift",
                "RoomTuneTab.swift",
                "InspectTab.swift",
                "SettingsTab.swift",
                "NowPlayingView.swift",
                "NowPlayingMonitor.swift",
                "NowPlayingPayload.swift",
                "LRCLIBClient.swift",
                "RoomcutGlassStyle.swift",
                "RoomcutBackgroundLayer.swift",
                "RoomcutMacroControls.swift",
                "EqMiniCurve.swift",
                "AdvancedControls.swift",
                "EqualizerView.swift",
                "ParametricEditor.swift",
                "AudioStatusView.swift",
                "UIFixture.swift",
                "SelfTest.swift",
            ],
            sources: [
                "RoomcutPresentation.swift",
                "RoomcutMainPresentation.swift",
                "RoomcutTheme.swift",
            ],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .target(
            name: "RoomcutCore",
            dependencies: ["CRoomcutClient", "RoomcutPresentationCore"],
            path: "apps/macos/Roomcut",
            exclude: [
                "main.swift",
                "RoomcutApp.swift",
                "MenuContent.swift",
                "MainWindow.swift",
                "WindowChrome.swift",
                "RoomcutAppCanvas.swift",
                "HomeTab.swift",
                "SpaceTab.swift",
                "RoomTuneInputScanner.swift",
                "RoomTuneAnalysis.swift",
                "RoomTuneRecorder.swift",
                "RoomTuneSweep.swift",
                "RoomTuneMeasurement.swift",
                "RoomTuneTab.swift",
                "InspectTab.swift",
                "SettingsTab.swift",
                "NowPlayingView.swift",
                "NowPlayingMonitor.swift",
                "RoomcutGlassStyle.swift",
                "RoomcutBackgroundLayer.swift",
                "RoomcutMacroControls.swift",
                "EqMiniCurve.swift",
                "AdvancedControls.swift",
                "EqualizerView.swift",
                "ParametricEditor.swift",
                "AudioStatusView.swift",
                "UIFixture.swift",
                "SelfTest.swift",
                "RoomcutPresentation.swift",
                "RoomcutMainPresentation.swift",
                "RoomcutTheme.swift",
            ],
            sources: [
                "EngineClient.swift",
                "RoomcutViewModel.swift",
                "NowPlayingPayload.swift",
                "LRCLIBClient.swift",
            ],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        // The menu-bar app: SwiftUI MenuBarExtra UI over an AppKit accessory
        // activation policy. `--selftest` exercises the client headlessly.
        .executableTarget(
            name: "Roomcut",
            dependencies: ["CRoomcutClient", "RoomcutCore", "RoomcutPresentationCore"],
            path: "apps/macos/Roomcut",
            exclude: [
                "EngineClient.swift",
                "RoomcutViewModel.swift",
                "RoomcutPresentation.swift",
                "RoomcutMainPresentation.swift",
                "RoomcutTheme.swift",
                "NowPlayingPayload.swift",
                "LRCLIBClient.swift",
            ],
            sources: [
                "main.swift",
                "RoomcutApp.swift",
                "MainWindow.swift",
                "WindowChrome.swift",
                "RoomcutAppCanvas.swift",
                "HomeTab.swift",
                "SpaceTab.swift",
                "RoomTuneInputScanner.swift",
                "RoomTuneAnalysis.swift",
                "RoomTuneRecorder.swift",
                "RoomTuneSweep.swift",
                "RoomTuneMeasurement.swift",
                "RoomTuneTab.swift",
                "InspectTab.swift",
                "SettingsTab.swift",
                "NowPlayingView.swift",
                "NowPlayingMonitor.swift",
                "RoomcutGlassStyle.swift",
                "RoomcutBackgroundLayer.swift",
                "RoomcutMacroControls.swift",
                "EqMiniCurve.swift",
                "AdvancedControls.swift",
                "MenuContent.swift",
                "EqualizerView.swift",
                "ParametricEditor.swift",
                "AudioStatusView.swift",
                "UIFixture.swift",
                "SelfTest.swift",
            ],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .testTarget(
            name: "RoomcutTests",
            dependencies: ["RoomcutCore", "RoomcutPresentationCore"],
            path: "apps/macos/RoomcutTests",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ],
    cxxLanguageStandard: .cxx17
)
