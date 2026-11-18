"""Original BIOS sector-write fixtures; no reference firmware or disk input."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


FLOPPY_SIZE = 80 * 2 * 15 * 512
MARKER = 0x7b
DIAGNOSTIC = 0x500
BEFORE_BUFFER = 0x10000
AFTER_BUFFER = 0x14000


def word(value):
    return int(value).to_bytes(2, "little")


def chs_word(cylinder, sector):
    return ((cylinder & 255) << 8) | ((cylinder >> 2) & 0xc0) | sector


def lba(cylinder, head, sector):
    return (cylinder * 2 + head) * 15 + sector - 1


def request(operation, count, cylinder, head, sector, *, es=0, bx=0x2000, drive=0):
    # Original real-mode interface fixture: set ES, AX/BX/CX/DX; INT 13h.
    return (b"\xb8" + word(es) + b"\x8e\xc0\xb8" + word((operation << 8) | count)
            + b"\xbb" + word(bx) + b"\xb9" + word(chs_word(cylinder, sector))
            + b"\xba" + word((head << 8) | drive) + b"\xcd\x13")


def observe(slot, drive=0):
    address = DIAGNOSTIC + slot * 6
    status_address = 0x474 if drive & 0x80 else 0x441
    # Save AX and FLAGS before reading the BIOS disk-status byte.
    return (b"\xa3" + word(address) + b"\x9c\x58\xa3" + word(address + 2)
            + b"\xa0" + word(status_address) + b"\xa2" + word(address + 4))


def fixture(*, count=2, cylinder=5, head=1, sector=14, write=None,
            source_es=0, source_bx=0x2000):
    payload = bytes((index * 37 + (index // 512) * 13 + 11) & 255 for index in range(count * 512))
    image = bytearray([0x6b]) * FLOPPY_SIZE
    source_at = lba(0, 1, 1) * 512
    image[source_at:source_at + len(payload)] = payload
    destination = lba(cylinder, head, sector) * 512
    original = bytes((index * 9 + 0x91) & 255 for index in range(len(payload)))
    image[destination:destination + len(original)] = original
    program = bytearray.fromhex("fa 31c0 8ed8 8ed0 bc007c fc")
    program += request(2, count, 0, 1, 1, es=source_es, bx=source_bx) + observe(0)
    program += request(2, count, cylinder, head, sector, es=BEFORE_BUFFER >> 4, bx=0) + observe(1)
    writing = dict(count=count, cylinder=cylinder, head=head, sector=sector,
                   es=source_es, bx=source_bx, drive=0)
    if write:
        writing.update(write)
    program += request(3, **writing) + observe(2, writing["drive"])
    program += request(1, 0, 0, 0, 1, drive=writing["drive"]) + observe(3, writing["drive"])
    program += request(2, count, cylinder, head, sector, es=AFTER_BUFFER >> 4, bx=0) + observe(4)
    program += bytes([0xb0, MARKER, 0xe6, 0x80, 0xfa, 0xf4])
    if len(program) > 510:
        raise ValueError("original fixture exceeds boot sector")
    image[:510] = bytes(510)
    image[:len(program)] = program
    image[510:512] = b"\x55\xaa"
    return bytes(image), payload, original, destination


class Pc286FloppyWriteTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ["BOOTHUSH_EXE"]).resolve(strict=True)

    def setUp(self):
        scratch = tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP"))
        self.addCleanup(scratch.cleanup)
        self.scratch = Path(scratch.name)
        self.serial = 0

    def boot(self, image, length, *, writable=False):
        self.serial += 1
        source = image if isinstance(image, Path) else self.scratch / f"input-{self.serial}.img"
        if not isinstance(image, Path):
            source.write_bytes(image)
        original_input = source.read_bytes()
        digest = hashlib.sha256(original_input).digest()
        output = self.scratch / f"output-{self.serial}.img"
        inspections = [(DIAGNOSTIC, 30)]
        for base in (BEFORE_BUFFER, AFTER_BUFFER):
            for offset in range(0, length, 4096):
                inspections.append((base + offset, min(4096, length - offset)))
        command = [self.exe, "--machine", "pc286", "--floppy", source,
                   "--cycles", "1000000", "--timeout-ms", "2000", "--until-post", hex(MARKER)]
        if writable:
            command += ["--floppy-out", output]
        for address, size in inspections:
            command += ["--inspect", f"{address:#x}:{size}"]
        process = subprocess.run(command, capture_output=True, encoding="utf-8", timeout=7,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        diagnostic = f"stdout={process.stdout!r}, stderr={process.stderr!r}"
        self.assertEqual(process.returncode, 0, diagnostic)
        self.assertEqual(process.stderr, "", diagnostic)
        response = json.loads(process.stdout)
        self.assertEqual(response["status"], "condition_met", diagnostic)
        self.assertEqual(response["post"], MARKER, diagnostic)
        self.assertEqual(response["cs"], 0, diagnostic)
        self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), digest)
        self.assertEqual(source.read_bytes(), original_input)
        ranges = response.get("memory_ranges") or [response["memory"]]
        memory = {item["address"]: bytes.fromhex(item["bytes"]) for item in ranges}
        self.assertEqual(set(memory), {address for address, _ in inspections})
        for address, size in inspections:
            self.assertEqual(len(memory[address]), size)
        before = b"".join(memory[BEFORE_BUFFER + offset] for offset in range(0, length, 4096))
        after = b"".join(memory[AFTER_BUFFER + offset] for offset in range(0, length, 4096))
        self.assertEqual(output.exists(), writable)
        if writable:
            self.assertEqual(output.stat().st_size, FLOPPY_SIZE)
        return memory[DIAGNOSTIC], before, after, output

    def result(self, diagnostic, slot, status, count):
        result = diagnostic[slot * 6:(slot + 1) * 6]
        self.assertEqual(int.from_bytes(result[:2], "little"), status * 256 + count)
        self.assertEqual(int.from_bytes(result[2:4], "little") & 1, int(status != 0))
        self.assertEqual(result[4], status)

    def test_default_write_protection_preserves_media_and_readback(self):
        image, payload, original, _ = fixture()
        diagnostic, before, after, _ = self.boot(image, len(payload))
        self.result(diagnostic, 0, 0, 2)
        self.result(diagnostic, 1, 0, 2)
        self.result(diagnostic, 2, 3, 0)
        self.result(diagnostic, 3, 3, 0)
        self.result(diagnostic, 4, 0, 2)
        self.assertEqual(before, original)
        self.assertEqual(after, original)

    def test_opt_in_writes_exact_range_and_survives_fresh_readonly_boot(self):
        image, payload, original, destination = fixture()
        diagnostic, before, after, output = self.boot(image, len(payload), writable=True)
        self.result(diagnostic, 2, 0, 2)
        self.result(diagnostic, 3, 0, 0)
        self.result(diagnostic, 4, 0, 2)
        self.assertEqual(before, original)
        self.assertEqual(after, payload)
        expected = bytearray(image)
        expected[destination:destination + len(payload)] = payload
        self.assertEqual(output.read_bytes(), expected)
        # Same original boot code, new machine and no write opt-in. The first
        # read occurs BEFORE its now-refused write, proving saved persistence.
        diagnostic, before, after, _ = self.boot(output, len(payload))
        self.result(diagnostic, 2, 3, 0)
        self.result(diagnostic, 3, 3, 0)
        self.assertEqual(before, payload)
        self.assertEqual(after, payload)
        self.assertEqual(output.read_bytes(), expected)

    def test_single_sector_full_final_track_and_exact_buffer_boundaries(self):
        cases = [dict(count=1, sector=15),
                 dict(count=15, cylinder=79, head=1, sector=1),
                 dict(count=1, source_bx=0xfe00),
                 dict(count=1, source_es=0x9fe0, source_bx=0)]
        for case in cases:
            with self.subTest(case=case):
                image, payload, original, destination = fixture(**case)
                count = len(payload) // 512
                diagnostic, before, after, output = self.boot(image, len(payload), writable=True)
                self.result(diagnostic, 0, 0, count)
                self.result(diagnostic, 2, 0, count)
                self.result(diagnostic, 3, 0, 0)
                self.result(diagnostic, 4, 0, count)
                self.assertEqual(before, original)
                self.assertEqual(after, payload)
                expected = bytearray(image)
                expected[destination:destination + len(payload)] = payload
                self.assertEqual(output.read_bytes(), expected)

    def test_invalid_geometry_count_and_buffer_leave_entire_copy_unchanged(self):
        cases = [(dict(count=0), 4), (dict(count=128), 4), (dict(count=255), 4),
                 (dict(cylinder=80), 4), (dict(cylinder=1023), 4), (dict(head=2), 4),
                 (dict(sector=0), 4), (dict(sector=16), 4), (dict(sector=15, count=2), 4),
                 (dict(drive=1), 1), (dict(es=0, bx=0xff00), 9),
                 (dict(es=0xa000, bx=0), 9), (dict(es=0xffff, bx=0xffff), 9)]
        for writing, status in cases:
            with self.subTest(write=writing, status=status):
                image, payload, original, _ = fixture(write=writing)
                diagnostic, before, after, output = self.boot(image, len(payload), writable=True)
                self.result(diagnostic, 0, 0, 2)
                self.result(diagnostic, 2, status, 0)
                self.result(diagnostic, 3, status if writing.get("drive", 0) == 0 else 1, 0)
                self.result(diagnostic, 4, 0, 2)
                self.assertEqual(before, original)
                self.assertEqual(after, original)
                self.assertEqual(output.read_bytes(), image)


if __name__ == "__main__":
    unittest.main()
