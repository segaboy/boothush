#!/usr/bin/env python3
"""Run an explicitly requested human Mac/286 session and preserve its evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', required=True, type=Path)
    p.add_argument('--machine', required=True, choices=('mac128k', 'pc286'))
    p.add_argument('--output', required=True, type=Path, help='new session directory')
    for name in ('rom', 'floppy', 'hard-disk', 'ega-font', 'ffmpeg'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--seconds', type=int, default=120, help='guest duration, 1-300 seconds')
    p.add_argument('--scale', type=int, choices=range(1, 5), default=2)
    p.add_argument('--record-seconds', type=int, default=0, help='record first N guest seconds at 5 fps')
    p.add_argument('--mouse-edge-cycles', type=int, default=8192)
    p.add_argument('--mouse-dcd-invert', type=int, choices=range(4), default=0)
    p.add_argument('--mouse-vbl-guard', type=int, default=4096)
    p.add_argument('--inspect', action='append', default=[], help='guest RAM ADDRESS:LENGTH at stop')
    p.add_argument('--prepare-only', action='store_true', help='write plan and launch.ps1 without opening a window')
    return p


def main(argv=None):
    p = parser(); a = p.parse_args(argv)
    if not 1 <= a.seconds <= 300 or not 0 <= a.record_seconds <= a.seconds:
        p.error('seconds must be 1-300; record-seconds must be 0-seconds')
    if not 1 <= a.mouse_edge_cycles <= 1000000 or not 0 <= a.mouse_vbl_guard <= 8192:
        p.error('invalid mouse timing')
    if a.machine == 'mac128k' and (not a.rom or a.hard_disk or a.ega_font):
        p.error('Mac requires a ROM and does not support hard-disk or ega-font')
    if a.machine == 'pc286' and not (a.rom or a.floppy or a.hard_disk):
        p.error('286 requires a ROM, floppy or hard disk')
    sources = {'launcher': Path(__file__).resolve(), 'python': Path(sys.executable).resolve()}
    for name in ('emulator', 'rom', 'floppy', 'hard_disk', 'ega_font', 'ffmpeg'):
        value = getattr(a, name)
        if value:
            value = value.resolve(strict=True)
            if not value.is_file():
                p.error(name + ' must be a file')
            setattr(a, name, value); sources[name] = value
    identities = {name: {'path': str(path), 'sha256': sha(path)} for name, path in sources.items()}
    out = a.output.resolve()
    # All output is new. Never modify mounted media or an earlier session.
    out.mkdir(parents=True, exist_ok=False)
    run = out / 'session' if a.prepare_only else out
    clock = 7833600 if a.machine == 'mac128k' else 6000000
    frame = run / ('final.pbm' if a.machine == 'mac128k' else 'final.ppm')
    cmd = [str(a.emulator), '--machine', a.machine, '--live', str(a.scale),
           '--cycles', str(clock * a.seconds), '--timeout-ms', str(a.seconds * 1000 + 1000),
           '--live-key-log', str(run / 'keyboard.bhlive'), '--frame', str(frame)]
    for name in ('rom', 'floppy', 'hard_disk', 'ega_font'):
        if getattr(a, name):
            cmd += ['--' + name.replace('_', '-'), str(getattr(a, name))]
    for value in a.inspect:
        cmd += ['--inspect', value]
    if a.machine == 'mac128k':
        cmd += ['--mouse-record', str(run / 'mouse.bhmouse'),
                '--mouse-edge-cycles', str(a.mouse_edge_cycles),
                '--mouse-dcd-invert', str(a.mouse_dcd_invert), '--mouse-vbl-guard', str(a.mouse_vbl_guard)]
    if a.record_seconds:
        cmd += ['--video', str(run / 'session.y4m'), '--video-fps', '5',
                '--video-frames', str(a.record_seconds * 5)]
    report = {'status': 'prepared', 'machine': a.machine, 'command': cmd,
              'inputs': identities, 'human_verified': False, 'qualification': 'pending human observations',
              'limits': 'Host messages can be synthetic. Accepted input is not proof of guest consumption. '
                        'Video uses guest time; original media are not saved back.'}
    save(out / 'plan.json', report)
    if a.prepare_only:
        args = [str(Path(__file__).resolve()), '--emulator', str(a.emulator), '--machine', a.machine,
                '--output', str(run), '--seconds', str(a.seconds), '--scale', str(a.scale),
                '--record-seconds', str(a.record_seconds), '--mouse-edge-cycles', str(a.mouse_edge_cycles),
                '--mouse-dcd-invert', str(a.mouse_dcd_invert), '--mouse-vbl-guard', str(a.mouse_vbl_guard)]
        for name in ('rom', 'floppy', 'hard_disk', 'ega_font', 'ffmpeg'):
            if getattr(a, name):
                args += ['--' + name.replace('_', '-'), str(getattr(a, name))]
        for value in a.inspect:
            args += ['--inspect', value]
        quote = lambda value: "'" + str(value).replace("'", "''") + "'"
        # Guard the reviewed inputs before opening a visible window.
        lines = ["$ErrorActionPreference = 'Stop'"]
        for item in identities.values():
            lines += ["if ((Get-FileHash -LiteralPath " + quote(item['path']) + ").Hash.ToLowerInvariant() -ne " +
                      quote(item['sha256']) + ") { throw 'Prepared session input changed; prepare again.' }"]
        lines += ['& ' + ' '.join(map(quote, [sys.executable, '-B', *args])), 'exit $LASTEXITCODE']
        (out / 'launch.ps1').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print(json.dumps({'status': 'prepared', 'launch': str(out / 'launch.ps1')}))
        return 0
    print('Opening BootHush. Click its window to focus; close it to finish. Evidence: ' + str(out), flush=True)
    started = time.monotonic()
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 20,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        (out / 'stdout.json').write_text(result.stdout, encoding='utf-8')
        (out / 'stderr.txt').write_text(result.stderr, encoding='utf-8')
        report.update(status='completed' if result.returncode == 0 else 'emulator_error', returncode=result.returncode)
        try:
            report['guest'] = json.loads(result.stdout)
        except ValueError:
            report['status'] = 'invalid_report'
    except (OSError, subprocess.TimeoutExpired) as error:
        report.update(status='supervisor_error', error=str(error))
    report['wall_seconds'] = round(time.monotonic() - started, 3)
    report['inputs_unchanged'] = all(path.is_file() and sha(path) == identities[name]['sha256'] for name, path in sources.items())
    if not report['inputs_unchanged']:
        report['status'] = 'input_changed'
    report['conversions'] = []
    if a.ffmpeg:
        conversions = [(frame, out / 'final.png', ['-frames:v', '1'])]
        if a.record_seconds and report.get('guest', {}).get('video', {}).get('frames', 0):
            conversions.append((out / 'session.y4m', out / 'session.mp4', ['-c:v', 'libx264', '-pix_fmt', 'yuv420p']))
        for src, dst, options in conversions:
            if not src.exists():
                continue
            command = [str(a.ffmpeg), '-hide_banner', '-loglevel', 'error', '-nostdin', '-n', '-i', str(src), *options, str(dst)]
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                report['conversions'].append({'command': command, 'returncode': result.returncode, 'stderr': result.stderr})
            except (OSError, subprocess.TimeoutExpired) as error:
                report['conversions'].append({'command': command, 'error': str(error)})
    (out / 'observations.md').write_text(
        '# Human observations — incomplete\n\nTester and date: \n\n'
        'Actions actually performed: \n\nExpected and observed behavior: \n\n'
        'Focus loss, held keys, release, and exit behavior: \n\n'
        'Capture references and defects: \n\nDisposition (pass / fail / incomplete): \n\n'
        'This template is not an attestation. Complete it after testing.\n', encoding='utf-8')
    report['artifacts'] = {file.name: {'bytes': file.stat().st_size, 'sha256': sha(file)}
                           for file in sorted(out.iterdir()) if file.is_file()}
    save(out / 'session.json', report)
    print(json.dumps({'status': report['status'], 'report': str(out / 'session.json'), 'human_verified': False}))
    return 0 if report['status'] == 'completed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
