import Combine
import Foundation

@MainActor
public protocol RoomTuneAudio: AnyObject {
    var inputPeak: Float { get }
    func requestPermission() async -> Bool
    func measure(deviceID: String) async throws -> [(freq: Double, db: Double)]
}

@MainActor
public final class RoomTuneWorkflow: ObservableObject {
    public enum Phase: Equatable {
        case idle, preparing, stopping, done
        case measuring(round: Int, total: Int)
        case failed(String)
    }
    public static let rounds = 3
    @Published public private(set) var phase: Phase = .idle
    @Published public private(set) var inputPeak: Float = 0
    @Published public private(set) var result: RoomTuneResult?
    @Published public var strength: RoomTuneStrength = .medium { didSet { recompute() } }

    private let audio: RoomTuneAudio
    private let pause: @MainActor () async throws -> Void
    private var responses: [[(freq: Double, db: Double)]] = []
    private var task: Task<Void, Never>?
    private var meterTask: Task<Void, Never>?
    public var isBusy: Bool { task != nil }
    private enum Failure: String, Error, LocalizedError {
        case permission = "마이크 권한이 필요합니다 (시스템 설정 › 개인정보 보호 › 마이크)"
        case bypass = "Roomcut 처리를 우회하지 못했습니다"
        var errorDescription: String? { rawValue }
    }

    public init(audio: RoomTuneAudio) {
        self.audio = audio
        pause = { try await Task.sleep(nanoseconds: 1_500_000_000) }
    }
    init(audio: RoomTuneAudio, pause: @escaping @MainActor () async throws -> Void) {
        self.audio = audio; self.pause = pause
    }

    @discardableResult
    public func start(deviceID: String, beforeStart: @escaping @MainActor () async -> Bool,
                      onFinish: @escaping @MainActor () async -> Bool) -> Bool {
        guard !isBusy else { return false }
        responses = []; result = nil; phase = .preparing
        task = Task {
            var shouldRestore = false
            var terminal: Phase = .done
            do {
                try Task.checkCancellation()
                let granted = await audio.requestPermission()
                try Task.checkCancellation()
                guard granted else { throw Failure.permission }
                // A failed or cancelled acknowledgement cannot prove that the
                // engine did not apply the request. Always compensate an attempt.
                shouldRestore = true
                let accepted = await beforeStart()
                try Task.checkCancellation()
                guard accepted else { throw Failure.bypass }
                for round in 0..<Self.rounds {
                    try Task.checkCancellation()
                    phase = .measuring(round: round + 1, total: Self.rounds)
                    startMeter()
                    let response = try await audio.measure(deviceID: deviceID)
                    try Task.checkCancellation()
                    stopMeter()
                    responses.append(response)
                    if round + 1 < Self.rounds { try await pause() }
                }
            } catch {
                terminal = error is CancellationError ? .idle : .failed(error.localizedDescription)
            }
            stopMeter()
            var restored = true
            if shouldRestore {
                phase = .stopping
                // Cleanup owns an uncancelled task, and this run keeps waiting
                // for it before releasing the audio/engine state to another run.
                restored = await Task { @MainActor in await onFinish() }.value
            }
            if !restored { terminal = .failed("Roomcut 처리 상태를 복구하지 못했습니다") }
            else if Task.isCancelled { terminal = .idle }
            task = nil
            if terminal == .done { result = RoomTuneAnalysis.analyze(responses: responses, strength: strength) }
            else { responses = []; result = nil }
            phase = terminal
        }
        return true
    }

    public func cancel() {
        guard let task else { return }
        phase = .stopping; stopMeter(); task.cancel()
    }
    public func cancelAndWait() async {
        guard let task else { return }
        cancel()
        await task.value
    }
    private func startMeter() {
        meterTask = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                inputPeak = audio.inputPeak
                do { try await Task.sleep(nanoseconds: 80_000_000) } catch { return }
            }
        }
    }
    private func stopMeter() { meterTask?.cancel(); meterTask = nil; inputPeak = 0 }
    private func recompute() {
        if !isBusy && !responses.isEmpty { result = RoomTuneAnalysis.analyze(responses: responses, strength: strength) }
    }
}
