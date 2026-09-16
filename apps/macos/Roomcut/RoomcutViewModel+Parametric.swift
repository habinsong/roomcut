import Foundation
import Combine
import CRoomcutClient
import RoomcutPresentationCore

// The N-band parametric EQ. Each setter validates one field and leaves the rest
// of the band alone, so a view can bind straight to it.
extension RoomcutViewModel {
    public func setParametricBand(_ index: Int, _ band: ParametricBand) {
        guard ensureParametricAvailable() else { return }
        guard parametric.indices.contains(index) else { return }
        var b = band
        b.type = max(0, min(ParametricBand.Kind.allCases.count - 1, b.type))
        b.freqHz = Self.clamp(b.freqHz, Self.parametricFreqRange.lowerBound, Self.parametricFreqRange.upperBound)
        b.gainDb = Self.clamp(b.gainDb, Self.parametricGainRange.lowerBound, Self.parametricGainRange.upperBound)
        b.q = Self.clamp(b.q, Self.parametricQRange.lowerBound, Self.parametricQRange.upperBound)
        parametric[index] = b
        schedulePushParams()
    }

    // Convenience mutators used by the editor controls.
    public func setParametricEnabled(_ index: Int, _ on: Bool) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.enabled = on; setParametricBand(index, b)
    }
    public func setParametricType(_ index: Int, _ type: Int) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.type = type; setParametricBand(index, b)
    }
    public func setParametricFreq(_ index: Int, _ hz: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.freqHz = hz; setParametricBand(index, b)
    }
    public func setParametricGain(_ index: Int, _ db: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.gainDb = db; setParametricBand(index, b)
    }
    public func setParametricQ(_ index: Int, _ q: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.q = q; setParametricBand(index, b)
    }
    public func setParametricDynamic(_ index: Int, _ on: Bool) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]
        b.dynamic = on
        // Turning it on with nothing to give away would look broken, so start it
        // where a resonance actually gets caught.
        if on && b.rangeDb <= 0 { b.rangeDb = 6 }
        setParametricBand(index, b)
    }
    public func setParametricThreshold(_ index: Int, _ db: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.thresholdDb = db; setParametricBand(index, b)
    }
    public func setParametricRange(_ index: Int, _ db: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.rangeDb = db; setParametricBand(index, b)
    }
    public func setParametricAttack(_ index: Int, _ ms: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.attackMs = ms; setParametricBand(index, b)
    }
    public func setParametricRelease(_ index: Int, _ ms: Double) {
        guard parametric.indices.contains(index) else { return }
        var b = parametric[index]; b.releaseMs = ms; setParametricBand(index, b)
    }
    public var dynamicEqAvailable: Bool { status.reachable && status.supportsDynamicEq }

    private func ensureParametricAvailable() -> Bool {
        guard parametricAvailable else {
            errorBanner = status.reachable ? "현재 엔진이 Parametric EQ를 지원하지 않습니다" : "연결 끊김"
            return false
        }
        return true
    }
}
