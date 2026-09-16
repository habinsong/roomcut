import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The chrome around the canvas: the draggable handle bar at the top, the
// always-on-top pin, the volume ring and the compact preset badge.
struct WindowHandleBar: View {
    @Binding var keepsWindowOnTop: Bool
    let presetName: String
    let showPreset: Bool
    // Hover capsule behind the "——"/toggle cluster (hidden until hover), sized and
    // shifted so it wraps the cluster: full bar in compact, a snug pill in expanded.
    // capsuleHeight < barHeight leaves an equal top/bottom margin (separation).
    let capsuleWidth: CGFloat
    let capsuleHeight: CGFloat
    let capsuleOffsetX: CGFloat
    let barWidth: CGFloat?       // nil = full width (expanded); fixed in compact
    let barHeight: CGFloat
    let onClick: (() -> Void)?
    let onDoubleClick: () -> Void
    // Roomcut output volume (0…1) for the little ring right of the toggle. nil =
    // no ring (expanded mode); the value drives a clockwise battery-style fill.
    var volume: Double? = nil

    // The toggle sits this far right of the centred "——"; the drag view punches a
    // hit-hole of this half-width there so the toggle's clicks get through.
    private let toggleOffsetX: CGFloat = 36
    private let toggleHoleHalfWidth: CGFloat = 18

    @Environment(\.colorScheme) private var scheme
    @State private var centerHovering = false   // pointer over the AppKit grab area
    @State private var sideHovering = false     // pointer over the rest of the bar
    @State private var pressed = false

    private var visible: Bool { centerHovering || sideHovering || pressed }

    var body: some View {
        let bar = ZStack {
            // Real Liquid Glass hover capsule, rendered in AppKit (NSGlassEffectView)
            // so it never swallows the window-drag / double-click — SwiftUI's
            // `.glassEffect` does, even with allowsHitTesting(false). GlassCapsule's
            // hitTest returns nil, so the drag view below stays fully interactive.
            GlassCapsule(visible: visible)
                .frame(width: capsuleWidth, height: capsuleHeight)
                .offset(x: capsuleOffsetX)

            // Full-bar grab area (AppKit): drag the whole top strip + double/
            // single click. It declines hits only in the toggle's band so the
            // overlaid SwiftUI toggle stays clickable.
            WindowCloseButton(
                onClick: onClick,
                onDoubleClick: onDoubleClick,
                onPressChanged: { pressed = $0 },
                onHoverChanged: { centerHovering = $0 },
                hitHoleCenterOffsetX: toggleOffsetX,
                hitHoleHalfWidth: visible ? toggleHoleHalfWidth : 0
            )
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Text("——")
                .font(.system(size: 18, weight: .semibold))
                .foregroundStyle(RoomcutTokens.textTertiary(scheme))
                .allowsHitTesting(false)

            if visible && showPreset {
                CompactPresetBadge(name: presetName, scheme: scheme)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.leading, 8)
                    .transition(.opacity.combined(with: .scale(scale: 0.92)))
            }

            if visible {
                // Just right of the centred "——", overlaid on the drag bar (which
                // opens a hit-hole here so this toggle receives the click).
                AlwaysOnTopToggle(isOn: $keepsWindowOnTop, scheme: scheme)
                    .offset(x: toggleOffsetX)
                    .transition(.opacity.combined(with: .scale(scale: 0.92)))

                // Roomcut volume, as a tiny battery-style ring just right of the
                // toggle (compact only — `volume` is nil in expanded mode).
                if let volume {
                    VolumeRing(value: volume, scheme: scheme)
                        .offset(x: toggleOffsetX + 21)
                        .transition(.opacity.combined(with: .scale(scale: 0.92)))
                }
            }
        }
        .contentShape(Rectangle())
        .onHover { sideHovering = $0 }
        .animation(.easeOut(duration: 0.12), value: visible)

        if let barWidth {
            bar.frame(width: barWidth, height: barHeight)
        } else {
            bar.frame(maxWidth: .infinity).frame(height: barHeight)
        }
    }
}

struct AlwaysOnTopToggle: View {
    @Binding var isOn: Bool
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        Button {
            withAnimation(.easeOut(duration: 0.14)) {
                isOn.toggle()
            }
        } label: {
            ZStack(alignment: isOn ? .trailing : .leading) {
                Capsule(style: .continuous)
                    .fill(ink.opacity(isOn ? 0.24 : 0.08))
                Circle()
                    .fill(ink.opacity(isOn ? 0.82 : 0.42))
                    .frame(width: 8, height: 8)
                    .padding(.horizontal, 3)
            }
            .frame(width: 24, height: 14)
            .contentShape(Capsule(style: .continuous))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("항상 위에 표시")
        .accessibilityValue(isOn ? "켬" : "끔")
    }
}

// Tiny battery-style ring that fills clockwise from 12 o'clock by the volume
// fraction. Display only (no hit testing) — sits in the sliver right of the toggle.
struct VolumeRing: View {
    let value: Double
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        ZStack {
            Circle().stroke(ink.opacity(0.22), lineWidth: 1.6)
            Circle()
                .trim(from: 0, to: max(0, min(1, value)))
                .stroke(ink.opacity(0.72), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
                .rotationEffect(.degrees(-90))
        }
        .frame(width: 11, height: 11)
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

struct CompactPresetBadge: View {
    let name: String
    let scheme: ColorScheme

    private var ink: Color { scheme == .dark ? .white : .black }

    var body: some View {
        Text(name)
            .font(.system(size: 9, weight: .semibold))
            .foregroundStyle(ink.opacity(0.48))
            .lineLimit(1)
            .minimumScaleFactor(0.55)
            .frame(width: 52, alignment: .leading)
            .allowsHitTesting(false)
            .accessibilityHidden(true)
    }
}
