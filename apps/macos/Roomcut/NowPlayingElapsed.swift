import Foundation
import AppKit
import Combine
import ImageIO
import RoomcutCore
import RoomcutPresentationCore

// Elapsed time between stream updates: the helper reports a position now and
// then, and this keeps a smooth clock running in between.
extension NowPlayingMonitor {
    // MARK: Elapsed estimation

    func rebaseline(from snap: Snapshot) {
        // elapsedTime was current at snap.timestamp; advance to now if playing.
        clock.rebaseline(elapsed: snap.elapsedTime, at: snap.timestamp)
        elapsedNow = currentElapsed(snap)
    }

    func currentElapsed(_ snap: Snapshot) -> Double {
        clock.position(playing: snap.playing, rate: snap.playbackRate, duration: snap.duration)
    }

    func startTicker() {
        ticker?.invalidate()
        ticker = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self, let snap = self.snapshot, snap.playing else { return }
                self.elapsedNow = self.currentElapsed(snap)
            }
        }
    }
}
