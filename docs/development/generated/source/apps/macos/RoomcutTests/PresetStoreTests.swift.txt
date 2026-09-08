import XCTest
@testable import RoomcutCore

@MainActor
final class PresetStoreTests: XCTestCase {
    func testUnsupportedArchiveCannotPartiallyImport() async throws {
        let suite = "roomcut-store-tests-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
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
        let suite = "roomcut-store-tests-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
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
}
