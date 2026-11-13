import json
import hashlib
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

def make_four_key_rom():
    image=make_keyboard_rom()
    program=(move_byte(0,VIA_PORT_A)+move_byte(0x10,VIA_DIRECTION_A)+
             struct.pack('>HII',0x23FC,0x400000+HANDLER_OFFSET,LEVEL_ONE_VECTOR)+move_byte(0x84,VIA_IER)+
             b''.join(exchange(0x14,RESULT+i) for i in range(4))+bytes.fromhex('60fe'))
    image[0x104:0x104+len(program)]=program
    return image


class MacKeyboardIntegration(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('BOOTHUSH_STB_MAC_PROBE'),'port controls probe not supplied')
    def test_port_controls_probe_witness_and_vbl_series(self):
        rom=Path(os.environ['BOOTHUSH_STB_MAC_PROBE'])
        digest='31b2666be66ac7a02d576998cb9b46aec3a91245d16679877d593748fd581fc6'
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),digest)
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp)
            def run(name):
                trace=root/(name+'.txt');series=root/(name+'.pbm')
                cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(rom),'--cycles','2000000',
                     '--timeout-ms','15000','--inspect','0xb00:12','--keyboard-witness',str(trace),
                     '--capture-series',str(series),'--capture-vbl','5','--capture-vbl','8','--capture-vbl','11',
                     '--capture-vbl','14','--capture-memory','0xb00:12',
                     '--key','600000:0:1','--key','900000:0:0','--key','1200000:1:1','--key','1500000:1:0']
                p=subprocess.run(cmd,capture_output=True,text=True,timeout=20,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr));return json.loads(p.stdout),trace.read_text(),series.read_bytes()
            result,trace,pixels=run('first');self.assertEqual((result,trace,pixels),run('repeat'))
            self.assertEqual(result['memory']['bytes'],'4e5354320000000101830202')
            rows=[line.split() for line in trace.splitlines()[2:]]
            self.assertEqual([int(row[3]) for row in rows if row[2]=='key_delivered'],[1,0x81,3,0x83])
            self.assertEqual(result['keyboard_witness']['dropped'],0)
            self.assertEqual([s['frame'] for s in result['frame_series']['samples']],[5,8,11,14])
            self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),digest)
    def test_keyboard_witness_overlapping_holds_and_release(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp);rom=root/'four.rom';rom.write_bytes(make_four_key_rom())
            def run(name):
                witness=root/(name+'.txt')
                cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(rom),'--cycles','500000',
                     '--timeout-ms','5000','--key','1000:0x24:1','--key','2000:0x25:1',
                     '--key','90000:0x24:0','--key','100000:0x25:0',
                     '--until','0xb03:1:0xcb','--inspect','0xb00:4','--keyboard-witness',str(witness)]
                p=subprocess.run(cmd,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr));return json.loads(p.stdout),witness.read_text()
            result,trace=run('first');self.assertEqual((result,trace),run('repeat'))
            self.assertEqual(result['memory']['bytes'],'494bc9cb')
            summary=result['keyboard_witness']
            self.assertEqual((summary['scheduled'],summary['accepted'],summary['pending'],summary['rejected'],summary['dropped']),(4,4,0,0,0))
            rows=[row.split() for row in trace.splitlines()[2:]]
            self.assertEqual([int(r[3]) for r in rows if r[2]=='input_due'],[0x49,0x4b,0xc9,0xcb])
            self.assertEqual([int(r[3]) for r in rows if r[2]=='queued'],[0x49,0x4b,0xc9,0xcb])
            self.assertEqual([int(r[3]) for r in rows if r[2]=='key_delivered'],[0x49,0x4b,0xc9,0xcb])
            self.assertTrue(any(r[2]=='shift_read' and int(r[3])==0xcb for r in rows))
            self.assertTrue(any(r[2]=='command_complete' for r in rows))

    def test_keyboard_witness_queue_overflow_and_existing_path(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp);rom=root/'idle.rom';rom.write_bytes(make_rom(bytes.fromhex('60fe')))
            trace=root/'overflow.txt'
            cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(rom),'--cycles','1000',
                 '--timeout-ms','5000','--keyboard-witness',str(trace)]
            for i in range(65):cmd += ['--key',f'0:{i%2}:1']
            def run():return subprocess.run(cmd,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            p=run();self.assertEqual(p.returncode,1,p.stdout+p.stderr);result=json.loads(p.stdout)
            self.assertEqual((result['keyboard_witness']['accepted'],result['keyboard_witness']['pending'],result['keyboard_witness']['rejected']),(64,1,1))
            text=trace.read_text();self.assertIn(' rejected ',text);before=trace.read_bytes()
            p=run();self.assertEqual(p.returncode,2);self.assertEqual(trace.read_bytes(),before)

    def test_keyboard_witness_bound_filter_and_noninterference(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp);rom=root/'repeat.rom'
            rom.write_bytes(make_rom(bytes.fromhex('13fc001c00eff7fe 60f6')))
            cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(rom),
                 '--cycles','1000000','--timeout-ms','5000']
            def run(extra):
                p=subprocess.run(cmd+extra,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,p.stdout+p.stderr);return json.loads(p.stdout)
            plain=run([]);full=run(['--keyboard-witness',str(root/'full.txt')])
            summary=full.pop('keyboard_witness');self.assertEqual(full,plain)
            self.assertEqual(summary['events'],8192);self.assertGreater(summary['dropped'],0)
            small=run(['--keyboard-witness',str(root/'small.txt'),'--keyboard-witness-range','0:100'])
            self.assertLess(small['keyboard_witness']['events'],100)
            self.assertEqual(small['keyboard_witness']['dropped'],0)

    def test_readiness_relative_key_press_and_release_are_observed(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            rom=Path(temp)/'keyboard.rom'; rom.write_bytes(make_keyboard_rom())
            cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(rom),
                 '--cycles','500000','--timeout-ms','5000','--input-after','0xb00:1:3',
                 '--key','0:0x24:1','--key','1:0x24:0','--until','0xb03:1:0xc9','--inspect','0xb00:7']
            def run():
                p=subprocess.run(cmd,capture_output=True,encoding='utf-8',timeout=10,
                                 creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr)); return json.loads(p.stdout)
            first=run(); self.assertEqual(first,run())
            self.assertEqual(first['memory']['bytes'],'037d49c9088400')
            self.assertTrue(first['input_schedule']['ready'])
            self.assertEqual((first['input_schedule']['scheduled'],first['input_schedule']['accepted']),(2,2))

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
