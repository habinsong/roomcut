import Accelerate
import Foundation

public enum RoomTuneResponseError: Error, LocalizedError {
    case unsupportedFormat, insufficientSignal, clipped, incomplete, noSweep

    public var errorDescription: String? {
        switch self {
        case .unsupportedFormat:
            return L("녹음 형식을 분석할 수 없습니다", "Unsupported recording format", "録音形式に対応していません", "Format d'enregistrement non pris en charge", "Aufnahmeformat nicht unterstützt")
        case .insufficientSignal:
            return L("입력 신호가 너무 작습니다. 마이크와 출력 볼륨을 확인하세요", "Input is too quiet. Check the microphone and output volume", "入力が小さすぎます。マイクと出力音量を確認してください", "Entrée trop faible. Vérifiez le microphone et le volume", "Eingang zu leise. Mikrofon und Ausgangslautstärke prüfen")
        case .clipped:
            return L("녹음이 클리핑되었습니다. 출력 볼륨을 낮춰 다시 측정하세요", "Recording clipped. Lower the output volume and measure again", "録音がクリップしました。出力音量を下げて再測定してください", "Enregistrement saturé. Baissez le volume et recommencez", "Aufnahme übersteuert. Ausgangslautstärke senken und erneut messen")
        case .incomplete:
            return L("테스트음이 끝까지 녹음되지 않았습니다. 다시 측정하세요", "The complete sweep was not recorded. Measure again", "テスト音が最後まで録音されていません。再測定してください", "Le balayage est incomplet. Recommencez la mesure", "Testsignal nicht vollständig aufgenommen. Erneut messen")
        case .noSweep:
            return L("녹음에서 테스트음을 확인하지 못했습니다", "The test sweep could not be identified in the recording", "録音からテスト音を確認できませんでした", "Le signal de test n'a pas été identifié", "Testsignal in der Aufnahme nicht erkannt")
        }
    }
}

// Offline system identification: Y * conjugate(X) / |X|². The magnitude does
// not depend on recording/playback start delay. An impulse-energy check rejects
// unrelated recordings; the detected delay also verifies that the sweep is complete.
enum RoomTuneResponse {
    static func measure(samples: [Float], sampleRate: Double) throws -> [(freq: Double, db: Double)] {
        guard sampleRate.isFinite, (44_100...192_000).contains(sampleRate),
              samples.count <= Int(sampleRate * 20), samples.allSatisfy(\.isFinite) else {
            throw RoomTuneResponseError.unsupportedFormat
        }
        guard samples.count >= Int(sampleRate * RoomTuneSignal.duration) else {
            throw RoomTuneResponseError.incomplete
        }
        let energy = samples.reduce(0.0) { $0 + Double($1) * Double($1) }
        guard energy / Double(samples.count) > 1e-7 else {
            throw RoomTuneResponseError.insufficientSignal
        }
        guard samples.filter({ abs($0) >= 0.999 }).count < max(2, samples.count / 1000) else {
            throw RoomTuneResponseError.clipped
        }
        let reference = RoomTuneSignal.samples(sampleRate: sampleRate)
        var size = 1
        while size < samples.count { size *= 2 }
        guard let forward = vDSP_DFT_zop_CreateSetup(nil, vDSP_Length(size), .FORWARD) else {
            throw RoomTuneResponseError.unsupportedFormat
        }
        defer { vDSP_DFT_DestroySetup(forward) }
        guard let inverse = vDSP_DFT_zop_CreateSetup(nil, vDSP_Length(size), .INVERSE) else {
            throw RoomTuneResponseError.unsupportedFormat
        }
        defer { vDSP_DFT_DestroySetup(inverse) }
        let x = spectrum(reference, size: size, setup: forward)
        let y = spectrum(samples, size: size, setup: forward)
        var real = [Float](repeating: 0, count: size)
        var imaginary = real
        let lower = max(1, Int(50 * Double(size) / sampleRate))
        let upper = min(size / 2 - 1, Int(8000 * Double(size) / sampleRate))
        var referencePeak = 0.0
        for bin in lower...upper {
            let real = Double(x.real[bin]), imaginary = Double(x.imaginary[bin])
            referencePeak = max(referencePeak, real * real + imaginary * imaginary)
        }
        let floor = referencePeak * 1e-8
        for bin in lower...upper {
            let xr = Double(x.real[bin]), xi = Double(x.imaginary[bin])
            let yr = Double(y.real[bin]), yi = Double(y.imaginary[bin])
            let denominator = xr * xr + xi * xi + floor
            real[bin] = Float((yr * xr + yi * xi) / denominator)
            imaginary[bin] = Float((yi * xr - yr * xi) / denominator)
            real[size - bin] = real[bin]
            imaginary[size - bin] = -imaginary[bin]
        }

        var impulse = [Float](repeating: 0, count: size)
        var imaginaryImpulse = impulse
        vDSP_DFT_Execute(inverse, real, imaginary, &impulse, &imaginaryImpulse)
        let peak = impulse.indices.max { abs(impulse[$0]) < abs(impulse[$1]) } ?? 0
        let total = impulse.reduce(0.0) { $0 + Double($1) * Double($1) }
        let radius = Int(sampleRate * 0.025)
        var concentrated = 0.0
        for offset in -radius...radius {
            let value = Double(impulse[(peak + offset + size) % size])
            concentrated += value * value
        }
        guard total > 0, concentrated / total >= 0.08 else {
            throw RoomTuneResponseError.noSweep
        }
        // A little tolerance allows speaker/mic group delay. The capture path
        // records an extra second after playback, so normal measurements fit.
        guard peak + reference.count <= samples.count + Int(sampleRate * 0.05) else {
            throw RoomTuneResponseError.incomplete
        }

        var response: [(freq: Double, db: Double)] = []
        var center = 50.0
        while center <= 8000 {
            let lo = max(lower, Int(center / pow(2, 1.0 / 12) * Double(size) / sampleRate))
            let hi = min(upper, Int(center * pow(2, 1.0 / 12) * Double(size) / sampleRate))
            if hi >= lo {
                var power = 0.0
                for bin in lo...hi {
                    power += Double(real[bin]) * Double(real[bin]) + Double(imaginary[bin]) * Double(imaginary[bin])
                }
                response.append((center, 10 * log10(max(power / Double(hi - lo + 1), 1e-12))))
            }
            center *= pow(2, 1.0 / 6)
        }
        return response
    }

    private static func spectrum(_ samples: [Float], size: Int, setup: vDSP_DFT_Setup)
        -> (real: [Float], imaginary: [Float]) {
        let input = samples + [Float](repeating: 0, count: size - samples.count)
        let zeros = [Float](repeating: 0, count: size)
        var real = zeros, imaginary = zeros
        vDSP_DFT_Execute(setup, input, zeros, &real, &imaginary)
        return (real, imaginary)
    }
}
