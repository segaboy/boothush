import json
import struct
import subprocess
import unittest
import os
import hashlib
from pathlib import Path

from test_boot import BootHushTestCase, make_rom
from test_mouse_cli import RESULT, SCC_A, VIA_PORT_B, sample, wait_for_bit
from test_pc286 import make_rom as make_pc_rom


SCC_WRITE_A = 0xBFFFFB
FIELDS = (
    "cycle kind address reg value x_phase y_phase pressed scc_pending scc_latched "
    "scc_dcd via_ifr via_ier irq_level"
).split()


def write_byte(address, value):
    return struct.pack(">HHI", 0x13FC, value, address)


class MacMouseWitnessCliTests(BootHushTestCase):
    def read_witness(self, path):
        header, fields, *lines = path.read_text(encoding="ascii").splitlines()
        magic, *metadata = header.split()
        self.assertEqual(magic, "BHMACMOUSE1")
        self.assertEqual(len(metadata), 5)
        self.assertEqual(fields.split(), FIELDS)
        rows = []
        for line in lines:
            values = line.split()
            self.assertEqual(len(values), len(FIELDS))
            rows.append({name: value if name == "kind" else int(value, 10)
                         for name, value in zip(FIELDS, values)})
        self.assertEqual(int(metadata[3]), len(rows))
        self.assertEqual([row["cycle"] for row in rows], sorted(row["cycle"] for row in rows))
        return tuple(map(int, metadata)), rows

    def test_witness_reports_mouse_pins_without_changing_guest(self):
        program = (
            write_byte(0xEFFFFE, 0) + write_byte(0xEFE7FE, 0x10)  # Disable the overlay.
            + write_byte(SCC_WRITE_A, 0x0F) + write_byte(SCC_WRITE_A, 0x08)  # Enable DCD latch.
            + write_byte(SCC_WRITE_A, 0x01) + write_byte(SCC_WRITE_A, 0x01)  # Enable external IRQ.
            + write_byte(SCC_WRITE_A, 0x09) + write_byte(SCC_WRITE_A, 0x0A)  # Enable master IRQ.
            + wait_for_bit(VIA_PORT_B, 4, False) + sample(VIA_PORT_B, 0x38, 0)
            + wait_for_bit(SCC_A, 3, True)
            + write_byte(SCC_WRITE_A, 0x03) + sample(SCC_A, 0x09, 1)  # Read RR3.
            + write_byte(SCC_WRITE_A, 0x10)  # Acknowledge the latched change.
            + write_byte(SCC_WRITE_A, 0x03) + sample(SCC_A, 0x09, 2)
            + wait_for_bit(VIA_PORT_B, 3, False) + sample(VIA_PORT_B, 0x38, 3)
            + wait_for_bit(VIA_PORT_B, 3, True) + sample(VIA_PORT_B, 0x38, 4)
            + write_byte(0x1A700, 0x80)
            + bytes.fromhex("60fe")
        )
        self.rom.write_bytes(make_rom(program))
        inputs = (
            "--inspect", "0xb00:5",
            "--mouse-phase", "1000:0:2", "--mouse-phase", "1200:1:2",
            "--mouse-button", "1500:1", "--mouse-button", "1800:0",
        )
        plain_frame = self.scratch / "plain.pbm"
        witness_frame = self.scratch / "witness.pbm"
        witness = self.scratch / "all.witness"
        plain = self.boot(*inputs, "--frame", plain_frame, cycles=2500)
        observed = self.boot(*inputs, "--frame", witness_frame, "--mouse-witness", witness, cycles=2500)
        self.assertEqual(plain["status"], "cycle_limit")
        self.assertEqual(plain["memory"], {"address": RESULT, "bytes": "2808002028"})
        self.assertEqual(observed, plain)
        self.assertEqual(witness_frame.read_bytes(), plain_frame.read_bytes())
        self.assertEqual(self.assert_frame(witness_frame)[0], 0x80)

        metadata, rows = self.read_witness(witness)
        self.assertEqual(metadata, (0, 2500, observed["cycles"], len(rows), 0))
        phases = [row for row in rows if row["kind"] == "phase"]
        self.assertEqual([(row["x_phase"], row["y_phase"], row["scc_pending"], row["scc_latched"],
                           row["scc_dcd"], row["irq_level"]) for row in phases],
                         [(0, 2, 0, 0, 0, 0), (1, 2, 8, 1, 1, 2)])
        buttons = [row for row in rows if row["kind"] == "button"]
        self.assertEqual([row["pressed"] for row in buttons], [1, 0])
        scc_reads = [row for row in rows if row["kind"] == "scc_read" and row["reg"] == 3]
        self.assertEqual([(row["address"], row["value"], row["scc_pending"]) for row in scc_reads],
                         [(SCC_A, 8, 8), (SCC_A, 0, 0)])
        self.assertTrue(any(row["kind"] == "scc_write" and row["address"] == SCC_WRITE_A
                            and row["reg"] == 0 and row["value"] == 0x10 for row in rows))
        self.assertTrue(any(row["kind"] == "via_read" and row["address"] == VIA_PORT_B
                            and row["reg"] == 0 and row["value"] & 0x38 == 0x20 for row in rows))
        self.assertIn(2, [row["irq_level"] for row in rows if row["kind"] == "irq"])

        first, last = phases[-1]["cycle"], buttons[0]["cycle"]
        filtered = self.scratch / "filtered.witness"
        self.assertEqual(self.boot(*inputs, "--mouse-witness", filtered,
                                   "--mouse-witness-range", f"{first:#x}:{last:#x}", cycles=2500), plain)
        metadata, filtered_rows = self.read_witness(filtered)
        self.assertEqual(metadata, (first, last, plain["cycles"], len(filtered_rows), 0))
        self.assertTrue(all(first <= row["cycle"] <= last for row in filtered_rows))
        self.assertEqual([row for row in filtered_rows if row["kind"] != "snapshot"],
                         [row for row in rows if row["kind"] != "snapshot" and first <= row["cycle"] <= last])

    def test_witness_records_scheduled_relative_motion(self):
        witness = self.scratch / "relative.witness"
        inputs = (
            "--mouse-motion", "300:1:0", "--mouse-dcd-invert", "1",
            "--mouse-edge-cycles", "100", "--mouse-vbl-guard", "0",
        )
        plain = self.boot(*inputs)
        observed = self.boot(*inputs, "--mouse-witness", witness)
        self.assertEqual(observed, plain)
        self.assertEqual(observed["mouse"], {"samples": 1, "steps": 2, "pending": 0})
        _, rows = self.read_witness(witness)
        phases = [row for row in rows if row["kind"] == "phase"]
        self.assertEqual([(row["x_phase"], row["y_phase"]) for row in phases], [(0, 2), (1, 2)])
        self.assertGreaterEqual(phases[1]["cycle"] - phases[0]["cycle"], 100)

    def test_rejects_invalid_witness_options(self):
        witness = self.scratch / "unused.witness"
        cases = (
            ("--mouse-witness-range", "1:2"),
            ("--mouse-witness", witness, "--mouse-witness-range", "10:1"),
            ("--mouse-witness", witness, "--mouse-witness-range", "10:"),
            ("--mouse-witness", witness, "--mouse-witness-range", "1:2:3"),
            ("--mouse-witness-detail", "1"),
            ("--mouse-witness", witness, "--mouse-witness-detail", "2"),
        )
        for args in cases:
            with self.subTest(args=args):
                result = self.boot(*args, exit_code=2)
                self.assertEqual(result["status"], "error")
                self.assertIn("mouse-witness", result["error"])
        self.assertFalse(witness.exists())

    def test_rejects_pc_witness(self):
        rom = self.scratch / "halt.rom"
        rom.write_bytes(make_pc_rom(bytes.fromhex("f4")))
        witness = self.scratch / "pc.witness"
        process = subprocess.run(
            [self.exe, "--machine", "pc286", "--rom", rom, "--cycles", "2000",
             "--timeout-ms", "2000", "--mouse-witness", witness],
            capture_output=True, encoding="utf-8", timeout=7,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(process.returncode, 2, (process.stdout, process.stderr))
        result = json.loads(process.stdout)
        self.assertEqual(result["status"], "error")
        self.assertIn("mac128k", result["error"])
        self.assertFalse(witness.exists())

    def read_detail(self, path):
        header, fields, *lines = path.read_text(encoding="ascii").splitlines()
        magic, *meta = header.split()
        self.assertEqual(magic, "BHMACMOUSE2")
        meta = list(map(int, meta))
        self.assertEqual(len(meta), 10)
        names = fields.split()
        rows = [dict(zip(names, [value if name == "kind" else int(value)
                                 for name, value in zip(names, line.split())])) for line in lines]
        self.assertEqual(len(rows), meta[3])
        self.assertEqual([row["seq"] for row in rows], list(range(len(rows))))
        self.assertEqual([row["cycle"] for row in rows], sorted(row["cycle"] for row in rows))
        return meta, rows

    def test_detail_sequence_replay_and_observer_invariance(self):
        trace = self.scratch / "record.bhmouse"
        detail = self.scratch / "detail.txt"
        plain_frame, detail_frame = self.scratch / "plain.pbm", self.scratch / "detail.pbm"
        common = ("--mouse-motion", "1000:2:-1", "--mouse-button", "2000:1",
                  "--mouse-motion", "400000:1:0", "--mouse-button", "500000:0",
                  "--mouse-motion", "800000:-1:0", "--mouse-edge-cycles", "100",
                  "--mouse-vbl-guard", "4096", "--inspect", "0xb00:4")
        plain = self.boot(*common, "--frame", plain_frame, cycles=1100000)
        traced = self.boot(*common, "--frame", detail_frame, "--mouse-record", trace,
                           "--mouse-witness", detail, "--mouse-witness-detail", "1", cycles=1100000)
        self.assertEqual(plain, traced)
        self.assertEqual(plain_frame.read_bytes(), detail_frame.read_bytes())
        meta, rows = self.read_detail(detail)
        self.assertEqual(meta[4:7], [0, 1, 1])
        self.assertEqual(meta[7:], [100, 4096, 0])
        accepted = [r for r in rows if r["kind"] == "sample_accepted"]
        self.assertEqual([(r["dx"], r["dy"], r["value"]) for r in accepted],
                         [(2, -1, 0), (0, 0, 2), (1, 0, 0), (0, 0, 1), (-1, 0, 0)])
        self.assertTrue(all(r["source"] == 1 for r in accepted))
        self.assertEqual(len([r for r in rows if r["kind"] == "input_due"]), 5)
        self.assertTrue(any(r["kind"] == "guard" and r["deadline"] > r["due"] for r in rows))
        self.assertTrue(any(r["kind"] == "phase" for r in rows))
        replay_detail = self.scratch / "replay.txt"
        replay_frame = self.scratch / "replay.pbm"
        replay = self.boot("--mouse-replay", trace, "--inspect", "0xb00:4", "--frame", replay_frame,
                           "--mouse-witness", replay_detail, "--mouse-witness-detail", "1", cycles=1100000)
        self.assertEqual(traced, replay)
        self.assertEqual(plain_frame.read_bytes(), replay_frame.read_bytes())
        _, replay_rows = self.read_detail(replay_detail)
        self.assertEqual([r["source"] for r in replay_rows if r["kind"] == "sample_accepted"], [3] * 5)

    def test_detail_bounded_range_truncation_and_readiness(self):
        self.rom.write_bytes(make_rom(bytes.fromhex("1039 00ef e1fe 60f8")))
        full = self.scratch / "full.txt"
        result = self.boot("--mouse-witness", full, "--mouse-witness-detail", "1", cycles=3000000)
        meta, rows = self.read_detail(full)
        self.assertEqual(meta[3], 65536)
        self.assertGreater(meta[4], 0)
        self.assertEqual(meta[5], 0)
        narrow = self.scratch / "narrow.txt"
        self.assertEqual(self.boot("--mouse-witness", narrow, "--mouse-witness-detail", "1",
                                   "--mouse-witness-range", "1000:2000", cycles=3000000), result)
        meta, rows = self.read_detail(narrow)
        self.assertEqual(meta[4:7], [0, 1, 1])
        self.assertTrue(all(1000 <= r["cycle"] <= 2000 for r in rows))
        absent = self.scratch / "absent.txt"
        missed = self.boot("--input-after", "0xb00:4:0xdeadbeef", "--mouse-motion", "0:1:0",
                           "--mouse-witness", absent, "--mouse-witness-detail", "1", cycles=2000, exit_code=1)
        self.assertEqual(missed["status"], "readiness_not_reached")
        self.assertEqual(missed["mouse"]["steps"], 0)
        _, rows = self.read_detail(absent)
        self.assertFalse(any(r["kind"] in ("input_due", "sample_accepted") for r in rows))
        early = self.scratch / "early.txt"
        self.boot("--mouse-witness", early, "--mouse-witness-detail", "1",
                  "--mouse-witness-range", "3000:4000", cycles=2000)
        meta, rows = self.read_detail(early)
        self.assertEqual(meta[3:7], [0, 0, 1, 0])
        self.assertEqual(rows, [])

    @unittest.skipUnless(os.environ.get("BOOTHUSH_CLASSICK_LIVE_ROM"), "ClasSICK live ROM not supplied")
    def test_classick_four_direction_timing_witness(self):
        rom = Path(os.environ["BOOTHUSH_CLASSICK_LIVE_ROM"])
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),
                         "faf37c6f96f8eb2ee6cf7a1e58a8b1b9538055d2fc89493c7419eeea816f6c8c")
        for edge in (4096, 2048):
            for dx, dy in ((32, 0), (-32, 0), (0, 32), (0, -32)):
                with self.subTest(edge=edge, dx=dx, dy=dy):
                    path = self.scratch / f"{edge}-{dx}-{dy}.txt"
                    result = self.boot("--mouse-dcd-invert", "3", "--mouse-edge-cycles", str(edge),
                        "--mouse-vbl-guard", "4096", "--mouse-motion", f"60000000:{dx}:{dy}",
                        "--inspect", "0xb00:1024", "--mouse-witness", path,
                        "--mouse-witness-detail", "1", "--mouse-witness-range", "60000000:60500000",
                        rom=rom, cycles=65000000, timeout_ms=20000)
                    data = bytes.fromhex(result["memory"]["bytes"])
                    def word(address):
                        return int.from_bytes(data[address - 0xb00:address - 0xb00 + 4], "big")
                    expected = (256 + dx, 171 + dy) if edge == 4096 else (
                        256 - (1 if dx > 0 else -1 if dx < 0 else 0),
                        171 - (1 if dy > 0 else -1 if dy < 0 else 0))
                    self.assertEqual((word(0xc5c), word(0xc60)), expected)
                    self.assertEqual(word(0xc1c), 0 if edge == 4096 else 29)
                    self.assertEqual(result["mouse"], {"samples": 1, "steps": 64, "pending": 0})
                    meta, rows = self.read_detail(path)
                    self.assertEqual(meta[4:7], [0, 1, 1])
                    self.assertEqual(len([r for r in rows if r["kind"] == "phase"]), 64)
                    self.assertTrue(any(r["kind"] == "irq_accept" for r in rows))
                    self.assertTrue(any(r["kind"] == "via_read" for r in rows))
                    self.assertTrue(any(r["kind"] == "scc_read" for r in rows))
                    accepted = [r for r in rows if r["kind"] == "sample_accepted"]
                    self.assertEqual([(r["dx"], r["dy"], r["pending"]) for r in accepted], [(dx, dy, 64)])


if __name__ == "__main__":
    unittest.main()
