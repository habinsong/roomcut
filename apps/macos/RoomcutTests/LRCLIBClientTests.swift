import Foundation
import XCTest
@testable import RoomcutCore

final class LRCLIBClientTests: XCTestCase {
    private actor StubLookup {
        private var results: [String?]
        private(set) var attempts = 0

        init(results: [String?]) {
            self.results = results
        }

        func next() -> String? {
            attempts += 1
            return results.isEmpty ? nil : results.removeFirst()
        }
    }

    private actor SlowFirstLookup {
        private(set) var attempts = 0

        func next() async -> String? {
            attempts += 1
            if attempts == 1 {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                return nil
            }
            return "[00:01.00] hedged"
        }
    }

    func testLookupUsesOfficialExactMetadataRequest() throws {
        let url = try XCTUnwrap(LRCLIBClient.lookupURL(
            title: "Track",
            artist: "Artist",
            album: "Album",
            duration: 180
        ))

        let components = try XCTUnwrap(
            URLComponents(url: url, resolvingAgainstBaseURL: false)
        )
        XCTAssertEqual(components.path, "/api/get")
        XCTAssertEqual(
            components.queryItems?.first { $0.name == "album_name" }?.value,
            "Album"
        )
        XCTAssertEqual(
            components.queryItems?.first { $0.name == "duration" }?.value,
            "180"
        )
    }

    func testPersistentCacheSurvivesClientRestart() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")
        let key = LRCLIBClient.cacheKey(
            title: "Track",
            artist: "Artist",
            duration: 180
        )

        let first = LRCLIBLyricsCache(fileURL: fileURL)
        await first.insert("[00:01.00] cached", for: key)

        let second = LRCLIBLyricsCache(fileURL: fileURL)
        let lyrics = await second.value(for: key)

