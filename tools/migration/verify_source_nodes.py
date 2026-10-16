"""Retain native source-node off/on captures without building a producer.

Guarded observations are distinct from source ancestry and node equivalence.
Only pv.source.* and pv.arena.* are removed for legacy-record comparison.
Backend coexistence reuses the established FP4/decision diagnostic; raw saved
CPU inputs are never guessed to be pointers or erased to manufacture parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from verify_producer import ROOT, full_trace, trace_line_endings
from verify_source_arena_runtime import compare_backend_history, first_difference


FAMILIES = ("pv.source.", "pv.arena.")
SITES = {"GC_1_3": 0x4f1c9e, "GC_1_3_2": 0x4f274e, "GC_2_7": 0x4f360e}
VERSIONS = {"GC_1_3": 1, "GC_1_3_2": 2, "GC_2_7": 3}
SHAPES = {**{tag: 1 for tag in [*range(9), 48]},
          **{tag: 2 for tag in range(9, 48)}, 53: 3, 56: 4,
          50: 5, 61: 6, 62: 6, 67: 7}
CHILDREN = {1: 1, 2: 2, 3: 3, 7: 4}
WORDS = {1: 1, 2: 2, 3: 3, 4: 1, 5: 2, 6: 1, 7: 4}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def fields(line):
    tokens = line.split()
    parsed = dict(token.split("=", 1) for token in tokens[1:])
    assert len(parsed) == len(tokens)-1, "duplicate wire field"
    return tokens[0], parsed


def reconcile_raw(trace, report, version):
    """Count actual wire facts independently; no inferred allocation or roles."""
    totals = {key: 0 for key in ["checkpoints", "owners", "owner_ends", "modes",
                               "mode_ends", "clones", "clone_ends", "emissions",
                               "emission_ends", "snapshots", "gaps"]}
    stacks = {key: [] for key in ["owner", "mode", "clone", "emission"]}
    frames, roots, nodes, pointer_reads, nesting = {}, {}, {}, {}, []
    actual_events, emissions, clones, opaque = [], [], [], []
    path, tree_key, previous_kind = [], None, None
    tree_nodes = []
    lines = trace.splitlines()

    def end_tree():
        for node in tree_nodes:
            count = CHILDREN.get(node["shape"], 0)
            expected = {i+1 for i in range(count) if node["data"][i]}
            assert set(node["children"]) == expected, "missing/extra child snapshot"
        tree_nodes.clear()
        path.clear()

    def current_owner():
        return int(stacks["owner"][-1]["owner"]) if stacks["owner"] else 0

    for position, line in enumerate(lines, 1):
        if not line.startswith("pv.source."):
            continue
        kind, f = fields(line)
        assert f["schema"] == f["stream"] == "1"
        assert int(f["event"]) == len(actual_events)+1, "missing/reordered source event"
        actual_events.append(position)
        if kind != "pv.source.node":
            end_tree()
            tree_key = None
        if kind == "pv.source.begin":
            assert len(actual_events) == 1 and f["scope"] == "guarded_source_nodes_v1"
            assert int(f["version"]) == VERSIONS[version]
        elif kind.startswith("pv.source.owner."):
            if kind.endswith(".begin"):
                totals["owners"] += 1
                assert int(f["owner"]) == totals["owners"]
                assert int(f["parent_owner"]) == current_owner()
                assert int(f["depth"]) == len(stacks["owner"])+1 <= 64
                stacks["owner"].append(f)
                nesting.append(("owner", int(f["owner"])))
            else:
                assert nesting.pop() == ("owner", int(f["owner"])), "crossed source scopes"
                begin = stacks["owner"].pop()
                for key in ["owner", "parent_owner", "function"]:
                    assert f[key] == begin[key]
                assert int(f["depth"]) == len(stacks["owner"])
                totals["owner_ends"] += 1
        elif kind.startswith(("pv.source.mode.", "pv.source.clone.")):
            role = "mode" if ".mode." in kind else "clone"
            parent_key = "parent_"+role
            if kind.endswith(".begin"):
                totals[role+"s"] += 1
                invocation = int(f["invocation"])
                assert invocation == totals[role+"s"]
                assert int(f[parent_key]) == (int(stacks[role][-1]["invocation"]) if stacks[role] else 0)
                assert int(f["owner"]) == current_owner()
                assert int(f["depth"]) == len(stacks[role])+1 <= 64
                if role == "clone":
                    assert int(f["mode_scope"]) == (int(stacks["mode"][-1]["invocation"]) if stacks["mode"] else 0)
                stacks[role].append(f)
                nesting.append((role, invocation))
                frames[(role, invocation)] = dict(begin=position, wire=f, stages=[])
            else:
                assert nesting.pop() == (role, int(f["invocation"])), "crossed source scopes"
                begin = stacks[role].pop()
                for key in ["invocation", parent_key, "owner", "input"] + (["mode_scope"] if role == "clone" else []):
                    assert f[key] == begin[key]
                assert int(f["depth"]) == len(stacks[role])
                assert int(f["owner"]) == current_owner()
                totals[role+"_ends"] += 1
                invocation = int(f["invocation"])
                expected_stages = ([2, 3] if role == "clone" else [5, 6]) if int(f["input"], 16) else []
                if int(f["output"], 16):
                    expected_stages.append(4 if role == "clone" else 7)
                assert frames[(role, invocation)]["stages"] == expected_stages, "source stage order changed"
                for stage, key in zip(([2, 3, 4] if role == "clone" else [5, 6, 7]),
                                      ["input_snapshot", "after_snapshot", "output_snapshot"]):
                    pointer = int(f["output" if key == "output_snapshot" else "input"], 16)
                    expected = roots.get((stage, invocation), 0)
                    assert int(f[key]) == expected
                    assert not pointer if not expected else nodes[expected]["node"] == pointer
                if role == "clone":
                    row = dict(invocation=invocation, parent_clone=int(f["parent_clone"]), mode_scope=int(f["mode_scope"]),
                               begin=frames[(role, invocation)]["begin"],
                               end=position, input=f["input"], output=f["output"],
                               snapshots=[int(f[k]) for k in ["input_snapshot", "after_snapshot", "output_snapshot"]])
                    observed = [nodes[n]["position"] for n in row["snapshots"] if n]
                    assert observed == sorted(observed) and len(set(n for n in row["snapshots"] if n)) == len(observed)
                    clones.append(row)
        elif kind.startswith("pv.source.emission."):
            assert int(f["site"], 16) == SITES[version]
            if kind.endswith(".begin"):
                totals["emissions"] += 1
                assert int(f["emission"]) == totals["emissions"]
                assert int(f["owner"]) == current_owner()
                assert int(f["depth"]) == len(stacks["emission"])+1 <= 64
                stacks["emission"].append(f)
                nesting.append(("emission", int(f["emission"])))
            else:
                assert nesting.pop() == ("emission", int(f["emission"])), "crossed source scopes"
                begin = stacks["emission"].pop()
                for key in ["emission", "owner", "function", "current_function", "statement", "root", "site"]:
                    assert f[key] == begin[key]
                assert int(f["depth"]) == len(stacks["emission"])
                totals["emission_ends"] += 1
                snapshot = roots.get((1, int(f["emission"])), 0)
                assert int(f["snapshot"]) == snapshot
                assert not int(f["root"], 16) if not snapshot else nodes[snapshot]["node"] == int(f["root"], 16)
                emissions.append(dict(emission=int(f["emission"]), snapshot=snapshot,
                                      root=f["root"], position=position))
        elif kind == "pv.source.node":
            totals["snapshots"] += 1
            snapshot = int(f["snapshot"])
            assert snapshot == totals["snapshots"]
            stage, frame = int(f["stage"]), int(f["frame"])
            role = {1: "emission", 2: "clone", 3: "clone", 4: "clone", 5: "mode", 6: "mode", 7: "mode"}[stage]
            assert stacks[role]
            assert frame == int(stacks[role][-1]["emission" if role == "emission" else "invocation"])
            assert nesting[-1] == (role, frame), "node observed outside most specific source frame"
            parent, edge = int(f["parent"]), int(f["edge"])
            new_tree = (stage, frame) != tree_key
            if new_tree:
                end_tree()
                tree_key = (stage, frame)
                assert parent == edge == 0 and tree_key not in roots
                roots[tree_key] = snapshot
                if role != "emission":
                    frames[(role, frame)]["stages"].append(stage)
            else:
                assert parent != 0, "extra root in one stage tree"
            assert (parent == 0) == (edge == 0)
            node = dict(snapshot=snapshot, position=position, node=int(f["node"], 16),
                        stage=stage, frame=frame, shape=int(f["shape"]), tag=int(f["tag"]),
                        data=[int(f["data"+str(i)], 16) for i in range(4)], children={})
            assert node["node"] and snapshot not in nodes
            if f["complete"] == "1":
                assert f["tag_known"] == "1" and f["reason"] == "ok"
                assert node["shape"] == SHAPES.get(node["tag"]) != 0
                assert all(n == 0 for n in node["data"][WORDS[node["shape"]]:])
            else:
                assert f["complete"] == "0" and node["shape"] == 0
                assert int(f["flags"], 16) == int(f["type"], 16) == 0 and node["data"] == [0]*4
                if f["reason"] == "unsupported":
                    assert f["tag_known"] == "1" and node["tag"] not in SHAPES
                else:
                    assert f["reason"] in ["depth", "capacity", "pointer", "cycle"]
                    assert f["tag_known"] == "0" and node["tag"] == 0
                opaque.append(dict(snapshot=snapshot, tag=node["tag"], reason=f["reason"]))
            if parent:
                while path and path[-1] != parent:
                    path.pop()
                assert path and edge <= CHILDREN.get(nodes[parent]["shape"], 0)
                assert edge not in nodes[parent]["children"]
                assert nodes[parent]["data"][edge-1] == node["node"]
                nodes[parent]["children"][edge] = snapshot
            path.append(snapshot)
            nodes[snapshot] = node
            tree_nodes.append(node)
            pointer_reads.setdefault(node["node"], []).append(snapshot)
        elif kind == "pv.source.gap":
            totals["gaps"] += 1
        elif kind == "pv.source.checkpoint":
            totals["checkpoints"] += 1
            assert {key: int(f[key]) for key in totals} == totals
            assert [int(f[role+"_depth"]) for role in stacks] == [len(stacks[role]) for role in stacks]
            assert int(f["unsupported"]) == int(totals["gaps"] != 0)
            if previous_kind not in ["pv.source.owner.end", "pv.source.mode.end"]:
                following = next(line for line in lines[position:] if line.strip())
                assert following.startswith("pv.arena.checkpoint ")
        else:
            raise AssertionError("unrecognized source record " + kind)
        previous_kind = kind
    end_tree()
    assert totals["checkpoints"] and totals["snapshots"] and not any(stacks.values()) and not nesting
    assert totals["owners"] == totals["owner_ends"] and totals["modes"] == totals["mode_ends"]
    assert totals["clones"] == totals["clone_ends"] and totals["emissions"] == totals["emission_ends"]
    assert actual_events == [r["line"] for r in report["records"]]
    assert totals["snapshots"] == len(report["nodes"])
    assert totals["emissions"] == len(report["emissions"])
    return dict(totals=totals, emissions=emissions, clones=clones, opaque=opaque,
                repeated_node_pointers=[dict(pointer=f"{p:08x}", snapshots=reads)
                                        for p, reads in pointer_reads.items() if len(reads) > 1],
                scope="Independent raw counts/tree/actual call snapshots; repeated pointers alone are not allocation identity or equivalent nodes")


def new_families_removed(trace):
    return "".join(line for line in trace.splitlines(keepends=True)
                   if not line.startswith(FAMILIES))


def backend_diagnostic(binary, directory, run):
    derived = directory / "backend-inputs"
    derived.mkdir(exist_ok=False)
    rows = []
    for mode in ["disabled", "enabled"]:
        original = directory / (mode+".json")
        value = json.loads(original.read_bytes())
        compilation = value["compilation"]
        compilation["trace"] = new_families_removed(compilation["trace"])
        compilation["trace_options"]["source_nodes"] = False
        compilation["trace_options"]["source_arenas"] = False
        path = derived / (mode+".json")
        write_json(path, value)
        shutil.copyfile(directory / (mode+".o"), derived / (mode+".o"))
        rows.append(dict(original=str(original), original_sha256=digest(original),
                         derived=str(path), derived_sha256=digest(path),
                         edits="only pv.source.*/pv.arena.* trace families removed; their two trace options false; no CPU values edited"))
    write_json(derived / "source-derivation.json", rows)
    return compare_backend_history(binary, derived, run)


def capture(args):
    binary, producer = args.binary.resolve(strict=True), args.oracle_root.resolve(strict=True)
    fixtures, output = args.fixtures.resolve(strict=True), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    inputs = output / "inputs"
    shutil.copytree(fixtures, inputs)
    manifest = json.loads((inputs / "fixture-manifest.json").read_bytes())
    commands = []
    evidence = dict(format_version=1, status="capturing", complete=False,
                    tools_sha256=digest(binary), producer_root=str(producer),
                    script_sha256=digest(__file__), fixture_manifest_sha256=digest(inputs / "fixture-manifest.json"),
                    environment_sha256=hashlib.sha256(json.dumps(sorted(os.environ.items()), ensure_ascii=True,
                                                               separators=(",", ":")).encode()).hexdigest(),
                    cases=[], limitations=["Selected assignment/clone observations only; unobserved ancestry stays indeterminate.",
                    "Allocation identity and fresh snapshots do not prove initialized bytes, equal nodes, original element ancestry or semantic lifetimes.",
                    "Callback/failure/alias-retry/discarded-shell paths are not implied by these fixtures."])

    def save():
        write_json(output / "verification.json", evidence)
        write_json(output / "commands.json", commands)

    def run(command, destination):
        command = [str(x) for x in command]
        start = time.monotonic()
        result = subprocess.run(command, cwd=inputs, capture_output=True, timeout=args.timeout)
        destination.write_bytes(result.stdout)
        destination.with_suffix(".stderr").write_bytes(result.stderr)
        commands.append(dict(argv=command, cwd=str(inputs), exit_code=result.returncode,
                             seconds=time.monotonic()-start, stdout=str(destination.relative_to(output)),
                             stdout_sha256=digest(destination), stderr_sha256=digest(destination.with_suffix(".stderr"))))
        save()
        if result.returncode:
            raise RuntimeError("native command failed; diagnostics retained at " + str(destination))
        return json.loads(result.stdout)

    save()
    try:
        for version in args.version or list(VERSIONS):
            for case in manifest["cases"]:
                source = inputs / case["source"]
                assert digest(source) == case["sha256"]
                for backend in ([False, True] if case["backend"] and not args.legacy_only else [False]):
                    name = version+"-"+source.stem+("-backend" if backend else "-legacy")
                    directory = output / name
                    directory.mkdir()
                    row = dict(name=name, version=version, fixture=case["source"], backend=backend,
                               source_sha256=case["sha256"], status="capturing")
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
                            command.append("--source-nodes")
                        command += ["--keep-object", directory / (mode+".o"), source, "--", *manifest["flags"]]
                        value = run(command, directory / (mode+".json"))
                        c = value["compilation"]
                        assert c["version"] == version and c["check_status"] == {"status": "verified"}
                        assert c["source_sha256"] == digest(source) == case["sha256"]
                        assert c["object_sha256"] == digest(directory / (mode+".o"))
                        if enabled:
                            assert c["trace_options"]["source_nodes"] and c["trace_options"]["source_arenas"]
                        (directory / (mode+".trace")).write_bytes(c["trace"].encode("utf-8"))
                        results.append(c)
                    off, on = results
                    for key in ["source_sha256", "producer_sha256", "original_sha256", "flags", "object_sha256", "diagnostics"]:
                        assert off[key] == on[key], (name, key)
                    assert not any(line.startswith(FAMILIES) for line in off["trace"].splitlines())
                    remaining = new_families_removed(on["trace"])
                    assert any(line.startswith("pv.source.") for line in on["trace"].splitlines())
                    assert any(line.startswith("pv.arena.") for line in on["trace"].splitlines())
                    difference = first_difference(full_trace(off["trace"]), full_trace(remaining))
                    write_json(directory / "legacy-comparison.json", dict(identical=difference is None,
                               first_difference=difference, removed_families=list(FAMILIES)))
                    if backend:
                        legacy_off = "".join(line for line in off["trace"].splitlines(keepends=True) if not line.startswith("pv."))
                        legacy_on = "".join(line for line in remaining.splitlines(keepends=True) if not line.startswith("pv."))
                        assert full_trace(legacy_off) == full_trace(legacy_on)
                        assert trace_line_endings(legacy_off) == trace_line_endings(legacy_on)
                        row["raw_backend_parity"] = dict(state="indeterminate" if difference else "satisfied", first_difference=difference)
                        row["backend_diagnostic"] = backend_diagnostic(binary, directory, run)
                    else:
                        assert difference is None, (name, difference)
                        assert trace_line_endings(off["trace"]) == trace_line_endings(remaining)
                    report = run([binary, "source-nodes", "inspect", directory / "enabled.trace", "--json"], directory / "source-nodes.json")
                    raw = reconcile_raw(on["trace"], report, version)
                    write_json(directory / "raw-reconciliation.json", raw)
                    row.update(status="capture_parity_and_raw_reconciliation_passed_native_queries_pending",
                               trace_sha256=digest(directory / "enabled.trace"), report_sha256=digest(directory / "source-nodes.json"),
                               object_sha256=on["object_sha256"], original_sha256=on["original_sha256"],
                               producer_sha256=on["producer_sha256"], raw_totals=raw["totals"])
                    save()
                    print(name+": stock object, legacy records and independent source counters retained", flush=True)
        evidence["status"] = "capture_parity_and_raw_reconciliation_passed_native_queries_pending"
        save()
    except Exception as error:
        evidence.update(status="failed", error=repr(error))
        save()
        raise


def validate_retained(args):
    """Query frozen captures; never recompile, rewrite them or import receipts."""
    binary, output = args.binary.resolve(strict=True), args.output.resolve(strict=True)
    evidence = json.loads((output / "verification.json").read_bytes())
    assert evidence["status"] == "capture_parity_and_raw_reconciliation_passed_native_queries_pending"
    validation = output / "native-validation"
    validation.mkdir(exist_ok=False)
    commands, rows = [], []

    def run(command, destination):
        command = [str(x) for x in command]
        start = time.monotonic()
        result = subprocess.run(command, capture_output=True, timeout=args.timeout)
        destination.write_bytes(result.stdout)
        destination.with_suffix(".stderr").write_bytes(result.stderr)
        commands.append(dict(argv=command, exit_code=result.returncode,
                             seconds=time.monotonic()-start, stdout=str(destination.relative_to(output)),
                             stdout_sha256=digest(destination), stderr_sha256=digest(destination.with_suffix(".stderr"))))
        write_json(validation / "commands.json", commands)
        if result.returncode:
            raise RuntimeError("native query failed; original diagnostics retained at " + str(destination))
        return json.loads(result.stdout)

    def query(source, kind, number, destination):
        answer = run([binary, "source-nodes", kind, source, number], destination)
        assert answer["observations"]["state"] in ["satisfied", "indeterminate"]
        assert (answer["value"] is not None) == (answer["observations"]["state"] == "satisfied")
        return answer

    def object_fields(snapshot, report_nodes):
        """Observed Object pointer fields only, never a guessed element index."""
        result, pending = set(), [snapshot["node"]["snapshot"]]
        while pending:
            node = report_nodes[pending.pop()]
            if node["tag"] == 56 and node["complete"]:
                result.add(node["data"][0])
            pending.extend(node["children"].values())
        return result

    try:
        for case in evidence["cases"]:
            # The backend duplicate already proves coexistence; source-query
            # mechanism validation uses its smaller legacy capture once/version.
            if case["backend"]:
                continue
            directory = output / case["name"]
            destination = validation / case["name"]
            destination.mkdir()
            source = directory / "enabled.trace"
            assert digest(source) == case["trace_sha256"]
            report = run([binary, "source-nodes", "inspect", source, "--json"], destination / "inspect.json")
            assert report["observations"]["state"] == "satisfied", "do not label unsupported/truncated capture as successful coverage"
            raw = reconcile_raw(source.read_text(encoding="utf-8"), report, case["version"])
            report_nodes = {node["snapshot"]: node for node in report["nodes"]}
            write_answers, clone_answers, snapshot_answers = {}, {}, {}
            for item in report["emissions"][:32]:
                number = item["emission"]
                write_answers[number] = query(source, "write", number, destination / f"write-{number}.json")
            good_writes = {number: answer["value"] for number, answer in write_answers.items() if answer["value"] is not None}
            if case["fixture"] == "ordinary_writes.cpp":
                assert len(good_writes) >= 3, "ordinary stores lack native SourceWrite evidence"
            if case["fixture"] == "inline_arrays.cpp":
                assert len(good_writes) >= 2 and raw["clones"], "inline mechanism absent"
                for item in raw["clones"][:64]:
                    number = item["invocation"]
                    clone_answers[number] = query(source, "clone", number, destination / f"clone-{number}.json")
                good_clones = [answer["value"] for answer in clone_answers.values() if answer["value"] is not None]
                assert good_clones, "actual clone edges remain indeterminate"
                # The final function in this fixture owns the two ordinary
                # left/right setup assignments. Match only observed Object*
                # fields through actual clone outputs; no +18 offset inference.
                main_owner = max(item["owner"] for item in report["emissions"])
                setup = [good_writes[item["emission"]] for item in report["emissions"]
                         if item["owner"] == main_owner and item["emission"] in good_writes]
                anchors = set().union(*(object_fields(write["lhs"], report_nodes) for write in setup))
                matches = []
                for edge in good_clones:
                    if edge["output"] is not None and edge["input"] is not None:
                        observed = object_fields(edge["output"], report_nodes) & anchors
                        if observed:
                            matches.append(dict(invocation=edge["invocation"], observed_object_fields=sorted(observed),
                                                input_snapshot=edge["input"]["node"]["snapshot"],
                                                output_snapshot=edge["output"]["node"]["snapshot"]))
                observed_actuals = set().union(*(set(match["observed_object_fields"]) for match in matches))
                assert len(anchors) >= 2 and len(observed_actuals) >= 2, "two-array actual Object fields were not observed through clone outputs"
                write_json(destination / "two-array-observed-fields.json", dict(
                    ordinary_write_object_fields=sorted(anchors), clone_matches=matches,
                    scope="Actual selected Object* fields and clone callback edges, not exact array-element or semantic source equivalence"))

            # Fresh IDs can observe the same live allocation. Keep this as native
            # JSON facts from one pinned trace, never deserialize receipt tokens.
            repeated = None
            for item in raw["repeated_node_pointers"][:16]:
                answers = []
                for number in item["snapshots"][:2]:
                    if number not in snapshot_answers:
                        snapshot_answers[number] = query(source, "snapshot", number, destination / f"snapshot-{number}.json")
                    answers.append(snapshot_answers[number])
                if all(answer["value"] is not None for answer in answers):
                    values = [answer["value"] for answer in answers]
                    allocations = [value["allocation"]["resolved"] for value in values]
                    keys = [tuple(a[k] for k in ["allocation", "pool", "epoch", "offset", "extent"]) for a in allocations]
                    if keys[0] == keys[1]:
                        assert values[0]["node"]["snapshot"] != values[1]["node"]["snapshot"]
                        repeated = dict(snapshots=item["snapshots"][:2], allocation_fields=allocations,
                                        scope="Fresh immutable observations with matching native historical allocation fields in the same frozen trace; no serialized receipt reuse")
                        break
            if raw["clones"]:
                assert repeated, "no native repeated-read allocation evidence"

            unsupported = None
            if case["fixture"] == "unsupported_call.cpp":
                for item in raw["opaque"]:
                    if item["tag"] in [54, 55] and item["reason"] == "unsupported":
                        number = item["snapshot"]
                        answer = query(source, "snapshot", number, destination / f"unsupported-snapshot-{number}.json")
                        assert answer["value"] is None
                        unsupported = dict(snapshot=number, answer=answer)
                        break
                assert unsupported, "unsupported call control did not exercise an opaque call node"
                opaque_roots = {e["emission"] for e in report["emissions"] if e["snapshot"] == unsupported["snapshot"]}
                assert opaque_roots and all(write_answers[number]["value"] is None for number in opaque_roots)

            sample = next(iter(good_writes), None)
            assert sample is not None, "fixture has no supported ordinary assignment for negative controls"
            trace = source.read_text(encoding="utf-8")
            lines = trace.splitlines(keepends=True)
            first_node = next(i for i, line in enumerate(lines) if line.startswith("pv.source.node "))
            counter_lines = []
            for line in lines:
                if line.startswith("pv.source.checkpoint "):
                    _, f = fields(line)
                    line = line.replace("snapshots="+f["snapshots"], "snapshots="+str(int(f["snapshots"])+1), 1)
                counter_lines.append(line)
            mutations = {
                "truncated-first-tree": "".join(lines[:first_node+1]),
                "dropped-first-node": "".join(lines[:first_node]+lines[first_node+1:]),
                "checkpoint-count": "".join(counter_lines),
                "unknown-node-field": "".join(lines[:first_node]+[lines[first_node].rstrip("\r\n")+" future_field=1\n"]+lines[first_node+1:]),
            }
            negatives = []
            for label, text in mutations.items():
                path = destination / (label+".trace")
                path.write_text(text, encoding="utf-8")
                inspected = run([binary, "source-nodes", "inspect", path, "--json"], destination / (label+"-inspect.json"))
                assert inspected["observations"]["state"] == "indeterminate"
                answer = query(path, "write", sample, destination / (label+"-write.json"))
                assert answer["value"] is None
                negatives.append(dict(name=label, trace_sha256=digest(path), observations=answer["observations"]))
            row = dict(name=case["name"], ordinary_source_writes=sorted(good_writes),
                       clone_queries=len(clone_answers), known_clone_edges=[n for n, a in clone_answers.items() if a["value"] is not None],
                       repeated_observation=repeated, unsupported_control=unsupported, negatives=negatives)
            rows.append(row)
            write_json(destination / "summary.json", row)
        write_json(validation / "summary.json", dict(analyzer_sha256=digest(binary), cases=rows,
                   scope="Guarded source assignment roots, actual clone edges and immutable observations; unobserved ancestry remains indeterminate"))
        evidence.update(status="passed", complete=True, native_validation="native-validation/summary.json",
                        query_analyzer_sha256=digest(binary), validator_sha256=digest(__file__))
        write_json(output / "verification.json", evidence)
    except Exception as error:
        evidence.update(status="failed", complete=False, native_validation_error=repr(error))
        write_json(output / "verification.json", evidence)
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new immutable evidence directory")
    parser.add_argument("--fixtures", type=Path, default=ROOT / "tests/fixtures/source-nodes")
    parser.add_argument("--version", action="append", choices=list(VERSIONS))
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--legacy-only", action="store_true")
    parser.add_argument("--validate-retained", action="store_true",
                        help="query and mutate derived copies of earlier captures; never recompile")
    args = parser.parse_args()
    (validate_retained if args.validate_retained else capture)(args)
