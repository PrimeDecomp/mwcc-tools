"""Freeze split derivation and executable remapping on redistributable synthetic images."""
import argparse
import copy
import hashlib
import importlib
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT / "tests/fixtures/metadata"


def stage(directory,metadata):
    for version,file,functions in (("fixtureA","from.pe",metadata["functions_from"]),("fixtureB","to.pe",metadata["functions_to"])):
        path=directory / f"build/compilers/GC/{version}/mwcceppc.exe"
        path.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(OUT / file,path)
        config=directory / ("config/GC_"+version)
        config.mkdir(parents=True,exist_ok=True)
        (config / "config.json").write_text(json.dumps(dict(original=path.relative_to(directory).as_posix())))
        (config / "functions.json").write_text(json.dumps(functions))
        (config / "bindings.json").write_text(json.dumps(metadata["bindings"]))
        (config / "splits.txt").write_text(metadata["reference_splits"])
        (config / "symbols.txt").write_text(metadata["reference_symbols"])
        for source,file in metadata["objects"].items():
            target=directory / f"build/GC_{version}/compiled/{source}.obj"
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(OUT / file,target)
    (directory / "config/lmgr326b.def").write_text("LIBRARY lmgr326b.dll\nEXPORTS\n    license @1 NONAME\n")
    path=directory / "lib/win32sdk/KERNEL32.LIB"
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(b"synthetic __imp__ExitProcess@4 import decoration")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    args=parser.parse_args()
    sys.path.insert(0,str(args.oracle_root / "tools"))
    derive,remap,pe_module=[importlib.import_module(n) for n in ("derive_splits","remap","pe")]
    OUT.mkdir(parents=True,exist_ok=True)
    inputs=ROOT / "tests/fixtures/investigate"
    metadata=json.loads((inputs / "project.json").read_text())
    functions=metadata["functions_from"]
    for i,f in enumerate(functions):
        f["name"]=["a","b","shared","shared","onlyref","f","f"][i]
        f["source"]="src/one.c" if i<3 else "src/two.c"
    objects=metadata["objects"]
    for file in objects.values():
        shutil.copyfile(inputs / file,OUT / file)
    image=bytearray((inputs / "from.pe").read_bytes())+bytearray(0xa00)
    struct.pack_into("<H",image,0x86,9)
    struct.pack_into("<I",image,0x178+3*40+8,19)  # BSS tail becomes one-byte COMMON.
    for i,(name,rva,offset) in enumerate(((b".idata",0x5000,0xa00),(b".rdata",0x6000,0xc00),
                                       (b".exc",0x7000,0xe00),(b".CRT",0x8000,0x1000),(b".tls",0x9000,0x1200)),4):
        struct.pack_into("<8sIIIIIIHHI",image,0x178+i*40,name,0x200,rva,0x200,offset,0,0,0,0,0xc0000040)
    struct.pack_into("<II",image,0x100,0x5000,60)
    struct.pack_into("<IIIII",image,0xa00,0x5080,0,0,0x50a0,0x5060)
    struct.pack_into("<IIIII",image,0xa14,0x5088,0,0,0x50b0,0x5070)
    struct.pack_into("<II",image,0xa60,0x50c0,0)
    struct.pack_into("<II",image,0xa70,0x80000001,0)
    image[0xaa0:0xaad]=b"KERNEL32.dll\0"
    image[0xab0:0xabd]=b"lmgr326b.dll\0"
    image[0xac0:0xace]=b"\0\0ExitProcess\0"
    image[0x410:0x416]=b"\xff\x25"+struct.pack("<I",0x405060)
    image[0x416:0x41c]=b"\xff\x25"+struct.pack("<I",0x405070)
    image[0x450:0x454]=b"\x55\x8b\xec\xc3"
    struct.pack_into("<I",image,0x447,0x404000)
    struct.pack_into("<I",image,0x467,0x404008)
    # The two thunk addresses also need HIGHLOW fixups.
    fixups=[7,18,24,71,103,135,167,199]
    struct.pack_into("<II",image,0x120,0x3000,8+2*len(fixups))
    struct.pack_into("<II",image,0x800,0x1000,8+2*len(fixups))
    for i,offset in enumerate(fixups):
        struct.pack_into("<H",image,0x808+i*2,0x3000+offset)
    (OUT / "from.pe").write_bytes(image)
    other=bytearray(image)
    struct.pack_into("<I",other,0xb4,0x410000)
    for offset in fixups:
        value=struct.unpack_from("<I",other,0x400+offset)[0]
        struct.pack_into("<I",other,0x400+offset,value+0x10000)
    struct.pack_into("<I",other,0x4c7,0x412018)  # Conflicting evidence for the duplicated data value.
    (OUT / "to.pe").write_bytes(other)
    to_functions=copy.deepcopy(functions)
    for f in to_functions:
        f["address"]=hex(int(f["address"],16)+0x10000)
    splits="Sections:\n"
    for name,kind in ((".text","code"),(".data","data"),(".reloc","data"),(".bss","bss"),(".idata","data"),
                      (".rdata","rodata"),(".exc","data"),(".CRT","data"),(".tls","data")):
        splits+=f"\t{name} type:{kind} align:4\n"
    splits+="src/one.c:\n\t.text start:0x00401000 end:0x00401040\ndata-only:\n\t.rdata start:0x00406000 end:0x00406200\nsrc/two.c:\n\t.text start:0x00401040 end:0x00401180\ncommon:\n\t.bss start:0x00404000 end:0x00404013\n"
    for name,address in ((".exc",0x407000),(".CRT",0x408000),(".tls",0x409000)):
        splits+=f"runtime-{name}:\n\t{name} start:0x{address:08X} end:0x{address+0x200:08X}\n"
    symbols="_fn_00401050 = .text:0x00401050; // type:function size:0x4 scope:global\n_onlyref@8 = .text:0x00401080; // type:function size:0x10 scope:global\n"
    for name,address in ((".rdata",0x406000),(".exc",0x407000),(".CRT",0x408000),(".tls",0x409000)):
        symbols+=f"runtime_{name[1:]} = {name}:0x{address:08X}; // type:object size:0x8 scope:global force_active\n"
    bindings={"_data_00402000":"0x00402000","_realData":"0x00402000","_a":"0x00401000",
              "_shared":"0x00402010","_interpolated":"0x00402008","_bss":"00404000","_lost":"0x00409080"}
    project=dict(objects=objects,functions_from=functions,functions_to=to_functions,reference_splits=splits,
                 reference_symbols=symbols,bindings=bindings)
    rng=random.Random(53)
    costs=[]
    for _ in range(256):
        units=rng.randrange(1,12)
        matrix=[[rng.choice([0.0,0.9,1.0,2.0]) for _ in range(units)] for _ in range(rng.randrange(0,48))]
        owners=derive.monotone_assignment(matrix,units,lambda i,u:matrix[i][u])
        costs.append(dict(costs=matrix,units=units,owners=owners))
    (OUT / "assignment.json").write_text(json.dumps(costs,indent=2)+"\n")
    patterns=[]
    for _ in range(256):
        blob=bytes(rng.randrange(4) for _ in range(rng.randrange(0,96)))
        code=bytes(rng.randrange(4) for _ in range(rng.randrange(0,20)))
        wild={i for i in range(len(code)) if rng.randrange(3)==0}
        low=rng.randrange(len(blob)+1)
        high=rng.randrange(low,len(blob)+1)
        pattern=remap.pattern(code,wild)
        matches=[i for i in range(low,high+1) if pattern.match(blob,i,high)]
        patterns.append(dict(blob=list(blob),code=list(code),wild=sorted(wild),low=low,high=high,matches=matches))
    (OUT / "patterns.json").write_text(json.dumps(patterns,indent=2)+"\n")
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        stage(directory,project)
        previous=Path.cwd()
        try:
            os.chdir(directory)
            expected=derive.derive("fixtureB","fixtureA",None,None)
            image,order,ranges,owners,refs,everywhere,_,sections=expected
            lines=derive.make_symbols(image,order,ranges,owners,refs,derive.read_symbols("config/GC_fixtureA/symbols.txt"))
            project["derived"]=dict(functions=image.functions,order=order,ranges={n:[dict(section=s,start=a,end=b,options=o) for s,a,b,o in rs] for n,rs in ranges.items()},
                                    owners=owners,refs={a:sorted(us) for a,us in refs.items()},everywhere=sorted(everywhere),sections=sections,
                                    imports=[dict(slot=s,symbol=n,dll=d) for s,n,d in derive.import_table(image)],
                                    splits=derive.format_splits(sections,order,ranges),symbols=lines)
            project["compiled_symbols"]=dict(definitions=derive.compiled_definitions("fixtureB"),decorated=derive.compiled_names("fixtureB"))
            a,b=pe_module.PEFile(OUT / "from.pe"),pe_module.PEFile(OUT / "to.pe")
            fix_a,fix_b=remap.base_relocations(a),remap.base_relocations(b)
            found,missing=remap.match_functions(functions,a,fix_a,b,fix_b)
            pairs=remap.data_pairs(functions,found,a,fix_a,b)
            project["remapped"]=dict(found=found,missing=missing,pairs=pairs,
                                     wildcards=[dict(address=int(f["address"],16),size=f["size"],code=list(c),wild=sorted(w)) for f in functions
                                                for c,w in [remap.wildcards(a,fix_a,int(f["address"],16),f["size"])]] )
            for symbols in (False,True):
                text=subprocess.check_output([sys.executable,str(args.oracle_root / "tools/remap.py"),"fixtureA","fixtureB",*(["--symbols"] if symbols else [])],text=True)
                project["with_symbols" if symbols else "without_symbols"]=dict(report=text,
                    functions=json.loads(Path("config/GC_fixtureB/functions.json").read_text()),bindings=json.loads(Path("config/GC_fixtureB/bindings.json").read_text()))
        finally:
            os.chdir(previous)
    (OUT / "project.json").write_text(json.dumps(project,indent=2)+"\n")
    manifest=dict(reference_commit=subprocess.check_output(["git","-C",str(args.oracle_root),"rev-parse","HEAD"],text=True).strip(),
                  reference_scripts={n:hashlib.sha256((args.oracle_root / "tools" / n).read_bytes()).hexdigest()
                                     for n in ("remap.py","derive_splits.py","split.py","pe.py")},
                  files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.iterdir()) if p.is_file() and p.name!="manifest.json"})
    (OUT / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("Captured ownership, import/runtime derivation and both remap workflows")


if __name__=="__main__":
    main()
