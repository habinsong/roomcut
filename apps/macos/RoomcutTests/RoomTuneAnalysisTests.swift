import AVFoundation
import XCTest
@testable import RoomcutCore

final class RoomTuneAnalysisTests: XCTestCase {
    private let sampleRate = 48000.0

    private func recording(delay: Double = 0, bell: Bool = false, gain: Float = 0.15) -> [Float] {
        var signal = RoomTuneSignal.samples(sampleRate: sampleRate)
        signal += [Float](repeating: 0, count: Int(sampleRate * 0.8))
        if bell {
            // Independent RBJ +12 dB bell, 500 Hz, Q=4, as a known physical response.
            let w = 2 * Double.pi * 500 / sampleRate
            let a = pow(10, 12.0 / 40)
            let alpha = sin(w) / 8
            let a0 = 1 + alpha / a
            let b0 = (1 + alpha * a) / a0
            let b1 = -2 * cos(w) / a0
            let b2 = (1 - alpha * a) / a0
            let a1 = -2 * cos(w) / a0
            let a2 = (1 - alpha / a) / a0
            var x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0
            for index in signal.indices {
                let x = Double(signal[index])
                let y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
                signal[index] = Float(y)
                x2 = x1; x1 = x; y2 = y1; y1 = y
            }
        }
        return [Float](repeating: 0, count: Int(delay * sampleRate)) + signal.map { $0 * gain }
    }

    func testFlatResponseRemovesExcitationSpectrumAndUnknownDelay() throws {
        for delay in [0.0, 0.1, 0.5] {
            let response = try RoomTuneAnalysis.bandResponse(samples: recording(delay: delay), sampleRate: sampleRate)
            XCTAssertGreaterThan(response.count, 30)
            for band in response where band.freq >= 60 && band.freq <= 4000 {
                XCTAssertEqual(band.db, 20 * log10(0.15), accuracy: 0.03, "\(band.freq) Hz, delay \(delay)")
            }
            let result = RoomTuneAnalysis.analyze(responses: [response, response, response], strength: .medium)
            XCTAssertTrue(result.bands.isEmpty)
        }
    }

    func testKnownResonanceStaysAt500HzAcrossRecordingDelays() throws {
        var responses: [[(freq: Double, db: Double)]] = []
        for delay in [0.0, 0.1, 0.3, 0.5] {
            let response = try RoomTuneAnalysis.bandResponse(samples: recording(delay: delay, bell: true), sampleRate: sampleRate)
            responses.append(response)
            let result = RoomTuneAnalysis.analyze(responses: [response, response, response], strength: .medium)
            let band = try XCTUnwrap(result.bands.first)
            XCTAssertEqual(band.freqHz, 500, accuracy: 30)
            XCTAssertLessThan(band.gainDb, -2.5)
            XCTAssertGreaterThanOrEqual(band.gainDb, -5)
        }
        let median = RoomTuneAnalysis.analyze(responses: responses, strength: .high)
        XCTAssertEqual(try XCTUnwrap(median.bands.first).freqHz, 500, accuracy: 30)
        XCTAssertTrue(median.bands.allSatisfy { $0.gainDb <= 0 && $0.gainDb >= -6.5 })
    }

