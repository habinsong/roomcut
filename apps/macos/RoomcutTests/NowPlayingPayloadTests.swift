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
}
