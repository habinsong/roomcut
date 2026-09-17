import Foundation

// A UserDefaults domain for one test, named after the test and emptied before
// and after it. Named rather than random: an emptied domain is written back to
// disk by cfprefsd some time later, so a random suite per run left a plist in
// ~/Library/Preferences every time (2,220 by 2026-09-17, and deleting the file
// in tearDown lost the race). A name per test reuses the same file.
func makeTestDefaults(_ test: String) -> (suite: String, defaults: UserDefaults) {
    let suite = "roomcut-tests-" + String(test.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) }.map(Character.init))
    let defaults = UserDefaults(suiteName: suite)!
    defaults.removePersistentDomain(forName: suite)
    return (suite, defaults)
}
