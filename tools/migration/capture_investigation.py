"""Freeze masked function search, binding inference and cross-version matching."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT / "tests/fixtures/investigate"


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root",type=Path,required=True)
    args=parser.parse_args()
    sys.path.insert(0,str(args.oracle_root / "tools"))
    locate,split,funcmatch,pe_module=[importlib.import_module(n) for n in ("locate","split","funcmatch","pe")]
    OUT.mkdir(parents=True,exist_ok=True)
    rng=random.Random(1937)
    searches=[]
    for index in range(256):
        size=rng.randrange(96,512)
        blob=bytearray(rng.randbytes(size))
        code=rng.randbytes(rng.randrange(0,64))
        wild={i for i in range(len(code)) if rng.randrange(5)==0}
        if index % 7==0:
            wild=set(range(len(code)))
        for _ in range(index%4):
            position=rng.randrange(size-len(code)+1)
            blob[position:position+len(code)]=code
            for at in wild:
                blob[position+at]=rng.randrange(256)
        limit=index%6
        searches.append(dict(blob=list(blob),code=list(code),wild=sorted(wild),limit=limit,
                             matches=locate.find(blob,code,wild,limit)))
    (OUT / "find.json").write_text(json.dumps(searches,indent=2)+"\n")
    image=bytearray((ROOT / "tests/fixtures/compare/project.pe").read_bytes())
    image[0x400:0x600]=bytes(512)
    struct.pack_into("<I",image,0x180,0x180)
    objects={}
    # Relocation conflicts, ambiguous matches, skipped temporary names and one missing function.
    specs=[("a",0x10,6,"_address",0,[0],0x402000),
           ("b",0x20,20,"_call",-4,[32],0x401080),
           ("c",0x30,6,"_conflict",0,[64],0x402004),
           ("d",0x40,6,"_conflict",0,[96],0x402008),
           ("e",0x50,6,"_@tmp",0,[128],0x402010),
           ("f",0x60,6,"_ambiguous",0,[160,192],0x402014),
           ("g",0x70,6,"_missing",0,[],0x402018)]
    fixups=[]
    functions=[]
    for source,selected in (("src/one.c",specs),("src/two.c",specs[:1])):
        coff=split.Coff()
        for name,salt,kind,symbol,addend,places,address in selected:
            opcode=0xe8 if kind==20 else 0x68
            code=bytes([0x55,0x8b,0xec,0x83,0xec,salt,opcode])+struct.pack("<I",addend&0xffffffff)+b"\x83\xc4\x10\x5d\xc3"
            section=coff.section(".text",0x60500020,code,len(code),[])
            coff.add_symbol("_"+name,0,section,0x20,2)
            dest=coff.external(symbol)
            coff.sections[section-1]=".text",0x60500020,code,len(code),[(7,dest,kind)]
            if source=="src/one.c":
                for place in places:
                    retail=bytearray(code)
                    value=(address+addend if kind==6 else address+addend-(0x401000+place+11))&0xffffffff
                    struct.pack_into("<I",retail,7,value)
                    image[0x400+place:0x410+place]=retail
                    if kind==6:
                        fixups.append(place+7)
                    functions.append(dict(name=name+str(place),address=hex(0x401000+place),size=16))
        coff.section(".text",0x60500020,b"",0,[])
        coff.section(".text",0x60500020,b"\xcc"*8,8,[])
        file=source.replace("/","-")+".obj"
        coff.write(OUT / file)
        objects[source]=file
    image[0x4d0:0x4d8]=b"\xcc"*8

    def relocations(image,places):
        entries=[0x3000+offset for offset in places]
        if len(entries)%2:
            entries.append(0)
        size=8+2*len(entries)
        struct.pack_into("<II",image,0x120,0x3000,size)
        struct.pack_into("<II",image,0x800,0x1000,size)
        for i,entry in enumerate(entries):
            struct.pack_into("<H",image,0x808+2*i,entry)
    relocations(image,fixups)
    other=bytearray(image)
    other[0x4e0:0x4f0]=image[0x400:0x410]
    struct.pack_into("<I",other,0x4e7,0xdeadbeef)
    relocations(other,[*fixups,231])
    (OUT / "from.pe").write_bytes(image)
    (OUT / "to.pe").write_bytes(other)
    functions_b=[*functions,dict(name="duplicate-a",address="0x4010e0",size=16)]
    splits="Sections:\n\t.text type:code align:16\nsrc/one.c:\n\t.text start:0x00401000 end:0x00401020\nsrc/two.c:\n\t.text start:0x00401020 end:0x00401100\n"
    project=dict(objects=objects,functions_from=functions,functions_to=functions_b,splits=splits,
                 bindings={"_address":"0x00402099","_old":"0x00402000"})
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        for version,payload,fs in (("fixtureA",image,functions),("fixtureB",other,functions_b)):
            path=directory / f"build/compilers/GC/{version}/mwcceppc.exe"
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(payload)
            config=directory / ("config/GC_"+version)
            config.mkdir(parents=True)
            (config / "config.json").write_text(json.dumps(dict(original=path.relative_to(directory).as_posix(),sha1=hashlib.sha1(payload).hexdigest())))
            (config / "functions.json").write_text(json.dumps(fs))
            (config / "splits.txt").write_text(splits)
            (config / "bindings.json").write_text(json.dumps(project["bindings"]))
        for source,file in objects.items():
            path=directory / f"build/GC_fixtureA/compiled/{source}.obj"
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes((OUT / file).read_bytes())
        previous=Path.cwd()
        try:
            os.chdir(directory)
            project["functions"]={source:[dict(name=n,code=list(c),wild=sorted(w),relocations=[dict(offset=o,symbol=s,kind=k,addend=a) for o,s,k,a in rs])
                                             for (n,c,w),rs in zip(locate.functions_of(OUT / file),locate.relocations_of(OUT / file))]
                                  for source,file in objects.items()}
            project["inferred"]={source:locate.bindings_from("fixtureA",source) for source in objects}
            project["locate"]=subprocess.check_output([sys.executable,str(args.oracle_root / "tools/locate.py"),"fixtureA",*objects],text=True)
            project["bindfrom"]=subprocess.check_output([sys.executable,str(args.oracle_root / "tools/bindfrom.py"),"fixtureA"],text=True)
            project["bound"]=json.loads((directory / "config/GC_fixtureA/bindings.json").read_text())
            result,ambiguous=funcmatch.match("fixtureA","fixtureB")
            project["matched"]=dict(result=result,ambiguous=ambiguous,total=len(functions))
            pe=pe_module.PEFile(OUT / "from.pe")
            fix=split.base_relocations(pe)
            project["masked"]=[dict(address=0x401000+at,size=size,bytes=list(funcmatch.masked(pe,fix,0x401000+at,size)))
                               for at,size in ((0,16),(32,16),(0,8),(0,9),(0,10),(0,11),(0,1),(160,16),(208,8))]
        finally:
            os.chdir(previous)
    (OUT / "project.json").write_text(json.dumps(project,indent=2)+"\n")
    manifest=dict(reference_commit=subprocess.check_output(["git","-C",str(args.oracle_root),"rev-parse","HEAD"],text=True).strip(),
                  files={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.iterdir()) if p.is_file() and p.name!="manifest.json"})
    (OUT / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"Captured {len(searches)} masked searches and complete investigation workflows")


if __name__ == "__main__":
    main()
