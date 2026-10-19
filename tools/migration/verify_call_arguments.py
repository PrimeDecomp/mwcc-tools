"""Retain native V6 call-argument captures and check the small mechanism fixture.

Development audit only. Uses an already built producer without configuring or
rebuilding it; keeps stock checking enabled and writes only to a new output root.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time

from verify_producer import ROOT, full_trace, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", action="append",
                        choices=["GC_1_3", "GC_1_3_2", "GC_2_7"])
    args = parser.parse_args()
    binary, oracle, output = (p.resolve() for p in
                              [args.binary, args.oracle_root, args.output])
    output.mkdir(parents=True, exist_ok=False)
    source = ROOT / "tests/fixtures/backend/call_arguments.cpp"
    evidence = dict(format_version=1, complete=False,
                    binary_sha256=sha(binary), oracle_root=str(oracle),
                    source_sha256=sha(source), cases=[])

    def save():
        (output / "verification.json").write_text(
            json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

    def run(command, path):
        command = [str(v) for v in command]
        path.with_suffix(".command.json").write_text(
            json.dumps(dict(argv=command, cwd=str(ROOT)), indent=2) + "\n",
            encoding="utf-8")
        started = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=180)
        path.write_bytes(result.stdout)
        path.with_suffix(".stderr").write_bytes(result.stderr)
        assert result.returncode == 0, (command, result.returncode,
                                        result.stderr.decode(errors="replace"))
        return json.loads(result.stdout), dict(
            seconds=time.monotonic()-started, stdout_sha256=sha(path),
            stderr_sha256=sha(path.with_suffix(".stderr")))

    save()
    for version in args.version or ["GC_1_3", "GC_1_3_2", "GC_2_7"]:
        dest = output / version
        dest.mkdir()
        command = [binary, "oracle", "compile-trace", "--oracle-root", oracle,
                   "--version", version, "--stages", "--locals", "--cse",
                   "--keep-object", dest / "disabled.o", source, "--",
                   "-nodefaults", "-proc", "gekko", "-fp", "hardware", "-O4,p"]
        disabled, off_meta = run(command, dest / "disabled.json")
        enabled_command = command.copy()
        enabled_command[enabled_command.index("--keep-object")+1] = dest / "enabled.o"
        enabled_command.insert(enabled_command.index("--stages"), "--backend")
        enabled, on_meta = run(enabled_command, dest / "enabled.json")
        off, on = disabled["compilation"], enabled["compilation"]
        assert off["check_status"] == on["check_status"] == {"status": "verified"}
        for key in ["source_sha256", "producer_sha256", "original_sha256", "flags",
                    "object_sha256", "diagnostics"]:
            assert off[key] == on[key], (version, key)
        assert on["source_sha256"] == sha(source)
        assert on["object_sha256"] == sha(dest / "enabled.o") == sha(dest / "disabled.o")
        assert not any(line.startswith("pv.") for line in off["trace"].splitlines())
        assert full_trace(off["trace"]) == full_trace("\n".join(
            line for line in on["trace"].splitlines() if not line.startswith("pv.")))
        report, inspect_meta = run(
            [binary, "provenance", "inspect", dest / "enabled.json", "--json"],
            dest / "provenance.json")
        history = report["call_arguments"]
        assert len(history["calls"]) == 5
        functions = {f["link"]: f["id"] for f in report["history"]["functions"]}
        selected = []
        for link, expected in [("address_argument__Fv", 2),
                               ("reference_argument__Fv", 0)]:
            call, = [c for c in history["calls"] if c["function"] == functions[link]]
            assert call["observations"]["state"] == "satisfied"
            assert call["callee"]["name_known"] and call["callee"]["name_complete"]
            assert call["callee"]["name_text"] == "SetTevOp__FUiRC4Pass"
            assert call["ordinal_kind"] == "lowered_argument_context_order"
            assert len(call["arguments"]) == 2 and call["arguments"][1]["gpr"] == 4
            episodes = [e for e in call["episodes"] if e["ordinal"] == 1]
            births = [b for e in episodes for b in e["births"]]
            assert len(births) == expected
            if expected:
                function, = [f for f in report["history"]["functions"]
                             if f["id"] == functions[link]]
                creation = [next(b for b in function["births"] if b["id"] == role["value"])
                            for role in births]
                materialization = [b["materialization"] for b in creation]
                assert [m["part"] for m in materialization] == ["high", "low"]
                assert all(m["symbol_known"] and m["symbol_complete"]
                           and m["symbol_text"] == "kPass" for m in materialization)
                assert materialization[0]["operand"] == materialization[1]["operand"]
                assert materialization[0]["object"] == materialization[1]["object"]
            selected.append(dict(link=link, ordinal=1, planned_gpr=4, births=births,
                                 stages=[e["stage"] for e in episodes]))
        assert all(c["callee"]["name_known"] and c["callee"]["name_complete"]
                   for c in history["calls"])
        gaps = [line for line in on["trace"].splitlines()
                if line.startswith("pv.argument.gap ")]
        assert len(gaps) == 1 and "reason=pcode_interval_incomplete" in gaps[0]
        nested = functions["nested_argument__FUi"]
        assert f"function={nested} " in gaps[0]
        stack, = [c for c in history["calls"]
                  if c["function"] == functions["stack_argument__FUi"]]
        assert len(stack["arguments"]) == 11
        assert stack["observations"]["state"] == "satisfied"
        evidence["cases"].append(dict(
            version=version, object_sha256=on["object_sha256"],
            producer_sha256=on["producer_sha256"], original_sha256=on["original_sha256"],
            disabled=off_meta, enabled=on_meta, inspection=inspect_meta,
            calls=5, known_callees=5, selected=selected, raw_gaps=gaps))
        save()
        print(f"{version}: stock objects agree; address=2 births, reference=0; "
              "nested block transition remains a gap", flush=True)
    evidence["complete"] = True
    save()


if __name__ == "__main__":
    main()
