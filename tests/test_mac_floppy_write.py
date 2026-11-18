"""Original bounded IWM records and same-format private-copy persistence checks."""
import hashlib
import struct

from test_boot import BootHushTestCase, ROM_ENTRY
from test_mac_floppy import (GCR, FLOPPY_SIZE, make_floppy_rom, disk_copy_image,
                             sector_payload)


def data_record(data, tags=bytes(12), sector=0):
    assert len(data) == 512 and len(tags) == 12
    output = bytearray((0xd5, 0xaa, 0xad, GCR[sector]))
    a_sum = b_sum = c_sum = 0

    def pack(a, b, c, last=False):
        output.extend((GCR[((a >> 2) & 0x30) | ((b >> 4) & 12) | (c >> 6)],
                       GCR[a & 63], GCR[b & 63]))
        if not last:
            output.append(GCR[c & 63])

    payload = tags + data
    for offset in range(0, len(payload), 3):
        last = offset + 2 == len(payload)
        a, b = payload[offset:offset+2]
        c = 0 if last else payload[offset+2]
        rotated = ((c_sum << 1) | (c_sum >> 7)) & 255
        carry_a, a_sum = divmod(a_sum + a + (rotated & 1), 256)
        carry_b, b_sum = divmod(b_sum + b + carry_a, 256)
        c_sum = rotated if last else (rotated + c + carry_b) & 255
        pack(a ^ rotated, b ^ a_sum, c ^ b_sum, last)
    pack(a_sum, b_sum, c_sum)
    output.extend((0xde, 0xaa))
    assert len(output) == 709
    return output


def write_rom(record, *, track=0, read_sector=0, readback=True, paced=True,
              motor=True, close=True):
    # Keep the independently tested sector reader at its original addresses.
    # Reset instead enters our writer at ROM+8000, then jumps to that reader.
    rom = make_floppy_rom(track, sector=read_sector)
    code, labels, fixups = bytearray(), {}, []
    entry = 0x408000

    def emit(words): code.extend(bytes.fromhex(words))
    def label(name): labels[name] = len(code)
    def branch(op, name):
        emit(op)
        fixups.append((len(code), name))
        emit('0000')
    def switch(reg): emit(f'4a39 00c0 {reg * 512:04x}')

    emit('13fc000000effffe 13fc003000efe7fe')
    switch(9)
    if motor:
        switch(3); switch(7); switch(6)
    if track:
        switch(2); switch(1)
        for _ in range(track): switch(7); switch(6)
    switch(13); switch(15)
    emit('47f9')  # LEA record,A3.
    pointer = len(code)
    emit(f'00000000 343c{len(record)-1:04x}')
    label('next')
    if paced:
        emit('323c0fff')
        label('poll')
        emit('103900c01800 08000006')  # Handshake, then test error bit.
        branch('6700', 'finish')
        emit('4a00')
        branch('6b00', 'ready')
        branch('51c9', 'poll')
        branch('6000', 'finish')
    label('ready')
    emit('13db00c01a00')  # MOVE.B (A3)+,Q6-high: one write strobe.
    branch('51ca', 'next')
    emit('323c0fff')
    label('ack')
    emit('103900c01800 08000006')
    branch('6700', 'finish')
    emit('4a00')
    branch('6b00', 'finish')
    branch('51c9', 'ack')
    label('finish')
    emit('13c000000b02')  # Preserve last handshake for the host assertion.
    if close:
        switch(14)
    if readback:
        if track:
            # Return to track zero before the existing reader's own seek.
            switch(0); switch(2); switch(5); switch(7); switch(6)
            switch(4); switch(1)
            for _ in range(track): switch(7); switch(6)
            switch(0); switch(7); switch(6)  # Restore inward direction for reader.
        emit(f'4ef9{ROM_ENTRY:08x}')
    else:
        emit('13fc009100000b00 60fe')
    if len(code) & 1: code.append(0)
    struct.pack_into('>I', code, pointer, entry + len(code))
    code.extend(record)
    for position, name in fixups:
        struct.pack_into('>h', code, position, labels[name] - position)
    assert len(code) <= 0x8000
    rom[0x8000:0x8000+len(code)] = code
    struct.pack_into('>I', rom, 4, entry)
    return rom


