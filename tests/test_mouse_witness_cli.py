import json
import struct
import subprocess
import unittest

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


if __name__ == "__main__":
    unittest.main()
