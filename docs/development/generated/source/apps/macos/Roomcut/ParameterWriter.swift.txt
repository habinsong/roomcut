import Combine
import Foundation

// Main-actor scheduler: one in-flight Mach request and at most one replacement.
// Cancellation invalidates UI completion; it never assumes sent IPC was cancelled.
@MainActor
final class ParameterWriter: ObservableObject {
    enum Command {
        case parameters(SoundSnapshot, comparison: SoundComparisonWrite?)
        case preset(String, UInt64, comparison: SoundComparisonWrite?)
    }
    private struct Request {
        let id: UInt64
        let command: Command
        var completion: ((Bool) -> Void)?
    }
    @Published private(set) var isBusy = false
    private(set) var revision: UInt64 = 0
    var didComplete: ((UInt64, Command, Result<Void, Error>) async -> Void)?
    private let client: EngineClientProtocol
    private let debounceNanoseconds: UInt64
    private var pending: Request?
    private var ready = false
    private var processing = false
    private var delay: Task<Void, Never>?

    init(client: EngineClientProtocol, debounceNanoseconds: UInt64) {
        self.client = client
        self.debounceNanoseconds = debounceNanoseconds
    }

    func parameters(_ snapshot: SoundSnapshot, comparison: SoundComparisonWrite? = nil, immediate: Bool = false) {
        submit(.parameters(snapshot, comparison: comparison), immediate: immediate)
    }

    func preset(_ id: String, editorRevision: UInt64, comparison: SoundComparisonWrite? = nil) async -> Bool {
        await withCheckedContinuation { continuation in
            submit(.preset(id, editorRevision, comparison: comparison), immediate: true) { continuation.resume(returning: $0) }
        }
    }

    func isCurrent(_ id: UInt64) -> Bool { revision == id }

    func cancel() {
        revision &+= 1
        delay?.cancel(); delay = nil
        pending?.completion?(false)
        pending = nil
        ready = false
        if !processing { isBusy = false }
    }

    private func submit(_ command: Command, immediate: Bool, completion: ((Bool) -> Void)? = nil) {
        revision &+= 1
        let id = revision
        pending?.completion?(false)
        pending = Request(id: id, command: command, completion: completion)
        isBusy = true
        if immediate {
            delay?.cancel(); delay = nil
            ready = true
            drain()
        } else if delay == nil && !ready {
            // A bounded coalescing window: continuous dragging cannot postpone
            // every write indefinitely. The timer sends the newest pending value.
            delay = Task { [weak self, debounceNanoseconds] in
                do { try await Task.sleep(nanoseconds: debounceNanoseconds) }
                catch { return }
                guard !Task.isCancelled, let self, self.pending != nil else { return }
                self.delay = nil
                self.ready = true
                self.drain()
            }
        }
    }

    private func drain() {
        guard !processing, ready, let request = pending else { return }
        pending = nil
        ready = false
        processing = true
        Task {
            let result: Result<Void, Error>
            do {
                switch request.command {
                case .parameters(let snapshot, let comparison):
                    if let comparison {
                        try await client.setComparison(.parameters(snapshot.parameters), reference: comparison.reference, enabled: comparison.enabled)
                    }
                    else { try await client.setParams(snapshot.parameters) }
                case .preset(let id, _, let comparison):
                    if let comparison { try await client.setComparison(.preset(id), reference: comparison.reference, enabled: comparison.enabled) }
                    else { try await client.setPreset(id) }
                }
                result = .success(())
            } catch { result = .failure(error) }
            if isCurrent(request.id) { await didComplete?(request.id, request.command, result) }
            let succeeded: Bool
            if case .success = result { succeeded = isCurrent(request.id) } else { succeeded = false }
            request.completion?(succeeded)
            processing = false
            if pending == nil { isBusy = false }
            drain()
        }
    }
}
