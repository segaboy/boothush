"""Original headless Mac quadrature pacing, polarity and replay regression."""
import struct
import unittest

from test_boot import BootHushTestCase, make_rom


VBL = 130240
SCC_A = 0x9FFFFA
SCC_B = 0x9FFFF8
VIA_B = 0xEFE1FE


def write_byte(address, value):
    return struct.pack(">HHI", 0x13FC, value, address)


def polling_rom():
    # Original fixed-path fixture: enable both SCC DCD latches, keep reset IPL7,
    # and read both SCC channels and VIA repeatedly without acknowledging them.
    # Equal instruction paths isolate bridge pacing from guest branch/IRQ costs.
    setup = write_byte(0xEFFFFE, 0) + write_byte(0xEFE7FE, 0x10)
    for port in (0xBFFFFB, 0xBFFFF9):
        setup += write_byte(port, 15) + write_byte(port, 8)
        setup += write_byte(port, 1) + write_byte(port, 1)
    setup += write_byte(0xBFFFFB, 9) + write_byte(0xBFFFFB, 10)
    loop = b"".join(struct.pack(">HII", 0x13F9, port, 0xB00 + offset)
                    for offset, port in enumerate((SCC_A, SCC_B, VIA_B)))
    loop += struct.pack(">H", 0x6000 | ((-len(loop) - 2) & 0xFF))
    return make_rom(setup + loop)


