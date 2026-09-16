import CoreAudio
import Foundation

// CoreAudio tells us the moment a device appears or disappears; polling only
// notices on its next turn. Connecting headphones and then hunting for them in
// a list that is up to five seconds stale reads as "the app doesn't see them",
// so the list follows the hardware instead of a timer.
//
// The listener lives in the app process (the same place the device list is
// read from), and the callback is delivered on the main queue.
final class DeviceListWatcher: @unchecked Sendable {
    private let lock = NSLock()
    private var registered: [(AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []
    private let onChange: @Sendable () -> Void

    init(onChange: @escaping @Sendable () -> Void) {
        self.onChange = onChange
    }

    deinit { stop() }

    // The device list itself and the system's default output: a Bluetooth pair
    // that connects moves both, and either one means the list is out of date.
    private static let selectors: [AudioObjectPropertySelector] = [
        kAudioHardwarePropertyDevices,
        kAudioHardwarePropertyDefaultOutputDevice,
    ]

    func start() {
        lock.lock()
        let alreadyRunning = !registered.isEmpty
        lock.unlock()
        guard !alreadyRunning else { return }

        var added: [(AudioObjectPropertyAddress, AudioObjectPropertyListenerBlock)] = []
        for selector in Self.selectors {
            var address = AudioObjectPropertyAddress(mSelector: selector,
                                                     mScope: kAudioObjectPropertyScopeGlobal,
                                                     mElement: kAudioObjectPropertyElementMain)
            let handler = onChange
            let block: AudioObjectPropertyListenerBlock = { _, _ in handler() }
            let status = AudioObjectAddPropertyListenerBlock(
                AudioObjectID(kAudioObjectSystemObject), &address, DispatchQueue.main, block)
            if status == noErr { added.append((address, block)) }
        }
        lock.lock()
        registered.append(contentsOf: added)
        lock.unlock()
    }

    func stop() {
        lock.lock()
        let pending = registered
        registered.removeAll()
        lock.unlock()
        for (address, block) in pending {
            var address = address
            AudioObjectRemovePropertyListenerBlock(
                AudioObjectID(kAudioObjectSystemObject), &address, DispatchQueue.main, block)
        }
    }
}
