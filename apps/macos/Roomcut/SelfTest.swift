//
// SelfTest.swift — headless verification of the app's engine-client path.
//
// Connects over the exact code the UI uses (CRoomcutClient → Mach control
// plane): fetches status, enumerates builtin presets, re-applies the current
// preset (exercises SET_PRESET without changing what the user hears), and
// fetches status again. Exit 0 = all good.
//
import Foundation
import CRoomcutClient
import RoomcutCore

private func stderrPrint(_ s: String) {
    FileHandle.standardError.write((s + "\n").data(using: .utf8)!)
}

func runSelfTest() -> Int32 {
    var st = RoomcutClientState()
    var rc = roomcutClientGetState(&st)
    guard rc == 0 else {
        stderrPrint("selftest: engine unreachable (rc \(rc))")
        return 1
    }
    let preset = presetIdString(st)
    print("selftest: state=\(st.state) preset=\(preset) peak=\(st.renderPeak) "
          + "frames=\(st.framesRendered) underruns=\(st.ringUnderruns)")

    let count = roomcutClientPresetCount()
    guard count > 0 else {
        stderrPrint("selftest: no builtin presets")
        return 1
    }
    var ids: [String] = []
    for i in 0..<count {
        var idBuf = [CChar](repeating: 0, count: 32)
        var nameBuf = [CChar](repeating: 0, count: 64)
        guard roomcutClientPresetInfo(Int32(i), &idBuf, 32, &nameBuf, 64) == 0 else {
            stderrPrint("selftest: preset info \(i) failed")
            return 1
        }
        ids.append(String(cString: idBuf))
    }
    print("selftest: presets=\(ids.joined(separator: ","))")

    // Read the authoritative parameters too (the path the main window relies on).
    var params = RoomcutClientParams()
    rc = roomcutClientGetParams(&params)
    guard rc == 0 else {
        stderrPrint("selftest: get params failed (rc \(rc))")
        return 1
    }
    let eqBefore = withUnsafeBytes(of: params.eqGainsDb) { Array($0.bindMemory(to: Double.self)) }
    print("selftest: params preamp=\(params.preampDb) eq=\(eqBefore) out=\(params.outputGainDb) rev=\(st.paramsRevision)")

    if (st.capabilities & UInt32(ROOMCUT_CLIENT_CAP_ANALYZER)) != 0 {
        var analysis = RoomcutClientAnalysis()
        rc = roomcutClientGetAnalysis(&analysis)
        guard rc == 0 else {
            stderrPrint("selftest: get analysis failed (rc \(rc))")
            return 1
        }
        let spectrum = withUnsafeBytes(of: analysis.spectrum) { Array($0.bindMemory(to: Float.self)) }
        guard spectrum.count == Int(ROOMCUT_CLIENT_ANALYSIS_SPECTRUM_BINS) else {
            stderrPrint("selftest: analysis spectrum shape mismatch")
            return 1
        }
        print("selftest: analysis valid=\(analysis.valid != 0) peakDb=\(analysis.peakDb) width=\(analysis.stereoWidth)")
    }

    // Re-apply WITHOUT changing what the user hears: a builtin preset re-applies
    // itself; a "custom" curve re-applies its exact params.
    if ids.contains(preset) {
        rc = roomcutClientSetPreset(preset)
        guard rc == 0 else {
            stderrPrint("selftest: set preset '\(preset)' failed (rc \(rc))")
            return 1
        }
        rc = roomcutClientGetState(&st)
        guard rc == 0, presetIdString(st) == preset else {
            stderrPrint("selftest: preset readback mismatch (rc \(rc), got \(presetIdString(st)))")
            return 1
        }
        print("selftest: re-applied builtin preset '\(preset)' OK")
    } else {
        let pbands = withUnsafeBytes(of: params.parametric) {
            Array($0.bindMemory(to: RoomcutClientParamBand.self))
        }
        rc = eqBefore.withUnsafeBufferPointer { buf in
            pbands.withUnsafeBufferPointer { pbuf in
                roomcutClientSetParams(params.preampDb, buf.baseAddress,
                                       params.limiterReleaseMs,
                                       params.outputGainDb, params.spatialWidth,
                                       params.centerFocus, params.crossfeed,
                                       params.roomReduce, params.spatialMode, pbuf.baseAddress)
            }
        }
        guard rc == 0 else {
            stderrPrint("selftest: set params failed (rc \(rc))")
            return 1
        }
        var after = RoomcutClientParams()
        rc = roomcutClientGetParams(&after)
        let eqAfter = withUnsafeBytes(of: after.eqGainsDb) { Array($0.bindMemory(to: Double.self)) }
        guard rc == 0, eqAfter == eqBefore, after.preampDb == params.preampDb,
              after.outputGainDb == params.outputGainDb,
              after.spatialWidth == params.spatialWidth,
              after.centerFocus == params.centerFocus,
              after.crossfeed == params.crossfeed,
              after.roomReduce == params.roomReduce else {
            stderrPrint("selftest: params readback mismatch (rc \(rc))")
            return 1
        }
        print("selftest: re-applied custom params (identical) OK")
    }
    print("selftest: PASS")
    return 0
}
