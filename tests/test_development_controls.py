from test_boot import BootHushTestCase
import test_pc286


class DevelopmentControls(BootHushTestCase):
    def test_same_stop_ranges_and_overlap(self):
        result = self.boot("--inspect", "0xb00:4", "--inspect", "0xb02:2", "--inspect", "0xb00:4")
        self.assertEqual(result["memory_ranges"], [
            {"address": 0xb00, "bytes": "1234abcd"},
            {"address": 0xb02, "bytes": "abcd"},
            {"address": 0xb00, "bytes": "1234abcd"},
        ])
        single = self.boot("--inspect", "0xb00:4")
        self.assertEqual(single["memory"], result["memory_ranges"][0])
        self.assertNotIn("memory_ranges", single)

    def test_invalid_ranges(self):
        for args in (("--inspect", "0:0"), ("--inspect", "bad:1"),
                     ("--inspect", "0x1ffff:2"), ("--inspect", "0:4097"),
                     tuple(["--inspect", "0:1"] * 17), tuple(["--inspect", "0:4096"] * 5)):
            with self.subTest(args=args):
                self.assertEqual(self.boot(*args, exit_code=2)["status"], "error")

    def test_ready_offsets_and_record_replay(self):
        trace = self.scratch / "ready.bhmouse"
        result = self.boot("--input-after", "0xb00:4:0x1234abcd", "--mouse-dcd-invert", "1",
                           "--mouse-vbl-guard", "0", "--mouse-motion", "0:1:0",
                           "--mouse-button", "100000:1", "--mouse-button", "400000:0",
                           "--mouse-record", trace, "--inspect", "0xb00:4", cycles=800000)
        schedule = result.pop("input_schedule")
        self.assertTrue(schedule["ready"])
        self.assertGreater(schedule["ready_cycle"], 0)
        self.assertEqual((schedule["scheduled"], schedule["accepted"]), (3, 3))
        lines = trace.read_text().splitlines()
        self.assertEqual(int(lines[1].split()[0]), schedule["ready_cycle"])
        replay = self.boot("--mouse-replay", trace, "--inspect", "0xb00:4", cycles=800000)
        self.assertEqual(result, replay)

    def test_absolute_input_can_arrive_before_readiness(self):
        common = ("--mouse-edge-cycles", "1", "--mouse-vbl-guard", "0", "--mouse-motion", "0:1:0")
        early = self.boot("--until", "0xb00:4:0x1234abcd", *common)
        self.assertEqual(early["mouse"]["steps"], 2)
        delayed = self.boot("--input-after", "0xb00:4:0x1234abcd", *common)
        self.assertGreater(delayed["input_schedule"]["ready_cycle"], 0)
        self.assertEqual(delayed["mouse"]["steps"], 2)

    def test_cancel_before_readiness(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateEventW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
        kernel.CreateEventW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        name = "Local\\BootHushReady-" + uuid.uuid4().hex
        handle = kernel.CreateEventW(None, True, True, name)
        self.assertTrue(handle)
        self.addCleanup(kernel.CloseHandle, handle)
        result = self.boot("--cancel-event", name, "--input-after", "0xb00:4:0x1234abcd",
                           "--mouse-motion", "0:1:0", exit_code=1)
        self.assertEqual(result["status"], "cancelled")
        self.assertFalse(result["input_schedule"]["ready"])

    def test_window_ready_and_unreached(self):
        trace = self.scratch / "window.bhmouse"
        result = self.boot("--input-after", "0xb00:4:0x1234abcd", "--window-mouse", "0:100:100:0",
                           "--window-mouse", "1:101:100:0", "--mouse-record", trace, cycles=50000)
        self.assertTrue(result["input_schedule"]["ready"])
        self.assertEqual(result["input_schedule"]["accepted"], 2)
        replay = self.boot("--mouse-replay", trace, cycles=50000)
        self.assertEqual(result["mouse"], replay["mouse"])
        self.assertEqual(result["cycles"], replay["cycles"])
        missed = self.boot("--input-after", "0xb00:4:0xdeadbeef", "--mouse-motion", "0:1:0",
                           cycles=2000, exit_code=1)
        self.assertEqual(missed["status"], "readiness_not_reached")
        self.assertFalse(missed["input_schedule"]["ready"])
        self.assertEqual(missed["mouse"]["steps"], 0)

    def test_timeout_before_readiness(self):
        result = self.boot("--input-after", "0xb00:4:0xdeadbeef", "--mouse-motion", "0:1:0",
                           cycles=4000000000, timeout_ms=1, exit_code=1)
        self.assertEqual(result["status"], "timeout")
        self.assertFalse(result["input_schedule"]["ready"])


class PcInspection(test_pc286.Pc286Tests):
    def test_multi_range_pc(self):
        result = self.boot(test_pc286.make_rom(bytes.fromhex("31c0 8ed8 c6060002aa c6060003bb f4")),
                           "--inspect", "0x200:1", "--inspect", "0x300:1")
        self.assertEqual(result["memory_ranges"], [{"address": 0x200, "bytes": "aa"},
                                                    {"address": 0x300, "bytes": "bb"}])
        self.assertEqual(self.boot(test_pc286.make_rom(bytes.fromhex("f4")),
                                   "--inspect", "0xbffff:2", exit_code=2)["status"], "error")

    def test_witness_filters_and_guest_state(self):
        image = test_pc286.make_rom(bytes.fromhex("31c0 8ed8 c6060002aa a00002 b05a e680 f4"))
        plain = self.boot(image, "--inspect", "0x200:1")
        path = self.scratch / "pc.trace"
        traced = self.boot(image, "--inspect", "0x200:1", "--pc-witness", path,
                           "--pc-witness-memory", "0x200:1", "--pc-witness-io", "0x80:1")
        self.assertEqual(plain, traced)
        header, columns, *rows = path.read_text().splitlines()
        self.assertEqual(columns, "cycle cs ip pc kind address value width")
        fields = header.split()
        self.assertEqual(fields[0], "BHPC286W1")
        self.assertEqual(int(fields[2]), len(rows))
        self.assertEqual(int(fields[3]), 0)
        events = [row.split() for row in rows]
        self.assertIn(["write", "512", "170", "1"], [row[4:] for row in events])
        self.assertIn(["read", "512", "170", "1"], [row[4:] for row in events])
        self.assertIn(["out", "128", "90", "1"], [row[4:] for row in events])
        self.assertEqual([int(row[0]) for row in events], sorted(int(row[0]) for row in events))

    def test_witness_capacity_and_cycle_filter(self):
        path = self.scratch / "long.trace"
        self.boot(test_pc286.make_rom(bytes.fromhex("ebfe")), "--pc-witness", path, cycles=2000000)
        magic, stop, count, dropped = path.read_text().splitlines()[0].split()
        self.assertEqual(magic, "BHPC286W1")
        self.assertEqual(int(count), 65536)
        self.assertGreater(int(dropped), 0)
        limited = self.scratch / "limited.trace"
        self.boot(test_pc286.make_rom(bytes.fromhex("ebfe")), "--pc-witness", limited,
                  "--pc-witness-cycles", "100:200", cycles=1000)
        rows = limited.read_text().splitlines()[2:]
        self.assertTrue(rows)
        self.assertTrue(all(100 <= int(row.split()[0]) <= 200 for row in rows))

    def test_supplied_text_font(self):
        image = bytearray(80 * 2 * 15 * 512)
        # Hide the cursor through the CRTC, then draw an X via the built-in BIOS.
        code = bytes.fromhex("b80300 cd10 bad403 b00a ee 42 b020 ee b8580e cd10 fa f4")
        image[:len(code)] = code
        image[510:512] = b"\x55\xaa"
        font = bytearray(256 * 14)
        font[ord("X") * 14] = 0x80
        path = self.scratch / "font.raw"
        path.write_bytes(font)
        frame = self.scratch / "text.ppm"
        self.boot(image, "--ega-font", path, "--frame", frame, floppy=True, cycles=100000)
        pixels = frame.read_bytes().split(b"\n", 3)[3]
        self.assertEqual(pixels[:6], bytes([170, 170, 170, 0, 0, 0]))
        bad_font = self.scratch / "bad-font.raw"
        bad_font.write_bytes(bytes(100))
        self.assertEqual(self.boot(image, "--ega-font", bad_font, floppy=True, exit_code=2)["status"], "error")
import ctypes
from ctypes import wintypes
import uuid
