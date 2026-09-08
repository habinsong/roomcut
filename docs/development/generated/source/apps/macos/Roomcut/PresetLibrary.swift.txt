import Foundation

// App-shipped preset library — curated, folder-grouped, FULL-state presets (each a
// SavedPreset so it carries the same fields a user save does). Applied via setParams
// like any saved preset; `builtin` blocks deletion/overwrite. Curves are 10-band
// voicings informed by each device/category's known tendencies (consumer gear is
// bass-lifted & treble-lively, studio monitors near-flat with a gentle HF tilt).
public enum PresetLibrary {
    public static let folderOrder = ["Signature", "Apple", "Speakers", "Headphones", "My Presets"]
    public static let all: [SavedPreset] = signature + apple + speakers + headphones

    public static func token(for preset: SavedPreset) -> String { "library:" + preset.id }
    static func preset(for token: String) -> SavedPreset? {
        guard token.hasPrefix("library:") else { return nil }
        return all.first { $0.id == String(token.dropFirst(8)) }
    }

    // bands: 31 62 125 250 500 1k 2k 4k 8k 16k
    private static func p(_ name: String, _ folder: String, _ g: [Double],
                          preamp: Double = 0, width: Double = 0,
                          crossfeed: Double = 0, roomReduce: Double = 0) -> SavedPreset {
        SavedPreset(name: name, preampDb: preamp, eqGainsDb: g, outputGainDb: 0,
                    spatialWidth: width, centerFocus: 0, crossfeed: crossfeed, roomReduce: roomReduce,
                    folder: folder, builtin: true)
    }

    static let signature: [SavedPreset] = [
        p("Flat",        "Signature", [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]),
        p("Warm",        "Signature", [2, 2.5, 1.5, 0.5, 0, -0.5, -1, -1.5, -1, -0.5]),
        p("Bright",      "Signature", [-1, -0.5, 0, 0, 0, 0.5, 1, 2, 2.5, 2]),
        p("Bass Boost",  "Signature", [5, 5.5, 3, 1, 0, 0, 0, 0, 0, 0], preamp: -3),
        p("Vocal Boost", "Signature", [0, 0, -0.5, 0, 1, 2.5, 2.5, 1.5, 0, 0]),
        p("Loudness",    "Signature", [4, 4, 2, 0, -0.5, -1, -0.5, 0, 2, 3], preamp: -2.5),
        p("Speech",      "Signature", [-4, -3, -1, 1, 2, 3, 2.5, 1.5, 0, -1]),
        p("Soft",        "Signature", [-1, -1, -0.5, 0, 0, -0.5, -1, -1.5, -1.5, -1]),
    ]

    static let apple: [SavedPreset] = [
        p("MacBook Pro Speakers", "Apple", [4, 4, 2, 0.5, 0, -0.5, -1, -1, 0.5, 1], preamp: -2),
        p("MacBook Air Speakers", "Apple", [5, 5, 3, 1, 0, -0.5, -1, -1, 1, 1.5], preamp: -2.5),
        p("AirPods Pro",          "Apple", [1.5, 1, 0.5, 0, 0, 0, -0.5, -1, -1.5, -0.5]),
        p("AirPods Max",          "Apple", [1, 0.5, 0, 0, 0, 0, 0.5, 1, 1, 0.5]),
        p("AirPods (3rd gen)",    "Apple", [5, 4, 2, 0.5, 0, 0, 0, 0, 1, 1], preamp: -2.5),
        p("Beats",                "Apple", [-2, -1, 0, 0, 0.5, 1, 1, 0.5, 0, 0]),
    ]

    static let speakers: [SavedPreset] = [
        p("Studio Monitors",   "Speakers", [0, 0, 0, 0, 0, 0, -0.5, -1, -1.5, -2]),
        p("Bookshelf",         "Speakers", [1.5, 2, 1, 0, 0, 0, 0.5, 1, 1, 0.5]),
        p("TV Speakers",       "Speakers", [2, 2, 1, 1, 1.5, 2.5, 2, 0.5, -0.5, -1], preamp: -1),
        p("Soundbar",          "Speakers", [3, 3, 1.5, 0, 0, 1, 1.5, 1, 1.5, 2], preamp: -1.5),
        p("Bluetooth Speaker", "Speakers", [3, 2, -1, -1, 0, 1, 1.5, 1, 1.5, 1], preamp: -1.5),
        p("Car",               "Speakers", [4, 3, 1, 0, -1, 0, 1, 2, 2, 1], preamp: -2),
        p("Cinema",            "Speakers", [3, 3, 1.5, 0, 0, 1.5, 1.5, 0.5, 1, 1.5],
          preamp: -1.5, width: 40, crossfeed: 0, roomReduce: 20),
    ]

    static let headphones: [SavedPreset] = [
        p("Harman Target", "Headphones", [4, 3.5, 1.5, 0, 0, 0, 1, 0.5, -1, -2], preamp: -2),
        p("Open-Back",     "Headphones", [2, 2, 1, 0, 0, 0, 0.5, 1, 1.5, 1]),
        p("Closed-Back",   "Headphones", [3, 3, 1, -0.5, 0, 0.5, 1, 0.5, -0.5, -1], preamp: -1),
        p("In-Ear (IEM)",  "Headphones", [4, 3, 1, 0, 0, 0.5, 1.5, 1, 0, -1], preamp: -1.5),
    ]
}
