#!/usr/bin/env python3
"""Compare Wii_Retro's function-template generator with native Wii 1.0RC1.

Requires Windows, stock RC1 (including lmgr8c.dll), mwcc-tools, Clang and LLD.
All generated compilers, objects and traces go below --out-dir.
"""

import argparse
from collections import Counter
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import wii_retro_link as link

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
CALLBACKS = {
    "CTemplTool_Template2TemplFunc": ("trace_fallback", 0x5C180E),
    "CIMan_NeedsTemplateInstance": ("trace_needs", 0x5C1873),
    "RC1_InstantiateStoredDefinition": ("trace_packed", 0x5C18C4),
    "CError_SetErrorSourceRef": ("trace_error_source", 0x5C18D8),
    "CError_Error": ("trace_error", 0x5C18E6),
    "CPrep_StreamInsert": ("trace_insert", 0x5C18FE),
    "CParser_GetDeclSourceInfo": ("trace_decl_source", 0x5C1909),
    "lex": ("trace_lex", 0x5C193D),
    "CError_Internal": ("trace_internal", 0x5C1971),
    "CPrep_BrowserCurrentFile": ("trace_browser", 0x5C1993),
    "CTemplTool_PushInstance": ("trace_push", 0x5C19C4),
    "CTemplTool_MergeArgNames": ("trace_merge", 0x5C19DB),
    "CTemplTool_SetupTemplateArgumentNameSpace": ("trace_arguments", 0x5C19EA),
    "CTemplTool_SetupOuterTemplateArgumentNameSpace": ("trace_outer", 0x5C1A03),
    "CFunc_ParseFuncDef": ("trace_parse", 0x5C1A18),
    "CTemplTool_RemoveOuterTemplateArgumentNameSpace": ("trace_remove_outer", 0x5C1A21),
    "CTemplTool_PopInstance": ("trace_pop", 0x5C1A33),
    "CPrep_StreamRemove": ("trace_remove", 0x5C1A47),
    "CBrowse_NewFunction": ("trace_browse", 0x5C1A6F),
}
FIXTURE = """
extern int external(int);
template<class T> int calculate(T n) { return external((int)n); }
template int calculate(short);
extern template int calculate(unsigned);
template<class T> int late(T n);
int use(int n) {
    return calculate(n) + calculate((short)n) + calculate((unsigned)n) + late(n);
}
template<class T> int late(T n) { return calculate((long)n); }

template<class T> struct Member {
    static int value;
    T base;
    Member(T declared);
    virtual int read(T declared) const;
};
template<class T> int Member<T>::value = external(sizeof(T));
template<class T> Member<T>::Member(T input) : base(input) {}
template<class T> int Member<T>::read(T input) const { return value + base + input; }
template struct Member<short>;
int use_member(int n) {
    Member<int> value(n);
    return value.read(n);
}

#pragma parse_func_templ on
#pragma parse_mfunc_templ on
struct PackedResult { int values[9]; };
struct Root { int value; Root() : value(external(7)) {} };
struct Parent : virtual Root { Parent() {} };
struct Extra { Extra(int a, int b) { external(a + b); } };
struct Pair { int value; Pair(int a, int b) : value(external(a - b)) {} };
template<class Base> struct Derived : Base, Extra {
    Pair pair;
    int field;
    Derived();
    int read() const;
};
template<class Base> Derived<Base>::Derived()
    : Base(), Extra(7, external(8)), pair(external(9), external(10)), field(external(11)) {}
template<class Base> int Derived<Base>::read() const { return pair.value + field; }
template<class T> struct Outer {
    template<class U> struct Inner {
        T first;
        U second;
        Inner(T a, U b);
        int read() const;
    };
};
template<class T> template<class U>
Outer<T>::Inner<U>::Inner(T a, U b) : first(a), second(b) {}
template<class T> template<class U>
int Outer<T>::Inner<U>::read() const { return external((int)first + (int)second); }
int use_nested(int a, short b) {
    Outer<short>::Inner<int> narrow(b, a);
    Outer<int>::Inner<int> wide(a, b);
    return narrow.read() + wide.read();
}
template<class T> T packed_result(T value) { return value; }
template<class T, int Code> int packed_switch(T value) {
    switch (value) {
    case Code + 4: return 1;
    default: return 0;
    }
}
typedef short PackedShort;
template<class T> int packed_declaration(T value) {
    typedef T LocalAlias;
    LocalAlias local = value;
    PackedShort copy = (PackedShort)local;
    static T once = external(value);
    return (int)local + (int)once + copy;
}
template<class T> int packed_array(T size) {
    T values[size];
    values[0] = size;
    return (int)values[0];
}
struct CleanupItem {
    int tag;
    CleanupItem() : tag(external(7)) {}
    CleanupItem(int n) : tag(external(n)) {}
    ~CleanupItem() { external(tag); }
};
struct CleanupError { int code; };
template<class T> int packed_catch(int value) {
    try { return external(value); }
    catch (const T& caught) { return caught.code; }
    catch (...) { return -1; }
}
template<class T, class Size> int packed_class_vla(Size size) {
    T values[size];
    return values[0].tag;
}
template<class T> int packed_cleanup(int n) {
    T one(n);
    T many[3];
    T initialized[3] = {T(n), T(n + 1), T(n + 2)};
    int matrix[2][2] = {{n, n + 1}, {n + 2, n + 3}};
    return one.tag + many[1].tag + initialized[2].tag + matrix[1][0];
}
PackedResult use_stored(PackedResult value) {
    Derived<Parent> member;
    external(member.read());
    external(packed_switch<int, 3>(packed_declaration(value.values[0])));
    external(packed_array(1 + (value.values[0] & 7)));
    external(packed_class_vla<CleanupItem>(1 + (value.values[0] & 7)));
    external(packed_cleanup<CleanupItem>(value.values[0]));
    external(packed_catch<CleanupError>(value.values[0]));
    return packed_result(value);
}
#pragma parse_mfunc_templ off
#pragma parse_func_templ off
"""


