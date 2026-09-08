import Foundation

actor LRCLIBLyricsCache {
    static let capacity = 1000
    static let durationTolerance = 2

    private struct Entry: Codable {
        let lyrics: String
        let savedAt: Date
    }

    struct Match: Sendable {
        let key: String
        let lyrics: String
    }

    private let fileURL: URL
    private var entries: [String: Entry]?

    init(fileURL: URL = LRCLIBLyricsCache.defaultFileURL()) {
        self.fileURL = fileURL
    }

    func value(for key: String) -> String? {
        loadIfNeeded()
        return entries?[key]?.lyrics
    }

    func value(
        for key: String,
        identityPrefix: String,
        duration: Int?
    ) -> Match? {
        loadIfNeeded()
        guard let entries else { return nil }
        if let exact = entries[key] {
            return Match(key: key, lyrics: exact.lyrics)
        }

        let candidates = entries.filter { $0.key.hasPrefix(identityPrefix) }
        if let duration {
            let closest = candidates
                .compactMap { candidate -> (key: String, entry: Entry, delta: Int)? in
                    guard let candidateDuration = Self.duration(
                        from: candidate.key,
                        identityPrefix: identityPrefix
                    ) else {
                        return nil
                    }
                    return (
                        candidate.key,
                        candidate.value,
                        abs(candidateDuration - duration)
                    )
                }
                .filter { $0.delta <= Self.durationTolerance }
                .sorted {
                    if $0.delta != $1.delta { return $0.delta < $1.delta }
                    return $0.entry.savedAt > $1.entry.savedAt
                }
                .first
            if let closest {
                return Match(key: closest.key, lyrics: closest.entry.lyrics)
            }

            let durationlessCandidates = candidates.filter { candidate in
                Self.duration(
                    from: candidate.key,
                    identityPrefix: identityPrefix
                ) == nil
            }
            if let durationless = durationlessCandidates.max(
                by: { $0.value.savedAt < $1.value.savedAt }
            ) {
                return Match(
                    key: durationless.key,
                    lyrics: durationless.value.lyrics
                )
            }
            return nil
        }

        guard let latest = candidates.max(
            by: { $0.value.savedAt < $1.value.savedAt }
        ) else {
            return nil
        }
        return Match(key: latest.key, lyrics: latest.value.lyrics)
    }

    func insert(_ lyrics: String, for key: String) {
        loadIfNeeded()
        entries?[key] = Entry(lyrics: lyrics, savedAt: Date())

        if let count = entries?.count, count > Self.capacity {
            let oldestKeys = entries?
                .sorted { $0.value.savedAt < $1.value.savedAt }
                .prefix(count - Self.capacity)
                .map(\.key) ?? []
            for oldKey in oldestKeys {
                entries?.removeValue(forKey: oldKey)
            }
        }
        persist()
    }

    func count() -> Int {
        loadIfNeeded()
        return entries?.count ?? 0
    }

    /// Forget every cached lyric and delete the on-disk file.
    func clear() {
        entries = [:]
        try? FileManager.default.removeItem(at: fileURL)
    }

    private func loadIfNeeded() {
        guard entries == nil else { return }
        guard let data = try? Data(contentsOf: fileURL),
              let decoded = try? JSONDecoder().decode([String: Entry].self, from: data) else {
            entries = [:]
            return
        }
        entries = decoded
    }

    private func persist() {
        guard let entries,
              let data = try? JSONEncoder().encode(entries) else { return }
        let directory = fileURL.deletingLastPathComponent()
        try? FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        try? data.write(to: fileURL, options: .atomic)
    }

    private static func duration(
        from key: String,
        identityPrefix: String
    ) -> Int? {
        let suffix = key.dropFirst(identityPrefix.count)
        guard let last = suffix.split(
            separator: "|",
            omittingEmptySubsequences: false
        ).last,
        !last.isEmpty else {
            return nil
        }
        return Int(last)
    }

    private static func defaultFileURL() -> URL {
        let base = FileManager.default.urls(
            for: .cachesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory
        return base
            .appendingPathComponent("com.habinsong.roomcut", isDirectory: true)
            .appendingPathComponent("lyrics.json")
    }
}

public struct LRCLIBTrackRequest: Hashable, Sendable {
    public let title: String
    public let artist: String
    public let album: String
    public let duration: Double

    public init(title: String, artist: String, album: String, duration: Double) {
        self.title = title
        self.artist = artist
        self.album = album
        self.duration = duration
    }
}

actor LRCLIBLookupCoordinator {
    private var tasks: [String: Task<String?, Never>] = [:]

    func value(
        for key: String,
        operation: @escaping @Sendable () async -> String?
    ) async -> String? {
        if let task = tasks[key] {
            return await task.value
        }

        let task = Task { await operation() }
        tasks[key] = task
        let result = await task.value
        tasks[key] = nil
        return result
    }
}

public enum LRCLIBClient {
    private struct Record: Decodable {
        let trackName: String?
        let artistName: String?
        let duration: Double?
        let syncedLyrics: String?
    }

    private static let persistentCache = LRCLIBLyricsCache()
    private static let lookupCoordinator = LRCLIBLookupCoordinator()
    static let maximumAttempts = 3
    static let retryDelayNanoseconds: UInt64 = 2_000_000_000
    static let requestTimeout: TimeInterval = 20
    static let maximumConnectionsPerHost = 18
    static let searchHedgeDelayNanoseconds: UInt64 = 0

    private static let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.urlCache = nil
        configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.timeoutIntervalForRequest = requestTimeout
        configuration.timeoutIntervalForResource = requestTimeout + 2
        configuration.waitsForConnectivity = false
        configuration.httpMaximumConnectionsPerHost = maximumConnectionsPerHost
        return URLSession(configuration: configuration)
    }()

    public static func prewarm() async {
        guard let url = URL(string: "https://lrclib.net/") else { return }
        var request = URLRequest(url: url)
        request.httpMethod = "HEAD"
        request.timeoutInterval = 3
        addHeaders(to: &request)
        _ = try? await session.data(for: request)
    }

    /// Number of tracks currently in the on-disk lyrics cache.
    public static func cachedTrackCount() async -> Int {
        await persistentCache.count()
    }

    /// Erase the on-disk lyrics cache (Settings → Lyrics).
    public static func clearCache() async {
        await persistentCache.clear()
    }

    public static func fetchSyncedLyrics(
        title: String,
        artist: String,
        album: String,
        duration: Double
    ) async -> String? {
        let key = cacheKey(
            title: title,
            artist: artist,
            duration: duration
        )
        if let cached = await cachedLyrics(
            key: key,
            title: title,
            artist: artist,
            duration: duration
        ) {
            return cached
        }

        return await lookupCoordinator.value(for: key) {
            if let cached = await cachedLyrics(
                key: key,
                title: title,
                artist: artist,
                duration: duration
            ) {
                return cached
            }
            return await fetchAndCache(
                key: key,
                title: title,
                artist: artist,
                album: album,
                duration: duration
            )
        }
    }

    private static func cachedLyrics(
        key: String,
        title: String,
        artist: String,
        duration: Double
    ) async -> String? {
        let match = await persistentCache.value(
            for: key,
            identityPrefix: cacheIdentityPrefix(title: title, artist: artist),
            duration: duration > 0 ? Int(duration.rounded()) : nil
        )
        guard let match else { return nil }
        if match.key != key {
            await persistentCache.insert(match.lyrics, for: key)
        }
        return match.lyrics
    }

    public static func prefetchSyncedLyrics(
        _ requests: [LRCLIBTrackRequest]
    ) async -> [LRCLIBTrackRequest] {
        await withTaskGroup(of: LRCLIBTrackRequest?.self) { group in
            for request in requests {
                group.addTask {
                    let lyrics = await fetchSyncedLyrics(
                        title: request.title,
                        artist: request.artist,
                        album: request.album,
                        duration: request.duration
                    )
                    return lyrics == nil ? request : nil
                }
            }

            var failed: [LRCLIBTrackRequest] = []
            for await request in group {
                if let request {
                    failed.append(request)
                }
            }
            return failed
        }
    }

    static func forEachConcurrently(
        _ requests: [LRCLIBTrackRequest],
        operation: @escaping @Sendable (LRCLIBTrackRequest) async -> Void
    ) async {
        await withTaskGroup(of: Void.self) { group in
            for request in requests {
                group.addTask {
                    await operation(request)
                }
            }
            await group.waitForAll()
        }
    }

    static func retrying(
        maxAttempts: Int,
        retryDelayNanoseconds: UInt64,
        operation: @escaping @Sendable () async -> String?
    ) async -> String? {
        guard maxAttempts > 0 else { return nil }

        return await withTaskGroup(of: String?.self) { group in
            for attempt in 0..<maxAttempts {
                group.addTask {
                    if attempt > 0 {
                        do {
                            try await Task.sleep(
                                nanoseconds: retryDelayNanoseconds * UInt64(attempt)
                            )
                        } catch {
                            return nil
                        }
                    }
                    guard !Task.isCancelled else { return nil }
                    return await operation()
                }
            }

            while let result = await group.next() {
                if let result {
                    group.cancelAll()
                    return result
                }
            }
            return nil
        }
    }

    private static func fetchAndCache(
        key: String,
        title: String,
        artist: String,
        album: String,
        duration: Double
    ) async -> String? {
        if let exact = await fetchAttempt(
            title: title, artist: artist, album: album, duration: duration
        ) {
            await persistentCache.insert(exact, for: key)
            return exact
        }

        // Broadened fallback. Apple Music (and other stores) tag titles like
        // "Song (feat. X)" / "Song (Remastered 2011)" / "Song - Single Version",
        // credit multiple artists, and file albums differently from LRCLIB, all of
        // which sink the exact lookup. Retry once with a cleaned title, the primary
        // artist, and no album so the search can still land a synced match. This
        // only runs after the normal lookup already returned nothing, so it can't
        // change an existing good match.
        let relaxedTitle = cleanedTitle(title)
        let relaxedArtist = primaryArtist(artist)
        guard relaxedTitle != title || relaxedArtist != artist || !album.isEmpty else {
            return nil
        }
        guard let relaxed = await fetchAttempt(
            title: relaxedTitle, artist: relaxedArtist, album: "", duration: duration
        ) else { return nil }
        await persistentCache.insert(relaxed, for: key)
        return relaxed
    }

    private static func fetchAttempt(
        title: String,
        artist: String,
        album: String,
        duration: Double
    ) async -> String? {
        guard let exactURL = lookupURL(
            title: title,
            artist: artist,
            album: album,
            duration: duration
        ),
        let searchURL = searchURL(title: title, artist: artist, album: album) else {
            return nil
        }
        return await retrying(
            maxAttempts: maximumAttempts,
            retryDelayNanoseconds: retryDelayNanoseconds
        ) {
            await fetchOnce(
                exactURL: exactURL,
                searchURL: searchURL,
                title: title,
                artist: artist,
                duration: duration
            )
        }
    }

    // Strip store-added qualifiers LRCLIB usually omits: a trailing "(…)" / "[…]"
    // (feat., Remastered, Live, Deluxe…) and a trailing " - …" suffix.
    static func cleanedTitle(_ title: String) -> String {
        var s = title.replacingOccurrences(
            of: #"\s*[\(\[][^\)\]]*[\)\]]\s*$"#, with: "", options: .regularExpression)
        s = s.replacingOccurrences(of: #"\s+-\s+.*$"#, with: "", options: .regularExpression)
        let trimmed = s.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? title : trimmed
    }

    // Take the lead artist before a "feat./ft./featuring" credit. Conservative on
    // purpose: it does NOT split on "&" or "," since duos are often filed in full.
    static func primaryArtist(_ artist: String) -> String {
        let s = artist.replacingOccurrences(
            of: #"(?i)\s*(feat\.?|ft\.?|featuring)\s+.*$"#, with: "", options: .regularExpression)
        let trimmed = s.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? artist : trimmed
    }

    private static func fetchOnce(
        exactURL: URL,
        searchURL: URL,
        title: String,
        artist: String,
        duration: Double
    ) async -> String? {
        await withTaskGroup(of: String?.self) { group -> String? in
            group.addTask {
                guard let data = await request(exactURL, timeout: requestTimeout) else {
                    return nil
                }
                return (try? JSONDecoder().decode(Record.self, from: data))?
                    .syncedLyrics?
                    .nonEmpty
            }
            group.addTask {
                if searchHedgeDelayNanoseconds > 0 {
                    try? await Task.sleep(
                        nanoseconds: searchHedgeDelayNanoseconds
                    )
                }
                guard !Task.isCancelled,
                      let data = await request(searchURL, timeout: requestTimeout) else {
                    return nil
                }
                return syncedLyrics(
                    fromSearchResponse: data,
                    title: title,
                    artist: artist,
                    duration: duration
                )
            }

            while let result = await group.next() {
                if let result {
                    group.cancelAll()
                    return result
                }
            }
            return nil
        }
    }

    static func lookupURL(
        title: String,
        artist: String,
        album: String,
        duration: Double
    ) -> URL? {
        var components = URLComponents(string: "https://lrclib.net/api/get")
        var items = [
            URLQueryItem(name: "track_name", value: title),
            URLQueryItem(name: "artist_name", value: artist),
        ]
        if !album.isEmpty {
            items.append(URLQueryItem(name: "album_name", value: album))
        }
        if duration > 0 {
            items.append(
                URLQueryItem(name: "duration", value: String(Int(duration.rounded())))
            )
        }
        components?.queryItems = items
        return components?.url
    }

    static func syncedLyrics(
        fromSearchResponse data: Data,
        title: String,
        artist: String,
        duration: Double
    ) -> String? {
        guard let records = try? JSONDecoder().decode([Record].self, from: data) else {
            return nil
        }
        return records
            .filter { $0.syncedLyrics?.nonEmpty != nil }
            .max {
                score($0, title: title, artist: artist, duration: duration)
                    < score($1, title: title, artist: artist, duration: duration)
            }?
            .syncedLyrics?
            .nonEmpty
    }

    public static func cacheKey(
        title: String,
        artist: String,
        duration: Double
    ) -> String {
        cacheIdentityPrefix(title: title, artist: artist)
            + (duration > 0 ? String(Int(duration.rounded())) : "")
    }

    static func cacheIdentityPrefix(title: String, artist: String) -> String {
        [normalized(title), normalized(artist), ""].joined(separator: "|")
    }

    private static func normalized(_ value: String) -> String {
        value
            .folding(
                options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive],
                locale: Locale(identifier: "en_US_POSIX")
            )
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func searchURL(
        title: String,
        artist: String,
        album: String
    ) -> URL? {
        var components = URLComponents(string: "https://lrclib.net/api/search")
        var items = [
            URLQueryItem(name: "track_name", value: title),
            URLQueryItem(name: "artist_name", value: artist),
        ]
        if !album.isEmpty {
            items.append(URLQueryItem(name: "album_name", value: album))
        }
        components?.queryItems = items
        return components?.url
    }

    private static func score(
        _ record: Record,
        title: String,
        artist: String,
        duration: Double
    ) -> Int {
        var value = 0
        if normalized(record.trackName ?? "") == normalized(title) { value += 100 }
        if normalized(record.artistName ?? "") == normalized(artist) { value += 100 }
        if duration > 0, let candidateDuration = record.duration {
            value += max(0, 50 - Int(abs(candidateDuration - duration).rounded()))
        }
        return value
    }

    private static func request(_ url: URL, timeout: TimeInterval) async -> Data? {
        var request = URLRequest(url: url)
        request.timeoutInterval = timeout
        addHeaders(to: &request)

        guard let (data, response) = try? await session.data(for: request),
              let http = response as? HTTPURLResponse,
              http.statusCode == 200 else {
            return nil
        }
        return data
    }

    private static func addHeaders(to request: inout URLRequest) {
        let client = "Roomcut v1.0 (https://github.com/habinsong/roomcut)"
        request.setValue(client, forHTTPHeaderField: "User-Agent")
        request.setValue(client, forHTTPHeaderField: "Lrclib-Client")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
    }
}

private extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
}
