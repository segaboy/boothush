import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROM_SIZE = 65536
FLOPPY_SIZE = 80 * 2 * 15 * 512


def make_rom(program, *, protected_mode=False):
    image = bytearray(b"\xff" * ROM_SIZE)
    image[-16:-11] = bytes.fromhex("ea 0000 00f0")  # jmp f000:0000
    image[:len(program)] = program
    if protected_mode:
        image[0x40:0x46] = bytes.fromhex("1700 5000 0f00")  # GDT at 0x0f0050
        image[0x50:0x68] = bytes.fromhex(
            "0000000000000000 "
            "ffff00010f9a0000 "  # Selector 8: code at 0x0f0100
            "ffff000000920000"   # Selector 16: writable data at 0
        )
        relocated = bytes.fromhex(
            "b81000 8ed8 "  # mov ax,16; mov ds,ax
            "0f01e0 a30206 "  # smsw ax; mov [0602],ax
            "b05a e680 f4"  # POST 5a and halt
        )
        image[0x100:0x100 + len(relocated)] = relocated
    return image


PROTECTED_PROGRAM = bytes.fromhex(
    "fa 31c0 8ed8 8ed0 bc0080 "  # cli; zero ds and ss; set sp
    "0f01e0 a30006 "  # smsw ax; mov [0600],ax
    "2e0f01164000 "  # lgdt cs:[0040]
    "83c801 0f01f0 "  # or ax,1; lmsw ax
    "ea00000800"  # jmp 0008:0000
)


def make_floppy():
    program = bytearray()
    failures = []
    for instructions, branch_on_failure in (
        ("b80102 bb0006 b90200 31d2 cd13", 0x72),  # read sector 2; jc
        ("b80103 cd13", 0x73),  # write sector 2 must fail; jnc
        ("80fc03", 0x75),  # AH must report write protection; jne
    ):
        program.extend(bytes.fromhex(instructions))
        program.append(branch_on_failure)
        failures.append(len(program))
        program.append(0)

    program.extend(bytes.fromhex(
        "b80300 cd10 b8580e cd10 "  # text mode 3; teletype X
        "b042 e680 fa f4"  # POST 42, then terminal halt
    ))
    for offset in failures:
        program[offset] = len(program) - offset - 1
    program.extend(bytes.fromhex("b0ff e680 fa f4"))  # failure POST

    image = bytearray(FLOPPY_SIZE)
    image[:len(program)] = program
    image[510:512] = b"\x55\xaa"
    image[512:516] = b"BHAT"
    return image


