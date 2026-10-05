// Whole-scene presets for the Space tab: one choice sets Surround, Room and
// Stage together, with a separate list for each output — a speaker room and a
// headphone virtualiser want different things from the same controls.
//
// A preset sets every value the tab has: Surround, Room and its amount, the
// Fine Tune sliders (Space, Center, Damping, Crossfeed / Crosstalk 3D) and, on a
// headphone layout, the upmix steering. Where the Fine Tune values are not one
// of the Stage row's named settings, that row reads Custom. How each value was
// measured is written above the tables below.

// The Stage row's named settings, in engine units (Space -200...200, Center and
// Damping 0...200, Crossfeed 0...100). Output-aware: crossfeed means opposite
// things on the two systems.
//   • Widen → speakers add XTC for an out-of-speaker stage; headphones keep full
//             separation (crossfeed 0), widening via the M/S side only.
//   • Focus → tight, centred, dry image; speakers keep XTC off (it would widen),
//             headphones add crossfeed to naturalise the hard separation.
public enum SpaceStage: CaseIterable, Sendable {
    case off, focus, widen

    public struct Values: Equatable, Sendable {
        public let width, centerFocus, crossfeed, roomReduce: Double
    }

    public func values(headphone: Bool) -> Values {
        switch self {
        case .off:   return Values(width: 0, centerFocus: 0, crossfeed: 0, roomReduce: 0)
        case .focus: return Values(width: -40, centerFocus: 40, crossfeed: headphone ? 25 : 0, roomReduce: 50)
        case .widen: return Values(width: 50, centerFocus: 0, crossfeed: headphone ? 0 : 35, roomReduce: 0)
        }
    }
}

public struct SpacePreset: Identifiable, Equatable, Sendable {
    public enum Group: Int, CaseIterable, Sendable { case everyday, music, cinema }

    public let id: String
    public let headphone: Bool
    public let group: Group
    // English, Japanese, French, German. The Korean slot shows English, as the
    // rest of the Space tab does.
    let names: (en: String, ja: String, fr: String, de: String)
    public let surround: RoomcutViewModel.SurroundChoice
    public let roomType: Double
    public let roomAmount: Double
    // The Fine Tune values, engine units: Space -200...200, Center and Damping
    // 0...200, Crossfeed / Crosstalk 3D 0...100.
    public let width: Double
    public let centerFocus: Double
    public let roomReduce: Double
    public let crossfeed: Double
    public let centerWidth: Double
    public let surroundDepth: Double

    public var name: String { L(names.en, names.en, names.ja, names.fr, names.de) }

    public static func == (a: SpacePreset, b: SpacePreset) -> Bool { a.id == b.id }

    // Whether the parameters are this preset. Crossfeed is skipped where the
    // engine does not render it (headphones while an upmix or head tracking
    // places the speakers), so turning tracking on does not read as a custom
    // setting. The upmix steering only counts when there is an upmix.
    func matches(_ p: EngineParameters, crossfeedRendered: Bool) -> Bool {
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 0.5 }
        let mode = Int(p.spatialMode.rounded())
        guard (mode == 1 || mode == 2) == headphone else { return false }
        let layout = Int(p.surroundType.rounded())
        let actual: RoomcutViewModel.SurroundChoice = layout >= 3 ? .virtual71
            : layout >= 2 ? .virtual51 : (mode == 2 || mode == 3 ? .ambience : .off)
        guard actual == surround else { return false }
        let room = Int(p.roomType.rounded())
        guard room == Int(roomType.rounded()) else { return false }
        if room >= 1, !near(p.roomAmount, roomAmount) { return false }
        guard near(p.spatialWidth, width), near(p.centerFocus, centerFocus),
              near(p.roomReduce, roomReduce) else { return false }
        if crossfeedRendered, !near(p.crossfeed, crossfeed) { return false }
        // Steering only exists on a headphone layout (see centerWidthApplies).
        if layout >= 2, headphone {
            if !near(p.centerWidth, centerWidth) || !near(p.surroundDepth, surroundDepth) { return false }
        }
        return true
    }
}

public enum SpacePresetLibrary {
    public static func presets(headphone: Bool) -> [SpacePreset] {
        headphone ? headphones : speakers
    }

    public static func preset(id: String) -> SpacePreset? {
        (speakers + headphones).first { $0.id == id }
    }

    // The first of each list: everything off.
    public static func reference(headphone: Bool) -> SpacePreset {
        presets(headphone: headphone)[0]
    }

    private static func make(_ id: String, _ headphone: Bool, _ group: SpacePreset.Group,
                             _ names: (String, String, String, String),
                             surround: RoomcutViewModel.SurroundChoice = .off,
                             room: Double = 0, amount: Double = 50,
                             width: Double = 0, center: Double = 0, damping: Double = 0, cross: Double = 0,
                             centerWidth: Double = 100, depth: Double = 50) -> SpacePreset {
        SpacePreset(id: id, headphone: headphone, group: group,
                    names: (en: names.0, ja: names.1, fr: names.2, de: names.3),
                    surround: surround, roomType: room, roomAmount: amount,
                    width: width, centerFocus: center, roomReduce: damping, crossfeed: cross,
                    centerWidth: centerWidth, surroundDepth: depth)
    }

