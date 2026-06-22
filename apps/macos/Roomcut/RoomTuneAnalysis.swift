//
// RoomTuneAnalysis.swift — Phase 3: multi-measurement room analysis → safe EQ cuts.
//
// iPhone mics are noisy/AGC'd, and any single point is position-dependent (room
// research: "never measure at one mic location" — 10 cm moves the 1–2 kHz region by
// up to 16 dB). So we take SEVERAL measurements and keep only what's consistent: the
// per-band MEDIAN across runs (rejects outliers / one-off spikes), then flag bands
// that stick UP beyond a broad trend (room modes) and emit conservative CUTs. We
// never boost and never chase deep, position-specific dips (those smooth out in the
// median anyway). Correction strength is user-selectable (Low/Medium/High).
//
import AVFoundation
import RoomcutCore

enum RoomTuneStrength: String, CaseIterable, Identifiable {
    case low, medium, high
    var id: String { rawValue }
    var title: String {
        switch self {
        case .low:    return L("약하게", "Low", "弱め", "Faible", "Niedrig")
        case .medium: return L("표준", "Medium", "標準", "Standard", "Standard")
        case .high:   return L("강하게", "High", "強め", "Élevé", "Hoch")
        }
    }
    var maxCutDb: Double {
        switch self { case .low: return 3.0; case .medium: return 5.0; case .high: return 6.5 }
    }
    var thresholdDb: Double {   // how far above the trend a band must stick to be cut
        switch self { case .low: return 3.5; case .medium: return 2.5; case .high: return 2.0 }
    }
}

struct RoomTuneResult {
    let bands: [ParametricBand]
    let response: [(freq: Double, db: Double)]   // the median curve (Before)
    let summary: String
}

enum RoomTuneAnalysis {
    static let lowLimitHz = 60.0       // below: unreliable (mic + speaker)
    static let highLimitHz = 4000.0    // above: position/mic variance too high
    static let maxBands = 5

    // One recording → its 1/6-octave band response (the sweep maps time → frequency).
    static func bandResponse(_ url: URL) -> [(freq: Double, db: Double)]? {
        guard let (samples, sr) = loadSamples(url) else { return nil }
        return bandResponse(samples: samples, sampleRate: sr)
    }

    static func bandResponse(samples: [Float], sampleRate sr: Double) -> [(freq: Double, db: Double)] {
        let f0 = 20.0, f1 = 20_000.0, dur = RoomTuneSweep.duration
        let k = dur / log(f1 / f0)
        let timeOf: (Double) -> Double = { k * log($0 / f0) }
        let n = samples.count
        var centers: [Double] = []
        var fc = 50.0
        while fc <= 8000 { centers.append(fc); fc *= pow(2, 1.0 / 6) }
        var resp: [(freq: Double, db: Double)] = []
        for c in centers {
            let lo = max(0, Int(timeOf(c / pow(2, 1.0 / 6)) * sr))
            let hi = min(n, Int(timeOf(c * pow(2, 1.0 / 6)) * sr))
            guard hi > lo else { continue }
            var sq = 0.0
            for i in lo..<hi { let v = Double(samples[i]); sq += v * v }
            resp.append((c, 20 * log10(max(sqrt(sq / Double(hi - lo)), 1e-9))))
        }
        return resp
    }

    // Several band responses → median curve → trend → consistent peaks → safe cuts.
    static func analyze(responses: [[(freq: Double, db: Double)]],
                        strength: RoomTuneStrength) -> RoomTuneResult {
        guard let first = responses.first(where: { !$0.isEmpty }) else {
            return RoomTuneResult(bands: [], response: [], summary: "측정 데이터가 없습니다")
        }
        // Per-band median across runs — rejects a single noisy/AGC'd measurement.
        var median: [(freq: Double, db: Double)] = []
        for j in first.indices {
            let dbs = responses.compactMap { j < $0.count ? $0[j].db : nil }.sorted()
            guard !dbs.isEmpty else { continue }
            median.append((first[j].freq, dbs[dbs.count / 2]))
        }
        // Broad 1-octave trend.
        var trend: [Double] = []
        for i in median.indices {
            let lo = max(0, i - 3), hi = min(median.count, i + 4)
            trend.append(median[lo..<hi].map { $0.db }.reduce(0, +) / Double(hi - lo))
        }
        // Peaks above the trend, inside the trustworthy band, gated by strength.
        var peaks: [(freq: Double, dev: Double)] = []
        for i in median.indices {
            let dev = median[i].db - trend[i]
            if dev > strength.thresholdDb, median[i].freq >= lowLimitHz, median[i].freq <= highLimitHz {
                peaks.append((median[i].freq, dev))
            }
        }
        // Merge neighbours within a half-octave (largest deviation wins).
        peaks.sort { $0.freq < $1.freq }
        var groups: [[(freq: Double, dev: Double)]] = []
        for p in peaks {
            if let last = groups.last?.last, p.freq / last.freq < sqrt(2.0) {
                groups[groups.count - 1].append(p)
            } else {
                groups.append([p])
            }
        }
        var merged = groups.compactMap { $0.max { $0.dev < $1.dev } }
        merged.sort { $0.dev > $1.dev }

        let bands = merged.prefix(maxBands).map { peak in
            ParametricBand(enabled: true, type: 0 /* Bell */,
                           freqHz: peak.freq, gainDb: -min(peak.dev, strength.maxCutDb), q: 2.0)
        }
        let summary = bands.isEmpty
            ? "\(responses.count)회 측정에서 공통된 룸 모드가 없습니다 — 방이 고른 편입니다."
            : "\(responses.count)회 공통: " + bands.map {
                "\(Int($0.freqHz.rounded()))Hz \(String(format: "%.1f", $0.gainDb))dB"
              }.joined(separator: " · ")
        return RoomTuneResult(bands: Array(bands), response: median, summary: summary)
    }

    static func loadSamples(_ url: URL) -> ([Float], Double)? {
        guard let file = try? AVAudioFile(forReading: url) else { return nil }
        let fmt = file.processingFormat
        let frames = AVAudioFrameCount(file.length)
        guard frames > 0,
              let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: frames),
              (try? file.read(into: buf)) != nil,
              let ch = buf.floatChannelData?[0] else { return nil }
        return (Array(UnsafeBufferPointer(start: ch, count: Int(buf.frameLength))), fmt.sampleRate)
    }
}
