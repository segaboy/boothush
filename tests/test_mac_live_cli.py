import json
import os
from pathlib import Path
import subprocess
import unittest

from test_boot import BootHushTestCase
from test_mouse_cli import RESULT, make_mouse_probe_rom


@unittest.skipUnless(os.environ.get("BOOTHUSH_LIVE_CLI_TEST_EXE"), "live CLI test binary not supplied")
class MacLiveCliTests(BootHushTestCase):
    def test_owned_window_records_and_replays_exact_guest_state(self):
        live_cli = Path(os.environ["BOOTHUSH_LIVE_CLI_TEST_EXE"]).resolve(strict=True)
        self.rom.write_bytes(make_mouse_probe_rom())
        trace = self.scratch / "live.bhmouse"
        live_frame = self.scratch / "live.pbm"
        result = subprocess.run(
            [
                live_cli, "--machine", "mac128k", "--rom", self.rom,
                "--live", "1", "--mouse-dcd-invert", "1", "--mouse-vbl-guard", "1024",
                "--mouse-record", trace, "--cycles", "80000000", "--timeout-ms", "5000",
                "--inspect", "0xb00:8", "--frame", live_frame,
            ],
            capture_output=True,
            encoding="utf-8",
            timeout=10,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "closed")
        self.assertEqual(report["mouse"], {"samples": 4, "steps": 4, "pending": 4})
        self.assertEqual(report["memory"], {"address": RESULT, "bytes": "2800080808080008"})
        self.assertEqual(self.assert_frame(live_frame)[0], 0x80)

        profile, *events = trace.read_text(encoding="ascii").splitlines()
        self.assertEqual(profile.split(), ["BHMOUSE1", "8192", "1", str(report["cycles"]), "1024"])
        recorded = [list(map(int, row.split())) for row in events]
        self.assertEqual([row[1:] for row in recorded], [[1, -1, 0], [0, 0, 2], [0, 0, 1], [2, 0, 0]])
        self.assertEqual(len({row[0] for row in recorded[:-1]}), 1)
        self.assertEqual(recorded[-1][0], report["cycles"])

        for attempt in range(2):
            replay_frame = self.scratch / f"replay-{attempt}.pbm"
            replay = self.boot(
                "--mouse-replay", trace, "--inspect", "0xb00:8", "--frame", replay_frame,
                cycles=80_000_000, timeout_ms=5000,
            )
            self.assertEqual(replay, dict(report, status="cycle_limit"))
            self.assertEqual(replay_frame.read_bytes(), live_frame.read_bytes())


if __name__ == "__main__":
    unittest.main()
