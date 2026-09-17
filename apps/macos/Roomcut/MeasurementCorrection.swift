//
// MeasurementCorrection.swift — a measured response the listener brings in, as
// the parametric bands that flatten it.
//
// The file is text, one point per line: frequency (Hz) then level (dB), split by
// commas, semicolons, tabs or spaces. A line whose first two fields are not both
// numbers (a header, a comment) is skipped, and columns after the second are
// ignored. What gets flattened is the curve as given, so a response measured
// against a target should come in as its difference from that target.
//
// The fit is the engine's own (core/dsp/ParametricFit.hpp, through
// roomcutClientFitCorrection), judged at the rate the output runs at, so the
// bands are exactly the biquads the engine will run.
//
import Foundation
import CRoomcutClient

public struct MeasurementCorrection: Equatable {
    public let bands: [ParametricBand]   // EngineParameters.paramBandCount; the unused ones disabled
    public let bandsUsed: Int
    public let pointCount: Int
    public let rmsBeforeDb: Double
    public let rmsAfterDb: Double

    public enum Failure: Error, Equatable {
        case notText
        case tooFewPoints(Int)
        case outOfRange   // the points miss 20 Hz - 16 kHz
    }

    public static func points(in text: String) -> [(freq: Double, db: Double)] {
        var points: [(freq: Double, db: Double)] = []
        for line in text.split(whereSeparator: \.isNewline) {
            let fields = line.split(whereSeparator: { $0 == "," || $0 == ";" || $0 == "\t" || $0 == " " })
            guard fields.count >= 2, let freq = Double(fields[0]), let db = Double(fields[1]),
                  freq.isFinite, freq > 0, db.isFinite else { continue }
            points.append((freq, db))
        }
        return points
    }

    public static func fit(_ data: Data, sampleRate: Double) throws -> MeasurementCorrection {
        guard let text = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1) else {
            throw Failure.notText
        }
        let points = points(in: text)
        guard points.count >= 2 else { throw Failure.tooFewPoints(points.count) }
        var native = [RoomcutClientParamBand](repeating: RoomcutClientParamBand(), count: EngineParameters.paramBandCount)
        var before = 0.0, after = 0.0
        let used = roomcutClientFitCorrection(points.map { $0.freq }, points.map { $0.db }, Int32(points.count),
                                              sampleRate, &native, &before, &after)
        guard used >= 0 else { throw Failure.outOfRange }
        let bands = native.map {
            ParametricBand(enabled: $0.enabled != 0, type: Int($0.type), freqHz: $0.freqHz, gainDb: $0.gainDb, q: $0.q)
        }
        return MeasurementCorrection(bands: bands, bandsUsed: Int(used), pointCount: points.count,
                                     rmsBeforeDb: before, rmsAfterDb: after)
    }
}
