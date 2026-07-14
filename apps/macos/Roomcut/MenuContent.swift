//
// MenuContent.swift — the menu-bar popover: two compact cards stacked top to
// bottom.
//
// Top: a Now Playing card (live system Now Playing via NowPlayingMonitor —
// artwork, title/artist, a drag-to-seek progress bar, transport). Bottom: an
// engine status card (On/Off, current preset, output device, "Open" the main
// window). Layout follows the design the user supplied, scaled down and with the
// progress bar wired for scrubbing; only the data + actions are bound here.
//
import SwiftUI
import AppKit
import RoomcutCore
import RoomcutPresentationCore

struct MenuContent: View {
    @ObservedObject var model: EngineModel
    @ObservedObject var monitor: NowPlayingMonitor
    @Environment(\.colorScheme) private var scheme
    // Dismiss the panel / open the main window (provided by AppDelegate).
    var onDismiss: () -> Void = {}
    var onOpenMain: () -> Void = {}

    // While dragging the progress bar, show this fraction instead of the live
    // elapsed; commit a real seek on release. nil = not scrubbing.
    @State private var scrubFraction: Double?

    var body: some View {
        nowPlayingCard
            .frame(width: 340)
    }

    // MARK: - Now Playing card (top)

    private var nowPlayingCard: some View {
        VStack(spacing: 0) {
            VStack(spacing: 0) {
                HStack(spacing: 10) {
                    albumArt
                    VStack(alignment: .leading, spacing: 1) {
                        Text(npTitle)
                            .font(.system(size: 13, weight: .semibold))
                            .lineLimit(1)
                        Text(npArtist)
                            .font(.system(size: 11, weight: .regular))
                            .foregroundStyle(ink.opacity(0.5))
                            .lineLimit(1)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .clipped()
                }
                .padding(.top, 14)
                .padding(.horizontal, 14)

                HStack(alignment: .center, spacing: 6) {
                    Text(clock(displayElapsed))
                        .frame(width: 42, alignment: .leading)
                        .lineLimit(1)
                        .foregroundStyle(ink.opacity(0.5))
                    progressBar
                    Text(remainingClock)
                        .frame(width: 48, alignment: .trailing)
                        .lineLimit(1)
                        .foregroundStyle(ink.opacity(0.5))
                }
                .font(.system(size: 11).monospacedDigit())
                .padding(.top, 10)
                .padding(.horizontal, 18)

                HStack(alignment: .bottom, spacing: 6) {
                    Button { NSApp.terminate(nil) } label: {
                        Image(systemName: "xmark")
                            .font(.system(size: 16, weight: .medium))
                            .frame(width: 42, alignment: .leading)
                    }
                    .buttonStyle(.plain)

                    HStack(spacing: 20) {
                        Button { monitor.command(.previous) } label: {
                            Image(systemName: "backward.fill").imageScale(.large)
                        }
                        Button { monitor.command(.togglePlayPause) } label: {
                            Image(systemName: isPlaying ? "pause.fill" : "play.fill")
                                .imageScale(.large)
                                .font(.title)
                        }
                        Button { monitor.command(.next) } label: {
                            Image(systemName: "forward.fill").imageScale(.large)
                        }
                    }
                    .buttonStyle(.plain)
                    .disabled(!hasNowPlaying)
                    .font(.title2)
                    .frame(maxWidth: .infinity)

                    Button(action: openMainWindow) {
                        Image(systemName: "record.circle.fill")
                            .font(.system(size: 16, weight: .medium))
                            .frame(width: 48, alignment: .trailing)
                    }
                    .buttonStyle(.plain)
                }
                .padding(.top, 6)
                .padding(.bottom, 18)
                .padding(.horizontal, 18)
                Spacer(minLength: 0)
            }
            .frame(height: 132)
            .foregroundStyle(ink)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 30, style: .continuous))
        }
        .frame(maxWidth: .infinity, alignment: .top)
        .padding(.vertical, 8)
    }

    // Live album art when available; otherwise a neutral placeholder.
    private var albumArt: some View {
        Group {
            if let art = monitor.artwork, hasNowPlaying {
                ExtendedArtwork(image: art, blurRadius: 10)
            } else {
                Rectangle()
                    .fill(ink.opacity(0.12))
                    .overlay(
                        Image(systemName: "music.note")
                            .font(.system(size: 16))
                            .foregroundStyle(ink.opacity(0.45))
                    )
            }
        }
        .frame(width: 46, height: 46)
        .clipped()
        .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
    }

    // Drag/click anywhere on the bar to seek (live tracks with a known duration).
    private var progressBar: some View {
        GeometryReader { geo in
            let w = geo.size.width
            ZStack(alignment: .leading) {
                Capsule(style: .continuous).fill(ink.opacity(0.33))
                Capsule(style: .continuous).fill(ink)
                    .frame(width: max(0, w * fraction))
            }
            .contentShape(Rectangle())
            .gesture(
                seekable
                ? DragGesture(minimumDistance: 0)
                    .onChanged { v in scrubFraction = clamp(v.location.x / w) }
                    .onEnded { v in
                        let f = clamp(v.location.x / w)
                        monitor.seek(toSeconds: f * duration)
                        scrubFraction = nil
                    }
                : nil
            )
        }
        .frame(height: 5)
        .accessibilityElement()
        .accessibilityLabel("재생 위치")
        .accessibilityValue("\(Int(fraction * 100)) 퍼센트")
    }

    // MARK: - Bindings / derived data

    private var snap: NowPlayingMonitor.Snapshot? {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty else { return nil }
        return s
    }
    private var hasNowPlaying: Bool { snap != nil }
    private var npTitle: String { snap?.title ?? "재생 정보 없음" }
    private var npArtist: String { snap?.artist.nonEmpty ?? snap?.appName ?? "" }
    private var isPlaying: Bool { snap?.playing ?? false }

    private var duration: Double { snap?.duration ?? 0 }
    private var seekable: Bool { hasNowPlaying && duration > 0 }
    private var liveElapsed: Double { duration > 0 ? min(duration, monitor.elapsedNow) : 0 }
    // Fraction shown on the bar: the scrub position while dragging, else live.
    private var fraction: Double {
        if let s = scrubFraction { return s }
        return duration > 0 ? clamp(liveElapsed / duration) : 0
    }
    private var displayElapsed: Double { fraction * duration }
    private var remainingClock: String {
        guard duration > 0 else { return "-0:00" }
        return "-" + clock(max(0, duration - displayElapsed))
    }

    private func openMainWindow() {
        onOpenMain()
    }

    // Card text/icons: white in dark, black in light.
    private var ink: Color { scheme == .dark ? .white : .black }

    private func clamp(_ x: Double) -> Double { max(0, min(1, x)) }
    private func clock(_ seconds: Double) -> String {
        let s = Int(seconds.rounded())
        return String(format: "%d:%02d", s / 60, s % 60)
    }
}

private extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
}
