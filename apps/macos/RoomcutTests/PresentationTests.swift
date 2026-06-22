import XCTest
@testable import RoomcutPresentationCore

final class PresentationTests: XCTestCase {
    func testAllEngineStatesHaveLabelAndSymbol() {
        let cases: [(reachable: Bool, state: UInt32, label: String, symbol: String)] = [
            (false, 0, "연결 끊김", "waveform.slash"),
            (true, 0, "오프라인", "waveform.slash"),
            (true, 1, "실행 중", "waveform"),
            (true, 2, "바이패스", "waveform.slash"),
            (true, 3, "복구 중", "exclamationmark.triangle"),
        ]

        for item in cases {
            let status = RoomcutPresentation.status(reachable: item.reachable, state: item.state)

            XCTAssertEqual(status.label, item.label)
            XCTAssertEqual(status.symbol, item.symbol)
        }
    }

    func testStatusIsDistinguishableWithoutColor() {
        let running = RoomcutPresentation.status(reachable: true, state: 1)
        let bypass = RoomcutPresentation.status(reachable: true, state: 2)
        let recover = RoomcutPresentation.status(reachable: true, state: 3)
        let offline = RoomcutPresentation.status(reachable: false, state: 0)

        XCTAssertNotEqual(running.label, bypass.label)
        XCTAssertNotEqual(running.symbol, bypass.symbol)
        XCTAssertNotEqual(recover.label, offline.label)
        XCTAssertNotEqual(recover.role, offline.role)
    }

    func testPeakDbConversionClampsToMeterRange() {
        XCTAssertEqual(RoomcutPresentation.peakDbFS(0), -60)
        XCTAssertEqual(RoomcutPresentation.peakDbFS(1), 0, accuracy: 0.001)
        XCTAssertEqual(RoomcutPresentation.peakDbFS(2), 0)
        XCTAssertEqual(RoomcutPresentation.peakDbFS(0.001), -60)
        XCTAssertEqual(RoomcutPresentation.peakDbFS(0.5), -6.0206, accuracy: 0.001)
    }

    func testPhase7PresetListIncludesSpatialModes() {
        let presets = [
            "flat",
            "clean",
            "dialogue",
            "night",
            "soft",
            "laptop-speaker",
            "airpods",
            "original-focus",
            "widen",
        ].filter { RoomcutPresentation.phase7PresetIds.contains($0) }

        XCTAssertEqual(presets, ["flat", "clean", "dialogue", "night", "soft", "laptop-speaker", "airpods", "original-focus", "widen"])
    }

    func testLimiterVisibilityRule() {
        XCTAssertFalse(RoomcutPresentation.shouldShowLimiter(gainReductionDb: 0.05))
        XCTAssertTrue(RoomcutPresentation.shouldShowLimiter(gainReductionDb: 0.051))
        XCTAssertTrue(RoomcutPresentation.shouldShowLimiter(gainReductionDb: -0.051))
    }

    func testThemeLayoutMatchesPhase6Bounds() {
        XCTAssertEqual(RoomcutTheme.layout.menuWidth, 340)
        XCTAssertEqual(RoomcutTheme.layout.menuMaxHeight, 420)
        XCTAssertEqual(RoomcutTheme.layout.mainWindowWidth, 1040)
        XCTAssertEqual(RoomcutTheme.layout.mainWindowHeight, 700)
        XCTAssertEqual(RoomcutTheme.layout.mainWindowMinWidth, 920)
        XCTAssertEqual(RoomcutTheme.layout.mainWindowMinHeight, 620)
        XCTAssertEqual(RoomcutTheme.layout.inspectorWidth, 240)
        XCTAssertLessThanOrEqual(RoomcutTheme.layout.cornerRadius, 8)
    }

    func testEqControlCurveUsesLogFrequencyOrder() {
        // Band order + range must match the engine contract.
        XCTAssertEqual(EqBands.count, 10)
        XCTAssertEqual(EqBands.centersHz, [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000])
        XCTAssertEqual(EqBands.labels, ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"])
        XCTAssertEqual(EqBands.gainRange, -24...24)

        // Endpoints anchor the axis; placement is strictly increasing.
        XCTAssertEqual(EqBands.normalizedX(0), 0, accuracy: 1e-9)
        XCTAssertEqual(EqBands.normalizedX(9), 1, accuracy: 1e-9)
        for i in 1..<EqBands.count {
            XCTAssertGreaterThan(EqBands.normalizedX(i), EqBands.normalizedX(i - 1))
        }

        // Logarithmic, not linear: 1 kHz (band 5) sits near the middle of the
        // axis (~0.556), nowhere near where a linear-frequency plot would put
        // it ((1000−31)/(16000−31) ≈ 0.061).
        let oneKHz = EqBands.normalizedX(5)
        XCTAssertEqual(oneKHz, 0.5561, accuracy: 0.005)
        let linearPos = (1000.0 - 31) / (16000 - 31)
        XCTAssertGreaterThan(oneKHz, linearPos + 0.4)
    }

    func testEqGainToNormalizedYCentersZero() {
        XCTAssertEqual(EqBands.normalizedY(gainDb: 0), 0.5, accuracy: 1e-9)
        XCTAssertEqual(EqBands.normalizedY(gainDb: 24), 1, accuracy: 1e-9)
        XCTAssertEqual(EqBands.normalizedY(gainDb: -24), 0, accuracy: 1e-9)
    }

    func testMeterRoleUsesSignalMeaningNotColorNames() {
        XCTAssertEqual(RoomcutTheme.meterRole(limiterActive: false, underrunsVisible: false), .peak)
        XCTAssertEqual(RoomcutTheme.meterRole(limiterActive: true, underrunsVisible: false), .limiter)
        XCTAssertEqual(RoomcutTheme.meterRole(limiterActive: false, underrunsVisible: true), .warning)
        XCTAssertEqual(RoomcutTheme.surfaceRole(for: 0), .window)
        XCTAssertEqual(RoomcutTheme.surfaceRole(for: 1), .panel)
    }

    func testAnalyzerMetricLabelsAreRoundedForStableReading() {
        XCTAssertEqual(RoomcutAnalysisPresentation.db(-12.4), "−12 dB")
        XCTAssertEqual(RoomcutAnalysisPresentation.db(0), "0 dB")
        XCTAssertEqual(RoomcutAnalysisPresentation.percent(0.72), "70%")
        XCTAssertEqual(RoomcutAnalysisPresentation.hz(1368), "1.4 kHz")
    }
}
