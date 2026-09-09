import XCTest
@testable import RoomcutCore

// ARC-03, first piece: the two things every Now Playing update goes through —
// framing the helper's output, and carrying the position forward between
// updates. Both were inline in a 940-line monitor and had no tests, so a pipe
// read that split a line in the wrong place had nothing to catch it.
final class NowPlayingStreamTests: XCTestCase {
    private func text(_ lines: [Data]) -> [String] {
        lines.map { String(decoding: $0, as: UTF8.self) }
    }

    func testWholeLinesComeOutOfOneRead() {
        var reader = NowPlayingLineReader()
        XCTAssertEqual(text(reader.take(Data("one\ntwo\n".utf8))), ["one", "two"])
        XCTAssertEqual(reader.pendingBytes, 0)
    }

    func testALineSplitAcrossReadsIsHeldUntilItEnds() {
        var reader = NowPlayingLineReader()
        XCTAssertTrue(reader.take(Data("{\"title\":\"Bo".utf8)).isEmpty, "half a line is not a line")
        XCTAssertGreaterThan(reader.pendingBytes, 0)
        XCTAssertEqual(text(reader.take(Data("hemian\"}\n".utf8))), ["{\"title\":\"Bohemian\"}"])
        XCTAssertEqual(reader.pendingBytes, 0)
    }

    func testTrailingDataWaitsForItsNewline() {
        var reader = NowPlayingLineReader()
        XCTAssertEqual(text(reader.take(Data("first\nsecond".utf8))), ["first"])
        XCTAssertEqual(text(reader.take(Data("\n".utf8))), ["second"])
    }

    func testEmptyLinesAreDropped() {
        var reader = NowPlayingLineReader()
        XCTAssertEqual(text(reader.take(Data("\n\nkept\n\n".utf8))), ["kept"])
    }

    func testResetForgetsAPartialLine() {
        var reader = NowPlayingLineReader()
        _ = reader.take(Data("half".utf8))
        reader.reset()
        XCTAssertEqual(reader.pendingBytes, 0)
        XCTAssertEqual(text(reader.take(Data(" line\n".utf8))), [" line"],
                       "what was buffered before the reset is gone, not stitched on")
    }

    func testJunkWithoutANewlineIsDroppedAndTheStreamPicksUpAgain() {
        var reader = NowPlayingLineReader()
        let junk = Data(repeating: 0x41, count: NowPlayingLineReader.lineLimit + 1)
        XCTAssertTrue(reader.take(junk).isEmpty)
        XCTAssertEqual(reader.pendingBytes, 0, "the buffer does not grow without bound")
        XCTAssertTrue(reader.isResynchronising)
        XCTAssertTrue(reader.take(Data("tail-of-the-junk\n".utf8)).isEmpty, "the rest of that line goes too")
        XCTAssertEqual(text(reader.take(Data("{\"ok\":1}\n".utf8))), ["{\"ok\":1}"], "and the next line is used")
        XCTAssertFalse(reader.isResynchronising)
    }

    func testAPausedTrackHoldsItsPosition() {
        var clock = NowPlayingClock()
        let base = Date()
        clock.rebaseline(elapsed: 42, at: base)
        XCTAssertEqual(clock.position(playing: false, rate: 1, duration: 200, now: base.addingTimeInterval(30)), 42)
    }

    func testPlayingAdvancesByTheRate() {
        var clock = NowPlayingClock()
        let base = Date()
        clock.rebaseline(elapsed: 10, at: base)
        XCTAssertEqual(clock.position(playing: true, rate: 1, duration: 200, now: base.addingTimeInterval(5)), 15,
                       accuracy: 1e-9)
        XCTAssertEqual(clock.position(playing: true, rate: 2, duration: 200, now: base.addingTimeInterval(5)), 20,
                       accuracy: 1e-9)
    }

    func testThePositionStopsAtTheDuration() {
        var clock = NowPlayingClock()
        let base = Date()
        clock.rebaseline(elapsed: 195, at: base)
        XCTAssertEqual(clock.position(playing: true, rate: 1, duration: 200, now: base.addingTimeInterval(60)), 200)
        XCTAssertEqual(clock.position(playing: true, rate: 1, duration: 0, now: base.addingTimeInterval(60)), 255,
                       accuracy: 1e-9, "an unknown duration is not a limit")
    }

    func testATimestampFromTheFutureDoesNotRewind() {
        var clock = NowPlayingClock()
        let base = Date()
        clock.rebaseline(elapsed: 30, at: base.addingTimeInterval(10))
        XCTAssertEqual(clock.position(playing: true, rate: 1, duration: 200, now: base), 30,
                       "the bar holds instead of jumping backwards")
    }
}
