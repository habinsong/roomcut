import Foundation
import XCTest
@testable import RoomcutCore

final class NowPlayingPayloadTests: XCTestCase {
    func testMetadataDecodeDoesNotRequireArtworkData() throws {
        let data = Data(#"""
        {
          "title": "Track",
          "artist": "Artist",
          "album": "Album",
          "appName": "Music",
          "playing": true,
          "duration": 180,
          "elapsedTime": 12.5,
          "timestamp": 1234,
          "playbackRate": 1,
          "trackKey": "Track|Artist|Album"
        }
        """#.utf8)

        let payload = try XCTUnwrap(
            NowPlayingPayloadDecoder.metadata(from: data, fallbackTimestamp: Date(timeIntervalSince1970: 0))
        )

        XCTAssertEqual(payload.trackKey, "Track|Artist|180")
        XCTAssertEqual(payload.title, "Track")
        XCTAssertEqual(payload.elapsedTime, 12.5)
        XCTAssertEqual(payload.timestamp, Date(timeIntervalSince1970: 1234))
    }

    func testArtworkDecodeReturnsTrackScopedBytes() throws {
        let expected = Data([0x01, 0x02, 0x03, 0x04])
        let data = Data(#"""
        {
          "trackKey": "Track|Artist|Album",
          "artworkData": "AQIDBA==",
          "artworkMimeType": "image/png"
        }
        """#.utf8)

        let payload = try XCTUnwrap(NowPlayingPayloadDecoder.artwork(from: data))

        XCTAssertEqual(payload.trackKey, "Track|Artist|Album")
        XCTAssertEqual(payload.data, expected)
        XCTAssertEqual(payload.mimeType, "image/png")
    }

    func testArtworkDecodePreservesValidNoArtworkResponse() throws {
        let data = Data(#"{"trackKey":"Track|Artist|Album"}"#.utf8)

        let payload = try XCTUnwrap(NowPlayingPayloadDecoder.artwork(from: data))

        XCTAssertEqual(payload.trackKey, "Track|Artist|Album")
        XCTAssertNil(payload.data)
        XCTAssertNil(payload.mimeType)
    }

    func testTrackIdentityDoesNotChangeWhenAlbumArrivesLater() throws {
        let early = Data(#"""
        {
          "title": "Track",
          "artist": "Artist",
          "duration": 180,
          "trackKey": "Track|Artist|"
        }
        """#.utf8)
        let complete = Data(#"""
        {
          "title": "Track",
          "artist": "Artist",
          "album": "Album",
          "duration": 180,
          "trackKey": "Track|Artist|Album"
        }
        """#.utf8)

        let earlyPayload = try XCTUnwrap(NowPlayingPayloadDecoder.metadata(from: early))
        let completePayload = try XCTUnwrap(NowPlayingPayloadDecoder.metadata(from: complete))

        XCTAssertEqual(earlyPayload.trackKey, completePayload.trackKey)
        XCTAssertEqual(earlyPayload.trackKey, "Track|Artist|180")
    }

    func testTrackIdentityTreatsMissingDurationAsSameTrack() {
        XCTAssertTrue(NowPlayingTrackIdentity.isSameTrack(
            title: "Track",
            artist: "Artist",
            duration: 0,
            otherTitle: "Track",
            otherArtist: "Artist",
            otherDuration: 180
        ))
        XCTAssertFalse(NowPlayingTrackIdentity.isSameTrack(
            title: "Track",
            artist: "Artist",
            duration: 180,
            otherTitle: "Track",
            otherArtist: "Artist",
            otherDuration: 240
        ))
    }

    func testFullPayloadUsesSameStableIdentityForMetadataAndArtwork() throws {
        let data = Data(#"""
        {
          "title": "Track",
          "artist": "Artist",
          "album": "Album",
          "duration": 180,
          "trackKey": "legacy-key",
          "artworkData": "AQIDBA==",
          "artworkMimeType": "image/png"
        }
        """#.utf8)

        let metadata = try XCTUnwrap(NowPlayingPayloadDecoder.metadata(from: data))
        let artwork = try XCTUnwrap(NowPlayingPayloadDecoder.artwork(from: data))

        XCTAssertEqual(metadata.trackKey, artwork.trackKey)
        XCTAssertEqual(artwork.trackKey, "Track|Artist|180")
    }

    func testQueueDecodeReturnsOnlyAdjacentTracks() throws {
        let data = Data(#"""
        {
          "items": [
            {
              "relativeOffset": -1,
              "title": "Previous",
              "artist": "Artist A",
              "album": "Album A",
              "duration": 181
            },
            {
              "relativeOffset": 1,
              "title": "Next",
              "artist": "Artist B",
              "album": "Album B",
              "duration": 202
            }
          ]
        }
        """#.utf8)

        let items = try XCTUnwrap(NowPlayingPayloadDecoder.queue(from: data))

        XCTAssertEqual(items.map(\.relativeOffset), [-1, 1])
        XCTAssertEqual(items.first?.title, "Previous")
        XCTAssertEqual(items.last?.album, "Album B")
    }

    func testObservedTransitionsLearnPreviousAndNextForQueueLessPlayers() {
        let first = NowPlayingQueueItemPayload(
            relativeOffset: 0,
            title: "First",
            artist: "Artist",
            album: "Album",
            duration: 180
        )
        let second = NowPlayingQueueItemPayload(
            relativeOffset: 0,
            title: "Second",
            artist: "Artist",
            album: "Album",
            duration: 200
        )
        var store = NowPlayingAdjacencyStore()

        store.observeTransition(from: first, to: second, direction: .next)

        XCTAssertEqual(
            store.items(around: first.trackKey).map(\.title),
            ["Second"]
        )
        XCTAssertEqual(
            store.items(around: second.trackKey).map(\.title),
            ["First"]
        )
        XCTAssertEqual(
            store.items(around: first.trackKey).map(\.relativeOffset),
            [1]
        )
        XCTAssertEqual(
            store.items(around: second.trackKey).map(\.relativeOffset),
            [-1]
        )
    }

    // MARK: Baked-in letterbox/pillarbox trimming (video artwork canvases)

    // A synthetic canvas: `fill` background with a `content`-coloured rect.
    private func makeCanvas(width: Int, height: Int,
                            contentRect: CGRect?,
                            fill: CGColor,
                            content: CGColor) -> CGImage {
        let ctx = CGContext(
            data: nil, width: width, height: height, bitsPerComponent: 8,
            bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.setFillColor(fill)
        ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
        if let contentRect {
            ctx.setFillColor(content)
            ctx.fill(contentRect)
        }
        return ctx.makeImage()!
    }

    private var gray: CGColor { CGColor(red: 0.5, green: 0.55, blue: 0.6, alpha: 1) }
    private var black: CGColor { CGColor(red: 0, green: 0, blue: 0, alpha: 1) }

    func testTrimsPillarboxBarsFromWideCanvas() {
        // A Shorts-style 16:9 canvas: black pillars, ~portrait content centred.
        let cg = makeCanvas(width: 336, height: 188,
                            contentRect: CGRect(x: 74, y: 0, width: 188, height: 188),
                            fill: black, content: gray)
        let out = ArtworkCanvas.trimmedLetterboxBars(cg)
        XCTAssertNotNil(out)
        XCTAssertLessThanOrEqual(abs(out!.width - 188), 3, "pillars trimmed to the content")
        XCTAssertEqual(out!.height, 188)
    }

    func testTrimsLetterboxBarsFromTallCanvas() {
        // Landscape content baked into a tall canvas (bars top/bottom).
        let cg = makeCanvas(width: 188, height: 336,
                            contentRect: CGRect(x: 0, y: 118, width: 188, height: 100),
                            fill: black, content: gray)
        let out = ArtworkCanvas.trimmedLetterboxBars(cg)
        XCTAssertNotNil(out)
        XCTAssertLessThanOrEqual(abs(out!.height - 100), 3, "letterbox trimmed to the content")
        XCTAssertEqual(out!.width, 188)
    }

    func testSquareCanvasIsNeverTrimmed() {
        // A dark album cover with black margins is art, not padding.
        let cg = makeCanvas(width: 300, height: 300,
                            contentRect: CGRect(x: 100, y: 100, width: 100, height: 100),
                            fill: black, content: gray)
        XCTAssertNil(ArtworkCanvas.trimmedLetterboxBars(cg))
    }

    func testFullBleedWideContentIsNotTrimmed() {
        let cg = makeCanvas(width: 336, height: 188, contentRect: nil,
                            fill: gray, content: gray)
        XCTAssertNil(ArtworkCanvas.trimmedLetterboxBars(cg))
    }

    func testAllBlackWideCanvasKeepsAUsableCore() {
        // Pathological: an entirely black frame must not trim to nothing — the
        // per-axis caps stop at 42% per side and the result stays usable.
        let cg = makeCanvas(width: 336, height: 188, contentRect: nil,
                            fill: black, content: black)
        if let out = ArtworkCanvas.trimmedLetterboxBars(cg) {
            XCTAssertGreaterThanOrEqual(out.width, 16)
            XCTAssertGreaterThanOrEqual(out.height, 16)
        }
    }
}
