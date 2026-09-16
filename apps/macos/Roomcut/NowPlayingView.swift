//
// NowPlayingView.swift — the centre of the main shell.
//
// Priority: live system Now Playing (NowPlayingMonitor) > fixture metadata (QA)
// > engine-signal fallback ("System Audio"). Live data brings the real title /
// artist / artwork / elapsed time and wires the transport buttons to MediaRemote
// commands. The same view renders two layouts — `full` (collapsed home, a soft
// glass "orb") and `compact` (a glass pill when the sound-controls sheet opens).
//
import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

struct NowPlayingView: View {
    let display: NowPlayingDisplayState
    let compact: Bool
    let audioFormat: AudioFormatInfo?
    var theme: RoomcutNowPlayingTheme = .halo
    var layout: RoomcutNowPlayingLayout = .a
    // B: the sound-controls sheet is half-open — fade the lyrics out.
    var soundControlsHalfOpen: Bool = false
    var menuLike: Bool = false
    var themeSync: Bool = false   // when on, the menu-like card uses the theme wash
    // Lyric type size — the sheet shrinks it a touch as it grows (Home passes it).
    var lyricFontSize: CGFloat = 20
    @ObservedObject var monitor: NowPlayingMonitor
    var onMenuCardTap: (() -> Void)?
    @Environment(\.colorScheme) var scheme
    // Morphs the shared hero elements (art / title / artist / album / transport)
    // between the full orb and the compact pill when the Sound Controls sheet toggles.
    @Namespace var morphNS

    // Scrubbing: while the user drags the progress bar, show this fraction
    // instead of the live elapsed (so it doesn't fight the 1s ticker). Committed
    // to a real seek on release. nil = not scrubbing.
    @State var scrubFraction: Double?

    // Live snapshot, only when the helper self-test passed and a titled track is
    // actually reporting. Everything below prefers it over fixture/fallback.
    var live: NowPlayingMonitor.Snapshot? {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty else { return nil }
        return s
    }

    // Live data with a known duration can be scrubbed to seek.
    var seekable: Bool {
        if let live, live.duration > 0 { return true }
        return false
    }

    var accent: Color { RoomcutTokens.blue(scheme) }

    var title: String {
        if let live { return live.title }
        return RoomcutMainPresentation.title(for: display)
    }

    var artist: String? {
        if let live { return live.artist.nonEmpty }
        if case .fixture(_, let a, _, _) = display { return a }
        return nil
    }

    var source: String? {
        // Don't surface the OS media-process name (e.g. "Safari Graphics and
        // Media"); only a real album reads as useful source info.
        if let live { return live.album.nonEmpty }
        if case .fixture(_, _, let s, _) = display, !s.isEmpty { return s }
        return nil
    }

    var fallbackSubtitle: String { RoomcutMainPresentation.subtitle(for: display) }

    var controlsEnabled: Bool {
        if live != nil { return true }
        return RoomcutMainPresentation.controlsEnabled(for: display)
    }

    var isPlaying: Bool {
        if let live { return live.playing }
        return controlsEnabled
    }

    // Elapsed / total seconds for the progress row. While scrubbing, elapsed
    // follows the drag. Live = real values; fixture = a nominal 4:03; fallback = none.
    var times: (elapsed: Double, total: Double)? {
        if let live, live.duration > 0 {
            let elapsed = scrubFraction.map { $0 * live.duration } ?? monitor.elapsedNow
            return (elapsed, live.duration)
        }
        if case .fixture(_, _, _, let p) = display { return (p * 243, 243) }
        return nil
    }

    var progress: Double {
        if let f = scrubFraction { return max(0, min(1, f)) }
        guard let t = times, t.total > 0 else { return 0 }
        return max(0, min(1, t.elapsed / t.total))
    }

    var menuInk: Color { scheme == .dark ? .white : .black }

    // Adaptive ink over the Cover/Mesh wash: in LIGHT mode the chrome text/controls
    // are dark; over a DARK wash they'd be unreadable, so render them (and the
    // lyrics) white. Dark mode already uses light ink; Halo keeps the normal tokens;
    // the album artwork thumbnail is never recolored. Only the full layout (not the
    // sheet-open compact pill or the menu-bar card) sits directly over the wash.
    var darkBackdrop: Bool {
        // A and B both drive their ink off the SAME judgement the tab bar uses
        // (theme + artwork colour), so chrome over the wash stays in sync.
        guard !menuLike, !compact, scheme == .light, live != nil else { return false }
        return NowPlayingInk.isDarkBackdrop(theme: theme, scheme: scheme,
                                            artworkColor: monitor.artworkColor,
                                            artworkPalette: monitor.artworkPalette)
    }

    // Dark-mode counterpart: a near-white Cover wash makes the light ink invisible,
    // so the whole Home chrome flips to BLACK — same judgement as the tab bar.
    var brightBackdrop: Bool {
        guard !menuLike, !compact, scheme == .dark, live != nil else { return false }
        return NowPlayingInk.isBrightBackdrop(theme: theme, scheme: scheme,
                                              artworkColor: monitor.artworkColor,
                                              artworkPalette: monitor.artworkPalette)
    }

