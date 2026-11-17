import hashlib
import struct
import subprocess

from test_boot import BootHushTestCase, ROM_ENTRY, make_rom


FLOPPY_SIZE = 409600
SECTOR_SIZE = 512
GCR = bytes.fromhex(
    "96 97 9a 9b 9d 9e 9f a6 a7 ab ac ad ae af b2 b3 "
    "b4 b5 b6 b7 b9 ba bb bc bd be bf cb cd ce cf d3 "
    "d6 d7 d9 da db dc dd de df e5 e6 e7 e9 ea eb ec "
    "ed ee ef f2 f3 f4 f5 f6 f7 f9 fa fb fc fd fe ff"
)


def make_floppy_rom(steps=0, attempt_write=False):
    """Assemble a small MC68000 boot ROM that reads and checks one real GCR sector."""
    code = bytearray()
    labels = {}
    relative = []
    absolute = []

    def emit(words):
        code.extend(bytes.fromhex(words))

    def label(name):
        labels[name] = len(code)

    def branch(opcode, target):
        emit(opcode)
        relative.append((len(code), target))
        emit("0000")

    def iwm_switch(reg):
        # TST.B abs.l. Either a read or write toggles the IWM soft switch.
        emit(f"4a39 00c0 {reg * 0x200:04x}")

    def prologue(name, last):
        label(name)
        for byte in (0xD5, 0xAA, last):
            branch("6100", "read")
            emit(f"0c00 {byte:04x}")  # CMPI.B #byte,D0
            branch("6600", name)

    def compare_byte(byte, target):
        branch("6100", "read")
        emit(f"0c00 {byte:04x}")
        branch("6600", target)

    emit("13fc 0000 00ef fffe 13fc 0030 00ef e7fe")  # Clear overlay and drive SEL low.
    iwm_switch(9)  # Enable IWM and select the internal drive.
    iwm_switch(3)  # CA1 high selects motor-on command.
    iwm_switch(7)  # Raise LSTRB to latch the command.
    iwm_switch(6)  # Lower LSTRB.
    if steps:
        iwm_switch(2)
        iwm_switch(1)  # CA0 high selects step; initial direction is inward.
        for _ in range(steps):
            iwm_switch(7)
            iwm_switch(6)
    if attempt_write:
        iwm_switch(13)  # Q6 high.
        emit("13fc 00ff 00c0 1e00")  # Q7 high and a guest write to the data register.
        iwm_switch(14)
    emit("41f9 00c0 1800")  # LEA Q6-low/data address,A0.
    emit("43f9")  # LEA inverse GCR table,A1.
    absolute.append((len(code), "gcr_table"))
    emit("0000 0000 45f9 0000 1000")  # LEA $1000,A2.
    emit("7a00 7c00 7e00")  # Clear D5-D7, the three rolling check bytes.

    prologue("address_mark", 0x96)
    compare_byte(GCR[steps & 63], "address_mark")  # Track low bits.
    compare_byte(GCR[0], "address_mark")  # Sector 0 on the selected track.
    compare_byte(GCR[steps >> 6], "fault")  # Track high bit, side 0.
    compare_byte(GCR[2], "fault")  # Single-sided format.
    compare_byte(GCR[(steps & 63) ^ (steps >> 6) ^ 2], "fault")
    prologue("data_mark", 0xAD)
    compare_byte(GCR[0], "fault")  # Data field's duplicate sector number.

    # Buffer the 699 data nibbles, four checksum nibbles, and two epilogue bytes
    # at media speed. The decoded payload and the nibble buffer are disjoint.
    emit("49f9 0000 2000 323c 02c0")  # LEA $2000,A4; MOVE.W #704,D1.
    label("capture")
    branch("6100", "read")
    emit("18c0")  # MOVE.B D0,(A4)+
    branch("51c9", "capture")  # DBRA D1,capture
    emit("49f9 0000 2000")

    for _ in range(174):
        branch("6100", "decode")  # Each complete group has three bytes.
    branch("6100", "decode_last")  # Final group contains two bytes.
    branch("6100", "unpack_two")
    branch("6100", "unpack_third")  # Encoded checksum triplet.
    for compare in ("b405", "b606", "b807"):  # CMP.B D5,D2; D6,D3; D7,D4
        emit(compare)
        branch("6600", "fault")
    for byte in (0xDE, 0xAA):
        emit(f"101c 0c00 {byte:04x}")  # MOVE.B (A4)+,D0; CMPI.B #byte,D0
        branch("6600", "fault")
    emit("4ef9 0000 100c")  # JMP to the executable loaded after the tags.

    label("decode")
    branch("6100", "unpack_two")
    branch("6100", "unpack_third")
    branch("6100", "store_two")
    emit("bd04 df04 14c4 4e75")  # EOR D6,D4; ADDX D4,D7; MOVE D4,(A2)+; RTS
    label("decode_last")
    branch("6100", "unpack_two")
    label("store_two")
    # Rotate C, set X from its low bit, then decode A and B with carry.
    emit("e31f 1007 e208 bf02 db02 bb03 dd03 14c2 14c3 4e75")

    label("unpack_two")
    branch("6100", "gcr")
    emit("1200")  # D1 holds the packed high bits for the group.
    branch("6100", "gcr")
    emit("1401 0202 0030 e50a 8400")  # Put the first byte in D2.
    branch("6100", "gcr")
    emit("1601 0203 000c e90b 8600 4e75")  # Put the second byte in D3.
    label("unpack_third")
    branch("6100", "gcr")
    emit("1801 0204 0003 ed0c 8800 4e75")  # Put the third byte in D4.

    label("gcr")
    emit("7000 101c 1031 0000 4e75")  # Index the inverse table with the next buffered nibble.
    label("read")
    emit("7000 1010")  # MOVEQ #0,D0; MOVE.B (A0),D0
    branch("6a00", "read")  # Poll until the data latch contains a complete nibble.
    emit("4e75")
    label("fault")
    emit("13fc 00ee 0000 0b00 60fe")  # Error marker, then spin.

    label("gcr_table")
    code.extend(bytes(GCR.index(byte) if byte in GCR else 0xFF for byte in range(256)))
    for position, target in relative:
        struct.pack_into(">h", code, position, labels[target] - position)
    for position, target in absolute:
        struct.pack_into(">I", code, position, ROM_ENTRY + labels[target])
    return make_rom(code)


