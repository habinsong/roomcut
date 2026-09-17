import XCTest
@testable import RoomcutCore

// Space presets set Surround, Room and Stage in one go, one list per output.
// The sound of each was measured in the DSP chain before it went in (see
// SpacePresetLibrary); these check what the app promises about them.
@MainActor
final class SpacePresetLibraryTests: XCTestCase {
    private func spatialModel(_ name: String) async -> (RoomcutViewModel, FakeEngineClient) {
        var status = EngineStatus()
        status.reachable = true
        status.state = EngineStatus.running
        status.presetId = "flat"
        status.capabilities = EngineStatus.spatialParamsCapability | EngineStatus.virtualRoomCapability
            | EngineStatus.upmixCapability | EngineStatus.headTrackingCapability
        let client = FakeEngineClient()
        // Every poll reads the same engine, not a default one after the first.
        client.stateReadHandler = { status }
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000,
                                     defaults: makeTestDefaults(name).defaults, headMotion: FakeHeadMotionSource())
        await model.refreshNow()
        return (model, client)
    }

    func testEachOutputHasItsOwnListStartingFromReference() {
        for headphone in [false, true] {
            let list = SpacePresetLibrary.presets(headphone: headphone)
            XCTAssertEqual(list.count, 15)
            XCTAssertTrue(list.allSatisfy { $0.headphone == headphone })
            XCTAssertEqual(Set(list.map(\.id)).count, list.count, "ids are unique")
            let reference = SpacePresetLibrary.reference(headphone: headphone)
            XCTAssertEqual(reference.surround, .off)
            XCTAssertEqual(reference.roomType, 0)
            XCTAssertEqual(reference.stage, .off)
            for group in SpacePreset.Group.allCases {
                XCTAssertFalse(list.filter { $0.group == group }.isEmpty, "every group has presets")
            }
        }
        XCTAssertTrue(Set(SpacePresetLibrary.presets(headphone: false).map(\.id))
            .isDisjoint(with: SpacePresetLibrary.presets(headphone: true).map(\.id)))
    }

    func testEveryPresetIsMadeOfChoicesTheTabCanShow() {
        for preset in SpacePresetLibrary.presets(headphone: false) + SpacePresetLibrary.presets(headphone: true) {
            XCTAssertTrue([0, 1, 2, 3].contains(preset.roomType), preset.id)
            XCTAssertTrue((0...100).contains(preset.roomAmount), preset.id)
            XCTAssertTrue((0...100).contains(preset.centerWidth), preset.id)
            XCTAssertTrue((0...100).contains(preset.surroundDepth), preset.id)
            if !preset.headphone { XCTAssertNotEqual(preset.surround, .virtual71, "speakers have no 7.1: \(preset.id)") }
        }
    }

    func testNoTwoPresetsOnAnOutputAreTheSameSound() {
        for headphone in [false, true] {
            let list = SpacePresetLibrary.presets(headphone: headphone)
            for (i, a) in list.enumerated() {
                for b in list[(i + 1)...] {
                    let same = a.surround == b.surround && a.roomType == b.roomType && a.stage == b.stage
                        && (a.roomType == 0 || a.roomAmount == b.roomAmount)
                        && (a.surround.rawValue < 2 || !headphone
                            || (a.surroundDepth == b.surroundDepth && a.centerWidth == b.centerWidth))
                    XCTAssertFalse(same, "\(a.id) and \(b.id) set the same parameters")
                }
            }
        }
    }

    func testApplyingAPresetSelectsItAndPushesTheWholeSceneOnce() async throws {
        let (model, client) = await spatialModel(name)
        for headphone in [false, true] {
            model.setSpatialOutput(headphone: headphone)
            for preset in SpacePresetLibrary.presets(headphone: headphone) {
                let pushes = client.setParamsValues.count
                model.applySpacePreset(preset)
                XCTAssertEqual(model.activeSpacePreset?.id, preset.id)
                for _ in 0..<2000 where client.setParamsValues.count == pushes {
                    try await Task.sleep(nanoseconds: 1_000_000)
                }
                let pushed = try XCTUnwrap(client.setParamsValues.last)
                XCTAssertTrue(preset.matches(pushed, crossfeedRendered: true), "\(preset.id) reached the engine whole")
                XCTAssertEqual(model.surroundChoice, preset.surround, preset.id)
            }
        }
    }

    func testMovingAnyControlMakesItCustomAndHeadTrackingDoesNot() async {
        let (model, _) = await spatialModel(name)
        model.setSpatialOutput(headphone: true)
        let preset = try! XCTUnwrap(SpacePresetLibrary.presets(headphone: true).first { $0.roomType >= 1 })
        model.applySpacePreset(preset)
        XCTAssertEqual(model.activeSpacePreset?.id, preset.id)

        model.setHeadTracking(true)
        XCTAssertTrue(model.headTrackingOn)
        XCTAssertEqual(model.activeSpacePreset?.id, preset.id, "tracking zeroes crossfeed the engine ignores anyway")

        model.setRoomAmount(preset.roomAmount + 10)
        XCTAssertNil(model.activeSpacePreset)
    }

    func testEachOutputComesBackToItsOwnPreset() async throws {
        let (model, _) = await spatialModel(name)
        model.setSpatialOutput(headphone: true)
        let headphonePreset = SpacePresetLibrary.presets(headphone: true)[3]
        model.applySpacePreset(headphonePreset)

        model.switchSpatialOutput(headphone: false)
        let speakerPreset = SpacePresetLibrary.presets(headphone: false)[4]
        model.applySpacePreset(speakerPreset)

        XCTAssertTrue(model.switchSpatialOutput(headphone: true))
        XCTAssertEqual(model.activeSpacePreset?.id, headphonePreset.id)
        XCTAssertTrue(model.switchSpatialOutput(headphone: false))
        XCTAssertEqual(model.activeSpacePreset?.id, speakerPreset.id)

        // Adjusted away from a preset: the output is left as it is next time.
        model.setRoomType(model.roomType >= 3 ? 1 : 3)
        XCTAssertTrue(model.switchSpatialOutput(headphone: true))
        XCTAssertFalse(model.switchSpatialOutput(headphone: false), "nothing to restore")
        XCTAssertFalse(model.spatialOutputIsHeadphone)
    }
}
