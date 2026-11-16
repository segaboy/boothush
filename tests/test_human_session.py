import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from test_boot import BootHushTestCase
from test_pc286 import make_rom

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/run_human_session.py'


class HumanPlanTests(BootHushTestCase):
    def test_preparation_does_not_run_and_refuses_overwrite(self):
        out = self.scratch / "plan with 'quote"
        cmd = [sys.executable, '-B', str(SCRIPT), '--emulator', str(self.exe), '--machine', 'mac128k',
               '--rom', str(self.rom), '--output', str(out), '--prepare-only']
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads((out / 'plan.json').read_text())
        self.assertEqual(plan['status'], 'prepared'); self.assertFalse(plan['human_verified'])
        self.assertFalse((out / 'session').exists())
        launch = (out / 'launch.ps1').read_text()
        self.assertIn("plan with ''quote", launch); self.assertIn('Get-FileHash -LiteralPath', launch)
        original = (out / 'plan.json').read_bytes()
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0); self.assertEqual((out / 'plan.json').read_bytes(), original)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_LIVE_KEY_TEST_EXE'), 'hidden fixture not supplied')
    def test_hidden_fixture_collects_session_without_human_attestation(self):
        self.rom.write_bytes(make_rom(bytes.fromhex('ebfe')))
        out = self.scratch / 'session'
        cmd = [sys.executable, '-B', str(SCRIPT), '--emulator', os.environ['BOOTHUSH_LIVE_KEY_TEST_EXE'],
               '--machine', 'pc286', '--rom', str(self.rom), '--output', str(out), '--seconds', '2', '--record-seconds', '1']
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        report = json.loads((out / 'session.json').read_text())
        self.assertEqual(report['status'], 'completed'); self.assertTrue(report['inputs_unchanged'])
        self.assertFalse(report['human_verified']); self.assertEqual(report['guest']['status'], 'closed')
        self.assertEqual(report['guest']['live_session']['keys_accepted'], 4)
        self.assertGreater(report['guest']['video']['frames'], 0)
        for name, item in report['artifacts'].items():
            self.assertEqual(hashlib.sha256((out / name).read_bytes()).hexdigest(), item['sha256'])
        for name in ('keyboard.bhlive', 'final.ppm', 'session.y4m', 'observations.md'):
            self.assertIn(name, report['artifacts'])
