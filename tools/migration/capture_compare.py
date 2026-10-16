"""Freeze per-function relocation/literal resolution against synthetic PE and COFF files."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests/fixtures/compare"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.oracle_root / "tools"))
    split, compare, pe_module = [importlib.import_module(n) for n in ("split", "compare", "pe")]
    OUT.mkdir(parents=True, exist_ok=True)
    cases = []

    def capture(name, *, kind=6, addend=0, dest_name="_dest", dest_section=0, dest_value=0,
                storage=2, addresses=None, data=None, flags=0xc0300040, contents=(),
                original_value=0x402020, target_size=5, table=(), query="_fn", function="_fn",
                boundary=None, extra_function=None, offset=0):
        image = bytearray((ROOT / "tests/fixtures/binary/synthetic.pe").read_bytes())
        image[0x400:] = b"\xbb" * 0x400
        struct.pack_into("<I", image, 0x200, original_value)
        image[0x204] = 0xc3
        for at, payload in contents:
            image[0x400+at:0x400+at+len(payload)] = payload
        image_path, object_path = OUT / (name+".pe"), OUT / (name+".obj")
        image_path.write_bytes(image)
        coff = split.Coff()
        code = struct.pack("<I", addend)+b"\xc3"
        coff.section(".text", 0x60500020, code, len(code), [])
        fn = coff.add_symbol(function, 0, 1, 0x20, 2)
        if boundary is not None:
            coff.add_symbol("_next_function", boundary, 1, 0x20, 2)
        if extra_function is not None:
            coff.add_symbol(extra_function, 0, 1, 0x20, 2)
        if data is not None:
            coff.section(".bss" if flags & 0x80 else ".data", flags,
                         b"" if flags & 0x80 else data, len(data), [])
        dest = coff.add_symbol(dest_name, dest_value, dest_section, 0, storage)
        coff.sections[0] = (".text", 0x60500020, code, len(code), [(offset, dest, kind)])
        if table:
            relocations = []
            for at, value, table_kind in table:
                symbol = fn if value == "fn" else coff.add_symbol("absolute_"+str(at), value, -1, 0, 2)
                relocations.append((at, symbol, table_kind))
            n, f, payload, size, _ = coff.sections[1]
            coff.sections[1] = n, f, payload, size, relocations
        coff.write(object_path)
        case = dict(name=name, symbol=query, target_address=0x401000, target_size=target_size,
                    addresses=addresses or {})
        try:
            body, resolutions = compare.resolve_function(*compare.read_object(object_path), query, 0x401000,
                                                         case["addresses"], pe_module.PEFile(image_path), target_size)
            case.update(body=list(body), resolutions=resolutions)
        except ValueError as error:
            case["error"] = str(error)
        cases.append(case)

    for kind in (6, 7, 20, 12):
        for addend in (0, 4, 0xffffffe0):
            capture(f"external-{kind}-{addend}", kind=kind, addend=addend, addresses={"_dest": 0x402020})
    capture("stdcall-reference", dest_name="_dest@8", addresses={"_dest":0x402020})
    capture("stdcall-function", function="_fn@4", addresses={"_dest":0x402020})
    capture("ambiguous-function", function="_fn@4", extra_function="@fn@8", addresses={"_dest":0x402020})
    capture("missing-function", query="_missing", addresses={"_dest":0x402020})
    capture("local-label", dest_name="local", dest_section=1, dest_value=2, storage=3)
    capture("absolute", dest_section=-1, dest_value=0x12345678)
    capture("unbound-external")
    capture("cross-boundary", boundary=2, addresses={"_dest":0x402020})
    capture("unbound-function", data=b"\xc3", flags=0x60500020, dest_section=2, storage=3)
    for repeated in (False, True):
        contents = [(0x20,b"needle\0"), *([(0x40,b"needle\0")] if repeated else [])]
        for bound in (False, True):
            capture(f"string-{repeated}-{bound}", data=b"needle\0", dest_section=2, storage=3,
                    addresses={"_dest":0x402020} if bound else {}, contents=contents,
                    original_value=0x402040 if repeated else 0x402020)
    capture("ambiguous-block-no-original", data=b"\x81\x82\x83\x84", dest_section=2, storage=3,
            contents=[(0x20,b"\x81\x82\x83\x84"),(0x40,b"\x81\x82\x83\x84")], target_size=None)
    capture("unique-block", data=b"\x81\x82\x83\x84", dest_section=2, storage=3,
            contents=[(0x20,b"\x81\x82\x83\x84")])
    capture("numeric-bound-static", data=b"\x81\x82\x83\x84", dest_section=2, storage=3,
            addresses={"_dest":0x402060})
    capture("mismatched-static-binding", data=b"needle\0", dest_section=2, storage=3,
            addresses={"_dest":0x402060}, contents=[(0x20,b"needle\0")])
    capture("pool-with-addend", data=b"different\0"+bytes(6)+b"needle\0", dest_section=2,
            storage=3, addend=16, contents=[(0x40,b"needle\0")], original_value=0x402040)
    capture("trimmed-jump-table", data=struct.pack("<II",0,2)+bytes(8), dest_section=2, storage=3,
            table=[(0,"fn",6),(4,"fn",6)], contents=[(0x20,struct.pack("<II",0x401000,0x401002))])
    capture("image-relative-table", data=struct.pack("<II",0,2), dest_section=2, storage=3,
            table=[(0,"fn",7),(4,"fn",7)], contents=[(0x20,struct.pack("<II",0x1000,0x1002))])
    capture("absolute-table", data=bytes(4), dest_section=2, storage=3,
            table=[(0,0x12345678,6)], contents=[(0x20,struct.pack("<I",0x12345678))])
    capture("bss-own-reference", data=bytes(8), flags=0xc0300080, dest_section=2, storage=3, original_value=0x405000)
    capture("bss-rejected-contents", data=bytes(8), flags=0xc0300080, dest_section=2, storage=3)
    capture("literal-subitem", data=b"other\0"+bytes(2)+b"\x81\x82\x83\x84", dest_section=2,
            storage=3, addend=8, contents=[(0x20,b"\x81\x82\x83\x84")])
    (OUT / "resolutions.json").write_text(json.dumps(cases, indent=2)+"\n")
    coff_cases=[]
    for i,(payload,symbol,code,size) in enumerate(((b"abc",None,True,0),(b"abc","_fn",True,0),
                                               (b"data","?decorated@@",False,0),(b"",None,False,20))):
        path = OUT / f"single-{i}.obj"
        compare.write_coff(path,payload,symbol,code,size)
        coff_cases.append(dict(file=path.name,data=list(payload),symbol=symbol,code=code,bss_size=size))
    (OUT / "single-coff.json").write_text(json.dumps(coff_cases,indent=2)+"\n")
    # A complete synthetic project separates byte equality from symbolic-fixup equality.
    image = bytearray(0xa00)
    image[:2]=b"MZ"
    struct.pack_into("<I",image,0x3c,0x80)
    image[0x80:0x84]=b"PE\0\0"
    struct.pack_into("<HHIIIHH",image,0x84,0x14c,4,0,0,0,224,0)
    struct.pack_into("<H",image,0x98,0x10b)
    struct.pack_into("<I",image,0xb4,0x400000)
    struct.pack_into("<II",image,0x120,0x3000,12)
    for i,(name,rva,virtual,size,offset,flags) in enumerate(((b".text",0x1000,0x80,0x200,0x400,0x60000020),
            (b".data",0x2000,0x40,0x200,0x600,0xc0000040),(b".reloc",0x3000,12,0x200,0x800,0x42000040),
            (b".bss",0x4000,16,0,0,0xc0000080))):
        struct.pack_into("<8sIIIIIIHHI",image,0x178+i*40,name,virtual,rva,size,offset,0,0,0,0,flags)
    for offset in (0,16,32,64):
        image[0x400+offset:0x405+offset]=struct.pack("<I",0x402000)+b"\xc3"
    image[0x460]=0xc3
    struct.pack_into("<IIHH",image,0x800,0x1000,12,0x3000,0x3010)
    (OUT / "project.pe").write_bytes(image)
    functions=[dict(name=name,address=hex(0x401000+at),size=5,source=source,**extras)
               for name,at,source,extras in (("a",0,"src/matched.cpp",{}),("b",16,"src/nonmatching.cpp",{}),
                    ("c",32,"src/nonmatching.cpp",{}),("missing",48,"src/nonmatching.cpp",{}),
                    ("patch",64,"src/matched.cpp",{"binary_patch":True}))]
    functions.append(dict(name="?unrecovered@@",address="0x401060",size=1))
    for label,names in (("matched",[("a",0,True),("patch",16,False)]),("nonmatching",[("b",0,False),("c",16,True)])):
        coff=split.Coff()
        code=bytearray(32)
        coff.section(".text",0x60500020,code,len(code),[])
        destination=coff.external("_data")
        relocs=[]
        for name,at,symbolic in names:
            code[at:at+5]=struct.pack("<I",0 if symbolic else 0x402000)+b"\xc3"
            coff.add_symbol("_"+name,at,1,0x20,2)
            coff.add_symbol("end_"+name,at+5,1,0x20,3)
            if symbolic:
                relocs.append((at,destination,6))
        coff.sections[0]=".text",0x60500020,bytes(code),len(code),relocs
        coff.write(OUT / ("project-"+label+".obj"))
    project=dict(config={"original":"original.exe","sha1":hashlib.sha1(image).hexdigest()},functions=functions,
                 bindings={"_data":"0x402000"},sources={"src/matched.cpp":{},"src/nonmatching.cpp":{"status":"NonMatching"}})
    with tempfile.TemporaryDirectory() as temporary:
        directory=Path(temporary)
        config_dir=directory / "config/FIXTURE"
        config_dir.mkdir(parents=True)
        for name,value in (("config",project["config"]),("functions",functions),("bindings",project["bindings"])):
            (config_dir / (name+".json")).write_text(json.dumps(value))
        (directory / "config/sources.json").write_text(json.dumps(project["sources"]))
        (directory / "original.exe").write_bytes(image)
        for label in ("matched","nonmatching"):
            path=directory / f"build/FIXTURE/compiled/src/{label}.cpp.obj"
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes((OUT / ("project-"+label+".obj")).read_bytes())
        previous=Path.cwd()
        try:
            os.chdir(directory)
            _,_,rows=compare.inventory("FIXTURE")
            _,checks=compare.check("FIXTURE")
            project["rows"]=rows
            project["checks"]={n:dict(body=list(b) if b is not None else None,exact=s) for n,(b,s) in checks.items()}
            compare.extract("FIXTURE")
            compare.compare("FIXTURE")
            project["objdiff"]=json.loads((directory / "objdiff.json").read_text())
            project["completion"]=(directory / "build/FIXTURE/ok").read_text()
            project["objects"]={p.relative_to(directory).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in (directory / "build/FIXTURE").rglob("*.obj") if "/compiled/" not in p.as_posix()}
            report=dict(units=[dict(name="unknown/.text/00401005",functions=[{"name":"__unknown"}],
                                    measures={"total_functions":1,"matched_functions_percent":0.0}),
                               dict(name="src/matched/a",functions=[{"name":"_a"}],measures={"total_functions":1})],
                        measures={"total_functions":2,"matched_functions":1},categories=[
                            dict(id="code",measures={"total_functions":2,"matched_functions":1}),
                            dict(id="data",measures={"total_functions":0})])
            project["report_input"]=report
            (directory / "build/FIXTURE/report.json").write_text(json.dumps(report))
            original_run=compare.subprocess.run
            try:
                compare.subprocess.run=lambda *args,**kwargs: None
                compare.report("FIXTURE")
            finally:
                compare.subprocess.run=original_run
            project["report_output"]=json.loads((directory / "build/FIXTURE/report.json").read_text())
        finally:
            os.chdir(previous)
    (OUT / "project.json").write_text(json.dumps(project,indent=2)+"\n")
    manifest={"reference_commit":subprocess.check_output(["git","-C",str(args.oracle_root),"rev-parse","HEAD"],text=True).strip(),
              "reference_script_sha256":hashlib.sha256((args.oracle_root / "tools/compare.py").read_bytes()).hexdigest(),
              "platform":sys.platform,
              "files":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(OUT.iterdir())
                       if p.is_file() and p.name != "manifest.json"}}
    (OUT / "manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(f"Captured {len(cases)} relocation/literal cases and {len(coff_cases)} extraction objects")


if __name__ == "__main__":
    main()
