"""Compare native locate/bindfrom/funcmatch with the pinned Python tools."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command,directory):
    result=subprocess.run(command,cwd=directory,capture_output=True,text=True,timeout=600)
    if result.returncode or result.stderr:
        raise RuntimeError(f"{command[:3]}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--version",default="GC_1_3")
    parser.add_argument("--source",action="append")
    parser.add_argument("--from",dest="from_version",default="1.3")
    parser.add_argument("--to",dest="to_version",default="1.3.2")
    parser.add_argument("--keep",type=Path)
    args=parser.parse_args()
    root,binary=args.oracle_root.resolve(),args.binary.resolve()
    sources=args.source or ["src/backend/Coloring.c","src/frontend/CInline.c"]
    outputs=[]
    with tempfile.TemporaryDirectory(prefix="investigation-parity-") as temporary:
        for backend in ("python","rust"):
            project=Path(temporary) / backend
            directory=project / "config" / args.version
            directory.mkdir(parents=True)
            original=root / "config" / args.version
            config=json.loads((original / "config.json").read_text())
            config["original"]=str((root / config["original"]).resolve())
            (directory / "config.json").write_text(json.dumps(config))
            for name in ("bindings.json","splits.txt"):
                shutil.copyfile(original / name,directory / name)
            for source in sources:
                name=f"build/{args.version}/compiled/{source}.obj"
                path=project / name
                path.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(root / name,path)
            output={}
            for command in ("locate","bindfrom"):
                invocation=([sys.executable,str(root / "tools" / (command+".py"))] if backend=="python" else
                            [str(binary),command,"--root","."])
                output[command]=run([*invocation,args.version,*sources] if command=="locate" else [*invocation,args.version],project)
            output["bindings"]=json.loads((directory / "bindings.json").read_text())
            outputs.append(output)
    if outputs[0]!=outputs[1]:
        if args.keep:
            args.keep.parent.mkdir(parents=True,exist_ok=True)
            args.keep.with_suffix(".mismatch.json").write_text(json.dumps(outputs,indent=2)+"\n")
        raise RuntimeError("locate/bindfrom results differ")
    code="import sys,json;sys.path.insert(0,sys.argv[1]);import funcmatch;r,a=funcmatch.match(sys.argv[2],sys.argv[3]);print(json.dumps(dict(result=r,ambiguous=a,total=len(funcmatch.config_functions(sys.argv[2])))))"
    expected=json.loads(run([sys.executable,"-c",code,str(root / "tools"),args.from_version,args.to_version],root))
    actual=json.loads(run([str(binary),"funcmatch",args.from_version,args.to_version,"--root",str(root),"--json"],root))
    if actual.pop("format_version")!=1 or actual!=expected:
        raise RuntimeError("funcmatch mapping or ambiguity lists differ")
    text=run([sys.executable,str(root / "tools/funcmatch.py"),args.from_version,args.to_version],root)
    native=run([str(binary),"funcmatch",args.from_version,args.to_version,"--root",str(root)],root)
    if text!=native:
        raise RuntimeError("funcmatch summaries differ")
    result=dict(version=args.version,sources=sources,identical=True,
                locate_lines=len(outputs[1]["locate"].splitlines()),binding_summary=outputs[1]["bindfrom"].splitlines()[0],
                from_version=args.from_version,to_version=args.to_version,funcmatch_summary=native.strip(),
                funcmatch_sha256=hashlib.sha256(json.dumps(actual,sort_keys=True).encode()).hexdigest())
    if args.keep:
        args.keep.parent.mkdir(parents=True,exist_ok=True)
        args.keep.write_text(json.dumps(result,indent=2)+"\n")
    print(f"locate/bindfrom agree; {native.strip()}")


if __name__ == "__main__":
    main()
