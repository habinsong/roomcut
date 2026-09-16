//
// NowPlayingMonitor.swift — reads system Now Playing via a child /usr/bin/perl
// process that loads the bundled RoomcutNowPlaying.dylib (private MediaRemote,
// isolated out-of-process). The main app never links MediaRemote.
//
// Lifecycle: start() runs np_test to gate availability, then spawns np_stream
// and parses one JSON object per line into a @Published Snapshot + artwork.
// Controls (send/seek) are one-shot perl invocations. Falls back silently to
// the engine-signal display when the helper is missing or self-test fails.
//
import Foundation
import AppKit
import Combine
import ImageIO
import RoomcutCore
import RoomcutPresentationCore

@MainActor
final class NowPlayingMonitor: ObservableObject {
    struct Snapshot: Equatable {
        var title: String
        var artist: String
        var album: String
        var appName: String
        var playing: Bool
        var duration: Double
        var elapsedTime: Double
        var timestamp: Date
        var playbackRate: Double
        var trackKey: String
    }

    @Published internal(set) var snapshot: Snapshot?
    @Published internal(set) var artwork: NSImage?
    // A representative accent colour extracted from the current artwork, for the
    // dynamic background/ring theme. nil when there's no artwork.
    @Published internal(set) var artworkColor: NSColor?
    @Published internal(set) var artworkPalette: [NSColor]?
    // Average colour of the artwork's top / bottom edge strips, for the B-layout
    // blend that extends the cover into the wash above and below.
    @Published internal(set) var artworkTopColor: NSColor?
    @Published internal(set) var artworkBottomColor: NSColor?
    @Published internal(set) var available = false
    // Estimated current elapsed time, advanced by a 1s timer while playing.
    @Published internal(set) var elapsedNow: Double = 0
    // Current synced lyric line from LRCLIB (nil = none / not yet loaded). Driven
    // by a light timer against the estimated elapsed time.
    @Published internal(set) var currentLyric: String?
    @Published internal(set) var nextLyric: String?

    // MRCommand ids (match MediaRemote.h).
    enum Command: Int {
        case play = 0, pause = 1, togglePlayPause = 2, stop = 3, next = 4, previous = 5
    }

    var streamProcess: Process?
    var lines = NowPlayingLineReader()
    var ticker: Timer?
    var lyricTimer: Timer?
    lazy var lyrics = NowPlayingLyrics { track in
        let synced = await LRCLIBClient.fetchSyncedLyrics(
            title: track.title, artist: track.artist, album: track.album, duration: track.duration)
        return synced.map(LyricsParsing.parse) ?? []
    }
    var lyricLines: [LyricLine] { lyrics.lines }
    var restartAttempted = false
    var artworkProcess: Process?
    var artworkTrackKey: String?
    var displayedArtworkTrackKey: String?
    var lastArtworkSignature: Int?
    var artworkAttemptCount = 0
    var queueProcess: Process?
    var queueTrackKey: String?
    var queueAttemptCount = 0
    var adjacencyStore = NowPlayingAdjacencyStore()
    var lyricsPrefetchRequested: Set<String> = []
    var pendingNavigation: (
        direction: NowPlayingTransitionDirection,
        expiresAt: Date
    )?

    // Baseline for elapsed estimation: elapsedTime captured at `baselineDate`.
    var clock = NowPlayingClock()

    // MARK: Paths

    // dylib + perl launcher: bundle Resources in production; build output in dev.
    static func helperPaths() -> (dylib: String, launcher: String)? {
        let fm = FileManager.default
        if let dylib = Bundle.main.url(forResource: "RoomcutNowPlaying", withExtension: "dylib"),
           let pl = Bundle.main.url(forResource: "roomcut-nowplaying", withExtension: "pl"),
           fm.fileExists(atPath: dylib.path), fm.fileExists(atPath: pl.path) {
            return (dylib.path, pl.path)
        }
        // Dev fallback (`swift run`): repo build output + source launcher.
        let cwd = fm.currentDirectoryPath
        let dylib = "\(cwd)/build/nowplaying/RoomcutNowPlaying.dylib"
        let pl = "\(cwd)/apps/macos/NowPlayingHelper/roomcut-nowplaying.pl"
        if fm.fileExists(atPath: dylib), fm.fileExists(atPath: pl) {
            return (dylib, pl)
        }
        return nil
    }

