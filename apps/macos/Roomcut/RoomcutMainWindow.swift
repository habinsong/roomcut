import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The app's NSWindow subclass. SwiftUI cannot reach window-level behaviour
// (activation policy, custom chrome, drag regions), so this is where AppKit
// still has to do the work.
final class RoomcutMainWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
    var allowsTransientResize = false
    var usesCompactSizing = false
    private var isLiveResizing = false
    private let softWindowTiming = CAMediaTimingFunction(controlPoints: 0.22, 0.92, 0.26, 1)
    private let resizeEdgeBand: CGFloat = 8
    private let resizeCornerBand: CGFloat = 28

    // While the user is dragging an edge/corner, don't let AppKit clamp the window
    // to the visible screen. The layout is tall (≈2.17:1), so on a laptop display
    // the screen height is reached well before the app's own maxWidth — the screen
    // clamp froze the drag there. The only limit during resize is the app's
    // RoomcutWindowMetrics min/max (enforced in trackResize via clampedWidth).
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect {
        isLiveResizing ? frameRect : super.constrainFrameRect(frameRect, to: screen)
    }

    override func sendEvent(_ event: NSEvent) {
        if event.type == .leftMouseDown,
           let edge = resizeEdge(at: event.locationInWindow) {
            trackResize(edge: edge)
            return
        }
        super.sendEvent(event)
    }

    func collapseToNowPlaying() {
        guard isVisible else { return }
        let start = frame
        let height = RoomcutWindowMetrics.compactHeight(forWidth: start.width)
        let end = frameKeepingTop(of: start, height: height)
        allowsTransientResize = true
        applyWindowSizing(compact: true)
        hasShadow = false
        invalidateShadow()
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.42
            context.timingFunction = softWindowTiming
            animator().setFrame(end, display: true)
        }, completionHandler: { [weak self] in
            self?.allowsTransientResize = false
        })
    }

    func expandFromNowPlaying() {
        let start = frame
        let height = RoomcutWindowMetrics.height(forWidth: start.width)
        let target = frameKeepingTop(of: start, height: height)
        allowsTransientResize = true
        applyWindowSizing(compact: false)
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.36
            context.timingFunction = softWindowTiming
            animator().setFrame(target, display: true)
        }, completionHandler: { [weak self] in
            self?.restoreStandardSizing()
        })
    }

    func rollUpAndHide(completion: (() -> Void)? = nil) {
        guard isVisible else { return }
        let start = frame
        let restoreHeight = RoomcutWindowMetrics.height(forWidth: start.width)
        let restore = frameKeepingTop(of: start, height: restoreHeight)
        let end = start.insetBy(dx: start.width * 0.035, dy: start.height * 0.10)
            .offsetBy(dx: 0, dy: 8)
        let restoreShadow = hasShadow
        allowsTransientResize = true
        hasShadow = false
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.28
            context.timingFunction = softWindowTiming
            animator().setFrame(end, display: true)
            animator().alphaValue = 0
        }, completionHandler: { [weak self] in
            guard let self else { return }
            self.orderOut(nil)
            self.setFrame(restore, display: false)
            self.alphaValue = 1
            self.hasShadow = restoreShadow || start.height < restoreHeight
            self.invalidateShadow()
            self.restoreStandardSizing()
            completion?()
        })
    }

    func setAlwaysOnTop(_ enabled: Bool) {
        level = enabled ? .floating : .normal
        if enabled {
            collectionBehavior.insert([.canJoinAllSpaces, .fullScreenAuxiliary])
        } else {
            collectionBehavior.remove([.canJoinAllSpaces, .fullScreenAuxiliary])
        }
    }

    private func frameKeepingTop(of frame: NSRect, height: CGFloat) -> NSRect {
        NSRect(
            x: frame.minX,
            y: frame.maxY - height,
            width: frame.width,
            height: height
        )
    }

    private func restoreStandardSizing() {
        allowsTransientResize = false
        hasShadow = true
        invalidateShadow()
        applyWindowSizing(compact: false)
    }

    func applyWindowSizing(compact: Bool) {
        usesCompactSizing = compact
        contentAspectRatio = NSSize(
            width: RoomcutWindowMetrics.baseWidth,
            height: compact
                ? RoomcutWindowMetrics.compactBaseHeight
                : RoomcutWindowMetrics.baseHeight
        )
        let minWidth = RoomcutWindowMetrics.minWidth
        let maxWidth = RoomcutWindowMetrics.maxWidth
        let minHeight = compact
            ? RoomcutWindowMetrics.compactHeight(forWidth: minWidth)
            : RoomcutWindowMetrics.height(forWidth: minWidth)
        let maxHeight = compact
            ? RoomcutWindowMetrics.compactHeight(forWidth: maxWidth)
            : RoomcutWindowMetrics.height(forWidth: maxWidth)
        contentMinSize = NSSize(
            width: minWidth,
            height: minHeight
        )
        contentMaxSize = NSSize(
            width: maxWidth,
            height: maxHeight
        )
        minSize = contentMinSize
        maxSize = contentMaxSize
    }

    private func resizeEdge(at point: NSPoint) -> WindowResizeEdge? {
        let windowBounds = NSRect(origin: .zero, size: frame.size)
        guard windowBounds.contains(point) else { return nil }

        let leftCorner = point.x <= resizeCornerBand
        let rightCorner = point.x >= windowBounds.width - resizeCornerBand
        let topCorner = point.y >= windowBounds.height - resizeCornerBand
        let bottomCorner = point.y <= resizeCornerBand

        if leftCorner && topCorner { return .topLeading }
        if rightCorner && topCorner { return .topTrailing }
        if leftCorner && bottomCorner { return .bottomLeading }
        if rightCorner && bottomCorner { return .bottomTrailing }
        if point.y >= windowBounds.height - resizeEdgeBand { return .top }
        if point.y <= resizeEdgeBand { return .bottom }
        if point.x <= resizeEdgeBand { return .leading }
        if point.x >= windowBounds.width - resizeEdgeBand { return .trailing }
        return nil
    }

    private func trackResize(edge: WindowResizeEdge) {
        let startFrame = frame
        let startMouse = NSEvent.mouseLocation
        NSCursor.resizeUpDown.push()
        isLiveResizing = true
        defer { isLiveResizing = false; NSCursor.pop() }

        while true {
            guard let event = nextEvent(matching: [.leftMouseDragged, .leftMouseUp]) else { break }
            guard event.type != .leftMouseUp else { break }
            let mouse = NSEvent.mouseLocation
            let width = proposedResizeWidth(
                edge: edge,
                startFrame: startFrame,
                dx: mouse.x - startMouse.x,
                dy: mouse.y - startMouse.y
            )
            let height = resizeHeight(forWidth: width)
            var nextFrame = startFrame
            nextFrame.size = NSSize(width: width, height: height)
            nextFrame.origin.x = resizeOriginX(edge: edge, startFrame: startFrame, width: width)
            nextFrame.origin.y = resizeOriginY(edge: edge, startFrame: startFrame, height: height)
            setFrame(nextFrame, display: true)
        }
    }

    private func proposedResizeWidth(
        edge: WindowResizeEdge,
        startFrame: NSRect,
        dx: CGFloat,
        dy: CGFloat
    ) -> CGFloat {
        var candidates: [CGFloat] = []
        if edge.usesTrailingDelta {
            candidates.append(startFrame.width + dx)
        }
        if edge.usesLeadingDelta {
            candidates.append(startFrame.width - dx)
        }
        if edge.usesTopDelta {
            candidates.append(resizeWidth(forHeight: startFrame.height + dy))
        }
        if edge.usesBottomDelta {
            candidates.append(resizeWidth(forHeight: startFrame.height - dy))
        }

        let proposed = candidates.max {
            abs($0 - startFrame.width) < abs($1 - startFrame.width)
        } ?? startFrame.width
        return CGFloat(RoomcutWindowMetrics.clampedWidth(Double(proposed)))
    }

    private func resizeHeight(forWidth width: CGFloat) -> CGFloat {
        let height = usesCompactSizing
            ? RoomcutWindowMetrics.compactHeight(forWidth: Double(width))
            : RoomcutWindowMetrics.height(forWidth: Double(width))
        return CGFloat(height)
    }

    private func resizeWidth(forHeight height: CGFloat) -> CGFloat {
        let baseHeight = usesCompactSizing
            ? RoomcutWindowMetrics.compactBaseHeight
            : RoomcutWindowMetrics.baseHeight
        let ratio = baseHeight / RoomcutWindowMetrics.baseWidth
        return CGFloat(RoomcutWindowMetrics.clampedWidth(Double(height) / ratio))
    }

    private func resizeOriginX(edge: WindowResizeEdge, startFrame: NSRect, width: CGFloat) -> CGFloat {
        if edge.usesLeadingDelta { return startFrame.maxX - width }
        if edge.usesTrailingDelta { return startFrame.minX }
        return startFrame.midX - width / 2
    }

    private func resizeOriginY(edge: WindowResizeEdge, startFrame: NSRect, height: CGFloat) -> CGFloat {
        if edge.usesTopDelta { return startFrame.minY }
        return startFrame.maxY - height
    }
}
