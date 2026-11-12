import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_boot import make_rom


VIA_PORT_A = 0xEFFFFE
VIA_DIRECTION_A = 0xEFE7FE
VIA_SHIFT = 0xEFF5FE
VIA_ACR = 0xEFF7FE
VIA_IFR = 0xEFFBFE
VIA_IER = 0xEFFDFE
LEVEL_ONE_VECTOR = 0x64
RESULT = 0xB00
HANDLER_OFFSET = 0x3A0


def move_byte(value, destination):
    return struct.pack(">HHI", 0x13FC, value, destination)


def read_byte(source):
    return struct.pack(">HI", 0x1039, source)


def store_byte(destination):
    return struct.pack(">HI", 0x13C0, destination)


def exchange(command, destination):
    stop = bytes.fromhex("4e72 2000")
    return (
        move_byte(0x1C, VIA_ACR)  # Shift out using the external keyboard clock.
        + move_byte(command, VIA_SHIFT)
        + stop
        + move_byte(0x0C, VIA_ACR)  # Shift in using the external keyboard clock.
        + stop
        + read_byte(VIA_SHIFT)
        + store_byte(destination)
    )


def make_keyboard_rom():
    program = (
        move_byte(0, VIA_PORT_A)
        + move_byte(0x10, VIA_DIRECTION_A)  # Disable the ROM overlay.
        + struct.pack(">HII", 0x23FC, 0x400000 + HANDLER_OFFSET, LEVEL_ONE_VECTOR)
        + move_byte(0x84, VIA_IER)  # Enable only the shift register interrupt.
        + exchange(0x16, RESULT)
        + exchange(0x36, RESULT + 1)
        + exchange(0x10, RESULT + 2)
        + exchange(0x14, RESULT + 3)
        + bytes.fromhex("60fe")
    )
    handler = (
        read_byte(VIA_IFR)
        + bytes.fromhex("0200 0084")  # Isolate IRQ and shift completion flags.
        + store_byte(RESULT + 5)
        + read_byte(VIA_SHIFT)  # Reading SR must clear its pending interrupt.
        + read_byte(VIA_IFR)
        + bytes.fromhex("0200 0084")
        + store_byte(RESULT + 6)
        + struct.pack(">HI", 0x5239, RESULT + 4)  # Count each interrupt.
        + bytes.fromhex("4e73")  # RTE.
    )
    image = make_rom(program)
    image[HANDLER_OFFSET : HANDLER_OFFSET + len(handler)] = handler
    return image


class MacKeyboardIntegration(unittest.TestCase):
    def test_via_irq_and_scheduled_key_transitions(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("BOOTHUSH_TEST_TMP")) as temp:
            rom = Path(temp) / "keyboard.rom"
            rom.write_bytes(make_keyboard_rom())
            result = subprocess.run(
                [
                    os.environ["BOOTHUSH_EXE"], "--machine", "mac128k", "--rom", rom,
                    "--cycles", "500000", "--timeout-ms", "5000",
                    "--key", "180000:0x24:1", "--key", "180001:0x24:0",
                    "--until", "0xb03:1:0xc9", "--inspect", "0xb00:7",
                ],
                capture_output=True,
                encoding="utf-8",
                timeout=10,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        report = json.loads(result.stdout)
        self.assertEqual(report["status"], "condition_met")
        self.assertEqual(
            report["memory"],
            {"address": RESULT, "bytes": "037d49c9088400"},
        )


if __name__ == "__main__":
    unittest.main()
