// The on-disk lyrics cache. Keyed per track with a small duration tolerance, so
// the same song from a different source still hits.
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
