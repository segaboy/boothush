"""Original instruction fixtures for stopped register observations."""
import struct
import subprocess
from test_boot import BootHushTestCase, make_rom, ROM_ENTRY


class MacCpuStateTests(BootHushTestCase):
    def test_all_registers_and_noninterference(self):
        data = [0, 1, 0x80000000, 0xFFFFFFFF, 0x12345678, 0xA5A55A5A, 0x76543210, 0xFEDCBA98]
        addr = [0x2000 + 0x102 * i for i in range(7)] + [0x6000]
        code = bytes.fromhex('13fc 0000 00ef fffe 13fc 0010 00ef e7fe')
        for i, value in enumerate(data):
            code += struct.pack('>HI', 0x203C + (i << 9), value)
        for i, value in enumerate(addr):
            code += struct.pack('>HI', 0x207C + (i << 9), value)
        code += bytes.fromhex('46fc 2715')
        stop = ROM_ENTRY + len(code)
        self.rom.write_bytes(make_rom(code + bytes.fromhex('60fe')))
        plain_frame, observed_frame = self.scratch/'plain.pbm', self.scratch/'observed.pbm'
        args = ('--until-pc', str(stop), '--inspect', '0xb00:16')
        plain = self.boot(*args, '--frame', plain_frame)
        observed = self.boot(*args, '--frame', observed_frame, '--mac-cpu-state', '1')
        self.assertEqual(observed.pop('cpu'), {'pc': stop, 'sr': 0x2715, 'd': data, 'a': addr})
        self.assertEqual(observed, plain)
        self.assertEqual(plain_frame.read_bytes(), observed_frame.read_bytes())
        self.assertEqual(plain['status'], 'condition_met')

    def test_sr_bits_at_cycle_boundary(self):
        # Explicit supervisor IPL and CCR combinations; no trace-bit exception.
        for sr in (0x2000, 0x201F, 0x230A, 0x2700, 0x2715):
            with self.subTest(sr=sr):
                self.rom.write_bytes(make_rom(struct.pack('>HHH', 0x46FC, sr, 0x60FE)))
                result = self.boot('--mac-cpu-state', '1', cycles=100)
                self.assertEqual(result['cpu']['sr'], sr)
                self.assertEqual(result['cpu']['pc'], result['pc'])
                self.assertEqual(result['cpu']['a'][7], 0x4000)

    def test_invalid_options(self):
        for options in (('--mac-cpu-state', '0'), ('--mac-cpu-state', '2'),
                        ('--mac-cpu-state', '1', '--mac-cpu-state', '1')):
            self.assertEqual(self.boot(*options, exit_code=2)['status'], 'error')
        result = subprocess.run([self.exe, '--machine', 'pc286', '--cycles', '100',
                                 '--timeout-ms', '100', '--mac-cpu-state', '1'],
                                capture_output=True, text=True, timeout=5,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 2)
        self.assertIn('mac-cpu-state requires mac128k', result.stdout)


if __name__ == '__main__':
    import unittest
    unittest.main()
