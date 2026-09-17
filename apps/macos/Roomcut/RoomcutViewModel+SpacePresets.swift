import Foundation

// Space presets (SpacePresetLibrary): applying one, telling which one is on, and
// keeping a choice per output so Speaker and Headphone each come back to their own.
extension RoomcutViewModel {
    public var spacePresets: [SpacePreset] { SpacePresetLibrary.presets(headphone: spatialOutputIsHeadphone) }

    // The preset the current settings are, or nil once anything was moved off it.
    public var activeSpacePreset: SpacePreset? {
        let parameters = editor.snapshot.parameters
        let crossfeedRendered = !spatialOutputIsHeadphone || !(headTrackingActive || surroundType >= 2)
        return spacePresets.first { $0.matches(parameters, crossfeedRendered: crossfeedRendered) }
    }

    // One edit, one push: the whole scene changes together, and it is one step
    // to undo.
    public func applySpacePreset(_ preset: SpacePreset) {
        guard ensureSpatialAvailable(), preset.headphone == spatialOutputIsHeadphone else { return }
        // An engine without the upmix has only Off and Ambience to offer.
        let surround = surroundChoices.contains(preset.surround) ? preset.surround : .off
        editor.update {
            $0.parameters.spatialMode = Self.encodeSpatialMode(headphone: preset.headphone,
                                                               surround: surround == .ambience)
            $0.parameters.surroundType = surround == .virtual71 ? 3 : (surround == .virtual51 ? 2 : 0)
            $0.parameters.roomType = preset.roomType
            $0.parameters.roomAmount = preset.roomAmount
            let stage = preset.stage.values(headphone: preset.headphone)
            $0.parameters.spatialWidth = stage.width
            $0.parameters.centerFocus = stage.centerFocus
            $0.parameters.crossfeed = stage.crossfeed
            $0.parameters.roomReduce = stage.roomReduce
            $0.parameters.centerWidth = preset.centerWidth
            $0.parameters.surroundDepth = preset.surroundDepth
        }
        preferences.setSpacePresetID(preset.id, headphone: preset.headphone)
        schedulePushParams(preservingPresetSelection: true)
    }

    // Speaker ↔ Headphone. What the output being left was playing is remembered
    // (a preset, or nothing if it had been adjusted), and the output being
    // entered gets back what it had. Returns whether a preset was applied, so
    // the caller can fall back to carrying the current settings across.
    @discardableResult
    public func switchSpatialOutput(headphone: Bool) -> Bool {
        guard headphone != spatialOutputIsHeadphone else { return false }
        preferences.setSpacePresetID(activeSpacePreset?.id, headphone: spatialOutputIsHeadphone)
        setSpatialOutput(headphone: headphone)
        guard let id = preferences.spacePresetID(headphone: headphone),
              let preset = SpacePresetLibrary.preset(id: id), preset.headphone == headphone else { return false }
        applySpacePreset(preset)
        return true
    }
}