    // MARK: Lifecycle

    func start() {
        guard streamProcess == nil else { return }
        Task { await LRCLIBClient.prewarm() }
        guard let paths = Self.helperPaths() else {
            available = false
            return
        }
        // Self-test gate: np_test exit 0 means MediaRemote is reachable.
        guard runOneShot(paths: paths, function: "np_test", env: [:]) == 0 else {
            available = false
            return
        }
        available = true
        connectLyrics()
        startStream(paths: paths)
    }

    func stop() {
        ticker?.invalidate()
        ticker = nil
        lyricTimer?.invalidate()
        lyricTimer = nil
        lyrics.stop()
        currentLyric = nil
        nextLyric = nil
        if let p = streamProcess, p.isRunning {
            p.terminate()
        }
        if let p = artworkProcess, p.isRunning {
            p.terminate()
        }
        if let p = queueProcess, p.isRunning {
            p.terminate()
        }
        streamProcess = nil
        artworkProcess = nil
        artworkTrackKey = nil
        displayedArtworkTrackKey = nil
        lastArtworkSignature = nil
        artworkAttemptCount = 0
        queueProcess = nil
        queueTrackKey = nil
        queueAttemptCount = 0
        adjacencyStore = NowPlayingAdjacencyStore()
        lyricsPrefetchRequested = []
        pendingNavigation = nil
        lines.reset()
    }

    deinit {
        // Process.terminate is safe off the main actor; avoid touching @Published.
        if let p = streamProcess, p.isRunning { p.terminate() }
        if let p = artworkProcess, p.isRunning { p.terminate() }
        if let p = queueProcess, p.isRunning { p.terminate() }
        ticker?.invalidate()
        lyricTimer?.invalidate()
    }

    // MARK: Stream

    func startStream(paths: (dylib: String, launcher: String)) {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, "np_stream"]
        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = FileHandle.nullDevice

        let handle = pipe.fileHandleForReading
        handle.readabilityHandler = { [weak self] fh in
            let data = fh.availableData
            guard !data.isEmpty else { return }
            Task { @MainActor in self?.ingest(data) }
        }

        proc.terminationHandler = { [weak self] _ in
            Task { @MainActor in self?.handleStreamExit(paths: paths) }
        }

