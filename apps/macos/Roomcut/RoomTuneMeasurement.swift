//
// RoomTuneMeasurement.swift — Phase 2/3: multi-pass measure + record + analyze.
//
// Runs the sweep+record `rounds` times (default 3) and keeps the per-band median, so
// one noisy iPhone-mic pass can't skew the result (web research: never trust a single
// point). The caller bypasses Roomcut's DSP for the whole run so the sweep isn't
// coloured by our own EQ. Correction `strength` re-analyses the SAME recordings live —
// no re-measure needed.
//
@preconcurrency import AVFoundation
import Combine

@MainActor
final class RoomTuneMeasurement: ObservableObject {
    enum Phase: Equatable {
        case idle
        case measuring(round: Int, total: Int)
        case done
        case failed(String)
    }
    static let rounds = 3

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var inputPeak: Float = 0
    @Published private(set) var result: RoomTuneResult?
    @Published var strength: RoomTuneStrength = .medium { didSet { recompute() } }

    private let playEngine = AVAudioEngine()
    private let player = AVAudioPlayerNode()
    private var attached = false
    private let recorder = RoomTuneRecorder()
    private var levelTimer: Timer?
    private var responses: [[(freq: Double, db: Double)]] = []
    private var device: AVCaptureDevice?
    private var onFinish: (() -> Void)?

    var isBusy: Bool { if case .measuring = phase { return true }; return false }

    /// Request mic access, then run `rounds` sweep+record passes and analyze. `onFinish`
    /// runs once at the very end (success or fail) — the caller restores bypass there.
    func start(device: AVCaptureDevice, onFinish: @escaping () -> Void) {
        guard !isBusy else { return }
        self.device = device
        self.onFinish = onFinish
        AVCaptureDevice.requestAccess(for: .audio) { [weak self] granted in
            Task { @MainActor in
                guard let self else { return }
                guard granted else {
                    self.fail("마이크 권한이 필요합니다 (시스템 설정 › 개인정보 보호 › 마이크)"); return
                }
                self.responses = []
                self.result = nil
                self.playRound(0)
            }
        }
    }

    private func playRound(_ round: Int) {
        guard let device, let buffer = RoomTuneSweep.makeBuffer() else { fail("테스트음 생성 실패"); return }
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("roomtune-\(Int(Date().timeIntervalSince1970))-\(round).wav")
        do { try recorder.start(device: device, to: url) }
        catch { fail("녹음 시작 실패: \(error.localizedDescription)"); return }
        if !attached {
            playEngine.attach(player)
            playEngine.connect(player, to: playEngine.mainMixerNode, format: buffer.format)
            attached = true
        }
        do { try playEngine.start() }
        catch { _ = recorder.stop(); fail("오디오 엔진 시작 실패: \(error.localizedDescription)"); return }
        phase = .measuring(round: round + 1, total: Self.rounds)
        levelTimer?.invalidate()
        levelTimer = Timer.scheduledTimer(withTimeInterval: 0.08, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.inputPeak = self?.recorder.peakLevel ?? 0 }
        }
        player.scheduleBuffer(buffer, at: nil, options: []) { [weak self] in
            Task { @MainActor in self?.finishRound(url: url, round: round) }
        }
        player.play()
    }

    private func finishRound(url: URL, round: Int) {
        player.stop(); playEngine.stop()
        levelTimer?.invalidate(); levelTimer = nil
        inputPeak = 0
        guard let saved = recorder.stop(),
              let resp = RoomTuneAnalysis.bandResponse(saved), !resp.isEmpty else {
            fail("녹음 분석 실패"); return
        }
        responses.append(resp)
        if round + 1 < Self.rounds {
            // Short gap so the user can nudge the phone a little (light spatial average).
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
                self?.playRound(round + 1)
            }
        } else {
            result = RoomTuneAnalysis.analyze(responses: responses, strength: strength)
            phase = .done
            finishUp()
        }
    }

    private func recompute() {
        guard !responses.isEmpty else { return }
        result = RoomTuneAnalysis.analyze(responses: responses, strength: strength)
    }

    private func fail(_ message: String) {
        playEngine.stop(); levelTimer?.invalidate(); levelTimer = nil; inputPeak = 0
        phase = .failed(message)
        finishUp()
    }

    private func finishUp() {
        let cb = onFinish
        onFinish = nil
        cb?()
    }
}
