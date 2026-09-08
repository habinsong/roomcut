import AVFoundation
import XCTest
@testable import RoomcutCore

final class RoomTunePCMTests: XCTestCase {
    private let values: [Float] = [0.25, -0.5, 0, 0.75, -1, 0.5]

    private func buffer(_ common: AVAudioCommonFormat, channels: AVAudioChannelCount, interleaved: Bool,
                        sampleRate: Double = 48000) throws -> AVAudioPCMBuffer {
        let format = try XCTUnwrap(AVAudioFormat(commonFormat: common, sampleRate: sampleRate,
                                                channels: channels, interleaved: interleaved))
        let buffer = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(values.count)))
        buffer.frameLength = AVAudioFrameCount(values.count)
        let buffers = UnsafeMutableAudioBufferListPointer(buffer.mutableAudioBufferList)
        for frame in values.indices {
            for channel in 0..<Int(channels) {
                let value = channel == 0 ? values[frame] : 0.125
                let index = interleaved ? frame * Int(channels) + channel : frame
                let data = try XCTUnwrap(buffers[interleaved ? 0 : channel].mData)
                switch common {
                case .pcmFormatFloat32: data.assumingMemoryBound(to: Float.self)[index] = value
                case .pcmFormatFloat64: data.assumingMemoryBound(to: Double.self)[index] = Double(value)
                case .pcmFormatInt16: data.assumingMemoryBound(to: Int16.self)[index] = Int16(Double(value) * 32768)
                case .pcmFormatInt32: data.assumingMemoryBound(to: Int32.self)[index] = Int32(Double(value) * 2147483648)
                default: XCTFail("unexpected test format")
                }
            }
        }
        return buffer
    }

    private func sampleBuffer(_ pcm: AVAudioPCMBuffer, count: Int? = nil, withData: Bool = true) throws -> CMSampleBuffer {
        var description: CMAudioFormatDescription?
        XCTAssertEqual(CMAudioFormatDescriptionCreate(allocator: kCFAllocatorDefault,
            asbd: pcm.format.streamDescription, layoutSize: 0, layout: nil,
            magicCookieSize: 0, magicCookie: nil, extensions: nil, formatDescriptionOut: &description), noErr)
        let format = try XCTUnwrap(description)
        var timing = CMSampleTimingInfo(duration: CMTime(value: 1, timescale: Int32(pcm.format.sampleRate)),
                                       presentationTimeStamp: .zero, decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        XCTAssertEqual(CMSampleBufferCreate(allocator: kCFAllocatorDefault, dataBuffer: nil,
            dataReady: false, makeDataReadyCallback: nil, refcon: nil, formatDescription: format,
            sampleCount: count ?? Int(pcm.frameLength), sampleTimingEntryCount: 1, sampleTimingArray: &timing,
            sampleSizeEntryCount: 0, sampleSizeArray: nil, sampleBufferOut: &sample), noErr)
        let result = try XCTUnwrap(sample)
        guard withData else { return result }
        XCTAssertEqual(CMSampleBufferSetDataBufferFromAudioBufferList(result,
            blockBufferAllocator: kCFAllocatorDefault, blockBufferMemoryAllocator: kCFAllocatorDefault,
            flags: 0, bufferList: pcm.audioBufferList), noErr)
        XCTAssertEqual(CMSampleBufferSetDataReady(result), noErr)
        return result
    }

    func testFirstChannelAcrossPCMFormatsAndLayouts() throws {
        let decoder = RoomTunePCM()
        for common: AVAudioCommonFormat in [.pcmFormatFloat32, .pcmFormatFloat64, .pcmFormatInt16, .pcmFormatInt32] {
            for channels: AVAudioChannelCount in [1, 2] {
                for interleaved in [false, true] {
                    let label = "format=\(common.rawValue), channels=\(channels), interleaved=\(interleaved)"
                    let input = try buffer(common, channels: channels, interleaved: interleaved)
                    let sample = try sampleBuffer(input)
                    guard let result = decoder.firstChannel(from: sample) else {
                        XCTFail("valid PCM was discarded: \(label)"); continue
                    }
                    XCTAssertEqual(result.frameLength, input.frameLength, label)
                    let channel = try XCTUnwrap(result.floatChannelData?[0])
                    let errors = values.indices.map { abs(channel[$0] - values[$0]) }
                    XCTAssertTrue(errors.allSatisfy { $0.isFinite && $0 < 0.000001 }, "first channel differs: \(label), errors=\(errors)")
                    print("PCM \(label): max error \(errors.max() ?? 0)")
                }
            }
        }
    }

    func testPacked24BitPCMEndiannessAndChannelLayouts() throws {
        let decoder = RoomTunePCM()
        for channels: UInt32 in [1, 2] {
            for interleaved in [false, true] {
                for bigEndian in [false, true] {
                    let bytesPerFrame = 3 * (interleaved ? channels : 1)
                    var asbd = AudioStreamBasicDescription(mSampleRate: 48000, mFormatID: kAudioFormatLinearPCM,
                        mFormatFlags: kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked |
                            (interleaved ? 0 : kAudioFormatFlagIsNonInterleaved) | (bigEndian ? kAudioFormatFlagIsBigEndian : 0),
                        mBytesPerPacket: bytesPerFrame, mFramesPerPacket: 1, mBytesPerFrame: bytesPerFrame,
                        mChannelsPerFrame: channels, mBitsPerChannel: 24, mReserved: 0)
                    let format = try XCTUnwrap(AVAudioFormat(streamDescription: &asbd))
                    let input = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(values.count)))
                    input.frameLength = AVAudioFrameCount(values.count)
                    let buffers = UnsafeMutableAudioBufferListPointer(input.mutableAudioBufferList)
                    for frame in values.indices {
                        for channel in 0..<Int(channels) {
                            let value = channel == 0 ? values[frame] : 0.125
                            let bits = UInt32(bitPattern: Int32(Double(value) * 8388608))
                            let data = try XCTUnwrap(buffers[interleaved ? 0 : channel].mData)
                            let index = interleaved ? frame * Int(channels) + channel : frame
                            for byte in 0..<3 {
                                let shift = (bigEndian ? 2 - byte : byte) * 8
                                data.storeBytes(of: UInt8((bits >> shift) & 255), toByteOffset: index * 3 + byte, as: UInt8.self)
                            }
                        }
                    }
                    let output = try XCTUnwrap(decoder.firstChannel(from: sampleBuffer(input)))
                    let samples = try XCTUnwrap(output.floatChannelData?[0])
                    let errors = values.indices.map { abs(samples[$0] - values[$0]) }
                    XCTAssertTrue(errors.allSatisfy { $0.isFinite && $0 < 0.000001 })
                    print("PCM packed24 channels=\(channels), interleaved=\(interleaved), bigEndian=\(bigEndian): max error \(errors.max() ?? 0)")
                }
            }
        }
    }

    func testCaptureConversionKeepsTheInputSampleRate() throws {
        let decoder = RoomTunePCM()
        for rate in [8000.0, 16000, 44100, 48000, 96000, 192000] {
            let input = try sampleBuffer(buffer(.pcmFormatFloat32, channels: 2, interleaved: false, sampleRate: rate))
            let output = try XCTUnwrap(decoder.firstChannel(from: input))
            XCTAssertEqual(output.format.sampleRate, rate)
            XCTAssertEqual(output.format.channelCount, 1)
            XCTAssertEqual(output.frameLength, AVAudioFrameCount(values.count))
            let samples = try XCTUnwrap(output.floatChannelData?[0])
            XCTAssertEqual(Array(UnsafeBufferPointer(start: samples, count: values.count)), values)
        }
    }

    func testRejectsUnreadyInvalidatedAndTruncatedBuffers() throws {
        let decoder = RoomTunePCM()
        let input = try buffer(.pcmFormatFloat32, channels: 1, interleaved: false)
        XCTAssertNil(decoder.firstChannel(from: try sampleBuffer(input, withData: false)))
        let valid = try sampleBuffer(input)
        let truncated = try sampleBuffer(input, count: values.count + 1, withData: false)
        let data = try XCTUnwrap(CMSampleBufferGetDataBuffer(valid))
        XCTAssertEqual(CMSampleBufferSetDataBuffer(truncated, newValue: data), noErr)
        XCTAssertEqual(CMSampleBufferSetDataReady(truncated), noErr)
        XCTAssertNil(decoder.firstChannel(from: truncated))
        let invalid = try sampleBuffer(input)
        XCTAssertEqual(CMSampleBufferInvalidate(invalid), noErr)
        XCTAssertNil(decoder.firstChannel(from: invalid))
    }

    func testNonFiniteInputFailsButClippedLevelsRemainVisible() throws {
        let decoder = RoomTunePCM()
        let input = try buffer(.pcmFormatFloat32, channels: 1, interleaved: false)
        input.floatChannelData?[0][0] = .nan
        XCTAssertNil(decoder.firstChannel(from: try sampleBuffer(input)))
        input.floatChannelData?[0][0] = 1.25
        let result = try XCTUnwrap(decoder.firstChannel(from: sampleBuffer(input)))
        XCTAssertEqual(result.floatChannelData?[0][0], 1.25, "conversion must not hide clipping from measurement validation")
    }

    func testRepeatedConvertedBuffersRoundTripThroughWAV() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("roomtune-capture-\(UUID()).wav")
        defer { try? FileManager.default.removeItem(at: url) }
        let decoder = RoomTunePCM()
        let input = try sampleBuffer(buffer(.pcmFormatFloat64, channels: 2, interleaved: false))
        do {
            let first = try XCTUnwrap(decoder.firstChannel(from: input))
            let file = try AVAudioFile(forWriting: url, settings: first.format.settings)
            try file.write(from: first)
            for _ in 1..<32 {
                try file.write(from: XCTUnwrap(decoder.firstChannel(from: input)))
            }
        }
        let decoded = try RoomTuneAudioFile.load(url)
        XCTAssertEqual(decoded.samples, Array(repeating: values, count: 32).flatMap { $0 })
    }
}
