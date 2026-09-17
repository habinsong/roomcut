// Whole-scene presets for the Space tab: one choice sets Surround, Room and
// Stage together, with a separate list for each output — a speaker room and a
// headphone virtualiser want different things from the same controls.
//
// A preset is made only of what the tab shows as choices: a Surround, a Room
// with its amount, and a Stage of Off, Focus or Widen (plus the upmix's
// steering). Picking one therefore selects exactly those rows underneath,
// rather than leaving Stage reading "Custom" for values no row can show.
//
// Every preset was measured before it went in: rendered through the DSP chain
// on six programmes (synthetic pop, ballad, electronic and speech, the NativeDSD
// Shostakovich excerpt and the demo loop) against the output's Reference. See
// SpacePresetLibraryTests for the limits they hold.

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
    public let stage: SpaceStage
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
        let v = stage.values(headphone: headphone)
        guard near(p.spatialWidth, v.width), near(p.centerFocus, v.centerFocus),
              near(p.roomReduce, v.roomReduce) else { return false }
        if crossfeedRendered, !near(p.crossfeed, v.crossfeed) { return false }
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
                             room: Double = 0, amount: Double = 50, stage: SpaceStage = .off,
                             centerWidth: Double = 100, depth: Double = 50) -> SpacePreset {
        SpacePreset(id: id, headphone: headphone, group: group,
                    names: (en: names.0, ja: names.1, fr: names.2, de: names.3),
                    surround: surround, roomType: room, roomAmount: amount, stage: stage,
                    centerWidth: centerWidth, surroundDepth: depth)
    }

    // Measured on six programmes against Reference (2026-09-17), all 30 as
    // they stand here:
    //   - Level within -0.23...+0.99 dB and 1/3-octave colour within 3.7 dB,
    //     except the 7.1 layout itself (5.1 dB on the demo loop).
    //   - Where two presets share a layout and a stage, their rooms differ in
    //     type or their reverb tails by at least 3 dB.
    //   - Focus only in the speech presets (Voice, Podcast Booth, Meeting Room,
    //     TV Show, Dialogue 5.1). It pulls the image to the centre and lifts it:
    //     +2.6 to +2.8 dB and at most 3.6 dB of colour on speech, which is the
    //     point, but up to 15 dB in one band of a wide mix.
    //   - Widen is in none: +3.2 to +3.7 dB louder and into the limiter. Wide
    //     surround does that job at +0.7 dB.
    //   - Dropped for being the same sound: a second hall at 85 (1.7 dB from
    //     70), Wide over a hall at 85 (2.2 dB from Wide alone), and 7.1 with
    //     other steering (IACC apart by more than 0.075 on one programme of six).
    static let speakers: [SpacePreset] = [
        make("sp-reference", false, .everyday, ("Reference", "リファレンス", "Référence", "Referenz")),
        make("sp-voice", false, .everyday, ("Voice", "ボイス", "Voix", "Stimme"), stage: .focus),
        make("sp-podcast", false, .everyday, ("Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"),
             room: 1, amount: 100, stage: .focus),
        make("sp-meeting", false, .everyday, ("Meeting Room", "会議室", "Salle de réunion", "Besprechungsraum"),
             room: 2, amount: 50, stage: .focus),
        make("sp-living", false, .everyday, ("Living Room", "リビングルーム", "Salon", "Wohnzimmer"), room: 2, amount: 50),
        make("sp-studio", false, .music, ("Studio", "スタジオ", "Studio", "Studio"), room: 1, amount: 100),
        make("sp-lounge", false, .music, ("Lounge", "ラウンジ", "Lounge", "Lounge"), room: 2, amount: 80),
        make("sp-ambience", false, .music, ("Ambience", "アンビエンス", "Ambiance", "Ambiente"), surround: .ambience),
        make("sp-chamber", false, .music, ("Chamber Hall", "室内楽ホール", "Salle de chambre", "Kammersaal"), room: 3, amount: 40),
        make("sp-concert", false, .music, ("Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"), room: 3, amount: 70),
        make("sp-gaming", false, .cinema, ("Gaming", "ゲーム", "Jeu", "Gaming"), surround: .virtual51, room: 1, amount: 100),
        make("sp-hometheater", false, .cinema, ("Home Theater", "ホームシアター", "Home cinéma", "Heimkino"),
             surround: .virtual51, room: 2, amount: 65),
        make("sp-widestage", false, .cinema, ("Wide Stage", "ワイドステージ", "Scène large", "Breite Bühne"), surround: .virtual51),
        make("sp-cinema", false, .cinema, ("Cinema", "シネマ", "Cinéma", "Kino"), surround: .virtual51, room: 3, amount: 40),
        make("sp-tvshow", false, .cinema, ("TV Show", "テレビ番組", "Émission TV", "TV-Show"), surround: .ambience, stage: .focus),
    ]

    static let headphones: [SpacePreset] = [
        make("hp-reference", true, .everyday, ("Reference", "リファレンス", "Référence", "Referenz")),
        make("hp-voice", true, .everyday, ("Voice", "ボイス", "Voix", "Stimme"), stage: .focus),
        make("hp-podcast", true, .everyday, ("Podcast Booth", "ポッドキャストブース", "Cabine podcast", "Podcast-Kabine"),
             room: 1, amount: 50, stage: .focus),
        make("hp-living", true, .everyday, ("Living Room", "リビングルーム", "Salon", "Wohnzimmer"), room: 2, amount: 40),
        make("hp-ambience", true, .everyday, ("Ambience", "アンビエンス", "Ambiance", "Ambiente"), surround: .ambience),
        make("hp-studio", true, .music, ("Studio", "スタジオ", "Studio", "Studio"), room: 1, amount: 80),
        make("hp-lounge", true, .music, ("Lounge", "ラウンジ", "Lounge", "Lounge"), room: 2, amount: 60),
        make("hp-concert", true, .music, ("Concert Hall", "コンサートホール", "Salle de concert", "Konzertsaal"), room: 3, amount: 35),
        make("hp-surroundmusic", true, .music, ("Surround Music", "サラウンドミュージック", "Musique surround", "Surround-Musik"),
             surround: .virtual51),
        make("hp-orchestra", true, .music, ("Orchestra", "オーケストラ", "Orchestre", "Orchester"),
             surround: .virtual51, room: 3, amount: 50),
        make("hp-hometheater", true, .cinema, ("Home Theater", "ホームシアター", "Home cinéma", "Heimkino"),
             surround: .virtual51, room: 1, amount: 50),
        make("hp-cinema71", true, .cinema, ("Cinema 7.1", "シネマ 7.1", "Cinéma 7.1", "Kino 7.1"), surround: .virtual71),
        make("hp-living71", true, .cinema, ("Living Room 7.1", "リビングルーム 7.1", "Salon 7.1", "Wohnzimmer 7.1"),
             surround: .virtual71, room: 2, amount: 40),
        make("hp-gaming71", true, .cinema, ("Gaming 7.1", "ゲーム 7.1", "Jeu 7.1", "Gaming 7.1"),
             surround: .virtual71, room: 1, amount: 40, depth: 80),
        make("hp-dialogue51", true, .cinema, ("Dialogue 5.1", "ダイアログ 5.1", "Dialogues 5.1", "Dialog 5.1"),
             surround: .virtual51, stage: .focus),
    ]
}
