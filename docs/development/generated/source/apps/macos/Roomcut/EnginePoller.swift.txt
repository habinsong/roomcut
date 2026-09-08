import Foundation

@MainActor
final class EnginePoller {
    struct Cycle { fileprivate let id: UInt64 }
    enum Read: Hashable {
        case devices, controls, analysis, comparison
        var interval: TimeInterval {
            switch self {
            case .devices: return 5
            case .controls, .analysis: return 0.5
            case .comparison: return 0.25
            }
        }
    }
    private var timer: Timer?
    private var sequence: UInt64 = 0
    private var pollingRevision: UInt64 = 0
    private var active: UInt64?
    private var completed: [Read: Date] = [:]
    private(set) var deviceRevision: UInt64 = 0

    deinit { timer?.invalidate() }

    func start(_ refresh: @escaping @MainActor () async -> Void) {
        guard timer == nil else { return }
        pollingRevision &+= 1
        let revision = pollingRevision
        Task { [weak self] in
            guard self?.timer != nil, self?.pollingRevision == revision else { return }
            await refresh()
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 12.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard self?.timer != nil, self?.pollingRevision == revision else { return }
                await refresh()
            }
        }
    }
    func stop() {
        timer?.invalidate(); timer = nil
        pollingRevision &+= 1
        active = nil
        completed.removeAll()
        deviceRevision &+= 1
    }
    func begin() -> Cycle? {
        guard active == nil else { return nil }
        sequence &+= 1
        active = sequence
        return Cycle(id: sequence)
    }
    func isCurrent(_ cycle: Cycle) -> Bool { active == cycle.id && !Task.isCancelled }
    func finish(_ cycle: Cycle) {
        if active == cycle.id { active = nil }
    }
    func isDue(_ read: Read, at now: Date, force: Bool = false) -> Bool {
        guard !force, let last = completed[read] else { return true }
        let elapsed = now.timeIntervalSince(last)
        return elapsed < 0 || elapsed >= read.interval
    }
    func didRead(_ read: Read, in cycle: Cycle, at now: Date = Date()) {
        if isCurrent(cycle) { completed[read] = now }
    }
    func didReadDevice(_ read: Read, revision: UInt64, at now: Date = Date()) {
        if deviceRevision == revision { completed[read] = now }
    }
    func invalidateDevices() {
        deviceRevision &+= 1
        completed[.controls] = nil
    }
}
