"""A stand-in for `mwcc-tools serve --project ROOT`, speaking its protocol (JSON lines), for the experiment
tests (the Echoes kit's tests have a copy).

session.hello; unit.load; unit.score (built, `scores`: SYMBOL's entry, `percent` widened from an f32 as the
engine's JSON gives it; MISSING is not found); experiment.run with a `list` space: each candidate is ROOT's files,
the plan's baseline over them and its own over that, written to OUTPUT/NNNN. A candidate with FAIL in a file is a
compile error; one equal to an earlier one is its `duplicate_of`; SLOW waits while ROOT/hold exists, until
cancelled. Rows go to
OUTPUT/results.jsonl, and a run in an existing directory resumes (another plan is refused); `keep_objects`
writes object.o and `diff` diff.json. Every request is answered on its own thread; `cancel` stops one.
"""
import json
from pathlib import Path
import struct
import sys
import threading
import time

ROOT = Path(sys.argv[sys.argv.index('--project') + 1]) if '--project' in sys.argv else Path.cwd()
out_lock = threading.Lock()
cancelled = set()
TIMING = {'load': 0, 'compile': 0, 'score': 0, 'trace': 0, 'model': 0, 'queue': 0, 'other': 0}


def send(message):
    with out_lock:
        sys.stdout.write(json.dumps(message) + '\n')
        sys.stdout.flush()


def widened(value):
    return struct.unpack('f', struct.pack('f', value))[0]


def entry(symbol, percent):
    return dict(percent=widened(percent), original=4, emitted=4, differing=0 if percent == 100 else 1,
                exact_instructions=percent == 100)


def run(rid, plan):
    out = Path(plan['output'])
    candidates = plan['space']['list']
    shared = {k: plan.get(k) for k in ('files', 'baseline', 'symbols', 'space')}
    done = set()
    if out.exists():
        if not (out / 'manifest.json').is_file():
            raise ValueError(f'{out} exists and is not a run to resume (it has no manifest.json)')
        if json.loads((out / 'manifest.json').read_text())['plan'] != shared:
            raise ValueError(f'{out} is a run of another plan')
        results = out / 'results.jsonl'
        if results.exists():
            done = {json.loads(line)['name'] for line in results.read_text().splitlines() if line.strip()}
    out.mkdir(parents=True, exist_ok=True)
    (out / 'manifest.json').write_text(json.dumps({'plan': shared, 'argv': ['mwcceppc.exe', '-c', 'fake']}))
    disk = {p: (ROOT / p).read_text(encoding='utf-8') if (ROOT / p).is_file() else None for p in plan['files']}
    start = dict(disk, **(plan.get('baseline') or {}))
    seen = {}
    for index, candidate in enumerate(candidates):
        if json.dumps(rid) in cancelled:
            break
        files = dict(start, **candidate.get('files', {}))
        key = json.dumps(files, sort_keys=True)
        name = candidate['name']
        directory = out / f'{index:04d}'
        row = dict(name=name, directory=str(directory))
        if key in seen:
            row.update(status=seen[key]['status'], duplicate_of=seen[key]['name'])
            if 'scores' in seen[key]:
                row['scores'] = seen[key]['scores']
        else:
            for path, text in files.items():
                if text is not None:
                    (directory / path).parent.mkdir(parents=True, exist_ok=True)
                    (directory / path).write_text(text, encoding='utf-8')
            texts = ''.join(t for t in files.values() if t)
            while 'SLOW' in texts and (ROOT / 'hold').exists() and json.dumps(rid) not in cancelled:
                time.sleep(0.01)
            if json.dumps(rid) in cancelled:
                break
            directory.mkdir(parents=True, exist_ok=True)
            if 'FAIL' in texts:
                (directory / 'console.txt').write_text('fake: error: FAIL\n')
                row['status'] = 'compile_error'
            else:
                row['status'] = 'ok'
                row['scores'] = {s: entry(s, 100.0 if 'MATCH' in texts else 99.8387) for s in plan.get('symbols', [])}
                if plan.get('keep_objects'):
                    (directory / 'object.o').write_text(texts)
                if plan.get('diff'):
                    (directory / 'diff.json').write_text('{}')
            seen[key] = row
        if name in done:
            continue
        with (out / 'results.jsonl').open('a') as stream:
            stream.write(json.dumps(row) + '\n')
        send({'id': rid, 'result': {'row': row}})
    send({'id': rid, 'result': {'summary': {'candidates': len(candidates), 'resumed': len(done)}, 'timing': TIMING}})


def handle(request):
    rid, method, params = request.get('id'), request.get('method'), request.get('params') or {}
    try:
        if method == 'session.hello':
            send({'id': rid, 'result': {'schema_version': 1, 'version': 'fake', 'methods': []}})
        elif method == 'unit.load':
            send({'id': rid, 'result': {'handle': 'unit:' + params['unit'], 'unit': {'name': params['unit']}}})
        elif method == 'unit.score':
            scores = {}
            for query in params.get('scores', []):
                if query == 'MISSING':
                    raise ValueError('missing comparison symbol: MISSING')
                scores[query] = entry(query, 99.8387)
            send({'id': rid, 'result': {'status': 'ok', 'scores': scores}})
        elif method == 'experiment.run':
            run(rid, params['plan'])
        elif method == 'cancel':
            cancelled.add(json.dumps(params.get('id')))
            send({'id': rid, 'result': {'cancelled': True}})
        else:
            raise ValueError('no method ' + str(method))
    except Exception as error:  # noqa: BLE001 - every failure is the request's error
        send({'id': rid, 'error': {'message': str(error)}})
        return
    send({'id': rid, 'done': True, 'timing': TIMING})


def main():
    threads = []
    for line in sys.stdin:
        if line.strip():
            thread = threading.Thread(target=handle, args=(json.loads(line),))
            thread.start()
            threads.append(thread)
    for thread in threads:
        thread.join()


if __name__ == '__main__':
    main()
