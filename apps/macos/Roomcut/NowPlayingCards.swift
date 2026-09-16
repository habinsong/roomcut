import AppKit
import SwiftUI
import RoomcutCore
import RoomcutPresentationCore

// The three card layouts Now Playing can take: the home orb, the full-bleed
// artwork variant, and the compact pill shown while the sound sheet is open.
extension NowPlayingView {
    // MARK: Full (home) — soft glass orb

    var fullCard: some View {
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
    var bMetaAttributed: AttributedString {
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

    var bMetaKey: String { "\(artist ?? "")|\(source ?? "")" }

    // Title as an attributed string so it can ride the same marquee when long.
    var bTitleAttributed: AttributedString {
        var s = AttributedString(title)
        s.font = .system(size: 22, weight: .semibold)
        s.foregroundColor = inkPrimary
        return s
    }

    var bCard: some View {
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
    func bArtwork(width: CGFloat, cornerRadius: CGFloat = 0) -> some View {
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
    var fullBackdrop: some View {
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

    var compactCard: some View {
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

    var menuLikeStack: some View {
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

    var menuLikeCard: some View {
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
}
