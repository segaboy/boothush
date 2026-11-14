"""Optional black-box check of the pinned port native-68000 C fixture."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('BOOTHUSH_STB_NATIVE_C_ROM'),'native C fixture not supplied')
class MacPortNativeCIntegration(unittest.TestCase):
    def test_native_movement_trace_and_mutation(self):
        rom=Path(os.environ['BOOTHUSH_STB_NATIVE_C_ROM']).resolve(strict=True)
        exe=Path(os.environ['BOOTHUSH_EXE']).resolve(strict=True)
        original=rom.read_bytes();digest=hashlib.sha256(original).hexdigest()
        self.assertEqual(digest,'97f44775d70f8fa83191018b8fc4faa19e5b6a4d9a18f1e866ffac36b3717110')
        def run(image):
            command=[str(exe),'--machine','mac128k','--rom',str(image),'--cycles','2000000',
                     '--timeout-ms','20000','--inspect','0xb00:46']
            result=subprocess.run(command,capture_output=True,text=True,timeout=25,
                                  creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self.assertEqual(result.returncode,0,(result.stdout,result.stderr))
            return json.loads(result.stdout)
        data=run(rom);self.assertEqual(data,run(rom));self.assertEqual(data['status'],'cycle_limit')
        expected=bytes.fromhex('4e4300018140014082c00180848001c086800200882001a0'
                               '896001408a4000e08ac000808ae000208ae00000a55a')
        self.assertEqual(bytes.fromhex(data['memory']['bytes']),expected)
        needle=bytes.fromhex('06400040');self.assertEqual(original.count(needle),1)
        self.assertGreaterEqual(original.index(needle),0x200)
        with tempfile.TemporaryDirectory(dir=os.environ.get('BOOTHUSH_TEST_TMP')) as temp:
            mutant=Path(temp)/'bad-acceleration.rom'
            mutant.write_bytes(original.replace(needle,bytes.fromhex('06400020'),1))
            damaged=bytes.fromhex(run(mutant)['memory']['bytes'])
            self.assertEqual(damaged[4:8],bytes.fromhex('81200120'))
            self.assertEqual(damaged[-2:],bytes.fromhex('a55a'))
            self.assertNotEqual(damaged,expected)
        self.assertEqual(hashlib.sha256(rom.read_bytes()).hexdigest(),digest)


if __name__=='__main__':unittest.main()
