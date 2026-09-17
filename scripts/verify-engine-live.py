#!/usr/bin/env python3
"""설치된 Roomcut 엔진이 실제로 제대로 돌고 있는지 한 번에 확인합니다.

재부팅 뒤나 `install-engine.sh` 뒤에 실행합니다. 기본은 읽기만 합니다.

  1. /etc/sudoers.d/roomcut-engine 가 있다 (없으면 앱이 엔진을 시작·정지하지 못한다)
  2. launchd 의 system/com.roomcut.engine 이 돌고 있다
  3. 엔진이 응답하고 RUNNING, 안전 바이패스가 걸려 있지 않다
  4. 설치된 엔진 바이너리가 build/ 의 것과 같다 (다르면 설치가 낡았다)
  5. 몇 초 동안 렌더된 프레임 ÷ 출력 장치 레이트 ≈ 1.00x, underrun 증가 0
     (N배면 소비자가 여럿이다 — 2026-07 드롭아웃 사고의 지표)
  6. 엔진 로그의 헤드폰 베드 렌더러 줄

--headphone-bed 를 주면 잠시 헤드폰 7.1로 바꿔 시스템 렌더러가 베드를 실제로
렌더하는지(렌더 비율 100%)와 채널 프로브가 출력에 나오는지(-45 dBFS, 1 s)를
확인하고, 바꾼 필드만 원래 값으로 되돌립니다.

표준 라이브러리만 씁니다.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CTL = ROOT / "build/engine/roomcutctl"
BUILT_ENGINE = ROOT / "build/engine/RoomcutAudioEngine"
INSTALLED_ENGINE = Path("/Library/Application Support/Roomcut/bin/RoomcutAudioEngine")
ERR_LOG = Path("/Library/Logs/Roomcut/engine.err.log")
SUDOERS = Path("/etc/sudoers.d/roomcut-engine")
LABEL = "system/com.roomcut.engine"

OPEN_LINE = re.compile(r"output device open '(?P<name>.*)' @ (?P<rate>\d+) Hz")
BED_LINE = re.compile(r"\[engine\] bed renderer: (?P<text>.*)")


def last_match(pattern, text):
    found = None
    for line in text.splitlines():
        m = pattern.search(line)
        if m:
            found = m
    return found


def frame_ratio(frames_before, frames_after, seconds, rate):
    return (frames_after - frames_before) / seconds / rate


class Report:
    def __init__(self):
        self.failures = 0

    def check(self, ok, what, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}  {what}{(' — ' + detail) if detail else ''}")
        if not ok:
            self.failures += 1
        return ok

    def note(self, what):
        print(f"      {what}")


def ctl(*args):
    result = subprocess.run([str(CTL), *map(str, args)], capture_output=True, text=True, timeout=10)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def headphone_bed_check(report):
    before = json.loads(ctl("params", "get", "--json"))
    touched = {k: before[k] for k in ("spatialMode", "surroundType", "bedRenderer")}
    try:
        ctl("params", "set", "spatialMode", 1, "surroundType", 3, "bedRenderer", 0)
        time.sleep(1.0)
        status = json.loads(ctl("status", "--json"))
        if status.get("bedRenderer") == "system":
            report.check(status.get("bedExternalGain", 0) >= 0.99, "헤드폰 7.1에서 시스템 렌더러가 베드를 렌더한다",
                         f"렌더 비율 {100 * status.get('bedExternalGain', 0):.0f}%, 유닛 {status.get('bedUnitRate', 0):g} Hz")
        else:
            report.note("시스템 렌더러가 붙어 있지 않아 내장 베드로 렌더합니다(--bed-renderer builtin 설치).")
        ctl("probe", "C", 1.0, -45)
        time.sleep(0.4)
        peak = json.loads(ctl("status", "--json")).get("peak", 0.0)
        report.check(peak > 0.0, "채널 프로브(-45 dBFS)가 출력에 나온다", f"peak {peak:.5f}")
        time.sleep(0.8)
    finally:
        ctl("params", "set", *[x for k, v in touched.items() for x in (k, v)])
        after = json.loads(ctl("params", "get", "--json"))
        same = all(after[k] == before[k] for k in before if k not in ("paramsRevision", "preset"))
        report.check(same, "바꾼 필드를 원래 값으로 되돌렸다")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=5.0, help="프레임 비율을 재는 시간")
    parser.add_argument("--headphone-bed", action="store_true", help="잠시 헤드폰 7.1로 바꿔 베드 렌더·프로브 확인")
    args = parser.parse_args(argv)
    report = Report()

    report.check(SUDOERS.exists(), "sudoers 파일", str(SUDOERS))
    launchd = subprocess.run(["launchctl", "print", LABEL], capture_output=True, text=True)
    report.check(launchd.returncode == 0 and "state = running" in launchd.stdout, "launchd 데몬 실행 중", LABEL)

    try:
        status = json.loads(ctl("status", "--json"))
    except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        report.check(False, "엔진 응답", str(error))
        return 1
    report.check(status.get("state") == "RUNNING" and not status.get("safeBypass"), "엔진 RUNNING, 안전 바이패스 없음",
                 f"state {status.get('state')}, safeBypass {status.get('safeBypass')}")

    if INSTALLED_ENGINE.exists() and BUILT_ENGINE.exists():
        report.check(sha256(INSTALLED_ENGINE) == sha256(BUILT_ENGINE), "설치된 엔진 = build/ 엔진",
                     "다르면 install-engine.sh 로 다시 설치" if sha256(INSTALLED_ENGINE) != sha256(BUILT_ENGINE) else "")
    else:
        report.note("설치본 또는 build/ 엔진이 없어 바이너리 비교를 건너뜁니다.")

    log = ERR_LOG.read_text(errors="replace") if ERR_LOG.exists() else ""
    opened = last_match(OPEN_LINE, log)
    bed = last_match(BED_LINE, log)
    if opened:
        rate = int(opened["rate"])
        first = json.loads(ctl("status", "--json"))
        started = time.monotonic()
        time.sleep(args.seconds)
        after_status = json.loads(ctl("status", "--json"))
        elapsed = time.monotonic() - started
        before = first["frames"], first["underruns"]
        ratio = frame_ratio(before[0], after_status["frames"], elapsed, rate)
        report.check(0.97 <= ratio <= 1.03, "프레임 비율 ≈ 1.00x", f"{ratio:.3f}x of {rate} Hz ('{opened['name']}')")
        report.check(after_status["underruns"] == before[1], "underrun 증가 없음",
                     f"+{after_status['underruns'] - before[1]}")
    else:
        report.check(False, "엔진 로그에서 출력 장치 레이트를 찾음", str(ERR_LOG))
    report.note(f"헤드폰 베드 렌더러(로그): {bed['text'] if bed else '기록 없음'}")
    report.note(f"개인화 HRTF: {'사용' if status.get('bedPersonalizedHrtf') else '사용 안 함'}")

    if args.headphone_bed:
        headphone_bed_check(report)

    print(f"\n{'모두 통과' if report.failures == 0 else f'{report.failures}건 실패'}")
    return 0 if report.failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
