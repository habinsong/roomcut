import AppKit
import Combine

// Routes the existing Edit menu and its standard shortcuts. Native text editing
// keeps its own undo manager; other focus targets use the sound-edit history.
@MainActor
public final class SoundEditMenuController: NSObject, NSUserInterfaceValidations {
    private weak var model: RoomcutViewModel?
    private var observer: NSObjectProtocol?
    private var edits: AnyCancellable?
    private var keyMonitor: Any?
    private let focusedTextEditor: @MainActor () -> NSTextView?

    public init(model: RoomcutViewModel,
                focusedTextEditor: @escaping @MainActor () -> NSTextView? = { NSApp.keyWindow?.firstResponder as? NSTextView }) {
        self.model = model
        self.focusedTextEditor = focusedTextEditor
        super.init()
        // Accessory/borderless windows do not always own the system menu bar.
        // Keep the same commands available through this app's local event path.
        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            self?.handleKey(event) == true ? nil : event
        }
        edits = model.objectWillChange.sink { [weak self] in
            DispatchQueue.main.async { self?.install() }
        }
        observer = NotificationCenter.default.addObserver(forName: NSMenu.didBeginTrackingNotification,
                                                          object: nil, queue: .main) { [weak self] _ in
            DispatchQueue.main.async { self?.install() }
        }
    }

    deinit {
        if let observer { NotificationCenter.default.removeObserver(observer) }
        if let keyMonitor { NSEvent.removeMonitor(keyMonitor) }
    }

    func handleKey(_ event: NSEvent) -> Bool {
        let modifiers = event.modifierFlags.intersection(.deviceIndependentFlagsMask).subtracting(.capsLock)
        guard event.charactersIgnoringModifiers?.lowercased() == "z" else { return false }
        if modifiers == [.command] {
            let available = textEditor.map { $0.undoManager?.canUndo ?? false } ?? (model?.canUndoSound ?? false)
            guard available else { return false }
            undoSound(nil)
            return true
        }
        if modifiers == [.command, .shift] {
            let available = textEditor.map { $0.undoManager?.canRedo ?? false } ?? (model?.canRedoSound ?? false)
            guard available else { return false }
            redoSound(nil)
            return true
        }
        return false
    }

    public func install(in menu: NSMenu? = nil) {
        guard let menu = menu ?? NSApp.mainMenu else { return }
        connect(menu)
    }

    private func connect(_ menu: NSMenu) {
        var changed = false
        for item in menu.items {
            if item.action == NSSelectorFromString("undo:") || item.action == #selector(undoSound(_:)) {
                item.target = self
                item.action = #selector(undoSound(_:))
                item.isEnabled = validateUserInterfaceItem(item)
                changed = true
            } else if item.action == NSSelectorFromString("redo:") || item.action == #selector(redoSound(_:)) {
                item.target = self
                item.action = #selector(redoSound(_:))
                item.isEnabled = validateUserInterfaceItem(item)
                changed = true
            }
            if let submenu = item.submenu { connect(submenu) }
        }
        if changed { menu.update() }
    }

    private var textEditor: NSTextView? {
        guard let editor = focusedTextEditor(), editor.isEditable else { return nil }
        return editor
    }

    @objc private func undoSound(_ sender: Any?) {
        if let editor = textEditor { editor.undoManager?.undo() }
        else { model?.undoSound() }
    }

    @objc private func redoSound(_ sender: Any?) {
        if let editor = textEditor { editor.undoManager?.redo() }
        else { model?.redoSound() }
    }

    public func validateUserInterfaceItem(_ item: NSValidatedUserInterfaceItem) -> Bool {
        if item.action == #selector(undoSound(_:)) {
            return textEditor.map { $0.undoManager?.canUndo ?? false } ?? (model?.canUndoSound ?? false)
        }
        if item.action == #selector(redoSound(_:)) {
            return textEditor.map { $0.undoManager?.canRedo ?? false } ?? (model?.canRedoSound ?? false)
        }
        return false
    }

}