    // Every value was measured, not chosen by ear (2026-09-18). Recordings,
    // loudest 8 s of each: Josh Woodward (pop, ballad, rock, acoustic; CC BY),
    // Nine Inch Nails "Ghosts I" (CC BY-NC-SA), ProleteR (hip-hop; CC BY-NC-ND),
    // Kevin MacLeod (electronic, jazz; CC BY), Musopen Chopin (CC0), the
    // NativeDSD Shostakovich excerpt, Scott Buckley (orchestral; CC BY) and
    // LibriVox readings (public domain) — measurement only, none of it ships.
    // Each render went through the DSPChain with AUSpatialMixer attached.
    //   - Music: interaural correlation (500 Hz-2 kHz octaves) made as close as
    //     possible to that of the recording over a standard +-30 deg speaker pair
    //     — on headphones at the ears directly, on speakers at the ears of a
    //     listener with the speakers close at +-15 deg.
    //   - Speech presets: dialogue-to-background ratio (300 Hz-4 kHz) as high as
    //     possible; movie presets the same without losing width.
    //   - What the Fine Tune values add may change level by at most 1 dB, raise
    //     peaks by at most 1 dB, colour a 1/3 octave by at most 3 dB (2 dB on
    //     speech) and, on speakers, cost the mono sum at most 1 dB. The scenes
    //     searched for the genre presets also hold, against Reference: level
    //     within 1.5 dB, colour within 4 dB, mono sum down at most 1 dB, and on a
    //     master peaking at -1 dBFS at most 1 dB for the output limiter.
    //   - On speakers the mono-sum limit stops Crosstalk 3D at 10 and the level
    //     limit stops Space short of +20 on wide mixes; a value moves only when
    //     it measurably helps.
    static let speakers: [SpacePreset] = [
        make("sp-reference", false, .everyday, ("Reference", "リファレンス", "Référence", "Referenz")),
        make("sp-voice", false, .everyday, ("Voice", "ボイス", "Voix", "Stimme"), width: -40, center: 20),
        make("sp-podcast", false, .everyday, ("Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"), room: 1, amount: 100, width: -40, center: 20),
        make("sp-meeting", false, .everyday, ("Meeting Room", "会議室", "Salle de réunion", "Besprechungsraum"), room: 2, amount: 50, width: -40, damping: 20),
        make("sp-living", false, .everyday, ("Living Room", "リビングルーム", "Salon", "Wohnzimmer"), room: 2, amount: 50, cross: 10),
        make("sp-pop", false, .music, ("Pop", "ポップ", "Pop", "Pop"), surround: .virtual51, room: 2, amount: 75, cross: 10),
        make("sp-ballad", false, .music, ("Ballad", "バラード", "Ballade", "Ballade"), surround: .virtual51, room: 3, amount: 50, cross: 10),
        make("sp-rock", false, .music, ("Rock", "ロック", "Rock", "Rock"), surround: .virtual51, room: 2, amount: 50),
        make("sp-hiphop", false, .music, ("Hip-Hop", "ヒップホップ", "Hip-hop", "Hip-Hop"), room: 3, amount: 25),
        make("sp-electronic", false, .music, ("Electronic", "エレクトロニック", "Électronique", "Elektronisch"), surround: .virtual51, room: 2, amount: 75),
        make("sp-jazz", false, .music, ("Jazz", "ジャズ", "Jazz", "Jazz"), surround: .virtual51, room: 3, amount: 100, center: 10),
        make("sp-classical", false, .music, ("Classical", "クラシック", "Classique", "Klassik"), surround: .virtual51, room: 3, amount: 25),
        make("sp-acoustic", false, .music, ("Acoustic", "アコースティック", "Acoustique", "Akustisch"), surround: .virtual51, room: 3, amount: 25, cross: 10),
        make("sp-studio", false, .music, ("Studio", "スタジオ", "Studio", "Studio"), room: 1, amount: 100, cross: 10),
        make("sp-lounge", false, .music, ("Lounge", "ラウンジ", "Lounge", "Lounge"), room: 2, amount: 80, cross: 10),
        make("sp-ambience", false, .music, ("Ambience", "アンビエンス", "Ambiance", "Ambiente"), surround: .ambience, cross: 10),
        make("sp-chamber", false, .music, ("Chamber Hall", "室内楽ホール", "Salle de chambre", "Kammersaal"), room: 3, amount: 40, cross: 10),
        make("sp-concert", false, .music, ("Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"), room: 3, amount: 70, cross: 10),
        make("sp-gaming", false, .cinema, ("Gaming", "ゲーム", "Jeu", "Gaming"), surround: .virtual51, room: 1, amount: 100, cross: 10),
        make("sp-hometheater", false, .cinema, ("Home Theater", "ホームシアター", "Home cinéma", "Heimkino"), surround: .virtual51, room: 2, amount: 65),
        make("sp-widestage", false, .cinema, ("Wide Stage", "ワイドステージ", "Scène large", "Breite Bühne"), surround: .virtual51, cross: 10),
        make("sp-cinema", false, .cinema, ("Cinema", "シネマ", "Cinéma", "Kino"), surround: .virtual51, room: 3, amount: 40),
        make("sp-tvshow", false, .cinema, ("TV Show", "テレビ番組", "Émission TV", "TV-Show"), surround: .ambience, width: -40, center: 20),
    ]

