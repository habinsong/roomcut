import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The parts every card layout draws from: artwork, the progress row, transport
// buttons, the format readout and the lyrics block.
extension NowPlayingView {
    // MARK: Pieces

    func reservedMetadataLine(
        _ text: String?,
        size: CGFloat,
        color: Color
    ) -> some View {
        Text(text ?? " ")
            .font(.system(size: size))
            .foregroundStyle(color)
            .lineLimit(1)
            .opacity(text == nil ? 0 : 1)
            .frame(height: size + 3)
    }

    func lyricsBlock(width: CGFloat, height: CGFloat) -> some View {
        TwoLineLyricView(
            current: lyricLines.current,
            next: lyricLines.next,
            fontSize: lyricFontSize,
            scheme: scheme,
            forceWhite: darkBackdrop,
            forceBlack: brightBackdrop
        )
        .frame(width: width, height: height, alignment: .top)
        .padding(.top, 2)
    }

    var menuAlbumArt: some View {
        Group {
            if let art = monitor.artwork, live != nil {
                ExtendedArtwork(image: art, blurRadius: 10)
            } else {
                Rectangle()
                    .fill(menuInk.opacity(0.12))
                    .overlay {
                        Image(systemName: "music.note")
                            .font(.system(size: 16))
                            .foregroundStyle(menuInk.opacity(0.45))
                    }
            }
        }
        .frame(width: 46, height: 46)
        .clipped()
        .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
    }

    var menuProgressBar: some View {
        GeometryReader { geo in
            let width = geo.size.width
            ZStack(alignment: .leading) {
                Capsule(style: .continuous)
                    .fill(menuInk.opacity(0.33))
                Capsule(style: .continuous)
                    .fill(menuInk)
                    .frame(width: max(0, width * menuFraction))
            }
            .contentShape(Rectangle())
            .gesture(menuSeekable ? menuSeekGesture(width: width) : nil)
        }
        .frame(height: 5)
        .accessibilityElement()
        .accessibilityLabel("재생 위치")
        .accessibilityValue("\(Int(menuFraction * 100)) 퍼센트")
    }

