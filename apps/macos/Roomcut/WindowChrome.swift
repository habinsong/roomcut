//
// WindowChrome.swift — a draggable strip for the borderless main window.
//
// The custom RoomcutMainWindow has isMovableByWindowBackground = false (so knob
// / slider / scrub drags don't move the whole window). This handle gives the
// user one explicit place to drag the window: mouseDown forwards to the window's
// performDrag, which moves it.
//
import SwiftUI
import AppKit
import RoomcutPresentationCore

struct WindowCloseButton: NSViewRepresentable {
    var onClick: (() -> Void)?
    var onDoubleClick: (() -> Void)?
    var onPressChanged: ((Bool) -> Void)?
    var onHoverChanged: ((Bool) -> Void)?
    // Click "hole": a vertical band (centred at midX + offset, ± halfWidth) where
    // this full-bar drag view declines hits, so a SwiftUI control overlaid there
    // (the always-on-top toggle) receives its own clicks. halfWidth 0 = no hole.
    var hitHoleCenterOffsetX: CGFloat = 0
    var hitHoleHalfWidth: CGFloat = 0

    func makeNSView(context: Context) -> NSView {
        let view = WindowChromeView()
        apply(to: view)
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        guard let view = nsView as? WindowChromeView else { return }
        apply(to: view)
    }

    private func apply(to view: WindowChromeView) {
        view.onClick = onClick
        view.onDoubleClick = onDoubleClick
        view.onPressChanged = onPressChanged
        view.onHoverChanged = onHoverChanged
        view.hitHoleCenterOffsetX = hitHoleCenterOffsetX
        view.hitHoleHalfWidth = hitHoleHalfWidth
    }
}

private final class WindowChromeView: NSView {
    var onClick: (() -> Void)?
    var onDoubleClick: (() -> Void)?
    var onPressChanged: ((Bool) -> Void)?
    var onHoverChanged: ((Bool) -> Void)?

    var hitHoleCenterOffsetX: CGFloat = 0
    var hitHoleHalfWidth: CGFloat = 0

    private var startFrame: NSRect = .zero
    private var startMouse: NSPoint = .zero
    private var didDrag = false
    private var handledDoubleClick = false
    private var trackingAreaRef: NSTrackingArea?

