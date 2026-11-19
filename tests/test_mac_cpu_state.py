"""Original instruction fixtures for stopped register observations."""
import struct
import subprocess
from test_boot import BootHushTestCase, make_rom, ROM_ENTRY


class MacCpuStateTests(BootHushTestCase):
    def test_stop_after_low_ram_steps_counts_only_starts_inside_range(self):
        # Move a loop into low RAM after removing the ROM overlay. The backward
        # branch at $1804 is outside our four-byte range and re-enters at $1802.
        code = bytes.fromhex('13fc000000effffe 13fc003000efe7fe '
                             '23fc7005528000001800 33fc60fc00001804 4ef900001800')
        self.rom.write_bytes(make_rom(code))
        at_entry = self.boot('--until-pc', '0x1800', '--mac-cpu-state', '1')
        self.assertEqual(at_entry['pc'], 0x1800)
        for count, expected_pc, expected_d0, last in ((1, 0x1802, 5, 0x1800),
                                                       (2, 0x1804, 6, 0x1802),
                                                       (3, 0x1804, 7, 0x1802)):
            with self.subTest(count=count):
                result = self.boot('--until-mac-ram-steps', f'0x1800:4:{count}', '--mac-cpu-state', '1')
                self.assertEqual(result['status'], 'condition_met')
                self.assertEqual((result['pc'], result['cpu']['d'][0]), (expected_pc, expected_d0))
                self.assertGreater(result['cycles'], at_entry['cycles'])
                self.assertEqual(result.pop('mac_ram_steps'), {
                    'address': 0x1800, 'length': 4, 'target': count, 'started': count,
                    'returned': count, 'first_pc': 0x1800, 'last_pc': last})
                control = self.boot('--until-pc', str(expected_pc), '--until-pc-after',
                                    str(result['cycles']), '--mac-cpu-state', '1')
                self.assertEqual(result, control)

    def test_ram_steps_exclude_overlay_and_report_faulting_step(self):
        overlay = make_rom(bytes.fromhex('60fe'))
        struct.pack_into('>I', overlay, 4, 0x104)  # Execute a ROM overlay alias.
        self.rom.write_bytes(overlay)
        result = self.boot('--until-mac-ram-steps', '0x104:2:1', exit_code=1)
        self.assertEqual((result['status'], result['pc']), ('cycle_limit', 0x104))
        self.assertEqual(result['mac_ram_steps']['started'], 0)
        code = bytes.fromhex('13fc000000effffe 13fc003000efe7fe '
                             '23fc13fc000100001800 23fc0080000000001804 4ef900001800')
        self.rom.write_bytes(make_rom(code))
        result = self.boot('--until-mac-ram-steps', '0x1800:2:1', exit_code=1)
        self.assertEqual(result['status'], 'machine_fault')
        steps = result['mac_ram_steps']
        self.assertEqual((steps['started'], steps['returned'], steps['last_pc']), (1, 0, 0x1800))

    def test_ram_step_predicate_rejects_invalid_ranges_or_combined_stops(self):
        for value in ('0:2:0', '0:0:1', '0x20000:1:1', '0x1ffff:2:1', '1:0xffffffff:2'):
            with self.subTest(value=value):
                self.assertEqual(self.boot('--until-mac-ram-steps', value, exit_code=2)['cycles'], 0)
        self.assertEqual(self.boot('--until-mac-ram-steps', '0:2:1', '--until-pc', '0', exit_code=2)['cycles'], 0)

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
