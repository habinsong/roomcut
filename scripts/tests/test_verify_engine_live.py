"""verify-engine-live.py 의 로그 해석과 프레임 비율 계산을 검증합니다."""

import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "verify-engine-live.py"
spec = importlib.util.spec_from_file_location("verify_engine_live", SCRIPT)
vel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vel)

LOG = """[engine] output device open 'Mac mini 스피커' @ 48000 Hz, 2 ch; STREAMING
[engine] bed renderer: built-in (--bed-renderer builtin)
[engine] shutting down
[engine] output device open 'iFi USB Audio SE' @ 384000 Hz, 2 ch; STREAMING
[engine] bed renderer: AUSpatialMixer at 384000 Hz, units at 96000 Hz (+308 frames), personalized HRTF not in use
"""


class LogTests(unittest.TestCase):
    def test_the_latest_open_and_renderer_lines_win(self):
        opened = vel.last_match(vel.OPEN_LINE, LOG)
        self.assertEqual((opened["name"], opened["rate"]), ("iFi USB Audio SE", "384000"))
        self.assertTrue(vel.last_match(vel.BED_LINE, LOG)["text"].startswith("AUSpatialMixer at 384000 Hz"))
        self.assertIsNone(vel.last_match(vel.OPEN_LINE, "nothing here"))

    def test_frame_ratio_names_extra_consumers(self):
        self.assertAlmostEqual(vel.frame_ratio(0, 1_920_000, 5.0, 384000), 1.0)
        self.assertAlmostEqual(vel.frame_ratio(0, 7_680_000, 5.0, 384000), 4.0, msg="four consumers drain four times")


if __name__ == "__main__":
    unittest.main()
