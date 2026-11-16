#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Dean Howell.
"""Generate original diagnostic lettering for --ega-font, not a BIOS font."""
import argparse
import hashlib
import json
from pathlib import Path

# Independently drawn 5x7 outlines. Lowercase deliberately shares capitals.
ROWS={
 'A':[14,17,17,31,17,17,17], 'B':[30,17,17,30,17,17,30],
 'C':[14,17,16,16,16,17,14], 'D':[30,17,17,17,17,17,30],
 'E':[31,16,16,30,16,16,31], 'F':[31,16,16,30,16,16,16],
 'G':[14,17,16,23,17,17,15], 'H':[17,17,17,31,17,17,17],
 'I':[14,4,4,4,4,4,14], 'J':[7,2,2,2,18,18,12],
 'K':[17,18,20,24,20,18,17], 'L':[16,16,16,16,16,16,31],
 'M':[17,27,21,21,17,17,17], 'N':[17,25,21,19,17,17,17],
 'O':[14,17,17,17,17,17,14], 'P':[30,17,17,30,16,16,16],
 'Q':[14,17,17,17,21,18,13], 'R':[30,17,17,30,20,18,17],
 'S':[15,16,16,14,1,1,30], 'T':[31,4,4,4,4,4,4],
 'U':[17,17,17,17,17,17,14], 'V':[17,17,17,17,17,10,4],
 'W':[17,17,17,21,21,21,10], 'X':[17,17,10,4,10,17,17],
 'Y':[17,17,10,4,4,4,4], 'Z':[31,1,2,4,8,16,31],
 '0':[14,17,19,21,25,17,14], '1':[4,12,4,4,4,4,14],
 '2':[14,17,1,2,4,8,31], '3':[30,1,1,14,1,1,30],
 '4':[2,6,10,18,31,2,2], '5':[31,16,16,30,1,1,30],
 '6':[14,16,16,30,17,17,14], '7':[31,1,2,4,8,8,8],
 '8':[14,17,17,14,17,17,14], '9':[14,17,17,15,1,1,14],
 ' ':[0]*7, '.':[0,0,0,0,0,6,6], ',':[0,0,0,0,6,6,4],
 ':':[0,6,6,0,6,6,0], ';':[0,6,6,0,6,6,4], '-':[0,0,0,31,0,0,0],
 '(':[2,4,8,8,8,4,2], ')':[8,4,2,2,2,4,8],
 '/':[1,2,2,4,8,8,16], '\\':[16,8,8,4,2,2,1],
 '=':[0,0,31,0,31,0,0], '!':[4,4,4,4,4,0,4],
 '?':[14,17,1,2,4,0,4], '+':[0,4,4,31,4,4,0],
 '_':[0,0,0,0,0,0,31], "'":[4,4,2,0,0,0,0],
 '"':[10,10,5,0,0,0,0], '[':[14,8,8,8,8,8,14], ']':[14,2,2,2,2,2,14],
}
def font_bytes():
    output=bytearray()
    for code in range(256):
        rows=ROWS.get(chr(code).upper(),[31,17,17,17,17,17,31])
        for row in rows:output.extend((row<<2,row<<2))
    return bytes(output)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    data=font_bytes();(out/'diagnostic-8x14.bin').write_bytes(data)
    report={'format':'256 glyphs, 14 rows, bit 7 leftmost','bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'license':'GPL-3.0-or-later','origin':'Original independently drawn diagnostic lettering; no firmware font',
            'limits':'Lowercase uses capitals; unsupported characters use a box. Not CP437 or historical EGA evidence.'}
    (out/'font.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'font':str(out/'diagnostic-8x14.bin'),'sha256':report['sha256']}))
if __name__=='__main__':main()
