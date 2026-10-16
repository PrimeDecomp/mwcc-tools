"""Retain serial source-arena off/on native captures; never build a producer.

Uses standalone tracked inputs copied to a new caller-owned output directory.
Capture/object/legacy-trace parity is separate from runtime ledger validation.
Only pv.arena.* is removed for exact legacy parity. Backend coexistence separately
uses diagnostic native FP4/decision comparisons; raw saved CPU values can be
indeterminate and are never erased or guessed to be pointers.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from verify_producer import ROOT, full_trace, trace_line_endings


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def first_difference(first, second):
    for index, (a, b) in enumerate(zip(first, second)):
        if a != b:
            return dict(line=index, disabled=a, enabled=b)
    if len(first) != len(second):
        return dict(line=min(len(first), len(second)),
                    disabled_lines=len(first), enabled_lines=len(second))
    return None


PAIR_POOLS = {1: 4, 2: 3, 3: 2, 4: 1, 5: 4, 6: 4, 7: 4, 8: 4, 9: 4}
POOL_NAMES = ["heap", "optimizer", "block", "local", "global"]


def reconcile_raw(trace, report):
    """Independently count the wire stream; native site guards remain mandatory."""
    events = []
    previous_kind = None
    totals = dict(checkpoints=0, requests=0, allocations=0, resets=0,
                  reset_ends=0, gaps=0)
    requests, resets = [], []
    pairs, operations, allocations = {}, {}, []
    sites, request_depths, reset_depths = {}, set(), set()
    epochs = [0] * 5
    raw_lines = trace.splitlines()
    for position, line in enumerate(raw_lines, 1):
        if not line.startswith("pv.arena."):
            continue
        tokens = line.split()
        fields = dict(token.split("=", 1) for token in tokens[1:])
        assert len(fields) == len(tokens)-1, "duplicate raw field"
        assert fields["schema"] == fields["stream"] == "1"
        event = int(fields["event"])
        assert event == len(events)+1, "missing/reordered wire record"
        kind = tokens[0]
        if kind == "pv.arena.begin":
            assert event == 1 and fields["scope"] == "guarded_retail_arenas_v1"
        elif kind == "pv.arena.request":
            totals["requests"] += 1
            assert int(fields["request"]) == totals["requests"]
            assert int(fields["pair"]) in PAIR_POOLS
            assert int(fields["pool"]) == PAIR_POOLS[int(fields["pair"])]
            assert int(fields["depth"]) == len(requests)+1
            assert int(fields["parent_request"]) == (int(requests[-1]["request"]) if requests else 0)
            assert int(fields["epoch"]) == epochs[int(fields["pool"])]
            requests.append(fields)
            request_depths.add(int(fields["depth"]))
            sites.setdefault(int(fields["pair"]), dict(request=set(), commit=set()))["request"].add(fields["site"])
        elif kind == "pv.arena.alloc":
            totals["allocations"] += 1
            assert int(fields["allocation"]) == totals["allocations"]
            assert fields["complete"] == fields["requested_known"] == "1"
            request = requests.pop()
            for key in ["request", "pair", "pool", "requested", "retail_esp", "epoch"]:
                assert request[key] == fields[key], ("raw request/commit", key)
            requested, reserved = int(fields["requested"]), int(fields["reserved"])
            address = int(fields["address"], 16)
            assert reserved == (requested & ~7)+8
            assert int(fields["freep"], 16) == address+reserved
            assert address and int(fields["block"], 16) and int(fields["free"]) >= 0
            pair = int(fields["pair"])
            pairs[pair] = pairs.get(pair, 0)+1
            sites.setdefault(pair, dict(request=set(), commit=set()))["commit"].add(fields["site"])
            allocations.append(dict(fields, trace_position=position,
                                    inside_reset=bool(resets)))
        elif kind == "pv.arena.reset.begin":
            totals["resets"] += 1
            op, mask, skip = int(fields["op"]), int(fields["mask"]), int(fields["skip"])
            assert mask == {1: 2, 2: 4, 3: 8, 4: 16, 5: 16, 6: 31, 7: 31, 8: 31}[op]
            assert skip == int(op == 3 and int(fields["lock_value"]) != 0)
            assert int(fields["reset"]) == totals["resets"]
            assert int(fields["depth"]) == len(resets)+1
            assert int(fields["parent_reset"]) == (int(resets[-1]["reset"]) if resets else 0)
            assert int(fields["request_depth"]) == len(requests)
            for pool in range(5):
                if not skip and mask & (1 << pool):
                    epochs[pool] += 1
            assert [int(fields["epoch"+str(i)]) for i in range(5)] == epochs
            resets.append(fields)
            reset_depths.add(int(fields["depth"]))
            operations[op] = operations.get(op, 0)+1
        elif kind == "pv.arena.reset.end":
            totals["reset_ends"] += 1
            reset = resets.pop()
            for key in ["reset", "parent_reset", "op", "mask", "skip"]:
                assert fields[key] == reset[key]
            assert int(fields["depth"]) == len(resets)
            assert int(fields["request_depth"]) == len(requests)
            assert [int(fields["epoch"+str(i)]) for i in range(5)] == epochs
        elif kind == "pv.arena.checkpoint":
            totals["checkpoints"] += 1
            assert {key: int(fields[key]) for key in totals} == totals
            assert int(fields["request_depth"]) == len(requests)
            assert int(fields["reset_depth"]) == len(resets)
            assert fields["unsupported"] == "0"
            assert [int(fields["epoch"+str(i)]) for i in range(5)] == epochs
            if int(fields["function"]) == 0:
                assert previous_kind == "pv.arena.reset.end"
            else:
                following = next(line for line in raw_lines[position:] if line.strip())
                assert following.startswith("pv.function.end ")
                following_fields = dict(token.split("=", 1) for token in following.split()[1:])
                assert following_fields["function"] == fields["function"]
        else:
            raise AssertionError("unsupported/gap raw record: " + kind)
        events.append((position, event))
        previous_kind = kind
    assert totals["checkpoints"] > 0
    assert totals["requests"] == totals["allocations"] and not requests
    assert totals["resets"] == totals["reset_ends"] and not resets
    assert events == [(item["trace_position"], item["event"]) for item in report["events"]]
    assert len(events) == sum(item["kind"].startswith("pv.arena.") for item in report["records"])
    assert report["scope"] == "guarded_retail_arenas_v1"
    assert report["coverage_intervals"], "no independently checkpointed query prefix"
    assert all(checkpoint["observations"]["state"] == "satisfied"
               for checkpoint in report["checkpoints"])
    return dict(totals=totals, allocation_pairs=pairs, reset_operations=operations,
                sites={pair: {role: sorted(values) for role, values in roles.items()}
                       for pair, roles in sites.items()},
                request_depths=sorted(request_depths), reset_depths=sorted(reset_depths),
                allocations=allocations)


def compare_backend_history(binary, directory, run):
    """Diagnostic FP4/decision projection; never asserts raw CPU-register parity."""
    reports = []
    derived = directory / "backend-parity"
    derived.mkdir(exist_ok=False)
    derivation = []
    for mode in ["disabled", "enabled"]:
        original = directory / (mode + ".json")
        value = json.loads(original.read_bytes())
        compilation = value["compilation"]
        removed = sum(line.startswith("pv.arena.") for line in compilation["trace"].splitlines())
        compilation["trace"] = "".join(line for line in compilation["trace"].splitlines(keepends=True)
                                         if not line.startswith("pv.arena."))
        compilation["trace_options"]["source_arenas"] = False
        path = derived / (mode + ".json")
        write_json(path, value)
        derivation.append(dict(original=str(original), original_sha256=digest(original),
                               derived=str(path), derived_sha256=digest(path),
                               removed_arena_records=removed,
                               edits="compilation.trace removes only pv.arena.*; compilation.trace_options.source_arenas=false; no other fields edited"))
        reports.append(run([binary, "provenance", "inspect", path, "--json"],
                           derived / (mode + "-provenance.json")))
    write_json(derived / "derivation.json", derivation)

    def decisions(report):
        pointers = {}
        def pointer(value):
            number = int(value, 16)
            if not number:
                return 0
            if number not in pointers:
                pointers[number] = len(pointers)+1
            return pointers[number]
        passes = []
        assert report["decisions"]["state"] == "satisfied"
        for p in report["passes"]:
            assert p["ended"] and p["decisions"]["state"] == "satisfied"
            candidates = []
            for c in p["candidates"]:
                assert c["evidence"]["state"] == "satisfied"
                operands = dict(c["operands"])
                operands["instruction"] = pointer(operands["instruction"])
                candidates.append(dict(id=c["id"], operands=operands,
                                       instruction=pointer(c["instruction"]),
                                       decision=c["decision"], evidence=c["evidence"],
                                       checks=[dict(condition=k["condition"], taken=k["taken"],
                                                    site=k["inputs"]["site"], eflags=k["inputs"]["eflags"],
                                                    raw_input_field_names=sorted(k["inputs"]))
                                               for k in c["checks"]]))
            passes.append(dict(id=p["id"], link=p["link"], class_=p["class"],
                               header=p["header"], candidates=candidates))
        return passes

    before, after = [decisions(report) for report in reports]
    write_json(derived / "decision-projections.json", dict(disabled=before, enabled=after,
               scope="Native validated branch outcomes, saved EFLAGS, operand facts and terminal decisions; candidate instruction pointer equality uses one encounter-ordered bijection. Raw saved GPR values remain retained but uncomparable."))
    assert before == after, "native validated coalescing decisions changed"
    selected = []
    graph_lists = [[(g["link"], g["class"]) for g in report["graphs"]["graphs"]]
                  for report in reports]
    assert graph_lists[0] == graph_lists[1] and graph_lists[0], "graph shape/count changed or empty"
    rounds = {}
    for index, (link, class_) in enumerate(graph_lists[0]):
        round_ = rounds.get((link, class_), 0)
        rounds[(link, class_)] = round_+1
        states = []
        for mode in ["disabled", "enabled"]:
            state = run([binary, "source-search", "state", derived / (mode + ".json"),
                         "--object", directory / (mode + ".o"), "--link", link,
                         "--class", class_, "--round", round_],
                        derived / (str(index) + "-" + mode + "-state.json"))
            assert state["fingerprint_format_version"] == 4
            assert state["normalization"]["state"] == "satisfied" and state["fingerprint"]
            states.append(state)
        assert states[0]["fingerprint"] == states[1]["fingerprint"], (link, class_, round_)
        selected.append(dict(link=link, class_=class_, round=round_, fingerprint=states[0]["fingerprint"]))
    result = dict(state="satisfied", graphs=selected,
                  decisions="native validated decision projections equal",
                  raw_probe_parity="indeterminate: raw saved CPU registers can contain volatile pointers and stale partial-register upper bits",
                  diagnostic_only=True, derivation="derivation.json")
    write_json(derived / "summary.json", result)
    return result


def validate_retained(args):
    """Run native historical/padding/adversarial queries on immutable captures."""
    binary, output = args.binary.resolve(strict=True), args.output.resolve(strict=True)
    evidence = json.loads((output / "verification.json").read_bytes())
    assert digest(binary) == evidence["tools_sha256"], "keep the capture analyzer pinned"
    assert evidence["status"] == "capture_parity_passed_parser_validation_pending"
    validation = output / "runtime-validation"
    validation.mkdir(exist_ok=False)
    commands, versions = [], {}

    def run(command, path):
        command = [str(x) for x in command]
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, timeout=args.timeout)
        path.write_bytes(result.stdout)
        path.with_suffix(".stderr").write_bytes(result.stderr)
        commands.append(dict(argv=command, exit_code=result.returncode,
                             seconds=time.monotonic()-started,
                             stdout=str(path.relative_to(output)),
                             stdout_sha256=digest(path)))
        write_json(validation / "commands.json", commands)
        assert result.returncode == 0, (command, result.stderr.decode(errors="replace"))
        return json.loads(result.stdout)

    def resolve(capture, allocation, label, position=None, address=None, extent=1):
        return run([binary, "source-arenas", "resolve", capture,
                    "--position", position if position is not None else allocation["trace_position"],
                    "--pool", POOL_NAMES[int(allocation["pool"])],
                    "--address", format(address if address is not None else int(allocation["address"], 16), "x"),
                    "--extent", extent], validation / (label + ".json"))

    for case in evidence["cases"]:
        name = case["version"] + "-" + Path(case["fixture"]).stem + ("-backend" if case["backend"] else "-legacy")
        directory = output / name
        assert digest(directory / "arenas.json") == case["report_sha256"]
        report = json.loads((directory / "arenas.json").read_bytes())
        assert report["events"][0]["observation"]["version"] == {
            "GC_1_3": 1, "GC_1_3_2": 2, "GC_2_7": 3}[case["version"]]
        trace = (directory / "enabled.trace").read_bytes().decode("utf-8")
        assert hashlib.sha256(trace.encode("utf-8")).hexdigest() == case["trace_sha256"]
        raw = reconcile_raw(trace, report)
        case["raw_reconciliation"] = {key: value for key, value in raw.items() if key != "allocations"}
        version = versions.setdefault(case["version"], dict(pairs=set(), operations=set(), reuse=[], padding=[]))
        version["pairs"].update(raw["allocation_pairs"])
        version["operations"].update(raw["reset_operations"])
        def covered(position):
            return any(interval["begin_position"] <= position <= interval["end_position"]
                       for interval in report["coverage_intervals"])

        eligible = [a for a in raw["allocations"] if int(a["requested"]) > 0
                    and not a["inside_reset"] and covered(a["trace_position"])]
        assert eligible, (name, "no checkpoint-covered payload allocation")
        previous, reuse = {}, {}
        for allocation in eligible:
            key = (int(allocation["pool"]), allocation["address"])
            old = previous.get(key)
            if old and old["epoch"] != allocation["epoch"]:
                reuse.setdefault(key[0], (old, allocation))
            previous[key] = allocation
        for pool, (old, new) in sorted(reuse.items()):
            first = resolve(directory / "enabled.trace", old, name + "-old-" + str(pool))
            second = resolve(directory / "enabled.trace", new, name + "-new-" + str(pool))
            assert first["observations"]["state"] == second["observations"]["state"] == "satisfied"
            assert first["allocation"]["allocation"] == int(old["allocation"])
            assert second["allocation"]["allocation"] == int(new["allocation"])
            assert first["allocation"]["allocation"] != second["allocation"]["allocation"]
            assert first["allocation"]["epoch"] != second["allocation"]["epoch"]
            version["reuse"].append(dict(fixture=case["fixture"], pool=POOL_NAMES[pool],
                                         address=old["address"], old=first, new=second))
        sample = eligible[0]
        good = resolve(directory / "enabled.trace", sample, name + "-payload")
        assert good["observations"]["state"] == "satisfied"
        assert good["allocation"]["allocation"] == int(sample["allocation"])
        padding = resolve(directory / "enabled.trace", sample, name + "-padding",
                          address=int(sample["address"], 16)+int(sample["requested"]))
        assert padding["allocation"] is None and padding["observations"]["state"] == "indeterminate"
        version["padding"].append(dict(fixture=case["fixture"], payload=good, padding=padding))
        case["native_query_validation"] = dict(
            reused_pools=[POOL_NAMES[pool] for pool in sorted(reuse)],
            payload=good, padding=padding)
        if case["backend"]:
            continue

        # Choose corruption before the selected positive query. Previously proven
        # earlier checkpoint prefixes may remain valid; this query cannot use one.
        lines = trace.splitlines(keepends=True)
        first_request = next(i for i, line in enumerate(lines) if line.startswith("pv.arena.request "))
        first_commit = next(i for i, line in enumerate(lines) if line.startswith("pv.arena.alloc "))
        variants = {}
        for field, value in [("retail_esp", "00000001"), ("pool", "0"),
                             ("site", "00000001"), ("pair", "0")]:
            changed = lines.copy()
            changed[first_request] = re.sub(r"(?<!\S)"+field+r"=\S+", field+"="+value,
                                             changed[first_request])
            variants["bad-request-"+field] = changed
        changed = lines.copy(); changed[first_request] = changed[first_request].rstrip("\r\n")+" unsupported_field=1\n"
        variants["unknown-field"] = changed
        changed = lines.copy(); changed[first_commit] = " ".join(changed[first_commit].split()[:5])+"\n"
        variants["truncated-record"] = changed
        changed = lines.copy(); del changed[first_commit]
        variants["deleted-commit"] = changed
        variants["missing-checkpoints"] = [line for line in lines if not line.startswith("pv.arena.checkpoint ")]
        changed = lines.copy()
        for i, line in enumerate(changed):
            if line.startswith("pv.arena.checkpoint "):
                changed[i] = re.sub(r"(?<!\S)allocations=(\d+)",
                                    lambda match: "allocations="+str(int(match[1])+1), line)
        variants["checkpoint-count"] = changed
        mutation_results = []
        for label, changed in variants.items():
            path = validation / (name + "-" + label + ".trace")
            path.write_bytes("".join(changed).encode("utf-8"))
            inspected = run([binary, "source-arenas", "inspect", path, "--json"],
                            validation / (name + "-" + label + "-inspect.json"))
            assert inspected["observations"]["state"] == "indeterminate", label
            result = resolve(path, sample, name + "-" + label + "-resolve")
            assert result["allocation"] is None and result["observations"]["state"] == "indeterminate", label
            mutation_results.append(dict(name=label, sha256=digest(path), resolution=result))
        case["malformed_raw_records"] = mutation_results

    for version, data in versions.items():
        assert data["pairs"] == set(range(1, 10)), (version, "unexercised allocation pairs", data["pairs"])
        assert {1, 2, 3} <= data["operations"], (version, "ordinary reset coverage", data["operations"])
        assert data["reuse"], (version, "no proven address reuse observed")
        data["pairs"] = sorted(data["pairs"])
        data["operations"] = sorted(data["operations"])
    write_json(validation / "summary.json", dict(versions=versions,
               scope="Guarded fixture paths and historical payload identity only; neither node initialization/type nor source ancestry is established."))
    evidence.update(status="passed", complete=True, runtime_validation="runtime-validation/summary.json",
                    validator_sha256=digest(__file__))
    write_json(output / "verification.json", evidence)
    print(json.dumps(dict(status="passed", versions=len(versions), output=str(output))))


def capture(args):
    binary = args.binary.resolve(strict=True)
    producer = args.oracle_root.resolve(strict=True)
    output = args.output.resolve()
    fixtures = args.fixtures.resolve(strict=True)
    manifest = json.loads((fixtures / "fixture-manifest.json").read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    inputs = output / "inputs"
    shutil.copytree(fixtures, inputs)
    versions = args.version or ["GC_1_3", "GC_1_3_2", "GC_2_7"]
    commands = []
    evidence = dict(format_version=1, complete=False,
                    status="capturing", tools_sha256=digest(binary),
                    backend_coexistence_requested=not args.legacy_only,
                    script_sha256=digest(__file__), producer_root=str(producer),
                    fixture_manifest_sha256=digest(inputs / "fixture-manifest.json"),
                    environment_sha256=hashlib.sha256(json.dumps(
                        sorted(os.environ.items()), ensure_ascii=True,
                        separators=(",", ":")).encode()).hexdigest(),
                    cases=[], limitations=[
                        "Paired fixture observations do not exercise every callback, locked reset, failure or arbitrary compiler input.",
                        "Runtime site presence is not complete original-source ancestry or semantic lifetime correspondence.",
                        "Unknown legacy trace differences fail parity. Backend raw CPU probe parity may be indeterminate; diagnostic FP4 plus validated decision comparisons are separate."])

    def save():
        write_json(output / "verification.json", evidence)
        write_json(output / "commands.json", commands)

    def run(command, destination):
        command = [str(x) for x in command]
        start = time.monotonic()
        result = subprocess.run(command, cwd=inputs, capture_output=True,
                                timeout=args.timeout)
        destination.write_bytes(result.stdout)
        destination.with_suffix(".stderr").write_bytes(result.stderr)
        meta = dict(argv=command, cwd=str(inputs), exit_code=result.returncode,
                    seconds=time.monotonic()-start,
                    stdout=str(destination.relative_to(output)),
                    stdout_sha256=digest(destination),
                    stderr_sha256=digest(destination.with_suffix(".stderr")))
        commands.append(meta)
        save()
        if result.returncode:
            raise RuntimeError("native command failed; original diagnostics retained in " + str(destination))
        return json.loads(result.stdout)

    save()
    try:
        for version in versions:
            for case in manifest["cases"]:
                source = inputs / case["source"]
                assert digest(source) == case["sha256"], "fixture content drift"
                for backend in ([False, True] if case["source"] == "arena_collisions.cpp" and not args.legacy_only else [False]):
                    name = version + "-" + source.stem + ("-backend" if backend else "-legacy")
                    dest = output / name
                    dest.mkdir()
                    row = dict(version=version, fixture=case["source"], backend=backend,
                               source_sha256=digest(source), status="capturing")
                    evidence["cases"].append(row)
                    results = []
                    for enabled in [False, True]:
                        mode = "enabled" if enabled else "disabled"
                        command = [binary, "oracle", "compile-trace", "--oracle-root", producer,
                                   "--version", version, "--working-directory", inputs,
                                   "--stages", "--locals", "--cse", "--blocks", "--schedule",
                                   "--inline-cost", "--full-cost", "--instantiation"]
                        if backend:
                            command.append("--backend")
                        if enabled:
                            command.append("--source-arenas")
                        command += ["--keep-object", dest / (mode + ".o"), source,
                                    "--", *manifest["flags"]]
                        value = run(command, dest / (mode + ".json"))
                        compilation = value["compilation"]
                        assert compilation["version"] == version
                        assert digest(source) == case["sha256"], "fixture changed during compilation"
                        assert compilation["check_status"] == {"status": "verified"}
                        assert compilation["source_sha256"] == case["sha256"]
                        assert compilation["object_sha256"] == digest(dest / (mode + ".o"))
                        (dest / (mode + ".trace")).write_bytes(compilation["trace"].encode("utf-8"))
                        results.append(compilation)
                    off, on = results
                    for key in ["source_sha256", "producer_sha256", "original_sha256",
                                "flags", "object_sha256", "diagnostics"]:
                        assert off[key] == on[key], (name, key)
                    assert not any(line.startswith("pv.arena.") for line in off["trace"].splitlines())
                    arena_records = [line for line in on["trace"].splitlines()
                                     if line.startswith("pv.arena.")]
                    assert arena_records, "arena-on emitted no runtime arena records"
                    remaining = "".join(line for line in on["trace"].splitlines(keepends=True)
                                        if not line.startswith("pv.arena."))
                    difference = first_difference(full_trace(off["trace"]), full_trace(remaining))
                    write_json(dest / "legacy-comparison.json", dict(
                        identical=difference is None, first_difference=difference,
                        removed_family="pv.arena.* only",
                        disabled_records=len(off["trace"].splitlines()),
                        retained_enabled_records=len(remaining.splitlines())))
                    if backend:
                        legacy_off = "".join(line for line in off["trace"].splitlines(keepends=True)
                                             if not line.startswith("pv."))
                        legacy_on = "".join(line for line in remaining.splitlines(keepends=True)
                                            if not line.startswith("pv."))
                        assert full_trace(legacy_off) == full_trace(legacy_on), "pre-backend legacy records changed"
                        assert trace_line_endings(legacy_off) == trace_line_endings(legacy_on)
                        row["raw_backend_trace_parity"] = dict(state="indeterminate" if difference else "satisfied",
                            first_difference=difference,
                            reason="Raw saved CPU-register values cannot be normalized from their textual hex shape.")
                        row["backend_semantic_parity"] = compare_backend_history(binary, dest, run)
                    else:
                        assert difference is None, (name, "preexisting trace difference", difference)
                        assert trace_line_endings(off["trace"]) == trace_line_endings(remaining)
                    report = run([binary, "source-arenas", "inspect", dest / "enabled.trace", "--json"],
                                 dest / "arenas.json")
                    row.update(status="capture_parity_passed_parser_validation_pending",
                               producer_sha256=on["producer_sha256"],
                               original_sha256=on["original_sha256"],
                               object_sha256=on["object_sha256"],
                               arena_records=len(arena_records),
                               trace_sha256=digest(dest / "enabled.trace"),
                               report_sha256=digest(dest / "arenas.json"))
                    save()
                    print(name + (": stock object, legacy traces and native FP4/decisions agree; raw CPU probe parity separate"
                                  if backend else ": stock object and every preexisting trace record agree"), flush=True)
        evidence["status"] = "capture_parity_passed_parser_validation_pending"
        save()
    except Exception as error:
        evidence["status"] = "failed"
        evidence["error"] = repr(error)
        save()
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="new directory; never overwrites retained proof")
    parser.add_argument("--fixtures", type=Path,
                        default=ROOT / "tests/fixtures/source-arenas")
    parser.add_argument("--version", action="append",
                        choices=["GC_1_3", "GC_1_3_2", "GC_2_7"])
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--legacy-only", action="store_true",
                        help="diagnostic subset; omit backend coexistence capture")
    parser.add_argument("--validate-retained", action="store_true",
                        help="validate earlier parity captures from this exact binary; native captures stay immutable")
    args = parser.parse_args()
    if args.validate_retained:
        try:
            validate_retained(args)
        except Exception as error:
            path = args.output.resolve() / "verification.json"
            if path.exists():
                evidence = json.loads(path.read_bytes())
                evidence.update(status="failed", complete=False,
                                runtime_validation_error=repr(error))
                write_json(path, evidence)
            raise
    else:
        capture(args)
