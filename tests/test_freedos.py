import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest


class FreeDosBootTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("BOOTHUSH_FREEDOS_FLOPPY"), "fixture not supplied")
    def test_official_1200_kib_floppy_reaches_installer(self):
        image = Path(os.environ["BOOTHUSH_FREEDOS_FLOPPY"]).resolve(strict=True)
        self.assertEqual(
            hashlib.sha256(image.read_bytes()).hexdigest(),
            "03df6088be016e57a6c44275f5bb9ab0244db71de1360957fd76ba83243b6a77",
        )
        exe = Path(os.environ["BOOTHUSH_EXE"]).resolve(strict=True)
        process = subprocess.run(
            [exe, "--machine", "pc286", "--floppy", image, "--cycles", "220000000",
             "--timeout-ms", "10000", "--scancode", "65000000:0x5a",
             "--inspect", "0xb8000:4000"],
            capture_output=True,
            encoding="utf-8",
            timeout=15,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(process.stderr, "")
        result = json.loads(process.stdout)
        self.assertEqual(result["status"], "cycle_limit")
        self.assertNotIn("exception", result)
        screen = bytes.fromhex(result["memory"]["bytes"])[::2]
        self.assertIn(b"FreeDOS 1.4 installation program", screen, repr(screen.strip()))
        self.assertIn(b"Do you want to proceed", screen, repr(screen.strip()))


if __name__ == "__main__":
    unittest.main()
