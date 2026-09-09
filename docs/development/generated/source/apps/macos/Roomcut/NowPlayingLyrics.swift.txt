import Foundation
import RoomcutPresentationCore

// Which lyrics belong to what is playing right now. Tracks change faster than
// the network answers, so this owns the order: one request per track, a debounce
// for the moment before the metadata settles, and a reply that only reaches the
// screen if it is still about the track on screen.
@MainActor
public final class NowPlayingLyrics {
    public struct Track: Equatable {
        public var key: String
        public var title: String
        public var artist: String
        public var album: String
        public var duration: Double

        public init(key: String, title: String, artist: String = "", album: String = "", duration: Double = 0) {
            self.key = key; self.title = title; self.artist = artist
            self.album = album; self.duration = duration
        }
    }

    public typealias Fetcher = @Sendable (Track) async -> [LyricLine]

    public private(set) var lines: [LyricLine] = []
    // Called when `lines` changes, and after any completed request so the caller
    // can start whatever it does next (the neighbouring-track prefetch).
    public var onLines: (() -> Void)?
    public var onFetched: ((String) -> Void)?

    private var cache: [String: [LyricLine]] = [:]
    private var inFlight: Set<String> = []
    private var requestedForCurrent = false
    private var currentKey: String?
    private var startTask: Task<Void, Never>?
    private let debounceNanoseconds: UInt64
    private let fetch: Fetcher

    public init(debounceNanoseconds: UInt64 = 150_000_000, fetch: @escaping Fetcher) {
        self.debounceNanoseconds = debounceNanoseconds
        self.fetch = fetch
    }

    public var isFetching: Bool { !inFlight.isEmpty }
    public func cached(_ key: String) -> [LyricLine]? { cache[key] }

    // A different track is playing: drop what is on screen and start again.
    public func trackChanged(to track: Track) {
        lines = []
        onLines?()
        currentKey = track.key
        requestedForCurrent = false
        startTask?.cancel()
        startTask = nil
        schedule(for: track)
    }

    // Same track, new metadata. Album art and album name arrive after the title,
    // and the album makes the lookup much better, so wait a moment for it —
    // unless it is already here, or the answer is already cached.
    public func schedule(for track: Track) {
        guard !track.title.isEmpty else { return }
        if cache[track.key] != nil {
            request(track)
            return
        }
        guard !requestedForCurrent else { return }
        if !track.album.isEmpty {
            startTask?.cancel()
            startTask = nil
            request(track)
            return
        }
        guard startTask == nil else { return }
        let key = track.key
        startTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: self?.debounceNanoseconds ?? 0)
            guard !Task.isCancelled, let self, self.currentKey == key else { return }
            self.startTask = nil
            self.request(track)
        }
    }

    public func stop() {
        startTask?.cancel()
        startTask = nil
        lines = []
        currentKey = nil
        requestedForCurrent = false
    }

    private func request(_ track: Track) {
        let key = track.key
        currentKey = key
        if let cached = cache[key] {
            lines = cached
            onLines?()
            return
        }
        guard !track.title.isEmpty, !inFlight.contains(key), !requestedForCurrent else { return }
        requestedForCurrent = true
        inFlight.insert(key)
        let fetch = self.fetch
        Task { @MainActor [weak self] in
            let found = await fetch(track)
            guard let self else { return }
            self.inFlight.remove(key)
            if !found.isEmpty {
                self.cache[key] = found
                // The track may have moved on while the network was busy. Cache
                // the answer either way; only show it if it is still the one.
                if self.currentKey == key {
                    self.lines = found
                    self.onLines?()
                }
            }
            self.onFetched?(key)
        }
    }
}
