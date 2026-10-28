"""The client of `mwcc-tools serve`: requests as JSON lines on its stdin, results routed back by id."""
import itertools
import json
import os
import queue
import shutil
import subprocess
import threading
from pathlib import Path

#: The methods' schema version this package speaks (mwcc-methods' SCHEMA_VERSION).
SCHEMA_VERSION = 1


class EngineError(RuntimeError):
    """A method failed, or the server ended."""


def find_binary():
    """MWCC_TOOLS, else the executable of the installed release this package is in, or the release build of the
    checkout it is in (so the two match), else `mwcc-tools` on the path."""
    override = os.environ.get('MWCC_TOOLS')
    if override:
        return override
    root = Path(__file__).resolve().parents[2]
    for directory in (root / 'bin', root / 'target' / 'release'):
        for name in ('mwcc-tools.exe', 'mwcc-tools'):
            built = directory / name
            if built.is_file():
                return str(built)
    found = shutil.which('mwcc-tools')
    if found:
        return found
    raise EngineError('mwcc-tools not found: install it (README.md, Installation), build it in the mwcc checkout '
                      '(cargo build --release -p mwcc-tools), or set MWCC_TOOLS to it')


_END = object()


class Engine:
    """One `mwcc-tools serve` and its session (what it loads stays loaded while it runs).

    engine.call(method, **params) iterates over the method's results; engine.call_one gives the only one.
    Calls may come from several threads at once.
    """

    def __init__(self, project=None, binary=None, command=None, env=None):
        if command is None:
            command = [binary or find_binary(), 'serve']
            if project is not None:
                command += ['--project', str(project)]
        self._process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env,
            text=True, encoding='utf-8', bufsize=1)
        self._ids = itertools.count(1)
        self._pending = {}
        self._lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._ended = False
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()
        #: The last call's timing (milliseconds by stage), as the server gives it.
        self.last_timing = None
        hello = self.call_one('session.hello')
        if hello.get('schema_version') != SCHEMA_VERSION:
            self.close()
            raise EngineError('mwcc-tools speaks schema %s, this package %s: update one of them'
                              % (hello.get('schema_version'), SCHEMA_VERSION))
        self.version = hello.get('version')
        self.methods = hello.get('methods', [])

    def _read(self):
        for line in self._process.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                message = json.loads(line)
            except ValueError:
                continue
            with self._lock:
                waiting = self._pending.get(json.dumps(message.get('id')))
            if waiting is not None:
                waiting.put(message)
        with self._lock:
            self._ended = True
            for waiting in self._pending.values():
                waiting.put(_END)

    def _send(self, request):
        line = json.dumps(request) + '\n'
        with self._write_lock:
            try:
                self._process.stdin.write(line)
                self._process.stdin.flush()
            except (BrokenPipeError, OSError, ValueError) as exc:
                raise EngineError('the server has ended') from exc

    def call(self, method, **params):
        """Iterate over METHOD's results (each a dict). An error raises EngineError; stopping early cancels it."""
        request_id = next(self._ids)
        key = json.dumps(request_id)
        waiting = queue.Queue()
        with self._lock:
            if self._ended:
                raise EngineError('the server has ended')
            self._pending[key] = waiting
        finished = False
        try:
            self._send({'id': request_id, 'method': method, 'params': params})
            while True:
                message = waiting.get()
                if message is _END:
                    finished = True
                    raise EngineError('the server ended during %s' % method)
                if 'result' in message:
                    yield message['result']
                elif message.get('done'):
                    finished = True
                    self.last_timing = message.get('timing')
                    return
                elif 'error' in message:
                    finished = True
                    raise EngineError('%s: %s' % (method, message['error'].get('message')))
        finally:
            if not finished:
                self.cancel(request_id)
            with self._lock:
                self._pending.pop(key, None)

    def call_one(self, method, **params):
        """METHOD's only result."""
        results = list(self.call(method, **params))
        if len(results) != 1:
            raise EngineError('%s gave %d results, not one' % (method, len(results)))
        return results[0]

    def cancel(self, request_id):
        """Stop the request REQUEST_ID (its results so far stand)."""
        try:
            self._send({'id': 'cancel-%s' % request_id, 'method': 'cancel', 'params': {'id': request_id}})
        except EngineError:
            pass

    def close(self):
        """End the server."""
        if self._process.poll() is None:
            try:
                self._process.stdin.close()
            except OSError:
                pass
            try:
                self._process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        self._reader.join(timeout=5)
        if not self._reader.is_alive():
            self._process.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Unit:
    """A project's unit in an engine: loaded once, then compiled and scored by its handle."""

    def __init__(self, engine, name, project=None):
        self.engine = engine
        params = {'unit': name}
        if project is not None:
            params['project'] = str(project)
        loaded = engine.call_one('unit.load', **params)
        self.handle = loaded['handle']
        self.info = loaded['unit']
        self.report = loaded.get('report')
        self.name = name
        #: The last experiment's summary (candidates, statuses, the controls, why it stopped, its timing).
        self.last_summary = None

    def compile(self, files=None, **options):
        """The unit compiled with FILES ({path relative to the project: text, or None for absent}) in place of the
        disk's; OPTIONS are unit.compile's (inline_max_size, trace, trace_path, object_path, timeout_ms)."""
        return self.engine.call_one('unit.compile', handle=self.handle, files=files or {}, **options)

    def score(self, files=None, built=False, symbols=None, rows=False, **options):
        """The unit's object scored against its target: compiled with FILES, or the build's (BUILT), or OPTIONS'
        `object`; SYMBOLS limits the functions."""
        params = dict(options, handle=self.handle, files=files or {}, built=built, rows=rows)
        if symbols is not None:
            params['symbols'] = list(symbols)
        return self.engine.call_one('unit.score', **params)

    def experiment(self, output, space, stages=None, strategy=None, budget=None, **plan):
        """Yield each finished candidate's row (as results.jsonl gets it) of a plan run in OUTPUT, a new directory
        (or one to resume): SPACE, e.g. {'variants': {'text': ...}}, {'list': [...]}, {'axes': [...]},
        {'combinations': {...}} or {'inline_max_size': [...]}; STAGES (default compile then score), STRATEGY,
        BUDGET and the plan's other fields (files, baseline, symbols, jobs, keep_objects, diff, timeout_ms).
        Stopping early cancels the run; the rows written stay. The summary is then `last_summary`."""
        plan = dict(plan, output=str(output), space=space)
        for key, value in (('stages', stages), ('strategy', strategy), ('budget', budget)):
            if value is not None:
                plan[key] = value
        self.last_summary = None
        for result in self.engine.call('experiment.run', handle=self.handle, plan=plan):
            if 'row' in result:
                yield result['row']
            elif 'summary' in result:
                self.last_summary = result['summary']