def logical_sector(track, sector):
    return sum(12 - t // 16 for t in range(track)) + sector


class MacFloppyWriteTests(BootHushTestCase):
    def prepare(self, format='raw', track=0, sector=0):
        raw = bytearray((i * 13 + (i >> 8) + 17) & 255 for i in range(FLOPPY_SIZE))
        logical = logical_sector(track, sector)
        raw[logical*512:(logical+1)*512] = sector_payload()
        tags = bytes((i * 31 + 7) & 255 for i in range(9600)) if format == 'tagged' else b''
        source = bytearray(raw if format == 'raw' else disk_copy_image(raw, tags))
        if format != 'raw':
            source[12:60] = bytes(range(48))  # Preserve opaque header metadata.
        self.disk = self.scratch/'source.image'
        self.disk.write_bytes(source)
        self.original = bytes(source)
        self.output = self.scratch/'copy.image'
        self.raw, self.tags = raw, tags
        self.track, self.sector, self.logical = track, sector, logical
        self.replacement = bytes.fromhex('13fc00a600000b00 60fe') + bytes((i*57+101)&255 for i in range(502))
        self.new_tags = bytes(reversed(range(12))) if tags else bytes(12)
        return data_record(self.replacement, self.new_tags, sector)

    def run_write(self, record, *, expected=0xa6, export=True, exit_code=0, **options):
        self.rom.write_bytes(write_rom(record, track=self.track, read_sector=self.sector, **options))
        args = ['--floppy', self.disk, '--until', f'0xb00:1:{expected}',
                '--inspect', '0x1000:524', '--inspect', '0xb00:4', '--mac-floppy-state', '1']
        if export: args += ['--floppy-out', self.output]
        result = self.boot(*args, cycles=6_000_000, timeout_ms=5000, exit_code=exit_code)
        self.assertEqual(self.disk.read_bytes(), self.original)
        return result

    def test_same_guest_and_fresh_boot_raw_and_dc42(self):
        for format, track, sector in (('raw', 0, 0), ('untagged', 16, 10),
                                      ('tagged', 64, 7), ('tagged', 0, 0)):
            with self.subTest(format=format, track=track, sector=sector):
                record = self.prepare(format, track, sector)
                result = self.run_write(record)
                self.assertEqual(bytes.fromhex(result['memory_ranges'][0]['bytes']), self.new_tags+self.replacement)
                self.assertEqual(result['memory_ranges'][1]['bytes'][4:6], 'ff')
                self.assertFalse(result['mac_floppy']['write_protected'])
                self.assertEqual(result['mac_floppy_write'], {'enabled': True, 'error': False,
                    'gate_open': False, 'pending_bytes': 0, 'accepted_bytes': 709,
                    'committed_sectors': 1, 'rejected_records': 0})
                self.raw[self.logical*512:(self.logical+1)*512] = self.replacement
                new_tags = bytearray(self.tags)
                if new_tags: new_tags[self.logical*12:(self.logical+1)*12] = self.new_tags
                expected = bytearray(self.raw if format == 'raw' else disk_copy_image(self.raw, new_tags))
                if format != 'raw': expected[12:60] = self.original[12:60]
                self.assertEqual(self.output.read_bytes(), expected)
                snapshot_hash = hashlib.sha256(self.output.read_bytes()).digest()
                self.rom.write_bytes(make_floppy_rom(track, sector=sector))
                fresh = self.boot('--floppy', self.output, '--until', '0xb00:1:0xa6',
                                  '--inspect', '0x1000:524', '--mac-floppy-state', '1', cycles=6_000_000)
                self.assertEqual(bytes.fromhex(fresh['memory']['bytes']), self.new_tags+self.replacement)
                self.assertTrue(fresh['mac_floppy']['write_protected'])
                self.assertEqual(hashlib.sha256(self.output.read_bytes()).digest(), snapshot_hash)
                self.output.unlink()

    def test_default_protection_and_no_write_snapshot(self):
        record = self.prepare()
        result = self.run_write(record, expected=0xa5, export=False)
        self.assertTrue(result['mac_floppy']['write_protected'])
        self.assertNotIn('mac_floppy_write', result)
        self.assertEqual(result['memory_ranges'][1]['bytes'][4:6], 'bf')
        self.assertFalse(self.output.exists())
        for format in ('raw', 'untagged', 'tagged'):
            self.prepare(format)
            self.rom.write_bytes(make_floppy_rom())
            self.boot('--floppy', self.disk, '--floppy-out', self.output,
                      '--until', '0xb00:1:0xa5', cycles=4_000_000)
            self.assertEqual(self.output.read_bytes(), self.original)
            self.output.unlink()

    def test_bad_records_do_not_commit_or_export(self):
        healthy = self.prepare()
        mutations = []
        for index, value in ((3, GCR[12]), (100, 0x80), (704, GCR[(GCR.index(healthy[704])+1)%64]),
                             (708, 0xab)):
            record = healthy.copy(); record[index] = value; mutations.append(record)
        mutations += [healthy[:1], healthy[:3], healthy[:-1], b'\xff'*30,
                      data_record(self.replacement, bytes(range(12)))]
        for record in mutations:
            with self.subTest(length=len(record), digest=hashlib.sha256(record).hexdigest()):
                result = self.run_write(record, expected=0xa5, exit_code=2)
                self.assertEqual(bytes.fromhex(result['memory_ranges'][0]['bytes']), bytes(12)+sector_payload())
                self.assertEqual(result['mac_floppy_write']['committed_sectors'], 0)
                self.assertGreater(result['mac_floppy_write']['rejected_records'], 0)
                self.assertFalse(self.output.exists())

    def test_busy_not_ready_and_unclosed_gate_refuse_export(self):
        record = self.prepare()
        result = self.run_write(record, expected=0xa5, paced=False, exit_code=2)
        self.assertEqual(result['mac_floppy_write']['committed_sectors'], 0)
        self.assertEqual(result['memory_ranges'][1]['bytes'][4:6], '3f')
        self.assertFalse(self.output.exists())
        result = self.run_write(record, expected=0x91, readback=False, close=False, exit_code=2)
        self.assertEqual(result['mac_floppy_write']['committed_sectors'], 1)
        self.assertTrue(result['mac_floppy_write']['gate_open'])
        self.assertFalse(self.output.exists())
        result = self.run_write(record, expected=0x91, readback=False, paced=False, motor=False, exit_code=2)
        self.assertEqual(result['mac_floppy_write']['committed_sectors'], 0)
        self.assertFalse(self.output.exists())

    def test_multiple_records_and_later_error_suppress_snapshot(self):
        first = self.prepare()
        second_data = bytes((i*19+63)&255 for i in range(512))
        second = data_record(second_data, sector=1)
        result = self.run_write(first+second)
        self.assertEqual(result['mac_floppy_write']['committed_sectors'], 2)
        expected = bytearray(self.original)
        expected[:512] = self.replacement
        expected[512:1024] = second_data
        self.assertEqual(self.output.read_bytes(), expected)
        self.output.unlink()
        for later in (second[:-1], second[:-1]+b'\x00'):
            with self.subTest(length=len(later)):
                result = self.run_write(first+later, exit_code=2)
                # First sector really committed privately, but the later rejection
                # prevents exporting a misleading partially completed transaction.
                self.assertEqual(bytes.fromhex(result['memory_ranges'][0]['bytes']), bytes(12)+self.replacement)
                self.assertEqual(result['mac_floppy_write']['committed_sectors'], 1)
                self.assertEqual(result['mac_floppy_write']['rejected_records'], 1)
                self.assertFalse(self.output.exists())

    def test_output_refusals_failed_run_and_failed_observation(self):
        record = self.prepare()
        self.rom.write_bytes(write_rom(record))
        for existing in (self.disk, self.scratch):
            result = self.boot('--floppy', self.disk, '--floppy-out', existing, exit_code=2)
            self.assertEqual(result['cycles'], 0)
            self.assertEqual(self.disk.read_bytes(), self.original)
        self.output.write_bytes(b'keep existing')
        self.boot('--floppy', self.disk, '--floppy-out', self.output, exit_code=2)
        self.assertEqual(self.output.read_bytes(), b'keep existing')
        self.output.unlink()
        self.boot('--floppy-out', self.output, exit_code=2)
        self.boot('--floppy', self.disk, '--floppy-out', self.output, '--floppy-out', self.output, exit_code=2)
        self.boot('--floppy', self.disk, '--floppy-out', self.output,
                  '--until', '0xb00:1:0xa6', cycles=1, exit_code=1)
        self.assertFalse(self.output.exists())
        frame = self.scratch/'existing.pbm'; frame.write_bytes(b'keep')
        self.boot('--floppy', self.disk, '--floppy-out', self.output, '--frame', frame,
                  '--until', '0xb00:1:0xa6', cycles=6_000_000, exit_code=2)
        self.assertFalse(self.output.exists())
        self.assertEqual(frame.read_bytes(), b'keep')
        self.assertEqual(self.disk.read_bytes(), self.original)
        from test_boot import make_rom, SMOKE_PROGRAM
        self.rom.write_bytes(make_rom(SMOKE_PROGRAM))
        result = self.boot('--floppy', self.disk, '--floppy-out', self.output,
                           cycles=20_000_000_000, timeout_ms=1, exit_code=1)
        self.assertEqual(result['status'], 'timeout')
        self.assertFalse(self.output.exists())
        self.prepare('tagged')
        corrupt = bytearray(self.original); corrupt[100] ^= 1
        self.disk.write_bytes(corrupt)
        self.boot('--floppy', self.disk, '--floppy-out', self.output, exit_code=2)
        self.assertEqual(self.disk.read_bytes(), corrupt)
        self.assertFalse(self.output.exists())
