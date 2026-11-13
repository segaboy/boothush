#!/usr/bin/env python3
"""Extract BootHush frame-series images into a new directory."""
import argparse
import json
from pathlib import Path

FORMATS = {
    'concatenated_ppm': (b'P6\n256 240\n255\n', 256 * 240 * 3, 'ppm'),
    'concatenated_pbm': (b'P4\n512 342\n', 64 * 342, 'pbm'),
}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--result',required=True,type=Path,help='JSON stdout from the original BootHush run')
    p.add_argument('--series',required=True,type=Path,help='frame series file from that run')
    p.add_argument('--output',required=True,type=Path,help='new output directory')
    a=p.parse_args()
    result=json.loads(a.result.read_text(encoding='utf-8-sig'))
    info=result['frame_series']
    if not info['written'] or info['format'] not in FORMATS:
        raise ValueError('series was not written or has an unsupported format')
    header,size,extension=FORMATS[info['format']]
    if not 0 <= info['requested'] <= 32 or a.series.stat().st_size>32*(size+len(header)):
        raise ValueError('series exceeds capture limits')
    data=a.series.read_bytes()
    if len(data)!=info['bytes'] or len(info['samples'])!=info['requested']:
        raise ValueError('series size or sample count does not match the manifest')
    offset=0;images=[]
    for i,sample in enumerate(info['samples']):
        if not sample['processed'] or not sample.get('image_available',False):
            continue
        length=sample['length']
        if sample['offset']!=offset or length!=len(header)+size or data[offset:offset+len(header)]!=header:
            raise ValueError(f'invalid frame {i} offset, length, or header')
        images.append((i,data[offset:offset+length]));offset+=length
    if offset!=len(data):
        raise ValueError('series has unreferenced or missing bytes')
    a.output.mkdir(parents=True,exist_ok=False)
    for i,image in images:
        with (a.output/f'frame-{i:03d}.{extension}').open('xb') as file:
            file.write(image)
    with (a.output/'manifest.json').open('x',encoding='utf-8',newline='\n') as file:
        json.dump(result,file,indent=2);file.write('\n')
    print(f'Extracted {len(images)} images; {info["pending"]} capture points pending.')

if __name__=='__main__':
    main()
