import XCTest
@testable import RoomcutCore

// EQ-01 on the app side: what a dynamic band does to a preset file, to the
// clamps, to the engine payload, and to a session that talks to an engine
// without the capability.
@MainActor
final class DynamicEqTests: XCTestCase {
    private func dynamicBand() -> ParametricBand {
        ParametricBand(enabled: true, type: 0, freqHz: 240, gainDb: -3, q: 1.4,
                       dynamic: true, thresholdDb: -21, rangeDb: 7.5, attackMs: 15, releaseMs: 180)
    }

    private func status(dynamicEq: Bool) -> EngineStatus {
        var value = EngineStatus()
        value.reachable = true
        value.presetId = "flat"
        value.state = EngineStatus.running
        value.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.parametricCapability
            | EngineStatus.dynamicsCapability | (dynamicEq ? EngineStatus.dynamicEqCapability : 0)
        return value
    }

    func testStaticBandJSONIsUnchanged() throws {
        let band = ParametricBand(enabled: true, type: 0, freqHz: 1000, gainDb: -2, q: 1)
        let json = try XCTUnwrap(String(data: try JSONEncoder().encode(band), encoding: .utf8))
        for key in ["dynamic", "thresholdDb", "rangeDb", "attackMs", "releaseMs"] {
            XCTAssertFalse(json.contains(key), "a static band writes no \(key)")
        }
    }

    func testAPresetWrittenBeforeThisDecodesAsStatic() throws {
        let older = Data(#"{"enabled":true,"type":0,"freqHz":900,"gainDb":-4,"q":1.2}"#.utf8)
        let band = try JSONDecoder().decode(ParametricBand.self, from: older)
        XCTAssertFalse(band.dynamic)
        XCTAssertEqual(band.rangeDb, 0)
        XCTAssertEqual(band.freqHz, 900)
    }

    func testDynamicBandSurvivesAPresetRoundTrip() throws {
        let band = dynamicBand()
        let decoded = try JSONDecoder().decode(ParametricBand.self, from: try JSONEncoder().encode(band))
        XCTAssertEqual(decoded, band)
    }

    func testOutOfRangeDynamicValuesAreClamped() {
        var params = EngineParameters.flat
        params.parametric[0] = ParametricBand(enabled: true, type: 0, freqHz: 500, gainDb: 0, q: 1,
                                              dynamic: true, thresholdDb: -400, rangeDb: 90,
                                              attackMs: 0, releaseMs: 99999)
        let band = params.normalized().parametric[0]
        XCTAssertEqual(band.thresholdDb, -60)
        XCTAssertEqual(band.rangeDb, 24)
        XCTAssertEqual(band.attackMs, 1)
        XCTAssertEqual(band.releaseMs, 2000)
    }

    func testOnlyBellsAndShelvesStayDynamic() {
        var params = EngineParameters.flat
        params.parametric[0] = ParametricBand(enabled: true, type: 5, freqHz: 500, gainDb: 0, q: 1,
                                              dynamic: true, rangeDb: 6)   // notch
        params.parametric[1] = ParametricBand(enabled: true, type: 2, freqHz: 8000, gainDb: -2, q: 1,
                                              dynamic: true, rangeDb: 6)   // high shelf
        let bands = params.normalized().parametric
        XCTAssertFalse(bands[0].dynamic, "a notch has no gain to take away")
        XCTAssertTrue(bands[1].dynamic)
    }

    func testAnEngineWithoutTheCapabilityGetsStaticBands() {
        var params = EngineParameters.flat
        params.parametric[0] = dynamicBand()
        XCTAssertTrue(params.supported(by: status(dynamicEq: true)).parametric[0].dynamic)
        let stripped = params.supported(by: status(dynamicEq: false)).parametric[0]
        XCTAssertFalse(stripped.dynamic, "the band is sent as the static filter it describes")
        XCTAssertEqual(stripped.freqHz, 240, "and keeps everything else")
        XCTAssertEqual(stripped.gainDb, -3)
    }

    func testTheEnginePayloadCarriesTheDynamicSide() {
        var params = EngineParameters.flat
        params.parametric[2] = dynamicBand()
        let restored = EngineParameters(native: params.nativeValues())
        XCTAssertEqual(restored.parametric[2], params.normalized().parametric[2])
        XCTAssertFalse(restored.parametric[0].dynamic, "untouched bands stay static")
    }

    func testTurningItOnGivesTheBandSomethingToTakeOff() async {
        let client = FakeEngineClient()
        client.states = [status(dynamicEq: true)]
        client.params = .flat
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)
        await model.refreshNow()
        XCTAssertTrue(model.dynamicEqAvailable)
        model.setParametricEnabled(0, true)
        model.setParametricDynamic(0, true)
        XCTAssertTrue(model.parametric[0].dynamic)
        XCTAssertGreaterThan(model.parametric[0].rangeDb, 0, "a range of zero would look broken")
        model.setParametricRange(0, 12)
        model.setParametricDynamic(0, false)
        model.setParametricDynamic(0, true)
        XCTAssertEqual(model.parametric[0].rangeDb, 12, "an existing range is left alone")
    }
}
