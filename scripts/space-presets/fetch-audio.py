#!/usr/bin/env python3
"""Fetch the recordings tune.cpp measures on and convert them to 48 kHz WAV.

    python3 scripts/space-presets/fetch-audio.py <out dir> [--dsf <Shostakovich .dsf>]

Every file is listed in audio.tsv with its Internet Archive item and licence;
all were released by their authors under Creative Commons or into the public
domain. They are measurement input only and are never committed or shipped.
The NativeDSD Shostakovich excerpt is not downloadable here: pass the .dsf from
the NativeDSD starter pack with --dsf and it is decoded by dsf2wav.cpp.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--dsf")
    args = ap.parse_args()
    os.makedirs(os.path.join(args.out, "raw"), exist_ok=True)
    for line in open(os.path.join(HERE, "audio.tsv"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        key, item, name, licence = line.rstrip("\n").split("\t")
        raw = os.path.join(args.out, "raw", key + os.path.splitext(name)[1])
        wav = os.path.join(args.out, key + ".wav")
        if not os.path.exists(raw):
            url = "https://archive.org/download/" + item + "/" + urllib.parse.quote(name)
            urllib.request.urlretrieve(url, raw)
        subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEF32@48000", "-c", "2", raw, wav], check=True)
        print(f"{key:10s} {os.path.getsize(raw) / 1e6:5.1f} MB  {licence}  {name}")
    if args.dsf:
        with tempfile.TemporaryDirectory() as tmp:
            tool = os.path.join(tmp, "dsf2wav")
            subprocess.run(["clang++", "-std=c++17", "-O3", os.path.join(HERE, "dsf2wav.cpp"), "-o", tool], check=True)
            mid = os.path.join(tmp, "shosta44.wav")
            subprocess.run([tool, args.dsf, mid], check=True)
            subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEF32@48000", "-c", "2", mid,
                            os.path.join(args.out, "shosta.wav")], check=True)
        print("shosta     decoded from", args.dsf)
    elif not os.path.exists(os.path.join(args.out, "shosta.wav")):
        print("note: no shosta.wav — the classical and general presets need it (--dsf)", file=sys.stderr)


if __name__ == "__main__":
    main()
