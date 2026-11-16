import ctypes
from ctypes import wintypes
import uuid

from test_boot import BootHushTestCase, make_rom


class RunControlTests(BootHushTestCase):
    def test_stop_at_initial_pc(self):
        result = self.boot("--until-pc", "0x400104")
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["pc"], 0x400104)

    def test_wall_timeout(self):
        result = self.boot(cycles=1_000_000_000, timeout_ms=1, exit_code=1)
        self.assertEqual(result["status"], "timeout")
        self.assertLess(result["cycles"], 1_000_000_000)

    def test_pc_stop_after_cycle(self):
        # Original loop: D0 counts visits; no input or external ROM required.
        self.rom.write_bytes(make_rom(bytes.fromhex('7000 5280 60fc')))
        pc = '0x400106'
        args = ('--until-pc', pc, '--mac-cpu-state', '1')
        initial = self.boot(*args)
        self.assertEqual(initial, self.boot(*args, '--until-pc-after', '0'))
        later = self.boot(*args, '--until-pc-after', '200')
        self.assertEqual(later['status'], 'condition_met')
        self.assertGreaterEqual(later['cycles'], 200)
        self.assertGreater(later['cpu']['d'][0], initial['cpu']['d'][0])
        self.assertEqual(later, self.boot(*args, '--until-pc-after', str(later['cycles'])))
        # A cycle-budget stop at the same instruction has the identical state.
        control = self.boot('--mac-cpu-state', '1', cycles=later['cycles'])
        self.assertEqual({k:v for k,v in later.items() if k != 'status'},
                         {k:v for k,v in control.items() if k != 'status'})
        missed = self.boot('--until-pc', '0x400104', '--until-pc-after', '200', exit_code=1)
        self.assertEqual(missed['status'], 'cycle_limit')

    def test_pc_after_refusals(self):
        for args in (('--until-pc-after', '1'),
                     ('--until-pc', '0x400104', '--until-pc-after', '2001'),
                     ('--until-pc', '0x400104', '--until-pc-after', '-1'),
                     ('--until-pc', '0x400104', '--until-pc-after', '4294967296'),
                     ('--until-pc', '0x400104', '--until-pc-after', 'x'),
                     ('--until-pc', '0x400104', '--until-pc-after', '0', '--until-pc-after', '0')):
            with self.subTest(args=args):
                self.assertEqual(self.boot(*args, exit_code=2)['status'], 'error')

    def test_cancellation_event(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateEventW.argtypes = [
            ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR,
        ]
        kernel.CreateEventW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        name = "Local\\BootHushTest-" + uuid.uuid4().hex
        event = kernel.CreateEventW(None, True, True, name)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        self.addCleanup(kernel.CloseHandle, event)

        result = self.boot("--cancel-event", name, exit_code=1)
        self.assertEqual(result["status"], "cancelled")
        extended = self.boot("--cancel-event", name, cycles=4_000_000_000,
                             timeout_ms=600_000, exit_code=1)
        self.assertEqual(extended["status"], "cancelled")

    def test_frame_never_overwrites_an_existing_file(self):
        frame = self.scratch / "existing.pbm"
        frame.write_bytes(b"keep")
        result = self.boot("--frame", frame, exit_code=2)
        self.assertEqual(result["status"], "error")
        self.assertEqual(frame.read_bytes(), b"keep")

    def test_mac_rejects_malformed_floppy(self):
        floppy = self.scratch / "bad.dsk"
        floppy.write_bytes(bytes(409601))
        result = self.boot("--floppy", floppy, exit_code=2)
        self.assertEqual(result["status"], "error")
        self.assertIn("Macintosh floppy", result["error"])

    def test_input_event_order_applies_across_device_types(self):
        result = self.boot("--key", "100:0:1", "--mouse-button", "99:1", exit_code=2)
        self.assertEqual(result["status"], "error")
        self.assertIn("order", result["error"])

    def test_mouse_button_press_and_release_reach_via(self):
        self.rom.write_bytes(make_rom(bytes.fromhex(
            "13fc 0000 00ef fffe "  # Clear VIA port A latch.
            "13fc 0010 00ef e7fe "  # Turn off ROM overlay.
            "1039 00ef e1fe 0800 0003 66f4 "  # Poll PB3 until pressed.
            "13fc 0001 0000 0b00 "
            "1039 00ef e1fe 0800 0003 67f4 "  # Poll PB3 until released.
            "13fc 0002 0000 0b00 60fe"
        )))
        result = self.boot(
            "--mouse-button", "500:1", "--mouse-button", "900:0",
            "--until", "0xb00:1:2", "--inspect", "0xb00:1",
        )
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["bytes"], "02")
        self.assertGreaterEqual(result["cycles"], 900)
        idle = self.boot("--inspect", "0xb00:1")
        self.assertEqual(idle["memory"]["bytes"], "00")
