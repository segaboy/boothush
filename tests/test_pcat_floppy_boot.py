import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_pc286 import FLOPPY_SIZE, make_rom


def make_diskette_rom():
    program = bytes.fromhex(
        "fa 31c0 8ed8 8ec0 8ed0 bc007c "  # cli; DS=ES=SS=0; SP=7C00h
        "c70638000001 c7063a0000f0 "  # IRQ6 vector points to F000:0100
        "b011 e620 b008 e621 b004 e621 b001 e621 b0bf e621 "  # master PIC, only IRQ6 enabled
        "baf203 b01c ee fb "  # release FDC reset, select and power A:, enable IRQs
        "803e000501 72f9 "  # wait for one IRQ6
        "baf503"  # DX = FDC FIFO
    )
    program += bytes.fromhex("b008 ee ec ec") * 4  # sense all four reset interrupts
    program += bytes.fromhex(
        "b0c0 e6d6 b000 e6d4 "  # enable DMA cascade on the secondary controller
        "b006 e60a b000 e60c e604 b07c e604 "  # mask and set DMA channel 2 address
        "b0ff e605 b001 e605 b000 e681 "  # 512 bytes, page zero
        "b046 e60b b002 e60a "  # device-to-memory single mode, unmask channel 2
        "baf503 b0e6 ee b000 ee ee ee b001 ee b002 ee b00f ee b01b ee b0ff ee "
        "803e000502 72f9 "  # wait for the read-data IRQ6
        "ec 84c0 750c ec ec ec ec ec ec "  # check ST0 and drain the result packet
        "fa ea007c0000 "  # jump to the loaded boot sector
        "fa b0ff e680 f4"
    )
    image = make_rom(program)
    handler = bytes.fromhex("50 fe060005 b020 e620 58 cf")  # count IRQ6, EOI, IRET
    image[0x100:0x100 + len(handler)] = handler
    return image


class PcatFloppyBootTests(unittest.TestCase):
    def test_external_rom_boots_through_fdc_dma_and_pic(self):
        exe = Path(os.environ["BOOTHUSH_EXE"]).resolve(strict=True)
        with tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP")) as scratch:
            rom = Path(scratch) / "diskette-test.rom"
            rom.write_bytes(make_diskette_rom())
            media = bytearray(FLOPPY_SIZE)
            boot = bytes.fromhex("b807b0 a30105 b046 e680 f4")
            media[:len(boot)] = boot
            media[510:512] = b"\x55\xaa"
            floppy = Path(scratch) / "diskette-test.img"
            floppy.write_bytes(media)
            process = subprocess.run(
                [exe, "--machine", "pc286", "--rom", rom, "--floppy", floppy,
                 "--cycles", "100000", "--timeout-ms", "2000", "--until-post", "0x46",
                 "--inspect", "0x500:3"],
                capture_output=True,
                encoding="utf-8",
                timeout=7,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            diagnostic = f"stdout={process.stdout!r}, stderr={process.stderr!r}"
            self.assertEqual(process.returncode, 0, diagnostic)
            result = json.loads(process.stdout)
            self.assertEqual(result["status"], "condition_met", diagnostic)
            self.assertEqual(result["cs"], 0)
            self.assertEqual(result["memory"]["bytes"], "0207b0")
            self.assertEqual(floppy.read_bytes(), media)
