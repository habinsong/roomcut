import XCTest
@testable import RoomcutCore
import RoomcutPresentationCore

// ARC-03: what happens to lyrics when tracks change faster than the network
// answers. These are the cancel and late-reply cases the plan asked to pin down
// before splitting Now Playing up.
@MainActor
final class NowPlayingLyricsTests: XCTestCase {
    private final class Network {
        var requests: [String] = []
        private var pending: [String: CheckedContinuation<[LyricLine], Never>] = [:]
        private var replies: [String: [LyricLine]] = [:]

        func answer(_ key: String, with lines: [LyricLine]) {
            if let waiter = pending.removeValue(forKey: key) { waiter.resume(returning: lines) }
            else { replies[key] = lines }
        }
        func fetcher() -> NowPlayingLyrics.Fetcher {
            { [self] track in
                await MainActor.run { self.requests.append(track.key) }
                return await withCheckedContinuation { continuation in
                    Task { @MainActor in
                        if let ready = self.replies.removeValue(forKey: track.key) {
                            continuation.resume(returning: ready)
                        } else {
                            self.pending[track.key] = continuation
                        }
                    }
                }
            }
        }
    }

    private func lines(_ text: String) -> [LyricLine] { [LyricLine(time: 0, text: text)] }
    private func track(_ key: String, album: String = "An Album") -> NowPlayingLyrics.Track {
        .init(key: key, title: "Title \(key)", artist: "Artist", album: album, duration: 200)
    }
    private func settle() async throws { try await Task.sleep(nanoseconds: 30_000_000) }

    func testAKnownAlbumIsLookedUpWithoutWaiting() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 5_000_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a"))
        try await settle()
        XCTAssertEqual(network.requests, ["a"], "there is nothing more to wait for")
    }

    func testATrackWithNoAlbumWaitsForOneToArrive() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 20_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a", album: ""))
        XCTAssertTrue(network.requests.isEmpty, "the album usually lands a moment later and makes the match better")
        lyrics.schedule(for: track("a"))   // album arrived
        try await settle()
        XCTAssertEqual(network.requests, ["a"], "and it is looked up once, not twice")
    }

    func testChangingTrackCancelsAPendingLookup() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 40_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a", album: ""))
        lyrics.trackChanged(to: track("b", album: ""))
        try await Task.sleep(nanoseconds: 80_000_000)
        XCTAssertEqual(network.requests, ["b"], "the track that is gone is not looked up")
    }

    func testALateReplyForAPreviousTrackNeverReachesTheScreen() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        var published = 0
        lyrics.onLines = { published += 1 }

        lyrics.trackChanged(to: track("a"))
        try await settle()
        lyrics.trackChanged(to: track("b"))
        try await settle()
        network.answer("b", with: lines("for b"))
        try await settle()
        XCTAssertEqual(lyrics.lines.first?.text, "for b")

        network.answer("a", with: lines("for a"))   // the old request finally lands
        try await settle()
        XCTAssertEqual(lyrics.lines.first?.text, "for b", "the screen keeps the track it is on")
        XCTAssertEqual(lyrics.cached("a")?.first?.text, "for a", "but the answer is not thrown away")
        XCTAssertGreaterThan(published, 0)
    }

    func testASecondVisitToATrackUsesTheCache() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a"))
        try await settle()
        network.answer("a", with: lines("cached"))
        try await settle()

        lyrics.trackChanged(to: track("b"))
        try await settle()
        lyrics.trackChanged(to: track("a"))
        try await settle()
        XCTAssertEqual(network.requests, ["a", "b"], "the network is not asked about a track it already answered")
        XCTAssertEqual(lyrics.lines.first?.text, "cached", "and the lines come straight back")
    }

    func testMetadataUpdatesDoNotStackUpRequests() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a"))
        try await settle()
        for _ in 0..<5 { lyrics.schedule(for: track("a")) }   // elapsed/rate updates keep arriving
        try await settle()
        XCTAssertEqual(network.requests, ["a"], "one request per track while it is in flight")
        XCTAssertTrue(lyrics.isFetching)
    }

    func testATrackWithNoTitleIsNeverLookedUp() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: .init(key: "a", title: "", artist: "", album: "", duration: 0))
        try await settle()
        XCTAssertTrue(network.requests.isEmpty)
    }

    func testAnEmptyAnswerIsNotCachedAsAnAnswer() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a"))
        try await settle()
        network.answer("a", with: [])
        try await settle()
        XCTAssertNil(lyrics.cached("a"), "a track with no lyrics can be tried again later")
        XCTAssertTrue(lyrics.lines.isEmpty)
    }

    func testStoppingClearsWhatIsOnScreen() async throws {
        let network = Network()
        let lyrics = NowPlayingLyrics(debounceNanoseconds: 1_000_000, fetch: network.fetcher())
        lyrics.trackChanged(to: track("a"))
        try await settle()
        network.answer("a", with: lines("visible"))
        try await settle()
        lyrics.stop()
        XCTAssertTrue(lyrics.lines.isEmpty)
    }
}