class MacMouseTimingTests(BootHushTestCase):
    def setUp(self):
        super().setUp()
        self.rom.write_bytes(polling_rom())

    def read_detail(self, path):
        header, columns, *lines = path.read_text(encoding="ascii").splitlines()
        magic, *metadata = header.split()
        self.assertEqual(magic, "BHMACMOUSE2")
        metadata = list(map(int, metadata))
        fields = columns.split()
        rows = []
        for line in lines:
            values = line.split()
            self.assertEqual(len(values), len(fields))
            rows.append({key: value if key == "kind" else int(value)
                         for key, value in zip(fields, values)})
        self.assertEqual(metadata[3], len(rows))
        self.assertEqual(metadata[4:7], [0, 1, 1], "incomplete witness")
        self.assertEqual([row["seq"] for row in rows], list(range(len(rows))))
        self.assertEqual([row["cycle"] for row in rows], sorted(row["cycle"] for row in rows))
        return rows

    def assert_phase_displacement(self, phases, vectors):
        # Decode physical DCD edges independently of bridge vector distribution.
        # Inversion mask 3 matches the explicit setting used by these cases.
        previous = (2, 2)
        x = y = 0
        checkpoints = {}
        count = total_x = total_y = 0
        for dx, dy in vectors:
            count += 2 * max(abs(dx), abs(dy))
            total_x += dx
            total_y += dy
            checkpoints[count] = (total_x, total_y)
        for index, row in enumerate(phases, 1):
            current = row["x_phase"], row["y_phase"]
            edges = previous[0] ^ current[0], previous[1] ^ current[1]
            self.assertTrue(all(edge in (0, 1, 2) for edge in edges))
            self.assertNotEqual(edges, (0, 0))
            if edges[0] == 1:
                x += 1 if bool((current[0] ^ 1) & 1) == bool(current[0] & 2) else -1
            if edges[1] == 1:
                y += 1 if bool((current[1] ^ 1) & 1) != bool(current[1] & 2) else -1
            if index in checkpoints:
                self.assertEqual((x, y), checkpoints[index])
            previous = current
        self.assertEqual(len(phases), count)

    def test_axis_swap_backlog_vbl_guard_and_replay(self):
        vectors = ((6, 2), (1, -3), (-4, 1))
        # X and Y use opposite direction polarity. Negating while swapping
        # preserves the physical phase trajectory when inversion remains 3.
        swapped = tuple((-dy, -dx) for dx, dy in vectors)
        for edge in (2048, 4096):
            for guard in (0, 4096):
                with self.subTest(edge=edge, guard=guard):
                    trajectories = []
                    for variant, movements in enumerate((vectors, swapped)):
                        stem = self.scratch / f"{edge}-{guard}-{variant}"
                        record = stem.with_suffix(".bhmouse")
                        witness = stem.with_suffix(".witness")
                        replay_witness = stem.with_suffix(".replay-witness")
                        plain_frame = stem.with_suffix(".plain.pbm")
                        traced_frame = stem.with_suffix(".traced.pbm")
                        replay_frame = stem.with_suffix(".replay.pbm")
                        common = ["--mouse-dcd-invert", "3", "--mouse-edge-cycles", str(edge),
                                  "--mouse-vbl-guard", str(guard)]
                        for due, (dx, dy) in zip((124000, 125000, 126000), movements):
                            common += ["--mouse-motion", f"{due}:{dx}:{dy}"]
                        inspect = ("--inspect", "0xb00:3")
                        plain = self.boot(*common, *inspect, "--frame", plain_frame, cycles=310000)
                        traced = self.boot(*common, *inspect, "--frame", traced_frame,
                                           "--mouse-record", record, "--mouse-witness", witness,
                                           "--mouse-witness-detail", "1", cycles=310000)
                        replayed = self.boot("--mouse-replay", record, *inspect, "--frame", replay_frame,
                                             "--mouse-witness", replay_witness,
                                             "--mouse-witness-detail", "1", cycles=310000)
                        self.assertEqual(plain, traced)
                        self.assertEqual(traced, replayed)
                        self.assertEqual(plain_frame.read_bytes(), traced_frame.read_bytes())
                        self.assertEqual(traced_frame.read_bytes(), replay_frame.read_bytes())
                        self.assertEqual(traced["status"], "cycle_limit")
                        self.assertEqual(traced["mouse"], {"samples": 3, "steps": 26, "pending": 0})
                        rows = self.read_detail(witness)
                        replay_rows = self.read_detail(replay_witness)
                        accepted = [r for r in rows if r["kind"] == "sample_accepted"]
                        self.assertEqual([(r["dx"], r["dy"]) for r in accepted], list(movements))
                        self.assertTrue(all(r["source"] == 1 for r in accepted))
                        self.assertEqual([r["source"] for r in replay_rows if r["kind"] == "sample_accepted"],
                                         [3, 3, 3])
                        # The second vector is accepted while the first remains queued.
                        self.assertGreater(accepted[1]["pending"], 2 * max(map(abs, movements[1])))
                        phases = [r for r in rows if r["kind"] == "phase"]
                        replay_phases = [r for r in replay_rows if r["kind"] == "phase"]
                        key = lambda r: (r["cycle"], r["x_phase"], r["y_phase"], r["pending"])
                        self.assertEqual(list(map(key, phases)), list(map(key, replay_phases)))
                        self.assert_phase_displacement(phases, movements)
                        self.assertTrue(all(b["cycle"] - a["cycle"] >= edge
                                            for a, b in zip(phases, phases[1:])))
                        self.assertTrue(all(guard <= r["cycle"] % VBL < VBL - guard for r in phases))
                        self.assertEqual(any(r["kind"] == "guard" for r in rows), bool(guard))
                        self.assertTrue(any(r["cycle"] < VBL for r in phases))
                        self.assertTrue(any(r["cycle"] > VBL for r in phases))
                        trajectories.append(list(map(key, phases)))
                    normalized = [(cycle, y, x, pending) for cycle, x, y, pending in trajectories[1]]
                    self.assertEqual(trajectories[0], normalized)

    def test_first_dcd_edge_depends_on_direction_and_axis(self):
        # Equal phase pacing does not imply equal first SCC interrupt times.
        for dx, dy, axis, first_dcd in ((3, 0, 0, 1), (-3, 0, 0, 0),
                                      (0, 3, 1, 0), (0, -3, 1, 1)):
            with self.subTest(dx=dx, dy=dy):
                witness = self.scratch / f"direction-{dx}-{dy}.witness"
                result = self.boot("--mouse-motion", f"1000:{dx}:{dy}", "--mouse-dcd-invert", "3",
                                   "--mouse-edge-cycles", "2048", "--mouse-vbl-guard", "0",
                                   "--mouse-witness", witness, "--mouse-witness-detail", "1", cycles=15000)
                self.assertEqual(result["mouse"], {"samples": 1, "steps": 6, "pending": 0})
                phases = [r for r in self.read_detail(witness) if r["kind"] == "phase"]
                self.assert_phase_displacement(phases, ((dx, dy),))
                phase_key = "y_phase" if axis else "x_phase"
                previous = 2
                dcd_indices = []
                for index, row in enumerate(phases):
                    if (row[phase_key] ^ previous) == 1:
                        dcd_indices.append(index)
                    previous = row[phase_key]
                self.assertEqual(dcd_indices, list(range(first_dcd, 6, 2)))
                pending_bit = 1 if axis else 8
                self.assertTrue(all(not (r["scc_pending"] & pending_bit) for r in phases[:first_dcd]))
                self.assertTrue(all(r["scc_pending"] & pending_bit for r in phases[first_dcd:]))


if __name__ == "__main__":
    unittest.main()
