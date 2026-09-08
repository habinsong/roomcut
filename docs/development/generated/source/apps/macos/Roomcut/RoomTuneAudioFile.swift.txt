import AVFoundation

// File decoding and sample-rate conversion are kept out of the response math.
public enum RoomTuneAudioFile {
    public static func load(_ url: URL) throws -> (samples: [Float], sampleRate: Double) {
        let file = try AVAudioFile(forReading: url)
        let format = file.processingFormat
        guard format.sampleRate.isFinite, (8000...192000).contains(format.sampleRate),
              file.length > 0, Double(file.length) <= format.sampleRate * 20 else {
            throw RoomTuneResponseError.unsupportedFormat
        }
        guard let outputFormat = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 1),
              let converter = AVAudioConverter(from: format, to: outputFormat),
              let output = AVAudioPCMBuffer(pcmFormat: outputFormat, frameCapacity: 4096) else {
            throw RoomTuneResponseError.unsupportedFormat
        }
        converter.sampleRateConverterQuality = AVAudioQuality.max.rawValue
        var result: [Float] = []
        var readError: Error?
        while true {
            output.frameLength = 0
            var conversionError: NSError?
            let status = converter.convert(to: output, error: &conversionError) { requested, state in
                let remaining = file.length - file.framePosition
                guard remaining > 0 else { state.pointee = .endOfStream; return nil }
                let count = min(requested, AVAudioFrameCount(remaining))
                guard let input = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: count) else {
                    readError = RoomTuneResponseError.unsupportedFormat
                    state.pointee = .endOfStream; return nil
                }
                do { try file.read(into: input, frameCount: count) }
                catch { readError = error; state.pointee = .endOfStream; return nil }
                state.pointee = .haveData
                return input
            }
            if let readError { throw readError }
            if let conversionError { throw conversionError }
            guard status != .error, let samples = output.floatChannelData?[0] else {
                throw RoomTuneResponseError.unsupportedFormat
            }
            result.append(contentsOf: UnsafeBufferPointer(start: samples, count: Int(output.frameLength)))
            if status == .endOfStream { break }
            guard output.frameLength > 0 else { throw RoomTuneResponseError.incomplete }
        }
        return (result, 48000)
    }
}
