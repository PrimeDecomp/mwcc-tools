"""Compare native remap and derive-splits with pinned Python without editing version metadata."""
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
        raise RuntimeError(f"{command[:4]}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--keep",type=Path)
    args=parser.parse_args()
    root,binary=args.oracle_root.resolve(),args.binary.resolve()
    results=[]
    with tempfile.TemporaryDirectory(prefix="metadata-parity-") as temporary:
        for source,target,symbols in (("1.3","1.3.2",False),("1.3","1.3.2",True),("1.3.2","2.7",True)):
            outputs=[]
            for backend in ("python","rust"):
                project=Path(temporary) / (source+"-"+target+"-"+str(symbols)) / backend
                folder="GC_"+source.replace(".","_")
                destination=project / "config" / folder
                destination.mkdir(parents=True)
                for name in ("functions.json","bindings.json","symbols.txt"):
                    shutil.copyfile(root / "config" / folder / name,destination / name)
                for version in (source,target):
                    path=project / "build/compilers/GC" / version / "mwcceppc.exe"
                    path.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copyfile(root / "build/compilers/GC" / version / "mwcceppc.exe",path)
                command=([sys.executable,str(root / "tools/remap.py")] if backend=="python" else [str(binary),"remap","--root","."])
                text=run([*command,source,target,*(["--symbols"] if symbols else [])],project)
                directory=project / "config" / ("GC_"+target.replace(".","_"))
                outputs.append((text,{n:(directory / n).read_bytes() for n in ("functions.json","bindings.json")}))
            if outputs[0]!=outputs[1]:
                if args.keep:
                    args.keep.parent.mkdir(parents=True,exist_ok=True)
                    for backend,(text,files) in zip(("python","rust"),outputs):
                        mismatch=args.keep.parent / f"remap-{source}-{target}-{symbols}-{backend}"
                        mismatch.mkdir(exist_ok=True)
                        (mismatch / "stdout.txt").write_text(text)
                        for name,data in files.items():
                            (mismatch / name).write_bytes(data)
                raise RuntimeError(f"remap {source} -> {target} symbols={symbols}: results differ")
            result=dict(command="remap",source=source,target=target,symbols=symbols,identical=True,
                        summary=outputs[1][0].splitlines()[:2],
                        hashes={n:hashlib.sha256(b).hexdigest() for n,b in outputs[1][1].items()})
            results.append(result)
            print(f"remap {source} -> {target} symbols={symbols}: metadata bytes and reports agree",flush=True)
        for version in ("GC_1_3_2","GC_2_7"):
            outputs=[]
            for backend in ("python","rust"):
                output=Path(temporary) / (version+"-derive-"+backend)
                command=([sys.executable,str(root / "tools/derive_splits.py")] if backend=="python" else [str(binary),"derive-splits","--root",str(root)])
                text=run([*command,version,"--out",str(output)],root)
                outputs.append((text,{n:(output / n).read_bytes() for n in ("splits.txt","symbols.txt")}))
            if outputs[0]!=outputs[1]:
                if args.keep:
                    args.keep.parent.mkdir(parents=True,exist_ok=True)
                    for backend,(text,files) in zip(("python","rust"),outputs):
                        mismatch=args.keep.parent / (version+"-derive-"+backend)
                        mismatch.mkdir(exist_ok=True)
                        for name,data in files.items():
                            (mismatch / name).write_bytes(data)
                raise RuntimeError(f"derive {version}: results differ")
            results.append(dict(command="derive-splits",version=version,identical=True,
                                lines={n:len(b.splitlines()) for n,b in outputs[1][1].items()},
                                hashes={n:hashlib.sha256(b).hexdigest() for n,b in outputs[1][1].items()}))
            print(f"derive-splits {version}: complete metadata bytes agree",flush=True)
        if args.keep:
            args.keep.parent.mkdir(parents=True,exist_ok=True)
            args.keep.write_text(json.dumps(results,indent=2)+"\n")


if __name__ == "__main__":
    main()
