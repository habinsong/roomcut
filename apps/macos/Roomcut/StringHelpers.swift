import Foundation

// Shared text helpers. `nonEmpty` existed as a private copy in three different
// files, which is the sort of duplication that quietly drifts apart; one
// definition is enough.
public extension String {
    // Treats an empty string as missing, so `title.nonEmpty ?? fallback` reads
    // the way the UI actually behaves.
    var nonEmpty: String? { isEmpty ? nil : self }
}
