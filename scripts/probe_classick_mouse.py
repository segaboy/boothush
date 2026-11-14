#!/usr/bin/env python3
"""Compare traced and untraced synthetic mouse runs on the pinned ClasSICK ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

DIGEST='b3a50ad806b589fdc02a4de579e6d72700c9107fb273d9ca73026320bebed807'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator',required=True,type=Path);p.add_argument('--rom',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path,help='new output directory')
    a=p.parse_args()
    if sha(a.rom)!=DIGEST:raise ValueError('unexpected fixture ROM hash')
    a.output.mkdir(parents=True,exist_ok=False)
    cases=[(edge,4096,dx,dy) for edge in (2048,4096) for dx,dy in ((32,0),(-32,0),(0,32),(0,-32))]
    cases.append((8192,0,32,0)); results=[]
    for i,(edge,guard,dx,dy) in enumerate(cases):
        common=[str(a.emulator),'--machine','mac128k','--rom',str(a.rom),'--cycles','65000000',
                '--timeout-ms','30000','--mouse-dcd-invert','3','--mouse-motion',f'60000000:{dx}:{dy}',
                '--mouse-edge-cycles',str(edge),'--mouse-vbl-guard',str(guard),'--inspect','0xb00:1024']
        records=[]
        for suffix in ('plain','traced'):
            frame=a.output/f'{i}-{suffix}.pbm';trace=a.output/f'{i}.trace'
            command=common+['--frame',str(frame)]
            if suffix=='traced':command+=['--mouse-witness',str(trace),'--mouse-witness-detail','1',
                                          '--mouse-witness-memory','0xc1c:4','--mouse-witness-range','60000000:65000000']
            r=subprocess.run(command,capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            records.append((json.loads(r.stdout),frame.read_bytes()))
        if records[0]!=records[1]:raise RuntimeError(f'witness changed guest output in case {i}')
        header=(a.output/f'{i}.trace').read_text().splitlines()[0].split()
        if header[0]!='BHMACMOUSE3' or header[5]!='0' or header[7]!='1':raise RuntimeError(f'incomplete witness in case {i}: {header}')
        result=records[0][0];data=bytes.fromhex(result['memory']['bytes'])
        def word(addr):return int.from_bytes(data[addr-0xb00:addr-0xb00+4],'big')
        results.append({'edge':edge,'guard':guard,'delta':[dx,dy],'pointer':[word(0xc5c),word(0xc60)],
                        'omissions':word(0xc1c),'mouse':result['mouse'],'trace_count':int(header[4]),
                        'trace_dropped':int(header[5]),'trace_range_reached':bool(int(header[7])),
                        'trace_ram_writes':int(header[13]),'trace_ram_dropped':int(header[14]),
                        'traced_matches_untraced':True})
    if sha(a.rom)!=DIGEST:raise RuntimeError('ROM changed')
    report={'rom_sha256':DIGEST,'emulator_sha256':sha(a.emulator),'cases':results,
            'scope':'Modeled synthetic input; no physical timing or fault attribution.'}
    (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
