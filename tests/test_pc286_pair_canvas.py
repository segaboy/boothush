"""Original, headless PC/AT EGA centered-canvas diagnostic; not a ClasSICK boot."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


def make_floppy(wait_for_key=False):
    # Original 16-bit boot sector. Set BIOS EGA mode 10h, then write 64 bytes
    # on each of 342 rows beginning at (64,4) in the 80-byte planar stride.
    # The POST byte is a guest stop marker. No external firmware/font is used.
    code = bytes.fromhex(
        "fc b81000 cd10 b800a0 8ec0 bf4801 b95601 "
        "51 b94000 b0ff f3aa 83c710 59 e2f2 "
    )
    if wait_for_key:
        # DS=0; BIOS wait/read, retain the returned word in original RAM,
        # and emit the marker only after the guest consumed a key.
        code += bytes.fromhex("31c0 8ed8 b400 cd16 a30006 b0a1 e680 ebfe")
    else:
        code += bytes.fromhex("b0a0 e680 ebfe")
    disk = bytearray(1228800)
    disk[:len(code)] = code
    disk[510:512] = b"\x55\xaa"
    return bytes(disk)


def make_keyboard_cursor_floppy():
    # An original boot-sector experiment: draw the same canvas, then invert
    # one 8-pixel byte as a cursor. Each BIOS arrow key moves one byte.
    # This is an input/display probe, not a window or operating system.
    code = bytearray.fromhex(
        "fc b81000 cd10 b800a0 8ec0 bf4801 b95601 "
        "51 b94000 b0ff f3aa 83c710 59 e2f2 "
        "31c0 8ed8 bf601f 268035ff"
    )
    labels, branches = {}, []

    def emit(hex_bytes):
        code.extend(bytes.fromhex(hex_bytes))

    def label(name):
        labels[name] = len(code)

    def jump(opcode, destination):
        code.extend((opcode, 0))
        branches.append((len(code) - 1, destination))

    label("poll")
    emit("b400 cd16 80fc4d")  # BIOS key, compare right scan code.
    jump(0x74, "right")
    emit("80fc4b")            # left scan code
    jump(0x75, "poll")
    emit("268035ff 4f")       # restore old byte; DEC DI
    jump(0xeb, "draw")
    label("right")
    emit("268035ff 47")       # restore old byte; INC DI
    label("draw")
    emit("268035ff 893e0006 ff060206 833e020601")
    jump(0x74, "first")
    emit("833e020602")
    jump(0x74, "second")
    jump(0xeb, "poll")
    label("first")
    emit("b0a2 e680")
    jump(0xeb, "poll")
    label("second")
    emit("b0a3 e680")
    jump(0xeb, "poll")
    for offset, destination in branches:
        displacement = labels[destination] - (offset + 1)
        if not -128 <= displacement <= 127:
            raise ValueError("diagnostic branch exceeds short range")
        code[offset] = displacement & 255
    if len(code) > 510:
        raise ValueError("diagnostic exceeds boot sector")
    disk = bytearray(1228800)
    disk[:len(code)] = code
    disk[510:512] = b"\x55\xaa"
    return bytes(disk)


class PC286PairCanvas(unittest.TestCase):
    def test_ega_centered_canvas(self):
        executable = os.environ.get("BOOTHUSH_EXE")
        if not executable:
            self.skipTest("BOOTHUSH_EXE not supplied")
        root = os.environ.get("BOOTHUSH_TEST_TMP")
        with tempfile.TemporaryDirectory(prefix="ega-pair-", dir=root) as folder:
            disk = Path(folder) / "canvas.img"
            frame = Path(folder) / "canvas.ppm"
            original = make_floppy()
            disk.write_bytes(original)
            command = [executable, "--machine", "pc286", "--floppy", str(disk),
                       "--cycles", "8000000", "--timeout-ms", "20000",
                       "--until-post", "0xa0", "--frame", str(frame)]
            flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
            run = subprocess.run(command, capture_output=True, text=True,
                                 timeout=25, creationflags=flags)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            result = json.loads(run.stdout)
            self.assertEqual(result.get("status"), "condition_met")
            self.assertEqual(result.get("post"), 0xa0)
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(),
                             hashlib.sha256(original).digest())
            raw = frame.read_bytes()
            header = b"P6\n640 350\n255\n"
            self.assertTrue(raw.startswith(header), "unexpected PPM format")
            pixels = raw[len(header):]
            self.assertEqual(len(pixels), 640 * 350 * 3)
            # Compare every pixel, including all four margins, to an
            # independently constructed two-color geometric expectation.
            expected = bytearray(len(pixels))
            for y in range(4, 346):
                start = (y * 640 + 64) * 3
                expected[start:start + 512 * 3] = b"\xff" * (512 * 3)
            self.assertEqual(pixels, expected)

    def test_ega_then_keyboard_direction(self):
        executable = os.environ.get("BOOTHUSH_EXE")
        if not executable:
            self.skipTest("BOOTHUSH_EXE not supplied")
        root = os.environ.get("BOOTHUSH_TEST_TMP")
        with tempfile.TemporaryDirectory(prefix="ega-key-", dir=root) as folder:
            disk = Path(folder) / "canvas-key.img"
            original = make_floppy(wait_for_key=True)
            disk.write_bytes(original)
            # Keyboard set-2 make 74h is Right Arrow; expect the BIOS-style
            # extended key word 4D00h. The test checks guest RAM, not just
            # acceptance of an event by the host API.
            command = [executable, "--machine", "pc286", "--floppy", str(disk),
                       "--cycles", "8000000", "--timeout-ms", "20000",
                       "--scancode", "2000000:0x74", "--until-post", "0xa1",
                       "--inspect", "0x600:2"]
            run = subprocess.run(command, capture_output=True, text=True, timeout=25,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            result = json.loads(run.stdout)
            self.assertEqual(result.get("status"), "condition_met")
            self.assertEqual(result.get("post"), 0xa1)
            self.assertEqual(result.get("memory", {}).get("bytes"), "004d")
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), hashlib.sha256(original).digest())

    def test_directional_cursor_restores_pixels(self):
        executable = os.environ.get("BOOTHUSH_EXE")
        if not executable:
            self.skipTest("BOOTHUSH_EXE not supplied")
        root = os.environ.get("BOOTHUSH_TEST_TMP")
        with tempfile.TemporaryDirectory(prefix="ega-cursor-", dir=root) as folder:
            disk = Path(folder) / "cursor.img"
            original = make_keyboard_cursor_floppy()
            disk.write_bytes(original)
            for marker, x, moves, expected_offset in ((0xa2, 264, 1, 0x1f61), (0xa3, 256, 2, 0x1f60)):
                with self.subTest(marker=marker):
                    frame = Path(folder) / f"cursor-{marker:x}.ppm"
                    command = [executable, "--machine", "pc286", "--floppy", str(disk),
                               "--cycles", "8000000", "--timeout-ms", "20000",
                               "--scancode", "2000000:0x74", "--scancode", "2100000:0xf0",
                               "--scancode", "2101000:0x74", "--scancode", "3000000:0x6b",
                               "--until-post", hex(marker), "--inspect", "0x600:4", "--frame", str(frame)]
                    run = subprocess.run(command, capture_output=True, text=True, timeout=25,
                                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                    result = json.loads(run.stdout)
                    self.assertEqual(result.get("status"), "condition_met")
                    self.assertEqual(result.get("post"), marker)
                    actual = bytes.fromhex(result["memory"]["bytes"])
                    self.assertEqual(actual, expected_offset.to_bytes(2, "little") + moves.to_bytes(2, "little"))
                    raw = frame.read_bytes()
                    header = b"P6\n640 350\n255\n"
                    self.assertTrue(raw.startswith(header))
                    expected = bytearray(640 * 350 * 3)
                    for y in range(4, 346):
                        start = (y * 640 + 64) * 3
                        expected[start:start + 512 * 3] = b"\xff" * (512 * 3)
                    start = (100 * 640 + x) * 3
                    expected[start:start + 8 * 3] = bytes(8 * 3)
                    self.assertEqual(raw[len(header):], expected)
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(), hashlib.sha256(original).digest())


if __name__ == "__main__":
    unittest.main()
