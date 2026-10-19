"""The kit scripts' reports from methods' results (no server)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mwcc_tools import reports  # noqa: E402


class Reports(unittest.TestCase):
    def test_try_variants(self):
        result = {
            'unit': 'main/X', 'fuzzy': 99.5, 'identical_to_build': False, 'warnings': ['w'],
            'function': {'function': 'f__Fv', 'demangled': 'f()', 'percent': 90.0},
            'variants': [
                {'name': 'a', 'status': 'ok', 'fuzzy': 99.6, 'delta': 0.1, 'percent': 95.0, 'percent_delta': 5.0,
                 'changed': [{'function': 'k()', 'before': 50.0, 'after': 60.0},
                             {'function': 'g()', 'before': 100.0, 'after': 90.0}],
                 'unpaired': ['h']},
                {'name': 'b', 'status': 'compile_error',
                 'error': 'src\\x.cpp: old text occurs 0 times (must be exactly once): "a\\n\\"b\\""'},
                {'name': 'c', 'status': 'compile_error', 'error': 'compile failed: x.cpp:1: error'}],
            'applied': 'a', 'applied_files': ['src/x.cpp']}
        self.assertEqual(reports.try_variants(result), [
            'main/X: unmodified fuzzy 99.50%, f() 90.00%  (WARNING: differs from the build object; run ninja, or '
            'the source has __FILE__/path-dependent code)',
            'warning: w',
            '  a: 95.00% (+5.00), fuzzy 99.60% (+0.10)',
            '      REGRESSION g(): 100.00% -> 90.00%',
            '      k(): 50.00% -> 60.00%',
            '      no longer paired: h',
            # (an edit's error as try-variants printed it: without the file, the old text in Python's quoting)
            "  b: old text occurs 0 times (must be exactly once): 'a\\n\"b\"'",
            '  c: compile failed: x.cpp:1: error',
            "applied 'a' to src/x.cpp"])

    def test_try_variants_without_a_function(self):
        result = {'unit': 'main/X', 'fuzzy': 100.0, 'identical_to_build': True, 'warnings': [],
                  'variants': [{'name': 'a', 'status': 'ok', 'fuzzy': 99.0, 'delta': -1.0, 'changed': [],
                                'unpaired': []}]}
        self.assertEqual(reports.try_variants(result),
                         ['main/X: unmodified fuzzy 100.00%', '  a: fuzzy 99.00% (-1.00)'])

    def test_inline_sweep(self):
        result = {'units': [
            {'unit': 'main/A', 'configured': 128, 'configured_fuzzy': 100.0, 'source_sets': False,
             'sizes': [{'size': 16, 'status': 'ok', 'fuzzy': 97.05,
                        'changed': [{'function': 'B()', 'before': 100.0, 'after': 32.2},
                                    {'function': 'A()', 'before': None, 'after': 50.0}]}]},
            {'unit': 'main/E', 'error': 'boom', 'sizes': []},
            {'unit': 'main/S', 'source_sets': True, 'configured': 128, 'configured_fuzzy': 90.0,
             'sizes': [{'size': 16, 'status': 'compile_error', 'console': 'x' * 290 + 'the end of it'}]}]}
        self.assertEqual(reports.inline_sweep(result), [
            'main/A                                           128: 100.00   16:  97.05   -',
            '          - ->  50.00  A()',
            '     100.00 ->  32.20  B()',
            'main/E: boom',
            'main/S: the source sets its own inline_max_size, so the flag measures nothing here',
            'main/S: 16 compile failed: ' + 'x' * 287 + 'the end of it'])

    def test_residuals(self):
        result = {'rows': [
            {'unit': 'main/B', 'function': 'b', 'match': 50.0, 'size': 40, 'register': 1, 'stack': 0, 'other': 3,
             'verdict': 'structural'},
            {'unit': 'main/A', 'function': 'a__Fv', 'demangled': 'a()', 'match': 99.0, 'size': 8, 'register': 2,
             'stack': 0, 'other': 0, 'verdict': 'register-only'},
            {'unit': 'main/A', 'function': 'c', 'match': 99.5, 'size': 8, 'register': 0, 'stack': 1, 'other': 0,
             'verdict': 'stack-only'}],
            'failed': [{'unit': 'main/F', 'error': 'no target'}],
            'summary': {'structural': 1, 'register-only': 1, 'stack-only': 1}}
        self.assertEqual(reports.residuals(result), [
            '== main/A',
            '   stack-only      99.50%      8B  reg   0 stack   1 other   0  c',
            '   register-only   99.00%      8B  reg   2 stack   0 other   0  a()',
            '== main/B',
            '   structural      50.00%     40B  reg   1 stack   0 other   3  b',
            '== main/F: no target',
            '',
            'register-only: 1, stack-only: 1, structural: 1'])

    def test_sinit_profile(self):
        result = {'profiles': [{'name': 'control', 'line': '5 ins'}, {'name': 'TARGET', 'line': 'no __sinit_'}]}
        self.assertEqual(reports.sinit_profile(result), ['control | 5 ins', 'TARGET | no __sinit_'])


if __name__ == '__main__':
    unittest.main()
