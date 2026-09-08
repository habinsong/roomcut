import Foundation

@MainActor
public final class BypassOverride {
    private let current: () -> Bool
    private let writer: BypassWriter
    private var ownership: BypassWriter.Ownership?
    private var previous = false
    private var began = false
    private var restoration: Task<Bool, Never>?
    init(writer: BypassWriter, current: @escaping () -> Bool) {
        self.writer = writer; self.current = current
    }
    public func begin() async -> Bool {
        guard !began, restoration == nil, !Task.isCancelled else { return false }
        began = true
        guard let (ownership, previous) = writer.openOverride(id: UUID(), fallback: current()) else { return false }
        self.ownership = ownership; self.previous = previous
        let accepted = await writer.temporary(true, ownership: ownership)
        return accepted && restoration == nil && writer.owns(ownership)
    }
    // Restoring a superseded override is a successful no-op: the user's newer
    // choice owns the engine. Never enqueue the old snapshot in that case.
    public func restore() async -> Bool {
        if let restoration { return await restoration.value }
        let writer = self.writer, ownership = self.ownership, previous = self.previous
        let restoration = Task { @MainActor in
            guard let ownership else { return true }
            defer { writer.closeOverride(ownership) }
            guard writer.owns(ownership) else { return true }
            let accepted = await writer.temporary(previous, ownership: ownership)
            return writer.owns(ownership) ? accepted : true
        }
        self.restoration = restoration
        return await restoration.value
    }
}
