import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import test_pc286 as fixtures


HARD_DISK_SIZE = 615 * 4 * 17 * 512


def checked_program(stages, success_post):
    program = bytearray()
    branches = []
    for stage, branch in stages:
        program.extend(bytes.fromhex(stage))
        if branch is not None:
            program.extend((branch, 0))
            branches.append(len(program) - 1)
    program.extend((0xb0, success_post, 0xe6, 0x80, 0xf4))
    for offset in branches:
        program[offset] = len(program) - offset - 1
    program.extend(bytes.fromhex("b0ff e680 f4"))
    return program


def boot_image(program, size):
    image = bytearray(size)
    image[:len(program)] = program
    image[510:512] = b"\x55\xaa"
    return image


class PcAtIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = Path(os.environ["BOOTHUSH_EXE"]).resolve(strict=True)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP"))
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def write(self, name, contents):
        path = self.directory / name
        path.write_bytes(contents)
        return path

    def run_guest(self, *arguments, cycles=100000, exit_code=0):
        process = subprocess.run(
            [self.exe, "--machine", "pc286", "--cycles", str(cycles),
             "--timeout-ms", "2000", *arguments],
            capture_output=True, encoding="utf-8", timeout=7,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(process.returncode, exit_code,
                         f"stdout={process.stdout!r}, stderr={process.stderr!r}")
        self.assertEqual(process.stderr, "")
        self.assertEqual(len(process.stdout.splitlines()), 1)
        return json.loads(process.stdout)

    def test_disk_boot_chs_writes_and_snapshot(self):
        program = checked_program((
            ("80fa80", 0x75),  # cmp dl,80h; BIOS boot drive
            ("b80103 bb007d b90200 ba8000 cd13", 0x72),  # write C: sector 2
            ("b80102 bb0006 cd13", 0x72),  # read it back to 0600h
        ), 0x41)
        image = boot_image(program, HARD_DISK_SIZE)
        image[0x100:0x104] = b"BHAT"
        disk = self.write("c.img", image)
        snapshot = self.directory / "disk-after.img"
        result = self.run_guest("--hard-disk", disk, "--disk-out", snapshot,
                                "--until-post", "0x41", "--inspect", "0x600:4")
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["bytes"], b"BHAT".hex())
        with disk.open("rb") as source, snapshot.open("rb") as output:
            source.seek(512)
            output.seek(512)
            self.assertEqual(source.read(4), bytes(4))
            self.assertEqual(output.read(4), b"BHAT")
        self.assertEqual(snapshot.stat().st_size, HARD_DISK_SIZE)
        snapshot.write_bytes(b"existing snapshot")
        collision = self.run_guest("--hard-disk", disk, "--disk-out", snapshot,
                                   "--until-post", "0x41", exit_code=2)
        self.assertEqual(collision["status"], "error")
        self.assertEqual(snapshot.read_bytes(), b"existing snapshot")

    def test_install_floppy_can_write_disk_and_disk_can_boot_after_ejection(self):
        disk_stage = bytes.fromhex("b80102 bb0006 b90200 ba8000 cd13 b043 e680 f4")
        installer = checked_program((
            ("80fa00", 0x75),  # Boot order selects A: with both media inserted.
            ("b80103 bb007d b90200 ba8000 cd13", 0x72),
        ), 0x42)
        image = boot_image(installer, fixtures.FLOPPY_SIZE)
        image[0x100:0x104] = b"INST"
        floppy = self.write("a.img", image)
        disk = self.write("c.img", boot_image(disk_stage, HARD_DISK_SIZE))
        snapshot = self.directory / "installed.img"
        result = self.run_guest("--floppy", floppy, "--hard-disk", disk,
                                "--disk-out", snapshot, "--until-post", "0x42")
        self.assertEqual(result["status"], "condition_met")
        boot = self.run_guest("--hard-disk", snapshot, "--until-post", "0x43",
                              "--inspect", "0x600:4")
        self.assertEqual(boot["memory"]["bytes"], b"INST".hex())

    def test_external_rom_hard_disk_pio_word_transfer(self):
        read = "baf201 b001 ee 42 ee 42 30c0 ee 42 ee 42 b0a0 ee 42 b020 ee ec a808"
        write = "baf201 b001 ee 42 b002 ee 42 30c0 ee 42 ee 42 b0a0 ee 42 b030 ee ec a808"
        program = checked_program((
            ("fa 31c0 8ed8 8ec0 fc " + read, 0x74),
            ("baf001 bf0006 b90001 f36d " + write, 0x74),
            ("baf001 be0006 b90001 f36f", None),
        ), 0x44)
        rom = self.write("bios.rom", fixtures.make_rom(program))
        image = bytearray(HARD_DISK_SIZE)
        image[:4] = b"PIO!"
        disk = self.write("c.img", image)
        snapshot = self.directory / "disk-after.img"
        result = self.run_guest("--rom", rom, "--hard-disk", disk, "--disk-out", snapshot,
                                "--until-post", "0x44", "--inspect", "0x600:4")
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["bytes"], b"PIO!".hex())
        with snapshot.open("rb") as output:
            output.seek(512)
            self.assertEqual(output.read(4), b"PIO!")

    def test_pit_irq_wakes_hlt(self):
        program = bytes.fromhex(
            "fa 31c0 8ed8 8ed0 bc0080 "  # Segment and stack setup
            "c70620008000 c706220000f0 "  # IRQ0 handler at F000:0080
            "b011 e620 b008 e621 b004 e621 b001 e621 b0fe e621 "  # Initialize PIC
            "b034 e643 b080 e640 30c0 e640 "  # PIT0 mode 2, count 128
            "fb f4 fa b033 e680 f4"  # STI; HLT; then publish and halt.
        )
        image = fixtures.make_rom(program)
        handler = bytes.fromhex("fe060006 b020 e620 cf")
        image[0x80:0x80 + len(handler)] = handler
        rom = self.write("bios.rom", image)
        result = self.run_guest("--rom", rom, "--until-post", "0x33", "--inspect", "0x600:1")
        self.assertEqual(result["memory"]["bytes"], "01")
        self.assertGreater(result["cycles"], 600)

    def test_keyboard_irq_wakes_hlt_at_scheduled_cycle(self):
        program = bytes.fromhex(
            "fa 31c0 8ed8 8ed0 bc0080 "
            "c70624008000 c706260000f0 "  # IRQ1 handler at F000:0080
            "b011 e620 b008 e621 b004 e621 b001 e621 b0fd e621 "
            "b060 e664 b041 e660 "  # 8042 translation and IRQ1 enabled
            "fb f4 fa b034 e680 f4"
        )
        image = fixtures.make_rom(program)
        handler = bytes.fromhex("e460 a20006 b020 e620 cf")
        image[0x80:0x80 + len(handler)] = handler
        rom = self.write("bios.rom", image)
        result = self.run_guest("--rom", rom, "--scancode", "5000:0x1c",
                                "--until-post", "0x34", "--inspect", "0x600:1")
        self.assertEqual(result["memory"]["bytes"], "1e")
        self.assertGreater(result["cycles"], 5000)

    def test_minimal_bios_keyboard_poll_and_wait(self):
        program = checked_program((
            ("b80001 cd16", 0x75),  # INT 16h poll must initially set ZF.
            ("31c0 cd16 a30006", None),  # Blocking read; store returned AX.
        ), 0x45)
        floppy = self.write("a.img", boot_image(program, fixtures.FLOPPY_SIZE))
        result = self.run_guest("--floppy", floppy, "--scancode", "2000:0x12",
                                "--scancode", "2500:0x1c", "--until-post", "0x45",
                                "--inspect", "0x600:2")
        self.assertEqual(result["memory"]["bytes"], "411e")  # 'A' plus BIOS scan code
        self.assertGreater(result["cycles"], 2500)

    def test_minimal_bios_timer_wakes_hlt_and_reports_one_tick(self):
        program = bytes.fromhex(
            "fb f4 b80000 cd1a 89160006 "  # Wait for BIOS IRQ0, then store tick count.
            "b80004 cd1a 890e0206 89160406 "  # Store BIOS RTC date.
            "b046 e680 fa f4"
        )
        floppy = self.write("a.img", boot_image(program, fixtures.FLOPPY_SIZE))
        result = self.run_guest("--floppy", floppy, "--until-post", "0x46",
                                "--inspect", "0x600:6", cycles=400000)
        self.assertEqual(result["memory"]["bytes"], "010084190101")

    def test_supplied_ega_option_rom_is_mapped_and_checked(self):
        rom = self.write("bios.rom", fixtures.make_rom(bytes.fromhex(
            "31c0 8ed0 bc0080 9a030000c0 f4"  # call far C000:0003
        )))
        option = bytearray(512)
        option[:8] = bytes.fromhex("55aa01 b067 e680 cb")
        option[-1] = -sum(option) & 0xff
        ega_rom = self.write("ega.rom", option)
        result = self.run_guest("--rom", rom, "--ega-rom", ega_rom, "--until-post", "0x67")
        self.assertEqual(result["status"], "condition_met")
        option[-1] ^= 1
        ega_rom.write_bytes(option)
        result = self.run_guest("--rom", rom, "--ega-rom", ega_rom, exit_code=2)
        self.assertEqual(result["status"], "error")
