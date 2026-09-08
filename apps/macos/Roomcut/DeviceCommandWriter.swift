import Foundation

@MainActor
final class DeviceCommandWriter {
    enum Command {
        case volume(Double), balance(Double), output(String)
        case format(uid: String, sampleRate: Double, bitDepth: Int)
        case master(Bool), keepDefault(Bool), claimDefault

        enum Key { case volume, balance, output, format, defaultOutput, keepDefault }
        var key: Key {
            switch self {
            case .volume: return .volume
            case .balance: return .balance
            case .output: return .output
            case .format: return .format
            case .master, .claimDefault: return .defaultOutput
            case .keepDefault: return .keepDefault
            }
        }
        var affectsControls: Bool { if case .claimDefault = self { return false }; return true }
        var failureMessage: String {
            switch self {
            case .volume: return "볼륨을 변경하지 못했습니다"
            case .balance: return "좌우 밸런스를 변경하지 못했습니다"
            case .output: return "출력 장치를 변경하지 못했습니다"
            case .format: return "출력 포맷을 변경하지 못했습니다"
            case .master(let on): return on ? "Roomcut을 켜지 못했습니다" : "Roomcut을 끄지 못했습니다"
            case .keepDefault: return "출력 고정을 변경하지 못했습니다"
            case .claimDefault: return "Roomcut 출력을 선택하지 못했습니다"
            }
        }
    }
    private struct Request { let id: UInt64; let generation: UInt64; let command: Command }
    private(set) var isBusy = false
    private(set) var error: String?
    private(set) var revision: UInt64 = 0
    private var feedbackRevision: UInt64 = 0
    var didComplete: ((UInt64, Command, Result<Void, Error>) -> Void)?
    private let client: EngineClientProtocol
    private var generation: UInt64 = 0
    private var pending: [Request] = []
    private var active: Request?

    init(client: EngineClientProtocol) { self.client = client }
    var blocksControls: Bool { active?.command.affectsControls == true || pending.contains { $0.command.affectsControls } }
    func isCurrent(_ id: UInt64) -> Bool { id == feedbackRevision }

    func submit(_ command: Command) {
        revision &+= 1
        // Startup routing must not erase the result of an explicit user edit.
        if command.affectsControls || feedbackRevision == 0 { feedbackRevision = revision }
        pending.removeAll { $0.command.key == command.key }
        pending.append(Request(id: revision, generation: generation, command: command))
        if isCurrent(revision) && error != nil { error = nil }
        updateBusy()
        drain()
    }
    func cancel() {
        generation &+= 1; revision &+= 1
        feedbackRevision = revision
        pending.removeAll()
        updateBusy()
    }
    private func updateBusy() {
        let busy = active != nil || !pending.isEmpty
        if isBusy != busy { isBusy = busy }
    }
    private func drain() {
        guard active == nil, !pending.isEmpty else { return }
        let request = pending.removeFirst()
        active = request
        Task {
            let result: Result<Void, Error>
            do {
                try checkGeneration(request)
                try await execute(request)
                result = .success(())
            } catch { result = .failure(error) }
            active = nil
            updateBusy()
            if request.generation == generation && isCurrent(request.id) {
                if case .failure = result { error = request.command.failureMessage }
                else if error != nil { error = nil }
                didComplete?(request.id, request.command, result)
            }
            drain()
        }
    }
    private func checkGeneration(_ request: Request) throws {
        if request.generation != generation { throw CancellationError() }
    }
    private func execute(_ request: Request) async throws {
        switch request.command {
        case .volume(let value): try await client.writeVolume(value)
        case .balance(let value): try await client.writeBalance(value)
        case .output(let uid):
            try await client.setOutputDevice(uid)
            try checkGeneration(request)
            try await client.setDefaultOutput(roomcut: true)
        case .format(let uid, let rate, let depth):
            try await client.setDeviceFormat(uid: uid, sampleRate: rate, bitDepth: depth)
        case .master(let on):
            try await client.setKeepDefault(on)
            try checkGeneration(request)
            try await client.setDefaultOutput(roomcut: on)
        case .keepDefault(let on): try await client.setKeepDefault(on)
        case .claimDefault: try await client.setDefaultOutput(roomcut: true)
        }
    }
}
