import Combine

public enum SoundComparisonSlot: String, CaseIterable {
    case a = "A", b = "B"
    var other: Self { self == .a ? .b : .a }
}

// One session's editing history. This type owns no engine transport or storage.
@MainActor
public final class SoundEditor: ObservableObject {
    private struct Timeline {
        var current = SoundSnapshot()
        var undo: [SoundSnapshot] = []
        var redo: [SoundSnapshot] = []
        var beforeEdit: SoundSnapshot?
    }
    private var timelines: [SoundComparisonSlot: Timeline] = [.a: Timeline(), .b: Timeline()]
    @Published public private(set) var activeSlot: SoundComparisonSlot = .a
    @Published public private(set) var snapshot = SoundSnapshot()
    @Published public private(set) var hasBaseline = false
    @Published public private(set) var revision: UInt64 = 0
    private var gestureActive = false
    private let historyLimit = 100

    public var isEditing: Bool { gestureActive || timelines[activeSlot]!.beforeEdit != nil }
    public var isGestureActive: Bool { gestureActive }
    public var canUndo: Bool { timelines[activeSlot]!.beforeEdit.map { $0 != snapshot } == true || !timelines[activeSlot]!.undo.isEmpty }
    public var canRedo: Bool { timelines[activeSlot]!.beforeEdit == nil && !timelines[activeSlot]!.redo.isEmpty }
    public var canCopy: Bool { timelines[activeSlot.other]!.current != snapshot }
    public var referenceSnapshot: SoundSnapshot { timelines[activeSlot.other]!.current }

    public init() {}

    func update(_ mutate: (inout SoundSnapshot) -> Void) {
        var next = snapshot
        mutate(&next)
        next.parameters = next.parameters.normalized()
        next.macros = next.macros.mapValues { SoundSnapshot.clamp($0, -1, 1) }
        guard next != snapshot else { return }
        if timelines[activeSlot]!.beforeEdit == nil { timelines[activeSlot]!.beforeEdit = snapshot }
        publish(next)
    }

    func relabel(saved: String?, builtin: String?) {
        var next = snapshot
        next.savedPresetName = saved
        next.builtinPresetID = builtin
        if next != snapshot { publish(next) }
    }

    public func beginGesture() { gestureActive = true }
    public func endGesture() {
        gestureActive = false
        commitEdit()
    }

    func commitEdit() {
        guard !gestureActive, let before = timelines[activeSlot]!.beforeEdit else { return }
        timelines[activeSlot]!.beforeEdit = nil
        if before != snapshot {
            timelines[activeSlot]!.undo.append(before)
            if timelines[activeSlot]!.undo.count > historyLimit { timelines[activeSlot]!.undo.removeFirst() }
            timelines[activeSlot]!.redo.removeAll()
        }
        revision &+= 1
    }

    public func undo() {
        gestureActive = false
        commitEdit()
        guard let previous = timelines[activeSlot]!.undo.popLast() else { return }
        timelines[activeSlot]!.redo.append(snapshot)
        publish(previous)
    }

    public func redo() {
        guard timelines[activeSlot]!.beforeEdit == nil,
              let next = timelines[activeSlot]!.redo.popLast() else { return }
        timelines[activeSlot]!.undo.append(snapshot)
        publish(next)
    }

    public func select(_ slot: SoundComparisonSlot) {
        guard activeSlot != slot else { return }
        gestureActive = false
        commitEdit()
        activeSlot = slot
        publish(timelines[slot]!.current)
    }

    public func copyToOther() {
        timelines[activeSlot.other] = Timeline(current: snapshot)
        revision &+= 1
    }

    func restoreReference(_ parameters: EngineParameters) {
        timelines[activeSlot.other] = Timeline(current: SoundSnapshot(parameters: parameters))
        revision &+= 1
    }

    // Initial synchronization seeds both slots. Remote changes/failures reset
    // only the active timeline, leaving the comparison reference available.
    func synchronize(_ value: SoundSnapshot, reset: Bool) {
        if !hasBaseline {
            timelines = [.a: Timeline(current: value), .b: Timeline(current: value)]
            hasBaseline = true
        } else if reset {
            gestureActive = false
            timelines[activeSlot] = Timeline(current: value)
        }
        publish(value)
    }

    func finishEditing() { gestureActive = false; commitEdit() }

    private func publish(_ value: SoundSnapshot) {
        timelines[activeSlot]!.current = value
        snapshot = value
        revision &+= 1
    }
}
