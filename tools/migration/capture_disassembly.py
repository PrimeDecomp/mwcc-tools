"""Freeze Capstone listings and the three legacy disassembly commands on synthetic inputs."""
import argparse
import hashlib
import importlib
import importlib.metadata
import json
import random
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import capstone

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT / "tests/fixtures/disassembly"


def stage(directory,metadata):
    config=directory / "config/GC_fixtureA"
    config.mkdir(parents=True)
    (config / "config.json").write_text(json.dumps(dict(original="original.exe")))
    (config / "symbols.txt").write_text(metadata["symbols"])
    shutil.copyfile(OUT / "original.pe",directory / "original.exe")
    for source,file in metadata["objects"].items():
        path=directory / f"build/GC_fixtureA/compiled/{source}.obj"
        path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(OUT / file,path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    args=parser.parse_args()
    sys.path.insert(0,str(args.oracle_root / "tools"))
    fdiff=importlib.import_module("fdiff")
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    OUT.mkdir(parents=True,exist_ok=True)
    scanner=json.loads((ROOT / "tests/fixtures/binary/branches.json").read_text())
    inputs=[bytes(c["code"]) for c in scanner]
    rng=random.Random(918)
    inputs.extend(rng.randbytes(rng.randrange(0,96)) for _ in range(256))
    cases=[]
    for index,code in enumerate(inputs):
        address=0 if index%2==0 else 0x401000
        wild={3,4,5} if address==0 else set()
        instructions=[dict(address=i.address,size=i.size,mnemonic=i.mnemonic,
                           text=f"{i.mnemonic} {i.op_str}".strip(),
                           field=any(j in wild for j in range(i.address,i.address+i.size))) for i in decoder.disasm(code,address)]
        cases.append(dict(code=list(code),address=address,wild=sorted(wild),instructions=instructions))
    (OUT / "instructions.json").write_text(json.dumps(cases,indent=2)+"\n")
    shape_inputs=[(t,f) for t in ("mov eax, dword ptr [esp + 0x10]","call 0x401020","push -4","ret","pshufd xmm1, xmm2, 0xff") for f in (False,True)]
    shapes=[dict(text=t,field=f,shape=fdiff.shape(t,f)) for t,f in shape_inputs]
    (OUT / "shapes.json").write_text(json.dumps(shapes,indent=2)+"\n")
    metadata=json.loads((ROOT / "tests/fixtures/metadata/project.json").read_text())
    objects=metadata["objects"]
    for file in objects.values():
        shutil.copyfile(ROOT / "tests/fixtures/metadata" / file,OUT / file)
    shutil.copyfile(ROOT / "tests/fixtures/metadata/from.pe",OUT / "original.pe")
    symbols="_entry = .text:0x00401000; // type:function size:0x10 scope:global\n_call = .text:0x00401020; // type:function size:0x10\n"
    symbols+="_c = .text:0x00401040; // type:function size:0x10\n_fn_00401050 = .text:0x00401050; // type:function size:0x4\n_d = .text:0x00401060; // type:function size:0x10\n"
    symbols+="_e = .text:0x00401080; // type:function size:0x10\n_f = .text:0x004010a0; // type:function size:0x10\n_z = .text:0x004010c0; // type:function size:0x10\n"
    symbols+="_data = .data:0x00402000; // type:object size:0x8\n_data_alias = .data:0x00402000; // type:object size:0x8\n_zero = .data:0x00402010; // type:object\n"
    project=dict(symbols=symbols,objects=objects,commands=[])
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        stage(directory,project)
        requests=[("disasm",["fixtureA","0x401000"]),("disasm",["fixtureA","0x401000","8"]),
                  ("disasm",["fixtureA","0x401050","1"]),("disasm",["fixtureA","0x401050","0"]),
                  ("disasm",["fixtureA","0x401051","1"]),
                  ("disasm",["fixtureA","0x401051"]),
                  ("fdiff",["fixtureA","src/one.c","_a","0x401000"]),
                  ("fdiff",["fixtureA","src/one.c","_b","0x401020"]),
                  ("fdiff",["fixtureA","src/one.c","_g","0x401020"]),
                  ("fdiff",["fixtureA","src/one.c","_g","0x4010e0"]),
                  ("nearest",["fixtureA",*objects])]
        for command,arguments in requests:
            output=subprocess.check_output([sys.executable,str(args.oracle_root / "tools" / (command+".py")),*arguments],cwd=directory,text=True)
            project["commands"].append(dict(command=command,arguments=arguments,output=output))
    (OUT / "project.json").write_text(json.dumps(project,indent=2)+"\n")
    manifest=dict(reference_commit=subprocess.check_output(["git","-C",str(args.oracle_root),"rev-parse","HEAD"],text=True).strip(),
                  capstone_package=importlib.metadata.version("capstone"),capstone_module=capstone.__version__,
                  capstone_api=capstone.cs_version(),
                  reference_scripts={n:hashlib.sha256((args.oracle_root / "tools" / n).read_bytes()).hexdigest() for n in ("disasm.py","fdiff.py","nearest.py")},
                  files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.iterdir()) if p.is_file() and p.name!="manifest.json"})
    (OUT / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"Captured {len(cases)} decoding inputs and {len(requests)} complete command reports")


if __name__=="__main__":
    main()
