#!/usr/bin/env python3
"""Compare explicitly mapped observations from two BootHush frame-series results."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import sys

class EvidenceError(ValueError):
    pass

def fraction(value,label):
    if not isinstance(value,list) or len(value)!=2 or any(type(x)!=int for x in value) or value[1]<=0:
        raise EvidenceError(f'{label} must be [integer, positive denominator]')
    return Fraction(value[0],value[1])

def pair(value):return [value.numerator,value.denominator]

def load_evidence(path,identity,side):
    raw=path.read_bytes()
    if len(raw)>4000000:raise EvidenceError(f'{side}: evidence exceeds size limit')
    result=json.loads(raw.decode('utf-8-sig'))
    if result.get('status') not in ('condition_met','cycle_limit','step_limit'):
        raise EvidenceError(f"{side}: run did not complete successfully: {result.get('status')}")
    if result.get('machine')!=identity['machine']:raise EvidenceError(f'{side}: machine mismatch')
    series=result.get('frame_series',{})
    if not series.get('written'):raise EvidenceError(f'{side}: capture series was not written')
    for key in ('rom_sha256','executable_sha256'):
        if not identity.get(key) or identity[key]!=series.get(key):raise EvidenceError(f'{side}: {key} mismatch')
    if result.get('readiness',{}).get('reached') is False or result.get('input_schedule',{}).get('ready') is False:
        raise EvidenceError(f'{side}: readiness not reached')
    if result.get('controller_schedule',{}).get('pending',0) or result.get('input_schedule',{}).get('scheduled',0)!=result.get('input_schedule',{}).get('accepted',0):
        raise EvidenceError(f'{side}: scheduled inputs remain pending')
    return result,hashlib.sha256(raw).hexdigest()

def observation(result,spec,side):
    sample_index=spec['sample'];field=spec['field'];address=spec['address'];width=spec['width']
    if type(sample_index)!=int or sample_index<0 or field not in ('memory','ppu','oam') or type(address)!=int or type(width)!=int or width not in (1,2,4,8):
        raise EvidenceError(f'{side}: invalid observation selector')
    samples=result['frame_series']['samples']
    if sample_index>=len(samples) or not samples[sample_index].get('processed'):
        raise EvidenceError(f'{side}: requested sample missing or pending')
    sample=samples[sample_index];region=sample.get(field)
    if not isinstance(region,dict):raise EvidenceError(f'{side}: {field} was not captured')
    data=bytes.fromhex(region['bytes']);offset=address-region['address']
    if offset<0 or offset+width>len(data):raise EvidenceError(f'{side}: observation exceeds captured range')
    byteorder=spec['byte_order'];signed=spec['signed']
    if byteorder not in ('big','little') or type(signed)!=bool:raise EvidenceError(f'{side}: invalid integer representation')
    raw=data[offset:offset+width];number=int.from_bytes(raw,byteorder,signed=signed)
    value=number*fraction(spec['scale'],f'{side} scale')+fraction(spec['bias'],f'{side} bias')
    return {'cycle':sample['cycle'],'frame':sample['frame'],'raw_hex':raw.hex(),'decoded':number,
            'normalized':pair(value),'selector':spec},value

def compare(source,target,mapping):
    if not isinstance(mapping.get('checks'),list) or not 1<=len(mapping['checks'])<=64:
        raise EvidenceError('checks must contain 1..64 entries')
    identities={side:mapping[side] for side in ('source','target')}
    for side in identities:
        clock=identities[side].get('clock_hz');origin=identities[side].get('origin_cycle')
        if type(clock)!=int or clock<=0 or type(origin)!=int or origin<0:
            raise EvidenceError(f'{side}: explicit clock_hz and origin_cycle required')
    evidence={};hashes={}
    for side,path in (('source',source),('target',target)):
        evidence[side],hashes[side]=load_evidence(path,identities[side],side)
    checks=[]
    for spec in mapping['checks']:
        name=spec['name']
        if not isinstance(name,str) or not name:raise EvidenceError('check requires a name')
        left,lv=observation(evidence['source'],spec['source'],'source')
        right,rv=observation(evidence['target'],spec['target'],'target')
        tolerance=fraction(spec['value_tolerance'],'value tolerance')
        timing_tolerance=fraction(spec['time_tolerance_us'],'time tolerance')
        if tolerance<0 or timing_tolerance<0:raise EvidenceError('tolerances must not be negative')
        delta=abs(lv-rv)
        st=Fraction((left['cycle']-identities['source']['origin_cycle'])*1000000,identities['source']['clock_hz'])
        tt=Fraction((right['cycle']-identities['target']['origin_cycle'])*1000000,identities['target']['clock_hz'])
        if st<0 or tt<0:raise EvidenceError('sample occurs before its declared origin')
        dt=abs(st-tt)
        checks.append({'name':name,'source':left,'target':right,'value_delta':pair(delta),'time_delta_us':pair(dt),
                       'value_match':delta<=tolerance,'time_match':dt<=timing_tolerance,'spec':spec})
    return {'status':'match' if all(x['value_match'] and x['time_match'] for x in checks) else 'mismatch',
            'source_result_sha256':hashes['source'],'target_result_sha256':hashes['target'],
            'source_result':str(source),'target_result':str(target),'mapping':mapping,'checks':checks,
            'scope':'Only declared observations and time mapping are compared; not hardware or full-game equivalence.'}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',required=True,type=Path);p.add_argument('--target',required=True,type=Path)
    p.add_argument('--mapping',required=True,type=Path);p.add_argument('--report',required=True,type=Path)
    a=p.parse_args()
    try:
        mapping=json.loads(a.mapping.read_text(encoding='utf-8-sig'))
        result=compare(a.source,a.target,mapping);code=0 if result['status']=='match' else 1
    except (OSError,ValueError,KeyError,TypeError,IndexError) as error:
        result={'status':'invalid_evidence','error':str(error)};code=2
    try:
        with a.report.open('x',encoding='utf-8',newline='\n') as out:json.dump(result,out,indent=2);out.write('\n')
    except OSError as error:
        print(f'cannot create report: {error}',file=sys.stderr);return 2
    print(json.dumps(result,indent=2));return code
if __name__=='__main__':sys.exit(main())
