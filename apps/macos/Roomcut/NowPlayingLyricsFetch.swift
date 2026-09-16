import Foundation
import AppKit
import Combine
import ImageIO
import RoomcutCore
import RoomcutPresentationCore

// Lyrics: which track we are asking about, fetching from LRCLIB, caching per
// track, and prefetching the neighbours so a skip lands with words already there.
extension NowPlayingMonitor {
    // MARK: Lyrics (LRCLIB, cached per track)

    // 0.3 s so the line lands promptly; only recomputes and publishes on change.
    // Cheap no-op when the current track has no lyrics.
    func startLyricTimer() {
        lyricTimer?.invalidate()
        lyricTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshLyricLine() }
        }
    }

    func refreshLyricLine() {
        guard !lyricLines.isEmpty, let snap = snapshot else {
            if currentLyric != nil { currentLyric = nil }
            if nextLyric != nil { nextLyric = nil }
            return
        }
        let lines = LyricsParsing.lyricLines(
            at: currentElapsed(snap),
            in: lyricLines
        )
        if currentLyric != lines.current { currentLyric = lines.current }
        if nextLyric != lines.next { nextLyric = lines.next }
    }

    func lyricsTrack(_ snap: Snapshot) -> NowPlayingLyrics.Track {
        .init(key: snap.trackKey, title: snap.title, artist: snap.artist,
              album: snap.album, duration: snap.duration)
    }

    func connectLyrics() {
        lyrics.onLines = { [weak self] in self?.refreshLyricLine() }
        lyrics.onFetched = { [weak self] key in self?.startAdjacentLyricsPrefetch(for: key) }
    }

    func scheduleLyricsFetch(for snap: Snapshot) {
        lyrics.schedule(for: lyricsTrack(snap))
    }

    func startAdjacentLyricsPrefetch(for currentTrackKey: String) {
        guard snapshot?.trackKey == currentTrackKey else { return }

        let requests: [LRCLIBTrackRequest] = adjacencyStore.items(around: currentTrackKey).compactMap { item in
            guard item.trackKey != currentTrackKey else { return nil }
            let key = LRCLIBClient.cacheKey(
                title: item.title,
                artist: item.artist,
                duration: item.duration
            )
            guard lyricsPrefetchRequested.insert(key).inserted else { return nil }
            return LRCLIBTrackRequest(
                title: item.title,
                artist: item.artist,
                album: item.album,
                duration: item.duration
            )
        }
        guard !requests.isEmpty else { return }

        Task { [weak self] in
            let failed = await LRCLIBClient.prefetchSyncedLyrics(requests)
            await MainActor.run {
                guard let self else { return }
                for request in failed {
                    self.lyricsPrefetchRequested.remove(
                        LRCLIBClient.cacheKey(
                            title: request.title,
                            artist: request.artist,
                            duration: request.duration
                        )
                    )
                }
            }
        }
    }

    func transitionDirection(
        from previous: Snapshot,
        elapsed: Double
    ) -> NowPlayingTransitionDirection? {
        defer { pendingNavigation = nil }
        if let pendingNavigation,
           pendingNavigation.expiresAt >= Date() {
            return pendingNavigation.direction
        }
        if previous.duration > 0,
           elapsed >= max(0, previous.duration - 15) {
            return .next
        }
        return nil
    }

    func queueItem(from snap: Snapshot) -> NowPlayingQueueItemPayload {
        NowPlayingQueueItemPayload(
            relativeOffset: 0,
            title: snap.title,
            artist: snap.artist,
            album: snap.album,
            duration: snap.duration
        )
    }
}