        XCTAssertEqual(lyrics, "[00:01.00] cached")
        try? FileManager.default.removeItem(at: directory)
    }

    func testClearEmptiesCacheAndDeletesFile() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")

        let cache = LRCLIBLyricsCache(fileURL: fileURL)
        await cache.insert("[00:01.00] one", for: "a|b|10")
        await cache.insert("[00:02.00] two", for: "c|d|20")

        let before = await cache.count()
        XCTAssertEqual(before, 2)
        XCTAssertTrue(FileManager.default.fileExists(atPath: fileURL.path))

        await cache.clear()

        let after = await cache.count()
        XCTAssertEqual(after, 0)
        let removed = await cache.value(for: "a|b|10")
        XCTAssertNil(removed)
        XCTAssertFalse(FileManager.default.fileExists(atPath: fileURL.path))

        try? FileManager.default.removeItem(at: directory)
    }

    func testCacheKeyUsesStableTrackIdentity() {
        let key = LRCLIBClient.cacheKey(
            title: "Track",
            artist: "Artist",
            duration: 180
        )

        XCTAssertEqual(key, "track|artist|180")
    }

    func testPersistentCacheFindsUnknownDurationEntryForKnownDuration() async {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")
        let cache = LRCLIBLyricsCache(fileURL: fileURL)
        let prefix = LRCLIBClient.cacheIdentityPrefix(
            title: "Track",
            artist: "Artist"
        )
        await cache.insert("[00:01.00] cached", for: prefix)

        let match = await cache.value(
            for: "\(prefix)180",
            identityPrefix: prefix,
            duration: 180
        )

        XCTAssertEqual(match?.lyrics, "[00:01.00] cached")
        try? FileManager.default.removeItem(at: directory)
    }

    func testPersistentCacheFindsLegacyAlbumKey() async {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")
        let cache = LRCLIBLyricsCache(fileURL: fileURL)
        let prefix = LRCLIBClient.cacheIdentityPrefix(
            title: "Track",
            artist: "Artist"
        )
        await cache.insert(
            "[00:01.00] legacy",
            for: "\(prefix)Album|180"
        )

        let match = await cache.value(
            for: "\(prefix)181",
            identityPrefix: prefix,
            duration: 181
        )

        XCTAssertEqual(match?.lyrics, "[00:01.00] legacy")
        try? FileManager.default.removeItem(at: directory)
    }

    func testPersistentCacheRejectsDifferentDurationVersion() async {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")
        let cache = LRCLIBLyricsCache(fileURL: fileURL)
        let prefix = LRCLIBClient.cacheIdentityPrefix(
            title: "Track",
            artist: "Artist"
        )
        await cache.insert("[00:01.00] other version", for: "\(prefix)240")

        let match = await cache.value(
            for: "\(prefix)180",
            identityPrefix: prefix,
            duration: 180
        )

        XCTAssertNil(match)
        try? FileManager.default.removeItem(at: directory)
    }

    func testSearchResultPrefersMatchingDuration() throws {
        let data = Data(#"""
        [
          {
            "trackName": "Track",
            "artistName": "Artist",
            "duration": 45,
            "syncedLyrics": "[00:01.00] wrong"
          },
          {
            "trackName": "Track",
            "artistName": "Artist",
            "duration": 180,
            "syncedLyrics": "[00:01.00] right"
          }
        ]
        """#.utf8)

        let lyrics = LRCLIBClient.syncedLyrics(
            fromSearchResponse: data,
            title: "Track",
            artist: "Artist",
            duration: 180
        )

        XCTAssertEqual(lyrics, "[00:01.00] right")
    }

    func testTransientFailureRetriesUntilThirdAttemptSucceeds() async {
        let lookup = StubLookup(results: [
            nil,
            nil,
            "[00:01.00] recovered",
        ])

        let lyrics = await LRCLIBClient.retrying(
            maxAttempts: 3,
            retryDelayNanoseconds: 0
        ) {
            await lookup.next()
        }

        XCTAssertEqual(lyrics, "[00:01.00] recovered")
        let attempts = await lookup.attempts
        XCTAssertEqual(attempts, 3)
    }

    func testDefaultRetryPolicyUsesThreeAttemptsAndTwoSecondDelay() {
        XCTAssertEqual(LRCLIBClient.maximumAttempts, 3)
        XCTAssertEqual(LRCLIBClient.retryDelayNanoseconds, 2_000_000_000)
        XCTAssertGreaterThan(LRCLIBClient.requestTimeout, 11)
        XCTAssertEqual(LRCLIBClient.searchHedgeDelayNanoseconds, 0)
        XCTAssertEqual(LRCLIBClient.maximumConnectionsPerHost, 18)
    }

    func testSecondAttemptStartsBeforeSlowFirstAttemptTimesOut() async {
        let lookup = SlowFirstLookup()
        let started = Date()

        let lyrics = await LRCLIBClient.retrying(
            maxAttempts: 3,
            retryDelayNanoseconds: 10_000_000
        ) {
            await lookup.next()
        }

        XCTAssertEqual(lyrics, "[00:01.00] hedged")
        XCTAssertLessThan(Date().timeIntervalSince(started), 0.5)
    }

    func testAdjacentLyricsPrefetchRunsPreviousAndNextConcurrently() async {
        let requests = [
            LRCLIBTrackRequest(title: "Previous", artist: "Artist", album: "", duration: 180),
            LRCLIBTrackRequest(title: "Next", artist: "Artist", album: "", duration: 200),
        ]
        let started = Date()

        await LRCLIBClient.forEachConcurrently(requests) { _ in
            try? await Task.sleep(nanoseconds: 300_000_000)
        }

        // Two 300 ms tasks running concurrently take ~0.3 s, not the ~0.6 s a
        // sequential run would. The 0.5 s threshold keeps generous head-room so a
        // slow/loaded CI runner's scheduling jitter can't flake it, while still
        // catching a regression to sequential execution (~0.6 s). The old
        // 100 ms / 0.18 s pair had only ~0.08 s of slack and flaked on CI.
        XCTAssertLessThan(Date().timeIntervalSince(started), 0.5)
    }

    func testPersistentCacheEvictsOldestEntryAfterOneThousandTracks() async throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        let fileURL = directory.appendingPathComponent("lyrics.json")
        let cache = LRCLIBLyricsCache(fileURL: fileURL)

        XCTAssertEqual(LRCLIBLyricsCache.capacity, 1000)
        for index in 0...LRCLIBLyricsCache.capacity {
            await cache.insert("lyrics-\(index)", for: "track-\(index)")
        }

        let oldest = await cache.value(for: "track-0")
        let newest = await cache.value(for: "track-\(LRCLIBLyricsCache.capacity)")
        XCTAssertNil(oldest)
        XCTAssertEqual(newest, "lyrics-\(LRCLIBLyricsCache.capacity)")
        try? FileManager.default.removeItem(at: directory)
    }
}
