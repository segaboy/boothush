import hashlib
import os
from pathlib import Path
import subprocess
import sys
import unittest

from test_boot import BootHushTestCase
from test_pc286 import make_rom as make_pc_rom


class VideoCaptureTests(BootHushTestCase):
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


if __name__ == '__main__':
    unittest.main()