    var inkPrimary: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    // Artist/album over a LIGHT backdrop: darker than the stock secondary grey
    // (0x6E6E73) but still below the title's near-black (0x1D1D1F), so the
    // metadata reads stronger without competing with the title. Dark mode uses
    // plain white (the grey token read as washed-out); a dark backdrop keeps
    // white; a bright dark-mode backdrop takes the light-backdrop grey.
    var inkSecondary: Color {
        if darkBackdrop { return Color.white.opacity(0.72) }
        if brightBackdrop { return Color(hex: 0x3A3A3C) }
        return scheme == .light ? Color(hex: 0x3A3A3C) : .white
    }
    var fullInk: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return menuInk
    }

    var fullMetadataColor: Color {
        // else == light mode over a light backdrop → use the same stronger grey
        // (0x3A3A3C) as inkSecondary instead of the lighter 0x6E6E73.
        if brightBackdrop { return Color(hex: 0x3A3A3C) }
        if darkBackdrop { return Color.white.opacity(0.78) }
        return scheme == .dark ? .white : Color(hex: 0x3A3A3C)
    }

    var menuTitle: String { live?.title ?? "재생 정보 없음" }

    var menuArtist: String {
        if let live { return live.artist.nonEmpty ?? live.appName }
        return ""
    }

    var menuDuration: Double { live?.duration ?? 0 }
    var menuSeekable: Bool { live != nil && menuDuration > 0 }

    var menuElapsed: Double {
        menuDuration > 0 ? min(menuDuration, monitor.elapsedNow) : 0
    }

    var menuFraction: Double {
        if let scrubFraction { return max(0, min(1, scrubFraction)) }
        return menuDuration > 0 ? max(0, min(1, menuElapsed / menuDuration)) : 0
    }

    var menuDisplayElapsed: Double { menuFraction * menuDuration }

    var menuRemainingClock: String {
        guard menuDuration > 0 else { return "-0:00" }
        return "-" + Self.clock(max(0, menuDuration - menuDisplayElapsed))
    }

    var lyricLines: (current: String?, next: String?) {
        if live != nil { return (monitor.currentLyric, monitor.nextLyric) }
        if case .fixture = display {
            return (
                "Lights blur on the highway tonight",
                "We keep the quiet signal bright"
            )
        }
        return (nil, nil)
    }

    var body: some View {
        // Morph (not crossfade) between the full orb and the compact pill: with no
        // card-level `.transition`, only one card exists per state, so matchedGeometry
        // gets a single clean source and the shared hero elements (album art / title /
        // artist / album / transport) fly AND resize between the two layouts. Driven
        // here (value: compact) so it animates no matter how `level` was set.
        Group {
            if menuLike {
                menuLikeStack
            } else if compact {
                compactCard
            } else if layout == .b {
                bCard
                    .transition(.opacity)
            } else {
                fullCard
            }
        }
        .animation(.smooth(duration: 0.34), value: compact)
        // ONE album-art layer that physically morphs position AND size between the full
        // orb and compact pill. The cards host only an invisible slot of the right size;
        // this single persistent image is scaled (transform) to whichever slot is active,
        // so it actually resizes instead of a fixed-size copy just sliding over.
        .overlay {
            // The ONE cover layer that physically scales + flies between the full slot
            // and the 44pt pill slot. matchedGeometry matches POSITION only; the size
            // is the explicit `.scaleEffect` (so width/height actually shrink/grow).
            // B's slot is the full canvas width, A's is the orb size.
            if !menuLike {
                if layout == .b {
                    let bw = RoomcutWindowMetrics.baseWidth
                    let topC = (live != nil ? monitor.artworkTopColor : nil).map { Color(nsColor: $0) } ?? .clear
                    let botC = (live != nil ? monitor.artworkBottomColor : nil).map { Color(nsColor: $0) } ?? .clear
                    bArtwork(width: bw, cornerRadius: compact ? bw * 9.0 / 44.0 : 0)
                        // Feather only the full-bleed cover; in the pill it would leave
                        // straight colour bands above/below the rounded art ("shadow").
                        .overlay(alignment: .top) {
                            LinearGradient(colors: [topC, .clear], startPoint: .top, endPoint: .bottom)
                                .frame(height: bw * 0.06)
                                .opacity(compact ? 0 : 1)
                        }
                        .overlay(alignment: .bottom) {
                            LinearGradient(colors: [.clear, botC], startPoint: .top, endPoint: .bottom)
                                .frame(height: bw * 0.06)
                                .opacity(compact ? 0 : 1)
                        }
                        .scaleEffect(compact ? 44.0 / bw : 1.0, anchor: .center)
                        .matchedGeometryEffect(id: "np-art", in: morphNS,
                                               properties: .position, anchor: .center, isSource: false)
                        .animation(.smooth(duration: 0.34), value: compact)
                        .allowsHitTesting(false)
                } else {
                    let fullArt: CGFloat = theme != .halo ? 150 : 124
                    artwork(size: fullArt)
                        .scaleEffect(compact ? 44.0 / fullArt : 1.0, anchor: .center)
                        .matchedGeometryEffect(id: "np-art", in: morphNS,
                                               properties: .position, anchor: .center, isSource: false)
                        .animation(.smooth(duration: 0.34), value: compact)
                        .allowsHitTesting(false)
                }
            }
        }
    }

}
