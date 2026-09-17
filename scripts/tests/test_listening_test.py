"""청취 도구의 채점과 진행 흐름을 가짜 roomcutctl로 검증합니다."""

import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "listening-test.py"
spec = importlib.util.spec_from_file_location("listening_test", SCRIPT)
lt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lt)


FAKE_CTL = """#!/usr/bin/env python3
import json, sys
from pathlib import Path
state = Path(__file__).with_name("state.json")
params = json.loads(state.read_text())
log = Path(__file__).with_name("calls.log")
with log.open("a") as f:
    f.write(" ".join(sys.argv[1:]) + "\\n")
args = sys.argv[1:]
if args[:2] == ["params", "get"]:
    print(json.dumps(params))
elif args[:2] == ["params", "set"]:
    for name, value in zip(args[2::2], args[3::2]):
        params[name] = float(value)
    state.write_text(json.dumps(params))
elif args[:1] == ["status"]:
    print(json.dumps({"bedRenderer": "system", "bedUnitRate": 96000.0, "bedPersonalizedHrtf": False,
                      "outputDevice": "fake"}))
"""


class ScoringTests(unittest.TestCase):
    def test_angles_wrap_round_the_circle(self):
        self.assertEqual(lt.angle_error(170, -170), 20)
        self.assertEqual(lt.angle_error(-135, 135), 90)
        self.assertEqual(lt.angle_error(0, 180), 180)

    def test_front_back_confusion_ignores_the_sides(self):
        self.assertTrue(lt.front_back_confused(30, 135))
        self.assertTrue(lt.front_back_confused(-110, -45))
        self.assertFalse(lt.front_back_confused(30, 45))
        self.assertFalse(lt.front_back_confused(30, 90), "an answer straight to the side is not front or back")
        self.assertFalse(lt.front_back_confused(-135, 180))

    def test_summary_counts_only_front_or_back_targets(self):
        trials = [
            {"channel": "L", "target": -30, "answer": -45},
            {"channel": "Ls", "target": -90, "answer": -135},    # side target: not eligible
            {"channel": "Lb", "target": -135, "answer": -45},    # back heard in front
        ]
        s = lt.summarize_channels(trials)
        self.assertEqual(s["front_back_trials"], 2)
        self.assertEqual(s["front_back_confusions"], 1)
        self.assertAlmostEqual(s["mean_error"], (15 + 45 + 90) / 3)
        self.assertEqual(s["per_channel"]["Lb"]["mean_error"], 90)

    def test_abx_p_value_is_the_binomial_tail(self):
        self.assertAlmostEqual(lt.abx_p_value(12, 12), 1 / 4096)
        self.assertAlmostEqual(lt.abx_p_value(0, 12), 1.0)
        self.assertAlmostEqual(lt.abx_p_value(9, 12), (220 + 66 + 12 + 1) / 4096)

    def test_fields_parse_and_reject_garbage(self):
        self.assertEqual(lt.parse_fields("surroundType=2, bedRenderer=1"), {"surroundType": 2.0, "bedRenderer": 1.0})
        with self.assertRaises(ValueError):
            lt.parse_fields("surroundType")


class FlowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.ctl = root / "roomcutctl"
        self.ctl.write_text(FAKE_CTL)
        self.ctl.chmod(self.ctl.stat().st_mode | stat.S_IEXEC)
        self.state = root / "state.json"
        self.state.write_text(json.dumps({"spatialMode": 0.0, "surroundType": 2.0, "bedRenderer": 0.0,
                                          "surroundDepth": 50.0, "centerWidth": 100.0, "roomType": 0.0}))
        self.out = root / "verification"
        patches = [mock.patch.object(lt, "CTL", self.ctl), mock.patch.object(lt, "OUT", self.out),
                   mock.patch.object(lt, "ROOT", root)]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)

    def calls(self):
        return (self.ctl.with_name("calls.log")).read_text().splitlines()

    def test_channel_identification_records_and_puts_the_engine_back(self):
        # Level check, then one answer per burst: every burst heard as straight ahead.
        answers = iter([""] + ["1"] * 7)
        with mock.patch("builtins.input", lambda _: next(answers)), mock.patch("builtins.print"):
            lt.main(["channels", "--tracking", "off", "--configure", "3", "--repeats", "1", "--seed", "4"])
        params = json.loads(self.state.read_text())
        self.assertEqual((params["spatialMode"], params["surroundType"]), (0.0, 2.0), "the engine is put back")
        probes = [c for c in self.calls() if c.startswith("probe ")]
        self.assertEqual(len(probes), 1 + 7 + 1, "level check, seven channels, and a stop")
        self.assertEqual(sorted(p.split()[1] for p in probes[1:8]), sorted(lt.CHANNELS[3]))
        report = next(self.out.glob("*-listening-channels-*.md")).read_text()
        self.assertIn("앞뒤 혼동: **2/5 = 40%**", report, "C/L/R/Lb/Rb count (Ls/Rs are at the sides); Lb and Rb were heard in front")
        self.assertIn("기준 미달", report)

    def test_abx_switches_only_the_named_field_and_scores(self):
        # Two trials: listen to X then answer; the seed decides what X is.
        answers = iter(["x", "xa", "x", "xb"])
        with mock.patch("builtins.input", lambda _: next(answers)), mock.patch("builtins.print"):
            lt.main(["abx", "--tracking", "on", "--a", "surroundType=2", "--b", "surroundType=3",
                     "--trials", "2", "--seed", "1"])
        sets = [c for c in self.calls() if c.startswith("params set")]
        self.assertTrue(all(c.split()[2] == "surroundType" for c in sets), sets)
        self.assertEqual(json.loads(self.state.read_text())["surroundType"], 2.0, "put back after the test")
        report = next(self.out.glob("*-listening-abx-*.md")).read_text()
        self.assertRegex(report, r"정답: \*\*\d/2")


if __name__ == "__main__":
    unittest.main()