    func menuSeekGesture(width: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                scrubFraction = max(0, min(1, value.location.x / max(1, width)))
            }
            .onEnded { value in
                let fraction = max(0, min(1, value.location.x / max(1, width)))
                monitor.seek(toSeconds: fraction * menuDuration)
                scrubFraction = nil
            }
    }

    @ViewBuilder
    func audioFormatText(alignment: Alignment = .center) -> some View {
        if let audioFormat {
            Text(RoomcutMainPresentation.audioFormatLabel(
                bitDepth: audioFormat.bitDepth,
                sampleRate: audioFormat.sampleRate,
                latencyMs: audioFormat.latencyMs))
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                // Bit / kHz / ms: bumped from 0.5 → 0.72 so the format line reads
                // clearly instead of fading into the backdrop.
                .foregroundStyle(inkPrimary.opacity(0.72))
                .lineLimit(1)
                .frame(maxWidth: .infinity, alignment: alignment)
        }
    }

    @ViewBuilder
    func artwork(size: CGFloat) -> some View {
        let radius: CGFloat = size > 80 ? 16 : 9
        let shape = RoundedRectangle(cornerRadius: radius, style: .continuous)

        Group {
            if let img = (live != nil ? monitor.artwork : nil) {
                // Real album art from the live source.
                ExtendedArtwork(image: img)
            } else {
                // Deterministic colours from the sample title (fixtures only); the
                // production fallback uses a calm neutral wash — never fake art.
                let colors: [Color] = {
                    if case .fixture(let t, _, _, _) = display {
                        let h = Double(abs(t.hashValue) % 360) / 360.0
                        return [
                            Color(hue: h, saturation: 0.62, brightness: scheme == .dark ? 0.72 : 0.86),
                            Color(hue: (h + 0.12).truncatingRemainder(dividingBy: 1),
                                  saturation: 0.55, brightness: scheme == .dark ? 0.42 : 0.7),
                        ]
                    }
                    return scheme == .dark
                        ? [Color(hex: 0x2A2D33), Color(hex: 0x17191D)]
                        : [Color(hex: 0xECECE8), Color(hex: 0xDADAD4)]
                }()
                LinearGradient(colors: colors, startPoint: .topLeading, endPoint: .bottomTrailing)
                    .overlay {
                        Image(systemName: "waveform")
                            .font(.system(size: size * 0.28, weight: .light))
                            .foregroundStyle(.white.opacity(0.85))
                            .shadow(color: .black.opacity(0.2), radius: 3)
                    }
            }
        }
        .frame(width: size, height: size)
        .clipShape(shape)
        .overlay(shape.strokeBorder(.white.opacity(scheme == .dark ? 0.14 : 0.5), lineWidth: 0.5))
        .shadow(color: .black.opacity(scheme == .dark ? 0.45 : 0.18),
                radius: size > 80 ? 14 : 6, y: size > 80 ? 8 : 3)
        .accessibilityHidden(true)
    }

    var progressRow: some View {
        HStack(alignment: .center, spacing: 6) {
            Text(progressElapsedClock)
                .frame(width: 42, alignment: .leading)
                .lineLimit(1)
                .foregroundStyle(fullInk.opacity(0.5))
            progressBar
            Text(progressRemainingClock)
                .frame(width: 48, alignment: .trailing)
                .lineLimit(1)
                .foregroundStyle(fullInk.opacity(0.5))
        }
        .font(.system(size: 11).monospacedDigit())
    }

    var progressElapsedClock: String {
        guard let t = times else { return "0:00" }
        return Self.clock(progress * t.total)
    }

    var progressRemainingClock: String {
        guard let t = times else { return "-0:00" }
        return "-" + Self.clock(max(0, t.total - progress * t.total))
    }

    var progressBar: some View {
        GeometryReader { geo in
            let width = geo.size.width
            ZStack(alignment: .leading) {
                Capsule(style: .continuous)
                    .fill(fullInk.opacity(0.33))
                Capsule(style: .continuous)
                    .fill(fullInk)
                    .frame(width: max(0, width * progress))
            }
            .contentShape(Rectangle())
            .gesture(seekable ? seekGesture(width: width) : nil)
        }
        .frame(height: 5)
        .accessibilityElement()
        .accessibilityLabel("재생 위치")
        .accessibilityValue(times.map { "\(Self.clock($0.elapsed)) / \(Self.clock($0.total))" } ?? "")
        .accessibilityAdjustableAction { direction in
            guard seekable, let t = times else { return }
            let step = 5.0
            let next = direction == .increment ? t.elapsed + step : t.elapsed - step
            monitor.seek(toSeconds: max(0, min(t.total, next)))
        }
    }

    // Drag anywhere on the bar scrubs; release commits the seek. A zero-distance
    // drag (a tap) works too — it jumps to the tapped position.
    func seekGesture(width: CGFloat) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                scrubFraction = max(0, min(1, value.location.x / max(1, width)))
            }
            .onEnded { value in
                let f = max(0, min(1, value.location.x / max(1, width)))
                if let total = times?.total {
                    monitor.seek(toSeconds: f * total)
                }
                scrubFraction = nil
            }
    }

    func transportControls(spacing: CGFloat, playSize: CGFloat, sideSize: CGFloat) -> some View {
        HStack(spacing: spacing) {
            transportButton("backward.fill", size: sideSize) {
                if live != nil { monitor.command(.previous) }
            }
            .matchedGeometryEffect(id: "np-prev", in: morphNS)
            // Play/Pause: bare glyph; the circular backing appears only while pressed.
            Button {
                if live != nil { monitor.command(.togglePlayPause) }
            } label: {
                Image(systemName: isPlaying ? "pause.fill" : "play.fill")
                    .font(.system(size: playSize * 0.5, weight: .medium))
                    .foregroundStyle(inkPrimary)
                    .offset(x: isPlaying ? 0 : 1)
                    .frame(width: playSize, height: playSize)
            }
            .buttonStyle(PressCircleButtonStyle(diameter: playSize, scheme: scheme))
            .matchedGeometryEffect(id: "np-play", in: morphNS)
            transportButton("forward.fill", size: sideSize) {
                if live != nil { monitor.command(.next) }
            }
            .matchedGeometryEffect(id: "np-next", in: morphNS)
        }
        .disabled(!controlsEnabled)
        .opacity(controlsEnabled ? 1 : 0.4)
        .accessibilityHidden(true)
    }

    func transportButton(_ symbol: String, size: CGFloat, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: size, weight: .medium))
                // Prev/Next: full ink (solid black over a light backdrop, white over
                // a dark one) so they're as strong as the play glyph, not the faded
                // secondary grey they used to be.
                .foregroundStyle(fullInk)
        }
        .buttonStyle(.plain)
    }

    static func clock(_ seconds: Double) -> String {
        let s = Int(seconds.rounded())
        return String(format: "%d:%02d", s / 60, s % 60)
    }
}