    override var intrinsicContentSize: NSSize {
        NSSize(width: NSView.noIntrinsicMetric, height: NSView.noIntrinsicMetric)
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    override var mouseDownCanMoveWindow: Bool { false }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    override func hitTest(_ point: NSPoint) -> NSView? {
        if isHidden || alphaValue <= 0 || !bounds.contains(point) { return nil }
        // Decline hits inside the toggle's band so SwiftUI handles them; the rest
        // of the bar still drags/collapses as before.
        if hitHoleHalfWidth > 0 {
            let holeCenterX = bounds.midX + hitHoleCenterOffsetX
            if abs(point.x - holeCenterX) <= hitHoleHalfWidth { return nil }
        }
        return self
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingAreaRef {
            removeTrackingArea(trackingAreaRef)
        }
        let area = NSTrackingArea(
            rect: .zero,
            options: [.mouseEnteredAndExited, .activeAlways, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        trackingAreaRef = area
    }

    override func mouseEntered(with event: NSEvent) {
        onHoverChanged?(true)
    }

    override func mouseExited(with event: NSEvent) {
        onHoverChanged?(false)
    }

    override func mouseDown(with event: NSEvent) {
        didDrag = false
        handledDoubleClick = false
        onPressChanged?(true)
        if event.clickCount == 2 {
            handledDoubleClick = true
            if let onDoubleClick {
                onDoubleClick()
            } else {
                (window as? RoomcutMainWindow)?.rollUpAndHide()
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { [weak self] in
                self?.onPressChanged?(false)
            }
            return
        }
        startFrame = window?.frame ?? .zero
        startMouse = NSEvent.mouseLocation
    }

    override func mouseDragged(with event: NSEvent) {
        guard let window else { return }
        didDrag = true
        onPressChanged?(true)
        let current = NSEvent.mouseLocation
        window.setFrameOrigin(NSPoint(
            x: startFrame.minX + current.x - startMouse.x,
            y: startFrame.minY + current.y - startMouse.y))
    }

    override func mouseUp(with event: NSEvent) {
        onPressChanged?(false)
        guard !handledDoubleClick, !didDrag else { return }
        onClick?()
    }
}

// Real Liquid Glass (AppKit), but deliberately transparent to the mouse: SwiftUI
// `.glassEffect` swallows clicks/drags in its area even with
// `allowsHitTesting(false)` (Liquid Glass ignores SwiftUI hit-testing), which
// broke window-drag / double-click through the hover capsule. Rendering the same
// material with NSGlassEffectView and returning nil from hitTest keeps the drag
// view beneath fully interactive while the glass still shows.
struct GlassCapsule: NSViewRepresentable {
    var visible: Bool

    func makeNSView(context: Context) -> GlassCapsuleHostView {
        let v = GlassCapsuleHostView()
        v.setVisible(visible)
        return v
    }

    // Show/hide INSTANTLY (no alpha animation). Animating a Liquid Glass view's
    // opacity — via SwiftUI `.opacity` or AppKit `animator().alphaValue` — hits an
    // OS bug (FB20216507) that flashes black pixels along the rounded edges. A
    // hard cut has no such artifact; the toggle/badge beside it still fade.
    func updateNSView(_ nsView: GlassCapsuleHostView, context: Context) {
        nsView.setVisible(visible)
    }
}

final class GlassCapsuleHostView: NSView {
    private let glassView = NonInteractiveGlassView()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setup()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        setup()
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    func setVisible(_ visible: Bool) {
        isHidden = !visible
        alphaValue = visible ? 1 : 0
        if visible {
            glassView.needsLayout = true
            glassView.needsDisplay = true
        }
    }

    override func layout() {
        super.layout()
        let bleed: CGFloat = 2
        glassView.frame = bounds.insetBy(dx: -bleed, dy: -bleed)
        glassView.cornerRadius = glassView.bounds.height / 2
        layer?.cornerRadius = bounds.height / 2
        layer?.cornerCurve = .continuous
    }

    private func setup() {
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        layer?.masksToBounds = true
        layer?.shadowOpacity = 0
        glassView.autoresizingMask = []
        addSubview(glassView)
    }
}

private final class NonInteractiveGlassView: NSGlassEffectView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        style = .regular
        shadow = nil
    }
    required init?(coder: NSCoder) {
        super.init(coder: coder)
        style = .regular
        shadow = nil
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func layout() {
        super.layout()
        cornerRadius = bounds.height / 2   // capsule
        // Kill the drop shadow — on the transparent window it shows as a black
        // halo above and beside the pill.
        layer?.shadowOpacity = 0
        layer?.shadowRadius = 0
    }
}

struct WindowDragHandle: NSViewRepresentable {
    func makeNSView(context: Context) -> NSView { DragView() }
    func updateNSView(_ nsView: NSView, context: Context) {}
}

private final class DragView: NSView {
    private var startFrame: NSRect = .zero
    private var startMouse: NSPoint = .zero

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent) {
        startFrame = window?.frame ?? .zero
        startMouse = NSEvent.mouseLocation
    }

    override func mouseDragged(with event: NSEvent) {
        guard let window else { return }
        let current = NSEvent.mouseLocation
        window.setFrameOrigin(NSPoint(
            x: startFrame.minX + current.x - startMouse.x,
            y: startFrame.minY + current.y - startMouse.y))
    }

    override var mouseDownCanMoveWindow: Bool { false }
}

enum WindowResizeEdge {
    case top
    case bottom
    case leading
    case trailing
    case topLeading
    case topTrailing
    case bottomLeading
    case bottomTrailing

    var usesLeadingDelta: Bool {
        self == .leading || self == .topLeading || self == .bottomLeading
    }

    var usesTrailingDelta: Bool {
        self == .trailing || self == .topTrailing || self == .bottomTrailing
    }

    var usesTopDelta: Bool {
        self == .top || self == .topLeading || self == .topTrailing
    }

    var usesBottomDelta: Bool {
        self == .bottom || self == .bottomLeading || self == .bottomTrailing
    }
}

private enum WindowResizeAspect {
    case full
    case compact

    var ratio: CGFloat {
        let height = self == .compact
            ? RoomcutWindowMetrics.compactBaseHeight
            : RoomcutWindowMetrics.baseHeight
        return CGFloat(height / RoomcutWindowMetrics.baseWidth)
    }

    func height(forWidth width: CGFloat) -> CGFloat {
        let height = self == .compact
            ? RoomcutWindowMetrics.compactHeight(forWidth: Double(width))
            : RoomcutWindowMetrics.height(forWidth: Double(width))
        return CGFloat(height)
    }

    func width(forHeight height: CGFloat) -> CGFloat {
        CGFloat(RoomcutWindowMetrics.clampedWidth(Double(height / ratio)))
    }
}

struct CornerResizeHandle: NSViewRepresentable {
    var edge: WindowResizeEdge = .bottomTrailing
    var compact: Bool = false

    func makeNSView(context: Context) -> NSView {
        ResizeView(edge: edge, aspect: compact ? .compact : .full)
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        guard let view = nsView as? ResizeView else { return }
        view.edge = edge
        view.aspect = compact ? .compact : .full
    }
}

private final class ResizeView: NSView {
    var edge: WindowResizeEdge
    var aspect: WindowResizeAspect
    private var startFrame: NSRect = .zero
    private var startMouse: NSPoint = .zero
    private var hasPushedResizeCursor = false

    init(edge: WindowResizeEdge, aspect: WindowResizeAspect) {
        self.edge = edge
        self.aspect = aspect
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    required init?(coder: NSCoder) {
        edge = .bottomTrailing
        aspect = .full
        super.init(coder: coder)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func mouseDown(with event: NSEvent) {
        startFrame = window?.frame ?? .zero
        startMouse = NSEvent.mouseLocation
        pushResizeCursor()
    }

    override func mouseUp(with event: NSEvent) {
        popResizeCursor()
    }

    override func mouseDragged(with event: NSEvent) {
        guard let w = window else {
            popResizeCursor()
            return
        }
        let mouse = NSEvent.mouseLocation
        let dx = mouse.x - startMouse.x
        let dy = mouse.y - startMouse.y
        let newW = proposedWidth(dx: dx, dy: dy)
        let newH = aspect.height(forWidth: newW)
        var f = startFrame
        f.size = NSSize(width: newW, height: newH)
        f.origin.x = originX(forWidth: newW)
        f.origin.y = originY(forHeight: newH)
        w.setFrame(f, display: true)
    }
    override var mouseDownCanMoveWindow: Bool { false }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if window == nil {
            popResizeCursor()
        }
    }

    deinit {
        popResizeCursor()
    }

    private func pushResizeCursor() {
        guard !hasPushedResizeCursor else { return }
        NSCursor.resizeUpDown.push()
        hasPushedResizeCursor = true
    }

    private func popResizeCursor() {
        guard hasPushedResizeCursor else { return }
        NSCursor.pop()
        hasPushedResizeCursor = false
    }

    private func proposedWidth(dx: CGFloat, dy: CGFloat) -> CGFloat {
        var candidates: [CGFloat] = []
        if edge.usesTrailingDelta {
            candidates.append(startFrame.width + dx)
        }
        if edge.usesLeadingDelta {
            candidates.append(startFrame.width - dx)
        }
        if edge.usesTopDelta {
            candidates.append(aspect.width(forHeight: startFrame.height + dy))
        }
        if edge.usesBottomDelta {
            candidates.append(aspect.width(forHeight: startFrame.height - dy))
        }

        let proposed = candidates.max {
            abs($0 - startFrame.width) < abs($1 - startFrame.width)
        } ?? startFrame.width
        return CGFloat(RoomcutWindowMetrics.clampedWidth(Double(proposed)))
    }

    private func originX(forWidth width: CGFloat) -> CGFloat {
        if edge.usesLeadingDelta { return startFrame.maxX - width }
        if edge.usesTrailingDelta { return startFrame.minX }
        return startFrame.midX - width / 2
    }

    private func originY(forHeight height: CGFloat) -> CGFloat {
        if edge.usesTopDelta { return startFrame.minY }
        return startFrame.maxY - height
    }
}
