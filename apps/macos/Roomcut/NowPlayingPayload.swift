import CoreGraphics
import Foundation

public struct NowPlayingMetadataPayload: Equatable, Sendable {
    public let title: String
    public let artist: String
    public let album: String
    public let appName: String
    public let playing: Bool
    public let duration: Double
    public let elapsedTime: Double
    public let timestamp: Date
    public let playbackRate: Double
    public let trackKey: String
}

public struct NowPlayingArtworkPayload: Equatable, Sendable {
    public let trackKey: String
    public let data: Data?
    public let mimeType: String?
}

public struct NowPlayingQueueItemPayload: Equatable, Sendable {
    public let relativeOffset: Int
    public let title: String
    public let artist: String
    public let album: String
    public let duration: Double

    public init(
        relativeOffset: Int,
        title: String,
        artist: String,
        album: String,
        duration: Double
    ) {
        self.relativeOffset = relativeOffset
        self.title = title
        self.artist = artist
        self.album = album
        self.duration = duration
    }

    public var trackKey: String {
        NowPlayingTrackIdentity.stableKey(title: title, artist: artist, duration: duration)
    }
}

public enum NowPlayingTrackIdentity {
    public static func stableKey(
        title: String,
        artist: String,
        duration: Double
    ) -> String {
        "\(title)|\(artist)|\(Int(duration.rounded()))"
    }

    public static func isSameTrack(
        title: String,
        artist: String,
        duration: Double,
        otherTitle: String,
        otherArtist: String,
        otherDuration: Double
    ) -> Bool {
        guard normalized(title) == normalized(otherTitle),
              normalized(artist) == normalized(otherArtist) else {
            return false
        }
        if duration <= 0 || otherDuration <= 0 {
            return true
        }
        return abs(duration - otherDuration) <= 3
    }

    private static func normalized(_ value: String) -> String {
        value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
    }
}

public enum NowPlayingTransitionDirection: Sendable {
    case previous
    case next
}

public struct NowPlayingAdjacencyStore: Sendable {
    private var previousByTrack: [String: NowPlayingQueueItemPayload] = [:]
    private var nextByTrack: [String: NowPlayingQueueItemPayload] = [:]

    public init() {}

    public mutating func observeTransition(
        from previous: NowPlayingQueueItemPayload,
        to current: NowPlayingQueueItemPayload,
        direction: NowPlayingTransitionDirection
    ) {
        guard previous.trackKey != current.trackKey else { return }
        switch direction {
        case .previous:
            previousByTrack[previous.trackKey] = current.withOffset(-1)
            nextByTrack[current.trackKey] = previous.withOffset(1)
        case .next:
            nextByTrack[previous.trackKey] = current.withOffset(1)
            previousByTrack[current.trackKey] = previous.withOffset(-1)
        }
    }

    public mutating func merge(
        _ items: [NowPlayingQueueItemPayload],
        around trackKey: String
    ) {
        for item in items {
            if item.relativeOffset == -1 {
                previousByTrack[trackKey] = item
            } else if item.relativeOffset == 1 {
                nextByTrack[trackKey] = item
            }
        }
    }

    public func items(around trackKey: String) -> [NowPlayingQueueItemPayload] {
        [previousByTrack[trackKey], nextByTrack[trackKey]].compactMap { $0 }
    }
}

private extension NowPlayingQueueItemPayload {
    func withOffset(_ offset: Int) -> NowPlayingQueueItemPayload {
        NowPlayingQueueItemPayload(
            relativeOffset: offset,
            title: title,
            artist: artist,
            album: album,
            duration: duration
        )
    }
}

