import Foundation

// Shared excitation for playback and transfer-response analysis.
public enum RoomTuneSignal {
    public static let duration = 6.0
    public static let startHz = 20.0
    public static let endHz = 20_000.0
    public static let amplitude: Float = 0.5
    public static let fade = 0.02

    public static func samples(sampleRate: Double) -> [Float] {
        let count = Int(sampleRate * duration)
        let fadeCount = max(1, Int(sampleRate * fade))
        let k = duration / log(endHz / startHz)
        return (0..<count).map { index in
            let t = Double(index) / sampleRate
            let phase = 2 * Double.pi * startHz * k * (exp(t / k) - 1)
            let envelope = min(1, min(Float(index), Float(count - index)) / Float(fadeCount))
            return Float(sin(phase)) * amplitude * envelope
        }
    }
}
