#!/usr/bin/env python3
"""Roomcut 청취 프로토콜(PRD P0-4)을 실제 엔진으로 진행하고 결과를 기록합니다.

  channels  채널 식별: 업믹스 채널마다 핑크노이즈 버스트를 무작위 순서로 들려주고
            들린 방향(8방향)을 받아 방위 오차와 앞뒤 혼동률을 계산합니다.
  abx       블라인드 ABX: 두 조건(예: surroundType=2 대 3, bedRenderer=0 대 1)을
            엔진에서 바꿔 가며 X가 A인지 B인지 답합니다. 음악을 틀어 둔 채 진행합니다.
  rate      현재 조건을 5점 척도로 평가합니다(SAQI 용어를 어휘로만 사용).

결과는 docs/development/verification/ 에 Markdown으로 남습니다. 엔진 설정은
`roomcutctl params set`으로 바꾼 필드만 건드리고, 끝나면(중단해도) 원래 값으로
되돌립니다. 헤드 트래킹은 앱에서 켜고 끈 뒤 --tracking 으로 적어 주세요.

표준 라이브러리만 씁니다.
"""

import argparse
import datetime
import json
import math
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CTL = ROOT / "build/engine/roomcutctl"
OUT = ROOT / "docs/development/verification"

# Upmixer 채널 순서와 SurroundStage/AUSpatialMixer 배치각(+ = 오른쪽).
CHANNELS = {
    2: {"C": 0, "L": -30, "R": 30, "Ls": -110, "Rs": 110},
    3: {"C": 0, "L": -30, "R": 30, "Ls": -90, "Rs": 90, "Lb": -135, "Rb": 135},
}
# 키패드 배치가 아니라 시계 방향 번호: 1 앞, 3 오른쪽, 5 뒤, 7 왼쪽.
ANSWERS = {"1": 0, "2": 45, "3": 90, "4": 135, "5": 180, "6": -135, "7": -90, "8": -45}
# PRD P0-FR2: 앞뒤 혼동률 기준.
CONFUSION_LIMIT = {"off": 0.25, "on": 0.10}
ABX_PASS = 0.75   # P0-FR5


