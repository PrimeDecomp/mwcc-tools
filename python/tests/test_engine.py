"""The client against a fake server (fake_server.py), and against `mwcc-tools serve` on a project when
MWCC_TEST_ECHOES names an Echoes checkout (and MWCC_TOOLS or the checkout's release build is there)."""
import json
import os
import sys
import tempfile
import threading
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mwcc_tools import Engine, EngineError, Trace, Unit  # noqa: E402
from mwcc_tools import engine as engine_module  # noqa: E402

FAKE = [sys.executable, str(Path(__file__).with_name('fake_server.py'))]


def fake(schema=None):
    env = dict(os.environ)
    if schema is not None:
        env['FAKE_SCHEMA'] = str(schema)
    return Engine(command=FAKE, env=env)


class FakeServer(unittest.TestCase):
    def test_results_stream_in_order_with_the_timing(self):
        with fake() as engine:
            self.assertEqual(engine.version, 'fake')
            results = list(engine.call('echo', items=['a', 'b', 'c']))
            self.assertEqual([r['item'] for r in results], ['a', 'b', 'c'])
            self.assertEqual(engine.last_timing['compile'], 1.5)

    def test_errors_raise(self):
        with fake() as engine:
            with self.assertRaisesRegex(EngineError, 'it failed'):
                list(engine.call('fail'))
            # (the engine goes on)
            self.assertEqual(engine.call_one('echo', items=[1])['item'], 1)

    def test_a_schema_mismatch_is_refused(self):
        with self.assertRaisesRegex(EngineError, 'schema 2'):
            fake(schema=2)

    def test_calls_from_threads_get_their_own_results(self):
        with fake() as engine:
            got = {}

            def run(name):
                got[name] = [r['item'] for r in engine.call('echo', items=[name] * 5, delay=0.01)]

            threads = [threading.Thread(target=run, args=('t%d' % i,)) for i in range(4)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
            self.assertEqual(got, {'t%d' % i: ['t%d' % i] * 5 for i in range(4)})

    def test_stopping_early_cancels_the_request(self):
        with fake() as engine:
            for result in engine.call('wait'):
                if result['tick'] == 3:
                    break
            # (the server is not left busy: a new call answers)
            self.assertEqual(engine.call_one('echo', items=['x'])['item'], 'x')

    def test_a_server_that_ends_fails_its_calls(self):
        engine = fake()
        with self.assertRaisesRegex(EngineError, 'ended'):
            list(engine.call('exit'))
        with self.assertRaises(EngineError):
            list(engine.call('echo', items=[1]))
        engine.close()

    def test_an_experiment_yields_rows_then_keeps_the_summary(self):
        with fake() as engine:
            unit = Unit(engine, 'main/A')
            rows = list(unit.experiment('out', {'list': [{'name': 'a'}, {'name': 'b'}]}, budget={'compiles': 2}))
            self.assertEqual([r['name'] for r in rows], ['a', 'b'])
            self.assertEqual(unit.last_summary, {'candidates': 2, 'output': 'out'})


class FindBinary(unittest.TestCase):
    def test_an_installed_release_runs_its_own_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'python' / 'mwcc_tools'
            package.mkdir(parents=True)
            (package / 'engine.py').write_text('')
            name = 'mwcc-tools.exe' if os.name == 'nt' else 'mwcc-tools'
            (root / 'bin').mkdir()
            (root / 'bin' / name).write_text('')
            environment = {k: v for k, v in os.environ.items() if k != 'MWCC_TOOLS'}
            with unittest.mock.patch.object(engine_module, '__file__', str(package / 'engine.py')), \
                    unittest.mock.patch.dict(os.environ, environment, clear=True):
                self.assertEqual(Path(engine_module.find_binary()), root / 'bin' / name)


@unittest.skipUnless(os.environ.get('MWCC_TEST_ECHOES'), 'MWCC_TEST_ECHOES is not set')
class RealServer(unittest.TestCase):
    def test_a_unit_compiles_and_scores(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
            built = unit.score(built=True)
            self.assertEqual(built['status'], 'ok')
            compiled = unit.score()
            self.assertEqual(compiled['compile']['status'], 'ok')
            self.assertEqual(compiled['score']['fuzzy'], built['score']['fuzzy'])
            broken = unit.compile(files={unit.info['source'].replace('\\', '/'): 'this is not C++'})
            self.assertEqual(broken['status'], 'compile_error')
            self.assertTrue(broken['console'])

    def test_an_experiment_runs_with_its_controls(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine, tempfile.TemporaryDirectory() as scratch:
            unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
            source = unit.info['source'].replace('\\', '/')
            space = {'list': [{'name': 'same'}, {'name': 'broken', 'files': {source: 'this is not C++'}},
                              {'name': 'no match', 'edits': {source: [['@@ nowhere @@', 'x']]}}]}
            rows = {r['name']: r for r in unit.experiment(Path(scratch) / 'run', space, jobs=2)}
            self.assertEqual(rows['same']['status'], 'ok')
            self.assertEqual(rows['broken']['status'], 'compile_error')
            self.assertEqual(rows['no match']['status'], 'evaluation_error')
            control = unit.last_summary['control']
            self.assertTrue(control['identical_to_build'])
            self.assertTrue(control['deterministic'])
            self.assertEqual(rows['same']['fuzzy'], control['fuzzy'])
            lines = (Path(scratch) / 'run' / 'results.jsonl').read_text().splitlines()
            self.assertEqual(len(lines), 3)
            # (a second run in the directory resumes it: nothing left to evaluate)
            self.assertEqual(list(unit.experiment(Path(scratch) / 'run', space, jobs=2)), [])
            self.assertEqual(unit.last_summary['resumed'], 3)

    def test_a_stopped_experiment_resumes_and_another_plan_is_refused(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine, tempfile.TemporaryDirectory() as scratch:
            unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
            output = Path(scratch) / 'sweep'
            space = {'inline_max_size': [0, 20, 40, 60, 80, 100]}
            for _ in unit.experiment(output, space, jobs=1):
                break
            rows = list(unit.experiment(output, space, jobs=2))
            names = [r['name'] for r in rows]
            written = [line for line in (output / 'results.jsonl').read_text().splitlines() if line]
            self.assertEqual(len(written), 6)
            self.assertEqual(len(set(names)), len(names))
            self.assertEqual(unit.last_summary['resumed'] + unit.last_summary['evaluated'], 6)
            self.assertTrue(unit.last_summary['control']['deterministic'])
            with self.assertRaisesRegex(EngineError, 'another plan'):
                list(unit.experiment(output, {'inline_max_size': [0]}))

    def test_residuals_have_residual_scans_rows(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            result = engine.call_one('unit.residuals', unit='main/MetroidPrime/ScriptLoader', built=True)
            sinit = [r for r in result['rows'] if r['function'] == '__sinit_ScriptLoader_cpp']
            self.assertEqual(len(sinit), 1)
            self.assertEqual(set(sinit[0]), {'unit', 'function', 'demangled', 'match', 'size', 'register', 'stack',
                                             'other', 'verdict'})
            self.assertEqual(sum(result['summary'].values()), len(result['rows']))
            compiled = engine.call_one('unit.residuals', unit='main/MetroidPrime/ScriptLoader')
            self.assertEqual(compiled['rows'], result['rows'])

    def test_an_inline_sweep_scores_sizes_and_the_sinit_profile_its_stores(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            swept = engine.call_one('unit.inline_sweep', unit='main/MetroidPrime/CControlMapper', sizes=[0, 200])
            unit = swept['units'][0]
            self.assertEqual((unit['configured'], unit['configured_fuzzy']), (128, 100.0))
            self.assertLess(unit['sizes'][0]['fuzzy'], 100.0)
            self.assertTrue(unit['sizes'][0]['changed'])
            profiles = engine.call_one('unit.sinit_profile', unit='main/MetroidPrime/ScriptLoader')['profiles']
            self.assertEqual([p['name'] for p in profiles], ['current', 'TARGET'])
            self.assertTrue(profiles[1]['line'].startswith('1424 ins stmw r14, frame -0x620(r1)'))

    def test_source_variants_report_as_try_variants(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
            path = Path(os.environ['MWCC_TEST_ECHOES']) / unit.info['source']
            first = path.read_text(encoding='utf-8').splitlines()[0]
            text = ('### no inlining\n@@@ old\n%s\n@@@ new\n#pragma inline_max_size(0)\n%s\n'
                    '### bad\n@@@ old\nnothing like this\n@@@ new\nx\n'
                    '### broken\n@@@ old\n%s\n@@@ new\nthis is not C++\n') % (first, first, first)
            before = path.read_bytes()
            result = engine.call_one('source.variants', handle=unit.handle, variants=text,
                                     function='ResetCommandOverrides')
            self.assertTrue(result['identical_to_build'])
            self.assertEqual(result['function']['demangled'], 'CControlMapper::ResetCommandOverrides()')
            by_name = {v['name']: v for v in result['variants']}
            self.assertLess(by_name['no inlining']['percent_delta'], 0)
            self.assertTrue(by_name['no inlining']['changed'])
            self.assertIn('0 times', by_name['bad']['error'])
            self.assertTrue(by_name['broken']['error'].startswith('compile failed'))
            with self.assertRaisesRegex(EngineError, 'did not compile'):
                engine.call_one('source.variants', handle=unit.handle, variants=text, apply='broken')
            self.assertEqual(path.read_bytes(), before)

    def test_the_oracle_traces_a_unit_and_explains_register_differences(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            traced = engine.call_one('oracle.trace', unit='main/MetroidPrime/CControlMapper',
                                     function='ResetCommandOverrides', views={'text': True, 'locals': True})
            self.assertTrue(traced['identical'])
            self.assertEqual(traced['document']['schema_version'], 6)
            self.assertTrue(traced['text'])
            explained = engine.call_one('oracle.regdiff', unit='main/MetroidPrime/Player/CPlayer',
                                        function='UpdateWaterSurfaceCameraBias__7CPlayerFR13CStateManager', text=True)
            self.assertEqual(explained['document']['compilation']['project_object_identical'], True)
            self.assertIn('== register differences (ours -> target, aligned rows) ==', explained['text'])

    def test_a_loaded_trace_replays_colouring_edits(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine:
            function = 'InitializeEffects__9CAnimDataFR13CStateManager7TAreaIdRC9CVector3f'
            trace = Trace(engine, 'main/MetroidPrime/CAnimData', function=function)
            self.assertTrue(trace.verify(function)['result']['ok'])
            wrong = trace.target(function)['result']['wrong']
            self.assertTrue(wrong)
            fixed = trace.replay(function, [['degree', 57, 1], ['link', 57, 71]], text=True)
            self.assertEqual(fixed['result']['wrong'], [])
            many = trace.replay_many(function, [[], [['degree', 57, 1], ['link', 57, 71]], [['bogus']]])
            self.assertEqual([len(r['wrong']) for r in many['results'][:2]], [len(wrong), 0])
            self.assertIn('unknown edit', many['results'][2]['error'])

    def test_duplicates_are_evaluated_once_and_equal_objects_scored_once(self):
        with Engine(project=os.environ['MWCC_TEST_ECHOES']) as engine, tempfile.TemporaryDirectory() as scratch:
            unit = Unit(engine, 'main/MetroidPrime/CControlMapper')
            source = unit.info['source'].replace('\\', '/')
            text = (Path(os.environ['MWCC_TEST_ECHOES']) / source).read_text(encoding='utf-8')
            commented = text + '\n// a comment\n'
            space = {'list': [{'name': 'base'}, {'name': 'commented', 'files': {source: commented}},
                              {'name': 'base again'}, {'name': 'commented again', 'files': {source: commented}}]}
            output = Path(scratch) / 'dedup'
            # (one compile: the first candidate, and its duplicate)
            rows = {r['name']: r for r in unit.experiment(output, space, budget={'compiles': 1}, jobs=1)}
            self.assertEqual(sorted(rows), ['base', 'base again'])
            self.assertEqual(rows['base again']['duplicate_of'], 'base')
            self.assertEqual(rows['base again']['fuzzy'], rows['base']['fuzzy'])
            # (the control compiled the baseline first: the cache answers its equal; its object was scored)
            self.assertTrue(rows['base'].get('cached'))
            summary = unit.last_summary
            self.assertEqual((summary['duplicates'], summary['compile_hits']), (1, 1))
            self.assertGreaterEqual(summary['score_hits'], 1)
            # (the rest on a resume: a comment changes the files, not the object)
            rows = {r['name']: r for r in unit.experiment(output, space, jobs=1)}
            self.assertEqual(sorted(rows), ['commented', 'commented again'])
            self.assertEqual(rows['commented again']['duplicate_of'], 'commented')
            self.assertFalse(rows['commented'].get('cached'))
            self.assertGreaterEqual(unit.last_summary['score_hits'], 1)
            variant = json.loads((Path(rows['commented again']['directory']) / 'variant.json').read_text())
            self.assertEqual(variant['duplicate_of'], 'commented')


if __name__ == '__main__':
    unittest.main()
