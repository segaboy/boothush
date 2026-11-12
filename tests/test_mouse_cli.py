import struct
import subprocess
import unittest
import os
import hashlib
from pathlib import Path

from test_boot import BootHushTestCase, make_rom


VIA_PORT_B = 0xEFE1FE
SCC_A = 0x9FFFFA
SCC_B = 0x9FFFF8
RESULT = 0xB00
STOP_CYCLE = 470_000
VALID_TRACE = b"BHMOUSE1 8192 1 470000 1024\n130240 1 -1 0\n130240 0 0 2\n130240 0 0 1\n"


def wait_for_bit(address, bit, set_bit):
    # BTST #bit,address.L; loop on BEQ (wait for set) or BNE (wait for clear).
    return struct.pack(">HHIH", 0x0839, bit, address, 0x67F6 if set_bit else 0x66F6)


def sample(address, mask, offset):
    # MOVE.B address.L,D0; ANDI.B #mask,D0; MOVE.B D0,result.L.
    return struct.pack(">HIHHHI", 0x1039, address, 0x0200, mask, 0x13C0, RESULT + offset)


def make_mouse_probe_rom():
    program = (
        bytes.fromhex("13fc 0000 00ef fffe 13fc 0010 00ef e7fe")  # Clear overlay.
        + wait_for_bit(VIA_PORT_B, 4, False)
        + sample(VIA_PORT_B, 0x38, 0)
        + sample(SCC_A, 0x08, 1)
        + sample(SCC_B, 0x08, 2)
        + wait_for_bit(SCC_A, 3, True)
        + sample(VIA_PORT_B, 0x38, 3)
        + sample(SCC_A, 0x08, 4)
        + sample(SCC_B, 0x08, 5)
        + wait_for_bit(VIA_PORT_B, 3, False)
        + sample(VIA_PORT_B, 0x38, 6)
        + wait_for_bit(VIA_PORT_B, 3, True)
        + sample(VIA_PORT_B, 0x38, 7)
        + struct.pack(">HHI", 0x13FC, 0x80, 0x1A700)  # Mark completion in the frame.
        + bytes.fromhex("60fe")
    )
    return make_rom(program)


