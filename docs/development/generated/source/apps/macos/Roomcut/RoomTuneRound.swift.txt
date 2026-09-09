import Foundation

// Synchronous transport operations are called only on RoomTuneWorkQueue.
public protocol RoomTuneTransport: AnyObject {
    func prepare(deviceID: String, url: URL) throws
    func play(completion: @escaping () -> Void) throws
    func stop() -> URL?
    func checkHealth() throws
}

public extension RoomTuneTransport {
    func checkHealth() throws {}
}

public enum RoomTuneRoundError: Error, LocalizedError {
    case timedOut, captureInterrupted, playbackInterrupted, recordingFailed
    public var errorDescription: String? {
        switch self {
        case .timedOut: return "테스트음 완료를 확인하지 못했습니다. 다시 측정하세요"
        case .captureInterrupted: return "녹음이 중단되었습니다. 마이크 연결을 확인하세요"
        case .playbackInterrupted: return "테스트음 재생이 중단되었습니다. 출력 장치를 확인하세요"
        case .recordingFailed: return "녹음 데이터를 처리하지 못했습니다. 다시 측정하세요"
        }
    }
}

public final class RoomTuneRound {
    private let work = RoomTuneWorkQueue()
    private let transport: RoomTuneTransport
    private let tail: () async throws -> Void
    private let analyze: (URL) throws -> [(freq: Double, db: Double)]
    private let timeoutNanoseconds: UInt64

    public init(transport: RoomTuneTransport) {
        self.transport = transport
        timeoutNanoseconds = UInt64((RoomTuneSignal.duration + 5) * 1_000_000_000)
        tail = { try await Task.sleep(nanoseconds: 1_000_000_000) }
        analyze = { url in
            let recording = try RoomTuneAudioFile.load(url)
            return try RoomTuneAnalysis.bandResponse(samples: recording.samples, sampleRate: recording.sampleRate)
        }
    }
    init(transport: RoomTuneTransport, tail: @escaping () async throws -> Void,
         timeoutNanoseconds: UInt64 = UInt64((RoomTuneSignal.duration + 5) * 1_000_000_000),
         analyze: @escaping (URL) throws -> [(freq: Double, db: Double)]) {
        self.transport = transport; self.tail = tail; self.analyze = analyze
        self.timeoutNanoseconds = timeoutNanoseconds
    }

    // One workflow owns a round at a time. Cancellation waits for submitted
    // native work, stops it, and removes the owned recording before returning.
    public func measure(deviceID: String) async throws -> [(freq: Double, db: Double)] {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("roomtune-\(UUID()).wav")
        let transport = self.transport
        let playback = AsyncStream<Void>.makeStream()
        var mayOwnHardware = false
        var stopped = false
        do {
            try Task.checkCancellation()
            mayOwnHardware = true
            try await work.perform { try transport.prepare(deviceID: deviceID, url: url) }
            try Task.checkCancellation()
            try await work.perform {
                try transport.checkHealth()
                try transport.play { playback.continuation.finish() }
            }
            try await waitForCapture(playback.stream)
            let saved = try await work.perform {
                try transport.checkHealth()
                return transport.stop()
            }
            stopped = true
            try Task.checkCancellation()
            guard let saved else { throw RoomTuneRoundError.recordingFailed }
            let analyze = self.analyze
            let response = try await work.perform { try analyze(saved) }
            try Task.checkCancellation()
            try await work.perform { try FileManager.default.removeItem(at: url) }
            return response
        } catch {
            playback.continuation.finish()
            if mayOwnHardware && !stopped { _ = try? await work.perform { transport.stop() } }
            _ = try? await work.perform { try FileManager.default.removeItem(at: url) }
            throw error
        }
    }

    private func waitForCapture(_ playback: AsyncStream<Void>) async throws {
        let work = self.work, transport = self.transport, tail = self.tail
        let deadline = ContinuousClock.now.advanced(by: .nanoseconds(Int64(timeoutNanoseconds)))
        try await withThrowingTaskGroup(of: Void.self) { group in
            group.addTask {
                for await _ in playback {}
                try Task.checkCancellation()
                try await tail()
            }
            group.addTask {
                while true {
                    try Task.checkCancellation()
                    guard ContinuousClock.now < deadline else { throw RoomTuneRoundError.timedOut }
                    try await work.perform { try transport.checkHealth() }
                    try await Task.sleep(nanoseconds: 100_000_000)
                }
            }
            defer { group.cancelAll() }
            _ = try await group.next()
            group.cancelAll()
            // Do not discard a real capture failure that raced with completion.
            do { while let _ = try await group.next() {} }
            catch is CancellationError {}
        }
        try Task.checkCancellation()
    }
}
