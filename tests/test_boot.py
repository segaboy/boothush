import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROM_SIZE = 65536
ROM_ENTRY = 0x400104
FRAME_SIZE = 64 * 342
CLASSICK_ROM_SHA256 = (
    "be506ad6e41d06ebcfe11678a4f6a64ba560920b16efa7fef884794f51690005"
)


def make_rom(instructions):
    image = bytearray(ROM_SIZE)
    struct.pack_into(">II", image, 0, 0x4000, ROM_ENTRY)
    image[0x104 : 0x104 + len(instructions)] = instructions
    return image


SMOKE_PROGRAM = bytes.fromhex(
    "13fc 0000 00ef fffe "  # MOVE.B #$00,$EFFFFE.L: clear VIA port A output latch
    "13fc 0010 00ef e7fe "  # MOVE.B #$10,$EFE7FE.L: drive overlay bit low
    "13fc 0080 0001 a700 "  # MOVE.B #$80,$01A700.L: set first screen pixel
    "23fc 1234 abcd 0000 0b00 "  # MOVE.L #$1234ABCD,$000B00.L: publish result
    "60fe"  # BRA.S to itself
)


class BootHushTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        exe = os.environ.get("BOOTHUSH_EXE")
        if not exe:
            raise RuntimeError("Set BOOTHUSH_EXE to the BootHush executable.")
        cls.exe = Path(exe).resolve(strict=True)

    def setUp(self):
        scratch = tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP"))
        self.addCleanup(scratch.cleanup)
        self.scratch = Path(scratch.name)
        self.rom = self.scratch / "smoke.rom"
        self.rom.write_bytes(make_rom(SMOKE_PROGRAM))

    def boot(self, *args, rom=None, cycles=2000, timeout_ms=2000, exit_code=0):
        process = subprocess.run(
            [
                self.exe,
                "--machine", "mac128k",
                "--rom", self.rom if rom is None else rom,
                "--cycles", str(cycles),
                "--timeout-ms", str(timeout_ms),
                *args,
            ],
            capture_output=True,
            encoding="utf-8",
            timeout=timeout_ms / 1000 + 5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        diagnostic = f"stdout={process.stdout!r}, stderr={process.stderr!r}"
        self.assertEqual(process.returncode, exit_code, diagnostic)
        lines = process.stdout.splitlines()
        self.assertEqual(len(lines), 1, diagnostic)
        return json.loads(lines[0])

    def assert_frame(self, path):
        magic, geometry, pixels = path.read_bytes().split(b"\n", 2)
        self.assertEqual(magic, b"P4")
        self.assertEqual(geometry.split(), [b"512", b"342"])
        self.assertEqual(len(pixels), FRAME_SIZE)
        return pixels


class BootHushTests(BootHushTestCase):
    def test_condition_memory_and_frame(self):
        frame = self.scratch / "frame.pbm"
        result = self.boot(
            "--until", "2816:4:0x1234abcd",
            "--inspect", "0xb00:4",
            "--frame", frame,
        )
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"], {"address": 0xB00, "bytes": "1234abcd"})
        self.assertGreater(result["cycles"], 0)
        self.assertLess(result["cycles"], 2000)
        self.assertGreater(result["pc"], ROM_ENTRY)
        self.assertEqual(result["vbl"], 0)
        self.assertEqual(self.assert_frame(frame), b"\x80" + bytes(FRAME_SIZE - 1))

    def test_byte_and_word_conditions(self):
        for condition in ("0xb01:1:0x34", "0xb02:2:0xabcd"):
            with self.subTest(condition=condition):
                result = self.boot("--until", condition)
                self.assertEqual(result["status"], "condition_met")

    def test_cycle_limit_without_condition(self):
        result = self.boot()
        self.assertEqual(result["status"], "cycle_limit")
        # A whole-instruction boundary can move the count by a BRA.S cycle cost.
        self.assertLess(abs(result["cycles"] - 2000), 10)

    def test_unmet_condition_is_unsuccessful(self):
        result = self.boot("--until", "0xb00:4:0x1234abcd", cycles=4, exit_code=1)
        self.assertEqual(result["status"], "cycle_limit")

    def test_invalid_condition_width(self):
        result = self.boot("--until", "0xb00:3:1", exit_code=2)
        self.assertEqual(result["status"], "error")

    def test_rejects_inspection_past_ram(self):
        result = self.boot("--inspect", "0x1ffff:2", exit_code=2)
        self.assertEqual(result["status"], "error")

    def test_rejects_short_rom(self):
        self.rom.write_bytes(b"\x00" * (ROM_SIZE - 1))
        result = self.boot(exit_code=2)
        self.assertEqual(result["status"], "error")

    def test_unmapped_write_faults(self):
        fault_program = bytes.fromhex("13fc 0001 0080 0000 60fe")
        self.rom.write_bytes(make_rom(fault_program))
        result = self.boot(exit_code=1)
        self.assertEqual(result["status"], "machine_fault")

    def test_odd_word_access_takes_68000_address_error_vector(self):
        program = bytes.fromhex(
            "13fc 0000 00ef fffe 13fc 0010 00ef e7fe "  # Clear overlay.
            "23fc 0040 0128 0000 000c "  # Install address-error vector 3.
            "3039 0000 0b01 "  # Read a word at odd address $B01.
            "60fe 4e71 "  # Spin if no exception was raised and pad to handler.
            "13fc 00a3 0000 0b00 60fe"  # Handler at $400128 reports success.
        )
        self.rom.write_bytes(make_rom(program))
        result = self.boot("--until", "0xb00:1:0xa3")
        self.assertEqual(result["status"], "condition_met")

    def test_via_timer_interrupt_flags_repeat_after_acknowledgement(self):
        program = bytes.fromhex(
            "13fc 0000 00ef fffe "  # Clear overlay output latch.
            "13fc 0010 00ef e7fe "  # Drive overlay low.
            "13fc 0040 00ef f7fe "  # ACR: free-running T1.
            "13fc 00ff 00ef e9fe "  # T1 low latch.
            "13fc 0000 00ef ebfe "  # T1 high latch and start.
            "13fc 00c0 00ef fdfe "  # Enable T1 interrupt, CPU still masked.
            "13f9 00ef f7fe 0000 0b00 "  # Save ACR and low latch.
            "13f9 00ef edfe 0000 0b01 "
            "0839 0006 00ef fbfe 67f6 "  # Poll IFR bit 6.
            "13f9 00ef fbfe 0000 0b02 "  # Save IFR with T1 pending.
            "13f9 00ef e9fe 0000 0b03 "  # Read T1CL to acknowledge.
            "13f9 00ef fbfe 0000 0b04 "  # Save cleared IFR.
            "0839 0006 00ef fbfe 67f6 "  # Wait for next expiry.
            "13f9 00ef fbfe 0000 0b05 60fe"
        )
        self.rom.write_bytes(make_rom(program))
        result = self.boot("--inspect", "0xb00:6", cycles=8000)
        self.assertEqual(result["status"], "cycle_limit")
        samples = bytes.fromhex(result["memory"]["bytes"])
        self.assertEqual(samples[:3], bytes.fromhex("40ff c0"))
        self.assertEqual(samples[4:], bytes.fromhex("00c0"))

    def test_scc_mask_readback_distinguishes_channels(self):
        program = bytes.fromhex(
            "13fc 0000 00ef fffe 13fc 0010 00ef e7fe "  # Clear overlay.
            "13fc 000f 00bf fffb 13fc 0008 00bf fffb "  # A WR15 = DCD.
            "13fc 000f 00bf fff9 13fc 0000 00bf fff9 "  # B WR15 = 0.
            "13fc 000f 00bf fffb "  # Select RR15, then read B.
            "13f9 009f fff8 0000 0b00 "
            "13f9 009f fffa 0000 0b01 "  # Pointer returned to RR0.
            "13fc 000f 00bf fffb 13f9 009f fffa 0000 0b02 "  # A RR15.
            "13fc 0009 00bf fffb 13fc 00c0 00bf fffb "  # Hardware reset.
            "13fc 000f 00bf fffb 13f9 009f fffa 0000 0b03 60fe"
        )
        self.rom.write_bytes(make_rom(program))
        result = self.boot("--inspect", "0xb00:4")
        self.assertEqual(result["status"], "cycle_limit")
        self.assertEqual(result["memory"]["bytes"], "00040800")

    def test_unicode_rom_and_frame_paths(self):
        rom = self.scratch / "test \u6d4b\u8bd5.rom"
        frame = self.scratch / "test \u6d4b\u8bd5.pbm"
        self.rom.rename(rom)
        result = self.boot("--frame", frame, rom=rom)
        self.assertEqual(result["status"], "cycle_limit")
        self.assertEqual(self.assert_frame(frame)[0], 0x80)

    @unittest.skipUnless(os.environ.get("BOOTHUSH_CLASSICK_ROM"), "fixture not supplied")
    def test_classick_startup_first_tick(self):
        rom = Path(os.environ["BOOTHUSH_CLASSICK_ROM"]).resolve(strict=True)
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(), CLASSICK_ROM_SHA256)
        frame = self.scratch / "classick.pbm"
        result = self.boot(
            "--until", "0xb08:4:1",
            "--inspect", "0xb00:28",
            "--frame", frame,
            rom=rom,
            cycles=80_000_000,
            timeout_ms=10_000,
        )
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["address"], 0xB00)
        diagnostic = struct.unpack(">7I", bytes.fromhex(result["memory"]["bytes"]))
        self.assertEqual(diagnostic, (0x43533039, 3, 1, 0, 1, 0xE333E427, 0))
        self.assertGreaterEqual(result["vbl"], 2)

        checksum = 2166136261
        for byte in self.assert_frame(frame):
            checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
        self.assertEqual(checksum, 0x9D63DD89)

    @unittest.skipUnless(os.environ.get("BOOTHUSH_CLASSICK_WINDOW_ROM"), "fixture not supplied")
    def test_classick_window_button_and_quadrature(self):
        # Use an invert-3 window ROM with the MWP1 v2 and CS09 diagnostics.
        rom = Path(os.environ["BOOTHUSH_CLASSICK_WINDOW_ROM"]).resolve(strict=True)
        expected_hash = os.environ["BOOTHUSH_CLASSICK_WINDOW_ROM_SHA256"].lower()
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(), expected_hash)

        def run(frame, cycles, *events):
            result = self.boot(
                "--inspect", "0xb00:1024", "--frame", frame, *events,
                rom=rom, cycles=cycles, timeout_ms=10_000,
            )
            self.assertEqual(result["status"], "cycle_limit")
            memory = bytes.fromhex(result["memory"]["bytes"])
            trace_offset = memory.find(b"MWP1")
            diag_offset = memory.find(b"CS09")
            self.assertGreaterEqual(trace_offset, 0, "MWP1 trace is missing")
            self.assertGreaterEqual(diag_offset, 0, "CS09 diagnostics are missing")
            trace = struct.unpack_from(">32I", memory, trace_offset)
            diag = struct.unpack_from(">7I", memory, diag_offset)
            self.assertEqual(trace[1:3], (2, 128))
            self.assertEqual((diag[1], diag[4], diag[6]), (16, 1, 0))
            return trace, self.assert_frame(frame)

        before, idle_frame = run(self.scratch / "idle.pbm", 100_000_000)
        after, replay_frame = run(
            self.scratch / "replay.pbm", 124_000_000,
            "--mouse-button", "100000000:1", "--mouse-button", "104000000:0",
            "--mouse-phase", "108000000:3:2", "--mouse-phase", "112000000:1:2",
            "--mouse-phase", "116000000:0:2", "--mouse-phase", "120000000:2:2",
        )
        self.assertEqual((before[8], before[9], before[12]), (1, 0, 0))
        self.assertEqual((after[8], after[9], after[12], after[22]), (1, 0, 4, 0))
        self.assertEqual((after[5] - before[5], after[13] - before[13]), (2, -2))
        self.assertEqual((after[14], after[17]), (before[14], before[17] + 1))
        self.assertNotEqual(replay_frame, idle_frame)


if __name__ == "__main__":
    unittest.main()
