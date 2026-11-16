import json
import hashlib
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

    def test_keyboard_schedule_accounting(self):
        # These counts describe delivery to the model, not guest consumption.
        image = self.write('idle.img', boot_image(bytes.fromhex('faebfe'), 1228800))
        neutral = self.run_guest('--floppy', image, cycles=4000)
        self.assertNotIn('pc_keyboard_input', neutral)
        self.assertNotIn('pc_keyboard_buffer', neutral)
        result = self.run_guest('--floppy', image, '--scancode', '1000:0x74',
                                '--scancode', '2000:0xf0', cycles=4000)
        self.assertEqual(result['status'], 'cycle_limit')
        self.assertEqual(result['pc_keyboard_input'],
                         {'scheduled': 2, 'accepted': 2, 'rejected': 0, 'pending': 0})
        self.assertEqual(result['pc_keyboard_buffer'], {'queued': 1, 'capacity': 32})

        # The stop predicate is checked before future scheduled input.
        image = self.write('early.img', boot_image(bytes.fromhex('b0aae680faebfe'), 1228800))
        result = self.run_guest('--floppy', image, '--until-post', '0xaa',
                                '--scancode', '1000:0x74', '--scancode', '2000:0x74', cycles=4000)
        self.assertEqual(result['status'], 'condition_met')
        self.assertEqual(result['pc_keyboard_input'],
                         {'scheduled': 2, 'accepted': 0, 'rejected': 0, 'pending': 2})
        self.assertEqual(result['pc_keyboard_buffer'], {'queued': 0, 'capacity': 32})

        # The first byte beyond model queue capacity is rejected; subsequent
        # scheduled bytes are still pending when the run stops on the error.
        image = self.write('overflow.img', boot_image(bytes.fromhex('faebfe'), 1228800))
        args = ['--floppy', image]
        for index in range(34):
            args += ['--scancode', f'{1000 + index * 20}:0x74']
        result = self.run_guest(*args, cycles=4000, exit_code=1)
        self.assertEqual(result['status'], 'machine_fault')
        self.assertEqual(result['pc_keyboard_input'],
                         {'scheduled': 34, 'accepted': 32, 'rejected': 1, 'pending': 1})
        self.assertEqual(result['pc_keyboard_buffer'], {'queued': 32, 'capacity': 32})

        # The accepted byte disappears from the snapshot when the guest reads
        # port 60h. Scheduled input and controller output are different queues.
        image = self.write('consume.img', boot_image(bytes.fromhex('b9e803 e2fe e460 b0aa e680 faebfe'), 1228800))
        result = self.run_guest('--floppy', image, '--scancode', '1000:0x74',
                                '--until-post', '0xaa', cycles=100000)
        self.assertEqual(result['status'], 'condition_met')
        self.assertEqual(result['pc_keyboard_input']['accepted'], 1)
        self.assertEqual(result['pc_keyboard_buffer'], {'queued': 0, 'capacity': 32})

    def test_selected_hard_disk_read_failure(self):
        # Two independently addressed BIOS reads; retain first data if the
        # second is rejected, and store the returned AX and FLAGS.
        program = bytearray.fromhex('31c0 8ec0 bb0006 b80102 b90200 ba8000 cd13 72 00')
        first_branch = len(program)-1
        program += bytes.fromhex('bb0008 b80102 b90100 ba8001 cd13 72 00')
        second_branch = len(program)-1
        program += bytes.fromhex('b042 e680 fa f4')
        first_failure = len(program)
        program += bytes.fromhex('a30005 9c58 a30205 b0e1 e680 fa f4')
        second_failure = len(program)
        program += bytes.fromhex('a30005 9c58 a30205 b0e2 e680 fa f4')
        program[first_branch] = first_failure-first_branch-1
        program[second_branch] = second_failure-second_branch-1
        image = boot_image(program, HARD_DISK_SIZE)
        image[512:516] = b'BHAT'
        image[17*512:17*512+4] = b'NEXT'
        disk = self.write('failure-disk.img', image)
        original = hashlib.sha256(disk.read_bytes()).hexdigest()
        for selected,post,first in (('0:0:2',0xe1,b'\0\0\0\0'),('0:1:1',0xe2,b'BHAT')):
            result = self.run_guest('--hard-disk', disk, '--fail-hard-disk-read', selected,
                                    '--until-post', hex(post), '--inspect', '0x500:4',
                                    '--inspect', '0x600:4', '--inspect', '0x800:4')
            self.assertEqual(result['status'], 'condition_met')
            fault = result['hard_disk_read_failure']
            self.assertEqual((fault['injected'],fault['bios_status']),(1,32))
            self.assertEqual([fault[x] for x in ('cylinder','head','sector')],
                             [int(x) for x in selected.split(':')])
            rows = [bytes.fromhex(row['bytes']) for row in result['memory_ranges']]
            self.assertEqual(rows[0][:2], b'\x00\x20')
            self.assertTrue(rows[0][2] & 1)
            self.assertEqual(rows[1:], [first,b'\0\0\0\0'])
        result = self.run_guest('--hard-disk', disk, '--fail-hard-disk-read', '614:3:17',
                                '--until-post', '0x42')
        self.assertEqual(result['hard_disk_read_failure']['cylinder'],614)
        self.assertEqual(result['hard_disk_read_failure']['injected'],0)
        # A different starting sector is not an injected read failure.
        result = self.run_guest('--hard-disk', disk, '--fail-hard-disk-read', '0:0:3',
                                '--until-post', '0x42')
        self.assertEqual(result['hard_disk_read_failure']['injected'],0)
        # A buffer crossing 64 KiB is rejected before the failure hook.
        crossing = checked_program((('31c0 8ec0 bb00ff b80102 b90200 ba8000 cd13', 0x73),), 0x43)
        crossing_disk = self.write('crossing.img', boot_image(crossing,HARD_DISK_SIZE))
        result = self.run_guest('--hard-disk', crossing_disk, '--fail-hard-disk-read', '0:0:2',
                                '--until-post', '0x43')
        self.assertEqual(result['hard_disk_read_failure']['injected'],0)
        invalid = self.run_guest('--hard-disk', disk, '--fail-hard-disk-read', '615:0:1', exit_code=2)
        self.assertEqual(invalid['status'],'error')
        missing = self.run_guest('--fail-hard-disk-read', '0:0:2', exit_code=2)
        self.assertEqual(missing['status'],'error')
        self.assertEqual(hashlib.sha256(disk.read_bytes()).hexdigest(),original)

    def test_transient_hard_disk_read_failure_and_retry(self):
        # The first valid read fails without changing RAM. After a disk reset,
        # a retry and a further identical read both succeed with original data.
        program = checked_program((
            ('31c0 8ec0 bb0006 b80102 b90200 ba8000 cd13', 0x73), # fail if carry clear
            ('80fc20', 0x75),                                    # AH must be 20h
            ('803e000600', 0x75),                                # destination untouched
            ('b80000 ba8000 cd13', 0x72),                        # reset succeeds
            ('bb0006 b80102 b90200 ba8000 cd13', 0x72),
            ('813e00064248', 0x75),                              # BH at 0600h
            ('bb0008 b80102 b90200 ba8000 cd13', 0x72),
            ('813e00084248', 0x75),
        ), 0x42)
        image=boot_image(program,HARD_DISK_SIZE);image[512:516]=b'BHAT'
        disk=self.write('transient-read.img',image);before=hashlib.sha256(disk.read_bytes()).hexdigest()
        result=self.run_guest('--hard-disk',disk,'--fail-hard-disk-read-once','0:0:2',
                              '--until-post','0x42','--inspect','0x600:4','--inspect','0x800:4')
        self.assertEqual(result['status'],'condition_met')
        self.assertEqual(result['hard_disk_read_failure'],
                         {'cylinder':0,'head':0,'sector':2,'injected':1,'bios_status':32,'limit':1})
        self.assertEqual([bytes.fromhex(row['bytes']) for row in result['memory_ranges']], [b'BHAT',b'BHAT'])
        # The old persistent option must still fail on the retry.
        persistent=self.run_guest('--hard-disk',disk,'--fail-hard-disk-read','0:0:2','--until-post','0xff')
        self.assertEqual(persistent['status'],'condition_met')
        self.assertEqual(persistent['hard_disk_read_failure']['injected'],2)
        self.assertNotIn('limit',persistent['hard_disk_read_failure'])
        invalid=self.run_guest('--hard-disk',disk,'--fail-hard-disk-read-once','615:0:1',exit_code=2)
        self.assertEqual(invalid['status'],'error')
        missing=self.run_guest('--fail-hard-disk-read-once','0:0:2',exit_code=2)
        self.assertEqual(missing['status'],'error')
        duplicate=self.run_guest('--hard-disk',disk,'--fail-hard-disk-read','0:0:2',
                                 '--fail-hard-disk-read-once','0:0:2',exit_code=2)
        self.assertEqual(duplicate['status'],'error')
        self.assertEqual(hashlib.sha256(disk.read_bytes()).hexdigest(),before)

    def test_hard_disk_read_failure_at_occurrence(self):
        program = checked_program((
            ('31c0 8ec0 bb0006 b80102 b90200 ba8000 cd13', 0x72),
            ('813e00064248', 0x75),
            ('bb0008 b80102 b90200 ba8000 cd13', 0x73),
            ('80fc20', 0x75),
            ('803e000800', 0x75),
            ('bb000a b80102 b90200 ba8000 cd13', 0x72),
            ('813e000a4248', 0x75),
        ), 0x42)
        image=boot_image(program,HARD_DISK_SIZE);image[512:516]=b'BHAT'
        disk=self.write('ordinal-read.img',image);before=hashlib.sha256(disk.read_bytes()).hexdigest()
        result=self.run_guest('--hard-disk',disk,'--fail-hard-disk-read-at','0:0:2:2',
                              '--until-post','0x42','--inspect','0x600:4','--inspect','0x800:4',
                              '--inspect','0xa00:4')
        self.assertEqual(result['hard_disk_read_failure'],
                         {'cylinder':0,'head':0,'sector':2,'injected':1,'bios_status':32,
                          'occurrence':2,'matched':3})
        self.assertEqual([bytes.fromhex(r['bytes']) for r in result['memory_ranges']], [b'BHAT',bytes(4),b'BHAT'])
        no_trigger_program=checked_program((
            ('31c0 8ec0 bb0006 b80102 b90200 ba8000 cd13', 0x72),
            ('bb0008 b80102 b90200 ba8000 cd13', 0x72),
        ),0x43)
        no_trigger_image=boot_image(no_trigger_program,HARD_DISK_SIZE);no_trigger_image[512:516]=b'BHAT'
        no_trigger_disk=self.write('ordinal-not-reached.img',no_trigger_image)
        no_trigger=self.run_guest('--hard-disk',no_trigger_disk,'--fail-hard-disk-read-at','0:0:2:3',
                                  '--until-post','0x43')
        self.assertEqual((no_trigger['hard_disk_read_failure']['matched'],
                          no_trigger['hard_disk_read_failure']['injected']),(2,0))
        wrong_chs=self.run_guest('--hard-disk',no_trigger_disk,'--fail-hard-disk-read-at','0:0:3:1',
                                 '--until-post','0x43')
        self.assertEqual((wrong_chs['hard_disk_read_failure']['matched'],
                          wrong_chs['hard_disk_read_failure']['injected']),(0,0))
        # A crossing buffer is rejected by the model before the ordinal
        # matcher; the next valid read must still be occurrence one.
        crossing_program=checked_program((
            ('31c0 8ec0 bb00ff b80102 b90200 ba8000 cd13', 0x73),
            ('80fc09', 0x75),
            ('bb0006 b80102 b90200 ba8000 cd13', 0x73),
            ('80fc20', 0x75),
            ('bb0008 b80102 b90200 ba8000 cd13', 0x72),
        ),0x44)
        crossing_disk=self.write('ordinal-crossing.img',boot_image(crossing_program,HARD_DISK_SIZE))
        crossing=self.run_guest('--hard-disk',crossing_disk,'--fail-hard-disk-read-at','0:0:2:1',
                                '--until-post','0x44')
        self.assertEqual((crossing['hard_disk_read_failure']['matched'],
                          crossing['hard_disk_read_failure']['injected']),(2,1))
        for bad in ('0:0:2:0','615:0:1:2','0:0:2','0:0:2:2:1'):
            self.assertEqual(self.run_guest('--hard-disk',disk,'--fail-hard-disk-read-at',bad,exit_code=2)['status'],'error')
        self.assertEqual(self.run_guest('--hard-disk',disk,'--fail-hard-disk-read','0:0:2',
                         '--fail-hard-disk-read-at','0:0:2:2',exit_code=2)['status'],'error')
        self.assertEqual(self.run_guest('--hard-disk',disk,'--fail-hard-disk-read-at','0:0:2:2',
                         '--fail-hard-disk-read-once','0:0:2',exit_code=2)['status'],'error')
        self.assertEqual(hashlib.sha256(disk.read_bytes()).hexdigest(),before)

    def test_hard_disk_write_failure_at_occurrence(self):
        # Write distinct bytes, reject the second write without transfer, read
        # the first bytes back, then permit a third write and read that back.
        program=checked_program((
            ('31c0 8ec0 c70600064141 bb0006 b80103 b90200 ba8000 cd13',0x72),
            ('c70600064242 bb0006 b80103 b90200 ba8000 cd13',0x73),
            ('80fc20',0x75),
            ('bb0008 b80102 b90200 ba8000 cd13',0x72),
            ('813e00084141',0x75),
            ('c70600064343 bb0006 b80103 b90200 ba8000 cd13',0x72),
            ('bb000a b80102 b90200 ba8000 cd13',0x72),
            ('813e000a4343',0x75),
        ),0x42)
        disk=self.write('ordinal-write.img',boot_image(program,HARD_DISK_SIZE))
        before=hashlib.sha256(disk.read_bytes()).hexdigest()
        snapshot=self.directory/'ordinal-write-after.img'
        result=self.run_guest('--hard-disk',disk,'--disk-out',snapshot,
                              '--fail-hard-disk-write-at','0:0:2:2','--until-post','0x42',
                              '--inspect','0x800:2','--inspect','0xa00:2')
        self.assertEqual(result['status'],'condition_met')
        self.assertEqual(result['hard_disk_write_failure'],
                         {'cylinder':0,'head':0,'sector':2,'injected':1,'bios_status':32,
                          'occurrence':2,'matched':3})
        self.assertEqual([bytes.fromhex(r['bytes']) for r in result['memory_ranges']],[b'AA',b'CC'])
        self.assertEqual(snapshot.read_bytes()[512:514],b'CC')
        self.assertEqual(hashlib.sha256(disk.read_bytes()).hexdigest(),before)
        no_trigger=checked_program((
            ('31c0 8ec0 bb0006 b80103 b90200 ba8000 cd13',0x72),
            ('bb0006 b80103 b90200 ba8000 cd13',0x72),
        ),0x43)
        other=self.write('ordinal-write-not-reached.img',boot_image(no_trigger,HARD_DISK_SIZE))
        for selection,matched in (('0:0:2:3',2),('0:0:3:1',0)):
            result=self.run_guest('--hard-disk',other,'--fail-hard-disk-write-at',selection,'--until-post','0x43')
            self.assertEqual((result['hard_disk_write_failure']['matched'],result['hard_disk_write_failure']['injected']),(matched,0))
        crossing=checked_program((
            ('31c0 8ec0 bb00ff b80103 b90200 ba8000 cd13',0x73),
            ('80fc09',0x75),
            ('bb0006 b80103 b90200 ba8000 cd13',0x73),
            ('80fc20',0x75),
            ('bb0006 b80103 b90200 ba8000 cd13',0x72),
        ),0x44)
        crossing_disk=self.write('ordinal-write-crossing.img',boot_image(crossing,HARD_DISK_SIZE))
        result=self.run_guest('--hard-disk',crossing_disk,'--fail-hard-disk-write-at','0:0:2:1','--until-post','0x44')
        self.assertEqual((result['hard_disk_write_failure']['matched'],result['hard_disk_write_failure']['injected']),(2,1))
        for bad in ('0:0:2:0','615:0:1:2','0:0:2','0:0:2:2:1'):
            self.assertEqual(self.run_guest('--hard-disk',disk,'--fail-hard-disk-write-at',bad,exit_code=2)['status'],'error')
        self.assertEqual(self.run_guest('--hard-disk',disk,'--fail-hard-disk-write','0:0:2',
                         '--fail-hard-disk-write-at','0:0:2:2',exit_code=2)['status'],'error')
        self.assertEqual(self.run_guest('--fail-hard-disk-write-at','0:0:2:2',exit_code=2)['status'],'error')

    def test_selected_hard_disk_write_failure(self):
        program = bytearray.fromhex('31c0 8ec0 bb007d b80103 b90200 ba8000 cd13 72 00')
        branch = len(program)-1
        program += bytes.fromhex('b042 e680 fa f4')
        failure = len(program)
        program += bytes.fromhex('a30005 9c58 a30205 b0e1 e680 fa f4')
        program[branch] = failure-branch-1
        image = boot_image(program,HARD_DISK_SIZE);image[0x100:0x104]=b'BHAT'
        disk = self.write('write-disk.img',image)
        before = hashlib.sha256(disk.read_bytes()).hexdigest()
        failed_snapshot = self.directory/'failed-write.img'
        result = self.run_guest('--hard-disk',disk,'--disk-out',failed_snapshot,
                                '--fail-hard-disk-write','0:0:2','--fail-hard-disk-read','614:3:17',
                                '--until-post','0xe1',
                                '--inspect','0x500:4')
        fault = result['hard_disk_write_failure']
        self.assertEqual((fault['cylinder'],fault['head'],fault['sector'],fault['injected'],fault['bios_status']),
                         (0,0,2,1,32))
        self.assertEqual(result['hard_disk_read_failure']['injected'],0)
        registers = bytes.fromhex(result['memory']['bytes'])
        self.assertEqual(registers[:2],b'\x00\x20')
        self.assertTrue(registers[2]&1)
        self.assertEqual(hashlib.sha256(failed_snapshot.read_bytes()).hexdigest(),before)
        good_snapshot = self.directory/'successful-write.img'
        result = self.run_guest('--hard-disk',disk,'--disk-out',good_snapshot,
                                '--fail-hard-disk-write','614:3:17','--until-post','0x42')
        self.assertEqual(result['hard_disk_write_failure']['injected'],0)
        self.assertEqual(good_snapshot.read_bytes()[512:516],b'BHAT')
        self.assertEqual(hashlib.sha256(disk.read_bytes()).hexdigest(),before)

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
