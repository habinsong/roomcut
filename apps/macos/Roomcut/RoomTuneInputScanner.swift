//
// RoomTuneInputScanner.swift — Phase 1 of iPhone Room Tune.
//
// Finds an iPhone Continuity microphone among the Mac's audio INPUT devices.
// Roomcut never controls the iPhone: the phone is exposed to macOS as an input
// device (Continuity Camera / Microphone), and we just enumerate and match it.
// Enumeration needs no microphone permission — only capture (Phase 2) does.
//
import AVFoundation
import Combine

enum RoomTuneInputState: Equatable {
    case notFound
    case detected(name: String)
}

@MainActor
final class RoomTuneInputScanner: ObservableObject {
    @Published private(set) var state: RoomTuneInputState = .notFound
    /// The matched capture device, handed to the recorder. Set alongside `state`.
    private(set) var device: AVCaptureDevice?

    /// Re-scan the Mac's audio input devices and match an iPhone Continuity mic.
    func refresh() {
        let session = AVCaptureDevice.DiscoverySession(
            deviceTypes: [.microphone, .external],   // Continuity mic shows as .external
            mediaType: .audio,
            position: .unspecified
        )
        let match = session.devices.first { Self.looksLikeIPhoneMic($0.localizedName) }
        device = match
        state = match.map { .detected(name: $0.localizedName) } ?? .notFound
    }

    /// Name-based match — "iPhone", "Continuity", or the Korean "연속성".
    static func looksLikeIPhoneMic(_ localizedName: String) -> Bool {
        let n = localizedName.lowercased()
        return n.contains("iphone") || n.contains("continuity") || n.contains("연속성")
    }
}
