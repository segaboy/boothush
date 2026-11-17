import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
from test_boot import BootHushTestCase,make_rom
from test_pc286 import make_rom as pc_rom
from test_mac_keyboard_integration import (make_keyboard_rom,move_byte,exchange,VIA_PORT_A,
    VIA_DIRECTION_A,LEVEL_ONE_VECTOR,HANDLER_OFFSET,VIA_IER,RESULT)

@unittest.skipUnless(os.environ.get('BOOTHUSH_LIVE_KEY_TEST_EXE'),'hidden live-key CLI binary not supplied')
class LiveKeyTests(BootHushTestCase):
    def test_relative_mouse_reaches_record_and_replay(self):
        for mode,gain in (('relative',100),('relative-free',100),('relative-free',400)):
            with self.subTest(mode=mode,gain=gain):
                self.check_relative_replay(mode,gain)

    def check_relative_replay(self,mode,gain):
        stem=mode+str(gain);trace=self.scratch/(stem+'.bhmouse');frame=self.scratch/(stem+'.pbm');pointer=self.scratch/(stem+'.pointer')
        env=os.environ.copy();env['BOOTHUSH_TEST_RELATIVE']='1'
        cmd=[env['BOOTHUSH_LIVE_KEY_TEST_EXE'],'--machine','mac128k','--rom',str(self.rom),
             '--live','1','--live-mouse',mode,'--mouse-record',str(trace),'--frame',str(frame),
             '--live-mouse-gain',str(gain),'--live-pointer-log',str(pointer),'--cycles','20000000','--timeout-ms','5000','--inspect','0xb00:4']
        r=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(r.returncode,0,(r.stdout,r.stderr));data=json.loads(r.stdout)
        self.assertEqual(data['relative_mouse'],{'packets':1,'captures':1,'releases':1,'captured_at_stop':False})
        self.assertEqual(data['relative_mouse_mode'],'unconfined' if mode=='relative-free' else 'confined')
        self.assertEqual(data['relative_mouse_gain'],{'initial_percent':gain,'final_percent':gain,'changes':0})
        self.assertEqual(data['mouse'],{'samples':1,'steps':128*gain//100,'pending':0})
        self.assertEqual(list(map(int,trace.read_text().splitlines()[1].split()))[1:],[64*gain//100,0,0])
        lines=pointer.read_text().splitlines()
        self.assertEqual(lines[0],'BHLIVEPOINTER1 1 0')
        row=list(map(int,lines[2].split()))
        self.assertEqual(row[1:],[1,64,0,64*gain//100,0,0,0,0,1,gain])
        original=pointer.read_bytes()
        retry=[x for x in cmd]
        for option in ('--mouse-record','--frame'):
            i=retry.index(option);del retry[i:i+2]
        again=subprocess.run(retry,env=env,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(again.returncode,2)
        self.assertEqual(pointer.read_bytes(),original)
        replay_frame=self.scratch/(stem+'-replay.pbm')
        replay=self.boot('--mouse-replay',trace,'--frame',replay_frame,'--inspect','0xb00:4',cycles=20000000,timeout_ms=5000)
        self.assertEqual(replay['memory'],data['memory']);self.assertEqual(frame.read_bytes(),replay_frame.read_bytes())

    def test_raw_input_error_reports_operation_and_code(self):
        env=os.environ.copy();env['BOOTHUSH_TEST_RELATIVE_ERROR']='1'
        cmd=[env['BOOTHUSH_LIVE_KEY_TEST_EXE'],'--machine','mac128k','--rom',str(self.rom),
             '--live','1','--live-mouse','relative-free','--cycles','20000000','--timeout-ms','5000']
        r=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(r.returncode,1,(r.stdout,r.stderr));data=json.loads(r.stdout)
        self.assertEqual(data['status'],'input_error');self.assertEqual(data['error'],'GetRawInputData')
        self.assertEqual(data['relative_mouse_error']['operation'],'GetRawInputData')
        self.assertNotEqual(data['relative_mouse_error']['win32_error'],0)
        self.assertEqual(data['relative_mouse']['captures'],1);self.assertEqual(data['relative_mouse']['releases'],1)

    def test_keys_reach_both_guests_and_focus_releases_are_recorded(self):
        for machine in ('mac128k','pc286'):
            with self.subTest(machine=machine):
                rom=self.scratch/(machine+'.rom');log=self.scratch/(machine+'.keys')
                if machine=='mac128k':
                    program=(move_byte(0,VIA_PORT_A)+move_byte(0x10,VIA_DIRECTION_A)+
                        struct.pack('>HII',0x23fc,0x400000+HANDLER_OFFSET,LEVEL_ONE_VECTOR)+move_byte(0x84,VIA_IER)+
                        b''.join(exchange(0x10 if i==0 else 0x14,RESULT+i) for i in range(4))+bytes.fromhex('60fe'))
                    data=make_keyboard_rom();base=make_rom(program);data[:HANDLER_OFFSET]=base[:HANDLER_OFFSET]
                    rom.write_bytes(data);inspect='0xb00:4';expected='01078187'
                else:
                    rom.write_bytes(pc_rom(bytes.fromhex('fa31c08ed8be0006b90600e464a80174fae460880446e2f3ebfe')))
                    inspect='0x600:6';expected='1c2bf01cf02b'
                cmd=[os.environ['BOOTHUSH_LIVE_KEY_TEST_EXE'],'--machine',machine,'--rom',str(rom),
                     '--live','1','--live-key-log',str(log),'--cycles','20000000','--timeout-ms','5000','--inspect',inspect]
                r=subprocess.run(cmd,capture_output=True,text=True,timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(r.returncode,0,(r.stdout,r.stderr));d=json.loads(r.stdout)
                self.assertEqual(d['status'],'closed');self.assertEqual(d['memory']['bytes'],expected)
                live=d['live_session'];self.assertEqual(live['keys_accepted'],4);self.assertEqual(live['focus_releases'],2)
                self.assertEqual(live['pending_keys'],0);self.assertFalse(live['human_verified'])
                lines=log.read_text().splitlines();self.assertEqual(lines[0].split()[:2],['BHLIVEKEY1',machine])
                events=[list(map(int,s.split())) for s in lines[2:]];self.assertEqual(len(events),4)
                self.assertEqual([e[2] for e in events],[1,1,0,0]);self.assertEqual([e[5] for e in events],[0,0,1,1])
                self.assertTrue(all(0<=e[0]<=d['cycles'] for e in events))
                # Logs never overwrite previous evidence.
                original=log.read_bytes();r=subprocess.run(cmd,capture_output=True,text=True,timeout=10,
                    creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(r.returncode,2);self.assertEqual(log.read_bytes(),original)

    def test_log_requires_live_and_pc_live_refuses_scheduled_input(self):
        rom=self.scratch/'pc.rom';rom.write_bytes(pc_rom(bytes.fromhex('ebfe')))
        for options in (['--live-key-log',str(self.scratch/'bad.log')],['--live','1','--scancode','100:0x29'],['--live','1','--live-mouse','relative']):
            r=subprocess.run([os.environ['BOOTHUSH_EXE'],'--machine','pc286','--rom',str(rom),
                '--cycles','10000','--timeout-ms','1000',*options],capture_output=True,text=True,timeout=5,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.assertEqual(r.returncode,2)

    def test_gain_requires_relative_mode_and_valid_range(self):
        for opts in (['--live','1','--live-mouse-gain','100'],['--live','1','--live-mouse','window','--live-mouse-gain','100'],
                     ['--live','1','--live-mouse','relative','--live-mouse-gain','24'],
                     ['--live','1','--live-mouse','relative','--live-mouse-gain','801']):
            cmd=[os.environ['BOOTHUSH_EXE'],'--machine','mac128k','--rom',str(self.rom),
                 '--cycles','10000','--timeout-ms','1000',*opts]
            r=subprocess.run(cmd,capture_output=True,text=True,timeout=5,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.assertEqual(r.returncode,2)
