import Foundation

@MainActor
final class BypassWriter {
    struct Ownership { let id: UUID; let manualRevision: UInt64 }
    private struct Request {
        let id: UInt64
        let value: Bool
        let ownership: Ownership?
        let completion: ((Bool) -> Void)?
    }
    private let client: EngineClientProtocol
    private var revision: UInt64 = 0
    private var manualRevision: UInt64 = 0
    private var overrideID: UUID?
    private var active: Request?
    private var pending: Request?
    private(set) var error: String?
    var didComplete: ((UInt64, Result<Void, Error>) async -> Void)?
    var isBusy: Bool { active != nil || pending != nil }

    init(client: EngineClientProtocol) { self.client = client }
    func isCurrent(_ id: UInt64) -> Bool { revision == id }
    func manual(_ value: Bool, completion: ((Bool) -> Void)? = nil) {
        manualRevision &+= 1
        enqueue(value, ownership: nil, completion: completion)
    }
    func manualAndWait(_ value: Bool) async -> Bool {
        await withCheckedContinuation { continuation in
            manual(value) { continuation.resume(returning: $0) }
        }
    }

    func openOverride(id: UUID, fallback: Bool) -> (Ownership, Bool)? {
        guard overrideID == nil else { return nil }
        overrideID = id
        // A user choice awaiting its reply is newer than the polled snapshot.
        let previous: Bool
        if let pending, pending.ownership == nil { previous = pending.value }
        else if let active, active.ownership == nil { previous = active.value }
        else { previous = fallback }
        return (Ownership(id: id, manualRevision: manualRevision), previous)
    }
    func owns(_ ownership: Ownership) -> Bool {
        overrideID == ownership.id && manualRevision == ownership.manualRevision
    }
    func closeOverride(_ ownership: Ownership) {
        if overrideID == ownership.id { overrideID = nil }
    }
    func temporary(_ value: Bool, ownership: Ownership) async -> Bool {
        guard owns(ownership) else { return false }
        return await withCheckedContinuation { continuation in
            enqueue(value, ownership: ownership) { continuation.resume(returning: $0) }
        }
    }

    private func enqueue(_ value: Bool, ownership: Ownership?, completion: ((Bool) -> Void)?) {
        revision &+= 1
        pending?.completion?(false)
        pending = Request(id: revision, value: value, ownership: ownership, completion: completion)
        error = nil
        drain()
    }
    private func drain() {
        guard active == nil, let request = pending else { return }
        pending = nil; active = request
        Task {
            guard isCurrent(request.id), request.ownership.map({ owns($0) }) ?? true else {
                active = nil; request.completion?(false); drain(); return
            }
            let result: Result<Void, Error>
            do { try await client.setBypass(request.value); result = .success(()) }
            catch { result = .failure(error) }
            if isCurrent(request.id) {
                if case .failure = result { self.error = "바이패스를 변경하지 못했습니다" }
                else { self.error = nil }
                await didComplete?(request.id, result)
            }
            let succeeded: Bool
            if case .success = result { succeeded = true } else { succeeded = false }
            active = nil
            request.completion?(succeeded)
            drain()
        }
    }
}
