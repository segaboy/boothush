"""Original, headless PC/AT EGA centered-canvas diagnostic; not a ClasSICK boot."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
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


def make_keyboard_drag_floppy():
    # Independent keyboard-controlled rectangle diagnostic. A space make
    # toggles a held-drag state; right moves the original frame by 8 pixels
    # only while that state is set. It is not a ClasSICK window or UI.
    code = bytearray.fromhex(
        "fc b81000 cd10 b800a0 8ec0 bf4801 b95601 "
        "51 b94000 b0ff f3aa 83c710 59 e2f2 "
        "31c0 8ed8 bf1019 893e0006"
    )
    labels, short_branches, calls = {}, [], []
    def emit(value): code.extend(bytes.fromhex(value))
    def label(name): labels[name] = len(code)
    def jump(op, name):
        code.extend((op, 0)); short_branches.append((len(code)-1,name))
    def call(name):
        code.extend((0xe8, 0, 0)); calls.append((len(code)-2,name))
    call("frame")
    label("poll")
    emit("b400 cd16 3c20")          # BIOS key; space toggles drag state
    jump(0x74, "toggle")
    emit("80fc4d")                  # right arrow scan
    jump(0x75, "poll")
    emit("803e020601")              # test drag byte
    jump(0x75, "ignored")
    call("frame")
    emit("47")                      # move right eight pixels
    call("frame")
    emit("893e0006 fe060306 b0a5 e680")
    jump(0xeb, "poll")
    label("ignored")
    emit("b0a7 e680")
    jump(0xeb, "poll")
    label("toggle")
    emit("8036020601 803e020601")
    jump(0x74, "pressed")
    emit("b0a6 e680")
    jump(0xeb, "poll")
    label("pressed")
    emit("b0a4 e680")
    jump(0xeb, "poll")
    label("frame")
    emit("57 51 b90800")             # preserve DI,CX; 8 bytes per top row
    label("top")
    emit("268035ff 47")
    jump(0xe2, "top")
    emit("83c748 b91e00")            # advance 72 bytes, then 30 middle rows
    label("middle")
    emit("268035ff 26807507ff 83c750")
    jump(0xe2, "middle")
    emit("b90800")
    label("bottom")
    emit("268035ff 47")
    jump(0xe2, "bottom")
    emit("59 5f c3")
    for offset, destination in short_branches:
        displacement = labels[destination] - (offset + 1)
        if not -128 <= displacement <= 127:
            raise ValueError("diagnostic branch exceeds short range")
        code[offset] = displacement & 255
    for offset, destination in calls:
        displacement = labels[destination] - (offset + 2)
        code[offset:offset+2] = displacement.to_bytes(2, "little", signed=True)
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

    def test_keyboard_drag_press_move_release(self):
        executable = os.environ.get("BOOTHUSH_EXE")
        if not executable:
            self.skipTest("BOOTHUSH_EXE not supplied")
        root = os.environ.get("BOOTHUSH_TEST_TMP")
        with tempfile.TemporaryDirectory(prefix="ega-drag-", dir=root) as folder:
            disk = Path(folder) / "drag.img"
            original = make_keyboard_drag_floppy()
            disk.write_bytes(original)
            keys = [(2000000,0x29),(2100000,0xf0),(2101000,0x29),
                    (3000000,0x74),(3100000,0xf0),(3101000,0x74),
                    (4000000,0x29),(4100000,0xf0),(4101000,0x29),(5000000,0x74)]
            for marker, x, state, moves in ((0xa4,128,1,0),(0xa5,136,1,1),
                                             (0xa6,136,0,1),(0xa7,136,0,1)):
                with self.subTest(marker=marker):
                    frame = Path(folder) / f"drag-{marker:x}.ppm"
                    command = [executable, "--machine", "pc286", "--floppy", str(disk),
                               "--cycles", "8000000", "--timeout-ms", "20000"]
                    for cycle, scan in keys: command += ["--scancode",f"{cycle}:{scan}"]
                    command += ["--until-post",hex(marker),"--inspect","0x600:4","--frame",str(frame)]
                    run = subprocess.run(command,capture_output=True,text=True,timeout=25,
                                         creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                    result=json.loads(run.stdout)
                    self.assertEqual(result.get("status"),"condition_met")
                    self.assertEqual(result.get("post"),marker)
                    address = 80*80+x//8
                    self.assertEqual(bytes.fromhex(result["memory"]["bytes"]),
                                     address.to_bytes(2,"little")+bytes((state,moves)))
                    raw=frame.read_bytes(); header=b"P6\n640 350\n255\n"
                    self.assertTrue(raw.startswith(header))
                    expected=bytearray(640*350*3)
                    for y in range(4,346):
                        offset=(y*640+64)*3; expected[offset:offset+512*3]=b"\xff"*(512*3)
                    for y in range(80,112):
                        for column in range(64):
                            if y in (80,111) or column<8 or column>=56:
                                offset=(y*640+x+column)*3; expected[offset:offset+3]=bytes(3)
                    self.assertEqual(raw[len(header):],expected)
            self.assertEqual(hashlib.sha256(disk.read_bytes()).digest(),hashlib.sha256(original).digest())

    def test_full_mac_sized_bitmap_to_ega(self):
        executable = os.environ.get("BOOTHUSH_EXE")
        if not executable:
            self.skipTest("BOOTHUSH_EXE not supplied")
        root = os.environ.get("BOOTHUSH_TEST_TMP")
        with tempfile.TemporaryDirectory(prefix="ega-bitmap-", dir=root) as folder:
            source = Path(folder)/"original.pbm"
            # Independent original bitmap with varying rows, byte boundaries,
            # corners and non-repeating black/white pixels.
            bitmap = bytes(((x*37 + y*71) ^ (x*y) ^ (y >> 1)) & 255
                           for y in range(342) for x in range(64))
            source.write_bytes(b"P4\n512 342\n"+bitmap)
            script = Path(__file__).resolve().parents[1]/"scripts/compare_mac_pc286.py"
            output = Path(folder)/"capture"
            run = subprocess.run([sys.executable,"-B",str(script),"--boothush",executable,
                                  "--pbm",str(source),"--output",str(output)],
                                 capture_output=True,text=True,timeout=30,
                                 creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            report=json.loads(run.stdout)
            self.assertEqual(report["status"],"pass")
            self.assertEqual(report["canvas_pixels_compared"],512*342)
            self.assertEqual(report["margin_pixels_compared"],640*350-512*342)
            self.assertTrue(report["input_floppy_unchanged"])
            invalid = Path(folder)/"invalid.pbm"
            invalid.write_bytes(b"P4\n512 342\n\0")
            rejected = Path(folder)/"rejected"
            bad = subprocess.run([sys.executable,"-B",str(script),"--boothush",executable,
                                  "--pbm",str(invalid),"--output",str(rejected)],
                                 capture_output=True,text=True,timeout=30,
                                 creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
            self.assertNotEqual(bad.returncode,0)
            self.assertFalse(rejected.exists())


if __name__ == "__main__":
    unittest.main()