def sector_payload():
    program = bytes.fromhex("13fc 00a5 0000 0b00 60fe")  # MOVE.B #$A5,$B00; BRA.S *
    return program + bytes((i * 89 + 13) & 255 for i in range(SECTOR_SIZE - len(program)))


def disk_image(track):
    image = bytearray(FLOPPY_SIZE)
    first = sum(12 - cylinder // 16 for cylinder in range(track)) * SECTOR_SIZE
    image[first : first + SECTOR_SIZE] = sector_payload()
    return image


def disk_copy_image(sectors, tags):
    def checksum(data):
        value = 0
        for (word,) in struct.iter_unpack(">H", data):
            value = (value + word) & 0xFFFFFFFF
            value = (value >> 1) | ((value & 1) << 31)
        return value

    header = bytearray(84)
    header[:9] = b"\x08BootHush"
    struct.pack_into(
        ">4I4B", header, 64, len(sectors), len(tags), checksum(sectors),
        checksum(tags[12:]), 0, 2, 1, 0,
    )
    return header + sectors + tags


class MacintoshFloppyTests(BootHushTestCase):
    def test_raw_floppy_boots_after_seek_to_different_speed_zone(self):
        image = self.scratch / "raw.dsk"
        self.rom.write_bytes(make_floppy_rom(64, attempt_write=True))
        image.write_bytes(disk_image(64))
        expected_hash = hashlib.sha256(image.read_bytes()).digest()
        args = ("--floppy", image, "--until", "0xb00:1:0xa5", "--inspect", "0x100c:512",
                "--mac-cpu-state", "1")
        plain_frame, observed_frame = self.scratch / "plain.pbm", self.scratch / "observed.pbm"
        plain = self.boot(*args, "--frame", plain_frame, cycles=4_000_000)
        result = self.boot(*args, "--frame", observed_frame, "--mac-floppy-state", "1", cycles=4_000_000)
        floppy = result.pop("mac_floppy")
        self.assertEqual(floppy["observation"], "modeled_iwm")
        self.assertTrue(floppy["media_present"])
        self.assertTrue(floppy["write_protected"])
        self.assertTrue(floppy["controller_enabled"])
        self.assertTrue(floppy["motor_on"])
        self.assertFalse(floppy["external_selected"])
        self.assertFalse(floppy["select_line"])
        self.assertEqual(floppy["track"], 64)
        self.assertEqual(floppy["step_commands"], 64)
        self.assertEqual(floppy["ignored_data_writes"], 1)
        self.assertEqual(floppy["eject_commands"], 0)
        self.assertGreaterEqual(floppy["nibbles_delivered"], 705)
        self.assertGreater(floppy["not_ready_data_reads"], 0)
        self.assertEqual(result, plain)  # Includes cycles, PC, all registers and loaded bytes.
        self.assertEqual(plain_frame.read_bytes(), observed_frame.read_bytes())
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(bytes.fromhex(result["memory"]["bytes"]), sector_payload())
        self.assertEqual(hashlib.sha256(image.read_bytes()).digest(), expected_hash)

    def test_disk_copy_tags_are_transferred_and_checksummed(self):
        image = self.scratch / "disk-copy.image"
        self.rom.write_bytes(make_floppy_rom())
        tag = bytes(range(12))
        tags = tag + bytes(reversed(tag)) + bytes(12 * 798)
        image.write_bytes(disk_copy_image(disk_image(0), tags))
        result = self.boot(
            "--floppy", image, "--until", "0xb00:1:0xa5", "--inspect", "0x1000:524",
            "--mac-floppy-state", "1", cycles=4_000_000,
        )
        self.assertEqual(result["status"], "condition_met")
        self.assertEqual(bytes.fromhex(result["memory"]["bytes"]), tag + sector_payload())
        self.assertEqual(result["mac_floppy"]["track"], 0)
        self.assertEqual(result["mac_floppy"]["step_commands"], 0)
        self.assertEqual(result["mac_floppy"]["ignored_data_writes"], 0)

    def test_disk_copy_checksum_mismatch_is_rejected(self):
        image = self.scratch / "damaged.image"
        payload = disk_copy_image(disk_image(0), b"")
        payload[84] ^= 1
        image.write_bytes(payload)
        result = self.boot("--floppy", image, exit_code=2)
        self.assertEqual(result["status"], "error")

    def test_iwm_sense_reports_read_only_disk_and_absent_external_drive(self):
        self.rom.write_bytes(make_rom(bytes.fromhex(
            "13fc 0020 00ef fffe 13fc 0030 00ef e7fe "  # Clear overlay, set PA5.
            "4a39 00c0 1200 "  # Enable IWM and select internal drive.
            "4a39 00c0 0200 "  # CA0 high: PA5 + CA0 selects write-protect sense.
            "13f9 00c0 1a00 0000 0b00 "  # Q6 high: active, write-protected.
            "13f9 00c0 1600 0000 0b01 "  # Select absent external drive.
            "60fe"
        )))
        image = self.scratch / "raw.dsk"
        image.write_bytes(disk_image(0))
        result = self.boot("--floppy", image, "--inspect", "0xb00:2", "--mac-floppy-state", "1")
        self.assertEqual(result["status"], "cycle_limit")
        self.assertEqual(result["memory"]["bytes"], "20a0")
        self.assertTrue(result["mac_floppy"]["external_selected"])
        self.assertTrue(result["mac_floppy"]["select_line"])
        self.assertFalse(result["mac_floppy"]["motor_on"])
        self.assertEqual(result["mac_floppy"]["nibbles_delivered"], 0)

    def test_no_disk_never_executes_a_boot_sector(self):
        self.rom.write_bytes(make_floppy_rom())
        result = self.boot(
            "--until", "0xb00:1:0xa5", "--inspect", "0xb00:1", "--mac-floppy-state", "1",
            cycles=500_000, exit_code=1,
        )
        self.assertEqual(result["status"], "cycle_limit")
        self.assertEqual(result["memory"]["bytes"], "00")
        self.assertFalse(result["mac_floppy"]["media_present"])
        self.assertFalse(result["mac_floppy"]["write_protected"])
        self.assertEqual(result["mac_floppy"]["nibbles_delivered"], 0)
        self.assertGreater(result["mac_floppy"]["not_ready_data_reads"], 0)

    def test_floppy_snapshot_distinguishes_disabled_reads_from_nibbles(self):
        self.rom.write_bytes(make_rom(bytes.fromhex("4a39 00c0 0000 60fe")))
        result = self.boot("--mac-floppy-state", "1")
        floppy = result["mac_floppy"]
        self.assertFalse(floppy["controller_enabled"])
        self.assertFalse(floppy["motor_on"])
        self.assertEqual(floppy["disabled_data_reads"], 1)
        self.assertEqual(floppy["not_ready_data_reads"], 0)
        self.assertEqual(floppy["nibbles_delivered"], 0)

    def test_floppy_snapshot_records_eject_without_editing_media(self):
        # Select internal motor-on command 2, then eject command 7.
        self.rom.write_bytes(make_rom(bytes.fromhex(
            "13fc 0000 00ef fffe 13fc 0030 00ef e7fe "
            "4a39 00c0 1200 4a39 00c0 0600 4a39 00c0 0e00 4a39 00c0 0c00 "
            "4a39 00c0 0200 4a39 00c0 0a00 4a39 00c0 0e00 60fe"
        )))
        image = self.scratch / "eject.dsk"
        original = disk_image(0)
        image.write_bytes(original)
        result = self.boot("--floppy", image, "--mac-floppy-state", "1")
        floppy = result["mac_floppy"]
        self.assertFalse(floppy["media_present"])
        self.assertFalse(floppy["write_protected"])
        self.assertFalse(floppy["motor_on"])
        self.assertTrue(floppy["disk_changed"])
        self.assertEqual(floppy["eject_commands"], 1)
        self.assertEqual(image.read_bytes(), original)

    def test_floppy_snapshot_rejects_invalid_options_and_other_machine(self):
        for options in (("--mac-floppy-state", "0"), ("--mac-floppy-state", "2"),
                        ("--mac-floppy-state", "1", "--mac-floppy-state", "1")):
            with self.subTest(options=options):
                result = self.boot(*options, exit_code=2)
                self.assertEqual(result["status"], "error")
                self.assertNotIn("mac_floppy", result)
        process = subprocess.run(
            [self.exe, "--machine", "pc286", "--cycles", "100", "--timeout-ms", "100",
             "--mac-floppy-state", "1"],
            capture_output=True, text=True, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.assertEqual(process.returncode, 2)
        self.assertIn("mac-floppy-state requires mac128k", process.stdout)
