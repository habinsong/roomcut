import AVFoundation
import XCTest
@testable import RoomcutCore

final class RoomTuneAudioFileTests: XCTestCase {
    func testPCMFileFormatsAndSampleRatesPreserveLevel() throws {
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("roomtune-pcm-\(UUID())")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        for rate in [8000.0, 16000, 44100, 48000, 96000, 192000] {
            for (depth, floating) in [(16, false), (24, false), (32, false), (32, true)] {
                let url = directory.appendingPathComponent("\(Int(rate))-\(depth)-\(floating).wav")
                let format = try XCTUnwrap(AVAudioFormat(standardFormatWithSampleRate: rate, channels: 1))
                let count = Int(rate / 4)
                let input = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(count)))
                input.frameLength = AVAudioFrameCount(count)
                let samples = try XCTUnwrap(input.floatChannelData?[0])
                for frame in 0..<count { samples[frame] = Float(0.1 * sin(Double(frame) * 2 * .pi * 1000 / rate)) }
                do {
                    let file = try AVAudioFile(forWriting: url, settings: [
                        AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: rate, AVNumberOfChannelsKey: 1,
                        AVLinearPCMBitDepthKey: depth, AVLinearPCMIsFloatKey: floating,
                        AVLinearPCMIsBigEndianKey: false, AVLinearPCMIsNonInterleaved: false])
                    try file.write(from: input)
                }
                let decoded = try RoomTuneAudioFile.load(url)
                XCTAssertEqual(decoded.sampleRate, 48000)
                XCTAssertEqual(Double(decoded.samples.count), 12000, accuracy: 4)
                guard decoded.samples.count >= 11000 else { continue }
                let rms = sqrt(decoded.samples[1000..<11000].reduce(0.0) { $0 + Double($1) * Double($1) } / 10000)
                XCTAssertEqual(rms, 0.1 / sqrt(2), accuracy: 0.001)
                print("WAV \(Int(rate)) Hz, \(depth) bit, float=\(floating): frames=\(decoded.samples.count), rms=\(rms)")
            }
        }
    }
}