def build_trace(args, native):
    source = ROOT / "gc2/tools/wii_retro/template_instantiation.c"
    data = source.read_text(encoding="utf-8")
    start = data.index("unsigned char retro_GenerateFunctionInstance(void* raw_template")
    end = data.index("\nunsigned char retro_InstantiateTemplateDefinitions(", start)
    implementation = "static " + data[start:end].replace(
        "retro_GenerateFunctionInstance(", "source_function(", 1
    )
    for original, (traced, _) in CALLBACKS.items():
        implementation = implementation.replace(original + "(", traced + "(")
    trace = (HERE / "function_instantiation_trace.h").read_text(encoding="utf-8")
    trace = trace.replace("IMPLEMENTATION", "" if native else implementation)
    trace = trace.replace("FUNCTION_WORKER", "native_function" if native else "source_function")
    output = args.out_dir / ("native" if native else "source")
    output.mkdir(parents=True, exist_ok=True)
    alternate = output / source.name
    alternate.write_text(data[:start] + trace + data[end:], encoding="utf-8")
    old_run, old_symbols, old_calls, old_redirect = (
        link.subprocess.run, link.DEFERRED_EXPORT_SYMBOLS,
        link.DEFERRED_EXPORT_CALLS, link.redirect_direct_calls,
    )
    try:
        def select(command, **kwargs):
            return old_run(
                [str(alternate) if Path(arg).name == source.name else arg for arg in command],
                **kwargs,
            )

        def redirect(objects, splits, callee, replacement):
            old_redirect(objects, splits, callee, replacement)
            if native and callee == "_CTempl_GenFuncInstance":
                # The alias names the same native entry; redirect its callers too.
                # The instrumented wrapper itself is compiled after this step.
                old_redirect(objects, splits, "_native_function", replacement)

        link.subprocess.run, link.redirect_direct_calls = select, redirect
        link.DEFERRED_EXPORT_SYMBOLS = old_symbols | {
            "RC1_trace_printf": 0x407DC0, "RC1_trace_link_name": 0x56A3E0,
        }
        if native:
            link.DEFERRED_EXPORT_SYMBOLS |= {"native_function": 0x5C17D0}
            link.DEFERRED_EXPORT_CALLS += tuple(
                (address, "_CTempl_GenFuncInstance",
                 "_" + original,
                 "_" + traced)
                for original, (traced, address) in CALLBACKS.items()
            )
        link.build(args.stock, output, args.tools, args.clang, args.linker)
    finally:
        link.subprocess.run, link.DEFERRED_EXPORT_SYMBOLS, link.DEFERRED_EXPORT_CALLS, link.redirect_direct_calls = (
            old_run, old_symbols, old_calls, old_redirect
        )
    return output / "mwcceppc.exe"


