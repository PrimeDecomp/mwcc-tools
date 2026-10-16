"""Compare native disassembly, instruction diffs and ranking reports with pinned Python."""
import argparse
import hashlib
import importlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import time


def run(command,cwd):
    started=time.perf_counter()
    result=subprocess.run(command,cwd=cwd,capture_output=True,text=True,timeout=600)
    if result.returncode or result.stderr:
        raise RuntimeError(f"{command[:5]}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout,time.perf_counter()-started


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--version",action="append")
    parser.add_argument("--source",action="append")
    parser.add_argument("--keep",type=Path)
    args=parser.parse_args()
    root,binary=args.oracle_root.resolve(),args.binary.resolve()
    sys.path.insert(0,str(root / "tools"))
    locate=importlib.import_module("locate")
    results=[]
    for version in args.version or ["GC_1_0","GC_1_3","GC_1_3_2","GC_2_7"]:
        functions=json.loads((root / "config" / version / "functions.json").read_text())
        chosen=[functions[0],functions[len(functions)//2],functions[-1]]
        commands=[("disasm",[version,f["address"]]) for f in chosen]
        sources=args.source or ["src/backend/Coloring.c","src/frontend/CInline.c"]
        available=[]
        for source in sources:
            path=root / f"build/{version}/compiled/{source}.obj"
            if not path.is_file():
                continue
            available.append(source)
            names={n for n,_,_ in locate.functions_of(path)}
            matches=[f for f in functions if f.get("source")==source and ("_"+f["name"] in names or f["name"] in names)]
            for f in matches[:2]:
                name=f["name"] if f["name"] in names else "_"+f["name"]
                commands.append(("fdiff",[version,source,name,f["address"]]))
        if available:
            commands.append(("nearest",[version,*available]))
        for command,arguments in commands:
            if command in ("disasm","nearest") and not (root / "config" / version / "symbols.txt").is_file():
                for invocation in ([sys.executable,str(root / "tools" / (command+".py")),*arguments],
                                   [str(binary),command,*arguments,"--root",str(root)]):
                    failure=subprocess.run(invocation,cwd=root,capture_output=True,text=True,timeout=60)
                    if not failure.returncode or failure.stdout or "symbols.txt" not in failure.stderr:
                        raise RuntimeError(f"{command} {version}: missing metadata was not rejected clearly")
                results.append(dict(command=command,arguments=arguments,missing_symbols_both_rejected=True))
                print(f"{command} {version}: both reject missing symbols.txt",flush=True)
                continue
            expected,python_seconds=run([sys.executable,str(root / "tools" / (command+".py")),*arguments],root)
            actual,rust_seconds=run([str(binary),command,*arguments,"--root",str(root)],root)
            if expected!=actual:
                if args.keep:
                    args.keep.parent.mkdir(parents=True,exist_ok=True)
                    args.keep.with_suffix(f".{command}-{version}-python.txt").write_text(expected)
                    args.keep.with_suffix(f".{command}-{version}-rust.txt").write_text(actual)
                raise RuntimeError(f"{command} {arguments}: report differs")
            results.append(dict(command=command,arguments=arguments,identical=True,lines=len(actual.splitlines()),sha256=hashlib.sha256(actual.encode()).hexdigest(),
                                python_seconds=python_seconds,rust_seconds=rust_seconds))
            if args.keep:
                args.keep.parent.mkdir(parents=True,exist_ok=True)
                args.keep.write_text(json.dumps(dict(capstone_package=importlib.metadata.version("capstone"),results=results),indent=2)+"\n")
            print(f"{command} {version}: {len(actual.splitlines())} lines agree; Python {python_seconds:.3f}s, Rust {rust_seconds:.3f}s",flush=True)


if __name__=="__main__":
    main()
