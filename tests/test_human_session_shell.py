"""Check the local PowerShell entry without opening an emulator or host window."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


WRAPPER = Path(__file__).resolve().parents[1] / 'scripts/Invoke-LocalHumanSession.cmd'
PWSH = os.environ.get('BOOTHUSH_TEST_PWSH')


@unittest.skipUnless(os.name == 'nt' and PWSH, 'Windows PowerShell path not supplied')
class HumanSessionShellTests(unittest.TestCase):
    def run_wrapper(self, arguments):
        # /s removes the outer quote pair. Inner quotes protect each path from
        # cmd metacharacters; no CRT backslash quoting is applied to /c text.
        comspec = os.environ.get('COMSPEC', r'C:\Windows\System32\cmd.exe')
        tail = ' '.join('"' + str(arg) + '"' for arg in (WRAPPER, *arguments))
        command = f'"{comspec}" /d /s /c "{tail}"'
        environment = os.environ.copy()
        environment.pop('POWERSHELL_TELEMETRY_OPTOUT', None)
        environment.pop('POWERSHELL_UPDATECHECK', None)
        return subprocess.run(command, capture_output=True, text=True, timeout=15,
                              env=environment, creationflags=subprocess.CREATE_NO_WINDOW)

    def test_argument_errors_do_not_start_powershell(self):
        self.assertEqual(self.run_wrapper([]).returncode, 2)
        self.assertEqual(self.run_wrapper([PWSH]).returncode, 2)
        self.assertEqual(self.run_wrapper([PWSH, 'missing.ps1', 'extra']).returncode, 2)

    def test_startup_environment_quoted_path_and_exit_code(self):
        with tempfile.TemporaryDirectory(prefix='bh shell & ! ') as directory:
            probe = Path(directory) / 'probe with spaces.ps1'
            probe.write_text(
                "@{ telemetry=$env:POWERSHELL_TELEMETRY_OPTOUT; "
                "updates=$env:POWERSHELL_UPDATECHECK; path=$PSCommandPath } | ConvertTo-Json -Compress\n"
                "exit 23\n", encoding='utf-8')
            result = self.run_wrapper([PWSH, probe])
            self.assertEqual(result.returncode, 23, result.stderr)
            observed = json.loads(result.stdout)
            self.assertEqual(observed['telemetry'], '1')
            self.assertEqual(observed['updates'], 'Off')
            self.assertEqual(Path(observed['path']), probe)
