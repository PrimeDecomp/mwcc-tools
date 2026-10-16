"""Compare real version Ninja/objdiff graphs and execute UTF-8 depfile tracking with Ninja."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from capture_build_config import VERSIONS,normalize_ninja,project as synthetic_project,stage


def run(command,directory):
    result=subprocess.run(command,cwd=directory,capture_output=True,text=True,encoding="utf-8",timeout=120)
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
    requests=[]
    for version in VERSIONS:
        requests.extend([(version,[]),(version,["--mode","compare"])])
        if version!="GC_1_0":
            requests.append((version,["--mode","link","--split-only"]))
        if version in ("GC_1_3","GC_1_3_2","GC_2_7"):
            requests.append((version,["--mode","link","--oracle"]))
    with tempfile.TemporaryDirectory(prefix="configure-parity-") as temporary:
        for index,(version,options) in enumerate(requests):
            outputs=[]
            for backend in ("python","rust"):
                directory=Path(temporary) / str(index) / backend
                (directory / "config").mkdir(parents=True)
                shutil.copyfile(root / "config/sources.json",directory / "config/sources.json")
                for name in VERSIONS:
                    config=directory / "config" / name
                    config.mkdir()
                    shutil.copyfile(root / "config" / name / "config.json",config / "config.json")
                    if (root / "config" / name / "splits.txt").exists():
                        shutil.copyfile(root / "config" / name / "splits.txt",config / "splits.txt")
                command=([sys.executable,str(root / "configure.py")] if backend=="python" else [str(binary),"configure"])
                run([*command,"--version",version,*options],directory)
                ninja=(directory / "build.ninja").read_text()
                targets=run(["ninja","-t","targets","all"],directory)
                objdiff=json.loads((directory / "objdiff.json").read_text()) if (directory / "objdiff.json").exists() else None
                # Native Windows rules name the PE executable explicitly; an
                # extensionless file cannot be launched by Ninja/CreateProcess.
                outputs.append(dict(ninja=normalize_ninja(ninja.replace("build/tools/objdiff-cli.exe", "build/tools/objdiff-cli")),
                                    objdiff=objdiff,targets=targets.replace("build/tools/objdiff-cli.exe", "build/tools/objdiff-cli")))
            if outputs[0]!=outputs[1]:
                if args.keep:
                    args.keep.parent.mkdir(parents=True,exist_ok=True)
                    args.keep.with_suffix(".mismatch.json").write_text(json.dumps(outputs,indent=2)+"\n")
                raise RuntimeError(f"{version} {options}: configuration differs")
            results.append(dict(version=version,options=options,identical=True,targets=len(outputs[1]["targets"].splitlines()),
                                ninja_sha256=hashlib.sha256(outputs[1]["ninja"].encode()).hexdigest()))
        # Use the generated native executable quoting from a path containing shell/Ninja metacharacters.
        directory=Path(temporary) / "unicode"
        stage(directory,synthetic_project())
        tool=directory / "native tools $ &" / binary.name
        tool.parent.mkdir()
        shutil.copyfile(binary,tool)
        tool.chmod(0o755)
        run([str(tool),"configure"],directory)
        variables=[line for line in (directory / "build.ninja").read_text().splitlines() if line.startswith(("tools = ","tools_dep = "))]
        prefix="cmd /c echo object>out.o" if sys.platform=="win32" else "printf object > out.o"
        (directory / "build.ninja").write_text("\n".join([*variables,"rule compile",f"  command = {prefix} && $tools transform-dep input.dep out.o",
                                                       "  depfile = out.o.d","  deps = gcc","build out.o: compile source.c",""]),encoding="utf-8")
        (directory / "source.c").write_text("int x;\n")
        (directory / "café.h").write_text("/* dependency */\n",encoding="utf-8")
        (directory / "input.dep").write_bytes(b"out.o: source.c caf\xe9.h\r\n")
        run(["ninja","-v"],directory)
        dependencies=run(["ninja","-t","deps"],directory)
        if "café.h" not in dependencies or (directory / "input.dep").exists():
            raise RuntimeError("native UTF-8 dependency was not recorded by Ninja")
        unchanged=run(["ninja","-n"],directory)
        if "no work to do" not in unchanged:
            raise RuntimeError("unchanged native depfile build is not up to date")
        timestamp=(directory / "out.o").stat().st_mtime+2
        os.utime(directory / "café.h",(timestamp,timestamp))
        changed=run(["ninja","-n"],directory)
        if "transform-dep" not in changed:
            raise RuntimeError("changing the accented header did not schedule a rebuild")
        results.append(dict(utf8_ninja_depfile=True,quoted_tool_path=True,unchanged_build_clean=True,header_change_rebuilds=True))
        directory=Path(temporary) / "compiler"
        metadata=synthetic_project()
        metadata["sources"]={"src/fixture.c":dict(compiler="pro53",flags=["-O4","-sym","on"])}
        stage(directory,metadata)
        (directory / "src").mkdir()
        (directory / "src/fixture.c").write_text('#include "inc.h"\nint fixture(int x) { return x + VALUE; }\n')
        (directory / "include/libc").mkdir(parents=True)
        (directory / "include/inc.h").write_text("#define VALUE 7\n")
        for name in ("mwcc.exe","lmgr326b.dll"):
            destination=directory / "build/compilers/pro53" / name
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(root / "build/compilers/pro53" / name,destination)
        if sys.platform!="win32":
            loader=directory / "build/tools/wibo"
            loader.parent.mkdir(parents=True)
            shutil.copyfile(shutil.which("wibo"),loader)
            loader.chmod(0o755)
        object_name="build/GC_1_2_5/compiled/src/fixture.c.obj"
        (directory / object_name).parent.mkdir(parents=True)
        compiled=[]
        for backend in ("python","rust"):
            if backend=="python":
                tools=directory / "tools"
                tools.mkdir()
                shutil.copyfile(root / "tools/transform_dep.py",tools / "transform_dep.py")
            command=([sys.executable,str(root / "configure.py")] if backend=="python" else [str(binary),"configure"])
            run([*command,"--version","GC_1_2_5","--mode","compare"],directory)
            compiler_command=run(["ninja","-t","commands",object_name],directory).splitlines()[-1]
            # Ninja launches the Windows command directly; another cmd /c would
            # move the compiler outside the environment-setting command's scope.
            compilation=subprocess.run(compiler_command,shell=sys.platform!="win32",cwd=directory,capture_output=True,text=True,timeout=120)
            if compilation.returncode or compilation.stdout or compilation.stderr:
                raise RuntimeError(f"{backend} generated compiler command failed:\n{compilation.stdout}\n{compilation.stderr}")
            compiled.append(((directory / object_name).read_bytes(),Path(str(directory / object_name)+".d").read_bytes()))
        if compiled[0]!=compiled[1]:
            raise RuntimeError("generated compiler commands produce different objects/depfiles")
        results.append(dict(generated_compiler_command=True,object_identical=True,depfile_identical=True,
                            object_sha256=hashlib.sha256(compiled[1][0]).hexdigest()))
    if args.keep:
        args.keep.parent.mkdir(parents=True,exist_ok=True)
        args.keep.write_text(json.dumps(results,indent=2)+"\n")
    print(f"{len(requests)} full version configurations agree; UTF-8 depfiles/tool quoting and generated compilation pass")


if __name__=="__main__":
    main()