class Trace:
    """A trace the engine keeps (trace.load): a unit's traced compile, or a saved trace (`load=PREFIX`).

    Its colouring methods take `function` and `class_` (GPR, FPR, VR or CR), and edits as lists such as
    ['degree', 35, 1], ['move', 40, 'after', 37], ['drop', 35], ['link', 35, 40], ['unlink', 35, 40],
    ['neighbours', 35, 1] and ['order', [35, 36, 37]].
    """

    def __init__(self, engine, unit=None, **params):
        self.engine = engine
        if isinstance(unit, Unit):
            params['handle'] = unit.handle
        elif unit is not None:
            params['unit'] = unit
        loaded = engine.call_one('trace.load', **params)
        self.handle = loaded['trace']
        self.functions = loaded['functions']
        self.version = loaded['version']

    def _call(self, method, function=None, class_=None, **params):
        if function is not None:
            params['function'] = function
        if class_ is not None:
            params['class'] = class_
        return self.engine.call_one(method, trace=self.handle, **params)

    def verify(self, function=None, class_=None, text=False):
        return self._call('trace.verify', function, class_, text=text)

    def target(self, function, class_=None, wants=None, text=False):
        return self._call('trace.target', function, class_, wants=wants or {}, text=text)

    def force(self, function, class_=None, wants=None, text=False):
        return self._call('trace.force', function, class_, wants=wants or {}, text=text)

    def replay(self, function, edits, class_=None, wants=None, text=False):
        return self._call('trace.replay', function, class_, edits=list(edits), wants=wants or {}, text=text)

    def replay_many(self, function, candidates, class_=None, wants=None, colours=False):
        """Each edit list of CANDIDATES replayed on the same graph: one result per list, in order."""
        return self._call('trace.replay_many', function, class_, candidates=[list(c) for c in candidates],
                          wants=wants or {}, colours=colours)

    def search(self, function, class_=None, wants=None, text=False, **options):
        """OPTIONS: iterations, seed, restarts, top, fixed, nodes, kinds."""
        return self._call('trace.search', function, class_, wants=wants or {}, text=text, **options)

    def realize(self, function, edits, class_=None, limit=None, text=False):
        """One solution's EDITS (search's `exact` or `best`) as source variants of the traced text: the result's
        `candidates` are an experiment's space (`{'list': result['candidates']}`), each labelled `suggested`."""
        params = {'edits': list(edits), 'text': text}
        if limit is not None:
            params['limit'] = limit
        return self._call('trace.realize', function, class_, **params)
