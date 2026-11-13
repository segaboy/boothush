"""Original generated cartridges for the bounded experimental NES CPU target."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


def cartridge(code, banks=1, start=0x8000, chr_banks=0):
    header = bytearray(b'NES\x1a' + bytes(12))
    header[4], header[5] = banks, chr_banks
    prg = bytearray([0xff] * (banks * 16384))
    offset = (start - 0x8000) % len(prg)
    prg[offset:offset + len(code)] = code
    prg[-4:-2] = start.to_bytes(2, 'little')
    return header + prg + bytes(chr_banks * 8192)


class NesTarget(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP'))
        self.addCleanup(tmp.cleanup)
        self.file = Path(tmp.name) / 'original.nes'

    def run_nes(self, image, *args):
        self.file.write_bytes(image)
        before = hashlib.sha256(image).hexdigest()
        proc = subprocess.run([os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(self.file),
                               '--steps', '100', '--timeout-ms', '3000', *args],
                              capture_output=True, text=True, timeout=10,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(proc.stderr, '')
        self.assertEqual(hashlib.sha256(self.file.read_bytes()).hexdigest(), before)
        return proc.returncode, json.loads(proc.stdout)

    def test_original_loop_and_ram_mirroring(self):
        # Sum five ones, store each result, and stop just after the loop.
        image = cartridge(bytes.fromhex('a200 a900 18 6901 8510 e8 e005 d0f6 ea'))
        for address in ('0x10', '0x810', '0x1010', '0x1810'):
            code, result = self.run_nes(image, '--until-pc', '0x800e', '--inspect', address + ':1')
            self.assertEqual((code, result['status'], result['a'], result['x'], result['memory']['bytes']),
                             (0, 'condition_met', 5, 5, '05'))
            self.assertEqual(result['rom_sha256'], hashlib.sha256(image).hexdigest())

    def test_nrom_128_mirror_and_256_reset_vectors(self):
        for banks, start in ((1, 0xc000), (2, 0xc000)):
            image = cartridge(bytes.fromhex('a92a 8500 ea'), banks, start)
            code, result = self.run_nes(image, '--until-pc', '0xc004', '--inspect', '0:1')
            self.assertEqual((code, result['status'], result['a'], result['memory']['bytes']),
                             (0, 'condition_met', 42, '2a'))
        # NROM-128 maps the same PRG bank at both addresses.
        image = cartridge(bytes.fromhex('a92a'), 1)
        code, result = self.run_nes(image, '--until-pc', '0x8000', '--inspect', '0xc000:2')
        self.assertEqual((code, result['memory']['bytes']), (0, 'a92a'))

    def test_binary_arithmetic_decimal_flag_and_overflow(self):
        image = cartridge(bytes.fromhex('f8 18 a909 6901 8501 18 a97f 6901 8502 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800f', '--inspect', '1:2')
        self.assertEqual((code, result['memory']['bytes']), (0, '0a80'))
        self.assertEqual(result['flags'] & 0xc9, 0xc8)  # N,V,D set; C clear
        image = cartridge(bytes.fromhex('38 a900 e901 8503 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8007', '--inspect', '3:1')
        self.assertEqual((code, result['memory']['bytes'], result['flags'] & 0x81), (0, 'ff', 0x80))

    def test_transfers_and_register_wrap(self):
        image = cartridge(bytes.fromhex('a980 aa a8 8a 98 8610 8411 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800a', '--inspect', '0x10:2')
        self.assertEqual((code, result['a'], result['x'], result['y'], result['memory']['bytes']),
                         (0, 128, 128, 128, '8080'))
        image = cartridge(bytes.fromhex('a200 ca e8 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8003')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 255, 0x80))
        code, result = self.run_nes(image, '--until-pc', '0x8004')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 0, 0x02))

    def test_faults_limits_and_invalid_images(self):
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 8d0020')), '--inspect', '0:1')
        self.assertEqual((code, r['status'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 'machine_fault', 0x8002, 'unsupported_bus_write', 0x2000))
        self.assertEqual(r['memory']['bytes'], '00')
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 ad0020')), '--inspect', '0:1')
        self.assertEqual((code, r['a'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 0x55, 0x8002, 'unsupported_bus_read', 0x2000))
        code, r = self.run_nes(cartridge(b'\x02'))
        self.assertEqual((code, r['fault']['operation'], r['fault']['opcode']), (1, 'unsupported_opcode', 2))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')), '--until-pc', '0x8003')
        self.assertEqual((code, r['status'], r['steps']), (1, 'condition_not_met', 100))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')))
        self.assertEqual((code, r['status'], r['steps']), (0, 'step_limit', 100))
        for mutator in ('magic', 'mapper', 'trainer', 'nes2', 'short'):
            image = cartridge(b'\xea')
            if mutator == 'magic': image[0] = 0
            if mutator == 'mapper': image[6] = 0x10
            if mutator == 'trainer': image[6] = 0x04
            if mutator == 'nes2': image[7] = 0x08
            if mutator == 'short': image.pop()
            code, result = self.run_nes(image)
            self.assertEqual((code, result['status']), (2, 'error'))
        code, result = self.run_nes(cartridge(b'\xea'), '--frame', str(self.file))
        self.assertEqual((code, result['status']), (2, 'error'))


if __name__ == '__main__':
    unittest.main()
