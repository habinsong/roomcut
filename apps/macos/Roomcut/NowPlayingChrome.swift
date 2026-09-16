import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// Small shared pieces of the Now Playing card: artwork that bleeds past its
// frame, the window's own resize handles, a title that scrolls when it does not
// fit, and the transport button style.
struct ExtendedArtwork: View {
    let image: NSImage
    var blurRadius: CGFloat = 22

    private var isNearSquare: Bool {
        let s = image.size
        guard s.width > 0, s.height > 0 else { return true }
        return abs(s.width / s.height - 1) < 0.05
    }

    var body: some View {
        if isNearSquare {
            Image(nsImage: image)
                .resizable()
                .aspectRatio(contentMode: .fill)
        } else {
            // Color.clear owns the LAYOUT: it adopts the caller's frame exactly,
            // and both image copies ride overlays, which never inflate the host.
            // Putting the overflowing .fill copy directly in a ZStack grew the
            // stack to the image's covering size (e.g. 16:9), and the stack then
            // re-proposed THAT size to the .fit copy — which therefore filled it
            // edge-to-edge and rendered as a centre crop (the "still cropped"
            // bug, reproduced in an offscreen render harness).
            Color.clear
                .overlay(
                    // Backdrop: fill copy, scaled past the edges so the blur
                    // never samples outside the bitmap (washed-out rims).
                    Image(nsImage: image)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                        .scaleEffect(1.3)
                        .blur(radius: blurRadius)
                        .saturation(1.1)
                )
                .overlay(
                    Image(nsImage: image)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                )
                .clipped()
        }
    }
}

struct MenuLikeResizeHandles: View {
    private let edge: CGFloat = 7
    private let corner: CGFloat = 22

    var body: some View {
        ZStack {
            CornerResizeHandle(edge: .top, compact: true)
                .frame(height: edge)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            CornerResizeHandle(edge: .bottom, compact: true)
                .frame(height: edge)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
            CornerResizeHandle(edge: .leading, compact: true)
                .frame(width: edge)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
            CornerResizeHandle(edge: .trailing, compact: true)
                .frame(width: edge)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .trailing)

            CornerResizeHandle(edge: .topLeading, compact: true)
                .frame(width: corner, height: corner)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            CornerResizeHandle(edge: .topTrailing, compact: true)
                .frame(width: corner, height: corner)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
            CornerResizeHandle(edge: .bottomLeading, compact: true)
                .frame(width: corner, height: corner)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
            CornerResizeHandle(edge: .bottomTrailing, compact: true)
                .frame(width: corner, height: corner)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomTrailing)
        }
    }
}

// A single-line label that scrolls horizontally (ping-pong) when its text is wider
// than the available width, so a long "artist + album" stays fully readable without
// truncation. Static (no animation) when it fits. Recreate via `.id(text)` on track
// change so it restarts cleanly from the left.
struct MarqueeLine: View {
    let text: AttributedString
    var speed: CGFloat = 10      // pt/sec — gentle

    @State private var textWidth: CGFloat = 0
    @State private var containerWidth: CGFloat = 0
    @State private var offsetX: CGFloat = 0
    @State private var animating = false

    private let gap: CGFloat = 48

    var body: some View {
        GeometryReader { geo in
            let overflow = textWidth > geo.size.width + 1
            Group {
                if overflow {
                    // Two copies a gap apart, scrolling left forever. When the first
                    // has fully passed, the second is exactly where the first started,
                    // so the wrap is seamless — it never bounces back.
                    HStack(spacing: gap) {
                        Text(text).fixedSize()
                        Text(text).fixedSize()
                    }
                    .offset(x: offsetX)
                } else {
                    Text(text).fixedSize()
                }
            }
            .frame(width: geo.size.width, alignment: .leading)
            .clipped()
            .background {
                Text(text).fixedSize().hidden()
                    .background {
                        GeometryReader { t in
                            Color.clear.preference(key: MarqueeWidthKey.self, value: t.size.width)
                        }
                    }
            }
            .onPreferenceChange(MarqueeWidthKey.self) { w in
                textWidth = w
                containerWidth = geo.size.width
                startLoopIfNeeded()
            }
            .onAppear { containerWidth = geo.size.width; startLoopIfNeeded() }
        }
    }

    // Start at offset 0 (the head of the text is visible), hold 1.2s so it can be
    // read, THEN scroll left forever. Arm exactly once — preference changes fire
    // repeatedly, and re-arming was what reset the head off-screen.
    private func startLoopIfNeeded() {
        guard !animating, containerWidth > 0, textWidth > containerWidth + 1 else { return }
        animating = true
        offsetX = 0
        let travel = textWidth + gap
        withAnimation(.linear(duration: Double(travel / speed)).delay(1.2).repeatForever(autoreverses: false)) {
            offsetX = -travel
        }
    }
}

struct MarqueeWidthKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}


// Play/pause button: bare glyph at rest; a soft circular backing fades in only
// while pressed (no permanent circle).
struct PressCircleButtonStyle: ButtonStyle {
    let diameter: CGFloat
    let scheme: ColorScheme
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background {
                Circle()
                    .fill(scheme == .dark ? Color.white.opacity(0.12) : Color.white.opacity(0.85))
                    .frame(width: diameter, height: diameter)
                    .opacity(configuration.isPressed ? 1 : 0)
            }
            .scaleEffect(configuration.isPressed ? 0.94 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
            .contentShape(Circle())
    }
}