    func testSilenceAndClippingAreMeasurementFailures() {
        let count = Int(sampleRate * 7)
        for (value, expected) in [(Float(0), RoomTuneResponseError.insufficientSignal),
                                  (Float(1), RoomTuneResponseError.clipped)] {
            XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: [Float](repeating: value, count: count), sampleRate: sampleRate)) {
                XCTAssertEqual(String(describing: $0), String(describing: expected))
            }
        }
    }

    func testUnrelatedToneIsNotReportedAsARoomMeasurement() {
        let samples = (0..<Int(sampleRate * 7)).map { Float(0.1 * sin(Double($0) * 2 * .pi * 1000 / sampleRate)) }
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: samples, sampleRate: sampleRate)) {
            XCTAssertEqual(String(describing: $0), String(describing: RoomTuneResponseError.noSweep))
        }
    }

    func testRecordingTruncatedByDelayedPlaybackIsRejected() {
        let truncated = Array(recording(delay: 0.5).prefix(Int(sampleRate * 6.05)))
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: truncated, sampleRate: sampleRate)) {
            XCTAssertEqual(String(describing: $0), String(describing: RoomTuneResponseError.incomplete))
        }
    }

    func testInvalidSampleRateAndNonFiniteRecordingAreRejected() {
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: [], sampleRate: .nan))
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: [1], sampleRate: 0))
        var samples = recording()
        samples[100] = .nan
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: samples, sampleRate: sampleRate))
    }

    private func noise(count: Int) -> [Float] {
        var state: UInt64 = 0x5234_1298
        return (0..<count).map { _ in
            state = state &* 6364136223846793005 &+ 1442695040888963407
            return Float(Double(state >> 32) / Double(UInt32.max) * 2 - 1)
        }
    }

    func testClockAndNoiseMatrixPreservesFlatResponse() throws {
        for ppm in [-1000.0, 0, 1000] {
            let inputRate = sampleRate * (1 + ppm / 1_000_000)
            let signal = [Float](repeating: 0, count: 9600) +
                RoomTuneSignal.samples(sampleRate: inputRate).map { $0 * 0.15 } +
                [Float](repeating: 0, count: 38400)
            let signalEnergy = signal.reduce(0.0) { $0 + Double($1) * Double($1) }
            let background = noise(count: signal.count)
            let noiseEnergy = background.reduce(0.0) { $0 + Double($1) * Double($1) }
            for snr in [20.0, 40] {
                let scale = Float(sqrt(signalEnergy / noiseEnergy / pow(10, snr / 10)))
                let samples = zip(signal, background).map { $0 + $1 * scale }
                let response = try RoomTuneAnalysis.bandResponse(samples: samples, sampleRate: sampleRate)
                let error = response.filter { $0.freq >= 60 && $0.freq <= 4000 }
                    .map { abs($0.db - 20 * log10(0.15)) }.max() ?? .infinity
                let result = RoomTuneAnalysis.analyze(responses: [response, response, response], strength: .medium)
                XCTAssertLessThan(error, 0.5, "clock=\(ppm) ppm, SNR=\(snr) dB")
                XCTAssertTrue(result.bands.isEmpty, "flat path must not produce correction bands")
                print("Room Tune clock=\(ppm) ppm, SNR=\(snr) dB: max error=\(error) dB, cuts=\(result.bands.count)")
            }
        }
    }

    func testUnrelatedWhiteNoiseIsRejected() {
        let samples = noise(count: 336000).map { $0 * 0.05 }
        XCTAssertThrowsError(try RoomTuneAnalysis.bandResponse(samples: samples, sampleRate: sampleRate)) {
            XCTAssertEqual($0 as? RoomTuneResponseError, .noSweep)
        }
    }

    func testAudioFileConvertsLowRateMicrophoneRecordings() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".wav")
        defer { try? FileManager.default.removeItem(at: url) }
        let format = try XCTUnwrap(AVAudioFormat(standardFormatWithSampleRate: 16000, channels: 1))
        let buffer = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: format, frameCapacity: 16000))
        buffer.frameLength = 16000
        let channel = try XCTUnwrap(buffer.floatChannelData?[0])
        for frame in 0..<16000 { channel[frame] = Float(0.1 * sin(Double(frame) * 2 * .pi * 1000 / 16000)) }
        do {
            let file = try AVAudioFile(forWriting: url, settings: format.settings)
            try file.write(from: buffer)
        }
        let decoded = try RoomTuneAudioFile.load(url)
        XCTAssertEqual(decoded.sampleRate, 48000)
        XCTAssertEqual(Double(decoded.samples.count), 48000, accuracy: 4)
        guard decoded.samples.count >= 47000 else { return }
        let rms = sqrt(decoded.samples[1000..<47000].reduce(0.0) { $0 + Double($1) * Double($1) } / 46000)
        XCTAssertEqual(rms, 0.1 / sqrt(2), accuracy: 0.001)
    }
}