        do {
            try proc.run()
            streamProcess = proc
            startTicker()
            startLyricTimer()
        } catch {
            available = false
        }
    }

    func handleStreamExit(paths: (dylib: String, launcher: String)) {
        streamProcess = nil
        // Restart once on unexpected exit; never loop (adapter guidance).
        guard !restartAttempted else {
            available = false
            return
        }
        restartAttempted = true
        startStream(paths: paths)
    }

    func ingest(_ data: Data) {
        for line in lines.take(data) {
            decodeLine(line)
        }
    }

    func decodeLine(_ data: Data) {
        guard let payload = NowPlayingPayloadDecoder.metadata(from: data) else { return }
        let inlineArtworkData = NowPlayingPayloadDecoder.artwork(from: data)?.data
        let hasInlineArtwork = inlineArtworkData != nil

        var snap = Snapshot(
            title: payload.title,
            artist: payload.artist,
            album: payload.album,
            appName: payload.appName,
            playing: payload.playing,
            duration: payload.duration,
            elapsedTime: payload.elapsedTime,
            timestamp: payload.timestamp,
            playbackRate: payload.playbackRate,
            trackKey: payload.trackKey
        )

        let previousSnapshot = snapshot
        if let previousSnapshot,
           previousSnapshot.trackKey != snap.trackKey,
           NowPlayingTrackIdentity.isSameTrack(
               title: previousSnapshot.title,
               artist: previousSnapshot.artist,
               duration: previousSnapshot.duration,
               otherTitle: snap.title,
               otherArtist: snap.artist,
               otherDuration: snap.duration
           ) {
            snap.trackKey = previousSnapshot.trackKey
            if snap.duration <= 0 {
                snap.duration = previousSnapshot.duration
            }
            if snap.album.isEmpty {
                snap.album = previousSnapshot.album
            }
        }
        let previousElapsed = previousSnapshot.map(currentElapsed)
        let trackChanged = previousSnapshot?.trackKey != snap.trackKey
        snapshot = snap
        rebaseline(from: snap)

        // np_stream re-emits the FULL payload on every InfoDidChange, not just on
        // track change. Decode the inline artwork when EITHER the track changed OR
        // the cover bytes actually differ from what's shown — so a corrected/late
        // cover for the current track still lands (self-healing) while identical
        // re-sends are skipped (no re-decode/re-blur churn, no stale lock-in).
        if let artData = inlineArtworkData {
            let newTrack = displayedArtworkTrackKey != snap.trackKey
            if newTrack || Self.artworkSignature(artData) != lastArtworkSignature {
                applyInlineArtwork(from: data, trackKey: snap.trackKey)
            }
        }

        if trackChanged {
            if let previousSnapshot,
               let previousElapsed,
               let direction = transitionDirection(
                   from: previousSnapshot,
                   elapsed: previousElapsed
               ) {
                adjacencyStore.observeTransition(
                    from: queueItem(from: previousSnapshot),
                    to: queueItem(from: snap),
                    direction: direction
                )
            } else {
                pendingNavigation = nil
            }
            artworkAttemptCount = 0
            if !hasInlineArtwork {
                fetchArtwork(for: snap.trackKey)
            }
            if let process = queueProcess, process.isRunning {
                process.terminate()
            }
            queueProcess = nil
            queueTrackKey = nil
            queueAttemptCount = 0
            startAdjacentLyricsPrefetch(for: snap.trackKey)
            fetchQueue(for: snap.trackKey)
            currentLyric = nil
            nextLyric = nil
            lyrics.trackChanged(to: lyricsTrack(snap))
            return
        }
        scheduleLyricsFetch(for: snap)
    }

    func fetchQueue(for trackKey: String) {
        if queueTrackKey == trackKey { return }
        guard queueAttemptCount < 3,
              let paths = Self.helperPaths() else { return }
        queueAttemptCount += 1

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, "np_queue"]
        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = FileHandle.nullDevice

        do {
            try proc.run()
            queueProcess = proc
            queueTrackKey = trackKey
        } catch {
            queueProcess = nil
            queueTrackKey = nil
            return
        }

        let handle = pipe.fileHandleForReading
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let data = handle.readDataToEndOfFile()
            proc.waitUntilExit()
            let items = proc.terminationStatus == 0
                ? NowPlayingPayloadDecoder.queue(from: data)
                : nil
            Task { @MainActor in
                guard let self else { return }
                if self.queueProcess === proc {
                    self.queueProcess = nil
                    self.queueTrackKey = nil
                }
                guard self.snapshot?.trackKey == trackKey else { return }
                if let items, !items.isEmpty {
                    self.adjacencyStore.merge(items, around: trackKey)
                    self.startAdjacentLyricsPrefetch(for: trackKey)
                    return
                }
                guard self.queueAttemptCount < 3 else { return }
                let delay = self.queueAttemptCount == 1 ? 0.25 : 0.75
                DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                    guard let self, self.snapshot?.trackKey == trackKey else { return }
                    self.fetchQueue(for: trackKey)
                }
            }
        }
    }

    struct PreparedArtwork {
        let trackKey: String
        let metadata: NowPlayingMetadataPayload
        let image: NSImage?
        let color: NSColor?
        let palette: [NSColor]?
        let topColor: NSColor?
        let bottomColor: NSColor?
        let signature: Int
    }

    // Cheap content fingerprint of the raw cover bytes (count + head/tail sample) so
    // we can tell a genuinely new cover from the same one re-sent every InfoDidChange.
}
