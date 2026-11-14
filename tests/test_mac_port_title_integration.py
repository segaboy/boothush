"""Optional black-box integration with the pinned Not Super Tilt Bro ROM."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('BOOTHUSH_STB_TITLE_ROM'), 'port title ROM not supplied')
class MacPortTitleIntegration(unittest.TestCase):
    def test_mouse_start_keyboard_play_and_repeatable_capture(self):
        rom=Path(os.environ['BOOTHUSH_STB_TITLE_ROM']).resolve(strict=True)
        exe=Path(os.environ['BOOTHUSH_EXE']).resolve(strict=True)
        admitted=('d84d0ab3a1c981c2cbfb1a8ec039c189f3f83500ae90672f19b16ddb976df54e',
                  '1a367b8eceb9f032e20b679e9224cedf44cde1fc4615720e25710d23f0bd839e')
        digest=hashlib.sha256(rom.read_bytes()).hexdigest()
        self.assertIn(digest,admitted)
        exe_hash=hashlib.sha256(exe.read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            root=Path(temp)
            def run(name):
                frames=root/(name+'.pbmseq');keys=root/(name+'.keyboard')
                cmd=[str(exe),'--machine','mac128k','--rom',str(rom),'--cycles','7000000','--timeout-ms','20000',
                     '--mouse-dcd-invert','3','--mouse-motion','1000000:16:0',
                     '--mouse-button','2000000:1','--mouse-button','2400000:0',
                     '--key','3500000:0:1','--key','4500000:0:0',
                     '--inspect','0xb00:86','--keyboard-witness',str(keys),'--capture-series',str(frames),
                     '--capture-memory','0xb00:86','--capture-vbl','12','--capture-vbl','25','--capture-vbl','46']
                p=subprocess.run(cmd,capture_output=True,text=True,timeout=25,
                                 creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr))
                return json.loads(p.stdout),keys.read_bytes(),frames.read_bytes()
            result,keys,frames=run('first')
            self.assertEqual((result,keys,frames),run('repeat'))
            self.assertEqual(result['status'],'cycle_limit')
            self.assertEqual(result['mouse']['pending'],0)
            witness=result['keyboard_witness']
            self.assertEqual((witness['accepted'],witness['pending'],witness['rejected'],witness['dropped']),(2,0,0,0))
            rows=[line.split() for line in keys.decode('ascii').splitlines()[2:]]
            self.assertEqual([int(r[3]) for r in rows if r[2]=='key_delivered'],[1,129])
            memory=bytes.fromhex(result['memory']['bytes'])
            self.assertEqual(memory[:8],b'NST4\0\0\0\1')
            self.assertEqual(memory[73],2)
            self.assertEqual(int.from_bytes(memory[64:66],'big'),272)
            self.assertGreater(int.from_bytes(memory[16:18],'big'),0x8000)
            series=result['frame_series']
            self.assertEqual((series['requested'],series['processed'],series['pending'],series['dropped']),(3,3,0,0))
            self.assertEqual((series['rom_sha256'],series['executable_sha256']),(digest,exe_hash))
            self.assertEqual([sample['frame'] for sample in series['samples']],[12,25,46])
            states=[bytes.fromhex(sample['memory']['bytes'])[73] for sample in series['samples']]
            self.assertEqual(states,[1,2,2])
            images=[frames[s['offset']:s['offset']+s['length']] for s in series['samples']]
            self.assertTrue(all(image.startswith(b'P4\n512 342\n') and len(image)==21899 for image in images))
            self.assertEqual(len(set(images)),3)
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),digest)


if __name__=='__main__':unittest.main()
