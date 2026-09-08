//
// LyricsParsingTests.swift — the pure LRC parser used for synced lyrics.
//
import XCTest
import RoomcutPresentationCore

final class LyricsParsingTests: XCTestCase {

    func testParsesSyncedLinesSortedAndSkipsMetadata() {
        let lrc = """
        [ar: Someone]
        [ti: A Song]
        [00:12.50] second line
        [00:05.00] first line
        plain line without a stamp
        """
        let lines = LyricsParsing.parse(lrc)
        XCTAssertEqual(lines.count, 2)                      // metadata + unstamped dropped
        XCTAssertEqual(lines.map(\.text), ["first line", "second line"]) // time-sorted
        XCTAssertEqual(lines[0].time, 5.0, accuracy: 1e-6)
        XCTAssertEqual(lines[1].time, 12.5, accuracy: 1e-6)
    }

    func testLineAtTimePicksLastStampAtOrBeforeTime() {
        let lines = [LyricLine(time: 5, text: "a"), LyricLine(time: 12.5, text: "b")]
        XCTAssertNil(LyricsParsing.line(at: 3, in: lines))          // before first
        XCTAssertEqual(LyricsParsing.line(at: 5, in: lines), "a")   // exactly on
        XCTAssertEqual(LyricsParsing.line(at: 9, in: lines), "a")
        XCTAssertEqual(LyricsParsing.line(at: 99, in: lines), "b")
    }

    func testLyricLinesReturnsCurrentAndNextLine() {
        let lines = [
            LyricLine(time: 5, text: "first"),
            LyricLine(time: 12.5, text: "second"),
            LyricLine(time: 20, text: "third"),
        ]

        let pair = LyricsParsing.lyricLines(at: 13, in: lines)

        XCTAssertEqual(pair.current, "second")
        XCTAssertEqual(pair.next, "third")
    }

    // An empty stamped line ("[00:20.00]") is a gap → the display clears there.
    func testEmptyStampedLineClearsDisplay() {
        let lines = LyricsParsing.parse("[00:01.00] hi\n[00:20.00]")
        XCTAssertEqual(LyricsParsing.line(at: 5, in: lines), "hi")
        XCTAssertNil(LyricsParsing.line(at: 25, in: lines))
    }
}
