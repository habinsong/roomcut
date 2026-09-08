import Foundation

// Synchronous media setup/teardown and file work share one non-UI queue.
// Already submitted work finishes even if its caller is cancelled, so cleanup
// can be ordered behind an in-flight native start rather than racing with it.
public final class RoomTuneWorkQueue {
    private let queue = DispatchQueue(label: "com.roomcut.roomtune.media", qos: .userInitiated)
    public init() {}

    public func perform<T>(_ work: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            queue.async {
                do { continuation.resume(returning: try work()) }
                catch { continuation.resume(throwing: error) }
            }
        }
    }
}
