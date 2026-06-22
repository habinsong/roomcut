#!/usr/bin/env python3
"""Verify a Roomcut engine --dump capture without ears.

Reads the float32 WAV that `RoomcutAudioEngine --dump out.wav` writes (the
post-DSP render output — exactly what reached the hardware) and checks the
rendered signal for the defects counters can't see. Two modes:

Single-sine mode (default; driver-sim's plain 440 Hz tone):
  frequency     zero-crossing estimate vs --freq   (default 440 Hz, +-2 Hz)
  peak          vs --peak                          (default 0.25, +-2%)
  DC offset     |mean| < 1e-3
  gaps          runs of near-silence >= 32 samples inside the active region
  clicks        sample-to-sample jumps > 3x the sine's max slope
  ch match      max |L-R| (the sim writes identical channels)

Tone-set mode (--tones; driver-sim --tones puts one sine on each EQ band):
  per-tone level (Goertzel, Hann window) in dBFS, plus DC/gaps/clicks/ch-match.
  With --compare REF.wav, reports each tone's delta dB against the reference
  capture — systematic effects (resampler rolloff, device SR) cancel in the
  delta, so the delta IS the DSP curve. With --expect F:DB[,F:DB...] the
  listed tones must match their delta within --tol (default 1.0 dB) and every
  other tone must stay within --tol-rest of 0 (default 2.0 dB — graphic-EQ
  bands overlap, so neighbours of a boosted band legitimately move a little).

EQ end-to-end example (flat reference, then +6 dB @ 1 kHz / -6 dB @ 8 kHz):
  RoomcutAudioEngine --dump ref.wav               + roomcut-driver-sim 3 --tones
  RoomcutAudioEngine --eq 0,0,0,0,0,6,0,0,-6,0 \
                     --dump eq.wav                + roomcut-driver-sim 3 --tones
  analyze-dump.py eq.wav --tones eq10 --compare ref.wav --expect 1000:6,8000:-6

Leading/trailing silence is expected (render starts before the producer and
drains after it); the active region is trimmed by 0.1 s head / 0.2 s tail.
Analysis runs on channel 0. Exit 0 = PASS, 1 = FAIL, 2 = usage/format error.
"""
import argparse
import array
import math
import struct
import sys

# GraphicEQ band centers — keep in sync with core/dsp/GraphicEQ.hpp kCenters.
EQ10 = [31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]


