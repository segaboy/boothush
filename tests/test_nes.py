"""Original generated cartridges for the bounded experimental NES CPU target."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import wave


def cartridge(code, banks=1, start=0x8000, chr_banks=0):
    header = bytearray(b'NES\x1a' + bytes(12))
    header[4], header[5] = banks, chr_banks
    prg = bytearray([0xff] * (banks * 16384))
    offset = (start - 0x8000) % len(prg)
    prg[offset:offset + len(code)] = code
    prg[-4:-2] = start.to_bytes(2, 'little')
    return header + prg + bytes(chr_banks * 8192)


class NesTarget(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP'))
        self.addCleanup(tmp.cleanup)
        self.file = Path(tmp.name) / 'original.nes'

    def run_nes(self, image, *args, steps=100):
        self.file.write_bytes(image)
        before = hashlib.sha256(image).hexdigest()
        proc = subprocess.run([os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(self.file),
                               '--steps', str(steps), '--timeout-ms', '3000', *args],
                              capture_output=True, text=True, timeout=10,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(proc.stderr, '')
        self.assertEqual(hashlib.sha256(self.file.read_bytes()).hexdigest(), before)
        return proc.returncode, json.loads(proc.stdout)

    def test_original_loop_and_ram_mirroring(self):
        # Sum five ones, store each result, and stop just after the loop.
        image = cartridge(bytes.fromhex('a200 a900 18 6901 8510 e8 e005 d0f6 ea'))
        for address in ('0x10', '0x810', '0x1010', '0x1810'):
            code, result = self.run_nes(image, '--until-pc', '0x800e', '--inspect', address + ':1')
            self.assertEqual((code, result['status'], result['a'], result['x'], result['memory']['bytes']),
                             (0, 'condition_met', 5, 5, '05'))
            self.assertEqual(result['rom_sha256'], hashlib.sha256(image).hexdigest())

    def test_nrom_128_mirror_and_256_reset_vectors(self):
        for banks, start in ((1, 0xc000), (2, 0xc000)):
            image = cartridge(bytes.fromhex('a92a 8500 ea'), banks, start)
            code, result = self.run_nes(image, '--until-pc', '0xc004', '--inspect', '0:1')
            self.assertEqual((code, result['status'], result['a'], result['memory']['bytes']),
                             (0, 'condition_met', 42, '2a'))
        # NROM-128 maps the same PRG bank at both addresses.
        image = cartridge(bytes.fromhex('a92a'), 1)
        code, result = self.run_nes(image, '--until-pc', '0x8000', '--inspect', '0xc000:2')
        self.assertEqual((code, result['memory']['bytes']), (0, 'a92a'))

    def test_nes2_nrom_header(self):
        image = cartridge(bytes.fromhex('a92a 8500 ea'), 2, 0x8000, 1)
        image[6], image[7], image[12], image[15] = 1, 8, 2, 1
        code, result = self.run_nes(image, '--until-pc', '0x8004', '--inspect', '0:1')
        self.assertEqual((code, result['status'], result['memory']['bytes']), (0, 'condition_met', '2a'))
        for index, value in ((8, 0x10), (9, 1), (10, 1), (11, 1), (12, 3), (13, 1), (14, 1), (15, 2)):
            invalid = bytearray(image); invalid[index] = value
            code, result = self.run_nes(invalid)
            self.assertEqual((code, result['status']), (2, 'error'))

    def test_binary_arithmetic_decimal_flag_and_overflow(self):
        image = cartridge(bytes.fromhex('f8 18 a909 6901 8501 18 a97f 6901 8502 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800f', '--inspect', '1:2')
        self.assertEqual((code, result['memory']['bytes']), (0, '0a80'))
        self.assertEqual(result['flags'] & 0xc9, 0xc8)  # N,V,D set; C clear
        image = cartridge(bytes.fromhex('38 a900 e901 8503 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8007', '--inspect', '3:1')
        self.assertEqual((code, result['memory']['bytes'], result['flags'] & 0x81), (0, 'ff', 0x80))

    def test_transfers_and_register_wrap(self):
        image = cartridge(bytes.fromhex('a980 aa a8 8a 98 8610 8411 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x800a', '--inspect', '0x10:2')
        self.assertEqual((code, result['a'], result['x'], result['y'], result['memory']['bytes']),
                         (0, 128, 128, 128, '8080'))
        image = cartridge(bytes.fromhex('a200 ca e8 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8003')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 255, 0x80))
        code, result = self.run_nes(image, '--until-pc', '0x8004')
        self.assertEqual((code, result['x'], result['flags'] & 0x82), (0, 0, 0x02))

    def test_scheduled_controller_latch(self):
        image = cartridge(bytes.fromhex('a901 8d1640 a900 8d1640 ad1640 8500 ad1640 8501 ea'))
        code, result = self.run_nes(image, '--until-pc', '0x8014', '--inspect', '0:2',
                                    '--controller-event', '3:1', '--controller-event', '4:0')
        self.assertEqual((code, result['status'], result['memory']['bytes']), (0, 'condition_met', '4140'))
        self.assertEqual(result['controller_schedule'], {'scheduled': 2, 'accepted': 2, 'pending': 0})
        code, timed = self.run_nes(image, '--until-pc', '0x8014', '--controller-event', '3:1',
                                   '--controller-event', '4:0', '--controller-timing', '1')
        self.assertEqual(code,0)
        self.assertEqual([e['requested_step'] for e in timed['instruction_schedule']['events']],[3,4])
        self.assertLess(timed['instruction_schedule']['events'][0]['actual_cycle'],timed['instruction_schedule']['events'][1]['actual_cycle'])

    def test_mapper2_banking_ram_and_validation(self):
        # A generated 512 KiB NES 2.0 board; reset in the fixed last bank.
        image = bytearray(b'NES\x1a' + bytes(12))
        image[4], image[6], image[7], image[8], image[10], image[11], image[12] = 32, 0x22, 8, 0x10, 7, 7, 1
        for bank in range(32):
            image.extend(bytes([bank]) * 16384)
        program = bytes.fromhex('a905 8d0080 ad0080 8500 a9a6 8d0060 ad0060 8501 ea')
        image[16+31*16384:16+31*16384+len(program)] = program
        image[-4:-2] = bytes.fromhex('00c0')
        code, result = self.run_nes(image, '--until-pc', hex(0xc000+len(program)-1), '--inspect', '0:2')
        self.assertEqual((code, result['memory']['bytes']), (0, '05a6'))
        self.assertEqual((result['cartridge']['mapper'], result['cartridge']['submapper'], result['cartridge']['selected_bank']), (2, 1, 5))
        self.assertEqual((result['cartridge']['prg_rom'], result['cartridge']['prg_ram'], result['cartridge']['chr_ram']), (524288, 8192, 8192))
        self.assertEqual((result['cartridge']['header_region'], result['cartridge']['region']), ('pal', 'pal'))
        # Upstream tilt.asm's UNROM header sets the battery flag but declares
        # no PRG NVRAM or RAM; the flag alone must not create fictitious RAM.
        upstream_header = bytearray(image); upstream_header[10] = 0
        code, exact = self.run_nes(upstream_header, '--until-pc', '0xc008', '--inspect', '0x8000:1')
        self.assertEqual((code, exact['memory']['bytes'], exact['cartridge']['prg_ram'], exact['cartridge']['battery_flag']), (0, '05', 0, True))
        code, result = self.run_nes(image, '--until-pc', hex(0xc000+len(program)-1), '--inspect', '0x8000:1', '--region', 'ntsc')
        self.assertEqual((code, result['memory']['bytes'], result['cartridge']['region'], result['cartridge']['region_override']), (0, '05', 'ntsc', True))
        for index, value in ((8, 0), (8, 0x20), (9, 1), (10, 0x70), (11, 0), (12, 3)):
            invalid = bytearray(image); invalid[index] = value
            code, result = self.run_nes(invalid)
            self.assertEqual((code, result['status']), (2, 'error'), (index, value))

    def test_two_independent_scheduled_controllers(self):
        program = bytes.fromhex('a901 8d1640 a900 8d1640 ad1640 8500 ad1740 8501 ad1640 8502 ad1740 8503')
        image = cartridge(program + b'\xea')
        args = ('--until-pc', hex(0x8000+len(program)), '--inspect', '0:4', '--controller1', '1', '--controller2', '2',
                '--controller2-event', '3:1', '--controller2-event', '4:0',
                '--controller-event', '3:2', '--controller-event', '4:0')
        code, result = self.run_nes(image, *args)
        self.assertEqual((code, result['memory']['bytes']), (0, '40414140'))
        self.assertEqual(result['controller_schedule'], {'scheduled': 4, 'accepted': 4, 'pending': 0})
        self.assertEqual([c['accepted'] for c in result['controllers']], [2, 2])
        self.assertEqual(self.run_nes(image, *args), (code, result))

    def test_cycle_scenarios_readiness_observations_and_order(self):
        # Publish ready=1, then repeatedly latch and record both controllers.
        image = cartridge(bytes.fromhex('a901 8510 a901 8d1640 a900 8d1640 ad1640 8500 ad1740 8501 4c0480'))
        def scenario(p1, p2):
            args = ('--input-after', '0x10:1:1', '--controller-cycle', f'0:{p1}',
                    '--controller2-cycle', f'0:{p2}', '--controller-cycle', '50:0',
                    '--controller2-cycle', '50:0', '--observe-cycle', '50:0:2',
                    '--observe-cycle', '100:0:2', '--until-input-cycle', '100')
            return self.run_nes(image, *args, steps=1000)
        neutral=scenario(0,0); player1=scenario(1,0); player2=scenario(0,1); both=scenario(1,1)
        self.assertEqual(scenario(1,1),both)
        for code,result in (neutral,player1,player2,both):
            self.assertEqual((code,result['status'],result['readiness']['reached']), (0,'condition_met',True))
            self.assertEqual((result['readiness']['cycle'],result['controller_schedule']['accepted']), (5,4))
            self.assertEqual((result['cycle_observations']['completed'],result['cycle_observations']['pending']), (2,0))
            for event in result['cycle_schedule']['events']:
                self.assertGreaterEqual(event['actual_cycle'],event['target_cycle'])
            self.assertEqual([event['port'] for event in result['cycle_schedule']['events']], [1,2,1,2])
            for sample in result['cycle_observations']['samples']:
                self.assertGreaterEqual(sample['actual_cycle'],sample['target_cycle'])
            self.assertEqual(result['cpu_cycles'],result['cycle_observations']['samples'][-1]['actual_cycle'])
        snapshots=[r['cycle_observations']['samples'][0]['bytes'] for _,r in (neutral,player1,player2,both)]
        self.assertEqual(snapshots,['4040','4140','4041','4141'])
        self.assertEqual(len({r['cycle_observations']['samples'][0]['actual_cycle'] for _,r in (neutral,player1,player2,both)}),1)
        code,without_events=self.run_nes(image,'--input-after','0x10:1:1','--observe-cycle','50:0:2',
                                         '--until-input-cycle','100',steps=1000)
        self.assertEqual((code,without_events['cycle_observations']['samples'][0]['bytes']),(0,'4040'))

    def test_cycle_stops_bounds_and_pending_events(self):
        image=cartridge(bytes.fromhex('ea 4c0080'))
        code,r=self.run_nes(image,'--controller-cycle','2:1','--observe-cycle','2:0:1','--until-cycle','2')
        self.assertEqual((code,r['status'],r['cpu_cycles']),(0,'condition_met',2))
        self.assertEqual((r['controller_schedule']['accepted'],r['controller_schedule']['pending']),(0,1))
        self.assertEqual(r['cycle_observations']['completed'],1)
        code,r=self.run_nes(image,'--input-after','0:1:1','--controller-cycle','0:1','--until-input-cycle','20',steps=10)
        self.assertEqual((code,r['status'],r['readiness']['reached'],r['controller_schedule']['pending']),(1,'readiness_not_reached',False,1))
        code,r=self.run_nes(image,'--until-cycle','1000',steps=2)
        self.assertEqual((code,r['status']),(1,'condition_not_met'))
        code,r=self.run_nes(image,'--until-frame','1',steps=20000)
        self.assertEqual((code,r['status'],r['ppu_frames']),(0,'condition_met',1))
        for args in (('--controller-event','1:1','--controller-cycle','2:1'),
                     ('--controller-cycle','2:1','--controller-cycle','1:0'),
                     ('--observe-cycle','0:0x1fff:2'),('--until-cycle','4','--until-frame','1'),
                     ('--input-after','0x2000:1:0','--controller-cycle','0:1'),
                     ('--until-input-cycle','1',)):
            with self.subTest(args=args): self.assertEqual(self.run_nes(image,*args)[0],2)

    def test_nes_frame_series_cycle_frame_memory_and_bounds(self):
        image=cartridge(bytes.fromhex('a901 8510 4c0480'))
        path=self.file.parent/'series.ppm'; repeat=self.file.parent/'repeat.ppm'
        def run(p):
            return self.run_nes(image,'--capture-series',str(p),'--capture-cycle','0',
                                '--capture-cycle','30000','--capture-cycle','60000','--capture-cycle','70000',
                                '--capture-memory','0x10:1','--capture-ppu','0:1','--capture-oam','0:1',
                                '--controller-cycle','30000:1','--until-cycle','60000',steps=40000)
        code,first=run(path);code2,second=run(repeat)
        self.assertEqual((code,code2),(0,0));self.assertEqual(first,second);self.assertEqual(path.read_bytes(),repeat.read_bytes())
        series=first['frame_series'];samples=series['samples'];payload=path.read_bytes()
        self.assertEqual(series['rom_sha256'],hashlib.sha256(image).hexdigest())
        self.assertEqual(series['executable_sha256'],hashlib.sha256(Path(os.environ['BOOTHUSH_EXE']).read_bytes()).hexdigest())
        self.assertEqual((series['processed'],series['pending']),(3,1))
        self.assertFalse(samples[0]['image_available']);self.assertEqual(samples[0]['memory']['bytes'],'00')
        self.assertEqual(samples[1]['memory']['bytes'],'01');self.assertEqual(samples[1]['inputs_accepted_before'],0)
        self.assertEqual(samples[2]['inputs_accepted_before'],1)
        self.assertEqual(samples[1]['ppu']['bytes'],'00');self.assertEqual(samples[1]['oam']['bytes'],'00')
        for sample in samples[1:3]:
            self.assertTrue(sample['image_available']);self.assertGreaterEqual(sample['cycle'],sample['target'])
            self.assertEqual(len(payload[sample['offset']:sample['offset']+sample['length']]),15+256*240*3)
            self.assertTrue(payload[sample['offset']:].startswith(b'P6\n256 240\n255\n'))
        before=path.read_bytes();self.assertEqual(run(path)[0],2);self.assertEqual(path.read_bytes(),before)
        code,r=self.run_nes(image,'--capture-series',str(self.file.parent/'frames.ppm'),
                            '--capture-frame','1','--capture-frame','2','--capture-frame','3',
                            '--until-frame','2',steps=40000)
        self.assertEqual((code,r['frame_series']['processed'],r['frame_series']['pending']),(0,2,1))
        self.assertEqual([x['frame'] for x in r['frame_series']['samples'][:2]],[1,2])
        missing=self.file.parent/'missing.ppm'
        code,r=self.run_nes(image,'--input-after','0x10:1:3','--capture-series',str(missing),
                            '--capture-cycle','0',steps=100)
        self.assertEqual((code,r['status'],r['frame_series']['processed'],r['frame_series']['pending']),(1,'readiness_not_reached',0,1))
        self.assertEqual(missing.read_bytes(),b'')
        for args in (('--capture-series',str(self.file.parent/'bad')),('--capture-cycle','1'),
                     ('--capture-series',str(self.file.parent/'bad'),'--capture-cycle','1','--capture-frame','1'),
                     ('--capture-series',str(self.file.parent/'bad'),'--capture-cycle','1','--capture-memory','0x1fff:2'),
                     ('--capture-series',str(self.file.parent/'bad'),'--capture-cycle','1','--capture-oam','255:2')):
            with self.subTest(args=args):self.assertEqual(self.run_nes(image,*args)[0],2)

    def test_filtered_execution_witness_banks_reads_writes_and_overflow(self):
        image=bytearray(b'NES\x1a'+bytes(12));image[4]=2;image[6]=0x20;image[7]=8;image[8]=0x10;image[11]=7
        prg=bytearray([0xea]*32768)
        prg[0:5]=bytes.fromhex('a901 8d0080')
        prg[16384:16384+8]=bytes.fromhex('a942 8510 ea 4c0080')
        prg[-4:-2]=bytes.fromhex('0080');image+=prg
        path=self.file.parent/'cpu.trace';controller=self.file.parent/'controller.trace'
        common=('--until-pc','0x8004','--inspect','0x10:1')
        code,plain=self.run_nes(image,*common)
        args=common+('--nes-witness',str(path),'--nes-witness-exec','0x8000:8',
                     '--nes-witness-read','0x8000:8','--nes-witness-write','0x10:32753',
                     '--controller-witness',str(controller))
        code2,traced=self.run_nes(image,*args)
        self.assertEqual((code,code2),(0,0));meta=traced.pop('nes_witness');traced.pop('controller_witness')
        self.assertEqual(traced,plain);self.assertEqual(plain['memory']['bytes'],'42');self.assertEqual(meta['dropped'],0)
        lines=path.read_text().splitlines();header=lines[0].split();rows=[line.split() for line in lines[2:]]
        self.assertEqual(header[0],'BHNESCPU1');self.assertEqual(header[-1],hashlib.sha256(image).hexdigest())
        executed=[r for r in rows if r[7]=='exec' and int(r[3])==0x8000]
        self.assertEqual([int(r[4]) for r in executed],[0,1])
        writes=[r for r in rows if r[7]=='write']
        self.assertTrue(any(int(r[8])==0x8000 and (int(r[5]),int(r[6]))==(0,1) and int(r[10])==0 for r in writes))
        self.assertTrue(any(int(r[8])==0x10 and int(r[9])==0x42 and int(r[10])==1 and int(r[11])==0 for r in writes))
        self.assertTrue(any(r[7]=='read' for r in rows))
        self.assertTrue(all((r[7]=='exec' and 0x8000<=int(r[8])<0x8008) or
                            (r[7]=='read' and 0x8000<=int(r[8])<0x8008) or
                            (r[7]=='write' and 0x10<=int(r[8])<=0x8000) for r in rows))
        repeat=self.file.parent/'repeat.trace'
        code3,_=self.run_nes(image,*common,'--nes-witness',str(repeat),'--nes-witness-exec','0x8000:8',
                             '--nes-witness-read','0x8000:8','--nes-witness-write','0x10:32753')
        self.assertEqual(code3,0);self.assertEqual(path.read_bytes(),repeat.read_bytes())
        symbols=self.file.parent/'symbols.json';annotated=self.file.parent/'annotated.json'
        symbols.write_text(json.dumps({'rom_sha256':header[-1],'build_identity':'synthetic fixture',
                                      'symbols':[{'name':'bank-zero','address':0x8000,'bank':0},
                                                 {'name':'bank-one','address':0x8000,'bank':1}]}),encoding='utf-8')
        helper=Path(__file__).resolve().parents[1]/'scripts'/'annotate_nes_witness.py'
        cmd=[sys.executable,helper,'--trace',path,'--symbols',symbols,'--output',annotated]
        p=subprocess.run(cmd,capture_output=True,text=True,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(p.returncode,0,p.stderr)
        mapped=json.loads(annotated.read_text());self.assertTrue(mapped['labels_are_caller_supplied'])
        hits=[r for r in mapped['rows'] if r['kind']=='exec' and r['pc']==0x8000]
        self.assertEqual([r['pc_labels'] for r in hits],[['bank-zero'],['bank-one']])
        symbols.write_text(json.dumps({'rom_sha256':'0'*64,'symbols':[]}),encoding='utf-8')
        mismatch=self.file.parent/'mismatch.json';cmd[-1]=mismatch
        self.assertNotEqual(subprocess.run(cmd,capture_output=True).returncode,0);self.assertFalse(mismatch.exists())
        tiny=self.file.parent/'tiny.trace'
        code4,r=self.run_nes(image,*common,'--nes-witness',str(tiny),'--nes-witness-exec','0x8000:8',
                             '--nes-witness-limit','1')
        self.assertEqual((code4,r['nes_witness']['events']),(0,1));self.assertGreater(r['nes_witness']['dropped'],0)
        filtered=self.file.parent/'filtered.trace'
        code5,r=self.run_nes(image,*common,'--nes-witness',str(filtered),'--nes-witness-write','0x10:1',
                             '--nes-witness-cycles','0:0')
        self.assertEqual((code5,r['nes_witness']['events']),(0,0))
        for options in (('--nes-witness',str(path)),('--nes-witness-exec','0:1'),
                        ('--nes-witness',str(path),'--nes-witness-write','65535:2'),
                        ('--nes-witness',str(path),'--nes-witness-exec','0:1','--nes-witness-limit','0')):
            with self.subTest(options=options): self.assertEqual(self.run_nes(image,*options)[0],2)

    def test_controller_witness_guest_reads_and_overflow(self):
        program=bytes.fromhex('a901 8d1640 a900 8d1640 ad1640 8500 ad1740 8501 4c0080')
        image=cartridge(program); witness=self.file.parent/'controller.txt'
        args=('--controller1','1','--controller2','0','--until-pc','0x8014','--controller-witness',str(witness))
        code,result=self.run_nes(image,*args)
        self.assertEqual((code,result['status']),(0,'condition_met'))
        rows=witness.read_text().splitlines(); header=rows[0].split(); events=[r.split() for r in rows[2:]]
        self.assertEqual(header[0],'BHNESCTRL1')
        self.assertEqual((result['controller_witness']['events'],result['controller_witness']['dropped']),(4,0))
        self.assertEqual([(e[4],int(e[5]),int(e[6]),int(e[7])) for e in events],
                         [('strobe',0,1,1),('strobe',0,0,0),('read',1,65,0),('read',2,64,0)])
        code,plain=self.run_nes(image,'--controller1','1','--controller2','0','--until-pc','0x8014')
        self.assertEqual(code,0); result.pop('controller_witness'); self.assertEqual(result,plain)
        self.assertEqual(self.run_nes(image,*args)[0],2) # Existing output is protected.
        filtered=self.file.parent/'filtered.txt'
        code,result=self.run_nes(image,'--until-pc','0x8014','--controller-witness',str(filtered),
                                 '--controller-witness-cycles','0:5')
        self.assertEqual((code,result['controller_witness']['events']),(0,1))
        full=self.file.parent/'full.txt'
        code,result=self.run_nes(image,'--controller-witness',str(full),steps=30000)
        self.assertEqual((code,result['controller_witness']['events']),(0,8192))
        self.assertGreater(result['controller_witness']['dropped'],0)
        self.assertEqual(self.run_nes(image,'--controller-witness-cycles','0:5')[0],2)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_STB_UNROM'), 'Super Tilt Bro UNROM fixture not supplied')
    def test_author_game_cycle_aligned_scenarios(self):
        source=Path(os.environ['BOOTHUSH_STB_UNROM'])
        digest='5d045246f12d424c2c67ecda883c66d8d346f7b5396ed819ddb326f44dcb5aaa'
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),digest)
        nav=[(1,1000000,1),(1,1030000,0),(1,1800000,1),(1,1830000,0),
             (1,4000000,8),(1,4050000,0),(1,6500000,1),(1,6550000,0),
             (1,7500000,8),(1,7550000,0),(2,6500000,1),(2,6550000,0),
             (1,10000000,0),(1,11500000,0),(2,10000000,0),(2,11500000,0)]
        common=[os.environ['BOOTHUSH_EXE'],'--machine','nes','--rom',str(source),'--timeout-ms','30000']
        legacy=common+['--steps','13000000','--controller-timing','1']
        for port,step,mask in nav: legacy += ['--controller-event' if port==1 else '--controller2-event',f'{step}:{mask}']
        p=subprocess.run(legacy,capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        baseline=json.loads(p.stdout); end_cycle=baseline['cpu_cycles']
        events=baseline['instruction_schedule']['events']
        self.assertEqual(len(events),len(nav)); self.assertTrue(all(e['applied'] for e in events))
        movement_start=next(e['actual_cycle'] for e in events if e['requested_step']==10000000)
        def run(name,p1,p2):
            frame=self.file.parent/(name+'.ppm')
            series_file=self.file.parent/(name+'.series')
            cmd=common+['--steps','15000000','--until-cycle',str(end_cycle),
                        '--observe-cycle',f'{end_cycle}:0:256','--inspect','0:2048',
                        '--inspect-oam','0:256','--frame',str(frame),
                        '--capture-series',str(series_file),'--capture-memory','0:32']
            for point in (movement_start+40000,movement_start+80000,movement_start+120000):
                cmd += ['--capture-cycle',str(point)]
            witness=self.file.parent/(name+'.ctrl')
            if name in ('cycle-both','cycle-both-repeat'):
                cmd += ['--controller-witness',str(witness),'--controller-witness-cycles',f'{movement_start}:{movement_start+100000}']
            for event in events:
                mask=event['mask']
                if event['requested_step']==10000000: mask=p1 if event['port']==1 else p2
                cmd += ['--controller-cycle' if event['port']==1 else '--controller2-cycle',
                        f"{event['actual_cycle']}:{mask}"]
            p=subprocess.run(cmd,capture_output=True,text=True,timeout=35,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            result=json.loads(p.stdout)
            self.assertEqual((result['status'],result['rom_sha256'],result['cartridge']['region']),('condition_met',digest,'pal'))
            self.assertEqual((result['controller_schedule']['accepted'],result['cycle_observations']['completed']),(len(events),1))
            self.assertEqual((result['frame_series']['processed'],result['frame_series']['pending']),(3,0))
            self.assertTrue(all(s['image_available'] and s['cycle']>=s['target'] for s in result['frame_series']['samples']))
            self.assertEqual(result['stop_condition']['target_cycle'],end_cycle)
            self.assertGreaterEqual(result['cpu_cycles'],end_cycle)
            self.assertLess(result['cpu_cycles']-end_cycle,1024)
            if witness.exists():
                rows=[row.split() for row in witness.read_text().splitlines()[2:]]
                self.assertEqual(result['controller_witness']['dropped'],0)
                self.assertTrue({1,2}.issubset({int(row[5]) for row in rows if row[4]=='read'}))
                self.assertTrue(all(any(row[4]=='read' and int(row[5])==port and int(row[6])==65 for row in rows) for port in (1,2)))
            return result,frame.read_bytes(),series_file.read_bytes()
        neutral=run('cycle-neutral',0,0); one=run('cycle-p1',128,0); two=run('cycle-p2',0,64)
        both=run('cycle-both',128,64); repeated=run('cycle-both-repeat',128,64)
        self.assertEqual(both,repeated)
        self.assertNotEqual(one[1],neutral[1]);self.assertNotEqual(two[1],neutral[1])
        self.assertEqual([s['target'] for s in one[0]['frame_series']['samples']],
                         [s['target'] for s in neutral[0]['frame_series']['samples']])
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),digest)

    def test_concurrent_cpu_ppu_and_oam_inspection(self):
        program = bytes.fromhex('a900 8d0620 8d0620 a97b 8d0720 a900 8d0320 a956 8d0420 a9aa 8500')
        image = cartridge(program + b'\xea')
        args = ('--until-pc', hex(0x8000+len(program)), '--inspect', '0:1', '--inspect', '0x800:1',
                '--inspect-ppu', '0:1', '--inspect-oam', '0:1')
        code, result = self.run_nes(image, *args)
        self.assertEqual(code, 0)
        self.assertEqual([x['bytes'] for x in result['memory_regions']], ['aa', 'aa'])
        self.assertEqual((result['ppu_memory']['bytes'], result['oam']['bytes']), ('7b', '56'))
        self.assertEqual(self.run_nes(image, *args), (code, result))
        for option, bad in (('--inspect-ppu', '0x3fff:2'), ('--inspect-oam', '255:2')):
            self.assertEqual(self.run_nes(image, option, bad)[0], 2)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_STB_UNROM'), 'Super Tilt Bro UNROM fixture not supplied')
    def test_author_super_tilt_bro_unrom_replay(self):
        source = Path(os.environ['BOOTHUSH_STB_UNROM'])
        digest = '5d045246f12d424c2c67ecda883c66d8d346f7b5396ed819ddb326f44dcb5aaa'
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), digest)
        common = ['--controller-event', '1000000:1', '--controller-event', '1030000:0',
                  '--controller-event', '1800000:1', '--controller-event', '1830000:0',
                  '--controller-event', '4000000:8', '--controller-event', '4050000:0',
                  '--controller-event', '6500000:1', '--controller-event', '6550000:0',
                  '--controller-event', '7500000:8', '--controller-event', '7550000:0',
                  '--controller2-event', '6500000:1', '--controller2-event', '6550000:0',
                  '--inspect', '0:2048', '--inspect-ppu', '0x2000:2048', '--inspect-oam', '0:256']
        movement = ['--controller-event', '10000000:128', '--controller-event', '11500000:0',
                    '--controller2-event', '10000000:64', '--controller2-event', '11500000:0']
        def run(name, extra):
            frame = self.file.parent / (name + '.ppm')
            command = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                       '--steps', '13000000', '--timeout-ms', '30000', *common, *extra, '--frame', str(frame)]
            proc = subprocess.run(command, capture_output=True, text=True, timeout=35,
                                  creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            result = json.loads(proc.stdout)
            self.assertEqual((result['status'], result['rom_sha256'], result['cartridge']['region']), ('step_limit', digest, 'pal'))
            self.assertEqual(result['controller_schedule']['pending'], 0)
            return result, frame.read_bytes()
        baseline, baseline_frame = run('baseline', [])
        first, first_frame = run('movement1', movement)
        second, second_frame = run('movement2', movement)
        self.assertEqual((first, first_frame), (second, second_frame))
        self.assertEqual(first['controller_schedule']['accepted'], 16)
        self.assertNotEqual(baseline_frame, first_frame)
        self.assertNotEqual(baseline['memory'], first['memory'])
        self.assertNotEqual(baseline['ppu_memory'], first['ppu_memory'])
        self.assertNotEqual(baseline['oam'], first['oam'])
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), digest)

    def test_frame_requires_completed_frame_and_no_overwrite(self):
        image = cartridge(bytes.fromhex('4c0080'))
        output = self.file.parent / 'frame.ppm'
        output.write_bytes(b'unchanged')
        code, result = self.run_nes(image, '--frame', str(output))
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', b'unchanged'))
        code, result = self.run_nes(image, '--frame', str(output), steps=40000)
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', b'unchanged'))
        output.unlink()
        code, result = self.run_nes(image, '--frame', str(output), steps=40000)
        self.assertEqual((code, result['status']), (0, 'step_limit'))
        self.assertEqual(len(output.read_bytes()), len(b'P6\n256 240\n255\n') + 256*240*3)

    def test_audio_capture_and_noninterference(self):
        # Original ROM enables a constant-volume pulse then loops.
        image = cartridge(bytes.fromhex('a940 8d1740 a901 8d1540 a9bf 8d0040 a9fd 8d0240 a908 8d0340 4c1980'))
        output = self.file.parent / 'sound.wav'
        code, captured = self.run_nes(image, '--audio', str(output), steps=20000)
        self.assertEqual((code, captured['status'], captured['audio_captured'], captured['audio_truncated']),
                         (0, 'step_limit', True, False))
        with wave.open(str(output), 'rb') as wav:
            self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (1, 2, 44100))
            self.assertEqual(wav.getnframes(), captured['audio_samples'])
            self.assertTrue(any(wav.readframes(wav.getnframes())))
        code, plain = self.run_nes(image, steps=20000)
        self.assertEqual(code, 0)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'apu_writes', 'ppu_frames'):
            self.assertEqual(captured[key], plain[key], key)
        sentinel = output.read_bytes()
        code, result = self.run_nes(image, '--audio', str(output), steps=20000)
        self.assertEqual((code, result['status'], output.read_bytes()), (2, 'error', sentinel))

    def test_video_capture_and_no_overwrite(self):
        image = cartridge(bytes.fromhex('4c0080'))
        path = self.file.parent / 'capture.y4m'
        code, result = self.run_nes(image, '--video', str(path), '--video-fps', '20',
                                    '--video-frames', '2', steps=100000)
        self.assertEqual((code, result['video']['width'], result['video']['height'],
                          result['video']['frames'], result['video']['truncated']), (0, 256, 240, 2, True))
        data = path.read_bytes()
        self.assertTrue(data.startswith(b'YUV4MPEG2 W256 H240 F20:1'))
        self.assertEqual(data.count(b'FRAME\n'), 2)
        code, plain = self.run_nes(image, steps=100000)
        self.assertEqual(code, 0)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'ppu_frames'):
            self.assertEqual(result[key], plain[key], key)
        code, result = self.run_nes(image, '--video', str(path), steps=100000)
        self.assertEqual((code, result['status'], path.read_bytes()), (2, 'error', data))
        code, result = self.run_nes(image, '--video-fps', '10')
        self.assertEqual((code, result['status']), (2, 'error'))

    def test_faults_limits_and_invalid_images(self):
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 8d1840')), '--inspect', '0:1')
        self.assertEqual((code, r['status'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 'machine_fault', 0x8002, 'unsupported_bus_write', 0x4018))
        self.assertEqual(r['memory']['bytes'], '00')
        code, r = self.run_nes(cartridge(bytes.fromhex('a955 ad0040')), '--inspect', '0:1')
        self.assertEqual((code, r['a'], r['pc'], r['fault']['operation'], r['fault']['address']),
                         (1, 0x55, 0x8002, 'unsupported_bus_read', 0x4000))
        code, r = self.run_nes(cartridge(b'\x02'))
        self.assertEqual((code, r['fault']['operation'], r['fault']['opcode']), (1, 'unsupported_opcode', 2))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')), '--until-pc', '0x8003')
        self.assertEqual((code, r['status'], r['steps']), (1, 'condition_not_met', 100))
        code, r = self.run_nes(cartridge(bytes.fromhex('4c0080')))
        self.assertEqual((code, r['status'], r['steps']), (0, 'step_limit', 100))
        for mutator in ('magic', 'mapper', 'trainer', 'nes2_extra', 'short'):
            image = cartridge(b'\xea')
            if mutator == 'magic': image[0] = 0
            if mutator == 'mapper': image[6] = 0x10
            if mutator == 'trainer': image[6] = 0x04
            if mutator == 'nes2_extra': image[7], image[10] = 0x08, 0x01
            if mutator == 'short': image.pop()
            code, result = self.run_nes(image)
            self.assertEqual((code, result['status']), (2, 'error'))
        code, result = self.run_nes(cartridge(b'\xea'), '--frame', str(self.file))
        self.assertEqual((code, result['status']), (2, 'error'))

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_start_and_repeat(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, press):
            frame = self.file.parent / (name + '.ppm')
            command = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                       '--steps', '1000000', '--timeout-ms', '20000', '--frame', str(frame)]
            if press:
                command += ['--controller-event', '500000:8', '--controller-event', '550000:0']
            p = subprocess.run(command, capture_output=True, text=True, timeout=25,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            self.assertEqual(result['status'], 'step_limit')
            self.assertEqual(result['rom_sha256'], before)
            if press:
                self.assertEqual(result['controller_schedule'], {'scheduled': 2, 'accepted': 2, 'pending': 0})
            data = frame.read_bytes()
            self.assertTrue(data.startswith(b'P6\n256 240\n255\n'))
            return result, data
        _, title = run('title', False)
        first, started = run('start-1', True)
        second, repeated = run('start-2', True)
        self.assertNotEqual(title, started)
        self.assertEqual(started, repeated)
        self.assertEqual(first, second)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_direction_changes_state(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, move):
            frame = self.file.parent / (name + '.ppm')
            cmd = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                   '--steps', '5000000', '--timeout-ms', '30000', '--inspect', '0:2048', '--frame', str(frame),
                   '--controller-event', '500000:8', '--controller-event', '550000:0']
            if move:
                cmd += ['--controller-event', '2600000:128', '--controller-event', '4500000:0']
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=35,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            self.assertEqual(result['status'], 'step_limit')
            return result, frame.read_bytes()
        neutral, neutral_frame = run('neutral', False)
        moving, moving_frame = run('moving', True)
        self.assertNotEqual(neutral['memory']['bytes'], moving['memory']['bytes'])
        self.assertNotEqual(neutral_frame, moving_frame)
        self.assertEqual(moving['controller_schedule'], {'scheduled': 4, 'accepted': 4, 'pending': 0})
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)

    @unittest.skipUnless(os.environ.get('BOOTHUSH_NES_ROM'), 'NES fixture not supplied')
    def test_mario_audio_capture_noninterference(self):
        source = Path(os.environ['BOOTHUSH_NES_ROM'])
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        if before != '0b3d9e1f01ed1668205bab34d6c82b0e281456e137352e4f36a9b2cfa3b66dea':
            self.skipTest('different cartridge')
        def run(name, audio):
            frame = self.file.parent / (name + '.ppm')
            wav = self.file.parent / (name + '.wav')
            cmd = [os.environ['BOOTHUSH_EXE'], '--machine', 'nes', '--rom', str(source),
                   '--steps', '5000000', '--timeout-ms', '30000', '--inspect', '0:2048', '--frame', str(frame),
                   '--controller-event', '500000:8', '--controller-event', '550000:0']
            if audio: cmd += ['--audio', str(wav)]
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=35,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            result = json.loads(p.stdout)
            if audio:
                with wave.open(str(wav), 'rb') as f:
                    self.assertEqual((f.getnchannels(), f.getsampwidth(), f.getframerate()), (1, 2, 44100))
                    self.assertEqual(f.getnframes(), result['audio_samples'])
                    self.assertTrue(any(f.readframes(f.getnframes())))
            return result, frame.read_bytes()
        silent, frame_a = run('without-audio', False)
        captured, frame_b = run('with-audio', True)
        self.assertEqual(frame_a, frame_b)
        for key in ('steps', 'cpu_cycles', 'pc', 'a', 'x', 'y', 'sp', 'flags', 'apu_writes', 'ppu_frames', 'memory'):
            self.assertEqual(silent[key], captured[key], key)
        self.assertFalse(captured['audio_truncated'])
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)


if __name__ == '__main__':
    unittest.main()
