import Foundation

public struct DeviceControlReadback {
    public let audioFormat: AudioFormatInfo?
    public let formatOptions: [DeviceFormatOption]
    public let volume: Double?
    public let balance: Double?

    public init(audioFormat: AudioFormatInfo? = nil, formatOptions: [DeviceFormatOption] = [],
                volume: Double? = nil, balance: Double? = nil) {
        self.audioFormat = audioFormat; self.formatOptions = formatOptions
        self.volume = volume; self.balance = balance
    }
}

// nil sections were not requested; an empty list or nil control value is a
// completed query reporting no device/capability. No writes occur while reading.
public struct DeviceReadback {
    public let roomcutIsDefault: Bool
    public let devices: [OutputDeviceChoice]?
    public let controls: DeviceControlReadback?

    public init(roomcutIsDefault: Bool, devices: [OutputDeviceChoice]? = nil, controls: DeviceControlReadback? = nil) {
        self.roomcutIsDefault = roomcutIsDefault; self.devices = devices; self.controls = controls
    }

    static func capture(using client: EngineClientProtocol, uid: String, devices: Bool, controls: Bool) -> Self {
        let isDefault = client.roomcutIsDefaultOutput()
        let outputs = devices ? client.outputDevices() : nil
        let values: DeviceControlReadback?
        if controls {
            values = DeviceControlReadback(
                audioFormat: uid.isEmpty ? nil : client.audioFormat(for: uid),
                formatOptions: uid.isEmpty ? [] : client.deviceFormatOptions(for: uid),
                volume: client.volumeGet(), balance: client.balanceGet())
        } else { values = nil }
        return Self(roomcutIsDefault: isDefault, devices: outputs, controls: values)
    }
}