def read_wav_f32(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    if len(data) < 12 or data[0:4] != b'RIFF' or data[8:12] != b'WAVE':
        sys.exit(f"error: not a RIFF/WAVE file: {path}")
    fmt, raw, pos = None, None, 12
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from('<I', data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', body, 0)
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    if fmt is None or raw is None:
        sys.exit("error: missing fmt/data chunk")
    tag, ch, sr, _, _, bits = fmt
    if tag != 3 or bits != 32:
        sys.exit(f"error: expected float32 WAV (tag=3 bits=32), got tag={tag} bits={bits}")
    samples = array.array('f')
    samples.frombytes(raw[:len(raw) // 4 * 4])
    return sr, ch, samples


def load_active(path, on_thresh=None):
    """Load a capture and trim it to the active (tone-playing) region.

    on_thresh: absolute activity threshold; None = 20% of the measured peak.
    Returns dict with sr/ch/seg (ch0)/other (ch1 or None)/lead_ms/dur/nframes.
    """
    sr, ch, inter = read_wav_f32(path)
    nframes = len(inter) // ch
    if nframes == 0:
        sys.exit(f"error: empty capture: {path}")
    mono = inter[0::ch]
    if on_thresh is None:
        on_thresh = 0.2 * max(abs(x) for x in mono)
        if on_thresh == 0.0:
            sys.exit(f"FAIL: {path}: all silence")
    first = next((i for i, x in enumerate(mono) if abs(x) > on_thresh), None)
    last = next((i for i in range(len(mono) - 1, -1, -1)
                 if abs(mono[i]) > on_thresh), None)
    if first is None:
        sys.exit(f"FAIL: {path}: no signal above threshold (all silence)")
    a, b = first + sr // 10, last - sr // 5
    if b - a < sr:
        sys.exit(f"FAIL: {path}: active region too short ({(b - a) / sr:.2f}s after trim)")
    return {
        'path': path, 'sr': sr, 'ch': ch, 'nframes': nframes,
        'seg': mono[a:b],
        'other': inter[1::ch][a:b] if ch >= 2 else None,
        'lead_ms': first * 1000.0 / sr,
        'dur': (b - a) / sr,
    }


def integrity_checks(seg, slope_lim):
    """Gaps / clicks / DC over the active segment (signal-agnostic)."""
    gaps, run, max_run = 0, 0, 0
    for x in seg:
        if abs(x) < 1e-4:
            run += 1
        else:
            if run >= 32:
                gaps += 1
            max_run = max(max_run, run)
            run = 0
    if run >= 32:
        gaps += 1
    max_run = max(max_run, run)

    max_d = max(abs(seg[i] - seg[i - 1]) for i in range(1, len(seg)))
    clicks = sum(1 for i in range(1, len(seg)) if abs(seg[i] - seg[i - 1]) > slope_lim)
    dc = sum(seg) / len(seg)
    return [
        ("dc", abs(dc) < 1e-3, f"{dc:+.6f}"),
        ("gaps", gaps == 0, f"{gaps} (longest near-silence run {max_run} samples)"),
        ("clicks", clicks == 0, f"{clicks} (max sample delta {max_d:.5f}, limit {slope_lim:.5f})"),
    ]


def ch_match_check(seg, other):
    if other is None:
        return []
    mism = max(abs(x - y) for x, y in zip(seg, other))
    return [("ch-match", mism < 1e-6, f"max |L-R| {mism:.2e}")]


def tone_amps(seg, sr, freqs):
    """Per-tone amplitude via Goertzel over a Hann-windowed segment."""
    n = len(seg)
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (n - 1)) for i in range(n)]
    wsum = sum(window)
    wx = [w * x for w, x in zip(window, seg)]
    amps = {}
    for f in freqs:
        coeff = 2.0 * math.cos(2.0 * math.pi * f / sr)
        s1 = s2 = 0.0
        for v in wx:
            s0 = v + coeff * s1 - s2
            s2, s1 = s1, s0
        power = s1 * s1 + s2 * s2 - coeff * s1 * s2
        amps[f] = 2.0 * math.sqrt(max(power, 0.0)) / wsum
    return amps


def db(amp):
    return 20.0 * math.log10(max(amp, 1e-12))


def rms(xs):
    return math.sqrt(sum(x * x for x in xs) / len(xs)) if xs else 0.0


def run_stereo(args):
    """Mid/side mode for Phase 7 spatial: the sim (--stereo) writes a 440 Hz
    tone anti-phase across L/R, so the input is pure side. The captured side
    RMS scales with the engine's spatial width/room (sideGain). With --compare
    FLAT.wav, the side RMS ratio vs the flat capture IS the width effect, and
    --expect-side asserts it (e.g. narrow width=-100 → 0.40, wide +100 → 1.60)."""
    cap = load_active(args.wav)
    left, sr = cap['seg'], cap['sr']
    right = cap['other']
    if right is None:
        sys.exit(f"FAIL: {args.wav}: --stereo needs a 2-channel capture")
    mid = [(l + r) * 0.5 for l, r in zip(left, right)]
    side = [(l - r) * 0.5 for l, r in zip(left, right)]
    l_rms, r_rms, m_rms, s_rms = rms(left), rms(right), rms(mid), rms(side)

    slope_lim = 3.0 * 2.0 * math.pi * args.freq / sr * max(abs(x) for x in left)
    checks = integrity_checks(left, slope_lim)

    ratio = None
    if args.compare:
        ref = load_active(args.compare)
        if ref['sr'] != sr:
            sys.exit(f"error: sample-rate mismatch ({sr} vs {ref['sr']}) — "
                     "capture both on the same device")
        if ref['other'] is None:
            sys.exit(f"FAIL: {args.compare}: --stereo reference needs 2 channels")
        ref_side = [(l - r) * 0.5 for l, r in zip(ref['seg'], ref['other'])]
        ref_s_rms = rms(ref_side)
        ratio = s_rms / ref_s_rms if ref_s_rms > 1e-9 else float('inf')
        detail = f"side {s_rms:.4f} / flat {ref_s_rms:.4f} = x{ratio:.3f}"
        passed = True
        if args.expect_side is not None:
            passed = abs(ratio - args.expect_side) <= args.tol_ratio
            detail += f" (expect x{args.expect_side:g} +/-{args.tol_ratio})"
        checks.append(("side-ratio", passed, detail))
    elif args.expect_side is not None:
        sys.exit("error: --expect-side requires --compare FLAT.wav")

    return cap, checks, (l_rms, r_rms, m_rms, s_rms), ratio


def run_single_sine(args):
    cap = load_active(args.wav, on_thresh=args.peak * 0.2)
    seg, sr = cap['seg'], cap['sr']

    peak = max(abs(x) for x in seg)
    crossings = sum(1 for i in range(1, len(seg))
                    if (seg[i - 1] >= 0.0) != (seg[i] >= 0.0))
    freq = crossings * sr / (2.0 * len(seg))
    slope_lim = 3.0 * 2.0 * math.pi * args.freq / sr * args.peak

    checks = [
        ("freq", abs(freq - args.freq) <= 2.0, f"{freq:.2f} Hz (expect {args.freq:g} ±2)"),
        ("peak", abs(peak - args.peak) <= args.peak * 0.02, f"{peak:.5f} (expect {args.peak:g} ±2%)"),
    ]
    checks += integrity_checks(seg, slope_lim)
    checks += ch_match_check(seg, cap['other'])
    return cap, checks


def run_tones(args, freqs):
    cap = load_active(args.wav)
    seg, sr = cap['seg'], cap['sr']
    amps = tone_amps(seg, sr, freqs)

    # Click limit from the measured tone set: 3x the sum of per-tone max slopes.
    slope_lim = 3.0 * sum(2.0 * math.pi * f * a for f, a in amps.items()) / sr
    checks = integrity_checks(seg, slope_lim)
    checks += ch_match_check(seg, cap['other'])

    deltas, expect = None, {}
    if args.compare:
        ref = load_active(args.compare)
        if ref['sr'] != sr:
            sys.exit(f"error: sample-rate mismatch ({sr} vs {ref['sr']}) — "
                     "capture both on the same device")
        ref_amps = tone_amps(ref['seg'], ref['sr'], freqs)
        deltas = {f: db(amps[f]) - db(ref_amps[f]) for f in freqs}
        if args.expect:
            for item in args.expect.split(','):
                fs, _, dbs = item.partition(':')
                f = float(fs)
                hit = [t for t in freqs if abs(t - f) <= 1e-4 * max(t, 1.0)]
                if not hit:
                    sys.exit(f"error: --expect {f:g} Hz is not in the tone set")
                expect[hit[0]] = float(dbs)
    elif args.expect:
        sys.exit("error: --expect requires --compare REF.wav")

    return cap, checks, amps, deltas, expect


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('wav')
    p.add_argument('--freq', type=float, default=440.0,
                   help='single-sine mode: expected tone Hz')
    p.add_argument('--peak', type=float, default=0.25,
                   help='single-sine mode: expected peak')
    p.add_argument('--tones', metavar='LIST',
                   help="tone-set mode: 'eq10' or comma-separated Hz")
    p.add_argument('--compare', metavar='REF.wav',
                   help='tone-set mode: reference capture for delta dB')
    p.add_argument('--expect', metavar='F:DB[,F:DB...]',
                   help='expected delta dB for listed tones (needs --compare)')
    p.add_argument('--tol', type=float, default=1.0,
                   help='tolerance dB for tones listed in --expect')
    p.add_argument('--tol-rest', type=float, default=2.0, dest='tol_rest',
                   help='tolerance dB around 0 for unlisted tones')
    p.add_argument('--stereo', action='store_true',
                   help='mid/side mode for Phase 7 spatial (sim --stereo)')
    p.add_argument('--expect-side', type=float, default=None, dest='expect_side',
                   help='stereo mode: expected side RMS ratio vs --compare flat')
    p.add_argument('--tol-ratio', type=float, default=0.05, dest='tol_ratio',
                   help='stereo mode: tolerance around --expect-side')
    args = p.parse_args()

    ms = None
    if args.stereo:
        cap, checks, ms, _ = run_stereo(args)
        amps = deltas = expect = None
    elif args.tones:
        freqs = EQ10 if args.tones == 'eq10' else [float(s) for s in args.tones.split(',')]
        cap, checks, amps, deltas, expect = run_tones(args, freqs)
    else:
        cap, checks = run_single_sine(args)
        amps = deltas = expect = None

    print(f"{cap['path']}: sr={cap['sr']} ch={cap['ch']} frames={cap['nframes']} "
          f"active={cap['dur']:.2f}s (analyzed, lead-in {cap['lead_ms']:.0f} ms)")
    ok = True
    for name, passed, detail in checks:
        print(f"  {'PASS' if passed else 'FAIL'}  {name:9s} {detail}")
        ok = ok and passed

    if ms is not None:
        l_rms, r_rms, m_rms, s_rms = ms
        print(f"  mid/side RMS: L={l_rms:.4f} R={r_rms:.4f} "
              f"mid={m_rms:.4f} side={s_rms:.4f}")

    if amps is not None:
        if deltas is None:
            print("  tone levels (dBFS):")
            for f in sorted(amps):
                print(f"        {f:8g} Hz  {db(amps[f]):+7.2f}")
        else:
            print(f"  tone deltas vs {args.compare} (dB):")
            for f in sorted(deltas):
                d = deltas[f]
                if f in expect:
                    want, tol = expect[f], args.tol
                    passed = abs(d - want) <= tol
                    note = f"expect {want:+.1f} ±{tol:g}"
                else:
                    want, tol = 0.0, args.tol_rest
                    passed = abs(d) <= tol
                    note = f"rest ±{tol:g}"
                if expect:
                    print(f"  {'PASS' if passed else 'FAIL'}  {f:8g} Hz  {d:+7.2f}  ({note})")
                    ok = ok and passed
                else:
                    print(f"        {f:8g} Hz  {d:+7.2f}")

    print("RESULT: PASS" if ok else "RESULT: FAIL")
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
