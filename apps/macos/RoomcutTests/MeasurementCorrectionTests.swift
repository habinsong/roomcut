import XCTest
@testable import RoomcutCore

// M5 ②: a measured response the listener brings in, read from the files that
// measurement software writes and flattened by the engine's own fitter.
@MainActor
final class MeasurementCorrectionTests: XCTestCase {
    // RBJ bell magnitude written out here, independent of the engine, so the
    // bands are checked for what they do and not only for what they say.
    private func bellDb(_ band: ParametricBand, _ f: Double, _ fs: Double) -> Double {
        let a = pow(10, band.gainDb / 40), w0 = 2 * Double.pi * band.freqHz / fs, alpha = sin(w0) / (2 * band.q)
        let b0 = 1 + alpha * a, b1 = -2 * cos(w0), b2 = 1 - alpha * a
        let a0 = 1 + alpha / a, a1 = -2 * cos(w0), a2 = 1 - alpha / a
        let w = 2 * Double.pi * f / fs
        func mag(_ c0: Double, _ c1: Double, _ c2: Double) -> Double {
            let re = c0 + c1 * cos(w) + c2 * cos(2 * w), im = -(c1 * sin(w) + c2 * sin(2 * w))
            return sqrt(re * re + im * im)
        }
        return 20 * log10(mag(b0, b1, b2) / mag(a0, a1, a2))
    }

    private func curve(_ bands: [ParametricBand], fs: Double) -> String {
        var lines = ["Frequency (Hz),Level (dB),Phase"]
        var f = 20.0
        while f <= 20000 {
            let db = -bands.reduce(0) { $0 + bellDb($1, f, fs) }
            lines.append(String(format: "%.4f,%.4f,0", f, db))
            f *= pow(2, 1.0 / 48)
        }
        return lines.joined(separator: "\n")
    }

    private func status() -> EngineStatus {
        var value = EngineStatus()
        value.reachable = true
        value.presetId = "flat"
        value.state = EngineStatus.running
        value.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.parametricCapability
        return value
    }

    func testPointsAreReadFromTheCommonTextLayouts() {
        let files = [
            "Frequency,SPL\n20,1.5\n1000,-2\n16000,3.25\n",
            "* exported\r\n* Freq(Hz)\tSPL(dB)\tPhase(degrees)\r\n20\t1.5\t12\r\n1000\t-2\t0\r\n16000\t3.25\t-40\r\n",
            "20 1.5\n\n1000   -2\n# note\n16000 3.25\n",
            "20;1.5\n1000; -2\n16000 ;3.25\n",
            "20.0, +1.5\n1e3, -2.0\n1.6e4, 3.25, 99\n",
        ]
        for text in files {
            let points = MeasurementCorrection.points(in: text)
            XCTAssertEqual(points.map { $0.freq }, [20, 1000, 16000], text)
            XCTAssertEqual(points.map { $0.db }, [1.5, -2, 3.25], text)
        }
        let rejected = MeasurementCorrection.points(in: "0,1\n-20,1\nnan,1\n100,inf\n100,abc\n100\n")
        XCTAssertTrue(rejected.isEmpty, "zero, negative and non-finite values and short lines are not points")
    }

    func testAKnownCurveIsFlattenedThroughTheEngineFitter() throws {
        let fs = 48000.0
        let truth = [ParametricBand(enabled: true, type: 0, freqHz: 2500, gainDb: -5, q: 1.5),
                     ParametricBand(enabled: true, type: 0, freqHz: 150, gainDb: 3.5, q: 0.9)]
        let correction = try MeasurementCorrection.fit(Data(curve(truth, fs: fs).utf8), sampleRate: fs)
        XCTAssertEqual(correction.bands.count, EngineParameters.paramBandCount)
        XCTAssertGreaterThanOrEqual(correction.bandsUsed, 2)
        XCTAssertEqual(correction.bands.filter { $0.enabled }.count, correction.bandsUsed)
        XCTAssertGreaterThan(correction.rmsBeforeDb, 1)
        XCTAssertLessThan(correction.rmsAfterDb, 0.3)
        // What the returned bands do, against the curve they were asked to flatten.
        var mean = 0.0, residuals: [Double] = []
        var f = 40.0
        while f <= 12000 {
            let measured = -truth.reduce(0) { $0 + bellDb($1, f, fs) }
            let applied = correction.bands.filter { $0.enabled }.reduce(0) { $0 + bellDb($1, f, fs) }
            residuals.append(measured + applied)
            f *= pow(2, 1.0 / 6)
        }
        mean = residuals.reduce(0, +) / Double(residuals.count)
        for r in residuals { XCTAssertEqual(r, mean, accuracy: 1.0) }
    }

    func testFilesWithNothingToFitSayWhy() {
        XCTAssertThrowsError(try MeasurementCorrection.fit(Data("Frequency,SPL\nn/a\n".utf8), sampleRate: 48000)) {
            XCTAssertEqual($0 as? MeasurementCorrection.Failure, .tooFewPoints(0))
        }
        XCTAssertThrowsError(try MeasurementCorrection.fit(Data("21000,1\n22000,2\n".utf8), sampleRate: 48000)) {
            XCTAssertEqual($0 as? MeasurementCorrection.Failure, .outOfRange)
        }
        let flat = try? MeasurementCorrection.fit(Data("20,-3\n20000,-3\n".utf8), sampleRate: 48000)
        XCTAssertEqual(flat?.bandsUsed, 0, "a flat curve at any level asks for no band")
    }

    func testImportReplacesTheBandsAndReachesTheEngine() async throws {
        let client = FakeEngineClient()
        client.states = [status()]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        model.setParametricBand(5, ParametricBand(enabled: true, type: 0, freqHz: 8000, gainDb: 4, q: 2))

        let truth = [ParametricBand(enabled: true, type: 0, freqHz: 3000, gainDb: -6, q: 2)]
        let correction = try XCTUnwrap(model.importMeasurement(Data(curve(truth, fs: 48000).utf8)))
        XCTAssertEqual(model.parametric, correction.bands, "every slot comes from the import, the unused ones off")
        XCTAssertFalse(model.parametric[5].enabled)

        for _ in 0..<200 where client.setParamsValues.last?.parametric != correction.bands {
            try await Task.sleep(nanoseconds: 10_000_000)
        }
        XCTAssertEqual(client.setParamsValues.last?.parametric, correction.bands)
    }

    func testAnUnreadableFileLeavesTheBandsAlone() async {
        let client = FakeEngineClient()
        client.states = [status()]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        let before = model.parametric
        XCTAssertNil(model.importMeasurement(Data("not a measurement".utf8)))
        XCTAssertEqual(model.parametric, before)
        XCTAssertNotNil(model.errorBanner)
    }
}
