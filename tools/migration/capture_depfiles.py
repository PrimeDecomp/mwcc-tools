"""Capture native-platform MWCC depfile conversion, with source deletion and exact bytes."""
import argparse
import hashlib
import json
import locale
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT / "tests/fixtures/build-config"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    cases=[]
    inputs=[("target.obj: relative/header.h \\\n include/a.h\n","result.obj"),
            ("target.obj: {ROOT}/headers/one.h {ROOT}/headers/../two.h\n","result.obj"),
            ("target.obj: {BACKROOT}\\headers\\one.h {BACKROOT}\\..\\outside.h\r\n","result.obj"),
            ("target.obj: ./include/a.h ../shared.h sub/../other.h\n","result.obj"),
            ("target.obj: caf\xe9.h \\\r\n next.h\rthird.h\r\n","out/spaced output.obj"),
            ("target.obj:\n","result.obj"),("","result.obj"),
            ("target.obj: include/space\\ name.h\n","result.obj")]
    with tempfile.TemporaryDirectory(prefix="depfile-reference-") as temporary:
        directory=Path(temporary)
        (directory / "out").mkdir()
        for text,output in inputs:
            expanded=text.replace("{ROOT}",directory.as_posix()).replace("{BACKROOT}",str(directory).replace("/","\\"))
            (directory / "input.dep").write_bytes(expanded.encode("latin-1"))
            result=subprocess.run([sys.executable,str(args.oracle_root / "tools/transform_dep.py"),"input.dep",output],cwd=directory,capture_output=True,text=True,check=True)
            cases.append(dict(input=text,output=output,bytes=list(Path(str(directory / output)+".d").read_bytes()),
                              text=Path(str(directory / output)+".d").read_text(),
                              removed=not (directory / "input.dep").exists(),stdout=result.stdout,stderr=result.stderr))
    platform="Windows" if sys.platform=="win32" else "Posix"
    destination=OUT / ("depfiles-"+platform+".json")
    destination.write_text(json.dumps(dict(platform=platform,output_encoding="utf-8" if sys.flags.utf8_mode else locale.getencoding(),reference_sha256=hashlib.sha256((args.oracle_root / "tools/transform_dep.py").read_bytes()).hexdigest(),cases=cases),indent=2)+"\n")
    print(f"Captured {len(cases)} {platform} depfile cases")


if __name__=="__main__":
    main()
