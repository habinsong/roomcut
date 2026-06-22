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
