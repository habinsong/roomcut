import XCTest
@testable import RoomcutCore

@MainActor
final class PresetStoreTests: XCTestCase {
    func testUnsupportedArchiveCannotPartiallyImport() async throws {
        let (suite, defaults) = makeTestDefaults(name)
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = PresetStore(defaults: defaults)
        let original = SavedPreset(name: "Original", preampDb: -2, eqGainsDb: [0], outputGainDb: 0)
        store.save(original)
        let data = try JSONEncoder().encode(PresetArchive(version: 99, presets: [
            SavedPreset(name: "Other", preampDb: 0, eqGainsDb: [0], outputGainDb: 0)
        ]))
        XCTAssertNil(store.importData(data))
        XCTAssertEqual(store.presets, [original])
        XCTAssertEqual(PresetStore(defaults: defaults).presets, [original])
    }

    func testImportNormalizesArraysAndDeleteRemovesCaseVariantMappings() async throws {
        let (suite, defaults) = makeTestDefaults(name)
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = PresetStore(defaults: defaults)
        store.setAutoApply(true)
        store.remember("saved:Mine", device: "A")
        let preset = SavedPreset(name: "mine", preampDb: 40, eqGainsDb: [50], outputGainDb: -100, builtin: true)
        let data = try JSONEncoder().encode([preset])
        XCTAssertEqual(store.importData(data), 1)
        let normalized = try XCTUnwrap(store.presets.first)
        XCTAssertFalse(normalized.builtin)
        XCTAssertEqual(normalized.eqGainsDb.count, 10)
        XCTAssertEqual(normalized.eqGainsDb[0], 24)
        XCTAssertEqual(normalized.preampDb, 12)
        store.delete(normalized)
        XCTAssertTrue(store.deviceMap.isEmpty)
        XCTAssertTrue(PresetStore(defaults: defaults).presets.isEmpty)
    }

    // "Harman Target" was renamed. A selection an older build stored under the
    // old name still resolves, and comes back spelled the new way so the picker
    // highlights it.
    func testARenamedLibraryPresetKeepsAnOlderSelection() async throws {
        let old = "library:Headphones/Harman Target"
        let renamed = try XCTUnwrap(PresetLibrary.preset(for: old))
        XCTAssertEqual(renamed.name, "Neutral Target")
        XCTAssertEqual(PresetLibrary.currentToken(old), PresetLibrary.token(for: renamed))
        XCTAssertEqual(PresetLibrary.currentToken("library:Signature/Flat"), "library:Signature/Flat")
        XCTAssertEqual(PresetLibrary.currentToken("night"), "night")
        XCTAssertFalse(PresetLibrary.all.contains { $0.name.localizedCaseInsensitiveContains("Harman") })

        let (suite, defaults) = makeTestDefaults(name)
        defer { defaults.removePersistentDomain(forName: suite) }
        PresetStore(defaults: defaults).recordSelection(saved: nil, builtin: old)
        let model = RoomcutViewModel(client: FakeEngineClient(), defaults: defaults)
        XCTAssertEqual(model.activeBuiltinPresetId, PresetLibrary.token(for: renamed))
    }
}
