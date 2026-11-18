"""CLI run-budget boundaries; opt-in original workloads cross the 32-bit clock."""
import hashlib
import json
import os
import subprocess
import time
import unittest

from test_boot import BootHushTestCase, make_rom, SMOKE_PROGRAM, ROM_ENTRY
import test_pc286


MAX_RUN_CYCLES = 20_000_000_000


class LongCycleLimits(BootHushTestCase):
    def test_run_budget_boundaries(self):
        # Stop at reset: acceptance is tested without spending the whole budget.
        for cycles in (1, 4_000_000_000, 4_000_000_001, 0xffffffff, 0x100000000,
                       MAX_RUN_CYCLES, hex(MAX_RUN_CYCLES)):
            with self.subTest(cycles=cycles):
                result = self.boot('--until-pc', hex(ROM_ENTRY), cycles=cycles)
                self.assertEqual((result['status'], result['cycles']), ('condition_met', 0))

    def test_invalid_run_budgets(self):
        for cycles in ('', '0', '-1', '+1', '1.5', '0x', '0xg', '1:2',
                       MAX_RUN_CYCLES+1, 0xffffffffffffffff, 0x10000000000000000):
            with self.subTest(cycles=cycles):
                result = self.boot(cycles=cycles, exit_code=2)
                self.assertEqual(result['status'], 'error')
                expected = ('option values must be nonempty' if cycles == '' else
                            'cycles must be between 1 and 20000000000')
                self.assertIn(expected, result['error'])
        self.assertEqual(self.boot('--cycles', '2', cycles=1, exit_code=2)['status'], 'error')

    def test_timeout_still_bounds_large_budget(self):
        result = self.boot(cycles=MAX_RUN_CYCLES, timeout_ms=1, exit_code=1)
        self.assertEqual(result['status'], 'timeout')
        self.assertLess(result['cycles'], MAX_RUN_CYCLES)
        for timeout in (0, 3_600_001):
            self.assertEqual(self.boot(cycles=MAX_RUN_CYCLES, timeout_ms=timeout,
                                       exit_code=2)['status'], 'error')

    def test_other_profiles_do_not_admit_native_budget(self):
        for machine in ('nes', 'x64'):
            process = subprocess.run([self.exe, '--machine', machine, '--rom', self.rom,
                                      '--cycles', str(MAX_RUN_CYCLES), '--timeout-ms', '2000'],
                                     capture_output=True, text=True, timeout=5,
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(process.returncode, 2, process.stdout+process.stderr)
            self.assertEqual(json.loads(process.stdout)['status'], 'error')

    def test_default_mouse_witness_end_is_not_truncated(self):
        for version in (1, 2, 3):
            path = self.scratch/('witness-'+str(version)+'.txt')
            options = ['--mouse-witness', path]
            if version >= 2:
                options += ['--mouse-witness-detail', '1']
            if version == 3:
                options += ['--mouse-witness-memory', '0xb00:4']
            self.boot('--until', '0xb00:4:0x1234abcd', *options, cycles=MAX_RUN_CYCLES)
            fields = path.read_text().splitlines()[0].split()
            self.assertEqual(fields[:3], ['BHMACMOUSE'+str(version), '0', str(MAX_RUN_CYCLES)])

    def test_unrelated_cycle_fields_keep_existing_bounds(self):
        for options in (('--video-start', '4000000001'), ('--capture-cycle', '4000000001'),
                        ('--until-pc', hex(ROM_ENTRY), '--until-pc-after', '4294967296'),
                        ('--key', '4294967296:0:1'),
                        ('--mouse-witness-range', '0:4000000001')):
            with self.subTest(options=options):
                self.assertEqual(self.boot(*options, cycles=MAX_RUN_CYCLES, exit_code=2)['status'], 'error')

    @unittest.skipUnless(os.environ.get('BOOTHUSH_LONG_CYCLE_TESTS') == '1',
                         'explicit long-run qualification only')
    def test_actual_mac_twenty_billion_cycles(self):
        # Original DIVU loop has a high modeled cycle cost but no disk or Apple code.
        # D0 remains quotient 0x1234 with zero remainder on every iteration.
        rom = self.scratch/'long-divide.rom'
        rom.write_bytes(make_rom(SMOKE_PROGRAM[:-2]+bytes.fromhex('203c00001234 80fc0001 60fa')))
        started = time.perf_counter()
        result = self.boot('--inspect', '0xb00:4', '--mac-cpu-state', '1', rom=rom,
                           cycles=MAX_RUN_CYCLES, timeout_ms=60000)
        elapsed = time.perf_counter()-started
        self.assertEqual(result['status'], 'cycle_limit')
        self.assertGreaterEqual(result['cycles'], MAX_RUN_CYCLES)
        self.assertLess(result['cycles']-MAX_RUN_CYCLES, 200)
        self.assertEqual(result['memory']['bytes'], '1234abcd')
        self.assertEqual(result['vbl'], result['cycles']//130240)
        self.assertEqual(result['cpu']['d'][0], 0x1234)
        print(json.dumps({'long_cycle_case': 'mac128k', 'rom_sha256': hashlib.sha256(rom.read_bytes()).hexdigest(),
                          'emulator_sha256': hashlib.sha256(self.exe.read_bytes()).hexdigest(),
                          'wall_seconds': elapsed, 'result': result}))


class PcLongCycleLimits(BootHushTestCase):
    boot = test_pc286.Pc286Tests.boot

    def test_pc_large_budget_accepted(self):
        result = self.boot(test_pc286.make_rom(bytes.fromhex('f4')), cycles=MAX_RUN_CYCLES)
        self.assertEqual(result['status'], 'halted')
        self.assertLess(result['cycles'], 100)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_LONG_CYCLE_TESTS') == '1',
                         'explicit long-run qualification only')
    def test_actual_pc_beyond_uint32(self):
        # Original DIV BX / branch loop; DX remains zero and AX remains 0x1234.
        rom = self.scratch/'long-pc-divide.rom'
        rom.write_bytes(test_pc286.make_rom(bytes.fromhex('b83412 31d2 bb0100 f7f3 ebfc')))
        target = 4_294_968_296
        started = time.perf_counter()
        process = subprocess.run([self.exe, '--machine', 'pc286', '--rom', rom,
                                  '--cycles', str(target), '--timeout-ms', '60000'],
                                 capture_output=True, text=True, timeout=65,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        elapsed = time.perf_counter()-started
        self.assertEqual(process.returncode, 0, process.stdout+process.stderr)
        result = json.loads(process.stdout)
        self.assertEqual(result['status'], 'cycle_limit')
        self.assertGreaterEqual(result['cycles'], target)
        self.assertLess(result['cycles']-target, 200)
        print(json.dumps({'long_cycle_case': 'pc286', 'rom_sha256': hashlib.sha256(rom.read_bytes()).hexdigest(),
                          'emulator_sha256': hashlib.sha256(self.exe.read_bytes()).hexdigest(),
                          'wall_seconds': elapsed, 'result': result}))
