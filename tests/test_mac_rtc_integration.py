import struct

from test_boot import BootHushTestCase, make_rom


ORB = 0xEFE1FE
DDRB = 0xEFE5FE
SAMPLES = 0xB00


def move_byte(value, address):
    return struct.pack(">HHI", 0x13FC, value, address)


def read_byte(source, address):
    return struct.pack(">HII", 0x13F9, source, address)


def serial_byte(value):
    program = bytearray()
    for bit in range(7, -1, -1):
        data = (value >> bit) & 1
        program.extend(move_byte(0, ORB))
        program.extend(move_byte(data, ORB))
        program.extend(move_byte(data | 2, ORB))
    return program


class MacRtcIntegrationTests(BootHushTestCase):
    def test_serial_pram_uses_via_port_and_direction(self):
        program = bytearray.fromhex("13fc 0000 00ef fffe 13fc 0010 00ef e7fe")
        program.extend(move_byte(6, ORB))  # Deselect and leave PB1 high.
        program.extend(move_byte(7, DDRB))
        program.extend(move_byte(2, ORB))
        program.extend(serial_byte(0x21))  # Write PRAM address 16.
        program.extend(serial_byte(0x9A))
        program.extend(move_byte(6, ORB))
        program.extend(move_byte(2, ORB))
        program.extend(serial_byte(0xA1))  # Read the same address.
        program.extend(move_byte(6, DDRB))  # Make PB0 an input.
        for bit in range(8):
            program.extend(move_byte(0, ORB))
            program.extend(read_byte(ORB, SAMPLES + bit))
            program.extend(move_byte(2, ORB))
        program.extend(move_byte(6, ORB))
        program.extend(move_byte(0xA5, SAMPLES + 8))
        program.extend(bytes.fromhex("60fe"))

        self.rom.write_bytes(make_rom(program))
        result = self.boot("--until", "0xb08:1:0xa5", "--inspect", "0xb00:8", cycles=20_000)
        self.assertEqual(result["status"], "condition_met")
        samples = bytes.fromhex(result["memory"]["bytes"])
        self.assertEqual([sample & 1 for sample in samples], [1, 0, 0, 1, 1, 0, 1, 0])

    def test_ca2_pending_and_acknowledgement(self):
        program = bytes.fromhex(
            "13fc 0000 00ef fffe 13fc 0010 00ef e7fe "  # Clear overlay.
            "13fc 0081 00ef fdfe "  # Enable CA2 interrupt, CPU still masked.
            "0839 0000 00ef fbfe 67f6 "  # Wait for first RTC second.
            "13f9 00ef fbfe 0000 0b00 "  # Save pending IFR.
            "13f9 00ef e3fe 0000 0b06 "  # Read ORA to acknowledge CA2.
            "13f9 00ef fbfe 0000 0b01 "  # Save cleared IFR.
            "13fc 0002 00ef f9fe "  # PCR: independent CA2 input.
            "0839 0000 00ef fbfe 67f6 "  # Wait for the next RTC second.
            "13f9 00ef fbfe 0000 0b02 "  # Save pending IFR.
            "13f9 00ef e3fe 0000 0b06 "  # ORA read leaves independent CA2 pending.
            "13f9 00ef fbfe 0000 0b03 "
            "13fc 0001 00ef fbfe "  # Write IFR bit 0 to clear it.
            "13f9 00ef fbfe 0000 0b04 "
            "13fc 00a5 0000 0b05 60fe"
        )
        self.rom.write_bytes(make_rom(program))
        result = self.boot(
            "--until", "0xb05:1:0xa5", "--inspect", "0xb00:5",
            cycles=16_000_000, timeout_ms=10_000,
        )
        self.assertEqual(result["status"], "condition_met")
        self.assertGreaterEqual(result["cycles"], 2 * 7_833_600)
        samples = bytes.fromhex(result["memory"]["bytes"])
        self.assertEqual([sample & 0x81 for sample in samples], [0x81, 0, 0x81, 0x81, 0])
