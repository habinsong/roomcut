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

    @Published private(set) var snapshot: Snapshot?
    @Published private(set) var artwork: NSImage?
    // A representative accent colour extracted from the current artwork, for the
    // dynamic background/ring theme. nil when there's no artwork.
    @Published private(set) var artworkColor: NSColor?
    @Published private(set) var artworkPalette: [NSColor]?
    // Average colour of the artwork's top / bottom edge strips, for the B-layout
    // blend that extends the cover into the wash above and below.
    @Published private(set) var artworkTopColor: NSColor?
    @Published private(set) var artworkBottomColor: NSColor?
    @Published private(set) var available = false
    // Estimated current elapsed time, advanced by a 1s timer while playing.
    @Published private(set) var elapsedNow: Double = 0
    // Current synced lyric line from LRCLIB (nil = none / not yet loaded). Driven
    // by a light timer against the estimated elapsed time.
    @Published private(set) var currentLyric: String?
    @Published private(set) var nextLyric: String?

    // MRCommand ids (match MediaRemote.h).
    enum Command: Int {
        case play = 0, pause = 1, togglePlayPause = 2, stop = 3, next = 4, previous = 5
    }

    private var streamProcess: Process?
    private var lineBuffer = Data()
    private var ticker: Timer?
    private var lyricTimer: Timer?
    private var lyricLines: [LyricLine] = []
    private var lyricsCache: [String: [LyricLine]] = [:]   // by trackKey — never refetched
    private var lyricsTrackKey: String?
    private var lyricsInFlight: Set<String> = []
    private var lyricsRequestedForCurrentTrack = false
    private var lyricsStartTask: Task<Void, Never>?
    private var restartAttempted = false
    private var artworkProcess: Process?
    private var artworkTrackKey: String?
    private var displayedArtworkTrackKey: String?
    private var lastArtworkSignature: Int?
    private var artworkAttemptCount = 0
    private var queueProcess: Process?
    private var queueTrackKey: String?
    private var queueAttemptCount = 0
    private var adjacencyStore = NowPlayingAdjacencyStore()
    private var lyricsPrefetchRequested: Set<String> = []
    private var pendingNavigation: (
        direction: NowPlayingTransitionDirection,
        expiresAt: Date
    )?

    // Baseline for elapsed estimation: elapsedTime captured at `baselineDate`.
    private var baselineElapsed: Double = 0
    private var baselineDate = Date()
    private var baselinePlaying = false

    // MARK: Paths

    // dylib + perl launcher: bundle Resources in production; build output in dev.
    private static func helperPaths() -> (dylib: String, launcher: String)? {
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
        startStream(paths: paths)
    }

    func stop() {
        ticker?.invalidate()
        ticker = nil
        lyricTimer?.invalidate()
        lyricTimer = nil
        lyricsStartTask?.cancel()
        lyricsStartTask = nil
        lyricLines = []
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
        lineBuffer.removeAll()
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

    private func startStream(paths: (dylib: String, launcher: String)) {
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

    private func handleStreamExit(paths: (dylib: String, launcher: String)) {
        streamProcess = nil
        // Restart once on unexpected exit; never loop (adapter guidance).
        guard !restartAttempted else {
            available = false
            return
        }
        restartAttempted = true
        startStream(paths: paths)
    }

    private func ingest(_ data: Data) {
        lineBuffer.append(data)
        while let nl = lineBuffer.firstIndex(of: 0x0A) {
            let lineData = lineBuffer.subdata(in: lineBuffer.startIndex..<nl)
            lineBuffer.removeSubrange(lineBuffer.startIndex...nl)
            if !lineData.isEmpty { decodeLine(lineData) }
        }
    }

    private func decodeLine(_ data: Data) {
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
            lyricLines = []
            currentLyric = nil
            nextLyric = nil
            lyricsTrackKey = snap.trackKey
            lyricsRequestedForCurrentTrack = false
            lyricsStartTask?.cancel()
            lyricsStartTask = nil
        }
        scheduleLyricsFetch(for: snap)
    }

    private func fetchQueue(for trackKey: String) {
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

    private struct PreparedArtwork {
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
    nonisolated private static func artworkSignature(_ data: Data) -> Int {
        var hasher = Hasher()
        hasher.combine(data.count)
        hasher.combine(data.prefix(1024))
        hasher.combine(data.suffix(1024))
        return hasher.finalize()
    }

    private func fetchArtwork(for trackKey: String) {
        if artworkTrackKey == trackKey { return }
        guard artworkAttemptCount < 3 else { return }
        if let current = artworkProcess, current.isRunning { current.terminate() }
        guard let paths = Self.helperPaths() else { return }
        artworkAttemptCount += 1

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, "np_get"]
        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = FileHandle.nullDevice

        do {
            try proc.run()
            artworkProcess = proc
            artworkTrackKey = trackKey
        } catch {
            artworkProcess = nil
            artworkTrackKey = nil
            return
        }

        let handle = pipe.fileHandleForReading
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let data = handle.readDataToEndOfFile()
            proc.waitUntilExit()
            let prepared = proc.terminationStatus == 0
                ? Self.prepareArtwork(from: data)
                : nil
            Task { @MainActor in
                guard let self else { return }
                if self.artworkProcess === proc {
                    self.artworkProcess = nil
                    self.artworkTrackKey = nil
                }
                guard self.snapshot?.trackKey == trackKey else {
                    return
                }
                // Apply when the fetched cover is for the track we're showing. Match
                // by identity (isSameTrack), not exact key — isSameTrack may have
                // remapped snap.trackKey on a duration drift, so an exact compare
                // against the freshly-fetched key would wrongly reject a valid cover.
                if let prepared, prepared.image != nil, let snap = self.snapshot,
                   NowPlayingTrackIdentity.isSameTrack(
                       title: snap.title, artist: snap.artist, duration: snap.duration,
                       otherTitle: prepared.metadata.title,
                       otherArtist: prepared.metadata.artist,
                       otherDuration: prepared.metadata.duration) {
                    self.applyPreparedArtwork(prepared, for: trackKey)
                }

                let needsRetry = prepared?.image == nil
                    || (self.snapshot?.album.isEmpty ?? true)
                guard needsRetry else { return }
                guard self.artworkAttemptCount < 3 else {
                    if self.displayedArtworkTrackKey != trackKey {
                        self.artwork = nil
                    }
                    return
                }
                let delay = self.artworkAttemptCount == 1 ? 0.25 : 0.75
                DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                    guard let self, self.snapshot?.trackKey == trackKey else { return }
                    self.fetchArtwork(for: trackKey)
                }
            }
        }
    }

    private func applyInlineArtwork(from data: Data, trackKey: String) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let prepared = Self.prepareArtwork(from: data)
            Task { @MainActor in
                // Inline artwork rides the SAME stream line as the metadata that set
                // the current trackKey, so it belongs to the current track by
                // construction — gate only on "are we still on this track". (Do NOT
                // compare prepared.trackKey: when isSameTrack remapped snap.trackKey
                // to the previous key, the line's recomputed key won't match and the
                // valid inline cover would be wrongly rejected → slow np_get / stale.)
                guard let self, self.snapshot?.trackKey == trackKey else { return }
                guard let prepared, prepared.image != nil else {
                    self.fetchArtwork(for: trackKey)
                    return
                }
                self.applyPreparedArtwork(prepared, for: trackKey)
            }
        }
    }

    private func applyPreparedArtwork(_ prepared: PreparedArtwork, for trackKey: String) {
        if var snap = snapshot,
           !prepared.metadata.album.isEmpty,
           snap.album != prepared.metadata.album {
            snap.album = prepared.metadata.album
            snapshot = snap
            scheduleLyricsFetch(for: snap)
        }
        if let image = prepared.image {
            artwork = image
            displayedArtworkTrackKey = trackKey
            artworkColor = prepared.color
            artworkPalette = prepared.palette
            artworkTopColor = prepared.topColor
            artworkBottomColor = prepared.bottomColor
            lastArtworkSignature = prepared.signature
        }
    }

    nonisolated private static func prepareArtwork(
        from data: Data
    ) -> PreparedArtwork? {
        guard let metadata = NowPlayingPayloadDecoder.metadata(from: data),
              let payload = NowPlayingPayloadDecoder.artwork(from: data) else {
            return nil
        }
        guard let data = payload.data else {
            return PreparedArtwork(
                trackKey: payload.trackKey,
                metadata: metadata,
                image: nil,
                color: nil,
                palette: nil,
                topColor: nil,
                bottomColor: nil,
                signature: 0
            )
        }
        guard let image = downsampledImage(from: data) else { return nil }
        let edges = edgeColors(of: image)
        return PreparedArtwork(
            trackKey: payload.trackKey,
            metadata: metadata,
            image: image,
            color: dominantColor(of: image),
            palette: colorPalette(of: image),
            topColor: edges?.top,
            bottomColor: edges?.bottom,
            signature: artworkSignature(data)
        )
    }

    // Decode the cover at a capped size instead of full resolution. It's only shown
    // at ≤150pt, and the colour/wash extraction downscale further anyway — so a full
    // ~1000px+ JPEG decode (×3: decode + dominant + palette) was wasted CPU on every
    // track change. CGImageSource thumbnails decode straight to the small size.
    nonisolated private static func downsampledImage(from data: Data, maxPixel: Int = 320) -> NSImage? {
        guard let src = CGImageSourceCreateWithData(data as CFData, nil) else {
            return NSImage(data: data)
        }
        let opts: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: maxPixel,
            kCGImageSourceCreateThumbnailWithTransform: true,
        ]
        guard let cg = CGImageSourceCreateThumbnailAtIndex(src, 0, opts as CFDictionary) else {
            return NSImage(data: data)
        }
        let final = ArtworkCanvas.trimmedLetterboxBars(cg) ?? cg
        return NSImage(cgImage: final, size: NSSize(width: final.width, height: final.height))
    }

    // Average colour of the artwork's top and bottom edge strips (8% tall), so the
    // B layout can extend those exact colours up / down into the wash. CGImage uses
    // a top-left origin, so y=0 is the visual top.
    nonisolated static func edgeColors(of image: NSImage) -> (top: NSColor, bottom: NSColor)? {
        guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
        let w = cg.width, h = cg.height
        let strip = max(1, Int(Double(h) * 0.08))
        guard let topCG = cg.cropping(to: CGRect(x: 0, y: 0, width: w, height: strip)),
              let bottomCG = cg.cropping(to: CGRect(x: 0, y: h - strip, width: w, height: strip)),
              let top = averageColor(of: topCG),
              let bottom = averageColor(of: bottomCG) else { return nil }
        return (top, bottom)
    }

    // Collapses a CGImage to one colour by drawing it into a 1×1 context.
    nonisolated private static func averageColor(of cg: CGImage) -> NSColor? {
        var px = [UInt8](repeating: 0, count: 4)
        guard let ctx = CGContext(
            data: &px, width: 1, height: 1, bitsPerComponent: 8,
            bytesPerRow: 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.interpolationQuality = .medium
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: 1, height: 1))
        let a = Double(px[3]) / 255.0
        guard a > 0.01 else {
            return NSColor(red: CGFloat(Double(px[0]) / 255.0),
                           green: CGFloat(Double(px[1]) / 255.0),
                           blue: CGFloat(Double(px[2]) / 255.0), alpha: 1)
        }
        return NSColor(red: CGFloat(min(1, Double(px[0]) / 255.0 / a)),
                       green: CGFloat(min(1, Double(px[1]) / 255.0 / a)),
                       blue: CGFloat(min(1, Double(px[2]) / 255.0 / a)), alpha: 1)
    }

    // Downsamples the artwork to a tiny bitmap and averages it, then nudges the
    // result toward a usable accent (floors saturation/brightness) so washed-out
    // or near-black covers still tint the theme. Runs on a 16×16 grid — cheap,
    // only on track change.
    nonisolated static func dominantColor(of image: NSImage) -> NSColor? {
        let side = 16
        guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
        let cs = CGColorSpaceCreateDeviceRGB()
        var px = [UInt8](repeating: 0, count: side * side * 4)
        guard let ctx = CGContext(
            data: &px, width: side, height: side, bitsPerComponent: 8,
            bytesPerRow: side * 4, space: cs,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: side, height: side))

        var rT = 0.0, gT = 0.0, bT = 0.0, n = 0.0
        for i in stride(from: 0, to: px.count, by: 4) {
            let a = Double(px[i + 3]) / 255.0
            if a < 0.1 { continue }
            // Skip near-grey pixels so a colourful highlight wins over a grey field.
            let r = Double(px[i]), g = Double(px[i + 1]), b = Double(px[i + 2])
            let mx = max(r, g, b), mn = min(r, g, b)
            let weight = 1.0 + (mx - mn) / 255.0 * 2.0   // saturated pixels count more
            rT += r * weight; gT += g * weight; bT += b * weight; n += weight
        }
        guard n > 0 else { return nil }
        var color = NSColor(red: CGFloat(rT / n / 255.0),
                            green: CGFloat(gT / n / 255.0),
                            blue: CGFloat(bT / n / 255.0), alpha: 1)
        if let hsb = color.usingColorSpace(.deviceRGB) {
            let s = hsb.saturationComponent >= 0.08 ? max(hsb.saturationComponent, 0.35) : hsb.saturationComponent
            let v = min(max(hsb.brightnessComponent, 0.45), 0.9)
            color = NSColor(hue: hsb.hueComponent, saturation: s, brightness: v, alpha: 1)
        }
        return color
    }

    // Extracts a palette of distinct colors for dynamic UI elements (like the corona gradient).
    nonisolated static func colorPalette(of image: NSImage) -> [NSColor]? {
        let side = 16
        guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
        let cs = CGColorSpaceCreateDeviceRGB()
        var px = [UInt8](repeating: 0, count: side * side * 4)
        guard let ctx = CGContext(
            data: &px, width: side, height: side, bitsPerComponent: 8,
            bytesPerRow: side * 4, space: cs,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: side, height: side))

        var colors: [NSColor] = []
        // Sample points from the 16x16 grid: corners, midpoints, and center
        let sampleIndices = [
            (2, 2), (13, 2), (2, 13), (13, 13), // 4 corners (inset)
            (8, 2), (8, 13), (2, 8), (13, 8),   // 4 mid-edges
            (7, 7), (8, 8)                      // center
        ]

        for (x, y) in sampleIndices {
            let i = (y * side + x) * 4
            let a = Double(px[i + 3]) / 255.0
            if a < 0.1 { continue }
            let r = CGFloat(Double(px[i]) / 255.0)
            let g = CGFloat(Double(px[i + 1]) / 255.0)
            let b = CGFloat(Double(px[i + 2]) / 255.0)
            var c = NSColor(red: r, green: g, blue: b, alpha: 1.0)
            
            if let hsb = c.usingColorSpace(.deviceRGB) {
                // Hue/saturation measured on a near-black pixel is JPEG/quantisation
                // noise, not a real colour. Without this guard a black cover's dark
                // pixels bloom into a vivid fake hue (purple) once brightness is
                // floored to 0.5 — so treat too-dark pixels as neutral grey instead
                // of amplifying their noisy saturation.
                let rawS = hsb.brightnessComponent >= 0.20 ? hsb.saturationComponent : 0
                let s = rawS >= 0.08 ? max(rawS, 0.4) : rawS
                let v = min(max(hsb.brightnessComponent, 0.5), 0.9)
                c = NSColor(hue: hsb.hueComponent, saturation: s, brightness: v, alpha: 1)
            }
            colors.append(c)
        }

        // Filter to mostly distinct colors (simple distance check)
        var distinct: [NSColor] = []
        for c in colors {
            guard let cRGB = c.usingColorSpace(.deviceRGB) else { continue }
            let isTooSimilar = distinct.contains { existing in
                guard let existingRGB = existing.usingColorSpace(.deviceRGB) else { return false }
                let dr = cRGB.redComponent - existingRGB.redComponent
                let dg = cRGB.greenComponent - existingRGB.greenComponent
                let db = cRGB.blueComponent - existingRGB.blueComponent
                return (dr*dr + dg*dg + db*db) < 0.05 // Distance threshold
            }
            if !isTooSimilar {
                distinct.append(c)
                if distinct.count >= 5 { break }
            }
        }

        // Ensure we have at least some colors, repeat if necessary for a smooth gradient
        if distinct.isEmpty, let dominant = dominantColor(of: image) {
            distinct = [dominant, dominant]
        }
        if distinct.count == 1 {
            distinct.append(distinct[0])
        }
        return distinct
    }

    // MARK: Elapsed estimation

    private func rebaseline(from snap: Snapshot) {
        // elapsedTime was current at snap.timestamp; advance to now if playing.
        baselineElapsed = snap.elapsedTime
        baselineDate = snap.timestamp
        baselinePlaying = snap.playing
        elapsedNow = currentElapsed(snap)
    }

    private func currentElapsed(_ snap: Snapshot) -> Double {
        guard snap.playing else { return baselineElapsed }
        let delta = Date().timeIntervalSince(baselineDate) * snap.playbackRate
        let value = baselineElapsed + max(0, delta)
        return snap.duration > 0 ? min(value, snap.duration) : value
    }

    private func startTicker() {
        ticker?.invalidate()
        ticker = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, let snap = self.snapshot, snap.playing else { return }
                self.elapsedNow = self.currentElapsed(snap)
            }
        }
    }

    // MARK: Lyrics (LRCLIB, cached per track)

    // 0.3 s so the line lands promptly; only recomputes and publishes on change.
    // Cheap no-op when the current track has no lyrics.
    private func startLyricTimer() {
        lyricTimer?.invalidate()
        lyricTimer = Timer.scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refreshLyricLine() }
        }
    }

    private func refreshLyricLine() {
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

    private func scheduleLyricsFetch(for snap: Snapshot) {
        guard !snap.title.isEmpty else { return }
        if lyricsCache[snap.trackKey] != nil {
            fetchLyrics(for: snap)
            return
        }
        guard !lyricsRequestedForCurrentTrack else { return }

        if !snap.album.isEmpty {
            lyricsStartTask?.cancel()
            lyricsStartTask = nil
            fetchLyrics(for: snap)
            return
        }
        guard lyricsStartTask == nil else { return }

        let key = snap.trackKey
        lyricsStartTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 150_000_000)
            guard !Task.isCancelled,
                  let self,
                  let current = self.snapshot,
                  current.trackKey == key else { return }
            self.lyricsStartTask = nil
            self.fetchLyrics(for: current)
        }
    }

    private func fetchLyrics(for snap: Snapshot) {
        let key = snap.trackKey
        lyricsTrackKey = key
        if let cached = lyricsCache[key] {
            lyricLines = cached
            refreshLyricLine()
            return
        }
        guard !snap.title.isEmpty,
              !lyricsInFlight.contains(key),
              !lyricsRequestedForCurrentTrack else { return }
        lyricsRequestedForCurrentTrack = true
        lyricsInFlight.insert(key)
        let (title, artist, album, duration) = (
            snap.title,
            snap.artist,
            snap.album,
            snap.duration
        )
        Task { [weak self] in
            let synced = await LRCLIBClient.fetchSyncedLyrics(
                title: title,
                artist: artist,
                album: album,
                duration: duration
            )
            let lines = synced.map(LyricsParsing.parse) ?? []
            await MainActor.run {
                guard let self else { return }
                self.lyricsInFlight.remove(key)
                if !lines.isEmpty {
                    self.lyricsCache[key] = lines
                    if self.lyricsTrackKey == key {
                        self.lyricLines = lines
                        self.refreshLyricLine()
                    }
                }
                self.startAdjacentLyricsPrefetch(for: key)
            }
        }
    }

    private func startAdjacentLyricsPrefetch(for currentTrackKey: String) {
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

    private func transitionDirection(
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

    private func queueItem(from snap: Snapshot) -> NowPlayingQueueItemPayload {
        NowPlayingQueueItemPayload(
            relativeOffset: 0,
            title: snap.title,
            artist: snap.artist,
            album: snap.album,
            duration: snap.duration
        )
    }

    // MARK: Controls

    func command(_ cmd: Command) {
        guard available, let paths = Self.helperPaths() else { return }
        if cmd == .previous {
            pendingNavigation = (.previous, Date().addingTimeInterval(5))
        } else if cmd == .next {
            pendingNavigation = (.next, Date().addingTimeInterval(5))
        }
        runOneShotAsync(paths: paths, function: "np_send",
                        env: ["ROOMCUT_NP_COMMAND": String(cmd.rawValue)])
    }

    func seek(toSeconds seconds: Double) {
        guard available, let paths = Self.helperPaths() else { return }
        let clamped = max(0, seconds)
        // Optimistic update: reflect the new position immediately so the bar
        // doesn't snap back while we wait for the next stream notification.
        if var snap = snapshot {
            baselineElapsed = clamped
            baselineDate = Date()
            snap.elapsedTime = clamped
            snap.timestamp = baselineDate
            snapshot = snap
            elapsedNow = clamped
        }
        let micros = Int((clamped * 1_000_000).rounded())
        runOneShotAsync(paths: paths, function: "np_seek",
                        env: ["ROOMCUT_NP_POSITION_US": String(micros)])
    }

    // MARK: One-shot perl helpers

    // Synchronous: used only for the self-test gate (fast, blocks start()).
    private func runOneShot(paths: (dylib: String, launcher: String),
                            function: String, env: [String: String]) -> Int32 {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, function]
        proc.standardOutput = FileHandle.nullDevice
        proc.standardError = FileHandle.nullDevice
        if !env.isEmpty {
            var merged = ProcessInfo.processInfo.environment
            for (k, v) in env { merged[k] = v }
            proc.environment = merged
        }
        do {
            try proc.run()
            proc.waitUntilExit()
            return proc.terminationStatus
        } catch {
            return -1
        }
    }

    // Fire-and-forget control command; never blocks the UI.
    private func runOneShotAsync(paths: (dylib: String, launcher: String),
                                 function: String, env: [String: String]) {
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/perl")
        proc.arguments = [paths.launcher, paths.dylib, function]
        proc.standardOutput = FileHandle.nullDevice
        proc.standardError = FileHandle.nullDevice
        if !env.isEmpty {
            var merged = ProcessInfo.processInfo.environment
            for (k, v) in env { merged[k] = v }
            proc.environment = merged
        }
        try? proc.run()
    }
}
