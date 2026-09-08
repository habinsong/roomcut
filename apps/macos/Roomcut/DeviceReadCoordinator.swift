import Foundation

@MainActor
final class DeviceReadCoordinator {
    struct Request {
        let uid: String
        let revision: UInt64
        var devices: Bool
        var controls: Bool
        func sameContext(as other: Request) -> Bool { uid == other.uid && revision == other.revision }
        func covers(_ other: Request) -> Bool { (!other.devices || devices) && (!other.controls || controls) }
    }

    // Cancellation handlers may run outside the main actor. Every mutable
    // ticket field is locked; continuations are resumed only after unlocking.
    final class Ticket: @unchecked Sendable {
        private let lock = NSLock()
        private var finished = false
        private var cancelled = false
        private var waiters: [CheckedContinuation<Void, Never>] = []
        var isCancelled: Bool { lock.withLock { cancelled } }
        func wait() async {
            await withTaskCancellationHandler {
                await withCheckedContinuation { continuation in
                    let immediate = lock.withLock {
                        if finished { return true }
                        waiters.append(continuation)
                        return false
                    }
                    if immediate { continuation.resume() }
                }
            } onCancel: { self.cancel() }
        }
        func cancel() { finish(cancelled: true) }
        fileprivate func finish(cancelled: Bool = false) {
            let pending: [CheckedContinuation<Void, Never>] = lock.withLock {
                guard !finished else { return [] }
                finished = true
                self.cancelled = cancelled
                let pending = waiters
                waiters.removeAll()
                return pending
            }
            pending.forEach { $0.resume() }
        }
    }

    private struct Entry { var request: Request; let generation: UInt64; let ticket: Ticket }
    private let client: EngineClientProtocol
    private var generation: UInt64 = 0
    private var active: Entry?
    private var pending: Entry?
    private var wanted: Request?
    var didRead: ((Request, DeviceReadback) -> Void)?

    init(client: EngineClientProtocol) { self.client = client }
    var isBusy: Bool { active != nil || pending != nil }

    // One physical read and one latest replacement. Repeated meter ticks join
    // the existing read instead of enqueuing another blocking HAL operation.
    func request(_ request: Request) -> Ticket {
        wanted = request
        if let pending, !pending.request.sameContext(as: request) {
            pending.ticket.cancel()
            self.pending = nil
        }
        if let active, active.generation == generation && !active.ticket.isCancelled &&
            active.request.sameContext(as: request) && active.request.covers(request) {
            return active.ticket
        }
        if let active, !active.request.sameContext(as: request) { active.ticket.cancel() }
        if var pending, pending.generation == generation && !pending.ticket.isCancelled {
            pending.request.devices = pending.request.devices || request.devices
            pending.request.controls = pending.request.controls || request.controls
            self.pending = pending
            return pending.ticket
        }
        let entry = Entry(request: request, generation: generation, ticket: Ticket())
        if active == nil { start(entry) }
        else { pending = entry }
        return entry.ticket
    }

    func cancel() {
        generation &+= 1
        wanted = nil
        active?.ticket.cancel()
        pending?.ticket.cancel()
        pending = nil
        // Keep the active slot until the native read actually returns.
    }

    private func start(_ entry: Entry) {
        active = entry
        Task {
            if canDeliver(entry) {
                let value = await client.deviceReadback(for: entry.request.uid,
                    includeDevices: entry.request.devices, includeControls: entry.request.controls)
                if canDeliver(entry) { didRead?(entry.request, value) }
            }
            active = nil
            entry.ticket.finish()
            if let next = pending {
                pending = nil
                start(next)
            }
        }
    }

    private func canDeliver(_ entry: Entry) -> Bool {
        entry.generation == generation && !entry.ticket.isCancelled &&
            wanted.map { $0.sameContext(as: entry.request) } == true
    }
}
