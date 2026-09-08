import AVFoundation

// Serialized by the recorder's capture queue. Reuse the format converter while
// the input format is unchanged; the selected microphone's first channel is kept.
public final class RoomTunePCM {
    private var converter: AVAudioConverter?

    public init() {}

    public func firstChannel(from sampleBuffer: CMSampleBuffer) -> AVAudioPCMBuffer? {
        guard CMSampleBufferIsValid(sampleBuffer), CMSampleBufferDataIsReady(sampleBuffer),
              let description = CMSampleBufferGetFormatDescription(sampleBuffer),
              var asbd = CMAudioFormatDescriptionGetStreamBasicDescription(description)?.pointee,
              asbd.mFormatID == kAudioFormatLinearPCM, asbd.mSampleRate.isFinite,
              (8000...192000).contains(asbd.mSampleRate), asbd.mChannelsPerFrame > 0,
              asbd.mBytesPerFrame > 0, asbd.mFramesPerPacket == 1 else { return nil }
        let frames = CMSampleBufferGetNumSamples(sampleBuffer)
        guard frames > 0, frames <= Int(Int32.max), let data = CMSampleBufferGetDataBuffer(sampleBuffer) else { return nil }
        let planes = asbd.mFormatFlags & kAudioFormatFlagIsNonInterleaved != 0 ? Int(asbd.mChannelsPerFrame) : 1
        let (planeBytes, planeOverflow) = frames.multipliedReportingOverflow(by: Int(asbd.mBytesPerFrame))
        let (requiredBytes, totalOverflow) = planeBytes.multipliedReportingOverflow(by: planes)
        guard !planeOverflow, !totalOverflow, requiredBytes <= CMBlockBufferGetDataLength(data),
              let format = AVAudioFormat(streamDescription: &asbd),
              let input = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frames)),
              let outputFormat = AVAudioFormat(standardFormatWithSampleRate: asbd.mSampleRate, channels: 1),
              let output = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: AVAudioFrameCount(frames)) else { return nil }
        input.frameLength = AVAudioFrameCount(frames)
        guard CMSampleBufferCopyPCMDataIntoAudioBufferList(sampleBuffer, at: 0, frameCount: Int32(frames),
                                                          into: input.mutableAudioBufferList) == noErr else { return nil }
        if converter?.inputFormat != format {
            converter = AVAudioConverter(from: format, to: outputFormat)
            converter?.channelMap = [0]
        }
        guard let converter else { return nil }
        do { try converter.convert(to: output, from: input) }
        catch { return nil }
        guard output.frameLength == AVAudioFrameCount(frames), let samples = output.floatChannelData?[0],
              UnsafeBufferPointer(start: samples, count: frames).allSatisfy(\.isFinite) else { return nil }
        return output
    }
}
