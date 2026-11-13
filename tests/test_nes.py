"""Original generated cartridges for the bounded experimental NES CPU target."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave


def cartridge(code, banks=1, start=0x8000, chr_banks=0):
    header = bytearray(b'NES\x1a' + bytes(12))
    header[4], header[5] = banks, chr_banks
    prg = bytearray([0xff] * (banks * 16384))
    offset = (start - 0x8000) % len(prg)
    prg[offset:offset + len(code)] = code
    prg[-4:-2] = start.to_bytes(2, 'little')
    return header + prg + bytes(chr_banks * 8192)


class NesTarget(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP'))
        self.addCleanup(tmp.cleanup)
        self.file = Path(tmp.name) / 'original.nes'

    def run_nes(self, image, *args, steps=100):
        self.file.write_bytes(image)
        before = hashlib.sha256(image).hexdigest()
        proc = subprocess.run([os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(self.file),
                               '--steps', str(steps), '--timeout-ms', '3000', *args],
                              capture_output=True, text=True, timeout=10,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(proc.stderr, '')
        self.assertEqual(hashlib.sha256(self.file.read_bytes()).hexdigest(), before)
        return proc.returncode, json.loads(proc.stdout)

    def test_original_loop_and_ram_mirroring(self):
        # Sum five ones, store each result, and stop just after the loop.
        image = cartridge(bytes.fromhex('a200 a900 18 6901 8510 e8 e005 d0f6 ea'))
        for address in ('0x10', '0x810', '0x1010', '0x1810'):
            code, result = self.run_nes(image, '--until-pc', '0x800e', '--inspect', address + ':1')
            self.assertEqual((code, result['status'], result['a'], result['x'], result['memory']['bytes']),
                             (0, 'condition_met', 5, 5, '05'))
            self.assertEqual(result['rom_sha256'], hashlib.sha256(image).hexdigest())

    def test_nrom_128_mirror_and_256_reset_vectors(self):
        for banks, start in ((1, 0xc000), (2, 0xc000)):
            image = cartridge(bytes.fromhex('a92a 8500 ea'), banks, start)
            code, result = self.run_nes(image, '--until-pc', '0xc004', '--inspect', '0:1')
            self.assertEqual((code, result['status'], result['a'], result['memory']['bytes']),
                             (0, 'condition_met', 42, '2a'))
        # NROM-128 maps the same PRG bank at both addresses.
        image = cartridge(bytes.fromhex('a92a'), 1)
        code, result = self.run_nes(image, '--until-pc', '0x8000', '--inspect', '0xc000:2')
        self.assertEqual((code, result['memory']['bytes']), (0, 'a92a'))

    def test_nes2_nrom_header(self):
        image = cartridge(bytes.fromhex('a92a 8500 ea'), 2, 0x8000, 1)
        image[6], image[7], image[12], image[15] = 1, 8, 2, 1
        code, result = self.run_nes(image, '--until-pc', '0x8004', '--inspect', '0:1')
        self.assertEqual((code, result['status'], result['memory']['bytes']), (0, 'condition_met', '2a'))
        for index, value in ((8, 0x10), (9, 1), (10, 1), (11, 1), (12, 1), (13, 1), (14, 1), (15, 2)):
            invalid = bytearray(image); invalid[index] = value
            code, result = self.run_nes(invalid)
            self.assertEqual((code, result['status']), (2, 'error'))

    def test_binary_arithmetic_decimal_flag_and_overflow(self):
        image = cartridge(bytes.fromhex('f8 18 a909 6901 8501 18 a97f 6901 8502 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800f', '--inspect', '1:2')
        self.assertEqual((code, result['memory']['bytes']), (0, '0a80'))
        self.assertEqual(result['flags'] & 0xc9, 0xc8)  # N,V,D set; C clear
        image = cartridge(bytes.fromhex('38 a900 e901 8503 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8007', '--inspect', '3:1')
        self.assertEqual((code, result['memory']['bytes'], result['flags'] & 0x81), (0, 'ff', 0x80))

    def test_transfers_and_register_wrap(self):
        image = cartridge(bytes.fromhex('a980 aa a8 8a 98 8610 8411 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800a', '--inspect', '0x10:2')
        self.assertEqual((code, result['a'], result['x'], result['y'], result['memory']['bytes']),
                         (0, 128, 128, 128, '8080'))
        image = cartridge(bytes.fromhex('a200 ca e8 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8003')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 255, 0x80))
        code, result = self.run_nes(image, '--until-pc', '0x8004')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 0, 0x02))

    def test_scheduled_controller_latch(self):
        image = cartridge(bytes.fromhex('a901 8d1640 a900 8d1640 ad1640 8500 ad1640 8501 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8014', '--inspect', '0:2',
                                    '--controller-event', '3:1', '--controller-event', '4:0')
        self.assertEqual((code, result['status'], result['memory']['bytes']), (0, 'condition_met', '4140'))
        self.assertEqual(result['controller_schedule'], {'scheduled': 2, 'accepted': 2, 'pending': 0})

    def test_frame_requires_completed_frame_and_no_overwrite(self):
        image = cartridge(bytes.fromhex('4c0080'))
        output = self.file.parent / 'frame.ppm'
        output.write_bytes(b'unchanged')
        code, result = self.run_nes(image, '--frame', str(output))
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', b'unchanged'))
        code, result = self.run_nes(image, '--frame', str(output), steps=40000)
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', b'unchanged'))
        output.unlink()
        code, result = self.run_nes(image, '--frame', str(output), steps=40000)
        self.assertEqual((code, result['status']), (0, 'step_limit'))
        self.assertEqual(len(output.read_bytes()), len(b'P6\n256 240\n255\n') + 256*240*3)

    def test_audio_capture_and_noninterference(self):
        # Original ROM enables a constant-volume pulse then loops.
        image = cartridge(bytes.fromhex('a940 8d1740 a901 8d1540 a9bf 8d0040 a9fd 8d0240 a908 8d0340 4c1980'))
        output = self.file.parent / 'sound.wav'
        code, captured = self.run_nes(image, '--audio', str(output), steps=20000)
        self.assertEqual((code, captured['status'], captured['audio_captured'], captured['audio_truncated']),
                         (0, 'step_limit', True, False))
        with wave.open(str(output), 'rb') as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (1, 2, 44100))
            self.assertEqual(wav.getnframes(), captured['audio_samples'])
            self.assertTrue(any(wav.readframes(wav.getnframes())))
        code, plain = self.run_nes(image, steps=20000)
        self.assertEqual(code, 0)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'apu_writes', 'ppu_frames'):
            self.assertEqual(captured[key], plain[key], key)
        sentinel = output.read_bytes()
        code, result = self.run_nes(image, '--audio', str(output), steps=20000)
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', sentinel))

    def test_video_capture_and_no_overwrite(self):
        image = cartridge(bytes.fromhex('4c0080'))
        path = self.file.parent / 'capture.y4m'
        code, result = self.run_nes(image, '--video', str(path), '--video-fps', '20',
                                    '--video-frames', '2', steps=100000)
        self.assertEqual((code, result['video']['width'], result['video']['height'],
                          result['video']['frames'], result['video']['truncated']), (0, 256, 240, 2, True))
        data = path.read_bytes()
        self.assertTrue(data.startswith(b'YUV4MPEG2 W256 H240 F20:1'))
        self.assertEqual(data.count(b'FRAME\n'), 2)
        code, plain = self.run_nes(image, steps=100000)
        self.assertEqual(code, 0)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'ppu_frames'):
            self.assertEqual(result[key], plain[key], key)
        code, result = self.run_nes(image, '--video', str(path), steps=100000)
        self.assertEqual((code, result['status'], path.read_bytes()), (2, 'error', data))
        code, result = self.run_nes(image, '--video-fps', '10')
        self.assertEqual((code, result['status']), (2, 'error'))

    def test_faults_limits_and_invalid_images(self):
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 8d1840')), '--inspect', '0:1')
        self.assertEqual((code, r['status'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 'machine_fault', 0x8002, 'unsupported_bus_write', 0x4018))
        self.assertEqual(r['memory']['bytes'], '00')
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 ad0040')), '--inspect', '0:1')
        self.assertEqual((code, r['a'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 0x55, 0x8002, 'unsupported_bus_read', 0x4000))
        code, r = self.run_nes(cartridge(b'\x02'))
        self.assertEqual((code, r['fault']['operation'], r['fault']['opcode']), (1, 'unsupported_opcode', 2))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')), '--until-pc', '0x8003')
        self.assertEqual((code, r['status'], r['steps']), (1, 'condition_not_met', 100))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')))
        self.assertEqual((code, r['status'], r['steps']), (0, 'step_limit', 100))
        for mutator in ('magic', 'mapper', 'trainer', 'nes2_extra', 'short'):
            image = cartridge(b'\xea')
            if mutator == 'magic': image[0] = 0
            if mutator == 'mapper': image[6] = 0x10
            if mutator == 'trainer': image[6] = 0x04
            if mutator == 'nes2_extra': image[7], image[10] = 0x08, 0x01
            if mutator == 'short': image.pop()
            code, result = self.run_nes(image)
            self.assertEqual((code, result['status']), (2, 'error'))
        code, result = self.run_nes(cartridge(b'\xea'), '--frame', str(self.file))
        self.assertEqual((code, result['status']), (2, 'error'))

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_start_and_repeat(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, press):
            frame = self.file.parent / (name + '.ppm')
            command = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                       '--steps', '1000000', '--timeout-ms', '20000', '--frame', str(frame)]
            if press:
                command += ['--controller-event', '500000:8', '--controller-event', '550000:0']
            p = subprocess.run(command, capture_output=True, text=True, timeout=25,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            self.assertEqual(result['status'], 'step_limit')
            self.assertEqual(result['rom_sha256'], before)
            if press:
                self.assertEqual(result['controller_schedule'], {'scheduled': 2, 'accepted': 2, 'pending': 0})
            data = frame.read_bytes()
            self.assertTrue(data.startswith(b'P6\n256 240\n255\n'))
            return result, data
        _, title = run('title', False)
        first, started = run('start-1', True)
        second, repeated = run('start-2', True)
        self.assertNotEqual(title, started)
        self.assertEqual(started, repeated)
        self.assertEqual(first, second)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_direction_changes_state(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, move):
            frame = self.file.parent / (name + '.ppm')
            cmd = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                   '--steps', '5000000', '--timeout-ms', '30000', '--inspect', '0:2048', '--frame', str(frame),
                   '--controller-event', '500000:8', '--controller-event', '550000:0']
            if move:
                cmd += ['--controller-event', '2600000:128', '--controller-event', '4500000:0']
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=35,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            self.assertEqual(result['status'], 'step_limit')
            return result, frame.read_bytes()
        neutral, neutral_frame = run('neutral', False)
        moving, moving_frame = run('moving', True)
        self.assertNotEqual(neutral['memory']['bytes'], moving['memory']['bytes'])
        self.assertNotEqual(neutral_frame, moving_frame)
        self.assertEqual(moving['controller_schedule'], {'scheduled': 4, 'accepted': 4, 'pending': 0})
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_audio_capture_noninterference(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, audio):
            frame = self.file.parent / (name + '.ppm')
            wav = self.file.parent / (name + '.wav')
            cmd = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                   '--steps', '5000000', '--timeout-ms', '30000', '--inspect', '0:2048', '--frame', str(frame),
                   '--controller-event', '500000:8', '--controller-event', '550000:0']
            if audio: cmd += ['--audio', str(wav)]
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=35,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            if audio:
                with wave.open(str(wav), 'rb') as f:
                    self.assertEqual((f.getnchannels(), f.getsampwidth(), f.getframerate()), (1, 2, 44100))
                    self.assertEqual(f.getnframes(), result['audio_samples'])
                    self.assertTrue(any(f.readframes(f.getnframes())))
            return result, frame.read_bytes()
        silent, frame_a = run('without-audio', False)
        captured, frame_b = run('with-audio', True)
        self.assertEqual(frame_a, frame_b)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'apu_writes', 'ppu_frames', 'memory'):
            self.assertEqual(silent[key], captured[key], key)
        self.assertFalse(captured['audio_truncated'])
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)


if __name__ == '__main__':
    unittest.main()
