#!/usr/bin/env python3
"""Rebuild the CLI using pinned existing core objects and fresh main.c twins.

This narrowly scoped experiment does not rebuild or alter any x64 work. The
existing base libraries are inputs, not a source-complete distribution build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BASE_EXE_SHA = '5d4f610e98cbb8b03476f28929d6e5c0f64b941aafac3c7d5b6ef12c76c7eeb3'
OBJECTS = ('CMakeFiles/boothush.dir/src/pc286.cpp.obj',
           'CMakeFiles/boothush.dir/src/video_capture.c.obj',
           'CMakeFiles/boothush.dir/src/media_convert.c.obj')
LIBS = ('libboothush_mac.a', 'libi286.a', 'libboothush_x64_media.a',
        'libboothush_nes_core.a', 'libmoira.a', 'libboothush_devices.a')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--base', required=True, type=Path)
    p.add_argument('--compiler-bin', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    base = a.base.resolve(strict=True)
    compiler = a.compiler_bin.resolve(strict=True)
    out = a.output.resolve()
    if out == ROOT or ROOT in out.parents:
        raise ValueError('output must be outside the repository')
    if sha(base/'boothush.exe') != BASE_EXE_SHA:
        raise ValueError('unexpected base executable identity')
    clang, clangxx = compiler/'clang.exe', compiler/'clang++.exe'
    inputs = [ROOT/'src/main.c', Path(__file__), clang, clangxx, base/'boothush.exe',
              *(base/item for item in (*OBJECTS, *LIBS))]
    before = {str(path): sha(path) for path in inputs}
    out.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env['PATH'] = str(compiler) + os.pathsep + env.get('PATH', '')
    env['SOURCE_DATE_EPOCH'] = '0'
    commands, executables = [], []
    for name in ('a', 'b'):
        folder = out/name
        folder.mkdir()
        obj, exe = folder/'main.c.obj', folder/'boothush.exe'
        compile_cmd = [clang, '-O3', '-DNDEBUG', '-Wall', '-Wextra', '-Wpedantic',
                       '-I', ROOT/'src', '-isystem', ROOT/'third_party/moira',
                       '-isystem', ROOT/'third_party/mame-i286', '-c', inputs[0], '-o', obj]
        link_cmd = [clangxx, '-O3', '-DNDEBUG', '-municode', '-static', '-Wl,--no-insert-timestamp', obj,
                    *(base/item for item in OBJECTS), *(base/item for item in LIBS),
                    '-luser32', '-lgdi32', '-lbcrypt', '-lkernel32', '-lwinspool',
                    '-lshell32', '-lole32', '-loleaut32', '-luuid', '-lcomdlg32',
                    '-ladvapi32', '-o', exe]
        for label, command in (('compile', compile_cmd), ('link', link_cmd)):
            args = [str(item) for item in command]
            result = subprocess.run(args, capture_output=True, text=True, timeout=90, env=env,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            (folder/(label+'.log')).write_text(result.stdout+'\n--- stderr ---\n'+result.stderr, encoding='utf-8')
            if result.returncode:
                raise RuntimeError(label+' failed; see '+str(folder/(label+'.log')))
            commands.append(args)
        executables.append(exe)
    if sha(executables[0]) != sha(executables[1]):
        raise ValueError('CLI twin builds differ')
    if before != {str(path): sha(path) for path in inputs}:
        raise ValueError('build input changed')
    report = {'status': 'pass', 'base_executable_sha256': BASE_EXE_SHA,
              'input_sha256': before, 'commands': commands, 'source_date_epoch': env['SOURCE_DATE_EPOCH'],
              'executable_sha256': sha(executables[0]),
              'limits': 'Fresh CLI objects linked with existing recorded build objects; not a source-complete rebuild or distribution qualification.'}
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'status': 'pass', 'executable_sha256': report['executable_sha256'],
                      'report': str(out/'report.json')}))


if __name__ == '__main__':
    main()