def check(compilers, work, source, mode, flags, expect_error):
    traces, results = {}, {}
    for label, compiler in compilers.items():
        output = work / f"{source.stem}-{mode}-{label}.o"
        output.unlink(missing_ok=True)
        proc = subprocess.run(
            [str(compiler), "-lang", "c++", *flags, "-c", str(source), "-o", str(output)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        lines = proc.stdout.decode(errors="replace").splitlines()
        (output.with_suffix(".log")).write_text("\n".join(lines) + "\n", encoding="utf-8")
        traces[label] = [line for line in lines if line.startswith("TF ")]
        results[label] = (
            proc.returncode, [line for line in lines if not line.startswith("TF ")],
            output.read_bytes() if output.is_file() else None,
        )
    if traces["native"] != traces["source"]:
        pairs = zip(traces["native"], traces["source"])
        difference = next(((i, a, b) for i, (a, b) in enumerate(pairs) if a != b), None)
        raise AssertionError((source.name, mode, difference,
                              len(traces["native"]), len(traces["source"])))
    failures = [line for line in traces["source"]
                if line.startswith("TF fixture-") and not line.endswith(" 1")]
    assert not failures, (source.name, mode, failures)
    assert all(result == results["production"] for result in results.values()), (source.name, mode)
    status, _, payload = results["production"]
    assert bool(status) == expect_error and bool(payload) != expect_error, (source.name, mode, status)
    counts = Counter(" ".join(line.split()[:2]) for line in traces["source"])
    assert all(counts["TF " + key] for key in (
        "enter", "leave", "parse", "browse", "fixture-stream", "fixture-declined",
        "fixture-fallback-chain", "fixture-packed-result",
    )), (source.name, mode, counts)
    if not expect_error:
        for name in ("packed_result", "packed_switch", "packed_declaration",
                     "packed_array", "packed_class_vla", "packed_cleanup", "packed_catch"):
            assert any(line.startswith(f"TF packed {name}<") for line in traces["source"]), (
                source.name, mode, name,
            )
    print(f"Matched {source.name} ({mode}): {counts['TF enter']} native calls; all fixture controls passed")
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, default=ROOT / "build/compilers/Wii/1.0RC1/mwcceppc.exe")
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/wii-retro-tests/function-instantiation")
    parser.add_argument("--tools", default=shutil.which("mwcc-tools"))
    parser.add_argument("--clang", default=shutil.which("clang"))
    parser.add_argument("--linker", default=shutil.which("lld-link"))
    args = parser.parse_args()
    for name in ("tools", "clang", "linker"):
        if not getattr(args, name):
            parser.error(f"{name} is missing; add it to PATH or pass --{name}")
    args.stock, args.out_dir = args.stock.resolve(), args.out_dir.resolve()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    production = args.out_dir / "production"
    link.build(args.stock, production, args.tools, args.clang, args.linker)
    compilers = {"production": production / "mwcceppc.exe",
                 "native": build_trace(args, True), "source": build_trace(args, False)}
    source = args.out_dir / "Functions.cpp"
    source.write_text(FIXTURE, encoding="utf-8")
    error = args.out_dir / "Functions_error.cpp"
    error.write_text(FIXTURE + """
template<class T> int broken_body(T n) { return n.no_such_member(); }
int broken = broken_body(1);
""", encoding="utf-8")
    summary = Counter()
    for mode, flags in (("configured", []), ("file", ["-ipa", "file"]),
                        ("program", ["-ipa", "program"]), ("debug", ["-ipa", "file", "-g"])):
        for unit in (source, error):
            summary.update(check(compilers, args.out_dir, unit, mode,
                                 ["-Cpp_exceptions", "on", *flags], unit == error))
    print("Matched native function-template generation:", dict(summary))


if __name__ == "__main__":
    main()