def angle_error(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def front_back_confused(target, answer):
    """정확히 옆(±90°)은 어느 쪽도 아니므로 혼동으로 치지 않는다."""
    def side(az):
        az = (az + 180) % 360 - 180
        if abs(az) < 90:
            return "front"
        if abs(az) > 90:
            return "back"
        return None
    t, a = side(target), side(answer)
    return t is not None and a is not None and t != a


def summarize_channels(trials):
    per = {}
    for t in trials:
        per.setdefault(t["channel"], []).append(angle_error(t["target"], t["answer"]))
    eligible = [t for t in trials if abs(t["target"]) != 90]
    confused = [t for t in eligible if front_back_confused(t["target"], t["answer"])]
    errors = [angle_error(t["target"], t["answer"]) for t in trials]
    return {
        "per_channel": {ch: {"n": len(v), "mean_error": sum(v) / len(v)} for ch, v in per.items()},
        "mean_error": sum(errors) / len(errors) if errors else float("nan"),
        "front_back_trials": len(eligible),
        "front_back_confusions": len(confused),
        "confusion_rate": len(confused) / len(eligible) if eligible else float("nan"),
    }


def abx_p_value(correct, trials):
    """맞힌 수 이상이 우연(p = 0.5)으로 나올 확률, 한쪽 이항 검정."""
    return sum(math.comb(trials, k) for k in range(correct, trials + 1)) / 2 ** trials


def parse_fields(text):
    fields = {}
    for part in text.split(","):
        name, _, value = part.partition("=")
        if not name.strip() or not value.strip():
            raise ValueError(f"'{text}': name=value 형식이어야 합니다")
        fields[name.strip()] = float(value)
    return fields


# ---- engine ----

def ctl(*args):
    result = subprocess.run([str(CTL), *map(str, args)], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"roomcutctl {' '.join(map(str, args))}: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def engine_params():
    return json.loads(ctl("params", "get", "--json"))


def engine_status():
    return json.loads(ctl("status", "--json"))


def set_fields(fields):
    if fields:
        ctl("params", "set", *[x for name, value in fields.items() for x in (name, value)])


class Restore:
    """바꾼 필드만 기억했다가 원래 값으로 돌린다."""

    def __init__(self):
        self.original = {}

    def change(self, fields):
        current = engine_params()
        for name in fields:
            if name not in current:
                raise ValueError(f"알 수 없는 필드: {name}")
            self.original.setdefault(name, current[name])
        set_fields(fields)

    def restore(self):
        if self.original:
            set_fields(self.original)
            print(f"엔진 설정을 원래대로 되돌렸습니다: {self.original}")


def condition_text(params, status, tracking):
    layout = {2: "5.1", 3: "7.1"}.get(int(params["surroundType"]), "업믹스 꺼짐")
    renderer = "내장" if params["bedRenderer"] >= 1 else (
        f"AUSpatialMixer ({status.get('bedUnitRate', 0) / 1000:g} kHz 유닛)" if status.get("bedRenderer") == "system"
        else "내장(시스템 렌더러 없음)")
    return (f"레이아웃 {layout} · 헤드폰 베드 {renderer} · 헤드 트래킹 {tracking} · "
            f"개인화 HRTF {'사용' if status.get('bedPersonalizedHrtf') else '사용 안 함'} · "
            f"Surround Depth {params['surroundDepth']:.0f} · Center Width {params['centerWidth']:.0f} · "
            f"룸 {params['roomType']:.0f}")


def ask(prompt, allowed):
    while True:
        reply = input(prompt).strip().lower()
        if reply in allowed:
            return reply
        print(f"  {', '.join(sorted(allowed))} 중에서 입력해 주세요.")


def write_result(kind, title, lines):
    OUT.mkdir(parents=True, exist_ok=True)
    now = datetime.datetime.now()
    path = OUT / f"{now:%Y-%m-%d}-listening-{kind}-{now:%H%M%S}.md"
    path.write_text(f"# {title}\n\n- 일시: {now:%Y-%m-%d %H:%M:%S}\n" + "\n".join(lines) + "\n", encoding="utf-8")
    print(f"결과를 기록했습니다: {path.relative_to(ROOT)}")


# ---- tests ----

def run_channels(args, restore):
    if args.configure:
        restore.change({"spatialMode": 1, "surroundType": args.configure})
    params = engine_params()
    if params["spatialMode"] not in (1.0, 2.0) or params["surroundType"] < 2:
        raise SystemExit("헤드폰 5.1/7.1이 아닙니다. Space 탭에서 바꾸거나 --configure 2|3 을 주세요.")
    layout = int(params["surroundType"])
    targets = CHANNELS[layout]
    status = engine_status()
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    order = [ch for ch in targets for _ in range(args.repeats)]
    random.Random(seed).shuffle(order)

    print(condition_text(params, status, args.tracking))
    print("음악은 멈춰 주세요. 먼저 센터 버스트로 음량을 맞춥니다.")
    while True:
        ctl("probe", "C", args.seconds, args.level)
        if ask("  괜찮으면 Enter, 다시 듣기 r: ", {"", "r"}) == "":
            break
    print("방향 번호: 1 앞, 2 앞오른쪽, 3 오른쪽, 4 뒤오른쪽, 5 뒤, 6 뒤왼쪽, 7 왼쪽, 8 앞왼쪽 (r 다시, q 중단)")
    trials = []
    for i, channel in enumerate(order, 1):
        while True:
            ctl("probe", channel, args.seconds, args.level)
            reply = ask(f"[{i}/{len(order)}] 어디서 들렸나요? ", set(ANSWERS) | {"r", "q"})
            if reply != "r":
                break
        if reply == "q":
            print("중단했습니다. 여기까지의 답을 기록합니다.")
            break
        trials.append({"channel": channel, "target": targets[channel], "answer": ANSWERS[reply]})
    if not trials:
        return
    s = summarize_channels(trials)
    limit = CONFUSION_LIMIT.get(args.tracking)
    lines = [f"- 조건: {condition_text(params, status, args.tracking)}",
             f"- 버스트: {args.seconds} s, {args.level} dBFS, 채널당 {args.repeats}회, 시드 {seed}",
             f"- 출력 장치: {status.get('outputDevice', '?')}", "",
             "| 채널 | 목표 방위 | 시도 | 평균 오차 |", "|---|---|---|---|"]
    for ch in targets:
        if ch in s["per_channel"]:
            v = s["per_channel"][ch]
            lines.append(f"| {ch} | {targets[ch]:+d}° | {v['n']} | {v['mean_error']:.1f}° |")
    verdict = ""
    if limit is not None and s["front_back_trials"]:
        verdict = " — **기준 통과**" if s["confusion_rate"] <= limit else " — **기준 미달**"
    lines += ["", f"- 전체 평균 방위 오차: **{s['mean_error']:.1f}°** (P0-FR1 수치 기준 10°는 하네스용, 청취는 참고)",
              f"- 앞뒤 혼동: **{s['front_back_confusions']}/{s['front_back_trials']}"
              f" = {100 * s['confusion_rate']:.0f}%** (P0-FR2 기준: 트래킹 Off ≤ 25%, On ≤ 10%){verdict}",
              "", "원자료:", "", "```json", json.dumps(trials, ensure_ascii=False), "```"]
    write_result("channels", "청취: 채널 식별", lines)


def run_abx(args, restore):
    a, b = parse_fields(args.a), parse_fields(args.b)
    if set(a) != set(b):
        raise SystemExit("A와 B는 같은 필드를 바꿔야 합니다.")
    restore.change(a)
    params, status = engine_params(), engine_status()
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    rng = random.Random(seed)
    print(f"A = {args.a}, B = {args.b}. 음악을 틀어 두세요.")
    print("a / b / x 로 들을 조건을 바꾸고, 판단이 서면 xa(X는 A) 또는 xb(X는 B)로 답합니다. q 중단.")
    answers = []
    for i in range(1, args.trials + 1):
        x_is_a = rng.random() < 0.5
        current = None
        while True:
            reply = ask(f"[{i}/{args.trials}] ", {"a", "b", "x", "xa", "xb", "q"})
            if reply in ("a", "b", "x"):
                target = a if reply == "a" else b if reply == "b" else (a if x_is_a else b)
                if target is not current:
                    set_fields(target)
                    current = target
                print(f"  지금: {reply.upper()}")
                continue
            break
        if reply == "q":
            print("중단했습니다.")
            break
        answers.append({"x": "A" if x_is_a else "B", "answer": reply[1].upper()})
    if not answers:
        return
    correct = sum(1 for r in answers if r["x"] == r["answer"])
    n = len(answers)
    rate = correct / n
    lines = [f"- 조건(시작 시점): {condition_text(params, status, args.tracking)}",
             f"- A: `{args.a}` · B: `{args.b}` · 시드 {seed}", "",
             f"- 정답: **{correct}/{n} = {100 * rate:.0f}%**, 우연히 이만큼 맞힐 확률 p = {abx_p_value(correct, n):.3f}",
             f"- P0-FR5 기준(정답률 ≥ 75%): **{'통과' if rate >= ABX_PASS else '미달'}**", "",
             "원자료:", "", "```json", json.dumps(answers, ensure_ascii=False), "```"]
    write_result("abx", "청취: ABX", lines)


SAQI_TERMS = [
    ("externalization", "외재화 — 소리가 머리 밖에 있다"),
    ("localizability", "정위 가능성 — 방향을 짚을 수 있다"),
    ("timbre", "음색 — 원음과 비교해 자연스럽다"),
    ("naturalness", "전체 자연스러움"),
]


def run_rate(args, restore):
    params, status = engine_params(), engine_status()
    print(condition_text(params, status, args.tracking))
    print("1(매우 나쁨) ~ 5(매우 좋음)로 답해 주세요.")
    scores = {key: int(ask(f"  {label}: ", {"1", "2", "3", "4", "5"})) for key, label in SAQI_TERMS}
    note = input("  메모(선택): ").strip()
    lines = [f"- 조건: {condition_text(params, status, args.tracking)}", f"- 라벨: {args.label or '-'}", "",
             "| 항목 | 점수 |", "|---|---|"]
    lines += [f"| {label} | {scores[key]} |" for key, label in SAQI_TERMS]
    if note:
        lines += ["", f"메모: {note}"]
    write_result("rate", "청취: 5점 평가", lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="test", required=True)
    for name in ("channels", "abx", "rate"):
        p = sub.add_parser(name)
        p.add_argument("--tracking", choices=["on", "off"], required=True, help="앱에서 설정한 헤드 트래킹 상태")
        if name == "channels":
            p.add_argument("--repeats", type=int, default=3)
            p.add_argument("--seconds", type=float, default=0.5)
            p.add_argument("--level", type=float, default=-25.0, help="버스트 RMS dBFS (-60..0)")
            p.add_argument("--configure", type=int, choices=[2, 3], help="헤드폰 5.1(2)/7.1(3)으로 잠시 바꿨다가 되돌림")
            p.add_argument("--seed", type=int)
        elif name == "abx":
            p.add_argument("--a", required=True, help="예: surroundType=2")
            p.add_argument("--b", required=True, help="예: surroundType=3")
            p.add_argument("--trials", type=int, default=12)
            p.add_argument("--seed", type=int)
        else:
            p.add_argument("--label", help="예: 영화 대사, 라이브")
    args = parser.parse_args(argv)
    if not CTL.exists():
        raise SystemExit(f"{CTL} 가 없습니다. cmake --build build 를 먼저 실행해 주세요.")
    restore = Restore()
    try:
        {"channels": run_channels, "abx": run_abx, "rate": run_rate}[args.test](args, restore)
    except KeyboardInterrupt:
        print("\n중단했습니다.")
    finally:
        restore.restore()
        if args.test == "channels":
            try:
                ctl("probe", "stop")
            except RuntimeError:
                pass


if __name__ == "__main__":
    main()
