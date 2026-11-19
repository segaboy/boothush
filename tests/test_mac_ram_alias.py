"""Guest bus addresses on the original 128 KiB board. All ROMs are authored here."""
import struct
import unittest

from test_boot import BootHushTestCase, ROM_ENTRY, make_rom


RELEASE_OVERLAY = bytes.fromhex("13fc 0000 00ef fffe 13fc 0010 00ef e7fe")


def move_word(source, destination):
    # MOVE.W absolute long to absolute long.
    return struct.pack(">HII", 0x33F9, source, destination)


def write_word(value, destination):
    return struct.pack(">HHI", 0x33FC, value, destination)


class MacRamAliasTests(BootHushTestCase):
    def observe(self, code, address, length):
        self.rom.write_bytes(make_rom(code + bytes.fromhex("60fe")))
        result = self.boot("--until-pc", str(ROM_ENTRY + len(code)),
                           "--inspect", f"{address}:{length}", cycles=20000)
        self.assertEqual(result["status"], "condition_met")
        return bytes.fromhex(result["memory"]["bytes"])

    def test_reads_and_writes_share_one_word_across_all_32_windows(self):
        # Write through each logical window, read through the next, and save
        # each observation separately so independent banks cannot pass.
        code = bytearray(RELEASE_OVERLAY)
        for index in range(32):
            code.extend(write_word(0xA100 + index, 0x1FFF0 + index * 0x20000))
            code.extend(move_word(0x1FFF0 + ((index + 1) % 32) * 0x20000, 0x1000 + index * 2))
        self.assertEqual(self.observe(code, 0x1000, 64),
                         struct.pack(">32H", *range(0xA100, 0xA120)))

    def test_long_access_wraps_between_banks_and_decodes_rom_at_aperture_end(self):
        # Across $03FFFE, the low word of the long lands at physical offset 0.
        # Across $3FFFFE, the second word instead comes from fixed ROM at $400000.
        code = RELEASE_OVERLAY + bytes.fromhex(
            "23fc abcd 1234 0003 fffe "  # MOVE.L #$ABCD1234,$03FFFE
            "23f9 0001 fffe 0000 1000 "  # Read spanning the first physical boundary.
            "23f9 003f fffe 0000 1004 "  # Read spanning the end of the entire aperture.
            "33f9 0000 0000 0000 1008")  # Physical offset 0, independent readback.
        self.assertEqual(self.observe(code, 0x1000, 10), bytes.fromhex("abcd1234 abcd0000 1234"))

    def test_overlay_high_ram_aliases_and_low_rom_priority(self):
        code = bytes.fromhex("23fc 2653 9102 0062 0000 ")  # RAM physical 0.
        code += bytes.fromhex("23f9 0000 0000 0060 1000 ")  # Reset stack from overlay ROM.
        for index in range(4):
            code += write_word(0x2200 + index, 0x61A700 + index * 0x20000)
            code += move_word(0x61A700, 0x601004 + index * 2)
        code += RELEASE_OVERLAY
        code += bytes.fromhex("23f9 0000 0000 0000 100c")  # Now low address 0 is RAM.
        code += move_word(0x7A700, 0x1010)  # Normal-mode screen alias.
        self.assertEqual(self.observe(code, 0x1000, 18),
                         bytes.fromhex("00004000 2200 2201 2202 2203 26539102 2203"))

    def test_normal_map_ends_at_rom_and_does_not_retain_overlay_ram(self):
        for instruction, operation, address in (
                (write_word(0x1234, 0x400000), "write16", 0x400000),
                (move_word(0x600000, 0x1000), "read16", 0x600000)):
            with self.subTest(address=address):
                self.rom.write_bytes(make_rom(RELEASE_OVERLAY + instruction + bytes.fromhex("60fe")))
                result = self.boot(exit_code=1)
                self.assertEqual(result["status"], "machine_fault")
                self.assertEqual(result["fault"], {"operation": operation, "address": address})

    def test_instruction_fetch_keeps_logical_pc(self):
        # Execute MOVEQ #7,D0 and a branch from an alias of physical $1800.
        code = RELEASE_OVERLAY + bytes.fromhex("23fc 7007 60fe 0000 1800 4ef9 0002 1800")
        self.rom.write_bytes(make_rom(code))
        result = self.boot("--until-pc", "0x21802", "--mac-cpu-state", "1")
        self.assertEqual((result["status"], result["pc"], result["cpu"]["d"][0]),
                         ("condition_met", 0x21802, 7))

    def test_published_screen_and_sound_buffer_addresses(self):
        code = RELEASE_OVERLAY + bytes.fromhex(
            "13fc 0080 0007 a700 "  # Set the upper-left main-screen pixel.
            "13fc 009a 0007 fd00 "  # Main sound sample.
            "13fc 002b 0007 fd01")  # Paired disk speed-control byte.
        self.rom.write_bytes(make_rom(code + bytes.fromhex("60fe")))
        frame = self.scratch / "aliased-screen.pbm"
        result = self.boot("--until-pc", str(ROM_ENTRY + len(code)),
                           "--inspect", "0x1fd00:2", "--frame", frame)
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(result["memory"]["bytes"], "9a2b")
        self.assertEqual(self.assert_frame(frame)[0], 0x80)


if __name__ == "__main__":
    unittest.main()