    static let headphones: [SpacePreset] = [
        make("hp-reference", true, .everyday, ("Reference", "リファレンス", "Référence", "Referenz")),
        make("hp-voice", true, .everyday, ("Voice", "ボイス", "Voix", "Stimme"), width: -40, center: 20),
        make("hp-podcast", true, .everyday, ("Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"), room: 1, amount: 50, width: -40, damping: 20),
        make("hp-living", true, .everyday, ("Living Room", "リビングルーム", "Salon", "Wohnzimmer"), room: 2, amount: 40, cross: 20),
        make("hp-ambience", true, .everyday, ("Ambience", "アンビエンス", "Ambiance", "Ambiente"), surround: .ambience, cross: 10),
        make("hp-pop", true, .music, ("Pop", "ポップ", "Pop", "Pop"), surround: .virtual71, room: 1, amount: 25, depth: 100),
        make("hp-ballad", true, .music, ("Ballad", "バラード", "Ballade", "Ballade"), surround: .virtual71, room: 1, amount: 25, centerWidth: 75, depth: 100),
        make("hp-rock", true, .music, ("Rock", "ロック", "Rock", "Rock"), room: 3, amount: 25, center: 10, cross: 20),
        make("hp-hiphop", true, .music, ("Hip-Hop", "ヒップホップ", "Hip-hop", "Hip-Hop"), room: 1, amount: 25),
        make("hp-electronic", true, .music, ("Electronic", "エレクトロニック", "Électronique", "Elektronisch"), room: 2, amount: 50),
        make("hp-jazz", true, .music, ("Jazz", "ジャズ", "Jazz", "Jazz"), surround: .virtual71, room: 1, amount: 25, centerWidth: 75, depth: 0),
        make("hp-classical", true, .music, ("Classical", "クラシック", "Classique", "Klassik"), surround: .virtual71, width: 40, damping: 20, centerWidth: 0),
        make("hp-acoustic", true, .music, ("Acoustic", "アコースティック", "Acoustique", "Akustisch"), surround: .ambience, room: 3, amount: 25),
        make("hp-studio", true, .music, ("Studio", "スタジオ", "Studio", "Studio"), room: 1, amount: 80),
        make("hp-lounge", true, .music, ("Lounge", "ラウンジ", "Lounge", "Lounge"), room: 2, amount: 60, center: 10),
        make("hp-concert", true, .music, ("Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"), room: 3, amount: 35, center: 10),
        make("hp-surroundmusic", true, .music, ("Surround Music", "サラウンドミュージック", "Musique surround", "Surround-Musik"), surround: .virtual51, center: 20, centerWidth: 75, depth: 0),
        make("hp-orchestra", true, .music, ("Orchestra", "オーケストラ", "Orchestre", "Orchester"), surround: .virtual51, room: 3, amount: 50, center: 30, depth: 100),
        make("hp-hometheater", true, .cinema, ("Home Theater", "ホームシアター", "Home cinéma", "Heimkino"), surround: .virtual51, room: 1, amount: 50, damping: 20, depth: 75),
        make("hp-cinema71", true, .cinema, ("Cinema 7.1", "シネマ 7.1", "Cinéma 7.1", "Kino 7.1"), surround: .virtual71, damping: 20, depth: 100),
        make("hp-living71", true, .cinema, ("Living Room 7.1", "リビングルーム 7.1", "Salon 7.1", "Wohnzimmer 7.1"), surround: .virtual71, room: 2, amount: 40, center: 10, centerWidth: 75, depth: 0),
        make("hp-gaming71", true, .cinema, ("Gaming 7.1", "ゲーム 7.1", "Jeu 7.1", "Gaming 7.1"), surround: .virtual71, room: 1, amount: 40, centerWidth: 75, depth: 25),
        make("hp-dialogue51", true, .cinema, ("Dialogue 5.1", "ダイアログ 5.1", "Dialogues 5.1", "Dialog 5.1"), surround: .virtual51, width: -40, center: 20, depth: 25),
    ]
}
