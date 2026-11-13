#!/usr/bin/env python3
"""Assemble a local BootHush bundle from a caller-supplied FFmpeg and license."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def copy_new_or_identical(source, destination):
    if destination.exists():
        if sha256(source) != sha256(destination):
            raise ValueError(f'{destination} already exists with different contents')
        return
    created = False
    try:
        with source.open('rb') as inp, destination.open('xb') as out:
            created = True
            shutil.copyfileobj(inp, out, 1024 * 1024)
    except OSError:
        if created:
            destination.unlink(missing_ok=True)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', required=True, type=Path)
    parser.add_argument('--license', required=True, type=Path)
    parser.add_argument('--destination', required=True, type=Path, help='Existing BootHush build directory')
    args = parser.parse_args()
    try:
        source = args.ffmpeg.resolve(strict=True)
        license_file = args.license.resolve(strict=True)
        target = args.destination.resolve(strict=True)
        if not source.is_file() or not license_file.is_file() or not (target / 'boothush.exe').is_file():
            raise ValueError('supply existing FFmpeg, license, and a build directory containing boothush.exe')
        result = subprocess.run([str(source), '-version'], stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                timeout=30, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if result.returncode != 0 or not result.stdout.startswith('ffmpeg version '):
            raise ValueError('supplied executable did not identify itself as FFmpeg')
        bundle = target / 'ffmpeg'
        bundle.mkdir(exist_ok=True)
        copy_new_or_identical(source, bundle / 'ffmpeg.exe')
        copy_new_or_identical(license_file, bundle / 'LICENSE.txt')
        helper = target / 'scripts'
        helper.mkdir(exist_ok=True)
        copy_new_or_identical(Path(__file__).with_name('convert_capture.py'), helper / 'convert_capture.py')
        manifest = {'ffmpeg_version': result.stdout.splitlines()[0], 'sha256': sha256(bundle / 'ffmpeg.exe'),
                    'license_sha256': sha256(bundle / 'LICENSE.txt'),
                    'source_path': str(source), 'version_output': result.stdout}
        manifest_path = bundle / 'manifest.json'
        manifest_text = json.dumps(manifest, indent=2) + '\n'
        if manifest_path.exists():
            if manifest_path.read_text(encoding='utf-8') != manifest_text:
                raise ValueError('existing FFmpeg manifest differs; assemble in a new build directory')
        else:
            with manifest_path.open('x', encoding='utf-8', newline='\n') as stream:
                stream.write(manifest_text)
        print(json.dumps({'status': 'bundled', 'ffmpeg': str(bundle / 'ffmpeg.exe'),
                          'sha256': manifest['sha256'], 'version': manifest['ffmpeg_version']}))
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f'Cannot bundle FFmpeg: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
