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

    def test_frame_never_overwrites_an_existing_file(self):
        frame = self.scratch / "existing.pbm"
        frame.write_bytes(b"keep")
        result = self.boot("--frame", frame, exit_code=2)
        self.assertEqual(result["status"], "error")
        self.assertEqual(frame.read_bytes(), b"keep")

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
