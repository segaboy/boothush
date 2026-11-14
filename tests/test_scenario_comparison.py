import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from test_boot import make_rom, SMOKE_PROGRAM
from test_nes import cartridge

class ScenarioComparison(unittest.TestCase):
    def test_original_guests_mapped_state_timing_and_errors(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp);exe=Path(os.environ['BOOTHUSH_EXE'])
            nes=root/'source.nes';mac=root/'target.rom'
            nes.write_bytes(cartridge(bytes.fromhex('a9cd8500 a9ab8501 a9348502 a9128503 4c1080')))
            mac.write_bytes(make_rom(SMOKE_PROGRAM))
            def invoke(args):
                p=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=10,
                                 creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,p.stdout+p.stderr);return json.loads(p.stdout)
            source=invoke([exe,'--machine','nes','--rom',nes,'--steps','1000','--timeout-ms','5000',
                           '--input-after','0:4:0x1234abcd','--capture-series',root/'source.ppm',
                           '--capture-cycle','0','--capture-memory','0:4','--until-input-cycle','1'])
            target=invoke([exe,'--machine','mac128k','--rom',mac,'--cycles','1000','--timeout-ms','5000',
                           '--input-after','0xb00:4:0x1234abcd','--capture-series',root/'target.pbm',
                           '--capture-cycle','0','--capture-memory','0xb00:4'])
            sp=root/'source.json';tp=root/'target.json';mp=root/'mapping.json'
            sp.write_text(json.dumps(source),encoding='utf-8');tp.write_text(json.dumps(target),encoding='utf-8')
            def selector(address,order):return {'sample':0,'field':'memory','address':address,'width':4,
                                                'byte_order':order,'signed':False,'scale':[1,1],'bias':[0,1]}
            def identity(result,clock,origin):return {'machine':result['machine'],'rom_sha256':result['frame_series']['rom_sha256'],
                                                     'executable_sha256':result['frame_series']['executable_sha256'],
                                                     'clock_hz':clock,'origin_cycle':origin}
            mapping={'source':identity(source,1789773,source['readiness']['cycle']),
                     'target':identity(target,7833600,target['input_schedule']['ready_cycle']),
                     'checks':[{'name':'original synthetic marker','source':selector(0,'little'),
                                'target':selector(0xb00,'big'),'value_tolerance':[0,1],'time_tolerance_us':[0,1]}]}
            helper=Path(__file__).resolve().parents[1]/'scripts'/'compare_scenarios.py'
            before=(sp.read_bytes(),tp.read_bytes(),(root/'source.ppm').read_bytes(),(root/'target.pbm').read_bytes())
            def compare(name,expected):
                mp.write_text(json.dumps(mapping),encoding='utf-8');report=root/(name+'.json')
                p=subprocess.run([sys.executable,helper,'--source',sp,'--target',tp,'--mapping',mp,'--report',report],
                                 capture_output=True,text=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,expected,p.stdout+p.stderr);return json.loads(report.read_text())
            result=compare('match',0);self.assertEqual(result['status'],'match')
            self.assertEqual(result['checks'][0]['source']['raw_hex'],'cdab3412')
            self.assertEqual(result['checks'][0]['target']['raw_hex'],'1234abcd')
            self.assertEqual(result['checks'][0]['source']['normalized'],[0x1234abcd,1])
            mapping['checks'][0]['target']['bias']=[1,1]
            self.assertEqual(compare('mismatch',1)['status'],'mismatch');mapping['checks'][0]['target']['bias']=[0,1]
            mapping['target']['origin_cycle']-=1
            self.assertFalse(compare('timing',1)['checks'][0]['time_match']);mapping['target']['origin_cycle']+=1
            mapping['checks'][0]['source']['sample']=99
            self.assertEqual(compare('missing',2)['status'],'invalid_evidence');mapping['checks'][0]['source']['sample']=0
            mapping['source']['rom_sha256']='0'*64
            self.assertEqual(compare('identity',2)['status'],'invalid_evidence')
            self.assertEqual(before,(sp.read_bytes(),tp.read_bytes(),(root/'source.ppm').read_bytes(),(root/'target.pbm').read_bytes()))
