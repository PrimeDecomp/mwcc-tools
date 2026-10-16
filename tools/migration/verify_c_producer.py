"""Verify that the opt-in Rust guards leave the default C producer unchanged."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def contents(module, path):
    symbols, sections = module.read_object(path)
    # Debug records encode source paths and line numbers. All executable/data
    # bytes, BSS sizes and symbolic relocations remain part of this comparison.
    selected = [dict(name=section["name"], data=section["data"].hex(), size=len(section["data"]), flags=section["flags"],
                 relocs=[(offset, symbols[index]["name"], kind) for offset, index, kind in section["relocs"]])
            for section in sections if not section["name"].startswith(".debug")]
    definitions = sorted((symbol["name"], sections[symbol["section"] - 1]["name"], symbol["value"])
                         for symbol in symbols.values() if symbol["section"] > 0 and symbol["storage"] == 2)
    return dict(sections=selected, definitions=definitions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--wibo", type=Path)
    args = parser.parse_args()
    reference = args.oracle_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(reference / "tools"))
    compare = importlib.import_module("compare")
    evidence = dict(reference_root=str(reference), versions=[])
    for version, version_index in [("GC_1_3", 5), ("GC_1_3_2", 6), ("GC_2_7", 7)]:
        config = json.loads((reference / f"config/{version}/config.json").read_text())
        source = json.loads((reference / "config/sources.json").read_text())["src/backend/Coloring.c"]
        compiler = config.get("compilers", {}).get(source["compiler"], source["compiler"])
        executable = reference / f"build/compilers/{compiler}/mwcc.exe"
        launch = [str(executable)]
        if os.name != "nt":
            loader = args.wibo or reference / "build/tools/wibo"
            launch.insert(0, str(loader if loader.is_file() else shutil.which("wibo")))
        runs, objects = [], []
        for label, root in [("reference", reference), ("current", ROOT)]:
            prefix = args.output.resolve() / f"{version}-{label}"
            output = prefix.with_suffix(".obj")
            command = [*launch, *source["flags"], f"-DVERSION={version_index}", "-DMWCC_ORACLE_TRACE=1",
                       "-I" + str(root / "include"), "-c", str(root / "src/oracle/Hooks.c"), "-o", str(output)]
            result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, MWCIncludes=str(root / "include/libc")),
                                    capture_output=True, timeout=60)
            for stream in ["stdout", "stderr"]:
                prefix.with_suffix("." + stream).write_bytes(getattr(result, stream))
            assert result.returncode == 0, (version, label, result.stdout, result.stderr)
            objects.append(contents(compare, output))
            runs.append(dict(label=label, source_sha256=sha(root / "src/oracle/Hooks.c"), object_sha256=sha(output),
                             stdout_sha256=sha(prefix.with_suffix(".stdout")), stderr_sha256=sha(prefix.with_suffix(".stderr"))))
        assert objects[0] == objects[1], f"{version}: default C producer executable sections/relocations changed"
        evidence["versions"].append(dict(version=version, compiler_sha256=sha(executable), flags=source["flags"], runs=runs,
            non_debug_sha256=hashlib.sha256(json.dumps(objects[0], sort_keys=True).encode()).hexdigest()))
        print(f"{version}: default C producer executable/data sections, BSS and relocations agree", flush=True)
    evidence["complete"] = True
    (args.output / "c-producer-verification.json").write_text(json.dumps(evidence, indent=2) + "\n")


if __name__ == "__main__":
    main()
