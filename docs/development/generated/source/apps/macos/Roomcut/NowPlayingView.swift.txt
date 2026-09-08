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
    @Environment(\.colorScheme) private var scheme
    // Morphs the shared hero elements (art / title / artist / album / transport)
    // between the full orb and the compact pill when the Sound Controls sheet toggles.
    @Namespace private var morphNS

    // Scrubbing: while the user drags the progress bar, show this fraction
    // instead of the live elapsed (so it doesn't fight the 1s ticker). Committed
    // to a real seek on release. nil = not scrubbing.
    @State private var scrubFraction: Double?

    // Live snapshot, only when the helper self-test passed and a titled track is
    // actually reporting. Everything below prefers it over fixture/fallback.
    private var live: NowPlayingMonitor.Snapshot? {
        guard monitor.available, let s = monitor.snapshot, !s.title.isEmpty else { return nil }
        return s
    }

    // Live data with a known duration can be scrubbed to seek.
    private var seekable: Bool {
        if let live, live.duration > 0 { return true }
        return false
    }

    private var accent: Color { RoomcutTokens.blue(scheme) }

    private var title: String {
        if let live { return live.title }
        return RoomcutMainPresentation.title(for: display)
    }

    private var artist: String? {
        if let live { return live.artist.nonEmpty }
        if case .fixture(_, let a, _, _) = display { return a }
        return nil
    }

    private var source: String? {
        // Don't surface the OS media-process name (e.g. "Safari Graphics and
        // Media"); only a real album reads as useful source info.
        if let live { return live.album.nonEmpty }
        if case .fixture(_, _, let s, _) = display, !s.isEmpty { return s }
        return nil
    }

    private var fallbackSubtitle: String { RoomcutMainPresentation.subtitle(for: display) }

    private var controlsEnabled: Bool {
        if live != nil { return true }
        return RoomcutMainPresentation.controlsEnabled(for: display)
    }

    private var isPlaying: Bool {
        if let live { return live.playing }
        return controlsEnabled
    }

    // Elapsed / total seconds for the progress row. While scrubbing, elapsed
    // follows the drag. Live = real values; fixture = a nominal 4:03; fallback = none.
    private var times: (elapsed: Double, total: Double)? {
        if let live, live.duration > 0 {
            let elapsed = scrubFraction.map { $0 * live.duration } ?? monitor.elapsedNow
            return (elapsed, live.duration)
        }
        if case .fixture(_, _, _, let p) = display { return (p * 243, 243) }
        return nil
    }

    private var progress: Double {
        if let f = scrubFraction { return max(0, min(1, f)) }
        guard let t = times, t.total > 0 else { return 0 }
        return max(0, min(1, t.elapsed / t.total))
    }

    private var menuInk: Color { scheme == .dark ? .white : .black }

    // Adaptive ink over the Cover/Mesh wash: in LIGHT mode the chrome text/controls
    // are dark; over a DARK wash they'd be unreadable, so render them (and the
    // lyrics) white. Dark mode already uses light ink; Halo keeps the normal tokens;
    // the album artwork thumbnail is never recolored. Only the full layout (not the
    // sheet-open compact pill or the menu-bar card) sits directly over the wash.
    private var darkBackdrop: Bool {
        // A and B both drive their ink off the SAME judgement the tab bar uses
        // (theme + artwork colour), so chrome over the wash stays in sync.
        guard !menuLike, !compact, scheme == .light, live != nil else { return false }
        return NowPlayingInk.isDarkBackdrop(theme: theme, scheme: scheme,
                                            artworkColor: monitor.artworkColor,
                                            artworkPalette: monitor.artworkPalette)
    }

    // Dark-mode counterpart: a near-white Cover wash makes the light ink invisible,
    // so the whole Home chrome flips to BLACK — same judgement as the tab bar.
    private var brightBackdrop: Bool {
        guard !menuLike, !compact, scheme == .dark, live != nil else { return false }
        return NowPlayingInk.isBrightBackdrop(theme: theme, scheme: scheme,
                                              artworkColor: monitor.artworkColor,
                                              artworkPalette: monitor.artworkPalette)
    }

    private var inkPrimary: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }
    // Artist/album over a LIGHT backdrop: darker than the stock secondary grey
    // (0x6E6E73) but still below the title's near-black (0x1D1D1F), so the
    // metadata reads stronger without competing with the title. Dark mode uses
    // plain white (the grey token read as washed-out); a dark backdrop keeps
    // white; a bright dark-mode backdrop takes the light-backdrop grey.
    private var inkSecondary: Color {
        if darkBackdrop { return Color.white.opacity(0.72) }
        if brightBackdrop { return Color(hex: 0x3A3A3C) }
        return scheme == .light ? Color(hex: 0x3A3A3C) : .white
    }
    private var fullInk: Color {
        if darkBackdrop { return .white }
        if brightBackdrop { return .black }
        return menuInk
    }

    private var fullMetadataColor: Color {
        // else == light mode over a light backdrop → use the same stronger grey
        // (0x3A3A3C) as inkSecondary instead of the lighter 0x6E6E73.
        if brightBackdrop { return Color(hex: 0x3A3A3C) }
        if darkBackdrop { return Color.white.opacity(0.78) }
        return scheme == .dark ? .white : Color(hex: 0x3A3A3C)
    }

    private var menuTitle: String { live?.title ?? "재생 정보 없음" }

    private var menuArtist: String {
        if let live { return live.artist.nonEmpty ?? live.appName }
        return ""
    }

    private var menuDuration: Double { live?.duration ?? 0 }
    private var menuSeekable: Bool { live != nil && menuDuration > 0 }

    private var menuElapsed: Double {
        menuDuration > 0 ? min(menuDuration, monitor.elapsedNow) : 0
    }

    private var menuFraction: Double {
        if let scrubFraction { return max(0, min(1, scrubFraction)) }
        return menuDuration > 0 ? max(0, min(1, menuElapsed / menuDuration)) : 0
    }

    private var menuDisplayElapsed: Double { menuFraction * menuDuration }

    private var menuRemainingClock: String {
        guard menuDuration > 0 else { return "-0:00" }
        return "-" + Self.clock(max(0, menuDuration - menuDisplayElapsed))
    }

    private var lyricLines: (current: String?, next: String?) {
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

    // MARK: Full (home) — soft glass orb

    private var fullCard: some View {
        // Cover / Mesh fill the whole screen, so the Now Playing block can be larger
        // and sit at the true vertical centre. Halo keeps its tighter card (its ring
        // backdrop frames the content). Lyrics are unchanged in either case.
        let big = theme != .halo
        let card: CGFloat = big ? 392 : 350
        let content: CGFloat = big ? 360 : 330
        let art: CGFloat = big ? 150 : 124
        let titleSize: CGFloat = big ? 22 : 19
        let progressW: CGFloat = big ? 268 : 236
        let tSpacing: CGFloat = big ? 30 : 26
        let tPlay: CGFloat = big ? 50 : 44
        let tSide: CGFloat = big ? 19 : 17

        return VStack(spacing: 16) {
            ZStack {
                fullBackdrop

                VStack(spacing: big ? 14 : 12) {
                    Color.clear
                        .frame(width: art, height: art)
                        .matchedGeometryEffect(id: "np-art", in: morphNS, properties: .position)

                    VStack(spacing: 6) {
                        VStack(spacing: 2) {
                            Text(title)
                                .font(.system(size: titleSize, weight: .semibold))
                                .foregroundStyle(inkPrimary)
                                .lineLimit(1)
                                .matchedGeometryEffect(id: "np-title", in: morphNS, properties: .position)
                            reservedMetadataLine(artist, size: big ? 13 : 12, color: fullMetadataColor)
                                .matchedGeometryEffect(id: "np-artist", in: morphNS, properties: .position)
                            reservedMetadataLine(source, size: big ? 12 : 11, color: fullMetadataColor)
                                .matchedGeometryEffect(id: "np-album", in: morphNS, properties: .position)
                        }

                        progressRow
                            .frame(width: progressW)
                        VStack(spacing: 3) {
                            audioFormatText()
                            transportControls(spacing: tSpacing, playSize: tPlay, sideSize: tSide)
                                .offset(y: -6)
                        }
                    }
                }
                .frame(width: content)
            }
            .frame(width: card, height: card)

            lyricsBlock(width: 320, height: TwoLineLyricView.reservedHeight(for: lyricFontSize))
                // Raise the lyrics so they sit at the midpoint between the Now
                // Playing card and the Sound Controls sheet (Cover/Mesh only).
                .offset(y: big ? -32 : 0)
        }
        .frame(height: big ? 520 : 458, alignment: .center)
        // The lyrics below pull the visual centre up; nudge the block down so the
        // Now Playing card lands on the true screen centre (Cover/Mesh only).
        .offset(y: big ? 52 : 0)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("재생 정보: \(title), \(source ?? fallbackSubtitle)")
    }

    // MARK: B (home) — full-bleed square artwork, metadata + controls stacked below

    // Artist + album on one marquee line — artist a touch larger/bolder/darker,
    // album slightly smaller and lighter, with a small gap between.
    private var bMetaAttributed: AttributedString {
        var result = AttributedString()
        if let a = artist {
            var part = AttributedString(a)
            part.font = .system(size: 14, weight: .medium)
            part.foregroundColor = inkSecondary
            result += part
        }
        if let src = source {
            if !result.characters.isEmpty {
                var gap = AttributedString("  ")
                gap.font = .system(size: 14)
                result += gap
            }
            var part = AttributedString(src)
            part.font = .system(size: 11)
            part.foregroundColor = inkSecondary
            result += part
        }
        return result
    }

    private var bMetaKey: String { "\(artist ?? "")|\(source ?? "")" }

    // Title as an attributed string so it can ride the same marquee when long.
    private var bTitleAttributed: AttributedString {
        var s = AttributedString(title)
        s.font = .system(size: 22, weight: .semibold)
        s.foregroundColor = inkPrimary
        return s
    }

    private var bCard: some View {
        GeometryReader { geo in
            let w = geo.size.width
            let botC = (live != nil ? monitor.artworkBottomColor : nil).map { Color(nsColor: $0) } ?? .clear
            VStack(spacing: 0) {
                // Position anchor only — the actual cover (+feather) is drawn by the
                // shared morph layer in `.overlay`, so it can scale + fly to the pill.
                Color.clear
                    .frame(width: w, height: w)
                    .matchedGeometryEffect(id: "np-art", in: morphNS, properties: .position, anchor: .center)

                // Extend the bottom-edge colour down past the cover — fading to clear —
                // long enough to sit behind the metadata, controls AND lyrics so they
                // stay legible on the blend rather than the bare wash.
                ZStack(alignment: .top) {
                    LinearGradient(colors: [botC, .clear], startPoint: .top, endPoint: .bottom)
                        .frame(maxWidth: .infinity)
                        .frame(height: w * 0.78, alignment: .top)
                        .allowsHitTesting(false)

                    VStack(alignment: .leading, spacing: 10) {
                        VStack(alignment: .leading, spacing: 3) {
                            MarqueeLine(text: bTitleAttributed)
                                .frame(height: 28)
                                .id("title-" + title)
                            MarqueeLine(text: bMetaAttributed)
                                .frame(height: 18)
                                .id(bMetaKey)
                        }

                        progressRow

                        VStack(spacing: 6) {
                            audioFormatText(alignment: .center)

                            transportControls(spacing: 34, playSize: 58, sideSize: 24)
                                .frame(maxWidth: .infinity, alignment: .center)
                                // Pull the buttons up so the format↔buttons gap matches
                                // the progress-bar↔format gap (the tall play button's
                                // frame padding otherwise inflates the visible gap).
                                .padding(.top, -12)
                        }

                        lyricsBlock(width: w - 44,
                                    height: TwoLineLyricView.reservedHeight(for: lyricFontSize))
                            .frame(maxWidth: .infinity)
                            .opacity(soundControlsHalfOpen ? 0 : 1)
                            .animation(.easeInOut(duration: 0.25), value: soundControlsHalfOpen)
                    }
                    .padding(.horizontal, 22)
                    .padding(.top, 14)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("재생 정보: \(title), \(source ?? fallbackSubtitle)")
    }

    // The square artwork bleeds to the full width.
    @ViewBuilder
    private func bArtwork(width: CGFloat, cornerRadius: CGFloat = 0) -> some View {
        Group {
            if let img = (live != nil ? monitor.artwork : nil) {
                ExtendedArtwork(image: img)
            } else {
                let colors: [Color] = scheme == .dark
                    ? [Color(hex: 0x2A2D33), Color(hex: 0x17191D)]
                    : [Color(hex: 0xECECE8), Color(hex: 0xDADAD4)]
                LinearGradient(colors: colors, startPoint: .topLeading, endPoint: .bottomTrailing)
                    .overlay {
                        Image(systemName: "waveform")
                            .font(.system(size: width * 0.16, weight: .light))
                            .foregroundStyle(.white.opacity(0.7))
                    }
            }
        }
        .frame(width: width, height: width)
        .clipShape(RoundedRectangle(cornerRadius: cornerRadius, style: .continuous))
        .accessibilityHidden(true)
    }

    @ViewBuilder
    private var fullBackdrop: some View {
        let artworkColor = live != nil ? monitor.artworkColor : nil
        let dark = scheme == .dark

        switch theme {
        case .halo:
            AuroraRing(
                diameter: 330,
                dark: dark,
                artworkColor: artworkColor)
        case .cover, .meshGradient:
            // Cover + Mesh fill the whole Home surface (RoomcutBackgroundLayer,
            // Layer 0); the card keeps no rounded backdrop so the artwork and
            // controls float over the full-screen wash.
            Color.clear
        }
    }

    // MARK: Compact (sheet open) — glass pill

    private var compactCard: some View {
        VStack(spacing: 6) {
            HStack(spacing: 12) {
                Color.clear
                    .frame(width: 44, height: 44)
                    .matchedGeometryEffect(id: "np-art", in: morphNS, properties: .position)
                let metaEmpty = artist?.nonEmpty == nil && source?.nonEmpty == nil
                let metaColor: Color = scheme == .dark ? .white : RoomcutTokens.textSecondary(scheme)
                VStack(alignment: .leading, spacing: 1) {
                    Text(title)
                        .font(.system(size: 14, weight: .semibold))
                        .foregroundStyle(RoomcutTokens.textPrimary(scheme))
                        .lineLimit(1)
                        .matchedGeometryEffect(id: "np-title", in: morphNS, properties: .position)
                    reservedMetadataLine(metaEmpty ? fallbackSubtitle : artist, size: 11, color: metaColor)
                        .matchedGeometryEffect(id: "np-artist", in: morphNS, properties: .position)
                    reservedMetadataLine(source, size: 11, color: metaColor)
                        .matchedGeometryEffect(id: "np-album", in: morphNS, properties: .position)
                }
                Spacer(minLength: 12)
                transportControls(spacing: 18, playSize: 30, sideSize: 13)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity)
            .roomcutGlass(.card)

            audioFormatText()
                .padding(.top, 2)
        }
        .padding(.horizontal, 14)
        .frame(maxWidth: .infinity)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("재생 정보: \(title)")
    }

    private var menuLikeStack: some View {
        VStack(spacing: 8) {
            menuLikeCard
            // Keep lyrics inside the window's straight edge: the compact window is
            // ~402 wide with 46pt continuous corners, so a line wider than this
            // would run into the rounded bottom corners. Narrower width → long
            // lines wrap to two lines instead (preferred over hitting the curve).
            lyricsBlock(width: 272, height: TwoLineLyricView.reservedHeight(for: lyricFontSize))
        }
        .frame(width: 340)
    }

    private var menuLikeCard: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                menuAlbumArt
                VStack(alignment: .leading, spacing: 1) {
                    Text(menuTitle)
                        .font(.system(size: 13, weight: .semibold))
                        .lineLimit(1)
                    Text(menuArtist)
                        .font(.system(size: 11, weight: .regular))
                        .foregroundStyle(menuInk.opacity(0.5))
                        .lineLimit(1)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .clipped()
            }
            .padding(.top, 14)
            .padding(.horizontal, 14)

            HStack(alignment: .center, spacing: 6) {
                Text(Self.clock(menuDisplayElapsed))
                    .frame(width: 42, alignment: .leading)
                    .lineLimit(1)
                    .foregroundStyle(menuInk.opacity(0.5))
                menuProgressBar
                Text(menuRemainingClock)
                    .frame(width: 48, alignment: .trailing)
                    .lineLimit(1)
                    .foregroundStyle(menuInk.opacity(0.5))
            }
            .font(.system(size: 11).monospacedDigit())
                .padding(.top, 10)
                .padding(.horizontal, 18)

            HStack {
                Button { if live != nil { monitor.command(.previous) } } label: {
                    Image(systemName: "backward.fill")
                        .imageScale(.large)
                        .frame(maxWidth: .infinity)
                }
                Button { if live != nil { monitor.command(.togglePlayPause) } } label: {
                    Image(systemName: isPlaying ? "pause.fill" : "play.fill")
                        .imageScale(.large)
                        .font(.title)
                }
                Button { if live != nil { monitor.command(.next) } } label: {
                    Image(systemName: "forward.fill")
                        .imageScale(.large)
                        .frame(maxWidth: .infinity)
                }
            }
            .buttonStyle(.plain)
            .disabled(live == nil)
            .frame(maxWidth: .infinity)
            .padding(.horizontal, 26)
            .padding(.top, 6)
            .padding(.bottom, 14)
            .font(.title2)
            Spacer(minLength: 0)
        }
        .frame(height: 132)
        .foregroundStyle(menuInk)
        .background {
            let shape = RoundedRectangle(cornerRadius: 30, style: .continuous)
            if themeSync {
                RoomcutBackgroundLayer(theme: theme, artwork: monitor.artwork,
                                       artworkColor: monitor.artworkColor,
                                       artworkPalette: monitor.artworkPalette,
                                       playing: monitor.snapshot?.playing ?? true)
                    .clipShape(shape)
            } else {
                shape.fill(.ultraThinMaterial)
            }
        }
        .contentShape(RoundedRectangle(cornerRadius: 30, style: .continuous))
        .onTapGesture { onMenuCardTap?() }
        .overlay { MenuLikeResizeHandles() }
    }

    // MARK: Pieces

    private func reservedMetadataLine(
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

    private func lyricsBlock(width: CGFloat, height: CGFloat) -> some View {
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

    private var menuAlbumArt: some View {
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

    private var menuProgressBar: some View {
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

    private func menuSeekGesture(width: CGFloat) -> some Gesture {
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
    private func audioFormatText(alignment: Alignment = .center) -> some View {
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
    private func artwork(size: CGFloat) -> some View {
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

    private var progressRow: some View {
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

    private var progressElapsedClock: String {
        guard let t = times else { return "0:00" }
        return Self.clock(progress * t.total)
    }

    private var progressRemainingClock: String {
        guard let t = times else { return "-0:00" }
        return "-" + Self.clock(max(0, t.total - progress * t.total))
    }

    private var progressBar: some View {
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
    private func seekGesture(width: CGFloat) -> some Gesture {
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

    private func transportControls(spacing: CGFloat, playSize: CGFloat, sideSize: CGFloat) -> some View {
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

    private func transportButton(_ symbol: String, size: CGFloat, action: @escaping () -> Void) -> some View {
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

    private static func clock(_ seconds: Double) -> String {
        let s = Int(seconds.rounded())
        return String(format: "%d:%02d", s / 60, s % 60)
    }
}

// Full-window mesh animation control points (3×3), shared by the Layer-0 wash.
enum NowPlayingMeshPoints {
    static func points(_ phase: TimeInterval) -> [SIMD2<Float>] {
        let t = Float(phase)
        func wave(_ base: Float, _ amplitude: Float, _ speed: Float, _ offset: Float = 0) -> Float {
            base + amplitude * sin(t * speed + offset)
        }
        return [
            SIMD2<Float>(0, 0),
            SIMD2<Float>(wave(0.50, 0.08, 0.62), 0),
            SIMD2<Float>(1, 0),
            SIMD2<Float>(0, wave(0.50, 0.07, 0.74, 0.7)),
            SIMD2<Float>(wave(0.50, 0.12, 0.45, 1.1), wave(0.50, 0.12, 0.58, 0.35)),
            SIMD2<Float>(1, wave(0.50, 0.07, 0.80, 1.7)),
            SIMD2<Float>(0, 1),
            SIMD2<Float>(wave(0.50, 0.08, 0.66, 2.1), 1),
            SIMD2<Float>(1, 1),
        ]
    }
}

enum NowPlayingBackdropPalette {
    static func tint(_ artworkColor: NSColor?, dark: Bool) -> Color {
        Color(nsColor: baseColor(artworkColor, dark: dark))
    }

    static func gradientColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [Color] {
        let colors = seedColors(artworkColor: artworkColor, artworkPalette: artworkPalette, dark: dark)
        let base = colors[0]
        let soft = colors[1].blended(withFraction: dark ? 0.18 : 0.32, of: .white) ?? colors[1]
        let deep = colors[2].blended(withFraction: dark ? 0.34 : 0.18, of: .black) ?? colors[2]
        return [Color(nsColor: soft), Color(nsColor: base), Color(nsColor: deep)]
    }

    static func meshColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [Color] {
        let colors = seedColors(artworkColor: artworkColor, artworkPalette: artworkPalette, dark: dark)
        let deep = colors[0].blended(withFraction: dark ? 0.34 : 0.20, of: .black) ?? colors[0]
        let bright = colors[1].blended(withFraction: dark ? 0.14 : 0.30, of: .white) ?? colors[1]
        let soft = colors[2].blended(withFraction: dark ? 0.10 : 0.24, of: .white) ?? colors[2]
        let shadow = colors[3].blended(withFraction: dark ? 0.28 : 0.16, of: .black) ?? colors[3]

        return [
            deep, colors[1], bright,
            colors[2], colors[0], colors[3],
            soft, colors[4], shadow,
        ].map { Color(nsColor: $0) }
    }

    private static func seedColors(
        artworkColor: NSColor?,
        artworkPalette: [NSColor]?,
        dark: Bool
    ) -> [NSColor] {
        var colors = (artworkPalette ?? []).compactMap { $0.usingColorSpace(.deviceRGB) }
        if let base = artworkColor?.usingColorSpace(.deviceRGB) {
            colors.insert(base, at: 0)
        }
        if colors.isEmpty {
            colors = [baseColor(nil, dark: dark)]
        }
        while colors.count < 5 {
            let nextBase = colors[colors.count % max(1, colors.count)]
            let blendTarget: NSColor = colors.count.isMultiple(of: 2) ? .white : .black
            colors.append(nextBase.blended(withFraction: dark ? 0.18 : 0.24, of: blendTarget) ?? nextBase)
        }
        return Array(colors.prefix(5))
    }

    private static func baseColor(_ color: NSColor?, dark: Bool) -> NSColor {
        if let rgb = color?.usingColorSpace(.deviceRGB) {
            return rgb
        }
        let white: CGFloat = dark ? 0.72 : 0.58
        return NSColor(deviceRed: white, green: white, blue: white, alpha: 1)
    }
}

// MARK: - Aurora ring (the Now Playing halo)

struct AuroraRing: View {
    let diameter: CGFloat
    let dark: Bool
    let artworkColor: NSColor?

    @State private var transitionFrom: AuroraBaseColor?
    @State private var transitionTo: AuroraBaseColor?
    @State private var transitionStart = Date.distantPast
    // Animate only DURING a colour transition; pause the timeline when settled so
    // the per-frame blur stops (idle CPU → ~0). Re-armed on each track change.
    @State private var animating = false

    private let transitionDuration: TimeInterval = 1.15

    private var rimTargetKey: String {
        "\(dark):\(Self.colorKey(artworkColor))"
    }

    private var targetBaseColor: AuroraBaseColor {
        Self.baseColor(artworkColor, dark: dark)
    }

    private func rim(for base: AuroraBaseColor, phase: Double) -> AngularGradient {
        let accent = base.nsColor
        let deep = accent.blended(withFraction: 0.22, of: .black) ?? accent
        let bright = accent.blended(withFraction: 0.18, of: .white) ?? accent
        return AngularGradient(
            colors: [
                Color(nsColor: deep), Color(nsColor: accent),
                Color(nsColor: bright), Color(nsColor: accent),
                Color(nsColor: deep), Color(nsColor: accent),
            ],
            center: .center,
            angle: .degrees(132 + phase * 8)
        )
    }

    private func rimCircle(for base: AuroraBaseColor, phase: Double) -> some View {
        Circle()
            .stroke(rim(for: base, phase: phase), lineWidth: dark ? 34 : 30)
            .frame(width: diameter - 8, height: diameter - 8)
            .blur(radius: dark ? 18 : 15)
            .opacity(dark ? 0.58 : 0.44)
    }

    var body: some View {
        TimelineView(.animation(paused: !animating)) { timeline in
            let progress = transitionProgress(at: timeline.date)
            let currentBase = currentBaseColor(at: timeline.date)

            ZStack {
                Circle()
                    .fill(.ultraThinMaterial)
                    .opacity(dark ? 0.45 : 0.65)
                    .frame(width: diameter, height: diameter)

                rimCircle(for: currentBase, phase: progress)

                Circle()
                    .strokeBorder(.white.opacity(dark ? 0.07 : 0.45), lineWidth: 1)
                    .frame(width: diameter, height: diameter)
            }
        }
        .accessibilityHidden(true)
        .allowsHitTesting(false)
        .onAppear {
            transitionFrom = targetBaseColor
            transitionTo = targetBaseColor
        }
        .onChange(of: rimTargetKey) {
            let now = Date()
            transitionFrom = currentBaseColor(at: now)
            transitionTo = targetBaseColor
            transitionStart = now
            animating = true
            // Settle (pause the timeline) once this transition has elapsed — unless
            // a newer track change has re-armed it in the meantime.
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: UInt64((transitionDuration + 0.1) * 1_000_000_000))
                if Date().timeIntervalSince(transitionStart) >= transitionDuration {
                    animating = false
                }
            }
        }
    }

    private func transitionProgress(at date: Date) -> Double {
        min(1, max(0, date.timeIntervalSince(transitionStart) / transitionDuration))
    }

    private func currentBaseColor(at date: Date) -> AuroraBaseColor {
        guard let from = transitionFrom, let to = transitionTo else { return targetBaseColor }
        return from.interpolated(to: to, progress: smoothstep(transitionProgress(at: date)))
    }

    private func smoothstep(_ value: Double) -> Double {
        value * value * (3 - 2 * value)
    }

    private static func baseColor(_ color: NSColor?, dark: Bool) -> AuroraBaseColor {
        if let rgb = color?.usingColorSpace(.deviceRGB) {
            return AuroraBaseColor(
                red: rgb.redComponent,
                green: rgb.greenComponent,
                blue: rgb.blueComponent,
                alpha: rgb.alphaComponent
            )
        }
        let white: CGFloat = dark ? 0.82 : 0.52
        return AuroraBaseColor(red: white, green: white, blue: white, alpha: 1)
    }

    private static func colorKey(_ color: NSColor?) -> String {
        guard let rgb = color?.usingColorSpace(.deviceRGB) else { return "nil" }
        return String(format: "%.3f:%.3f:%.3f:%.3f",
                      rgb.redComponent,
                      rgb.greenComponent,
                      rgb.blueComponent,
                      rgb.alphaComponent)
    }
}

private struct AuroraBaseColor {
    let red: CGFloat
    let green: CGFloat
    let blue: CGFloat
    let alpha: CGFloat

    var nsColor: NSColor {
        NSColor(deviceRed: red, green: green, blue: blue, alpha: alpha)
    }

    func interpolated(to other: AuroraBaseColor, progress: Double) -> AuroraBaseColor {
        let t = CGFloat(progress)
        return AuroraBaseColor(
            red: red + (other.red - red) * t,
            green: green + (other.green - green) * t,
            blue: blue + (other.blue - blue) * t,
            alpha: alpha + (other.alpha - alpha) * t
        )
    }
}

// Artwork in the square art slot, letterbox-aware. Album covers are 1:1 and
// fill the square exactly; video thumbnails (YouTube etc.) are 16:9 and used to
// be centre-cropped, losing the left/right edges. Non-square art now renders
// the FULL image (fit) and fills the top/bottom bands with a blurred, scaled
// copy of itself — the blur-extension treatment video players use. Square art
// keeps the plain fast path (the fit layer would cover the frame anyway, so
// the blur would be invisible cost). Shared by every artwork slot (Card,
// Poster, compact card, menu-bar popover), so `blurRadius` scales down for the
// tiny thumbnails.
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

private struct MenuLikeResizeHandles: View {
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
private struct MarqueeLine: View {
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

private struct MarqueeWidthKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}

private extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
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

private struct TwoLineLyricView: View {
    let current: String?
    let next: String?
    let fontSize: CGFloat
    let scheme: ColorScheme
    var forceWhite: Bool = false
    var forceBlack: Bool = false

    private var inkBase: Color {
        if forceWhite { return .white }
        if forceBlack { return .black }
        return RoomcutTokens.textPrimary(scheme)
    }

    private var primaryColor: Color {
        inkBase.opacity(0.76)
    }

    private var secondaryColor: Color {
        inkBase.opacity(0.44)
    }

    static func reservedHeight(for fontSize: CGFloat) -> CGFloat {
        slotHeight(for: fontSize) + slotHeight(for: max(11, fontSize - 4)) + 2
    }

    private static func slotHeight(for size: CGFloat) -> CGFloat {
        ceil(size * 2.15 + 6)
    }

    var body: some View {
        VStack(spacing: 2) {
            lyricSlot(
                current,
                size: fontSize,
                weight: .medium,
                color: primaryColor
            )
            lyricSlot(
                next,
                size: max(11, fontSize - 4),
                weight: .regular,
                color: secondaryColor
            )
        }
        .multilineTextAlignment(.center)
        .animation(.easeInOut(duration: 0.18), value: current)
        .animation(.easeInOut(duration: 0.18), value: next)
    }

    private func lyricSlot(
        _ text: String?,
        size: CGFloat,
        weight: Font.Weight,
        color: Color
    ) -> some View {
        ZStack {
            if let text {
                AdaptiveLyricText(
                    text: text,
                    baseSize: size,
                    weight: weight,
                    color: color
                )
                    .id(text)
                    .transition(
                        .asymmetric(
                            insertion: .opacity.combined(with: .move(edge: .bottom)),
                            removal: .opacity.combined(with: .move(edge: .top))
                        )
                    )
            } else {
                Text(" ")
                    .font(.system(size: size, weight: weight))
                    .lineLimit(1)
                    .hidden()
            }
        }
        .frame(maxWidth: .infinity)
        .frame(height: Self.slotHeight(for: size))
        .clipped()
    }
}

private struct AdaptiveLyricText: View {
    let text: String
    let baseSize: CGFloat
    let weight: Font.Weight
    let color: Color

    var body: some View {
        GeometryReader { geo in
            let layout = Self.layout(
                text: text,
                width: geo.size.width,
                baseSize: baseSize,
                weight: nsWeight
            )
            Text(layout.text)
                .font(.system(size: layout.fontSize, weight: weight))
                .foregroundStyle(color)
                .lineLimit(layout.lineLimit)
                .minimumScaleFactor(0.92)
                .allowsTightening(true)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
                .animation(.smooth(duration: 0.18), value: layout.text)
                .animation(.smooth(duration: 0.18), value: layout.fontSize)
        }
    }

    private var nsWeight: NSFont.Weight {
        if weight == .ultraLight { return .ultraLight }
        if weight == .thin { return .thin }
        if weight == .light { return .light }
        if weight == .regular { return .regular }
        if weight == .medium { return .medium }
        if weight == .semibold { return .semibold }
        if weight == .bold { return .bold }
        if weight == .heavy { return .heavy }
        if weight == .black { return .black }
        return .regular
    }

    private struct Layout: Equatable {
        let text: String
        let fontSize: CGFloat
        let lineLimit: Int
    }

    private static func layout(
        text: String,
        width: CGFloat,
        baseSize: CGFloat,
        weight: NSFont.Weight
    ) -> Layout {
        let available = max(1, width)
        let singleWidth = measuredWidth(text, size: baseSize, weight: weight)
        let singleScale = min(1, available / max(1, singleWidth))
        if singleScale >= 0.78 {
            return Layout(
                text: text,
                fontSize: max(baseSize * singleScale, baseSize * 0.78),
                lineLimit: 1
            )
        }

        let split = balancedSplit(text, size: baseSize, weight: weight)
        let twoLineText = split.second.isEmpty ? split.first : "\(split.first)\n\(split.second)"
        let firstWidth = measuredWidth(split.first, size: baseSize, weight: weight)
        let secondWidth = measuredWidth(split.second, size: baseSize, weight: weight)
        let twoLineScale = min(1, available / max(1, max(firstWidth, secondWidth)))
        return Layout(
            text: twoLineText,
            fontSize: max(baseSize * twoLineScale, baseSize * 0.58),
            lineLimit: split.second.isEmpty ? 1 : 2
        )
    }

    private static func balancedSplit(
        _ text: String,
        size: CGFloat,
        weight: NSFont.Weight
    ) -> (first: String, second: String) {
        let words = text.split(separator: " ")
        guard words.count > 1 else { return (text, "") }

        var bestFirst = String(words[0])
        var bestSecond = words.dropFirst().joined(separator: " ")
        var bestScore = CGFloat.greatestFiniteMagnitude

        for index in 1..<words.count {
            let first = words[..<index].joined(separator: " ")
            let second = words[index...].joined(separator: " ")
            let firstWidth = measuredWidth(first, size: size, weight: weight)
            let secondWidth = measuredWidth(second, size: size, weight: weight)
            let score = max(firstWidth, secondWidth) + abs(firstWidth - secondWidth) * 0.18
            if score < bestScore {
                bestScore = score
                bestFirst = first
                bestSecond = second
            }
        }

        return (bestFirst, bestSecond)
    }

    private static func measuredWidth(
        _ text: String,
        size: CGFloat,
        weight: NSFont.Weight
    ) -> CGFloat {
        let font = NSFont.systemFont(ofSize: size, weight: weight)
        let attributes: [NSAttributedString.Key: Any] = [.font: font]
        return ceil((text as NSString).size(withAttributes: attributes).width)
    }
}