// MediaRemote hands video artwork on a FIXED 16:9 canvas: a portrait/Shorts
// clip arrives pillarboxed (baked-in black bars left/right), cinema-scope
// content letterboxed (bars top/bottom). Trim those bars off the decoded
// thumbnail so the UI letterboxes the TRUE content — and so the colour/wash
// extraction stops sampling the black. Square canvases (album covers) are
// left alone: a dark cover's black margins are art, not padding.
public enum ArtworkCanvas {
    // Returns nil when there is nothing to trim.
    public static func trimmedLetterboxBars(_ cg: CGImage) -> CGImage? {
        let w = cg.width, h = cg.height
        guard w > 16, h > 16 else { return nil }
        let canvasRatio = Double(w) / Double(h)
        guard canvasRatio > 1.2 || canvasRatio < 0.83 else { return nil }

        guard let ctx = CGContext(
            data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
            space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else { return nil }
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        guard let raw = ctx.data else { return nil }
        let px = raw.bindMemory(to: UInt8.self, capacity: w * h * 4)

        // Near-black pixel: every channel under ~10% (headroom for JPEG noise).
        func isDark(_ x: Int, _ bufferRow: Int) -> Bool {
            let o = (bufferRow * w + x) * 4
            return px[o] < 26 && px[o + 1] < 26 && px[o + 2] < 26
        }
        // A row/column is a "bar" when ~all of its pixels are near-black.
        func isBarColumn(_ x: Int) -> Bool {
            var dark = 0
            for r in 0..<h where isDark(x, r) { dark += 1 }
            return Double(dark) >= Double(h) * 0.97
        }
        func isBarRow(_ bufferRow: Int) -> Bool {
            var dark = 0
            for x in 0..<w where isDark(x, bufferRow) { dark += 1 }
            return Double(dark) >= Double(w) * 0.97
        }

        // Trim caps keep a usable core even for extreme content (a 9:16 short on
        // a 16:9 canvas trims ~35% per side) without ever eating everything.
        let maxTrimX = Int(Double(w) * 0.42)
        let maxTrimY = Int(Double(h) * 0.42)
        var left = 0
        while left < maxTrimX, isBarColumn(left) { left += 1 }
        var right = 0
        while right < maxTrimX, isBarColumn(w - 1 - right) { right += 1 }
        // CGContext buffer row 0 is the image's BOTTOM row; CGImage.cropping's
        // rect uses a top-left origin — flip when building the crop rect.
        var bottom = 0
        while bottom < maxTrimY, isBarRow(bottom) { bottom += 1 }
        var top = 0
        while top < maxTrimY, isBarRow(h - 1 - top) { top += 1 }

        let newW = w - left - right
        let newH = h - top - bottom
        guard newW >= 16, newH >= 16 else { return nil }
        // Only act on a REAL bar (≥ 2% of the axis) — skip 1px encoder edges.
        guard left + right > w / 50 || top + bottom > h / 50 else { return nil }
        return cg.cropping(to: CGRect(x: left, y: top, width: newW, height: newH))
    }
}

public enum NowPlayingPayloadDecoder {
    public static func metadata(
        from data: Data,
        fallbackTimestamp: Date = Date()
    ) -> NowPlayingMetadataPayload? {
        guard let object = dictionary(from: data) else { return nil }
        let title = object["title"] as? String ?? ""
        let artist = object["artist"] as? String ?? ""
        let album = object["album"] as? String ?? ""
        let duration = number(object["duration"]) ?? 0
        let trackKey = NowPlayingTrackIdentity.stableKey(title: title, artist: artist, duration: duration)
        let timestamp = number(object["timestamp"])
            .map { Date(timeIntervalSince1970: $0) } ?? fallbackTimestamp

        return NowPlayingMetadataPayload(
            title: title,
            artist: artist,
            album: album,
            appName: object["appName"] as? String ?? "",
            playing: object["playing"] as? Bool ?? false,
            duration: duration,
            elapsedTime: number(object["elapsedTime"]) ?? 0,
            timestamp: timestamp,
            playbackRate: number(object["playbackRate"]) ?? 1,
            trackKey: trackKey
        )
    }

    public static func artwork(from data: Data) -> NowPlayingArtworkPayload? {
        guard let object = dictionary(from: data) else {
            return nil
        }
        let title = object["title"] as? String
        let artist = object["artist"] as? String
        let duration = number(object["duration"]) ?? 0
        let trackKey: String
        if let title, let artist {
            trackKey = NowPlayingTrackIdentity.stableKey(title: title, artist: artist, duration: duration)
        } else if let sourceKey = object["trackKey"] as? String {
            trackKey = sourceKey
        } else {
            return nil
        }
        let bytes = (object["artworkData"] as? String).flatMap {
            Data(base64Encoded: $0)
        }
        return NowPlayingArtworkPayload(
            trackKey: trackKey,
            data: bytes,
            mimeType: object["artworkMimeType"] as? String
        )
    }

    public static func queue(from data: Data) -> [NowPlayingQueueItemPayload]? {
        guard let object = dictionary(from: data),
              let rawItems = object["items"] as? [[String: Any]] else {
            return nil
        }
        return rawItems.compactMap { item in
            guard let offset = (item["relativeOffset"] as? NSNumber)?.intValue,
                  offset == -1 || offset == 1,
                  let title = item["title"] as? String,
                  !title.isEmpty else {
                return nil
            }
            return NowPlayingQueueItemPayload(
                relativeOffset: offset,
                title: title,
                artist: item["artist"] as? String ?? "",
                album: item["album"] as? String ?? "",
                duration: number(item["duration"]) ?? 0
            )
        }
    }

    private static func dictionary(from data: Data) -> [String: Any]? {
        try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    }

    private static func number(_ value: Any?) -> Double? {
        (value as? NSNumber)?.doubleValue
    }
}
