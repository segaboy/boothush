import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from test_boot import BootHushTestCase, make_rom
from test_pc286 import make_rom as make_pc_rom
from test_mouse_cli import make_mouse_probe_rom, VALID_TRACE


class VideoCaptureTests(BootHushTestCase):
    def test_mouse_replay_frame_series_is_noninvasive(self):
        self.rom.write_bytes(make_mouse_probe_rom())
        trace = self.scratch / 'mouse.bhmouse'
        trace.write_bytes(VALID_TRACE)
        before = trace.read_bytes()
        plain = self.boot('--mouse-replay', trace, '--inspect', '0xb00:8', cycles=500000)
        def capture(path):
            return self.boot('--mouse-replay', trace, '--inspect', '0xb00:8',
                             '--capture-series', path, '--capture-memory', '0xb00:8',
                             '--capture-cycle', '130240', '--capture-cycle', '150000',
                             '--capture-cycle', '470000', '--capture-cycle', '480000',
                             cycles=500000)
        one = self.scratch / 'one.pbm'; two = self.scratch / 'two.pbm'
        first = capture(one); second = capture(two)
        self.assertEqual(first, second)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        series = first.pop('frame_series'); first.pop('series_inputs')
        self.assertEqual(first, plain)
        self.assertEqual((series['processed'], series['pending']), (3, 1))
        samples = series['samples']
        self.assertEqual(samples[0]['replay_samples_queued_before'], 0)
        self.assertEqual(samples[1]['replay_samples_queued_before'], 3)
        self.assertEqual(samples[2]['replay_samples_queued_before'], 3)
        self.assertEqual(samples[1]['inputs_accepted_before'], 0)
        self.assertEqual(trace.read_bytes(), before)
        unchanged = one.read_bytes()
        self.boot('--mouse-replay', trace, '--capture-series', one,
                  '--capture-cycle', '1', cycles=500000, exit_code=2)
        self.assertEqual(one.read_bytes(), unchanged)
        vbl = self.boot('--mouse-replay', trace, '--capture-series', self.scratch / 'vbl-replay.pbm',
                        '--capture-vbl', '1', '--capture-vbl', '2', cycles=500000)
        self.assertEqual([s['frame'] for s in vbl['frame_series']['samples']], [1, 2])
        self.boot('--mouse-replay', trace, '--input-after', '0xb00:4:0x1234abcd',
                  '--capture-series', self.scratch / 'invalid.pbm', '--capture-cycle', '1',
                  cycles=500000, exit_code=2)

    def test_mac_display_witness_vbl_framebuffer_and_fault(self):
        trace=self.scratch/'display.txt';series=self.scratch/'display.pbm'
        plain=self.boot(cycles=280000)
        result=self.boot('--display-witness',trace,'--display-witness-memory','0x1a700:1',
                         '--capture-series',series,'--capture-vbl','1','--capture-vbl','2',
                         '--capture-memory','0x1a700:1',cycles=280000)
        info=result.pop('display_witness');frames=result.pop('frame_series');result.pop('series_inputs')
        self.assertEqual(result,plain);self.assertEqual(info['dropped'],0)
        rows=[x.split() for x in trace.read_text().splitlines()[2:]]
        self.assertTrue(any(r[3]=='write' and int(r[4])==0x1a700 and int(r[5])==0x80 for r in rows))
        vbl=[r for r in rows if r[3]=='vbl'];self.assertGreaterEqual(len(vbl),2)
        for sample,row in zip(frames['samples'],vbl):
            self.assertEqual(sample['frame'],int(row[1]));self.assertGreaterEqual(sample['cycle'],int(row[0]))
            self.assertEqual(sample['memory']['bytes'],'80')
        before=trace.read_bytes()
        self.boot('--display-witness',trace,cycles=2000,exit_code=2);self.assertEqual(trace.read_bytes(),before)
        self.rom.write_bytes(make_rom(bytes.fromhex('13fc000100800000 60fe')))
        fault=self.scratch/'fault.txt';r=self.boot('--display-witness',fault,exit_code=1)
        self.assertEqual(r['status'],'machine_fault');self.assertIn(' fault ',fault.read_text())
        self.boot('--display-witness-memory','0x1a700:1',exit_code=2)

    def test_display_witness_reports_overflow(self):
        self.rom.write_bytes(make_rom(bytes.fromhex('13fc000000effffe 13fc001000efe7fe 13fc00800001a700 60f6')))
        path=self.scratch/'full-display.txt'
        r=self.boot('--display-witness',path,'--display-witness-memory','0x1a700:1',cycles=1000000)
        self.assertEqual(r['display_witness']['events'],8192)
        self.assertGreater(r['display_witness']['dropped'],0)

    def test_mac_frame_series_cycles_vbl_and_readiness(self):
        path=self.scratch/'series.pbm'; repeated=self.scratch/'repeat.pbm'
        def run(p):
            return self.boot('--capture-series',p,'--capture-cycle','0','--capture-cycle','100',
                             '--capture-cycle','100000','--capture-cycle','280000',
                             '--capture-memory','0xb00:4','--key','100:0:1',cycles=280000)
        first=run(path); second=run(repeated)
        self.assertEqual(first,second);self.assertEqual(path.read_bytes(),repeated.read_bytes())
        series=first['frame_series']; samples=series['samples']; payload=path.read_bytes()
        self.assertEqual(series['rom_sha256'],hashlib.sha256(self.rom.read_bytes()).hexdigest())
        self.assertEqual(series['executable_sha256'],hashlib.sha256(self.exe.read_bytes()).hexdigest())
        self.assertEqual((series['requested'],series['processed'],series['pending']),(4,4,0))
        self.assertTrue(series['written']);self.assertEqual(samples[0]['memory']['bytes'],'00000000')
        self.assertEqual(samples[2]['memory']['bytes'],'1234abcd')
        self.assertEqual(samples[1]['inputs_accepted_before'],0)
        self.assertEqual(samples[2]['inputs_accepted_before'],1)
        self.assertEqual(first['series_inputs']['events'][0]['kind'],'key')
        self.assertEqual(first['series_inputs']['events'][0]['requested'],100)
        for sample in samples:
            self.assertGreaterEqual(sample['cycle'],sample['target'])
            image=payload[sample['offset']:sample['offset']+sample['length']]
            self.assertEqual(len(image),len(b'P4\n512 342\n')+64*342)
            self.assertTrue(image.startswith(b'P4\n512 342\n'))
        manifest=self.scratch/'result.json';manifest.write_text(json.dumps(first),encoding='utf-8')
        destination=self.scratch/'extracted'
        helper=Path(__file__).resolve().parents[1]/'scripts'/'extract_frame_series.py'
        cmd=[sys.executable,helper,'--result',manifest,'--series',path,'--output',destination]
        extraction=subprocess.run(cmd,capture_output=True,text=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(extraction.returncode,0,extraction.stdout+extraction.stderr)
        self.assertEqual(len(list(destination.glob('*.pbm'))),4)
        self.assertEqual(json.loads((destination/'manifest.json').read_text()),first)
        self.assertNotEqual(subprocess.run(cmd,capture_output=True).returncode,0)
        corrupt=self.scratch/'corrupt.pbm';corrupt.write_bytes(b'X'+path.read_bytes()[1:])
        bad_dest=self.scratch/'corrupt-output'
        bad=subprocess.run([sys.executable,helper,'--result',manifest,'--series',corrupt,'--output',bad_dest],capture_output=True)
        self.assertNotEqual(bad.returncode,0);self.assertFalse(bad_dest.exists())
        before=path.read_bytes();self.boot('--capture-series',path,'--capture-cycle','0',exit_code=2)
        self.assertEqual(path.read_bytes(),before)
        vbl=self.boot('--capture-series',self.scratch/'vbl.pbm','--capture-vbl','1',
                      '--capture-vbl','2',cycles=280000)['frame_series']
        self.assertEqual([x['frame'] for x in vbl['samples']],[1,2])
        rel=self.boot('--input-after','0xb00:4:0x1234abcd','--capture-series',self.scratch/'relative.pbm',
                      '--capture-cycle','0','--capture-memory','0xb00:4',cycles=2000)
        self.assertTrue(rel['frame_series']['relative_to_readiness'])
        self.assertEqual(rel['frame_series']['samples'][0]['cycle'],rel['input_schedule']['ready_cycle'])
        self.assertEqual(rel['frame_series']['samples'][0]['memory']['bytes'],'1234abcd')
        simultaneous=self.boot('--input-after','0xb00:4:0x1234abcd','--until','0xb00:4:0x1234abcd',
                               '--key','0:0:1','--capture-series',self.scratch/'stop.pbm','--capture-cycle','0')
        self.assertEqual(simultaneous['frame_series']['samples'][0]['inputs_accepted_before'],0)
        self.assertFalse(simultaneous['series_inputs']['events'][0]['accepted'])
        absent_path=self.scratch/'no-ready.pbm'
        absent=self.boot('--input-after','0xb00:4:0xdeadbeef','--capture-series',absent_path,
                         '--capture-cycle','0',cycles=2000,exit_code=1)
        self.assertEqual((absent['status'],absent['frame_series']['processed'],absent['frame_series']['pending']),('readiness_not_reached',0,1))
        self.assertEqual(absent_path.read_bytes(),b'')
        for args in (('--capture-series',self.scratch/'bad'),('--capture-cycle','1'),
                     ('--capture-series',self.scratch/'bad','--capture-cycle','1','--capture-vbl','1'),
                     ('--capture-series',self.scratch/'bad','--capture-cycle','1','--capture-memory','0x1ffff:2'),
                     ('--capture-series',self.scratch/'bad',*(('--capture-cycle','1')*33))):
            with self.subTest(args=args): self.boot(*args,exit_code=2)

    def read_y4m(self, path, width, height):
        data = path.read_bytes()
        header, data = data.split(b'\n', 1)
        self.assertEqual(header, f'YUV4MPEG2 W{width} H{height} F60:1 Ip A1:1 C444'.encode())
        size = width * height * 3
        self.assertEqual(len(data) % (size + 6), 0)
        frames = []
        while data:
            self.assertEqual(data[:6], b'FRAME\n')
            frames.append(data[6:6 + size])
            data = data[size + 6:]
        return frames

    def test_mac_video_preserves_guest_and_captures_change(self):
        path = self.scratch / 'test.y4m'
        args = ('--inspect', '0xb00:4')
        plain = self.boot(*args, cycles=280000)
        result = self.boot(*args, '--video', path, '--video-fps', '60', cycles=280000)
        video = result.pop('video')
        self.assertEqual(result, plain)
        self.assertEqual(video['frames'], 3)
        self.assertFalse(video['truncated'])
        frames = self.read_y4m(path, 512, 342)
        self.assertEqual(len(frames), 3)
        self.assertEqual(frames[0][0], 235)
        self.assertEqual(frames[1][0], 16)
        self.assertEqual(frames[1][512 * 342], 128)
        repeat = self.scratch / 'repeat.y4m'
        self.boot(*args, '--video', repeat, '--video-fps', '60', cycles=280000)
        self.assertEqual(hashlib.sha256(path.read_bytes()).digest(), hashlib.sha256(repeat.read_bytes()).digest())

    def test_start_cap_and_existing_file(self):
        path = self.scratch / 'cap.y4m'
        result = self.boot('--video', path, '--video-fps', '60', '--video-frames', '1',
                           '--video-start', '1000', cycles=280000)
        self.assertEqual(result['video']['frames'], 1)
        self.assertTrue(result['video']['truncated'])
        self.assertGreaterEqual(result['video']['last_cycle'], 1000)
        self.assertEqual(len(self.read_y4m(path, 512, 342)), 1)
        before = path.read_bytes()
        self.boot('--video', path, cycles=2000, exit_code=2)
        self.assertEqual(path.read_bytes(), before)
        for args in (('--video-fps', '5'), ('--video', self.scratch / 'x', '--video-fps', '0'),
                     ('--video', self.scratch / 'x', '--video-frames', '3601')):
            with self.subTest(args=args):
                self.boot(*args, exit_code=2)

    def test_pc_headless_video(self):
        rom = self.scratch / 'pc.rom'
        rom.write_bytes(make_pc_rom(bytes.fromhex('ebfe')))
        video = self.scratch / 'pc.y4m'
        cmd = [self.exe, '--machine', 'pc286', '--rom', rom, '--cycles', '220000', '--timeout-ms', '10000',
               '--video', video, '--video-fps', '60']
        result = subprocess.run(cmd, capture_output=True, text=True,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        frames = self.read_y4m(video, 640, 350)
        self.assertEqual(len(frames), 3)
        self.assertEqual(frames[0][0], 16)

    def test_pc_color_planes_are_recorded(self):
        image = bytearray(80 * 2 * 15 * 512)
        program = bytes.fromhex(
            'b81000 cd10 '  # EGA 640x350 mode.
            'bac403 b80201 ef '  # Sequencer map mask: plane 0 only.
            'b800a0 8ec0 26c606480180 '  # Write one colored pixel.
            'ebfe')
        image[:len(program)] = program
        image[510:512] = b'\x55\xaa'
        floppy = self.scratch / 'color.img'
        floppy.write_bytes(image)
        video, still = self.scratch / 'color.y4m', self.scratch / 'color.ppm'
        cmd = [self.exe, '--machine', 'pc286', '--floppy', floppy, '--cycles', '300000',
               '--timeout-ms', '10000', '--video', video, '--video-fps', '60',
               '--video-start', '100000', '--frame', still]
        result = subprocess.run(cmd, capture_output=True, text=True,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        frames = self.read_y4m(video, 640, 350)
        pixel = 4 * 640 + 64
        ppm = still.read_bytes().split(b'\n', 3)[3]
        r, g, b = ppm[pixel * 3:pixel * 3 + 3]
        self.assertNotEqual((r, g, b), (0, 0, 0))
        self.assertFalse(r == g == b)
        size = 640 * 350
        self.assertNotEqual((frames[0][size + pixel], frames[0][size * 2 + pixel]), (128, 128))

    @unittest.skipUnless(os.environ.get('BOOTHUSH_TEST_FFMPEG'), 'local FFmpeg not supplied')
    def test_conversion_to_gif_mp4_and_png(self):
        video = self.scratch / 'video.y4m'
        frame = self.scratch / 'still.pbm'
        self.boot('--video', video, '--video-fps', '60', '--frame', frame, cycles=280000)
        converter = Path(__file__).resolve().parents[1] / 'scripts' / 'convert_capture.py'
        ffmpeg = os.environ['BOOTHUSH_TEST_FFMPEG']
        for ext in ('gif', 'mp4', 'png'):
            output = self.scratch / f'capture.{ext}'
            source = frame if ext == 'png' else video
            cmd = [sys.executable, converter, '--ffmpeg', ffmpeg, '--input', source, '--output', output]
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            before = output.read_bytes()
            self.assertGreater(len(before), 100)
            decode = subprocess.run([ffmpeg, '-v', 'error', '-i', output, '-f', 'null', '-'],
                                    capture_output=True, text=True,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(decode.returncode, 0, decode.stderr)
            pixels = subprocess.run([ffmpeg, '-v', 'error', '-i', output, '-fps_mode', 'passthrough',
                                     '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1'],
                                    capture_output=True,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(pixels.returncode, 0, pixels.stderr)
            frame_size = 512 * 342 * 3
            self.assertEqual(len(pixels.stdout), frame_size * (1 if ext == 'png' else 3))
            if ext == 'gif':
                self.assertGreater(min(pixels.stdout[:3]), 240)
                self.assertLess(max(pixels.stdout[frame_size:frame_size + 3]), 16)
            if ext == 'png':
                self.assertEqual(pixels.stdout[:3], bytes(3))
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 2)
            self.assertEqual(output.read_bytes(), before)

    def test_bundled_native_conversion_without_path(self):
        bundled = self.exe.parent / 'ffmpeg' / 'ffmpeg.exe'
        if not bundled.is_file():
            self.skipTest('bundled FFmpeg not supplied')
        video = self.scratch / 'native.y4m'
        still = self.scratch / 'still.pbm'
        self.boot('--video', video, '--video-fps', '60', '--frame', still, cycles=280000)
        env = os.environ.copy()
        env['PATH'] = str(self.scratch)  # Do not use a system FFmpeg.
        for ext in ('gif', 'mp4', 'png'):
            output = self.scratch / f'guest space & ü.{ext}'
            source = still if ext == 'png' else video
            cmd = [self.exe, '--convert-capture', source, '--output', output]
            result = subprocess.run(cmd, capture_output=True, text=True, env=env,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout), {'status': 'capture_converted', 'format': ext})
            before = output.read_bytes()
            self.assertGreater(len(before), 100)
            decoded = subprocess.run([bundled, '-v', 'error', '-i', output, '-f', 'null', '-'],
                                     capture_output=True, env=env,
                                     creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(decoded.returncode, 0, decoded.stderr)
            repeated = subprocess.run(cmd, capture_output=True, text=True, env=env,
                                      creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(repeated.returncode, 2)
            self.assertEqual(output.read_bytes(), before)
        self.assertFalse(list(self.scratch.glob('*.boothush-*.tmp')))
        bad = subprocess.run([self.exe, '--convert-capture', video, '--output', self.scratch / 'x.bad'],
                             capture_output=True, text=True, env=env,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(bad.returncode, 2)
        invalid = self.scratch / 'invalid.y4m'
        invalid.write_bytes(b'not a video')
        failed_output = self.scratch / 'invalid.mp4'
        failed = subprocess.run([self.exe, '--convert-capture', invalid, '--output', failed_output],
                                capture_output=True, text=True, env=env,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(failed.returncode, 2)
        self.assertFalse(failed_output.exists())
        self.assertFalse(list(self.scratch.glob('*.boothush-*.tmp')))


if __name__ == '__main__':
    unittest.main()
