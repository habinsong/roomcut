//
// RoomTuneSweep.swift — Phase 2 test signal for iPhone Room Tune.
//
// An exponential (log) sine sweep 20 Hz → 20 kHz with short raised-cosine fades.
// A log sweep spends more time in the low end (where room modes live) and is the
// standard room-measurement excitation. Mono content duplicated to L/R.
//
import AVFoundation

enum RoomTuneSweep {
    static let sampleRate: Double = 48_000
    static let duration: Double = 6.0
    static let f0: Double = 20
    static let f1: Double = 20_000
    static let amplitude: Float = 0.5
    static let fade: Double = 0.02   // 20 ms in/out

    /// Build the stereo (duplicated-mono) sweep buffer, or nil on allocation failure.
    static func makeBuffer() -> AVAudioPCMBuffer? {
        guard let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2),
              let buffer = AVAudioPCMBuffer(pcmFormat: format,
                                            frameCapacity: AVAudioFrameCount(frameCount)) else {
            return nil
        }
        buffer.frameLength = AVAudioFrameCount(frameCount)
        guard let channels = buffer.floatChannelData else { return nil }
        let left = channels[0], right = channels[1]
        let k = duration / log(f1 / f0)
        let fadeN = Int(sampleRate * fade)
        let n = frameCount
        for i in 0..<n {
            let t = Double(i) / sampleRate
            // Instantaneous phase of an exponential sweep.
            let phase = 2.0 * .pi * f0 * k * (exp(t / k) - 1.0)
            var s = Float(sin(phase)) * amplitude
            if i < fadeN {
                s *= Float(i) / Float(fadeN)
            } else if i >= n - fadeN {
                s *= Float(n - i) / Float(fadeN)
            }
            left[i] = s
            right[i] = s
        }
        return buffer
    }

    static var frameCount: Int { Int(sampleRate * duration) }
}
