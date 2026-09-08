import XCTest
@testable import RoomcutCore

@MainActor
final class LocalizationTests: XCTestCase {
    private var savedPreference: AppLanguage!

    override func setUp() {
        super.setUp()
        savedPreference = AppLanguage.preference
    }

    override func tearDown() {
        AppLanguage.preference = savedPreference
        super.tearDown()
    }

    func testLResolvesPerEffectiveLanguage() {
        let cases: [(AppLanguage, String)] = [
            (.korean, "한국어"), (.english, "English"), (.japanese, "日本語"),
            (.french, "Français"), (.german, "Deutsch"),
        ]
        for (lang, expected) in cases {
            AppLanguage.preference = lang
            XCTAssertEqual(L("한국어", "English", "日本語", "Français", "Deutsch"), expected)
        }
    }

    func testEffectiveFollowsSystemWhenAuto() {
        AppLanguage.preference = .auto
        // `effective` must resolve to a concrete language, never `.auto`.
        XCTAssertNotEqual(AppLanguage.effective, .auto)
        XCTAssertEqual(AppLanguage.effective, AppLanguage.system)
        XCTAssertNotEqual(AppLanguage.system, .auto)
    }

    func testSetLanguageUpdatesPreferenceAndPersists() {
        let client = FakeEngineClient()
        let model = RoomcutViewModel(client: client, debounceNanoseconds: 1_000_000)

        model.setLanguage(.japanese)

        XCTAssertEqual(model.language, .japanese)
        XCTAssertEqual(AppLanguage.preference, .japanese)
        XCTAssertEqual(UserDefaults.standard.string(forKey: "com.roomcut.language"), "japanese")

        // A fresh model restores the persisted choice and re-mirrors the static.
        let reloaded = RoomcutViewModel(client: FakeEngineClient(), debounceNanoseconds: 1_000_000)
        XCTAssertEqual(reloaded.language, .japanese)
        XCTAssertEqual(AppLanguage.preference, .japanese)

        // Clean up so the persisted key doesn't leak into other test runs.
        UserDefaults.standard.removeObject(forKey: "com.roomcut.language")
    }
}
