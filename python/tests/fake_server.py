"""A stand-in for `mwcc-tools serve`, speaking its protocol: session.hello (schema FAKE_SCHEMA, default 1), echo
(one result per item of `items`), unit.load, experiment.run (a row per candidate of a list space, then the
summary), fail (an error), wait (results until cancelled), and exit (the server ends)."""
import json
import os
import sys
import threading
import time

out_lock = threading.Lock()
cancelled = set()


def send(message):
    with out_lock:
        sys.stdout.write(json.dumps(message) + '\n')
        sys.stdout.flush()


def handle(request):
    rid, method, params = request.get('id'), request.get('method'), request.get('params') or {}
    timing = {'load': 0, 'compile': 1.5, 'score': 0, 'trace': 0, 'model': 0, 'queue': 0, 'other': 0}
    if method == 'session.hello':
        send({'id': rid, 'result': {'schema_version': int(os.environ.get('FAKE_SCHEMA', '1')),
                                    'version': 'fake', 'methods': ['echo', 'fail', 'wait']}})
    elif method == 'echo':
        for index, item in enumerate(params.get('items', [])):
            time.sleep(float(params.get('delay', 0)))
            send({'id': rid, 'result': {'index': index, 'item': item}})
    elif method == 'unit.load':
        send({'id': rid, 'result': {'handle': 'fake|' + params.get('unit', ''), 'unit': {'source': 'a.c'}}})
    elif method == 'experiment.run':
        candidates = params['plan']['space']['list']
        for candidate in candidates:
            send({'id': rid, 'result': {'row': {'name': candidate['name'], 'status': 'ok'}}})
        send({'id': rid, 'result': {'summary': {'candidates': len(candidates), 'output': params['plan']['output']},
                                    'timing': timing}})
    elif method == 'fail':
        send({'id': rid, 'error': {'message': 'it failed'}})
        return
    elif method == 'wait':
        count = 0
        while json.dumps(rid) not in cancelled:
            send({'id': rid, 'result': {'tick': count}})
            count += 1
            time.sleep(0.01)
        send({'id': rid, 'error': {'message': 'cancelled'}})
        return
    elif method == 'cancel':
        cancelled.add(json.dumps(params.get('id')))
        send({'id': rid, 'result': {'cancelled': True}})
    elif method == 'exit':
        os._exit(0)
    else:
        send({'id': rid, 'error': {'message': 'no method %s' % method}})
        return
    send({'id': rid, 'done': True, 'timing': timing})


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
