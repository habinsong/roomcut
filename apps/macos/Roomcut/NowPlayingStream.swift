import Foundation

// The helper process writes newline-delimited JSON on a pipe, and a read can end
// anywhere — mid-line, on the newline, or across several lines at once. This
// keeps the leftovers so the decoder only ever sees whole lines.
public struct NowPlayingLineReader {
    // A line this long is not something the helper writes; it means the pipe is
    // carrying junk. Drop what is buffered and pick up at the next newline rather
    // than growing until the app runs out of memory.
    public static let lineLimit = 1 << 20   // 1 MiB

    private var buffer = Data()
    private var discarding = false

    public init() {}

    public var pendingBytes: Int { buffer.count }
    public var isResynchronising: Bool { discarding }

    public mutating func take(_ data: Data) -> [Data] {
        var lines: [Data] = []
        buffer.append(data)
        while let newline = buffer.firstIndex(of: 0x0A) {
            let line = buffer.subdata(in: buffer.startIndex..<newline)
            buffer.removeSubrange(buffer.startIndex...newline)
            if discarding {
                discarding = false   // that newline ends the junk
                continue
            }
            if !line.isEmpty { lines.append(line) }
        }
        if buffer.count > Self.lineLimit {
            buffer.removeAll(keepingCapacity: false)
            discarding = true
        }
        return lines
    }

    public mutating func reset() {
        buffer.removeAll(keepingCapacity: false)
        discarding = false
    }
}

// Where playback has got to between helper updates. The helper reports elapsed
// time as of its own timestamp; this carries it forward so the position moves
// once a second without asking the helper again.
public struct NowPlayingClock {
    private var elapsed: Double = 0
    private var at = Date()

    public init() {}

    public mutating func rebaseline(elapsed: Double, at moment: Date) {
        self.elapsed = elapsed
        self.at = moment
    }

    // Paused holds where it is. Playing advances by the wall clock times the
    // playback rate, never backwards (a helper timestamp can arrive from the
    // future), and never past a known duration.
    public func position(playing: Bool, rate: Double, duration: Double, now: Date = Date()) -> Double {
        guard playing else { return elapsed }
        let advanced = elapsed + max(0, now.timeIntervalSince(at) * rate)
        return duration > 0 ? min(advanced, duration) : advanced
    }
}
