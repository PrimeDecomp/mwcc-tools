"""run_variants, score_symbols and run_matrix against a fake server of a project (fake_project_server.py)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mwcc_tools import (Engine, checked_replace, matrix_cases, refresh_coverage, run_matrix,  # noqa: E402
                        run_variants, score_symbols)

FAKE = Path(__file__).with_name('fake_project_server.py')
UNIT = 'main/Example'
BUILD = ['ninja', 'build/G2ME01/src/Example.o']


class Project(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        (self.root / 'objdiff.json').write_text(json.dumps({'units': [{'name': UNIT, 'base_path': BUILD[-1]}]}))
        (self.root / 'a.cpp').write_bytes(b'original\r\n')
        (self.root / 'a.h').write_text('original header')
        self.engine = Engine(command=[sys.executable, str(FAKE), '--project', str(self.root)])
        self.addCleanup(self.engine.close)

    def variants(self, variants, **options):
        options = dict(dict(files=['a.cpp', 'a.h', 'new.h'], build=BUILD, output_dir='results', root=self.root,
                            symbols=['Example'], engine=self.engine, quiet=True), **options)
        return run_variants(variants, **options)


class Variants(Project):
    def test_headers_absent_files_and_a_compile_error(self):
        rows = self.variants([('first', {'a.cpp': 'MATCH', 'a.h': 'different', 'new.h': b'new'}),
                              ('fails', {'a.cpp': 'FAIL'}), ('no header', {'a.h': None})])
        self.assertEqual([r['status'] for r in rows], ['ok', 'compile_error', 'ok'])
        self.assertEqual(rows[0]['scores']['Example']['percent'], 100.0)
        self.assertEqual(rows[2]['scores']['Example']['percent'], 99.8387)  # (objdiff's f32, as it wrote it)
        self.assertEqual((self.root / 'results/0000/new.h').read_text(), 'new')
        self.assertFalse((self.root / 'results/0002/a.h').exists())
        self.assertEqual((self.root / 'a.cpp').read_bytes(), b'original\r\n')

    def test_evaluate_writes_evaluations_and_a_duplicate_reuses_its_original(self):
        calls = []

        def evaluate(candidate):
            calls.append(candidate['name'])
            if candidate['files']['a.cpp'].read_text() == 'bad':
                raise ValueError('evaluation failed')
            return {'object': candidate['object'].read_text()}
        rows = self.variants([('good', {'a.cpp': 'good'}), ('bad', {'a.cpp': 'bad'}), ('again', {'a.cpp': 'bad'})],
                             evaluate=evaluate)
        self.assertEqual(calls, ['good', 'bad'])
        self.assertEqual([r['status'] for r in rows], ['ok', 'evaluation_error', 'evaluation_error'])
        self.assertEqual(rows[2]['duplicate_of'], 'bad')
        evaluations = [json.loads(line) for line in (self.root / 'results/evaluations.jsonl').read_text().splitlines()]
        self.assertEqual([e['status'] for e in evaluations], ['ok', 'evaluation_error', 'evaluation_error'])

    def test_a_resume_evaluates_what_an_interrupted_evaluation_left(self):
        variants = [('one', {'a.cpp': 'one'}), ('two', {'a.cpp': 'two'})]
        calls = []

        def evaluate(candidate):
            calls.append(candidate['name'])
            if calls == ['one']:
                raise KeyboardInterrupt()
            return {'evaluated': candidate['name']}
        with self.assertRaises(KeyboardInterrupt):
            self.variants(variants, evaluate=evaluate)
        rows = self.variants(variants, evaluate=evaluate)
        self.assertEqual({r['name']: r['scores'] for r in rows}, {'one': {'evaluated': 'one'}, 'two': {'evaluated': 'two'}})

    def test_errors_before_anything_runs(self):
        with self.assertRaisesRegex(TypeError, r'evaluate\(candidate\)'):
            self.variants([('x', {})], evaluate=lambda: {})
        with self.assertRaisesRegex(ValueError, 'no objdiff unit'):
            self.variants([('x', {})], build=['ninja', 'build/G2ME01/main.elf'])
        with self.assertRaisesRegex(ValueError, 'missing from files'):
            self.variants([('x', {'other.cpp': 'x'})])
        with self.assertRaisesRegex(ValueError, 'UTF-8'):
            self.variants([('x', {'a.cpp': b'\xff'})])
        self.assertFalse((self.root / 'results').exists())

    def test_score_symbols(self):
        scores = score_symbols(UNIT, ['Example'], root=self.root, engine=self.engine)
        self.assertEqual(scores['Example']['percent'], 99.8387)
        with self.assertRaisesRegex(ValueError, 'Missing comparison symbol: MISSING'):
            score_symbols(UNIT, ['MISSING'], root=self.root, engine=self.engine)


class Matrix(Project):
    def cases(self, axes, render, **options):
        return run_matrix(axes, render, files=['a.cpp'], build=BUILD, output_dir='out', root=self.root,
                          engine=self.engine, quiet=True, **options)

    def test_coverage_counts_evaluations_skips_and_generation_errors(self):
        def render(choice):
            if choice['case'] == 'generate':
                return {'a.cpp': checked_replace('BASE', 'MISSING', 'edit')}
            return {'a.cpp': {'ok': 'OK', 'duplicate': 'OK', 'compile': 'FAIL', 'evaluate': 'EVALUATE'}[choice['case']]}

        def evaluate(candidate):
            if candidate['files']['a.cpp'].read_text() == 'EVALUATE':
                raise ValueError('evaluation failed')
            return {'fixture': True}
        coverage = self.cases({'case': ['ok', 'duplicate', 'compile', 'evaluate', 'generate', 'skip']}, render,
                              evaluate=evaluate, skip=lambda c: 'not this one' if c['case'] == 'skip' else None)
        self.assertEqual(coverage['counts'], {'ok': 2, 'compile_error': 1, 'evaluation_error': 1,
                                              'generation_error': 1, 'skipped': 1})
        self.assertEqual(coverage['cases'][1]['duplicate_of'], '0000')
        self.assertEqual(json.loads((self.root / 'out/coverage.json').read_text())['counts'], coverage['counts'])

    def test_an_interrupted_evaluation_leaves_the_rest_untested(self):
        def evaluate(candidate):
            if candidate['files']['a.cpp'].read_text() == 'interrupt':
                raise KeyboardInterrupt()
            return {'fixture': True}
        with self.assertRaises(KeyboardInterrupt):
            self.cases({'case': ['ok', 'interrupt', 'unreached']}, lambda c: {'a.cpp': c['case']}, evaluate=evaluate)
        self.assertEqual(refresh_coverage(self.root / 'out')['counts'], {'ok': 1, 'untested': 2})
        self.assertTrue((self.root / 'out/runner-error.json').exists())

    def test_axes(self):
        for axes in [{}, {'a': []}, {'a': ['x', 'x']}, {'a': 'xy'}]:
            with self.assertRaises(ValueError):
                matrix_cases(axes)
        self.assertEqual(len(matrix_cases({'a': ['x', 'y'], 'b': ['z', 'w']})), 4)
        with self.assertRaises(ValueError):
            matrix_cases({'a': ['x', 'y'], 'b': ['z', 'w']}, max_cases=3)


if __name__ == '__main__':
    unittest.main()