class MacMouseCliTests(BootHushTestCase):
    @unittest.skipUnless(os.environ.get("BOOTHUSH_CLASSICK_LIVE_ROM"), "ClasSICK live ROM not supplied")
    def test_classick_background_window_drags(self):
        rom = Path(os.environ["BOOTHUSH_CLASSICK_LIVE_ROM"])
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         "faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c")
        self.rom.write_bytes(rom.read_bytes())
        trace = self.scratch / "candidate.bhmouse"
        frame = self.scratch / "candidate.pbm"
        events = ["100000000:256:171:0", "108000000:100:80:0", "120000000:100:80:1",
                  "128000000:132:80:2", "136000000:132:80:3", "144000000:148:80:0",
                  "152000000:148:80:1", "160000000:148:56:2", "168000000:148:56:3",
                  "176000000:148:64:0", "184000000:150:110:0", "192000000:150:110:1",
                  "192000000:150:110:3"]
        options = [value for event in events for value in ("--window-mouse", event)]
        result = self.boot("--mouse-dcd-invert", "3", "--mouse-record", trace,
                           "--inspect", "0xb00:1024", "--frame", frame, *options,
                           cycles=205000000, timeout_ms=20000)
        self.assertEqual(result.pop("input_source"), "injected_window_messages")
        self.assertEqual(result["mouse"]["pending"], 0)
        data = bytes.fromhex(result["memory"]["bytes"])
        pos = data.index(b"MWP1")
        words = struct.unpack_from(">32I", data, pos)
        self.assertEqual((words[9], *words[13:18], words[22]), (0, 150, 110, 112, 46, 1, 0))
        self.assertEqual(struct.unpack_from(">I", data, data.index(b"MMD1") + 4)[0], 0)
        self.assertEqual(struct.unpack_from(">I", data, data.index(b"CS09") + 12)[0], 3)
        replay_frame = self.scratch / "replay.pbm"
        replay = self.boot("--mouse-replay", trace, "--inspect", "0xb00:1024",
                           "--frame", replay_frame, cycles=205000000, timeout_ms=20000)
        self.assertEqual(result, replay)
        self.assertEqual(frame.read_bytes(), replay_frame.read_bytes())

    def test_help_and_version(self):
        for option, expected in (("--help", "--window-mouse"), ("--version", "BootHush build")):
            result = subprocess.run([self.exe, option], capture_output=True, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.assertEqual(result.returncode, 0)
            self.assertIn(expected, result.stdout)

    def test_invisible_window_drag_records_and_replays(self):
        self.rom.write_bytes(make_mouse_probe_rom())
        trace = self.scratch / "window.bhmouse"
        result = self.boot("--mouse-dcd-invert", "1", "--mouse-vbl-guard", "1024",
                           "--mouse-record", trace, "--inspect", "0xb00:8",
                           "--window-mouse", "130240:100:100:0",
                           "--window-mouse", "130240:101:99:0",
                           "--window-mouse", "130240:101:99:1",
                           "--window-mouse", "140000:102:99:2",
                           "--window-mouse", "150000:102:99:3",
                           "--window-mouse", "160000:103:99:0", cycles=800000)
        self.assertEqual(result.pop("input_source"), "injected_window_messages")
        self.assertEqual(result["mouse"]["pending"], 0)
        replay = self.boot("--mouse-replay", trace, "--inspect", "0xb00:8", cycles=800000)
        self.assertEqual(result, replay)
        rows = [line.split()[-1] for line in trace.read_text().splitlines()[1:]]
        self.assertEqual(rows, ["0", "2", "0", "1", "0"])

    def test_empty_recording_replays(self):
        trace = self.scratch / "empty.bhmouse"
        trace.write_text("BHMOUSE1 8192 0 10000 4096\n", encoding="ascii")
        result = self.boot("--mouse-replay", trace, cycles=11000)
        self.assertEqual(result["mouse"], {"samples": 0, "steps": 0, "pending": 0})

    def test_replay_queues_final_boundary_sample_without_advancing_guest(self):
        self.rom.write_bytes(make_rom(bytes.fromhex("60fe")))
        baseline = self.boot(cycles=512)
        stop = baseline["cycles"]
        trace = self.scratch / "final-boundary.bhmouse"
        trace.write_text(f"BHMOUSE1 8192 0 {stop} 0\n{stop} 1 0 2\n", encoding="ascii")

        replay = self.boot("--mouse-replay", trace, cycles=stop + 100)
        self.assertEqual(replay["mouse"], {"samples": 1, "steps": 0, "pending": 3})
        self.assertEqual({key: value for key, value in replay.items() if key != "mouse"}, baseline)

    def test_relative_motion_and_replay_reach_guest_pins(self):
        self.rom.write_bytes(make_mouse_probe_rom())
        trace = self.scratch / "mouse.bhmouse"
        trace.write_bytes(VALID_TRACE)

        direct = self.boot(
            "--inspect", "0xb00:8",
            "--mouse-edge-cycles", "8192", "--mouse-dcd-invert", "1", "--mouse-vbl-guard", "1024",
            "--mouse-motion", "130240:1:-1",
            "--mouse-button", "130240:1", "--mouse-button", "130240:0",
            cycles=STOP_CYCLE,
        )
        replay = self.boot("--inspect", "0xb00:8", "--mouse-replay", trace, cycles=STOP_CYCLE + 20_000)

        self.assertEqual(direct["status"], "cycle_limit")
        self.assertEqual(direct["mouse"], {"samples": 3, "steps": 4, "pending": 0})
        self.assertEqual(direct["memory"], {"address": RESULT, "bytes": "2800080808080008"})
        self.assertEqual(replay, direct)

    def test_rejects_malformed_replay(self):
        trace = self.scratch / "invalid.bhmouse"
        cases = {
            "unknown version": b"BHMOUSE2 8192 1 470000 1024\n",
            "zero edge period": b"BHMOUSE1 0 1 470000 1024\n",
            "invalid polarity": b"BHMOUSE1 8192 4 470000 1024\n",
            "invalid guard": b"BHMOUSE1 8192 1 470000 130240\n",
            "invalid button": b"BHMOUSE1 8192 1 470000 1024\n100 0 0 3\n",
            "reversed order": b"BHMOUSE1 8192 1 470000 1024\n100 1 0 0\n99 0 0 2\n",
            "past stop cycle": b"BHMOUSE1 8192 1 470000 1024\n470001 0 0 2\n",
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                trace.write_bytes(data)
                result = self.boot("--mouse-replay", trace, exit_code=2)
                self.assertEqual(result["status"], "error")
                self.assertIn("BHMOUSE1", result["error"])

    def test_rejects_conflicting_mouse_options(self):
        trace = self.scratch / "mouse.bhmouse"
        trace.write_bytes(VALID_TRACE)
        cases = (
            (("--mouse-motion", "100:1:0", "--mouse-phase", "110:3:2"), "raw mouse phases"),
            (("--mouse-replay", trace, "--mouse-edge-cycles", "8192"), "mouse-replay supplies"),
            (("--mouse-replay", trace, "--mouse-vbl-guard", "1024"), "mouse-replay supplies"),
            (("--mouse-record", trace), "mouse-record requires live input"),
        )
        for args, message in cases:
            with self.subTest(args=args):
                result = self.boot(*args, exit_code=2)
                self.assertEqual(result["status"], "error")
                self.assertIn(message, result["error"])


if __name__ == "__main__":
    unittest.main()
