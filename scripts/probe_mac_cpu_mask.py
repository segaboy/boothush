#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Dean Howell.
"""Compare bounded original-ROM mouse runs with and without CPU-mask tracing.

Supply a reviewed ROM hash and a schedule known to follow that guest's readiness.
This reports emulator instruction-boundary observations, not hardware latency.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(path):
    header, fields, *lines = path.read_text(encoding='ascii').splitlines()
    h = header.split()
    if h[0] != 'BHMACMOUSE2' or h[5:8] != ['0', '1', '1']:
        raise ValueError('witness range incomplete or rows dropped: ' + header)
    names = fields.split()
    rows = [dict(zip(names, line.split())) for line in lines]
    if len(rows) != int(h[4]):
        raise ValueError('witness row count mismatch')
    masks = [r for r in rows if r['kind'] == 'cpu_mask']
    if not masks or int(masks[0]['reg']) != 8:
        raise ValueError('missing initial CPU-mask observation')
    spans = []
    for index, (a, b) in enumerate(zip(masks, masks[1:])):
        if a['value'] != b['reg']:
            raise ValueError('broken CPU-mask observation chain')
        spans.append({'mask': int(a['value']), 'start': int(a['cycle']),
                      'end': int(b['cycle']), 'cycles': int(b['cycle']) - int(a['cycle']),
                      'entry_address': int(a['address']), 'exit_address': int(b['address']),
                      'left_censored': index == 0})
    complete = [s for s in spans if not s['left_censored']]
    maxima = {str(mask): max(s['cycles'] for s in complete if s['mask'] == mask)
              for mask in sorted({s['mask'] for s in complete})}
    return {'header': header, 'sha256': sha(path), 'mask_observations': masks,
            'closed_spans': spans, 'complete_max_cycles_by_mask': maxima,
            'right_censored': {'start': int(masks[-1]['cycle']), 'mask': int(masks[-1]['value'])},
            'event_counts': dict(Counter(r['kind'] for r in rows))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--rom-sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--input-cycle', type=int, required=True)
    p.add_argument('--stop-cycle', type=int, required=True)
    p.add_argument('--trace-last', type=int, required=True)
    p.add_argument('--edge-cycles', type=int, nargs='+', default=[2048, 4096])
    p.add_argument('--guard', type=int, default=4096)
    p.add_argument('--inspect', default='0xb00:1024')
    p.add_argument('--motion', default='32:0')
    a = p.parse_args()
    if not 1000 <= a.input_cycle < a.trace_last <= a.stop_cycle <= 1000000000:
        p.error('require 1000 <= input < trace-last <= stop <= 1000000000')
    if len(set(a.edge_cycles)) != len(a.edge_cycles) or min(a.edge_cycles) <= 0:
        p.error('edge intervals must be positive and distinct')
    identities = {'rom': sha(a.rom), 'emulator': sha(a.emulator), 'script': sha(Path(__file__))}
    if identities['rom'] != a.rom_sha256.lower():
        p.error('ROM hash differs from reviewed input')
    a.output.mkdir(parents=True, exist_ok=False)
    cases = []
    for edge in a.edge_cycles:
        states, frames, commands = [], [], []
        witness = a.output / f'{edge}.trace'
        for traced in (False, True):
            name = f'{edge}-' + ('traced' if traced else 'plain')
            frame = a.output / (name + '.pbm')
            cmd = [str(a.emulator), '--machine', 'mac128k', '--rom', str(a.rom),
                   '--cycles', str(a.stop_cycle), '--timeout-ms', '30000',
                   '--mouse-motion', f'{a.input_cycle}:{a.motion}', '--mouse-dcd-invert', '3',
                   '--mouse-edge-cycles', str(edge), '--mouse-vbl-guard', str(a.guard),
                   '--inspect', a.inspect, '--frame', str(frame)]
            if traced:
                cmd += ['--mouse-witness', str(witness), '--mouse-witness-detail', '1',
                        '--mouse-witness-range', f'{a.input_cycle-1000}:{a.trace_last}']
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=35,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            state = json.loads(result.stdout)
            if state['status'] != 'cycle_limit' or state['mouse']['pending']:
                raise ValueError('run did not complete scheduled motion')
            (a.output / (name + '.json')).write_text(result.stdout, encoding='utf-8')
            states.append(state); frames.append(frame.read_bytes()); commands.append(cmd)
        if states[0] != states[1] or frames[0] != frames[1]:
            raise ValueError('tracing changed guest state or full framebuffer')
        cases.append({'edge_cycles': edge, 'commands': commands, 'state': states[0],
                      'traced_matches_plain': True,
                      'frame_sha256': hashlib.sha256(frames[0]).hexdigest(),
                      'witness': summarize(witness)})
    if identities != {'rom': sha(a.rom), 'emulator': sha(a.emulator), 'script': sha(Path(__file__))}:
        raise ValueError('input changed during comparison')
    report = {'identities': identities, 'cases': cases,
              'limits': 'Instruction boundaries in BootHush; first and final spans censored. '
                        'Address names require matching guest build. Not physical timing or release qualification.'}
    (a.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': 'pass', 'cases': len(cases), 'report': str(a.output / 'report.json')}))


if __name__ == '__main__':
    main()
