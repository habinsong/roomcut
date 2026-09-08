import XCTest
@testable import RoomcutPresentationCore

// Covers the Now Playing main-shell presentation rules: engine health, On/Off
// toggle semantics, fallback labels, basic EQ macros, and launch defaults.
final class MainPresentationTests: XCTestCase {

    // MARK: EngineHealth

    func testHealthNormalWhenRunningAndClean() {
        let h = RoomcutMainPresentation.engineHealth(
            reachable: true, state: 1, manualBypass: false,
            limiterActive: false, underrunActive: false)
        XCTAssertEqual(h, .normal)
    }

    func testHealthDegradedOnBypassLimiterOrUnderrun() {
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: true, state: 1, manualBypass: true,
            limiterActive: false, underrunActive: false), .degraded)
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: true, state: 1, manualBypass: false,
            limiterActive: true, underrunActive: false), .degraded)
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: true, state: 1, manualBypass: false,
            limiterActive: false, underrunActive: true), .degraded)
        // recover / bypass engine states are also "attention".
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: true, state: 3, manualBypass: false,
            limiterActive: false, underrunActive: false), .degraded)
    }

    func testHealthStoppedWhenUnreachableOrStopped() {
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: false, state: 1, manualBypass: false,
            limiterActive: false, underrunActive: false), .stopped)
        XCTAssertEqual(RoomcutMainPresentation.engineHealth(
            reachable: true, state: 0, manualBypass: false,
            limiterActive: false, underrunActive: false), .stopped)
    }

    // metadata availability is NOT an input to health: a running clean engine
    // with no Now Playing metadata is still normal/green.
    func testMetadataUnavailableDoesNotDegradeHealth() {
        let normal = RoomcutMainPresentation.engineHealth(
            reachable: true, state: 1, manualBypass: false,
            limiterActive: false, underrunActive: false)
        let display = RoomcutMainPresentation.fallbackDisplay(reachable: true, state: 1, peak: 0)
        XCTAssertEqual(normal, .normal)
        XCTAssertEqual(display, .fallback(signalActive: false)) // "no metadata"
    }

    func testStatusDotCarriesNonColorLabel() {
        XCTAssertEqual(RoomcutMainPresentation.statusDot(for: .normal).accessibilityLabel, "Roomcut 상태: 정상")
        XCTAssertEqual(RoomcutMainPresentation.statusDot(for: .degraded).accessibilityLabel, "Roomcut 상태: 주의")
        XCTAssertEqual(RoomcutMainPresentation.statusDot(for: .stopped).accessibilityLabel, "Roomcut 상태: 정지")
    }

    // MARK: On/Off toggle

    func testRoomcutEnabledIsInverseOfManualBypass() {
        XCTAssertTrue(RoomcutMainPresentation.roomcutEnabled(manualBypass: false))
        XCTAssertFalse(RoomcutMainPresentation.roomcutEnabled(manualBypass: true))
        XCTAssertEqual(RoomcutMainPresentation.manualBypass(forEnabled: true), false)
        XCTAssertEqual(RoomcutMainPresentation.manualBypass(forEnabled: false), true)
    }

    // MARK: Now Playing fallback

    func testFallbackSignalActiveOnlyWhenRunningWithPeak() {
        XCTAssertEqual(RoomcutMainPresentation.fallbackDisplay(reachable: true, state: 1, peak: 0.5),
                       .fallback(signalActive: true))
        XCTAssertEqual(RoomcutMainPresentation.fallbackDisplay(reachable: true, state: 1, peak: 0),
                       .fallback(signalActive: false))
        XCTAssertEqual(RoomcutMainPresentation.fallbackDisplay(reachable: false, state: 1, peak: 0.5),
                       .fallback(signalActive: false))
        XCTAssertEqual(RoomcutMainPresentation.fallbackDisplay(reachable: true, state: 2, peak: 0.5),
                       .fallback(signalActive: false))
    }

    func testFallbackLabels() {
        let active = NowPlayingDisplayState.fallback(signalActive: true)
        let idle = NowPlayingDisplayState.fallback(signalActive: false)
        XCTAssertEqual(RoomcutMainPresentation.title(for: active), "System Audio")
        XCTAssertEqual(RoomcutMainPresentation.subtitle(for: active), "Signal active")
        XCTAssertEqual(RoomcutMainPresentation.subtitle(for: idle), "No media metadata available")
        // Production fallback never enables transport controls.
        XCTAssertFalse(RoomcutMainPresentation.controlsEnabled(for: active))
    }

    func testFixtureDisplayShowsSampleAndEnablesControls() {
        let fixture = NowPlayingDisplayState.fixture(
            title: "Sample Track", artist: "Sample Artist", source: "Demo", progress: 0.4)
        XCTAssertEqual(RoomcutMainPresentation.title(for: fixture), "Sample Track")
        XCTAssertEqual(RoomcutMainPresentation.subtitle(for: fixture), "Sample Artist · Demo")
        XCTAssertTrue(RoomcutMainPresentation.controlsEnabled(for: fixture))
    }

    // MARK: Sheet state transitions

    func testSheetStepsThroughCollapsedBasicAdvanced() {
        XCTAssertEqual(SoundSheetState.collapsed.expanded(), .basic)
        XCTAssertEqual(SoundSheetState.basic.expanded(), .advanced)
        XCTAssertEqual(SoundSheetState.advanced.expanded(), .advanced)
        XCTAssertEqual(SoundSheetState.advanced.collapsedOneStep(), .basic)
        XCTAssertEqual(SoundSheetState.basic.collapsedOneStep(), .collapsed)
        XCTAssertEqual(SoundSheetState.collapsed.collapsedOneStep(), .collapsed)
    }

    func testSoundControlsLevelStepsThroughMinimizedControlsExpanded() {
        XCTAssertEqual(SoundControlsLevel.minimized.expandedOneStep(), .controls)
        XCTAssertEqual(SoundControlsLevel.controls.expandedOneStep(), .expanded)
        XCTAssertEqual(SoundControlsLevel.expanded.expandedOneStep(), .expanded)
        XCTAssertEqual(SoundControlsLevel.expanded.collapsedOneStep(), .controls)
        XCTAssertEqual(SoundControlsLevel.controls.collapsedOneStep(), .minimized)
        XCTAssertEqual(SoundControlsLevel.minimized.collapsedOneStep(), .minimized)
    }

    func testLaunchDefaultsAreCollapsedHome() {
        XCTAssertEqual(RoomcutMainPresentation.launchSidebar, .collapsed)
        XCTAssertEqual(RoomcutMainPresentation.launchInspector, .collapsed)
        XCTAssertEqual(RoomcutMainPresentation.launchSheet, .collapsed)
    }

    func testWindowWidthClampsToFortyAndOneHundredFortyPercent() {
        XCTAssertEqual(RoomcutWindowMetrics.clampedWidth(80), 160.8, accuracy: 1e-9)
        XCTAssertEqual(RoomcutWindowMetrics.clampedWidth(402), 402, accuracy: 1e-9)
        XCTAssertEqual(RoomcutWindowMetrics.clampedWidth(900), 562.8, accuracy: 1e-9)
        XCTAssertEqual(RoomcutWindowMetrics.height(forWidth: 160.8), 349.6, accuracy: 1e-9)
        XCTAssertEqual(RoomcutWindowMetrics.compactHeight(forWidth: 402), 260, accuracy: 1e-9)

        let minimum = RoomcutWindowMetrics.constrainedSize(proposedWidth: 80)
        XCTAssertEqual(minimum.width, 160.8, accuracy: 1e-9)
        XCTAssertEqual(minimum.height, 349.6, accuracy: 1e-9)

        let maximum = RoomcutWindowMetrics.constrainedSize(proposedWidth: 900)
        XCTAssertEqual(maximum.width, 562.8, accuracy: 1e-9)
        XCTAssertEqual(maximum.height, 1_223.6, accuracy: 1e-9)
    }

    func testAudioFormatLabelUsesBitDepthSampleRateAndLatency() {
        XCTAssertEqual(
            RoomcutMainPresentation.audioFormatLabel(
                bitDepth: 32, sampleRate: 48_000, latencyMs: 11.6),
            "32-bit · 48 kHz · Latency 12 ms"
        )
        XCTAssertEqual(
            RoomcutMainPresentation.audioFormatLabel(
                bitDepth: 32, sampleRate: 44_100, latencyMs: 7.2),
            "32-bit · 44.1 kHz · Latency 7 ms"
        )
    }

    // MARK: EQ macros

    func testBassMacroBoostsLowBandsOnly() {
        let flat = [Double](repeating: 0, count: EqBands.count)
        let out = RoomcutMacros.apply(.bass, value: 1.0, to: flat)
        XCTAssertEqual(out[0], 3.0, accuracy: 1e-9)   // 31 Hz
        XCTAssertEqual(out[1], 6.0, accuracy: 1e-9)   // 62 Hz
        XCTAssertEqual(out[2], 6.0, accuracy: 1e-9)   // 125 Hz
        // Mid/high bands untouched.
        for i in 3..<EqBands.count { XCTAssertEqual(out[i], 0, accuracy: 1e-9) }
    }

    func testMacroPreservesUnrelatedBands() {
        var gains = [Double](repeating: 0, count: EqBands.count)
        gains[4] = 5.0   // user-set 500 Hz
        gains[9] = -3.0  // user-set 16 kHz
        let out = RoomcutMacros.apply(.vocal, value: 0.5, to: gains)
        XCTAssertEqual(out[4], 5.0, accuracy: 1e-9)   // untouched by Vocal
        XCTAssertEqual(out[9], -3.0, accuracy: 1e-9)  // untouched by Vocal
        XCTAssertEqual(out[5], 2.0, accuracy: 1e-9)   // 1k: 4.0 * 0.5
    }

    func testMacroClampsToGainRange() {
        let high = [Double](repeating: 23, count: EqBands.count)
        let out = RoomcutMacros.apply(.bass, value: 1.0, to: high)
        XCTAssertLessThanOrEqual(out[1], EqBands.gainRange.upperBound)
        XCTAssertEqual(out[1], EqBands.gainRange.upperBound, accuracy: 1e-9)
    }

    func testMacroValueIsClampedToInputRange() {
        let flat = [Double](repeating: 0, count: EqBands.count)
        // value beyond 1.0 behaves like 1.0 (no extra boost).
        let atMax = RoomcutMacros.apply(.bass, value: 5.0, to: flat)
        let atOne = RoomcutMacros.apply(.bass, value: 1.0, to: flat)
        XCTAssertEqual(atMax, atOne)
    }

    func testEveryMacroHasOnlyKnownBandIndices() {
        for macro in EqMacro.allCases {
            for index in macro.weights.keys {
                XCTAssertTrue((0..<EqBands.count).contains(index),
                              "\(macro) targets out-of-range band \(index)")
            }
        }
    }
}
