"""Original synthetic trace checks for censored CPU-mask span reporting."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('mask_probe', Path(__file__).resolve().parents[1] / 'scripts/probe_mac_cpu_mask.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class CpuMaskProbeTests(unittest.TestCase):
    def report(self, header, rows):
        with tempfile.TemporaryDirectory() as root:
            trace = Path(root) / 'mask.trace'
            trace.write_text(header + '\nseq cycle kind address reg value\n' + rows, encoding='ascii')
            return probe.summarize(trace)

    def test_censored_edges_excluded_from_complete_maximum(self):
        result = self.report('BHMACMOUSE2 100 1000 1000 4 0 1 1 0 0 0',
                             '0 110 cpu_mask 100 8 7\n1 900 cpu_mask 102 7 0\n'
                             '2 910 cpu_mask 104 0 7\n3 920 cpu_mask 106 7 0\n')
        self.assertEqual(result['complete_max_cycles_by_mask'], {'0': 10, '7': 10})
        self.assertTrue(result['closed_spans'][0]['left_censored'])
        self.assertEqual(result['closed_spans'][0]['cycles'], 790)
        self.assertEqual(result['right_censored'], {'start': 920, 'mask': 0})

    def test_constant_mask_is_only_a_censored_observation(self):
        result = self.report('BHMACMOUSE2 100 1000 1000 1 0 1 1 0 0 0', '0 110 cpu_mask 100 8 7\n')
        self.assertEqual(result['complete_max_cycles_by_mask'], {})
        self.assertEqual(result['closed_spans'], [])

    def test_incomplete_and_discontinuous_evidence_rejected(self):
        good = 'BHMACMOUSE2 100 1000 1000 2 0 1 1 0 0 0'
        rows = '0 110 cpu_mask 100 8 7\n1 900 cpu_mask 102 7 0\n'
        for header, body in (
                (good.replace('2 0 1 1', '2 1 0 1'), rows),
                (good.replace('2 0 1 1', '2 0 1 0'), rows),
                (good.replace('1000 2', '1000 3'), rows),
                (good, rows.replace('8 7', '0 7')),
                (good, rows.replace('102 7 0', '102 2 0'))):
            with self.subTest(header=header, body=body), self.assertRaises(ValueError):
                self.report(header, body)


if __name__ == '__main__':
    unittest.main()