class Pc286Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ["BOOTHUSH_EXE"]).resolve(strict=True)

    def setUp(self):
        scratch = tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP"))
        self.addCleanup(scratch.cleanup)
        self.scratch = Path(scratch.name)

    def boot(self, image, *args, floppy=False, cycles=2000, exit_code=0):
        path = self.scratch / ("disk.img" if floppy else "bios.rom")
        path.write_bytes(image)
        process = subprocess.run(
            [self.exe, "--machine", "pc286", "--floppy" if floppy else "--rom", path,
             "--cycles", str(cycles), "--timeout-ms", "2000", *args],
            capture_output=True,
            encoding="utf-8",
            timeout=7,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        diagnostic = f"stdout={process.stdout!r}, stderr={process.stderr!r}"
        self.assertEqual(process.returncode, exit_code, diagnostic)
        self.assertEqual(len(process.stdout.splitlines()), 1, diagnostic)
        self.assertEqual(process.stderr, "", diagnostic)
        return json.loads(process.stdout)

    def test_boot_drive_with_attached_floppy(self):
        # Both boot sectors save their entry DL and identify themselves in RAM.
        # The fixed disk also reads a floppy sector and attempts a protected write.
        fixed_code = bytes.fromhex(
            "88160005 c6060105ad "  # [0500]=DL, [0501]=fixed guest ID
            "b80102 bb0006 b90200 31d2 cd13 720f "  # read floppy sector 2
            "b80103 cd13 7308 88260205 b06d e680 "  # [0502]=write error AH, POST
            "fa f4")
        fixed = self.scratch / "fixed.img"
        with fixed.open("wb") as stream:
            stream.write(fixed_code)
            stream.seek(510)
            stream.write(b"\x55\xaa")
            stream.truncate(615 * 4 * 17 * 512)
        floppy = bytearray(FLOPPY_SIZE)
        floppy_code = bytes.fromhex("88160005 c6060105fd b06d e680 fa f4")
        floppy[:len(floppy_code)] = floppy_code
        floppy[510:512] = b"\x55\xaa"
        payload = bytes((i * 29 + 1) & 255 for i in range(512))
        floppy[512:1024] = payload
        for drive, signature, expected in ((None, True, "00fd00"), ("floppy", True, "00fd00"),
                                           ("hard-disk", True, "80ad03"), (None, False, "80ad03")):
            with self.subTest(drive=drive, signature=signature):
                floppy[510:512] = b"\x55\xaa" if signature else bytes(2)
                args = ("--boot-drive", drive) if drive else ()
                result = self.boot(floppy, "--hard-disk", fixed, *args, "--until-post", "0x6d",
                                   "--inspect", "0x500:3", "--inspect", "0x600:512", floppy=True)
                self.assertEqual(result["status"], "condition_met")
                self.assertEqual(result["memory_ranges"][0]["bytes"], expected)
                if expected.startswith("80"):
                    self.assertEqual(bytes.fromhex(result["memory_ranges"][1]["bytes"]), payload)
                self.assertEqual((self.scratch / "disk.img").read_bytes(), floppy)

        # A selected unsigned disk must stop even when the other disk is bootable.
        refused = self.boot(floppy, "--hard-disk", fixed, "--boot-drive", "floppy", floppy=True, exit_code=1)
        self.assertIn("missing_boot_signature", str(refused))

    def test_boot_drive_requires_builtin_firmware_and_selected_disk(self):
        for args, floppy in ((("--boot-drive", "hard-disk"), True), (("--boot-drive", "usb"), True),
                             (("--boot-drive", "floppy", "--boot-drive", "hard-disk"), True),
                             (("--boot-drive", "floppy"), False)):
            with self.subTest(args=args, floppy=floppy):
                result = self.boot(make_floppy() if floppy else make_rom(b"\xf4"), *args,
                                   floppy=floppy, exit_code=2)
                self.assertEqual(result["cycles"], 0)

    def test_reset_vector_and_far_jump(self):
        image = make_rom(bytes.fromhex("f4"))
        reset = self.boot(image, "--until-pc", "0xfffff0")
        self.assertEqual((reset["cycles"], reset["cs"], reset["ip"]), (0, 0xf000, 0xfff0))
        jump = self.boot(image, "--until-pc", "0xf0000")
        self.assertEqual(jump["status"], "condition_met")
        self.assertEqual((jump["cs"], jump["ip"]), (0xf000, 0))
        halted = self.boot(image)
        self.assertEqual(halted["status"], "halted")
        self.assertTrue(halted["halted"])
        self.assertEqual(halted["pc"], 0xf0001)

    def test_pc_after_cycle(self):
        # Original near loop writes an incrementing counter to conventional RAM.
        image = make_rom(bytes.fromhex('31c0 8ed8 40 a30006 ebfa'))
        args = ('--until-pc', '0xf0004', '--inspect', '0x600:2')
        first = self.boot(image, *args)
        self.assertEqual(first, self.boot(image, *args, '--until-pc-after', '0'))
        later = self.boot(image, *args, '--until-pc-after', '200')
        self.assertEqual(later['status'], 'condition_met')
        self.assertGreaterEqual(later['cycles'], 200)
        self.assertNotEqual(later['memory']['bytes'], first['memory']['bytes'])
        self.assertEqual(later, self.boot(image, *args, '--until-pc-after', str(later['cycles'])))

    def test_286_protected_mode_and_post_condition(self):
        image = make_rom(PROTECTED_PROGRAM, protected_mode=True)
        reset_msw = self.boot(image, "--until", "0x600:2:0xfff0", "--inspect", "0x600:4")
        self.assertEqual(reset_msw["status"], "condition_met")
        self.assertEqual(reset_msw["memory"]["bytes"], "f0ff0000")
        relocated = self.boot(image, "--until-pc", "0xf0100")
        self.assertEqual((relocated["cs"], relocated["ip"]), (8, 0))
        result = self.boot(image, "--until-post", "0x5a", "--inspect", "0x600:4")
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["post"], 0x5a)
        self.assertEqual(result["memory"]["bytes"], "f0fff1ff")
        self.assertFalse(result["halted"])

    def test_floppy_boot_read_and_write_protection(self):
        image = make_floppy()
        result = self.boot(image, "--until-post", "0x42", "--inspect", "0x600:4", floppy=True)
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["bytes"], b"BHAT".hex())
        self.assertEqual(result["cs"], 0)
        self.assertEqual(hashlib.sha256((self.scratch / "disk.img").read_bytes()).digest(),
                         hashlib.sha256(image).digest())

        result = self.boot(image, "--inspect", "0xb8000:2", floppy=True)
        self.assertEqual(result["status"], "halted")
        self.assertEqual(result["memory"]["bytes"], "5807")

        result = self.boot(image, "--inspect", "0x410:5", floppy=True)
        self.assertEqual(result["memory"]["bytes"], "0100008002")

    def test_selected_floppy_read_failure(self):
        # Two independent BIOS reads. Failure paths save AX and FLAGS before
        # posting distinct codes. The second buffer must never be clobbered.
        program = bytearray.fromhex("31c0 8ec0 bb0006 b80102 b90200 31d2 cd13 72 00")
        first_branch = len(program) - 1
        program += bytes.fromhex("bb0008 b80102 b90100 ba0001 cd13 72 00")
        second_branch = len(program) - 1
        program += bytes.fromhex("b042 e680 fa f4")
        first_failure = len(program)
        program += bytes.fromhex("a30005 9c58 a30205 b0e1 e680 fa f4")
        second_failure = len(program)
        program += bytes.fromhex("a30005 9c58 a30205 b0e2 e680 fa f4")
        program[first_branch] = first_failure - first_branch - 1
        program[second_branch] = second_failure - second_branch - 1
        image = bytearray(FLOPPY_SIZE)
        image[:len(program)] = program
        image[510:512] = b"\x55\xaa"
        image[512:516] = b"BHAT"
        image[15 * 512:15 * 512 + 4] = b"HEAD"
        for selector, post, expected in ((None, 0x42, "4248415448454144"),
                                         ("0:0:2", 0xe1, "0000000000000000"),
                                         ("0:1:1", 0xe2, "4248415400000000"),
                                         ("1:0:2", 0x42, "4248415448454144")):
            args = ["--until-post", hex(post), "--inspect", "0x600:4", "--inspect", "0x800:4", "--inspect", "0x500:4"]
            if selector:
                args += ["--fail-floppy-read", selector]
            result = self.boot(image, *args, floppy=True)
            self.assertEqual(result["status"], "condition_met")
            ranges = result["memory_ranges"]
            self.assertEqual(ranges[0]["bytes"] + ranges[1]["bytes"], expected)
            self.assertEqual(hashlib.sha256((self.scratch / "disk.img").read_bytes()).digest(), hashlib.sha256(image).digest())
            if selector:
                self.assertEqual(result["floppy_read_failure"]["injected"], int(post != 0x42))
            else:
                self.assertNotIn("floppy_read_failure", result)
            if post != 0x42:
                status = bytes.fromhex(ranges[2]["bytes"])
                self.assertEqual(status[:2], b"\x00\x20")  # AL=0, AH=20h
                self.assertTrue(status[2] & 1)  # Carry flag.

        for invalid in ("0:0:0", "80:0:1", "0:2:1", "0:0:16"):
            self.boot(image, "--fail-floppy-read", invalid, floppy=True, exit_code=2)
        self.boot(make_rom(b"\xf4"), "--fail-floppy-read", "0:0:2", exit_code=2)

        retry = bytearray(FLOPPY_SIZE)
        retry_code = bytes.fromhex(
            "31c0 8ec0 bb0006 b80102 b90200 31d2 cd13 "
            "b80102 b90200 31d2 cd13 b044 e680 fa f4")
        retry[:len(retry_code)] = retry_code
        retry[510:512] = b"\x55\xaa"
        result = self.boot(retry, "--until-post", "0x44", "--fail-floppy-read", "0:0:2", floppy=True)
        self.assertEqual(result["floppy_read_failure"]["injected"], 2)

        existing = self.scratch / "existing.ppm"
        existing.write_bytes(b"do not replace")
        self.boot(image, "--until-post", "0xe1", "--fail-floppy-read", "0:0:2",
                  "--frame", existing, floppy=True, exit_code=2)
        self.assertEqual(existing.read_bytes(), b"do not replace")

    def test_a20_gate_via_keyboard_controller(self):
        image = make_rom(bytes.fromhex(
            "31c0 8ed8 c60600055a "  # mov byte [0500],5a
            "b0d1 e664 b001 e660 "  # 8042 output port bit 1 clear
            "b8ffff 8ed8 a01005 "  # read FFFF:0510
            "31db 8edb a20105 "  # save alias byte at [0501]
            "b0d1 e664 b003 e660 "  # 8042 output port bit 1 set
            "b8ffff 8ed8 a01005 "  # read FFFF:0510 again
            "31db 8edb a20205 f4"  # save open bus at [0502]; halt
        ))
        result = self.boot(image, "--inspect", "0x500:3")
        self.assertEqual(result["status"], "halted")
        self.assertEqual(result["memory"]["bytes"], "5a5aff")

    def test_invalid_floppy_signature(self):
        image = make_floppy()
        image[510] = 0
        result = self.boot(image, floppy=True, exit_code=1)
        self.assertEqual(result["status"], "machine_fault")
        self.assertEqual(result["fault"]["operation"], "missing_boot_signature")
        self.assertEqual(result["cycles"], 0)

    def test_floppy_ega_frame(self):
        image = bytearray(FLOPPY_SIZE)
        program = bytes.fromhex(
            "b81000 cd10 "  # INT 10h, set EGA mode 10h
            "b800a0 8ec0 26c606480180 "  # ES=A000h; pixel (64,4) on all planes
            "b044 e680 f4"
        )
        image[:len(program)] = program
        image[510:512] = b"\x55\xaa"
        frame = self.scratch / "ega.ppm"
        result = self.boot(image, "--until-post", "0x44", "--frame", frame, floppy=True)
        self.assertEqual(result["status"], "condition_met")
        magic, geometry, depth, pixels = frame.read_bytes().split(b"\n", 3)
        self.assertEqual((magic, geometry, depth), (b"P6", b"640 350", b"255"))
        first_canvas_pixel = (4 * 640 + 64) * 3
        expected = bytearray(640 * 350 * 3)
        expected[first_canvas_pixel:first_canvas_pixel + 3] = b"\xff\xff\xff"
        self.assertEqual(pixels, expected)

    def test_retrace_wait_is_independent_of_stop_condition(self):
        image = make_rom(bytes.fromhex(
            "bada03 "  # mov dx,03dah
            "ec a808 75fb "  # wait for retrace to end
            "ec a808 74fb "  # wait for retrace to start
            "a20007 b02e e680 f4"
        ))
        post = self.boot(image, "--until-post", "0x2e", "--inspect", "0x700:1", cycles=200000)
        halted = self.boot(image, "--inspect", "0x700:1", cycles=200000)
        self.assertEqual(post["status"], "condition_met")
        self.assertEqual(halted["status"], "halted")
        self.assertTrue(int(post["memory"]["bytes"], 16) & 8)
        self.assertEqual(post["memory"], halted["memory"])
        self.assertEqual(halted["cycles"] - post["cycles"], 2)  # HLT

    def test_cycle_bound(self):
        result = self.boot(make_rom(bytes.fromhex("ebfe")), cycles=100)
        self.assertEqual(result["status"], "cycle_limit")
        self.assertGreaterEqual(result["cycles"], 100)
        self.assertLess(result["cycles"], 107)
        self.assertEqual(result["pc"], 0xf0000)
