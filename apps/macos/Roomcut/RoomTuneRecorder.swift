//
// RoomTuneRecorder.swift — Phase 2 (step 2): capture the iPhone mic to a WAV.
//
// Records the chosen input device (the iPhone Continuity mic) via AVCaptureSession
// and writes mono float WAV. Lives OFF the main actor — the capture callback runs on
// its own queue; the owner reads `peakLevel` for UI and gets the file URL from stop().
//
// Note on AGC: Continuity-mic audio is processed on the iPhone, so the Mac can't turn
// off automatic gain control here (no public API). Room Tune compensates by being
// conservative (cut-only, limited gain) rather than relying on a flat mic response.
//
import AVFoundation

enum RoomTuneError: Error { case cannotAddInput, cannotAddOutput }

final class RoomTuneRecorder: NSObject, AVCaptureAudioDataOutputSampleBufferDelegate {
    private let session = AVCaptureSession()
    private let output = AVCaptureAudioDataOutput()
    private let queue = DispatchQueue(label: "com.roomcut.roomtune.capture")
    private var file: AVAudioFile?
    private var url: URL?
    private let lock = NSLock()
    private var _peak: Float = 0

    var peakLevel: Float { lock.lock(); defer { lock.unlock() }; return _peak }

    /// Start capturing `device` to a WAV at `url`. The file is created from the first
    /// buffer's real format. Throws on session setup failure.
    func start(device: AVCaptureDevice, to url: URL) throws {
        session.beginConfiguration()
        session.inputs.forEach { session.removeInput($0) }
        session.outputs.forEach { session.removeOutput($0) }
        let input = try AVCaptureDeviceInput(device: device)
        guard session.canAddInput(input) else { throw RoomTuneError.cannotAddInput }
        session.addInput(input)
        output.setSampleBufferDelegate(self, queue: queue)
        guard session.canAddOutput(output) else { throw RoomTuneError.cannotAddOutput }
        session.addOutput(output)
        session.commitConfiguration()
        self.url = url
        file = nil
        lock.lock(); _peak = 0; lock.unlock()
        session.startRunning()
    }

    /// Stop capturing; returns the written WAV URL (nil if nothing was recorded).
    func stop() -> URL? {
        session.stopRunning()
        let result = (file != nil) ? url : nil
        file = nil
        return result
    }

    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        guard let pcm = Self.monoBuffer(from: sampleBuffer) else { return }
        if file == nil, let url {
            file = try? AVAudioFile(forWriting: url, settings: pcm.format.settings)
        }
        try? file?.write(from: pcm)
        if let ch = pcm.floatChannelData?[0] {
            var p: Float = 0
            for i in 0..<Int(pcm.frameLength) { p = max(p, abs(ch[i])) }
            lock.lock(); _peak = p; lock.unlock()
        }
    }

    /// CMSampleBuffer → mono float32 buffer (channel 0). Handles float or int16,
    /// interleaved input — the common Continuity-mic / CoreAudio formats.
    private static func monoBuffer(from sampleBuffer: CMSampleBuffer) -> AVAudioPCMBuffer? {
        guard let fd = CMSampleBufferGetFormatDescription(sampleBuffer),
              let asbd = CMAudioFormatDescriptionGetStreamBasicDescription(fd)?.pointee else { return nil }
        let frames = CMSampleBufferGetNumSamples(sampleBuffer)
        guard frames > 0,
              let format = AVAudioFormat(standardFormatWithSampleRate: asbd.mSampleRate, channels: 1),
              let out = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frames)),
              let dst = out.floatChannelData?[0] else { return nil }
        out.frameLength = AVAudioFrameCount(frames)

        var blockBuffer: CMBlockBuffer?
        var abl = AudioBufferList()
        let status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sampleBuffer, bufferListSizeNeededOut: nil, bufferListOut: &abl,
            bufferListSize: MemoryLayout<AudioBufferList>.size,
            blockBufferAllocator: nil, blockBufferMemoryAllocator: nil,
            flags: kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
            blockBufferOut: &blockBuffer)
        guard status == noErr, let mData = abl.mBuffers.mData else { return nil }
        let ch = max(1, Int(asbd.mChannelsPerFrame))
        let isFloat = (asbd.mFormatFlags & kAudioFormatFlagIsFloat) != 0
        if isFloat {
            let src = mData.assumingMemoryBound(to: Float.self)
            for i in 0..<frames { dst[i] = src[i * ch] }
        } else {
            let src = mData.assumingMemoryBound(to: Int16.self)
            for i in 0..<frames { dst[i] = Float(src[i * ch]) / 32768.0 }
        }
        return out
    }
}
