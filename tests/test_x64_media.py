"""Original synthetic media; these checks do not boot or emulate x86-64."""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path

SECTOR = 512
FIRST = 2048
FAT_START = FIRST + 78
DATA = FIRST + 78 + 2 * 985


def crc32(data):
    value = 0xffffffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xedb88320 if value & 1 else 0)
    return value ^ 0xffffffff


def make_media():
    disk = bytearray(64 * 1024 * 1024)
    disk[510:512] = b'\x55\xaa'
    disk[450] = 0xee
    struct.pack_into('<II', disk, 454, 1, len(disk) // SECTOR - 1)
    entries = bytearray(128 * 128)
    entries[:16] = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
    entries[16:32] = uuid.UUID('12345678-1234-1234-1234-123456789abc').bytes_le
    struct.pack_into('<QQ', entries, 32, FIRST, 129023)
    crc = crc32(entries)
    last = len(disk) // SECTOR - 1
    for current, other, table in ((1, last, 2), (last, 1, last - 32)):
        header = bytearray(SECTOR)
        struct.pack_into('<8sIIIIQQQQ16sQIII', header, 0, b'EFI PART', 0x10000, 92, 0, 0,
                         current, other, 34, last - 33, bytes(range(16)), table, 128, 128, crc)
        struct.pack_into('<I', header, 16, crc32(header[:92]))
        disk[current * SECTOR:(current + 1) * SECTOR] = header
        disk[table * SECTOR:table * SECTOR + len(entries)] = entries
    boot = memoryview(disk)[FIRST * SECTOR:(FIRST + 1) * SECTOR]
    struct.pack_into('<H', boot, 11, SECTOR)
    boot[13] = 1
    struct.pack_into('<H', boot, 14, 78)
    boot[16] = 2
    struct.pack_into('<I', boot, 32, 126976)
    struct.pack_into('<I', boot, 36, 985)
    struct.pack_into('<I', boot, 44, 2)
    boot[510:512] = b'\x55\xaa'
    del boot
    for fat_lba in (FAT_START, FAT_START + 985):
        for cluster, value in enumerate((0x0ffffff8, 0x0fffffff, 0x0fffffff,
                                         0x0fffffff, 0x0fffffff, 0x0fffffff)):
            struct.pack_into('<I', disk, fat_lba * SECTOR + cluster * 4, value)
    for cluster, name, attr, target, size in ((2, b'EFI        ', 0x10, 3, 0),
                                               (3, b'BOOT       ', 0x10, 4, 0),
                                               (4, b'BOOTX64 EFI', 0x20, 5, 512)):
        offset = (DATA + cluster - 2) * SECTOR
        disk[offset:offset + 11] = name
        disk[offset + 11] = attr
        struct.pack_into('<H', disk, offset + 26, target)
        struct.pack_into('<I', disk, offset + 28, size)
    efi = bytearray(512)
    efi[:2] = b'MZ'
    struct.pack_into('<I', efi, 0x3c, 0x80)
    efi[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', efi, 0x84, 0x8664, 1)
    struct.pack_into('<H', efi, 0x94, 240)
    opt = 0x98
    struct.pack_into('<H', efi, opt, 0x20b)
    struct.pack_into('<I', efi, opt + 16, 0x1000)
    struct.pack_into('<I', efi, opt + 56, 0x2000)
    struct.pack_into('<H', efi, opt + 68, 10)
    section = opt + 240
    struct.pack_into('<IIII', efi, section + 8, 0x1000, 0x1000, 0x20, 0x1e0)
    struct.pack_into('<I', efi, section + 36, 0x20000000)
    disk[(DATA + 3) * SECTOR:(DATA + 4) * SECTOR] = efi
    return disk, efi


class X64MediaTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP'))
        cls.path = Path(cls.tmp.name)
        disk, efi = make_media()
        cls.disk = cls.path / 'synthetic.img'
        cls.efi = cls.path / 'synthetic.efi'
        cls.disk.write_bytes(disk)
        cls.efi.write_bytes(efi)
        cls.disk_hash = hashlib.sha256(disk).hexdigest()
        cls.efi_hash = hashlib.sha256(efi).hexdigest()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_verify(self, disk, *args):
        result = subprocess.run([os.environ['BOOTHUSH_EXE'], '--verify-x64-media', str(disk), *map(str, args)],
                                capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        return result.returncode, json.loads(result.stdout)

    def test_host_capability_does_not_claim_execution(self):
        result = subprocess.run([os.environ['BOOTHUSH_EXE'], '--x64-capability'], capture_output=True,
                                text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        record = json.loads(result.stdout)
        self.assertEqual(record['backend'], 'whpx')
        self.assertFalse(record['implemented'])
        self.assertFalse(record['guest_executed'])
        if result.returncode == 0:
            self.assertEqual(record['status'], 'x64_capability')
            self.assertIsInstance(record['hypervisor_present'], bool)
        else:
            self.assertEqual(result.returncode, 1)
            self.assertEqual(record['status'], 'x64_capability_error')
            self.assertTrue('hresult' in record or 'error' in record)

    def test_valid_and_read_only(self):
        code, result = self.run_verify(self.disk, '--expect-efi', self.efi)
        self.assertEqual(code, 0, result)
        self.assertEqual(result['disk_sha256'], self.disk_hash)
        self.assertEqual(result['efi_sha256'], self.efi_hash)
        self.assertEqual(result['esp_lba'], FIRST)
        self.assertEqual(result['entry_rva'], 0x1000)
        self.assertFalse(result['executed'])
        self.assertEqual(hashlib.sha256(self.disk.read_bytes()).hexdigest(), self.disk_hash)

    def test_invalid_media(self):
        cases = ((512 + 16, b'\0\0\0\0', 'GPT header'),
                 ((FAT_START + 985) * SECTOR + 20, b'\x05', 'FAT32 copies'),
                 (DATA * SECTOR, b'X', 'missing'),
                 ((DATA + 3) * SECTOR, b'X', 'PE32+'))
        bad = self.path / 'bad.img'
        for offset, value, message in cases:
            with self.subTest(message=message):
                shutil.copyfile(self.disk, bad)
                with bad.open('r+b') as stream:
                    stream.seek(offset)
                    stream.write(value)
                code, result = self.run_verify(bad)
                self.assertEqual(code, 2, result)
                self.assertIn(message, result['error'])
        bad.unlink()

    def test_expected_mismatch_and_short_disk(self):
        code, result = self.run_verify(self.disk, '--expect-efi', self.disk)
        self.assertEqual(code, 2, result)
        code, result = self.run_verify(self.disk, '--expect-efi', __file__)
        self.assertEqual(code, 2, result)
        self.assertIn('differs', result['error'])
        code, result = self.run_verify(self.efi)
        self.assertEqual(code, 2, result)
        self.assertIn('67108864', result['error'])

    def test_cycle_in_file_chain(self):
        bad = self.path / 'cycle.img'
        shutil.copyfile(self.disk, bad)
        with bad.open('r+b') as stream:
            for fat in (FAT_START, FAT_START + 985):
                stream.seek(fat * SECTOR + 5 * 4)
                stream.write(struct.pack('<I', 5))
            stream.seek((DATA + 2) * SECTOR + 28)
            stream.write(struct.pack('<I', 1024))
        code, result = self.run_verify(bad)
        self.assertEqual(code, 2, result)
        self.assertIn('cluster chain', result['error'])
        bad.unlink()

    def test_classick_media_when_supplied(self):
        disk = os.environ.get('BOOTHUSH_CLASSICK_X64_DISK')
        efi = os.environ.get('BOOTHUSH_CLASSICK_X64_EFI')
        if not disk or not efi:
            self.skipTest('ClasSICK x64 disk and EFI not supplied')
        code, result = self.run_verify(disk, '--expect-efi', efi)
        self.assertEqual(code, 0, result)
        self.assertEqual(result['disk_sha256'], '80c279a69264cb07f2042bf14503c0687fa32e71057ee1196fc8174afbeeae4f')
        self.assertEqual(result['efi_sha256'], 'df284097539ed6b7c90700cb27589900b20d2e4c7574b7637ba5f387abba9fe9')
        self.assertFalse(result['executed'])


if __name__ == '__main__':
    unittest.main()
