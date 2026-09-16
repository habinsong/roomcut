import Foundation
import CRoomcutClient
import RoomcutPresentationCore

// The small value types the UI picks from: a built-in preset, an output device,
// and the format that device is running. All plain data — no engine calls.
public struct EnginePreset: Identifiable, Hashable {
    public let id: String
    public let name: String

    public init(id: String, name: String) {
        self.id = id
        self.name = name
    }
}

public struct OutputDeviceChoice: Identifiable, Hashable {
    public let uid: String
    public let name: String
    public var id: String { uid }

    public init(uid: String, name: String) {
        self.uid = uid
        self.name = name
    }
}

public struct AudioFormatInfo: Equatable, Sendable {
    public let bitDepth: Int
    public let sampleRate: Double
    public let latencyMs: Double

    public init(bitDepth: Int, sampleRate: Double, latencyMs: Double) {
        self.bitDepth = bitDepth
        self.sampleRate = sampleRate
        self.latencyMs = latencyMs
    }
}

// One physical format (sample rate + bit depth) the real output device supports.
public struct DeviceFormatOption: Equatable, Hashable, Sendable {
    public let sampleRate: Double
    public let bitDepth: Int

    public init(sampleRate: Double, bitDepth: Int) {
        self.sampleRate = sampleRate
        self.bitDepth = bitDepth
    }
}
