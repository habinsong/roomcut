import AppKit
import XCTest
@testable import RoomcutCore

@MainActor
final class SoundEditMenuTests: XCTestCase {
    private var suite = ""
    private var defaults: UserDefaults!

    override func setUp() async throws {
        (suite, defaults) = makeTestDefaults(name)
    }

    override func tearDown() async throws { defaults.removePersistentDomain(forName: suite) }

    private func makeModel() -> RoomcutViewModel {
        let model = RoomcutViewModel(client: FakeEngineClient(), defaults: defaults)
        model.status.reachable = true
        model.status.state = EngineStatus.running
        model.editor.synchronize(SoundSnapshot(), reset: true)
        model.editor.update { $0.parameters.preampDb = -3 }
        model.editor.commitEdit()
        return model
    }

    private func menu() -> NSMenu {
        let menu = NSMenu(title: "Edit")
        menu.addItem(withTitle: "Undo", action: NSSelectorFromString("undo:"), keyEquivalent: "z")
        menu.addItem(withTitle: "Redo", action: NSSelectorFromString("redo:"), keyEquivalent: "Z")
        return menu
    }

    private func key(redo: Bool = false) throws -> NSEvent {
        try XCTUnwrap(NSEvent.keyEvent(with: .keyDown, location: .zero,
            modifierFlags: redo ? [.command, .shift] : [.command], timestamp: 0,
            windowNumber: 0, context: nil, characters: redo ? "Z" : "z",
            charactersIgnoringModifiers: redo ? "Z" : "z", isARepeat: false, keyCode: 6))
    }

    func testNativeMenuShortcutsInvokeSoundUndoAndRedo() async throws {
        _ = NSApplication.shared
        let model = makeModel()
        let controller = SoundEditMenuController(model: model, focusedTextEditor: { nil })
        let menu = menu()
        controller.install(in: menu)
        XCTAssertTrue(menu.items[0].isEnabled)
        XCTAssertTrue(menu.performKeyEquivalent(with: try key()))
        XCTAssertEqual(model.preampDb, 0)
        controller.install(in: menu)
        XCTAssertTrue(menu.items[1].isEnabled)
        XCTAssertTrue(menu.performKeyEquivalent(with: try key(redo: true)))
        XCTAssertEqual(model.preampDb, -3)
        model.stopPolling()
    }

    func testNativeTextUndoTakesPriorityOverSoundHistory() async throws {
        _ = NSApplication.shared
        let model = makeModel()
        let text = UndoTextView(frame: .zero)
        text.isEditable = true
        text.allowsUndo = true
        text.manager.groupsByEvent = false
        text.manager.beginUndoGrouping()
        text.insertText("123", replacementRange: NSRange(location: 0, length: 0))
        text.manager.endUndoGrouping()
        let controller = SoundEditMenuController(model: model, focusedTextEditor: { text })
        let menu = menu()
        controller.install(in: menu)
        XCTAssertTrue(text.manager.canUndo)
        XCTAssertTrue(menu.performKeyEquivalent(with: try key()))
        XCTAssertEqual(text.string, "")
        XCTAssertEqual(model.preampDb, -3, "text undo cannot change DSP parameters")
        controller.install(in: menu)
        XCTAssertTrue(menu.performKeyEquivalent(with: try key(redo: true)))
        XCTAssertEqual(text.string, "123")
        XCTAssertEqual(model.preampDb, -3)
        model.stopPolling()
    }

    func testLocalKeyRouteWorksWithoutOwningTheMenuBar() async throws {
        _ = NSApplication.shared
        let model = makeModel()
        let controller = SoundEditMenuController(model: model, focusedTextEditor: { nil })
        XCTAssertTrue(controller.handleKey(try key()))
        XCTAssertEqual(model.preampDb, 0)
        XCTAssertTrue(controller.handleKey(try key(redo: true)))
        XCTAssertEqual(model.preampDb, -3)
        model.stopPolling()
    }
}

@MainActor
private final class UndoTextView: NSTextView {
    let manager = UndoManager()
    override var undoManager: UndoManager? { manager }
}
