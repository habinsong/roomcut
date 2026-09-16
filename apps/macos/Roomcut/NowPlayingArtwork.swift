import Foundation
import AppKit
import Combine
import ImageIO
import RoomcutCore
import RoomcutPresentationCore

// Artwork: fetching it out-of-process, downsampling it, and pulling the colours
// the Now Playing backdrop is built from. The heavy image work is nonisolated so
// it stays off the main actor.
extension NowPlayingMonitor {
    nonisolated static func artworkSignature(_ data: Data) -> Int {
        var hasher = Hasher()
        hasher.combine(data.count)
        hasher.combine(data.prefix(1024))
        hasher.combine(data.suffix(1024))
        return hasher.finalize()
    }

    func fetchArtwork(for trackKey: String) {
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

    func applyInlineArtwork(from data: Data, trackKey: String) {
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

    func applyPreparedArtwork(_ prepared: PreparedArtwork, for trackKey: String) {
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

    nonisolated static func prepareArtwork(
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
    nonisolated static func downsampledImage(from data: Data, maxPixel: Int = 320) -> NSImage? {
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
    nonisolated static func averageColor(of cg: CGImage) -> NSColor? {
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
}
