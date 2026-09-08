import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class SoundEditorTests: XCTestCase {
    private func makeEditor() -> SoundEditor {
        let editor = SoundEditor()
        editor.synchronize(SoundSnapshot(parameters: .flat, builtinPresetID: "flat"), reset: true)
        return editor
    }

    func testWholeGestureIsOneUndoAndRedoRestoresAllSoundState() async {
        let editor = makeEditor()
        let original = editor.snapshot
        editor.beginGesture()
        for gain in 1...5 {
            editor.update {
                $0.parameters.eqGainsDb[0] = Double(gain)
                $0.parameters.parametric[0].enabled = true
                $0.parameters.parametric[0].freqHz = 250
                $0.parameters.compAmount = 30
                $0.macros[.bass] = Double(gain) / 10
                $0.builtinPresetID = nil
            }
            editor.commitEdit() // an engine ACK during a drag must not split history
        }
        editor.endGesture()
        let edited = editor.snapshot
        editor.undo()
        XCTAssertEqual(editor.snapshot, original)
        XCTAssertFalse(editor.canUndo)
        XCTAssertTrue(editor.canRedo)
        editor.redo()
        XCTAssertEqual(editor.snapshot, edited)
    }

    func testComparisonSlotsKeepIndependentHistories() async {
        let editor = makeEditor()
        editor.update { $0.parameters.preampDb = -3 }
        editor.commitEdit()
        let a = editor.snapshot
        editor.select(.b)
        XCTAssertEqual(editor.snapshot.parameters.preampDb, 0)
        XCTAssertFalse(editor.canUndo)
        editor.update { $0.parameters.outputGainDb = -2 }
        editor.commitEdit()
        let b = editor.snapshot
        editor.select(.a)
        XCTAssertEqual(editor.snapshot, a)
        editor.undo()
        XCTAssertEqual(editor.snapshot.parameters.preampDb, 0)
        editor.select(.b)
        XCTAssertEqual(editor.snapshot, b)
        XCTAssertTrue(editor.canUndo)
    }

    func testCopyReplacesOtherReferenceWithoutChangingCurrentAudio() async {
        let editor = makeEditor()
        editor.update { $0.parameters.spatialWidth = 35 }
        editor.commitEdit()
        let current = editor.snapshot
        XCTAssertTrue(editor.canCopy)
        editor.copyToOther()
        XCTAssertEqual(editor.snapshot, current)
        XCTAssertFalse(editor.canCopy)
        editor.select(.b)
        XCTAssertEqual(editor.snapshot, current)
        XCTAssertFalse(editor.canUndo)
    }

    func testNewBranchInvalidatesRedoAndHistoryIsBounded() async {
        let editor = makeEditor()
        for step in 0..<130 {
            editor.update { $0.parameters.parametric[0].freqHz = 100 + Double(step) }
            editor.commitEdit()
        }
        for _ in 0..<100 { editor.undo() }
        XCTAssertFalse(editor.canUndo)
        XCTAssertEqual(editor.snapshot.parameters.parametric[0].freqHz, 129)
        XCTAssertTrue(editor.canRedo)
        editor.update { $0.parameters.preampDb = -8 }
        editor.commitEdit()
        XCTAssertFalse(editor.canRedo)
    }

    func testRemoteOrFailedWriteResetsActiveHistoryAndPreservesReference() async {
        let editor = makeEditor()
        editor.update { $0.parameters.preampDb = -4 }
        editor.commitEdit()
        editor.copyToOther()
        editor.synchronize(SoundSnapshot(parameters: .flat), reset: true)
        XCTAssertFalse(editor.canUndo)
        editor.select(.b)
        XCTAssertEqual(editor.snapshot.parameters.preampDb, -4)
    }

    func testImportedSnapshotHasSafeLengthsAndFiniteBounds() async {
        var params = EngineParameters.flat
        params.eqGainsDb = [50, .nan]
        params.preampDb = .infinity
        params.spatialMode = 1.6
        params.parametric = [ParametricBand(enabled: true, type: 99, freqHz: 50000, gainDb: -100, q: 0)]
        let snapshot = SoundSnapshot(parameters: params)
        XCTAssertEqual(snapshot.parameters.eqGainsDb.count, 10)
        XCTAssertEqual(snapshot.parameters.eqGainsDb[0], 24)
        XCTAssertEqual(snapshot.parameters.eqGainsDb[1], -24)
        XCTAssertEqual(snapshot.parameters.preampDb, -24)
        XCTAssertEqual(snapshot.parameters.spatialMode, 2)
        XCTAssertEqual(snapshot.parameters.parametric.count, 6)
        XCTAssertEqual(snapshot.parameters.parametric[0].freqHz, 20000)
        XCTAssertEqual(snapshot.parameters.parametric[0].type, 0)
        XCTAssertEqual(snapshot.parameters.parametric[0].q, 0.1)
    }
}
