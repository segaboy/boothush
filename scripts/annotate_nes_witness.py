#!/usr/bin/env python3
"""Attach caller-supplied, ROM-hash-bound PC labels to a BHNESCPU1 trace."""
import argparse
import json
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace',required=True,type=Path)
    parser.add_argument('--symbols',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path,help='new JSON file')
    args=parser.parse_args()
    if args.trace.stat().st_size>16000000 or args.symbols.stat().st_size>4000000:
        raise ValueError('input exceeds size limit')
    symbols=json.loads(args.symbols.read_text(encoding='utf-8'))
    lines=args.trace.read_text(encoding='ascii').splitlines()
    if len(lines)<2 or len(lines[0].split())!=8 or lines[0].split()[0]!='BHNESCPU1':
        raise ValueError('invalid trace header')
    header=lines[0].split();digest=header[7]
    if symbols.get('rom_sha256')!=digest:
        raise ValueError('symbol ROM identity does not match trace')
    entries=symbols.get('symbols')
    if not isinstance(entries,list) or len(entries)>100000:
        raise ValueError('invalid symbol list')
    index={}
    for entry in entries:
        name=entry.get('name');address=entry.get('address');bank=entry.get('bank')
        if not isinstance(name,str) or not name or len(name)>256 or type(address)!=int or not 0<=address<=65535:
            raise ValueError('invalid symbol')
        if bank is not None and (type(bank)!=int or not 0<=bank<4294967295):
            raise ValueError('invalid symbol bank')
        index.setdefault((address,4294967295 if bank is None else bank),[]).append(name)
    columns=lines[1].split()
    if columns!=['cycle','step','frame','pc','pc_bank','selected_before','selected_after','kind','address','value','old_valid','old_value','success','interrupt']:
        raise ValueError('unexpected trace columns')
    if len(lines)-2!=int(header[4]):raise ValueError('trace count mismatch')
    rows=[]
    for line in lines[2:]:
        fields=line.split()
        if len(fields)!=len(columns):raise ValueError('malformed trace row')
        row={key:(value if key=='kind' else int(value)) for key,value in zip(columns,fields)}
        row['pc_labels']=index.get((row['pc'],row['pc_bank']),[])
        rows.append(row)
    result={'schema':'boothush-nes-annotated-v1','rom_sha256':digest,
            'symbols_identity':symbols.get('build_identity'),'raw_header':header,
            'labels_are_caller_supplied':True,'rows':rows}
    with args.output.open('x',encoding='utf-8',newline='\n') as out:
        json.dump(result,out,indent=2);out.write('\n')

if __name__=='__main__':
    main()
