"""Retain native static arena audits and retail listings without changing inputs.

This does not build a producer, compile game code, or establish runtime coverage.
Requires only Python's standard library and an already-built native mwcc-tools.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def capture(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    executable = args.tools.resolve(strict=True)
    oracle_root = args.oracle_root.resolve(strict=True)
    commands = []

    def run(arguments, destination):
        command = [str(executable)] + arguments + ["--root", str(oracle_root)]
        result = subprocess.run(command, capture_output=True, timeout=60)
        commands.append({"command": command, "exit_code": result.returncode,
                         "stdout": destination.name, "stderr": result.stderr.decode("utf-8", errors="replace")})
        destination.write_bytes(result.stdout)
        (output / "commands.json").write_bytes(encode(commands))
        if result.returncode:
            raise RuntimeError("native command failed; see commands.json")
        return result.stdout

    reports = []
    for version in ("GC_1_3", "GC_1_3_2", "GC_2_7"):
        report_bytes = run(["provenance", "audit-source-arenas", "--version", version], output / (version + ".json"))
        report = json.loads(report_bytes)
        assert report["version"] == version
        assert report["format_version"] == 1
        assert report["allocation_coverage_complete"] is False
        assert report["reset_coverage_complete"] is False
        streams = [report["local_lock"], report["failure_callback"]] + report["arenas"]
        assert all(not stream["unresolved"] for stream in streams)
        assert all(ref["function_decoded_completely"] for stream in streams for ref in stream["references"])
        global_pool = next(pool for pool in report["arenas"] if pool["name"] == "global")
        writers = {ref["function"] for ref in global_pool["references"]
                   if ref["write"] and global_pool["base"] + 12 in ref["targets"]}
        assert {"_InitNameHash", "_GetHashNameNode", "_GetHashNameNodeExport"} <= writers
        assert any(ref["function"] == "_freelheap" and ref["read"] and ref["width"] == 2
                   for ref in report["local_lock"]["references"])
        assert any(ref["function"] == "_select_or_allocate_pool_node" and ref["read"]
                   for ref in report["failure_callback"]["references"])
        functions = {}
        for stream in streams:
            for function in stream["functions"]:
                previous = functions.setdefault(function["name"], function)
                assert previous == function
        listing_hashes = {}
        for name, function in sorted(functions.items()):
            destination = output / (version + "-" + name + ".txt")
            data = run(["disasm", version, format(function["address"], "x"),
                        format(function["size"], "x")], destination)
            listing_hashes[destination.name] = sha256(data)
        reports.append({"version": version, "report_sha256": sha256(report_bytes),
                        "reference_counts": {pool["name"]: len(pool["references"]) for pool in report["arenas"]},
                        "local_lock_references": len(report["local_lock"]["references"]),
                        "failure_callback_references": len(report["failure_callback"]["references"]),
                        "guarded_functions": len(functions), "listing_sha256": listing_hashes})
        if args.compare:
            previous = args.compare / (version + ".json")
            assert json.loads(previous.read_bytes()) == report, "native-host audit disagreement: " + version
            for name, expected_hash in listing_hashes.items():
                assert sha256((args.compare / name).read_bytes()) == expected_hash, "native-host listing disagreement: " + name
    manifest = {"format_version": 1, "verification": "passed",
                "scope": "static direct relocated references and retail listings; no runtime lifetime claim",
                "tools_sha256": sha256(executable.read_bytes()), "script_sha256": sha256(pathlib.Path(__file__).read_bytes()),
                "cross_host_comparison": str(args.compare.resolve()) if args.compare else None,
                "versions": reports}
    (output / "verification.json").write_bytes(encode(manifest))
    print(json.dumps({"verification": "passed", "output": str(output), "versions": len(reports)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=pathlib.Path, required=True)
    parser.add_argument("--oracle-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True, help="new directory; existing outputs are never overwritten")
    parser.add_argument("--compare", type=pathlib.Path, help="compare exact reports and listing bytes from another native host")
    capture(parser.parse_args())
