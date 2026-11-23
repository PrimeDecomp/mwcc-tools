#!/usr/bin/env python3
"""Exercise the source-built Wii 1.0RC1 compiler on Windows."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "build/Wii_1_0RC1/clang/mwcceppc.exe"
FLAGS = ("-nodefaults", "-proc", "gekko", "-fp", "hard")
CPP_SOURCE = """\
#if __MWERKS__ != 0x4201 || __CWCC__ != 0x4201 || __CWBUILD__ != 140
#error incorrect compiler macros
#endif
#if !defined(__PPCGEKKO__) || !defined(__PPCBROADWAY__)
#error incorrect CPU macros
#endif
template<class T> inline T add_three(T value) { return value + 3; }
extern "C" int increment(int value) { return add_three(value); }
"""
COPY_SOURCE = """\
typedef struct Bytes { unsigned char data[8]; } Bytes;
void copy(Bytes* dest, const Bytes* src) { *dest = *src; }
"""
IPA_SOURCE = """\
static int unused(int x) { return x + 99; }
static int leaf(int);
int forward(int x) { return leaf(x); }
static int leaf(int x) { return x + 3; }
static int chained(int x) { return leaf(x + 2); }
int entry(int x) { return chained(x) + 1; }
static int address_leaf(int x) { return x - 2; }
int (*address_taken)(int) = address_leaf;
static int data_dead;
static int data_used = 55;
int accessor(void) { return data_used; }
"""
IPA_ASSEMBLY_SOURCE = """\
static int target(int x);
asm int invoke(int x) {
    nofralloc
    b target
}
static int target(int x) { return x + 7; }
"""
IPA_CPP_SOURCE = """\
struct Left { virtual int left() { return 1; } };
struct Right { virtual int right() { return 2; } };
struct Widget : Left, Right {
    Widget();
    virtual int right();
    int value;
};
extern int supplied();
Widget::Widget() : value(supplied()) {}
int Widget::right() { return value; }
static Widget state;
Right* use_widget() { return &state; }
static int dead_value() { return 4; }
template<typename T> static T add2(const T& value) { return value + 2; }
int use_template(int value) { return add2(value); }
"""
IPA_EXCEPTION_SOURCE = """\
extern void observe(int);
struct Guard {
    int value;
    explicit Guard(int x) : value(x) {}
    ~Guard() { observe(value); }
};
static int fail(int x) { if (x < 0) { throw x; } return x + 2; }
static void dead_catch(int x) { try { observe(x); } catch (char c) { observe(c); } }
void live_throw(char c) { throw c; }
int entry(int x) {
    try { Guard guard(x); return fail(x); }
    catch (int y) { return y - 1; }
}
"""
TEMPLATE_FRIEND_SOURCE = """\
template<class T> class Box {
    int value;
    template<class U> friend class Box;
public:
    explicit Box(int v) : value(v) {}
    template<class U> int read(const Box<U>& other) { return other.value; }
};
int entry() { Box<int> first(1); return first.read(Box<char>(4)); }
"""
SUBSTITUTION_SOURCE = """\
template<bool Enabled> struct Result {};
template<> struct Result<true> { typedef int type; };
struct HasValue { enum { allowed = 1 }; };
struct LacksValue { enum { allowed = 0 }; };
template<class T> typename Result<T::allowed != 0>::type select(T, int) { return 11; }
template<class T> int select(T, ...) { return 17; }
int enabled() { return select(HasValue(), 0); }
int disabled() { return select(LacksValue(), 0); }
"""


def run(*args, cwd, timeout=30):
    return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT, timeout=timeout)


def check_ipa(compiler, objcopy, nm, work):
    objects = {}
    for name, content, exceptions in (
        ("ipa.c", IPA_SOURCE, "off"),
        ("ipa_assembly.c", IPA_ASSEMBLY_SOURCE, "off"),
        ("ipa_cpp.cpp", IPA_CPP_SOURCE, "off"),
        ("ipa_exceptions.cpp", IPA_EXCEPTION_SOURCE, "on"),
        ("ipa_friend.cpp", TEMPLATE_FRIEND_SOURCE, "off"),
        ("ipa_substitution.cpp", SUBSTITUTION_SOURCE, "off"),
    ):
        source = work / name
        source.write_text(content, encoding="ascii")
        obj = source.with_suffix(".o")
        run(
            compiler,
            *FLAGS,
            "-O4,p",
            "-inline",
            "auto",
            "-Cpp_exceptions",
            exceptions,
            "-func_align",
            "4",
            "-ipa",
            "file",
            "-c",
            source,
            "-o",
            obj,
            cwd=work,
        )
        objects[name] = obj

    obj = objects["ipa.c"]
    names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines() if line}
    live = {"forward", "entry", "address_leaf", "address_taken", "data_used", "accessor"}
    dead = {"unused", "leaf", "chained", "data_dead"}
    if live - names or dead & names:
        raise ValueError(f"{compiler}: unexpected file IPA symbols: missing {live - names}, retained {dead & names}")
    section = work / "ipa.text"
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    code = section.read_bytes()
    if len(code) != 32:
        raise ValueError(f"{compiler}: unexpected file IPA instructions\n{code.hex(' ', 4)}")

    per_function = []
    for mode in ("off", "function"):
        obj = work / f"ipa_{mode}.o"
        run(compiler, *FLAGS, "-O4,p", "-ipa", "file", "-ipa", mode, "-c", work / "ipa.c", "-o", obj, cwd=work)
        per_function.append(obj.read_bytes())
    if per_function[0] != per_function[1]:
        raise ValueError(f"{compiler}: -ipa function differs from -ipa off")

    obj = objects["ipa_assembly.c"]
    names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines() if line}
    if not {"invoke", "target"} <= names:
        raise ValueError(f"{compiler}: file IPA discarded a function referenced by assembly")

    obj = objects["ipa_cpp.cpp"]
    names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines() if line}
    live = {
        "__vt__4Left",
        "__vt__5Right",
        "__vt__6Widget",
        "left__4LeftFv",
        "right__5RightFv",
        "right__6WidgetFv",
        "@4@right__6WidgetFv",
        "use_template__Fi",
    }
    has_initializer = any(name.startswith("__sinit_") and name.endswith("ipa_cpp_cpp") for name in names)
    if live - names or not has_initializer or any(name.startswith(("dead_value", "add2<")) for name in names):
        raise ValueError(f"{compiler}: unexpected C++ file IPA definitions\n{sorted(names)}")
    undefined = run(nm, "--undefined-only", obj, cwd=work).split()
    if undefined != ["U", "supplied__Fv"]:
        raise ValueError(f"{compiler}: unexpected C++ file IPA references\n{undefined}")

    obj = objects["ipa_exceptions.cpp"]
    undefined = {line.split()[-1] for line in run(nm, "--undefined-only", obj, cwd=work).splitlines() if line}
    if undefined != {"observe__Fi", "__throw", "__dl__FPv"}:
        raise ValueError(f"{compiler}: unexpected exception references under file IPA\n{sorted(undefined)}")

    obj = objects["ipa_substitution.cpp"]
    names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines() if line}
    if "select<10LacksValue>__F10LacksValuee_i" not in names:
        raise ValueError(f"{compiler}: missing variadic overload with RC1 template mangling")

    section = work / "ipa_friend.text"
    run(objcopy, f"--dump-section=.text={section}", objects["ipa_friend.cpp"], cwd=work)
    if section.read_bytes() != bytes.fromhex("38600004 4e800020"):
        raise ValueError(f"{compiler}: unexpected instructions for template-friend access")
    return code


def check_source_encoding(compiler, objcopy, work):
    source = work / "encoding.c"
    source.write_bytes(b'const char narrow[] = "\x83\x5c";\nunsigned short wide[] = L"\x83\x5c";\n')
    obj = source.with_suffix(".o")
    run(
        compiler, *FLAGS, "-encoding", "SJIS", "-inline", "noauto,nobottomup,level=8", "-c", source, "-o", obj, cwd=work
    )
    for name, expected in ((".sdata", b"\x30\xbd\x00\x00"), (".sdata2", b"\x83\x5c\x00")):
        section = source.with_suffix(name)
        run(objcopy, f"--dump-section={name}={section}", obj, cwd=work)
        if section.read_bytes() != expected:
            raise ValueError(f"{compiler}: unexpected Shift-JIS literal data in {name}")


def check_pch(compiler, objcopy, work):
    header = work / "prefix.h"
    header.write_text(
        '#pragma section code_type ".pch_decl"\n'
        'extern "C" int retained(int value);\n'
        '#pragma section code_type ".pch_code"\n'
        '#pragma section data_type ".pch_data" ".pch_bss"\n'
        "struct Point { int x; int y; };\n"
        "inline int square(int x) { return x * x; }\n"
        'extern "C" __declspec(section ".pch_decl") int explicit_section(int value);\n'
        "#define BONUS 7\n",
        encoding="ascii",
    )
    source = work / "pch.cpp"
    source.write_text(
        'extern "C" int values[16] = {2};\n'
        'extern "C" int zeros[16];\n'
        'extern "C" int retained(int value) { return value + 2; }\n'
        'extern "C" int explicit_section(int value) { return value + 3; }\n'
        'extern "C" int entry(Point p) { return square(values[p.x]) + retained(zeros[p.y]) + BONUS; }\n'
        "int zeros[16];\n",
        encoding="ascii",
    )
    prefix = work / "prefix.mch"
    obj = work / "pch.o"
    flags = (*FLAGS, "-O2", "-lang=c++", "-inline", "on", "-func_align", "4")

    def sections(include):
        run(compiler, *flags, "-prefix", include, "-c", source, "-o", obj, cwd=work)
        files = {name: work / name for name in (".pch_decl", ".pch_code", ".pch_data")}
        run(objcopy, *(f"--dump-section={name}={file}" for name, file in files.items()), obj, cwd=work)
        if b"\0.pch_bss\0" not in obj.read_bytes():
            raise ValueError(f"{compiler}: PCH fixture is missing .pch_bss")
        return tuple(file.read_bytes() for file in files.values())

    expected = sections(header)
    run(compiler, *flags, "-precompile", prefix, header, cwd=work)

    # Compile with only the precompiled header available.
    header.unlink()
    if not all(expected) or sections(prefix) != expected:
        raise ValueError(f"{compiler}: PCH and text header produced different section contents")


def check_stock_pch_macros(compiler, reference, work):
    (work / "pch-once.h").write_text(
        "#pragma once\n"
        "#ifdef PCH_ONCE_SEEN\n"
        "#error a once-only header was reopened\n"
        "#endif\n"
        "#define PCH_ONCE_SEEN 1\n",
        encoding="ascii",
    )
    other_dir = work / "other"
    other_dir.mkdir()
    (other_dir / "pch-once.h").write_text("#define PCH_OTHER_SEEN 1\n", encoding="ascii")
    (work / "pch-import.h").write_text(
        "#ifdef PCH_IMPORT_SEEN\n"
        "#error an imported header was reopened\n"
        "#endif\n"
        "#define PCH_IMPORT_SEEN 1\n",
        encoding="ascii",
    )
    (work / "pch-plain.h").write_text(
        "#ifdef PCH_PLAIN_SEEN\n"
        "#define PCH_PLAIN_REOPENED 1\n"
        "#endif\n"
        "#define PCH_PLAIN_SEEN 1\n",
        encoding="ascii",
    )
    guarded_header = work / "pch-guard.h"
    guarded_header.write_text(
        "#ifndef PCH_GUARD\n"
        "#define PCH_GUARD\n"
        "#define PCH_ORIGINAL_GUARD_SEEN 1\n"
        "#endif\n",
        encoding="ascii",
    )
    header = work / "stock-macros.h"
    header.write_text(
        '#include "pch-once.h"\n'
        '#import "pch-import.h"\n'
        '#include "pch-plain.h"\n'
        '#include "pch-guard.h"\n'
        "#define EMPTY\n"
        "#define ZERO() 42\n"
        "#define JOIN(a, b) a ## b\n"
        "#define STR(value) #value\n"
        "#define CALL(f, ...) f(0, ## __VA_ARGS__)\n"
        "#define SHIFT(a, b) ((a) << (b))\n"
        "#define NUMBER 0x12UL\n"
        '#define WIDE L"\\x0100" L"A"\n'
        '#define BYTES "\\0007"\n'
        "struct StockPair { int x; int y; };\n",
        encoding="ascii",
    )
    source = work / "stock-macros.cpp"
    source.write_text(
        '#include "pch-once.h"\n'
        '#include "other/pch-once.h"\n'
        '#include "pch-import.h"\n'
        '#include "pch-plain.h"\n'
        '#include "pch-guard.h"\n'
        "#if defined(PCH_OTHER_SEEN) || !defined(PCH_PLAIN_REOPENED) || defined(PCH_GUARDED_REOPENED)\n"
        "#error stock PCH include state was not restored\n"
        "#endif\n"
        '#include <other/pch-once.h>\n'
        "#ifndef PCH_OTHER_SEEN\n"
        "#error a system header was skipped because of a quoted include\n"
        "#endif\n"
        "#undef PCH_GUARD\n"
        '#include "pch-guard.h"\n'
        "#ifndef PCH_GUARDED_REOPENED\n"
        "#error a header stayed guarded after its guard was undefined\n"
        "#endif\n"
        "#if !defined(EMPTY) || ZERO() != 42\n"
        "#error macros from the stock PCH are missing\n"
        "#endif\n"
        "static int combine(int x, int y = 3) { return x + 5 * y; }\n"
        'extern "C" int JOIN(macro_,entry)(StockPair p) {\n'
        "    return SHIFT(p.x, 2) + p.y + CALL(combine) + CALL(combine, NUMBER);\n"
        "}\n"
        'extern "C" const char msg[] = STR(two words);\n'
        'extern "C" const char bytes[] = BYTES;\n'
        'extern "C" const wchar_t wide[] = WIDE;\n',
        encoding="ascii",
    )
    prefix = work / "stock-macros.mch"
    replay = work / "macro-replay.cpp"
    obj = work / "macro-replay.o"
    flags = (*FLAGS, "-lang=c++", "-O2", "-i", work)
    run(reference, *flags, "-gcc_extensions", "on", "-precompile", prefix, header, cwd=work)
    header.unlink()
    guarded_header.write_text("#define PCH_GUARDED_REOPENED 1\n", encoding="ascii")

    def replay_object(preprocessor):
        output = run(preprocessor, *flags, "-E", "-prefix", prefix.name, source, cwd=work)
        if "#   Error:" in output:
            raise ValueError(f"{preprocessor}: preprocessing failed\n{output}")
        if f'#include "{prefix.name}"' not in output:
            raise ValueError(f"{preprocessor}: preprocessed source lost its stock PCH include")
        replay.write_text(output, encoding="ascii")
        run(reference, *flags, "-c", replay, "-o", obj, cwd=work)
        return obj.read_bytes()

    if replay_object(compiler) != replay_object(reference):
        raise ValueError(f"{compiler}: stock PCH macro expansions changed the replayed object")


def check_pch_include_state(compiler, reference, objcopy, work, *, timeout=30):
    compiler = compiler if isinstance(compiler, tuple) else (compiler,)
    directory = work / "pch-include-state"
    alternate = directory / "other"
    alternate.mkdir(parents=True)
    inner = directory / "inner.h"
    outer = directory / "outer.h"
    body = directory / "body.cpp"
    routes = directory / "routes.cpp"
    repeated = directory / "repeated.cpp"
    prefix = directory / "prefix.mch"
    obj = directory / "body.o"
    section = directory / "body.text"
    flags = (*FLAGS, "-lang=c++", "-O2", "-i", ".")

    (alternate / "inner.h").write_text("different_header\n", encoding="ascii")
    routes.write_text(
        '#include "inner.h"\n'
        '#include "./inner.h"\n'
        '#include "other/inner.h"\n'
        '#include <other/inner.h>\n'
        '#include "other/inner.h"\n'
        "int route() { return PCH_VALUE; }\n",
        encoding="ascii",
    )
    body.write_text(
        '#include "./inner.h"\nint entry() { return PCH_VALUE; }\n', encoding="ascii"
    )
    repeated.write_text(
        '#import "prefix.mch"\n#include "./prefix.mch"\nPCH_VALUE\n', encoding="ascii"
    )
    for prologue, directive in (
        ("#pragma once\n", '#include "inner.h"\n'),
        ("", '#import "inner.h"\n'),
        ("", '#pragma once on\n#include "inner.h"\n'),
    ):
        outer.write_text(directive, encoding="ascii")
        results = []
        for tool in ((reference,), compiler):
            inner.write_text(prologue + "#define PCH_VALUE 23\n", encoding="ascii")
            run(*tool, *flags, "-precompile", prefix, outer, cwd=directory, timeout=timeout)
            inner.write_text("#error precompiled once-only file was reopened\n", encoding="ascii")
            preprocessed = run(
                *tool, *flags, "-E", "-prefix", prefix.name, routes.name, cwd=directory,
                timeout=timeout
            )
            imported = run(*tool, *flags, "-E", repeated.name, cwd=directory, timeout=timeout)
            run(*tool, *flags, "-prefix", prefix.name, "-MMD", "-c", body.name, "-o", obj.name,
                cwd=directory, timeout=timeout)
            run(objcopy, f"--dump-section=.text={section}", obj, cwd=directory)
            dependency = obj.with_suffix(".d").read_bytes()
            if b"inner.h" not in dependency:
                raise ValueError(f"{tool}: a skipped PCH include is missing from its dependencies")
            results.append((preprocessed, imported, section.read_bytes(), dependency))
            inner.unlink()
            missing = subprocess.run(
                (*tool, *flags, "-msgstyle", "parseable", "-prefix", prefix.name,
                 "-c", body.name, "-o", obj.name),
                cwd=directory, text=True, capture_output=True, timeout=timeout,
            )
            if missing.returncode != 1 or "(10151)" not in missing.stdout:
                raise ValueError(f"{tool}: a missing once-only header did not report error 10151")
        if results[0] != results[1]:
            raise ValueError(f"{compiler}: once-only PCH include replay differs from stock")

    outer.write_text(
        '#define CHECK_PCH(n) ((n) * 2 == 0)\n#include "inner.h"\n', encoding="ascii"
    )
    for start, end, expected in (
        ("#ifndef GUARD\n", "#endif\n", "23"),
        ("#if !defined(GUARD)\n", "#endif\n", "23"),
        ("#if 0\n#elif !defined(GUARD)\n", "#endif\n", "23"),
        ("#ifdef ABSENT\n#else\n#ifndef GUARD\n", "#endif\n#endif\n", "23"),
        ("#if CHECK_PCH(GUARD) && 1 ? 2 * 3 == 6 : 0\n", "#endif\n", "23"),
        ("#if !GUARD || CHECK_PCH\n", "#endif\n", "23"),
        ("#if !GUARD || (-1 < 'AB') == 0\n", "#endif\n", "23"),
        ("#if 0\n#elif !GUARD /* multiline\ncomment */ && \\\n !defined(ABSENT)\n",
         "#endif\n", "23"),
        ("#if !GUARD || \\\n __LINE__ == 2\n", "#endif\n", "PCH_VALUE"),
    ):
        inner.write_text(
            start + "#define GUARD 1\n#define PCH_VALUE 23\n" + end,
            encoding="ascii",
        )
        results = []
        for builder, readers in (((reference,), ((reference,), compiler)), (compiler, (compiler,))):
            run(*builder, *flags, "-precompile", prefix, outer, cwd=directory, timeout=timeout)
            for tool in readers:
                outputs = []
                for first in (
                    '#import "inner.h"\n',
                    '#pragma once on\n#include "inner.h"\n#pragma once off\n',
                ):
                    routes.write_text(
                        first + '#undef GUARD\n#undef PCH_VALUE\n#include "./inner.h"\n'
                        "PCH_VALUE __LINE__\n",
                        encoding="ascii",
                    )
                    output = run(
                        *tool, *flags, "-E", "-prefix", prefix.name, routes.name, cwd=directory,
                        timeout=timeout
                    )
                    if output.splitlines()[-1].split()[0] != expected:
                        raise ValueError(f"{tool}: guarded PCH include replay differs for {start!r}")
                    outputs.append(output)
                results.append(outputs)
        if any(output != results[0] for output in results[1:]):
            raise ValueError(f"{compiler}: guarded PCH include output differs for {start!r}")

    inner.write_text("#if !GUARD\n#define GUARD 1\n#endif\n", encoding="ascii")
    routes.write_text(
        '#undef GUARD\n#define GUARD 1/0\n#import "inner.h"\n'
        '#undef GUARD\n#include "inner.h"\nGUARD\n',
        encoding="ascii",
    )
    outputs = []
    for tool in ((reference,), compiler):
        run(*tool, *flags, "-precompile", prefix, outer, cwd=directory, timeout=timeout)
        outputs.append(
            run(*tool, *flags, "-msgstyle", "parseable", "-E", "-prefix", prefix.name,
                routes.name, cwd=directory, timeout=timeout)
        )
    if outputs[0] != outputs[1] or "(10139) division by 0" not in outputs[1]:
        raise ValueError(f"{compiler}: saved PCH expression warning differs from stock")

    inner.write_text("ordinary_header\n", encoding="ascii")
    routes.write_text(
        '#include "inner.h"\n'
        '#include "./inner.h"\n'
        '#import "inner.h"\n'
        '#include "./inner.h"\n'
        '#pragma once on\n'
        '#include "other/inner.h"\n'
        '#pragma once off\n'
        '#include <other/inner.h>\n',
        encoding="ascii",
    )
    if run(*compiler, *flags, "-E", routes.name, cwd=directory, timeout=timeout) != run(
        reference, *flags, "-E", routes.name, cwd=directory
    ):
        raise ValueError(f"{compiler}: live import or once-only include behavior differs from stock")


def check_stock_pch_declarations(compiler, reference, objcopy, nm, work):
    cases = (
        (
            "externs", "c", ".text",
            "typedef const unsigned long word;\n"
            "extern word flag;\n"
            "extern word fetch(register int id, char label[3]);\n"
            "extern char* text;\n"
            "extern const int array[13];\n"
            "extern short consume(word value, const char* message, ...);\n"
            "extern void noargs(void);\n"
            "extern int anyargs();\n"
            "extern int (*callback)(float);\n",
            "word* flagp(void) { return &flag; }\n"
            "int f(void) {\n"
            "    noargs();\n"
            "    return sizeof(array) + consume(flag, text, anyargs(4)) + array[3]\n"
            "        + callback(1.5f) + fetch(2, text);\n"
            "}\n",
            {"anyargs", "array", "callback", "consume", "fetch", "flag", "noargs", "text"},
        ),
        (
            "overload", "c++", ".text",
            'extern "C" int consume(const int*, double&);\n'
            "extern long convert(int a) throw();\n"
            "extern long convert(const char* a) throw(int, double);\n"
            "extern int global;\n"
            'extern "C" int c_global;\n',
            "long convert(int a) throw();\n"
            "long f(double& d) { return consume(&global, d) + c_global + convert(3) + convert(\"abc\"); }\n",
            {"c_global", "consume", "convert__FPCc", "convert__Fi", "global"},
        ),
        (
            "section", "c", ".pch_code",
            '#pragma section code_type ".pch_code"\n'
            '__declspec(section ".pch_code") int retained(int a);\n'
            "extern int fin(void);\n",
            "int f(int a) { return retained(a) + fin(); }\n",
            {"fin", "retained"},
        ),
        (
            "aggregates", "c", ".text",
            "struct Opaque;\n"
            "extern struct Opaque* opaque;\n"
            "union Multi { char c; short s[2]; long n; };\n"
            "struct Bits { unsigned a:3; signed b:7; unsigned :0; unsigned char c:2; unsigned d:9; };\n"
            "typedef struct Combined Combined;\n"
            "struct Combined { union Multi word; struct Bits bits; Combined* child; };\n"
            "extern Combined* root;\n",
            "struct Opaque { int n; };\n"
            "extern struct Combined* root;\n"
            "int extract(void) {\n"
            "    return opaque->n + root->word.s[1] + root->bits.a + root->child->bits.b\n"
            "        + root->bits.c + root->child->bits.d;\n"
            "}\n"
            "void store(struct Bits* p, unsigned x) { p->c = x; p->d = x; }\n"
            "unsigned size(void) { return sizeof(Combined) + 10 * sizeof(struct Bits); }\n",
            {"opaque", "root"},
        ),
        (
            "recursive-callback", "c", ".text",
            "struct Link;\n"
            "typedef int (*LinkCallback)(struct Link* self);\n"
            "struct Link { LinkCallback run; struct Link* next; };\n"
            "extern LinkCallback find(void);\n",
            "int invoke(struct Link* p) { LinkCallback cb = p->run; return cb(p->next); }\n",
            set(),
        ),
        (
            "enum", "c", ".text",
            "typedef enum { Low = -7, High = 65536 } Number;\n"
            "enum State { STATE_READY = 0 };\n"
            "extern Number next(Number);\n",
            "Number get(Number a) { return (Number)(next(a) == Low ? High : STATE_READY); }\n"
            "int s(void) { return sizeof(Number) + sizeof(enum State) * 10; }\n",
            {"next"},
        ),
        (
            "cpp-enum", "c++", ".text",
            "enum State { Ready = 1 };\n"
            "extern State value(State);\n",
            "State copy(State r) { return r == Ready ? value(r) : Ready; }\n",
            {"value__F5State"},
        ),
        (
            "wide-enum", "c", ".sdata",
            "#pragma longlong_enums on\n"
            "#pragma enumsalwaysint off\n"
            "enum Wide { BIG = 0x100000005LL, NEG = -0x100000007LL };\n",
            "typedef char size_check[sizeof(enum Wide) == 8 ? 1 : -1];\n"
            "long long a = BIG, b = NEG;\n",
            set(),
        ),
        (
            "class-scope", "c++", ".text",
            "namespace util {\n"
            "    enum Flag {ready = 3};\n"
            "    struct Outer {\n"
            "        struct Inner { int x; };\n"
            "        static int call(Flag);\n"
            "        float sample(int) const;\n"
            "    private: int hidden;\n"
            "    };\n"
            "    extern Outer::Inner entry(Flag);\n"
            "}\n"
            "namespace alias = util;\n"
            "namespace imported { using namespace util; }\n"
            "using namespace imported;\n"
            "using util::entry;\n",
            "int direct(Outer* p) {\n"
            "    return p->sample(3) + alias::Outer::call(util::ready) + entry(util::ready).x;\n"
            "}\n",
            {"call__Q24util5OuterFQ24util4Flag", "entry__4utilFQ24util4Flag", "sample__Q24util5OuterCFi"},
        ),
        (
            "class-bases", "c++", ".text",
            "class Base { public: int a; };\n"
            "struct Spill { int b; };\n"
            "class Child : protected Base, private Spill {\n"
            "public: static int sample(short); void call(int); int word;\n"
            "};\n",
            "void Child::call(int n) { a = n; b = sample(n); word = a + b; }\n",
            {"sample__5ChildFs"},
        ),
        (
            "class-virtual", "c++", ".text",
            "struct VBase { virtual int test(int); char v; };\n"
            "struct Derived : virtual VBase { char padding; };\n",
            "int VBase::test(int n) { return n + v; }\n"
            "int dispatch(Derived* p) { return p->test(5) + p->v + p->padding; }\n"
            "VBase* up(Derived* p) { return p; }\n"
            "int run(void) { Derived d; d.v = 3; return d.test(4); }\n",
            set(),
        ),
        (
            "class-using", "c++", ".text",
            "struct Base { short a; int call(int) const; };\n"
            "class Child : private Base { public: using Base::a; using Base::call; };\n",
            "int call(const Child* p) { return p->a + p->call(13); }\n",
            {"call__4BaseCFi"},
        ),
        (
            "class-member-pointers", "c++", ".text",
            "struct Outer { short a; int sample(int) const; };\n"
            "typedef short Outer::* Member;\n"
            "typedef int (Outer::*Method)(int) const;\n"
            "extern Member slot;\n"
            "extern Method callback;\n",
            "int direct(const Outer* p) { return p->*slot + (p->*callback)(7); }\n",
            {"__ptmf_scall", "callback", "slot"},
        ),
        (
            "defaults-scalar", "c++", (".text", ".sdata", ".sdata2"),
            "extern int external;\n"
            "extern int producer(int);\n"
            "extern void integral(long long a = 0x100000005LL);\n"
            "extern void real(double n = 1.25);\n"
            "extern void pointed(int* p = &external);\n"
            'extern void stringed(const char* s = "abc");\n'
            "extern void invoked(int n = producer(5));\n"
            "extern void referenced(int n = external);\n",
            "void run(void) { integral(); real(); pointed(); stringed(); invoked(); referenced(); }\n",
            {
                "external", "integral__Fx", "invoked__Fi", "pointed__FPi", "producer__Fi",
                "real__Fd", "referenced__Fi", "stringed__FPCc",
            },
        ),
        (
            "defaults-branch", "c++", (".text", ".sdata"),
            "extern int first, second;\n"
            "extern int get(int, int);\n"
            "extern void composite(int n = first ? get(first + 3, -second) : (short)second);\n"
            'extern void wide(const wchar_t* p = L"AB");\n',
            "void run(void) { composite(); wide(); }\n",
            {"composite__Fi", "first", "get__Fii", "second", "wide__FPCw"},
        ),
        (
            "defaults-constructor", "c++", ".text",
            "struct Box { Box(int); ~Box(); int x; };\n"
            "extern void boxed(Box b = Box(5));\n"
            "struct Point { explicit Point(int); int x; };\n"
            "struct Consumer { int go(const Point& p = Point(9)); };\n",
            "void run(Consumer* c) { boxed(); c->go(); }\n",
            {"__ct__3BoxFi", "__ct__5PointFi", "__dt__3BoxFv", "boxed__F3Box", "go__8ConsumerFRC5Point"},
        ),
        (
            "defaults-new", "c++", ".text",
            "struct Sample { Sample(int); int x; };\n"
            "extern void use(Sample* ptr = new Sample(4));\n"
            "extern void use_array(int* ptr = new int[3]);\n",
            "void run() { use(); use_array(); }\n",
            {"__ct__6SampleFi", "__nw__FUl", "__nwa__FUl", "use__FP6Sample", "use_array__FPi"},
        ),
        (
            "defaults-member-pointer", "c++", (".text", ".data"),
            "struct Sample { short datum; int invoke(int); };\n"
            "extern void call(int (Sample::*p)(int) = &Sample::invoke);\n"
            "extern void fetch(short Sample::*p = &Sample::datum);\n",
            "void run() { call(); fetch(); }\n",
            {"call__FM6SampleFPCvPvi_i", "fetch__FM6Samples", "invoke__6SampleFi"},
        ),
        (
            "friends", "c++", ".text",
            "class Secret;\n"
            "struct Member { static int access(const Secret&); };\n"
            "class Reader;\n"
            "int helper(const Secret&);\n"
            "class Secret {\n"
            "    int value;\n"
            "    friend class Reader;\n"
            "    friend int helper(const Secret&);\n"
            "    friend int Member::access(const Secret&);\n"
            "};\n",
            "class Reader { public: static int inspect(const Secret& x) { return x.value; } };\n"
            "int helper(const Secret& x) { return x.value + 1; }\n"
            "int Member::access(const Secret& x) { return x.value + 2; }\n"
            "int run(const Secret& x) { return Reader::inspect(x) + helper(x) + Member::access(x); }\n",
            set(),
        ),
        (
            "saved-inline-data", "c++", (".text", ".sdata2"),
            "static const int negative = -47;\n"
            "static const unsigned long long wide = 0xffeeddcc11223344ull;\n"
            "static const float single = 1.25f;\n"
            "static const double real = -9.5;\n"
            "enum Choice { selected = 7 };\n"
            "static const Choice enumerated = selected;\n"
            "static int* const nullpointer = 0;\n",
            "int scalar() { return negative + (int)(wide >> 32) + enumerated; }\n"
            "const int* negative_address() { return &negative; }\n"
            "const unsigned long long* wide_address() { return &wide; }\n"
            "const float* single_address() { return &single; }\n"
            "const double* real_address() { return &real; }\n"
            "const Choice* choice_address() { return &enumerated; }\n"
            "int* const* pointer_address() { return &nullpointer; }\n",
            set(),
        ),
    )

    for name, lang, sections, declarations, consumer, expected_names in cases:
        header = work / f"stock-{name}.h"
        prefix = header.with_suffix(".mch")
        source = work / f"stock-{name}-use.cpp"
        obj = source.with_suffix(".o")
        if isinstance(sections, str):
            sections = (sections,)
        dumps = {section: source.with_suffix(f"{section}.bin") for section in sections}
        header.write_text(declarations, encoding="ascii")
        source.write_text(consumer, encoding="ascii")
        flags = (*FLAGS, f"-lang={lang}", "-O2")

        def compile_with(tool, include):
            obj.unlink(missing_ok=True)
            for path in dumps.values():
                path.unlink(missing_ok=True)
            run(tool, *flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
            run(objcopy, *(f"--dump-section={section}={path}" for section, path in dumps.items()), obj, cwd=work)
            names = {line.split()[-1] for line in run(nm, "-u", obj, cwd=work).splitlines() if line.strip()}
            if names != expected_names:
                raise ValueError(f"{tool}: {include.name} produced unexpected symbols {sorted(names)}")
            return {section: path.read_bytes() for section, path in dumps.items()}

        expected = compile_with(compiler, header)
        run(reference, *flags, "-precompile", prefix, header, cwd=work)
        header.unlink()
        stock_code = compile_with(reference, prefix)
        actual = compile_with(compiler, prefix)
        if not all(expected.values()) or actual != expected or (
            name in {"section", "enum", "wide-enum"} and actual != stock_code
        ):
            raise ValueError(f"{compiler}: {prefix.name} changed the compiled code")


def check_stock_pch_static_data(compiler, reference, objcopy, nm, objdump, work):
    header = work / "stock-static.h"
    prefix = header.with_suffix(".mch")
    source = work / "stock-static-use.cpp"
    obj = source.with_suffix(".o")
    dump = obj.with_suffix(".bin")
    flags = (*FLAGS, "-lang=c++", "-O2")
    header.write_text(
        "extern int external[10];\n"
        "extern int calculate(int);\n"
        "static int counter;\n"
        "static int seeded = 47;\n"
        "static int* link = &external[3];\n"
        "static int (*callback)(int) = calculate;\n"
        'static const char message[] = "hello";\n'
        "static const int readonly_values[] = { 14, 23 };\n"
        'static const char* string_link = "pooled sample";\n',
        encoding="ascii",
    )
    source.write_text(
        "int run(int i) {\n"
        "    return ++counter + ++seeded + *link + callback(i) + message[i] +\n"
        "        readonly_values[i] + string_link[i];\n"
        "}\n",
        encoding="ascii",
    )

    def compile_with(tool, include, *extra_flags):
        run(tool, *flags, *extra_flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
        names = {line.split()[-1] for line in run(nm, "-u", obj, cwd=work).splitlines() if line.strip()}
        if names != {"calculate__Fi", "external"}:
            raise ValueError(f"{tool}: {include.name} produced unexpected symbols {sorted(names)}")
        symbols = {tuple(line.split()[-2:]) for line in run(nm, "--defined-only", obj, cwd=work).splitlines()}
        if ("b", "counter") not in symbols:
            raise ValueError(f"{tool}: {include.name} did not allocate the saved BSS object")
        relocations = {tuple(line.split()[1:]) for line in run(objdump, "-r", obj, cwd=work).splitlines()}
        if {("R_PPC_ADDR32", "external+0xc"), ("R_PPC_ADDR32", "calculate__Fi")} - relocations:
            raise ValueError(f"{tool}: {include.name} lost a saved initializer relocation")

    def sections():
        result = {}
        for section in (".text", ".data", ".sdata", ".sdata2"):
            run(objcopy, f"--dump-section={section}={dump}", obj, cwd=work)
            result[section] = dump.read_bytes()
        return result

    compile_with(compiler, header)
    expected = sections()
    run(reference, *flags, "-precompile", prefix, header, cwd=work)
    header.unlink()
    # Stock's file IPA path emits the saved definitions.
    compile_with(reference, prefix, "-ipa", "file")
    for extra_flags in ((), ("-ipa", "file")):
        compile_with(compiler, prefix, *extra_flags)
        if sections() != expected:
            raise ValueError(f"{compiler}: {prefix.name} changed the saved static data or code")


def check_stock_pch_functions(compiler, reference, objcopy, nm, work):
    cases = (
        (
            "member", ("on", "off"), set(), {"combine__6MethodCFi", "read__6MethodCFv"},
            "struct Method {\n"
            "    int x;\n"
            "    int read() const { return x; }\n"
            "    int combine(int scale) const { return read() * scale; }\n"
            "};\n",
            "int use(const Method& x, int scale) { return x.combine(scale); }\n",
        ),
        (
            "switch", ("off",), {"side__Fi"}, {"dispatch__Fi"},
            "extern int side(int);\n"
            "inline int dispatch(int x) {\n"
            "    switch (x) {\n"
            "        case -1: return side(x);\n"
            "        case 3: return x + side(x + 2);\n"
            "        case 11: return 53;\n"
            "        default: return x - 11;\n"
            "    }\n"
            "}\n",
            "int run(int x) { return dispatch(x); }\n"
            "int (*address())(int) { return &dispatch; }\n",
        ),
        (
            "exceptions", ("off",), {"__ct__10ScopeGuardFi", "__dt__10ScopeGuardFv", "may_throw__Fi"},
            {"with_catch__Fi"},
            "struct ScopeGuard { ScopeGuard(int); ~ScopeGuard(); };\n"
            "extern int may_throw(int);\n"
            "inline int with_catch(int x) {\n"
            "    ScopeGuard guard(x);\n"
            "    try { return may_throw(x); }\n"
            "    catch (int failure) { return failure + 2; }\n"
            "}\n",
            "int run(int x) { return with_catch(x); }\n",
        ),
        (
            "recursion", ("off",), {"upstream__Fi"}, {"odd__Fi", "even__Fi"},
            "extern int upstream(int);\n"
            "inline int even(int);\n"
            "inline int odd(int x) { return x > 0 && even(x - 1); }\n"
            "inline int even(int x) { return x == 0 || odd(x - 1); }\n",
            "int run(int x) { return even(x) + upstream(x); }\n",
        ),
        (
            "strings", ("off",), {"observe__FPCc"},
            {"strings__Fv", "@STRING@strings__Fv", "@STRING@strings__Fv@0"},
            "extern void observe(const char*);\n"
            "inline void unused() { observe(\"unused\"); }\n"
            "inline void strings() { observe(\"first\"); observe(\"second\"); }\n",
            "void (*anchor())() { return &strings; }",
        ),
    )
    flags = (*FLAGS, "-lang=c++", "-O2", "-Cpp_exceptions", "on")
    stub = work / "stock-functions-repack.h"
    stub.write_text("#define REPACKED_FUNCTION_HEADER 1\n", encoding="ascii")
    for name, modes, undefined, out_of_line, declarations, consumer in cases:
        header = work / f"stock-functions-{name}.h"
        prefix = header.with_suffix(".mch")
        source = work / f"stock-functions-{name}.cpp"
        obj, dump = source.with_suffix(".o"), source.with_suffix(".text")
        repacked = work / f"source-functions-{name}.mch"
        sections = (".text", "extab") if name == "exceptions" else (".text",)
        if name == "strings":
            sections += (".sdata",)
        source.write_text(consumer, encoding="ascii")

        def compile_with(tool, include):
            run(tool, *mode_flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
            names = {line.split()[-1] for line in run(nm, "-u", obj, cwd=work).splitlines() if line.strip()}
            if names != undefined:
                raise ValueError(f"{tool}: {include.name} produced unexpected symbols {sorted(names)}")
            names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines()}
            if mode == "off" and out_of_line - names:
                raise ValueError(f"{tool}: {include.name} lost definitions {sorted(out_of_line - names)}")
            if name == "strings" and {symbol for symbol in names if symbol.startswith("@STRING@")} != {
                "@STRING@strings__Fv", "@STRING@strings__Fv@0",
            }:
                raise ValueError(f"{tool}: {include.name} emitted an unexpected inline literal")
            saved = []
            for section in sections:
                run(objcopy, f"--dump-section={section}={dump}", obj, cwd=work)
                saved.append(dump.read_bytes())
            return saved

        for mode in modes:
            mode_flags = (*flags, "-inline", mode)
            header.write_text(declarations, encoding="ascii")
            expected = compile_with(compiler, header)
            run(reference, *mode_flags, "-precompile", prefix, header, cwd=work)
            header.unlink()
            stock_sections = compile_with(reference, prefix)
            if name == "strings" and stock_sections != expected:
                raise ValueError(f"{compiler}: {header.name} changed the inline literal layout")
            repack_mode = "off" if mode == "on" else "on"
            run(compiler, *flags, "-inline", repack_mode, "-prefix", prefix.name, "-precompile", repacked, stub, cwd=work)
            if not all(expected) or (
                compile_with(compiler, prefix) != expected or compile_with(compiler, repacked) != expected
            ):
                raise ValueError(f"{compiler}: {prefix.name} changed code with inlining {mode}")


def check_stock_pch_inline_locals(compiler, reference, objcopy, work):
    header = work / "inline_locals.h"
    header.write_text(
        "inline double abs_value(double x) { return __fabs(x); }\n"
        "inline double with_local(double x) { double y = x * x; return abs_value(y) + y; }\n"
        "inline int with_scope(int x) {\n"
        "    int result = x;\n"
        "    { volatile int value = x * 3; result += value; }\n"
        "    return result;\n"
        "}\n"
        "struct LocalPair { int first, second; };\n"
        "inline int with_aggregate(int x) { LocalPair pair = {x, x + 3}; return pair.second; }\n",
        encoding="ascii",
    )
    source = work / "inline_locals.cpp"
    source.write_text(
        "extern double nested(double);\n"
        "double first(double x) { return abs_value(nested(x)) + 1.234567; }\n"
        "double second(double x) { return with_local(nested(x)) + 7.392849; }\n"
        "int third(int x) { return with_scope(x); }\n"
        "int fourth(int x) { return with_aggregate(x); }\n",
        encoding="ascii",
    )
    prefix = header.with_suffix(".mch")
    rebuilt = work / "inline_locals_source.mch"
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    flags = (*FLAGS, "-fp_contract", "off", "-inline", "auto", "-O4,p", "-lang=c++", "-func_align", "4")
    run(reference, *flags, "-precompile", prefix, header, cwd=work)
    run(compiler, *flags, "-precompile", rebuilt, header, cwd=work)
    code = []
    for tool, pch in ((reference, prefix), (compiler, prefix), (compiler, rebuilt)):
        run(tool, *flags, "-prefix", pch, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code.append(section.read_bytes())
    if code[0] != code[1] or code[0] != code[2]:
        raise ValueError(f"{compiler}: instruction bytes differ for precompiled inline locals")


def check_stock_pch_templates(compiler, reference, objcopy, nm, work):
    cases = (
        (
            "decl", {"identity<i>__Fi_i"}, (), (),
            "template<class T> T identity(T);\n",
            "int apply(int x) { return identity(x); }\n",
        ),
        (
            "definition", set(), ("calculate<i,l,3>__FRCil_i",), (),
            "template<class T, class U, int N> T calculate(const T& a, U b) {\n"
            "    T v = a + (T)(b * N) - 4;\n"
            "    if (v >= a) return v;\n"
            "    return a;\n"
            "}\n",
            "int apply() { return calculate<int, long, 3>(6, 9L); }\n",
        ),
        (
            "overload", set(), ("adjust<i>__5OuterFPi_i", "adjust<i>__5OuterFRCi_i"), (),
            "namespace Outer {\n"
            "    template<class T> inline T adjust(T* x) { return *x + 3; }\n"
            "    template<class T> inline T adjust(const T& x) { return x + 4; }\n"
            "}\n",
            "int apply(int* x, int y) { return Outer::adjust<int>(x) + Outer::adjust(y); }\n",
        ),
        (
            "member", set(), (), (),
            "struct Multiplier {\n"
            "    int scale;\n"
            "    template<class T> T multiply(T value) const { return value * scale; }\n"
            "};\n",
            "int apply(const Multiplier& m, int x) { return m.multiply(x); }\n",
        ),
        (
            "literals", set(), ("mix<i>__Fi_i",), (".sdata", ".sdata2"),
            "template<class T> T mix(T x) {\n"
            '    const char* label = "run";\n'
            '    const wchar_t* extra = L"ab";\n'
            '    const unsigned char* pascal_string = "\\pABC";\n'
            "    const double real = 2.5;\n"
            "    T old = x++;\n"
            "    return old + (T)(real * label[0]) + (T)extra[1] + pascal_string[0];\n"
            "}\n",
            "int apply(int x) { return mix(x); }\n",
        ),
        (
            "array", set(), ("length<i,7>__FRA7_Ci_i",), (),
            "template<class T, int N> int length(const T (&values)[N]) { return N; }\n",
            "int apply(int (&values)[7]) { return length(values); }\n",
        ),
        (
            "qualified", set(), ("get_value<5Input>__Fi_i",), (),
            "template<class T> typename T::Value get_value(typename T::Value value) { return value + 5; }\n",
            "struct Input { typedef int Value; }; int apply(int x) { return get_value<Input>(x); }\n",
        ),
        (
            "template-param", set(), ("template_tag<5Local>__Fi_i",), (),
            "template<template<class T> class Container>\n"
            "int template_tag(int value) { return value + 7; }\n",
            "template<class T> class Local {};\n"
            "int apply(int value) { return template_tag<Local>(value); }\n",
        ),
        (
            "function-deferred-instance", set(), ("saved__Fi", "transform<i>__Fi_i"), (),
            "template<class T> T transform(T value) { return value * 3; }\n"
            "inline int saved(int x) { return transform(x); }\n",
            "int run(int x) { return saved(x) + transform(x + 2); }\n",
        ),
        (
            "function-packed-instances", set(), ("saved__Fi", "transform<i,3>__Fi_i", "transform<i,5>__Fi_i"), (),
            "template<class T, int N> inline T transform(T value) { return value * N; }\n"
            "inline int saved(int x) { return transform<int,3>(x) + transform<int,5>(x); }\n",
            "int run(int x) { return saved(x) + transform<int,3>(x + 2); }\n",
        ),
        (
            "function-specialization", set(), ("transform<i>__Fi_i", "transform<l>__Fl_l"), (),
            "template<class T> T transform(T value) { return value * 3; }\n"
            "template<> inline int transform<int>(int value) { return value + 2; }\n",
            "int run(int x) { return transform(x) + transform((long)x); }\n",
        ),
        (
            "function-clone", set(),
            ("get<i>__9Holder<i>CFi_i", "get<s>__9Holder<i>CFs_i", "saved__FRC9Holder<i>"), (),
            "template<class T> struct Holder {\n"
            "    T member;\n"
            "    template<class U> T get(U x) const { return member + x; }\n"
            "};\n"
            "typedef Holder<int> IntHolder;\n"
            "inline int saved(const IntHolder& h) { return h.get((short)3); }\n",
            "int run(const IntHolder& h) { return saved(h) + h.get((short)5) + h.get(6); }\n",
        ),
        (
            "class-forward", set(), (), (),
            "template<class T> struct Holder;\n",
            "template<class T> struct Holder { T member; };\n"
            "int run(Holder<int> h) { return h.member; }\n",
        ),
        (
            "class-fields", set(), (), (),
            "template<class T, int N = 2> struct Holder {\n"
            "    char pad;\n"
            "    T a[N];\n"
            "    typedef T Item;\n"
            "    char bytes[sizeof(T) + __alignof__(T)];\n"
            "    T z;\n"
            "    static const int count = N + 2;\n"
            "    enum Kind { first = N + 4, next, last = 20 };\n"
            "};\n",
            "int run(Holder<int>& h) {\n"
            "    Holder<int>::Item x = h.a[1];\n"
            "    return sizeof(h) + h.z + x + Holder<int>::count + Holder<int>::next + Holder<int>::last;\n"
            "}\n",
        ),
        (
            "class-bitfield", set(), (), (),
            "template<int N> struct Holder { unsigned field : N; unsigned trailer : 2; };\n",
            "int run(Holder<4>& h) { return h.field + h.trailer; }\n",
        ),
        (
            "class-static-member", set(), ("counter__8Outer<i>",), (".sdata",),
            "template<class T> struct Outer { static int counter; };\n"
            "template<class T> int Outer<T>::counter = sizeof(T) + 2;\n",
            "int run() { return Outer<int>::counter; }\n",
        ),
        (
            "class-methods", set(), (
                "get__9Holder<i>CFi", "outside__9Holder<i>CFi", "pick<i>__9Holder<i>CFi_i",
            ), (),
            "template<class T> struct Holder {\n"
            "    T member;\n"
            "    T get(int x) const { return member + x; }\n"
            "    T outside(int x) const;\n"
            "    template<class U> T pick(U x) const { return member - x; }\n"
            "};\n"
            "template<class T> T Holder<T>::outside(int x) const { return member * x; }\n",
            "int run(const Holder<int>& h) { return h.get(2) + h.outside(3) + h.pick(4); }\n",
        ),
        (
            "class-inheritance", set(), (), (),
            "struct Base { int n; };\n"
            "template<class T> struct Holder : Base, T { using T::k; int m; };\n",
            "struct Local { int k; };\n"
            "int run(Holder<Local>& h) { return h.n + h.k + h.m; }\n",
        ),
        (
            "class-nested", set(), (), (),
            "template<class T> struct Pack { T x; };\n"
            "template<class T> struct Holder {\n"
            "    struct Inner { T member; };\n"
            "    Pack<T> p;\n"
            "    Holder<T>* next;\n"
            "};\n",
            "int run(const Holder<int>& h) { return h.p.x + sizeof(Holder<int>::Inner) + sizeof(*h.next); }\n",
        ),
        (
            "class-template-param", set(), (), (),
            "template<template<class U> class P, class T> struct Holder { P<T> p; };\n",
            "template<class T> struct Pack { T x; };\n"
            "int run(const Holder<Pack, int>& h) { return h.p.x; }\n",
        ),
        (
            "class-partial", set(), (), (),
            "template<class T> struct Holder { T member; };\n"
            "template<class T> struct Holder<T*> { T other; };\n",
            "int run(Holder<int*>& h) { return h.other; }\n",
        ),
        (
            "class-stored", set(), ("call__FRC9Holder<i>", "get__9Holder<i>CFi"), (),
            "template<class T> struct Holder { T member; T get(int x) const { return member + x; } };\n"
            "typedef Holder<int> IntHolder;\n"
            "typedef Holder<char> CharHolder;\n"
            "inline int call(const IntHolder& h) { return h.get(1); }\n",
            "int run(const IntHolder& h, const CharHolder& c) { return call(h) + h.get(2) + c.member; }\n",
        ),
        (
            "class-clones", set(),
            ("read__FRCQ28Outer<i>8Inner<c>", "size_wide__FRCQ28Outer<x>8Inner<c>"), (),
            "template<class T> struct Outer { template<class U> struct Inner { T a; U b; }; };\n"
            "typedef Outer<int>::Inner<char> Nested;\n"
            "typedef Outer<long long>::Inner<char> Wide;\n"
            "inline int read(const Nested& value) { return value.a; }\n"
            "inline int size_wide(const Wide&) { return sizeof(Wide); }\n",
            "int run(const Nested& v, const Wide& w, const Outer<int>::Inner<short>& extra) {\n"
            "    return read(v) + size_wide(w) + v.b + sizeof(extra);\n"
            "}\n",
        ),
        (
            "class-deep-clone", set(), ("read__FRCQ38Outer<i>9Middle<s>8Inner<c>",), (),
            "template<class T> struct Outer {\n"
            "    template<class U> struct Middle {\n"
            "        template<class V> struct Inner { T a; U b; V c; };\n"
            "    };\n"
            "};\n"
            "typedef Outer<int>::Middle<short>::Inner<char> Deep;\n"
            "inline int read(const Deep& v) { return v.a - v.b; }\n",
            "int run(const Deep& v, const Outer<int>::Middle<short>::Inner<long>& extra) {\n"
            "    return read(v) + v.c + sizeof(extra);\n"
            "}\n",
        ),
    )
    flags = (*FLAGS, "-lang=c++", "-O2", "-inline", "off")
    stub = work / "stock-templates-repack.h"
    stub.write_text("#define REPACKED_TEMPLATE_HEADER 1\n", encoding="ascii")
    for name, undefined, definitions, extra_sections, declarations, consumer in cases:
        header = work / f"stock-templates-{name}.h"
        prefix = header.with_suffix(".mch")
        source = header.with_suffix(".cpp")
        obj, dump = source.with_suffix(".o"), source.with_suffix(".section")
        repacked = work / f"source-templates-{name}.mch"
        header.write_text(declarations, encoding="ascii")
        source.write_text(consumer, encoding="ascii")

        def compile_with(tool, include):
            run(tool, *flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
            unresolved = {line.split()[-1] for line in run(nm, "-u", obj, cwd=work).splitlines() if line.strip()}
            if unresolved != undefined:
                raise ValueError(f"{tool}: {include.name} produced unexpected symbols {sorted(unresolved)}")
            defined = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines() if line.strip()}
            missing = set(definitions) - defined
            if missing:
                raise ValueError(f"{tool}: {include.name} lost template definitions {sorted(missing)}")
            sections = []
            for section in (".text", *extra_sections):
                run(objcopy, f"--dump-section={section}={dump}", obj, cwd=work)
                sections.append(dump.read_bytes())
            return unresolved, sections

        expected = compile_with(compiler, header)
        run(reference, *flags, "-precompile", prefix, header, cwd=work)
        header.unlink()
        run(reference, *flags, "-prefix", prefix.name, "-c", source, "-o", obj, cwd=work)
        run(compiler, *flags, "-prefix", prefix.name, "-precompile", repacked, stub, cwd=work)
        if compile_with(compiler, prefix) != expected or compile_with(compiler, repacked) != expected:
            raise ValueError(f"{compiler}: {prefix.name} changed code or data")


def check_stock_pch_initializers(compiler, reference, objcopy, nm, objdump, work):
    flags = (*FLAGS, "-O2")
    stub = work / "initialize-repack.h"
    stub.write_text("#define REPACKED_INITIALIZERS 1\n", encoding="ascii")

    c_flags = (*flags, "-lang=c")
    header = work / "initialize-tentative.h"
    prefix = header.with_suffix(".mch")
    rebuilt = work / "initialize-tentative-rebuilt.mch"
    repacked = work / "initialize-tentative-repacked.mch"
    source = work / "initialize-tentative-use.c"
    obj = source.with_suffix(".o")
    section_file = source.with_suffix(".bin")
    header.write_text(
        "static int preserved;\n"
        "static int completed;\n"
        "static int filled[2];\n"
        "static int already;\n"
        "static int already = 7;\n"
        "int shared;\n",
        encoding="ascii",
    )
    source.write_text(
        "static int completed = 29;\n"
        "static int filled[2] = { 11, 17 };\n"
        "int inspect(int i) { return ++preserved + completed + filled[i] + already + ++shared; }\n",
        encoding="ascii",
    )

    def compile_tentative(tool, include, *extra_flags):
        run(tool, *c_flags, *extra_flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
        symbols = {tuple(line.split()[-2:]) for line in run(nm, "--defined-only", obj, cwd=work).splitlines()}
        if {("b", "preserved"), ("d", "completed"), ("d", "filled"), ("d", "already"), ("B", "shared")} - symbols:
            raise ValueError(f"{tool}: {include.name} produced unexpected tentative-definition symbols")
        sections = []
        for section in (".text", ".sdata"):
            run(objcopy, f"--dump-section={section}={section_file}", obj, cwd=work)
            sections.append(section_file.read_bytes())
        return sections

    expected = compile_tentative(compiler, header)
    run(reference, *c_flags, "-precompile", prefix, header, cwd=work)
    run(compiler, *c_flags, "-precompile", rebuilt, header, cwd=work)
    header.unlink()
    # Stock file IPA also emits the definition completed in the archived header.
    compile_tentative(reference, prefix, "-ipa", "file")
    run(compiler, *c_flags, "-prefix", prefix.name, "-precompile", repacked, stub, cwd=work)
    for include in (prefix, rebuilt, repacked):
        if compile_tentative(compiler, include) != expected:
            raise ValueError(f"{compiler}: {include.name} changed code or data for C tentative definitions")

    cpp_flags = (*flags, "-lang=c++")
    for name, declarations, consumer, startup_calls, extra_calls in (
        (
            "ordered",
            "extern int source();\n"
            "extern int adjust(int);\n"
            "static int first = source();\n"
            "static int next_value = adjust(first);\n"
            "struct Entry { Entry(int); ~Entry(); int value; };\n"
            "static Entry last(next_value);\n",
            "int read_last(void) { return last.value; }\n",
            ("source__Fv", "adjust__Fi", "__ct__5EntryFi", "__register_global_object"),
            {"__dt__5EntryFv"},
        ),
        (
            "array",
            "struct Element { Element(); ~Element(); int value; };\n"
            "static Element elements[2];\n",
            "int read_value(int i) { return elements[i].value; }\n",
            ("__construct_array", "__register_global_object"),
            {"__ct__7ElementFv", "__dt__7ElementFv", "__destroy_arr"},
        ),
    ):
        header = work / f"initialize-{name}.h"
        prefix = header.with_suffix(".mch")
        repacked = work / f"initialize-{name}-repacked.mch"
        source = work / f"initialize-{name}-use.cpp"
        obj = source.with_suffix(".o")
        header.write_text(declarations, encoding="ascii")
        source.write_text(consumer, encoding="ascii")

        def compile_dynamic(tool, include, *extra_flags):
            run(tool, *cpp_flags, *extra_flags, "-prefix", include.name, "-c", source, "-o", obj, cwd=work)
            relocations = {}
            for section in run(objdump, "-r", obj, cwd=work).split("RELOCATION RECORDS FOR [")[1:]:
                section_name, records = section.split("]:", 1)
                relocations[section_name] = [line.split() for line in records.splitlines() if line.strip()]
            branches = tuple(
                parts[2] for parts in relocations.get(".text", [])
                if len(parts) == 3 and parts[1] == "R_PPC_REL24" and parts[2] in startup_calls
            )
            if branches != startup_calls:
                raise ValueError(f"{tool}: {include.name} produced startup calls {branches}")
            names = {line.split()[-1] for line in run(nm, "-u", obj, cwd=work).splitlines() if line.strip()}
            if names != set(startup_calls) | extra_calls:
                raise ValueError(f"{tool}: {include.name} produced unexpected symbols {sorted(names)}")
            slots = relocations.get(".ctors", [])
            if (
                len(slots) != 2 or len(slots[1]) != 3 or slots[1][:2] != ["00000000", "R_PPC_ADDR32"]
                or not slots[1][2].startswith("__sinit_")
            ):
                raise ValueError(f"{tool}: {include.name} did not register its startup function")
            names = {line.split()[-1] for line in run(nm, "--defined-only", obj, cwd=work).splitlines()}
            if slots[1][2] not in names:
                raise ValueError(f"{tool}: {include.name} did not define its registered startup function")
            run(objcopy, f"--dump-section=.ctors={section_file}", obj, cwd=work)
            if section_file.read_bytes() != b"\0" * 4:
                raise ValueError(f"{tool}: {include.name} produced an unexpected .ctors slot")

        compile_dynamic(compiler, header)
        run(reference, *cpp_flags, "-precompile", prefix, header, cwd=work)
        header.unlink()
        compile_dynamic(reference, prefix, "-ipa", "file")
        run(compiler, *cpp_flags, "-prefix", prefix.name, "-precompile", repacked, stub, cwd=work)
        compile_dynamic(compiler, prefix)
        compile_dynamic(compiler, prefix, "-ipa", "file")
        compile_dynamic(compiler, repacked)


def check_spe_options(compiler, work):
    source = work / "spe_options.c"
    source.write_text(
        "spe __SPE__\nfeatures __MOT_ISEL__ __MOT_SPE__ __MOT_ZEN__\naltivec __VEC__\n",
        encoding="ascii",
    )
    for cpu, flags, spe, altivec, fallback in (
        ("e500", (), "__SPE__", "__VEC__", False),
        ("85xx", ("-spe_vector",), "1", "__VEC__", False),
        ("e500v2", ("-spe_addl_vector",), "1", "__VEC__", True),
        ("5565", ("-spe_addl_vector",), "1", "__VEC__", False),
        ("generic", ("-spe_vector", "-vector", "on"), "0", "10205", False),
        ("gekko", ("-spe_addl_vector",), "__SPE__", "__VEC__", False),
    ):
        output = run(compiler, "-nodefaults", "-proc", cpu, *flags, "-E", source, cwd=work)
        selected = [line for line in output.splitlines() if line.startswith(("spe ", "features ", "altivec "))]
        warning = "Additional SPE instructions are not supported in this processor." in output
        if selected != [f"spe {spe}", "features 1 1 1", f"altivec {altivec}"] or warning != fallback:
            raise ValueError(f"{compiler}: unexpected SPE options for {cpu} {flags}\n{output}")

    source.write_text(
        "flags __SPE__ __PPCe500__ __PPCe500v2__ __option(sfp_emulation) "
        "__option(e500_floatingpoint) __option(e500v2_floatingpoint)\n",
        encoding="ascii",
    )
    for cpu, mode, expected, automatic in (
        ("generic", "efpu", "1 1 __PPCe500v2__ 1 1 0", True),
        ("generic", "dpfp", "1 __PPCe500__ 1 1 0 1", True),
        ("gekko", "spfp", "__SPE__ __PPCe500__ __PPCe500v2__ 1 0 0", False),
    ):
        output = run(compiler, "-nodefaults", "-proc", cpu, "-fp", mode, "-E", source, cwd=work)
        selected = [line for line in output.splitlines() if line.startswith("flags ")]
        warning = "SPFP and DPFP require support from the SPE vector unit." in output
        if selected != [f"flags {expected}"] or warning != automatic:
            raise ValueError(f"{compiler}: unexpected floating point mode for {cpu} {mode}\n{output}")


def check_scalar_spe(compiler, objcopy, work):
    source = work / "scalar_spe.c"
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    results = {}
    for name, cpu, vector, mode, body, words in (
        ("SPFP arithmetic", "e500", "-spe_vector", "efpu",
         "float add(float a, float b) { return a + b; }\n"
         "float divide(float a, float b) { return a / b; }\n"
         "int less(float a, float b) { return a < b; }\n"
         "int to_int(float a) { return a; }\n",
         "106322c0 4e800020 106322c9 4e800020 100322dd 7c000026 540317be 4e800020"
         " 9421fff0 7c0802a6 90010014 10601ada 80010014 7c0803a6 38210010 4e800020"),
        ("Zen fused SPFP", "Zen", "-spe_addl_vector", "spfp",
         "float muladd(float a, float b, float c) { return a * b + c; }\n",
         "10a322c2 7ca32b78 4e800020"),
        ("DPFP load", "e500v2", "-spe_vector", "dpfp", "double read(double *p) { return *p; }\n",
         "10801b00 1064222f 4e800020"),
        ("DPFP arithmetic", "e500v2", "-spe_vector", "dpfp",
         "double add(double a, double b) { return a + b; }\n",
         "1063222d 1005322d 108302e0 1064222f 4e800020"),
        ("DPFP runtime widening", "e500v2", "-spe_vector", "dpfp",
         "double widen(float a) { return a; }\n",
         "9421fff0 7c0802a6 90010014 48000001 1083222d 1064222f 80010014 7c0803a6 38210010 4e800020"),
        ("DPFP nested arithmetic", "e500v2", "-spe_vector", "dpfp",
         "double product(double a, double b, double c, double d) { return (a + b) * (c - d); }\n",
         "1163222d 1085322d 1067422d 1009522d 106302e1 100b22e0 10801ae8 1064222f 4e800020"),
        ("DPFP live across a call", "e500v2", "-spe_vector", "dpfp",
         "extern double get(double); double pass(double a, double b) { return get(a) + b; }\n",
         "9421ffe0 7c0802a6 90010024 38000018 13e10320 1083222d 13e5322d 1064222f 48000001 "
         "1003222d 109f02e0 1064222f 38000018 13e10300 80010024 7c0803a6 38210020 4e800020"),
    ):
        source.write_text(body, encoding="ascii")
        run(compiler, "-nodefaults", "-proc", cpu, vector, "-fp", mode, "-O2", "-func_align", "4",
            "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != bytes.fromhex(words):
            raise ValueError(f"{compiler}: unexpected {name}\n{code.hex(' ', 4)}")
        results[name] = code
    return results


def check_spe_frames(compiler, objcopy, work):
    scalar = "unsigned f(unsigned a) { return step(a) + a; }\n"
    scalar_words = (
        "9421ffe0 7c0802a6 90010024 93e1001c 7c7f1b78 48000001 7c7f1a14 83e1001c "
        "80010024 7c0803a6 38210020 4e800020"
    )
    source = work / "spe_frames.c"
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    results = {}
    for name, cpu, mode, body, words in (
        ("e500 scalar frame", "e500", "efpu", scalar, scalar_words),
        ("e500v2 scalar frame", "e500v2", "soft", scalar, scalar_words),
        ("DPFP scalar frame", "e500v2", "dpfp", scalar,
         "9421ffe0 7c0802a6 90010024 38000018 13e10320 7c7f1b78 48000001 7c7f1a14 "
         "38000018 13e10300 80010024 7c0803a6 38210020 4e800020"),
        ("SPE assembly frame", "e500", "soft",
         "unsigned f(void *p, unsigned x) { asm {\n    evor r31,r4,r5\n    evstdd r31,0(r3)\n}\n"
         "return step(x) + x; }\n",
         "9421ffe0 7c0802a6 90010024 38000018 13e10320 38000010 13c10320 7c9e2378 "
         "13e42a17 13e30321 7fc3f378 48000001 7c7e1a14 38000018 13e10300 38000010 "
         "13c10300 80010024 7c0803a6 38210020 4e800020"),
    ):
        source.write_text("extern unsigned step(unsigned);\n" + body, encoding="ascii")
        flags = ["-spe_vector"] if mode != "soft" else []
        run(compiler, "-nodefaults", "-proc", cpu, "-fp", mode, *flags, "-O2", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != bytes.fromhex(words):
            raise ValueError(f"{compiler}: unexpected {name}\n{code.hex(' ', 4)}")
        results[name] = code
    return results


def check_interrupt_frames(compiler, objcopy, work):
    source = work / "interrupts.c"
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    increment = "void irq(void) { ++state; }\n"
    state = "volatile int state;\n"
    results = {}
    for name, options, body, words in (
        ("empty interrupt", FLAGS, "#pragma interrupt on\nvoid irq(void) {}\n", "4c000064"),
        ("condition register interrupt", FLAGS,
         "#pragma interrupt on\nvolatile int a,b; void irq(void) { if (a>b) a=0; }\n",
         "9421ffe0 9001000c 7c000026 90010008 90610010 80600000 80000000 7c030000 "
         "4081000c 38000000 90000000 80610010 80010008 7c0ff120 8001000c 38210020 4c000064"),
        ("SPE interrupt", ("-nodefaults", "-proc", "e500", "-fp", "efpu", "-spe_vector"),
         state + "__declspec(interrupt save_spe) " + increment,
         "9421ffe0 90010010 7c0000a6 64000200 7c000124 1000022f 90010014 10611b21 "
         "80600000 38030001 90000000 10611b01 10011301 38210020 4c000064"),
        ("DPFP calling interrupt", ("-nodefaults", "-proc", "e500v2", "-fp", "dpfp", "-spe_vector"),
         "extern void step(void);\n#pragma interrupt on\nvoid irq(void) { step(); }\n",
         "9421ff80 90010020 7c0000a6 64000200 7c000124 1000022f 90010024 7c0902a6 90010010 "
         "7c0102a6 90010014 7c000026 90010018 7c0802a6 9001001c 10612b21 10813321 10a13b21 "
         "10c14321 10e14b21 11015321 11215b21 11416321 11616b21 11817321 48000001 10612b01 "
         "10813301 10a13b01 10c14301 10e14b01 11015301 11215b01 11416301 11616b01 11817301 "
         "80010010 7c0903a6 80010014 7c0103a6 80010018 7c0ff120 8001001c 7c0803a6 10012301 "
         "38210080 4c000064"),
        ("realigned SPE interrupt", ("-nodefaults", "-proc", "e500", "-fp", "soft", "-spe_vector"),
         "extern void step(void*);\n#pragma interrupt save_spe on\n"
         "void irq(void) { __declspec(align(32)) char local[64]; step(local); }\n",
         "9421ff20 90010070 7c0000a6 64000200 7c000124 1000022f 90010074 1161bb21 "
         "542006fe 7c0000d0 396100e0 7d61016e 800bff90 90010070 800bff94 90010074 800bffd8 900100b8 "
         "800bffdc 900100bc 7c0902a6 90010060 7c0102a6 90010064 7c000026 90010068 7c0802a6 9001006c "
         "10617b21 10818321 10a18b21 10c19321 10e19b21 1101a321 1121ab21 1141b321 1181c321 "
         "38610020 48000001 81410000 10617b01 10818301 10a18b01 10c19301 10e19b01 1101a301 1121ab01 "
         "1141b301 1181c301 80010060 7c0903a6 80010064 7c0103a6 80010068 7c0ff120 8001006c 7c0803a6 "
         "10017301 1161bb01 80210000 4c000064"),
        (
            "debug interrupt", ("-nodefaults", "-proc", "Zen", "-fp", "soft", "-spe_vector"),
            "#pragma interrupt debug SRR on\n" + state + increment,
            "9421ffe0 90010018 9061001c 7c1e8aa6 90010010 7c1f8aa6 90010014 80600000 38030001 "
            "90000000 80010010 7c1e8ba6 80010014 7c1f8ba6 8061001c 80010018 38210020 4c00004e",
        ),
    ):
        source.write_text(body, encoding="ascii")
        run(compiler, *options, "-O2", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != bytes.fromhex(words):
            raise ValueError(f"{compiler}: unexpected {name}\n{code.hex(' ', 4)}")
        results[name] = code
    return results


def check_spe_assembly(compiler, objcopy, work):
    source = work / "spe.c"
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    body = """\
asm void spe(void) {
    nofralloc
    evor r3,r4,r5
    evmr r4,r3
    evsplati r6,-16
    evsplatfi r6,-16
    evmergelo r7,r8,r9
    evldd r3,8(r4)
    evldd r3,8
    evlddx r3,r4,r5
    evldw r6,248(r7)
    evldwx r6,r7,r8
    evldh r15,24(r16)
    evldhx r15,r16,r17
    evstdd r9,16(r10)
    evstddx r9,r10,r11
    evstdw r12,0(r13)
    evstdwx r12,r13,r14
    evstdh r20,40(r21)
    evstdhx r20,r21,r22
    blr
}
"""
    expected = bytes.fromhex(
        "10642a17 10831a17 10d00229 10d0022b 10e84a2d 10640b01 10600b01 10642b00"
        " 10c7fb03 10c74302 11f01b05 11f08b04 112a1321 112a5b20 118d0323"
        " 118d7322 12952b25 1295b324 4e800020"
    )
    results = {}
    for opt in ("-O0", "-O2", "-O4,p"):
        source.write_text(body, encoding="ascii")
        run(compiler, "-nodefaults", "-proc", "e500", opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != expected:
            raise ValueError(f"{compiler}: unexpected SPE instructions for {opt}\n{code.hex(' ', 4)}")
        results[f"{source.name} {opt}"] = code

    for machine, instruction, word in (
        ("e500", "evor r3,r4,r5", "10642a17"),
        ("e500v2", "efdadd r3,r5,r7", "10653ae0"),
        ("Zen", "evfsmadd r3,r5,r7", "10653a82"),
        ("5565", "evfsmadd r3,r5,r7", "10653a82"),
    ):
        source.write_text(
            f"asm void spe(void) {{\n    nofralloc\n    machine {machine}\n    {instruction}\n    blr\n}}\n",
            encoding="ascii",
        )
        run(compiler, *FLAGS, "-O2", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != bytes.fromhex(f"{word} 4e800020"):
            raise ValueError(f"{compiler}: unexpected encoding after machine {machine}\n{code.hex(' ', 4)}")
        results[f"machine {machine}"] = code

    source.write_text(
        """\
unsigned f(unsigned value) {
    asm {
        evldh r6,24(r4)
        evmr r5,r6
        evstdhx r5,r4,r7
    }
    return value + 1;
}
""",
        encoding="ascii",
    )
    run(compiler, "-nodefaults", "-proc", "e500", "-O2", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    code = section.read_bytes()
    if code != bytes.fromhex("10c41b05 10a63217 10a43b24 38630001 4e800020"):
        raise ValueError(f"{compiler}: unexpected SPE instructions in a C function\n{code.hex(' ', 4)}")
    results["SPE in a C function"] = code

    for cpu, instructions, words in (
        ("e500", (
            "evaddiw r3,r5,11", "evsubifw r3,11,r7", "evabs r3,r5", "evfscfsi r3,r5",
            "evcmpeq cr2,r5,r7", "evsel r3,r5,r7,cr2", "evnot r3,r5",
            "evlhhesplat r3,8(r5)", "evlwwsplat r3,8(r5)",
            "evmhossfa r3,r5,r7", "evmhosmi r3,r5,r7", "evmhosmia r3,r5,r7", "evmra r3,r5", "evdivwu r3,r5,r7",
        ), "106b2a02 106b3a06 10650208 10602a91 11053a34 10653a7a 10652a18 10652309 10651319"
           " 10653c27 10653c0d 10653c2d 106504c4 10653cc7"),
        ("e500v2", ("efdadd r3,r5,r7",), "10653ae0"),
        ("Zen", ("evfsmadd r3,r5,r7",), "10653a82"),
    ):
        assembly = "\n    ".join(instructions)
        source.write_text(f"asm void spe(void) {{\n    nofralloc\n    {assembly}\n}}\n", encoding="ascii")
        run(compiler, "-nodefaults", "-proc", cpu, "-O2", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if code != bytes.fromhex(words):
            raise ValueError(f"{compiler}: unexpected SPE encoding for {cpu}\n{code.hex(' ', 4)}")
        results[f"SPE opcodes for {cpu}"] = code

    for cpu, instruction, message in (
        ("gekko", "evor r3,r4,r5", "illegal instruction for this processor"),
        ("e500", "efdadd r3,r5,r7", "illegal instruction for this processor"),
        ("e500v2", "evfsmadd r3,r5,r7", "illegal instruction for this processor"),
        ("Zen", "efdadd r3,r5,r7", "illegal instruction for this processor"),
        ("e500", "evldd r3,1(r4)", "number is out of range"),
        ("e500", "evldd r3,256(r4)", "number is out of range"),
        ("e500", "evldd r3,-8(r4)", "number is out of range"),
        ("e500", "evldh r3,2(r4)", "number is out of range"),
    ):
        source.write_text(f"asm void spe(void) {{\n    nofralloc\n    {instruction}\n    blr\n}}\n", encoding="ascii")
        result = subprocess.run(
            (compiler, "-nodefaults", "-proc", cpu, "-c", source, "-o", obj),
            cwd=work, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
        )
        if not result.returncode or message not in result.stdout:
            raise ValueError(f"{compiler}: missing SPE diagnostic for {cpu} {instruction}\n{result.stdout}")
    return results


def check_spe_c(compiler, objcopy, work):
    fixtures = (
        (
            "spe_literal.c", "-O2", ".text",
            "__ev64_u32__ f(void) { return (__ev64_u32__){0x12345678U, 0x90abcdefU}; }\n",
            "3c601234 38835678 3c6090ac 3803cdef 1064022d 4e800020",
        ),
        (
            "spe_splat.c", "-O2", ".text",
            "__ev64_u32__ f(void) { return (__ev64_u32__){3}; }\n",
            "10630229 4e800020",
        ),
        (
            "spe_halfwords.c", "-O2", ".text",
            "typedef __ev64_u16__ V;\n"
            "V get(const V* p) { return *p; }\n"
            "void copy(V* a, const V* b) { *a = *b; }\n",
            "10601b04 4e800020 10002304 10001b24 4e800020",
        ),
        (
            "spe_reg_arg.c", "-O2", ".text",
            "__ev64_u32__ f(int a, __ev64_u32__ b) { return b; }\n",
            "10642217 4e800020",
        ),
        (
            "spe_opaque.c", "-O2", ".text",
            "__ev64_opaque__ f(__ev64_u16__ x) { return x; }\n",
            "4e800020",
        ),
        (
            "spe_stack_arg.c", "-O2", ".text",
            "typedef __ev64_u32__ V;\n"
            "V f(int a, int b, int c, int d, int e, int q, int g, int h, V i) { return i; }\n",
            "38010008 10600302 4e800020",
        ),
        (
            "spe_static.c", "-O2", ".sdata",
            "typedef __ev64_u32__ V;\n"
            "V value = (V){0xa2500001U, 0x12345678U};\n",
            "a2500001 12345678",
        ),
    )
    results = {}
    for name, opt, section, code, expected in fixtures:
        source = work / name
        source.write_text(code, encoding="ascii")
        obj = source.with_suffix(".o")
        dump = source.with_suffix(section)
        run(compiler, "-nodefaults", "-proc", "e500", opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section={section}={dump}", obj, cwd=work)
        actual = dump.read_bytes()
        if actual != bytes.fromhex(expected):
            raise ValueError(f"{compiler}: unexpected SPE output for {name}\n{actual.hex(' ', 4)}")
        results[name] = actual
    return results


def check_stock_spe_pch(compiler, reference, objcopy, work):
    header = work / "spe_prefix.h"
    header.write_text(
        "typedef __ev64_u32__ V;\n"
        "static inline V getv(void) { return (V){5}; }\n",
        encoding="ascii",
    )
    prefix = header.with_suffix(".mch")
    source = work / "spe_prefix_client.c"
    source.write_text("V test(void) { return getv(); }\n", encoding="ascii")
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    flags = ("-nodefaults", "-proc", "e500", "-O2")
    run(reference, *flags, "-precompile", prefix, header, cwd=work)
    run(compiler, *flags, "-prefix", prefix, "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    code = section.read_bytes()
    if code != bytes.fromhex("10650229 4e800020"):
        raise ValueError(f"{compiler}: unexpected SPE inline function from stock PCH\n{code.hex(' ', 4)}")


def check_aligned_pch(compiler, reference, objcopy, work):
    header = work / "aligned_prefix.h"
    header.write_text(
        "typedef __declspec(align(32)) char AlignedChar;\n"
        "__declspec(align(64)) static unsigned value = 11;\n"
        "__declspec(interrupt save_spe critical) void irq(void);\n",
        encoding="ascii",
    )
    source = work / "aligned_prefix_client.c"
    source.write_text(
        "extern void sink(void*);\nvoid f(void) { AlignedChar local; sink(&local); }\n"
        "volatile unsigned state; void irq(void) { ++state; }\nunsigned get(void) { return value; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    dump = source.with_suffix(".text")
    flags = ("-nodefaults", "-proc", "e500", "-fp", "soft", "-spe_vector", "-O2")
    run(reference, *flags, "-include", header.name, "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={dump}", obj, cwd=work)
    expected = dump.read_bytes()
    for label, producer in (("stock", reference), ("source", compiler)):
        prefix = work / f"{label}_aligned_prefix.mch"
        run(producer, *flags, "-precompile", prefix, header, cwd=work)
        run(compiler, *flags, "-prefix", prefix, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={dump}", obj, cwd=work)
        if dump.read_bytes() != expected:
            raise ValueError(f"{compiler}: unexpected code from {prefix.name}")
        sections = run("llvm-readobj", "--sections", obj, cwd=work)
        section = sections.split("Name: .sdata (", 1)[-1].split("}", 1)[0]
        if "AddressAlignment: 64\n" not in section:
            raise ValueError(f"{compiler}: {prefix.name} lost static data alignment")

    legacy_prefix = work / "legacy_aligned_prefix.mch"
    image = bytearray((work / "source_aligned_prefix.mch").read_bytes())
    image[4:6] = (5145).to_bytes(2, "little")
    legacy_prefix.write_bytes(image)
    try:
        run(compiler, *flags, "-prefix", legacy_prefix, "-c", source, "-o", obj, cwd=work)
    except subprocess.CalledProcessError as error:
        if "illegal precompiled header version" not in error.output:
            raise
    else:
        raise ValueError(f"{compiler}: accepted a source PCH with the previous qualifier layout")


def check_void_delete_diagnostics(compiler, work):
    fixtures = {
        "void_delete.cpp": (
            "#define DELETE(p) delete p\n"
            "void scalar(void* p) { DELETE(p); }\n"
            "void array(const volatile void* p) { ::delete[] p; }\n"
            "class Incomplete;\n"
            "void incomplete(Incomplete* p) { delete p; }\n",
            2,
            0,
        ),
        "nested_void_delete.cpp": (
            "template<class T> void leaf(T* p) { delete p; }\n"
            "template<class T> void inner(T* p) { leaf(p); }\n"
            "template<class T> void outer(T* p) { inner(p); }\n"
            "void root(void* p) { outer(p); }\n",
            1,
            3,
        ),
        "member_void_delete.cpp": (
            "template<class T> struct Box { T* p; ~Box() { delete p; } void release(); };\n"
            "template<class T> void Box<T>::release() { delete[] p; }\n"
            "void destroy(Box<void>* p) { delete p; }\n"
            "void release(Box<void>& p) { p.release(); }\n",
            2,
            2,
        ),
        "explicit_void_delete.cpp": (
            "template<class T> void kill(T* p) { delete p; }\n"
            "template void kill<void>(void*);\n",
            1,
            1,
        ),
    }
    results = {}
    for name, (contents, warnings, notes) in fixtures.items():
        source = work / name
        source.write_text(contents, encoding="ascii")
        for flags, expected in (((), warnings), (("-w", "off"), 0),
                                (("-pragma", "ANSI_strict on"), warnings)):
            output = run(compiler, *FLAGS, *flags, "-msgstyle", "parseable", "-c", source.name,
                         "-o", source.with_suffix(".o"), cwd=work)
            if (output.count(">(10480) deleting a void pointer is undefined") != expected or
                    output.count("|Compiler|Warning") != expected or
                    output.count(">(10544)     (corresponding point of instantiation for ") != notes):
                raise ValueError(f"{compiler}: unexpected delete diagnostics for {name}\n{output}")
            results[f"{name} {flags}"] = output

    source = work / "numbered_diagnostic_length.cpp"
    for length in (133, 134, 135):
        source.write_text(
            "template<class T> void kill(T* p) { delete p; }\n"
            f"void {'a' * length}(void* p) {{ kill(p); }}\n",
            encoding="ascii",
        )
        output = run(compiler, *FLAGS, "-msgstyle", "parseable", "-nowraplines", "-c",
                     source.name, "-o", source.with_suffix(".o"), cwd=work)
        frame = ">  (instantiating: 'kill<void>(void *)'"
        if length != 134:
            frame += ")"
        if frame not in output.splitlines():
            raise ValueError(f"{compiler}: unexpected numbered message length\n{output}")
        results[f"{source.name} {length}"] = output
    return results


def check_template_actions(compiler, objcopy, nm, work):
    cases = {
        "template_action_order.cpp": (
            "template<class T> void A(T* p) { delete p; }\n"
            "template<class T> void B(T* p) { delete[] p; }\n"
            "void caller(void* p) {\n"
            "    A(p);\n"
            "    B(p);\n"
            "    A(p);\n"
            "}\n"
            "void ordinary(void* p) { delete p; }\n"
        ),
        "template_action_forward.cpp": (
            "template<class T> void forward(T* p);\n"
            "void early(void* p) { forward(p); }\n"
            "void intervening(void* p) { delete[] p; }\n"
            "template<class T> void forward(T* p) { delete p; }\n"
            "void again(void* p) { forward(p); }\n"
            "void last(void* p) { delete[] p; }\n"
        ),
        "template_action_address.cpp": (
            "template<class T> void target(T* p) { delete p; }\n"
            "void (*callback)(void*) = target;\n"
            "void last(void* p) { delete[] p; }\n"
        ),
    }
    results = {}
    for name, contents in cases.items():
        source = work / name
        source.write_text(contents, encoding="ascii")
        obj = source.with_suffix(".o")
        normalized = source.with_suffix(".normalized")
        for ipa in ("off", "file"):
            output = run(compiler, *FLAGS, "-O4,p", "-inline", "off", "-ipa", ipa,
                         "-msgstyle", "parseable", "-c", source.name, "-o", obj, cwd=work)
            run(objcopy, "--remove-section=.comment", obj, normalized, cwd=work)
            results[f"{name} -ipa {ipa}"] = (output, normalized.read_bytes())

    source = work / "template_action_liveness.cpp"
    source.write_text(
        "template<class T> void unused_target(T* p) { delete p; }\n"
        "template<class T> void target(T* p) { delete p; }\n"
        "inline void unused(void* p) { unused_target(p); }\n"
        "inline void delayed(void* p) { target(p); }\n"
        "void before(void* p) { delete[] p; }\n"
        "void root(void* p) { delayed(p); }\n"
        "void last(void* p) { delete[] p; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for ipa in ("off", "file"):
        output = run(compiler, *FLAGS, "-O4,p", "-inline", "off", "-ipa", ipa,
                     "-msgstyle", "parseable", "-c", source.name, "-o", obj, cwd=work)
        symbols = run(nm, "--defined-only", obj, cwd=work)
        if "target<v>__FPv_v" not in symbols or "unused_target" in symbols:
            raise ValueError(f"{compiler}: unexpected template liveness\n{symbols}")
        normalized = source.with_suffix(".normalized")
        run(objcopy, "--remove-section=.comment", obj, normalized, cwd=work)
        results[f"{source.name} -ipa {ipa}"] = (output, normalized.read_bytes())
    return results


def check_reference_cast_diagnostics(compiler, objcopy, work):
    source = work / "reference_cast.cpp"
    source.write_text(
        "struct Base { int value; };\n"
        "struct Derived : Base { int extra; };\n"
        "extern Derived make();\n"
        "extern const Derived empty;\n"
        "extern void use(const Base&);\n"
        "void conditional(bool choose) { use(static_cast<const Derived&>(choose ? make() : empty)); }\n"
        "void temporary() { use(static_cast<const Base&>(make())); }\n"
        "void lvalue(Derived& value) { use(static_cast<const Base&>(value)); }\n"
        "void implicit() { use(make()); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    normalized = source.with_suffix(".normalized")
    results = {}
    for args, severity in (
        ((), "Warning"),
        (("-pragma", "ANSI_strict on"), "Warning"),
        (("-w", "off"), None),
        (("-w", "error"), "Error"),
    ):
        obj.unlink(missing_ok=True)
        proc = subprocess.run(
            (compiler, *FLAGS, *args, "-msgstyle", "parseable", "-c", source.name, "-o", obj),
            cwd=work, capture_output=True, text=True, timeout=30,
        )
        count = proc.stdout.count("(10142)")
        if count != (0 if severity is None else 2) or bool(proc.returncode) != (severity == "Error"):
            raise ValueError(f"{compiler}: unexpected reference-cast diagnostics\n{proc.stdout}{proc.stderr}")
        contents = None
        if not proc.returncode:
            run(objcopy, "--remove-section=.comment", obj, normalized, cwd=work)
            contents = normalized.read_bytes()
        results[f"{source.name} {' '.join(args)}"] = (proc.stdout, proc.stderr, contents)

    source = work / "scalar_reference_cast.cpp"
    source.write_text(
        "extern int make_int();\n"
        "extern void use_int(const int&);\n"
        "void temporary() { use_int(static_cast<const int&>(make_int())); }\n"
        "void literal() { use_int(static_cast<const int&>(42)); }\n",
        encoding="ascii",
    )
    proc = subprocess.run(
        (compiler, *FLAGS, "-w", "error", "-msgstyle", "parseable", "-c", source.name),
        cwd=work, capture_output=True, text=True, timeout=30,
    )
    if proc.returncode == 0 or proc.stdout.count("(10142)") != 2:
        raise ValueError(f"{compiler}: unexpected promoted cast diagnostics\n{proc.stdout}{proc.stderr}")
    results[source.name] = (proc.stdout, proc.stderr)
    return results


def check_return_diagnostics(compiler, work):
    cases = (
        (
            "return_paths.cpp",
            """\
extern volatile void stop();
extern int test(int);
struct State {
    int data;
    bool check() const { return test(data); }
};
int paths(const State* s) {
    if (!s) return 0;
    else if (!s->check()) return 1;
    else if (!s->data) { stop(); }
    else { const int data = s->data; return test(data); }
}
int partial(int n) { if (n) return 1; }
int explicit_return(int n) { if (n) return; return 1; }
""",
            0,
            [("Warning", 13, 38), ("Warning", 14, 42), ("Warning", 14, 54)],
        ),
        (
            "return_pragmas.c",
            """\
int default_c(void) {}
#pragma warn_missingreturn on
int enabled(void) {}
#pragma warn_missingreturn_as_error on
int error(void) {}
#pragma warn_missingreturn_as_error reset
int restored(void) {}
#pragma warn_missingreturn reset
int disabled(void) {}
#pragma c9x on
int c99_explicit(void) { return; }
#pragma ANSI_strict on
int strict_c99(void) { return; }
""",
            1,
            [("Warning", 3, 19), ("Error", 5, 17), ("Warning", 11, 31), ("Error", 13, 29)],
        ),
        (
            "return_templates.cpp",
            """\
template<class T> T select(T n) {
    if (n) { const T x = n; return x; }
    else return 0;
}
template<class T> T partial(T n) { if (n) return n; }
int instantiate(int n) { return select(n) + partial(n); }
""",
            0,
            [("Warning", 5, 52)],
        ),
    )
    results = {}
    for name, body, status, expected in cases:
        source = work / name
        source.write_text(body, encoding="ascii")
        proc = subprocess.run(
            (compiler, *FLAGS, "-O4,p", "-ipa", "file", "-maxerrors", "12",
             "-msgstyle", "parseable", "-c", name, "-o", source.with_suffix(".o")),
            cwd=work, capture_output=True, text=True, timeout=30,
        )
        lines = proc.stdout.splitlines()
        diagnostics = []
        for index, line in enumerate(lines):
            if line == ">(10184) return value expected":
                severity = lines[index - 3].rsplit("|", 1)[-1]
                row, column = lines[index - 2].split("|")[1:3]
                diagnostics.append((severity, int(row), int(column)))
        if proc.returncode != status or diagnostics != expected:
            raise ValueError(f"{compiler}: unexpected return diagnostics\n{proc.stdout}{proc.stderr}")
        results[name] = (proc.stdout, proc.stderr)
    return results


def check_preprocess_output(compiler, work):
    source = work / "preprocess_output.c"
    source.write_bytes(
        b"// Intro comment\n#define A 3\n# pragma   c99 on\n\n"
        b"  int   one = A; /* tail */\n#include \"preprocess_output.h\"\n\n"
        b"int two=ID(0x1UL);\n\n#line 42 \"fake\\file.c\"\nint in_fake=__LINE__;\n"
        b"#line 45\nint same_file=__LINE__;\n"
    )
    (work / "preprocess_output.h").write_bytes(
        b"#pragma once\n#define ID(x) x\nint from_head;\n"
    )
    expected = (
        b"int one = 3 ;\r\nint from_head;\r\nint two=0x1UL ;\r\nint in_fake=42 ;\r\n"
        b"int same_file=45 ;\r\n"
    )
    results = {}
    options = (
        ("-E",),
        ("-EP",),
        ("-E", "-ppopt", "break"),
        ("-E", "-ppopt", "line,full"),
        ("-E", "-ppopt", "line,nobreak"),
        ("-E", "-ppopt", "pragma,line"),
        ("-E", "-ppopt", "pragma,comment,space"),
        ("-E", "-ppopt", "line", "-DTEST=1"),
        ("-EP", "-ppopt", "line"),
        ("-E", "-pragma", "simple_prepdump off"),
    )
    for args in options:
        data = subprocess.check_output(
            (compiler, *FLAGS, *args, source.name), cwd=work, stderr=subprocess.STDOUT, timeout=30,
        )
        if args in (("-E",), ("-EP",), ("-E", "-ppopt", "line,nobreak")) and data != expected:
            raise ValueError(f"{compiler}: unexpected preprocessed output with {' '.join(args)}\n{data!r}")
        results[f"preprocess {' '.join(args)}"] = data

    output = work / "preprocess_output.i"
    diagnostic = subprocess.check_output(
        (compiler, *FLAGS, "-P", "-o", output.name, source.name),
        cwd=work, stderr=subprocess.STDOUT, timeout=30,
    )
    if diagnostic or output.read_bytes() != expected:
        raise ValueError(f"{compiler}: unexpected preprocessed file output\n{diagnostic!r}")
    results["preprocess -P"] = output.read_bytes()

    show_macros = work / "preprocess_macros.c"
    show_macros.write_bytes(
        b"#define I 1\n#define DOUBLE(x) x+x\nint n = DOUBLE(  I );\n#undef I\n"
    )
    data = subprocess.check_output(
        (compiler, *FLAGS, "-E", "-pragma", "macro_prepdump on", show_macros.name),
        cwd=work, stderr=subprocess.STDOUT, timeout=30,
    )
    expected = b"/*#define I 1*/\r\n/*#define DOUBLE(x) x+x*/\r\nint n = 1+ 1 ;\r\n/*#undef I*/\r\n"
    if data != expected:
        raise ValueError(f"{compiler}: unexpected macro dump\n{data!r}")
    results["preprocess macro_prepdump"] = data

    tokens = work / "preprocess_tokens.c"
    tokens.write_bytes(
        b"<: :> <% %> %: %:%:\n??=pragma c99 on\n"
        b"int a; /* start\n * middle\n * end */ int follow;\n"
        b"<: :> <% %> %: %:%:\n"
    )
    data = subprocess.check_output(
        (compiler, *FLAGS, "-E", "-ppopt", "pragma,comment,line", "-pragma", "trigraphs on", tokens.name),
        cwd=work, stderr=subprocess.STDOUT, timeout=30,
    )
    if (b"#pragma c99 on\r\n" not in data or b"< : : > < % % > % : % : % :\r\n" not in data
            or not data.endswith(b"<: :> <% %> %: %:%:\r\n")):
        raise ValueError(f"{compiler}: unexpected text for directive and digraph spellings\n{data!r}")
    results["preprocess directive and token spelling"] = data

    expressions = work / "preprocess_expressions.c"
    expressions.write_text(
        "#pragma bool off\n"
        "#if true && !false && (-1 < 'AB')\nconditional_tokens\n#endif\n"
        "#define true 0\n"
        "#if !true\nmacro_shadows_constant\n#endif\n",
        encoding="ascii",
    )
    data = subprocess.check_output(
        (compiler, *FLAGS, "-lang=c", "-E", expressions.name),
        cwd=work, stderr=subprocess.STDOUT, timeout=30,
    )
    if data != b"conditional_tokens\r\nmacro_shadows_constant\r\n":
        raise ValueError(f"{compiler}: unexpected conditional tokens\n{data!r}")
    results["preprocess conditional tokens"] = data
    return results


def check_c99_language(compiler, objcopy, work):
    source = work / "c99_language.cpp"
    source.write_text(
        """\
#if __option(c99) != C99 || __option(c9x) != C99
#error incorrect C99 option
#endif
#if defined(__cplusplus) != CPP
#error incorrect language
#endif
#if __STDC_VERSION__ != (C99 ? 199901L : 199409L)
#error incorrect standard version
#endif
#pragma c99 off
#if __option(c9x) || __STDC_VERSION__ != 199409L
#error c99 off did not clear the option
#endif
#pragma c99 reset
#if __option(c9x) != C99
#error c99 reset did not restore the command-line option
#endif
#pragma c9x on
#if !__option(c99) || __STDC_VERSION__ != 199901L
#error c9x did not set c99
#endif
int result(void) { return __STDC_VERSION__ == 199901L; }
""",
        encoding="ascii",
    )
    results = {}
    obj = source.with_suffix(".o")
    normal = source.with_suffix(".normalized.o")
    for options, c99, cpp in (
        ((), 0, 1),
        (("-lang", "c99"), 1, 1),
        (("-lang", "c99", "-lang", "c"), 1, 0),
        (("-lang", "c", "-dialect", "c99"), 1, 0),
    ):
        output = run(
            compiler, *FLAGS, *options, f"-DC99={c99}", f"-DCPP={cpp}", "-w", "pragmas",
            "-c", source.name, "-o", obj, cwd=work,
        )
        if output:
            raise ValueError(f"{compiler}: unexpected C99 diagnostics\n{output}")
        run(objcopy, "--remove-section=.comment", obj, normal, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = normal.read_bytes()
    return results


def check_backend_diagnostics(compiler, objcopy, work):
    source = work / "uninitialized.c"
    source.write_text(
        """\
int first(void) {
    register int value;
    return value;
}

int second(void) {
    register int result;
    return result;
}
""",
        encoding="ascii",
    )
    results = {}
    for mode in ("off", "file"):
        output = run(
            compiler, *FLAGS, "-O4,p", "-ipa", mode, "-w", "on", "-msgstyle", "parseable",
            "-c", source.name, "-o", source.with_suffix(".o"), cwd=work,
        )
        lines = output.splitlines()
        warnings = [
            (lines[index - 2].split("|")[1:4], line)
            for index, line in enumerate(lines)
            if line.startswith(">(10185) ")
        ]
        if warnings != [
            (["2", "17", "5"], ">(10185) variable 'value' is not initialized before being used"),
            (["7", "17", "6"], ">(10185) variable 'result' is not initialized before being used"),
        ]:
            raise ValueError(f"{compiler}: unexpected variable warning locations with -ipa {mode}\n{output}")
        results[f"{source.name} {mode}"] = output

    source = work / "section_promotion.cpp"
    source.write_text(
        """\
extern const float input;
extern const float runtime;
const float* address() { return &runtime; }
extern const float runtime = input;

int after() { return 1; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    normal = source.with_suffix(".normalized.o")
    for mode, small, warnings, row, column, section in (
        ("off", "0", "on", "4", "34", ".rodata"),
        ("file", "0", "on", "6", "24", ".rodata"),
        ("file", "4", "on", "6", "24", ".sbss2"),
        ("file", "0", "off", "6", "24", ".rodata"),
        ("file", "0", "error", "6", "24", ".rodata"),
    ):
        proc = subprocess.run(
            (compiler, *FLAGS, "-O4,p", "-ipa", mode, "-sdata", small, "-sdata2", small,
             "-w", warnings, "-msgstyle", "parseable", "-c", source.name, "-o", obj),
            cwd=work, capture_output=True, text=True, timeout=30,
        )
        status = int(warnings == "error")
        severity = "Error" if status else "Warning"
        lines = proc.stdout.splitlines()
        if warnings == "off":
            diagnostic_matches = not lines
        else:
            diagnostic_matches = (
                len(lines) >= 6
                and lines[0].endswith(f"|Compiler|{severity}")
                and lines[1].split("|")[1:4] == [row, column, "1"]
                and lines[3].startswith(f">(33058) 'RW' object 'runtime' is being put into section '{section}'")
                and lines[5] == ">section's access permission will be changed to 'RW'"
            )
        if proc.returncode != status or not diagnostic_matches:
            raise ValueError(f"{compiler}: unexpected section permission diagnostic\n{proc.stdout}{proc.stderr}")
        if status:
            contents = b""
        else:
            headers = run("llvm-readobj", "--sections", obj, cwd=work)
            header = headers.split(f"Name: {section} (", 1)[-1].split("}", 1)[0]
            if "SHF_WRITE" not in header:
                raise ValueError(f"{compiler}: {section} remained read-only\n{header}")
            run(objcopy, "--remove-section=.comment", obj, normal, cwd=work)
            contents = normal.read_bytes()
        results[f"{source.name} {mode} {small} {warnings}"] = (proc.stdout, proc.stderr, contents)

    source = work / "vtable_section.cpp"
    source.write_text(
        """\
struct Widget { Widget(); virtual ~Widget(); };
Widget::Widget() {}
Widget::~Widget() {}
Widget value;
""",
        encoding="ascii",
    )
    for mode in ("off", "file"):
        obj = source.with_suffix(".o")
        output = run(
            compiler, *FLAGS, "-O4,p", "-RTTI", "off", "-sdata", "0", "-sdata2", "0",
            "-ipa", mode, "-msgstyle", "parseable", "-c", source.name, "-o", obj, cwd=work,
        )
        symbols = run("llvm-readobj", "--symbols", obj, cwd=work)
        vtable = symbols.partition("Name: __vt__6Widget (")[2].partition("}")[0]
        if output or "Section: .data (" not in vtable:
            raise ValueError(f"{compiler}: unexpected vtable section with -ipa {mode}\n{output}{vtable}")
    return results


def check_reference_initializers(compiler, objcopy, work):
    cases = {
        "local_reference_cast.cpp": """\
struct Value { int data; };
extern Value make();
extern void use(const Value&);
void test() {
    const Value& value = static_cast<const Value&>(make());
    use(value);
}
""",
        "local_reference_lifetime.cpp": """\
struct Value { Value(); Value(const Value&); ~Value(); int data; };
extern Value make();
extern void use(const Value&);
void result() { const Value& value = make(); use(value); }
void construct() { const Value& value = Value(); use(value); }
""",
        "local_reference_scalar.cpp": """\
extern int make_int();
extern void use(const int&);
extern void use_volatile(const volatile int&);
extern void use_short(short);
void test() {
    const int& value((make_int(), 42));
    use(value);
    use_short(-1);
}
void preserve_store() {
    volatile int value = make_int();
    const volatile int& ref = value;
    use_volatile(ref);
}
""",
        "local_reference_alias.cpp": """\
struct Value { int data; };
extern Value make();
extern void use(const Value*, const Value*);
void test() {
    const Value first = make();
    const Value second = first;
    use(&first, &second);
}
""",
        "global_reference_relocation.cpp": """\
struct Value { int data; };
extern Value source;
const int& value(source.data);
""",
        "global_reference_lifetime.cpp": """\
struct Value { Value(); Value(const Value&); ~Value(); int data; };
extern Value make();
const Value& value = make();
""",
        "global_reference_register_result.cpp": """\
struct Value { int data; };
extern Value make();
const Value& value = make();
""",
        "static_reference_lifetime.cpp": """\
struct Value { Value(); Value(const Value&); ~Value(); int data; };
extern void use(const Value&);
void test() {
    static const Value& value = Value();
    use(value);
}
""",
        "static_reference_conversion.cpp": """\
extern float source;
extern void use(const int&);
void test() {
    static const int& value = source;
    use(value);
}
""",
        "inline_static_reference.cpp": """\
struct Value { int data; };
extern Value source;
extern void use(const Value&);
inline void f() {
    static const Value& value = source;
    use(value);
}
void test() { f(); }
""",
        "inline_static_constant.cpp": """\
extern void use(const int&);
inline void f() {
    static int value = 42;
    use(value);
}
void test() { f(); }
""",
        "inline_function_names.cpp": """\
extern void use(const char*);
inline void f() {
    use(__func__);
    use(__FUNCTION__);
    use(__PRETTY_FUNCTION__);
}
void test() { f(); }
""",
        "inline_terminal_return.cpp": """\
extern int value();
extern void use(int);
inline int f() {
    use(1);
    return value();
}
void discard() { f(); use(2); }
int forward() { return f(); }
""",
        "inline_discarded_conditional_result.cpp": """\
extern bool ready();
extern void use(int);
inline int f() {
    if (ready())
        return 1;
    return 2;
}
void test() { f(); use(10); }
""",
        "file_ipa_branch.cpp": """\
extern bool ready();
extern void use(int);
static int increment(int value) { return value + 1; }
void test(int value) {
    if (ready())
        use(increment(value));
    use(2);
}
""",
        "pod_value_initialization.cpp": """\
struct Tag {};
struct Tiny { char bytes[3]; };
struct Block { int words[17]; };
extern int accept(const Tag&);
inline int ignore(const Tag& = Tag()) { return 7; }
int pass() { return accept(Tag()); }
int discard() { return ignore(); }
int read_local() { Tag tag = Tag(); return accept(tag); }
Tiny result() { return Tiny(); }
void assign(Block* target) { *target = Block(); }
""",
        "global_object_data.cpp": """\
struct Value { int data; };
Value empty = Value();
Value zero = {0};
Value nonzero = {1};
extern const Value read_only_zero = {0};
""",
        "global_runtime_data.cpp": """\
extern int source();
struct Value { int data; };
Value runtime = {source()};
""",
        "overloaded_function_reference.cpp": """\
void target();
int target(int);
int (&global)(int) = target;
int local(int value) {
    int (&reference)(int) = target;
    return reference(value);
}
""",
    }

    results = {}
    for name, contents in cases.items():
        source = work / name
        source.write_text(contents, encoding="ascii")
        obj = source.with_suffix(".o")
        normalized = source.with_suffix(".normalized")
        modes = [("-O0", "off", ()), ("-O4,p", "file", ())]
        if name == "inline_discarded_conditional_result.cpp":
            modes.append(("-O4,p", "off", ()))
        if name == "file_ipa_branch.cpp":
            modes.append(("-O4,p", "file", ("-inline", "off")))
        if name in {"global_object_data.cpp", "global_runtime_data.cpp"}:
            modes.append(("-O4,p", "file", ("-pragma", "explicit_zero_data on")))
        for opt, ipa, options in modes:
            output = run(compiler, *FLAGS, opt, "-ipa", ipa, *options, "-msgstyle", "parseable",
                         "-c", source.name, "-o", obj, cwd=work)
            run(objcopy, "--remove-section=.comment", obj, normalized, cwd=work)
            results[" ".join((name, opt, "-ipa", ipa, *options))] = (output, normalized.read_bytes())

    source = work / "invalid_reference_initializers.cpp"
    source.write_text(
        "int& global = 1;\n"
        "struct Value { int data; };\n"
        "void local() { Value& reference = Value(); }\n",
        encoding="ascii",
    )
    process = subprocess.run(
        (compiler, *FLAGS, "-maxerrors", "8", "-msgstyle", "parseable", "-c", source.name,
         "-o", source.with_suffix(".o")),
        cwd=work, capture_output=True, text=True, timeout=30,
    )
    output = process.stdout + process.stderr
    messages = [line for line in output.splitlines() if line.startswith(">")]
    if process.returncode != 1 or messages != [
        ">(10228) non-const '&' reference initialized to temporary"
    ] * 3:
        raise ValueError(f"{compiler}: unexpected reference initializer diagnostics\n{output}")
    results[source.name] = output
    return results


def check_unconditional_branch_ranges(compiler, objcopy, work):
    forward = "asm void target() {\n    b label\n    nop\n    nop\nlabel:\n    blr\n}\n"
    backward = "asm void target() {\nlabel:\n    nop\n    nop\n    nop\n    b label\n}\n"
    results = {}
    for name, pragmas, body, diagnostic in (
        ("forward_out", "#pragma b_range 12\n", forward, "(33122) branch instruction needs to branch 12 but range is 11 ... -12"),
        ("forward_in", "#pragma b_range 13\n", forward, None),
        ("backward_out", "#pragma b_range 11\n", backward, "(33122) branch instruction needs to branch -12 but range is 10 ... -11"),
        ("backward_in", "#pragma b_range 12\n", backward, None),
        ("absolute_label", "#pragma b_range 0\n", forward.replace("b label", "ba label"),
         "(33078) illegal use of label or constant in a absolute branch"),
        ("compressed_reset", "#pragma b_range 4\n#pragma prepare_compress off\n", forward, None),
        ("compressed_conflict", "#pragma b_range 4\n#pragma prepare_compress on\n",
         "int target(int x) { return x + 1; }\n", "(33121) use pragma b_range after pragma prepare_compress"),
        ("invalid", "#pragma b_range 0x2000001\n", "int target(int x) { return x + 1; }\n", "(10186) illegal #pragma"),
        ("float", "#pragma b_range 1e+2\n", "int target(int x) { return x + 1; }\n", "(10186) illegal #pragma"),
        ("eol", "#pragma b_range off extra\n", "int target(int x) { return x + 1; }\n", "(10113) end of line expected"),
    ):
        source = work / f"b_range_{name}.cpp"
        source.write_text(pragmas + body, encoding="ascii")
        obj = source.with_suffix(".o")
        proc = subprocess.run(
            (compiler, *FLAGS, "-O0", "-inline", "noauto", "-func_align", "4", "-Cpp_exceptions", "off",
             "-c", source.name, "-o", obj),
            cwd=work, text=True, capture_output=True, timeout=30,
        )
        if diagnostic:
            if proc.returncode != 1 or diagnostic not in proc.stdout:
                raise ValueError(f"{compiler}: unexpected {name} branch diagnostics\n{proc.stdout}{proc.stderr}")
            results[source.name] = (proc.stdout, proc.stderr)
        else:
            if proc.returncode or proc.stdout or proc.stderr:
                raise ValueError(f"{compiler}: {name} branch failed\n{proc.stdout}{proc.stderr}")
            section = source.with_suffix(".text")
            run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
            results[source.name] = section.read_bytes()
    return results


def check_auto_aggregate_initialization(compiler, work):
    results = {}
    for name, body in (
        ("full", "struct Pair { int x, y; }; int entry(int x) { Pair p = {x, x + 3}; return p.y; }"),
        ("partial", "struct Pair { int x, y; }; int entry(int x) { Pair p = {x}; use(&p); return p.y; }"),
        ("nonzero", "struct Pair { int x, y; }; int entry(int x) { Pair p = {x, 3}; use(&p); return p.y; }"),
        ("padding", "struct Pad { short s; int n; }; int entry(int x) { Pad p = {(short)x, x}; use(&p); return p.n; }"),
        ("bitfield", "struct Bits { unsigned x:3, y:5; }; int entry(int x) { Bits p = {(unsigned)x}; use(&p); return p.y; }"),
        ("class_array", "struct P { int x; }; P make(); int entry() { P p[2] = {make(), make()}; use(p); return p[1].x; }"),
        ("large_zero", "struct Large { int x[10]; }; int entry() { Large v = {}; use(&v); return v.x[0]; }"),
    ):
        source = work / f"local_aggregate_{name}.cpp"
        obj = source.with_suffix(".o")
        source.write_text("extern void use(const void*);\n" + body + "\n", encoding="ascii")
        run(compiler, *FLAGS, "-O4,p", "-lang=c++", "-inline", "noauto", "-func_align", "4",
            "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work)
        results[source.name] = obj.read_bytes()
    return results


def check_unrolled_loop_guard(compiler, work):
    results = {}
    for name, bound_type, index_type, pragma in (
        ("signed", "int", "int", ""),
        ("promoted", "short", "int", ""),
        ("unsigned", "unsigned int", "unsigned int", ""),
        ("disabled", "int", "int", "#pragma opt_unroll_loops off\n"),
    ):
        source = work / f"unroll_guard_{name}.cpp"
        obj = source.with_suffix(".o")
        source.write_text(
            pragma
            + f'extern "C" int sum(const int* p, {bound_type} n) {{\n'
            + "    int result = 0;\n"
            + f"    for ({index_type} i = 0; i < n; ++i) result += p[i];\n"
            + "    return result;\n"
            + "}\n",
            encoding="ascii",
        )
        run(compiler, *FLAGS, "-O4,p", "-lang=c++", "-inline", "noauto", "-func_align", "4",
            "-Cpp_exceptions", "off", "-c", source.name, "-o", obj, cwd=work)
        results[source.name] = obj.read_bytes()
    return results


def check(compiler, objcopy, nm, objdump, work):
    if "Version 4.2 build 140" not in run(compiler, "-version", cwd=work):
        raise ValueError(f"{compiler}: incorrect version banner")

    options = work / "options.c"
    options.write_text("__option(auto_inline)\n", encoding="ascii")
    for args, expected in (
        (("-O4",), "1"),
        (("-O4", "-inline", "noauto"), "0"),
        (("-inline", "noauto", "-O4"), "1"),
        (("-inline", "auto", "-O0"), "0"),
        (("-opt", "level=4"), "0"),
    ):
        actual = run(compiler, *FLAGS, "-E", *args, options, cwd=work).strip()
        if actual != expected:
            raise ValueError(f"{compiler}: {' '.join(args)} expands auto_inline to {actual!r}, expected {expected}")

    results = {}
    source = work / "auto_inline_limit.cpp"
    obj = source.with_suffix(".o")
    body = """\
extern void ext(int);
extern pascal void ext_pascal(int);
static int plain(int x) { return x + 3; }
static int with_call(int x) { ext(x); return x; }
static int with_pascal(int x) { ext_pascal(x); return x; }
static int with_scope(int x) { { int unused; } return x; }
int call_plain(int x) { return plain(x); }
int call_with(int x) { return with_call(x); }
int call_pascal(int x) { return with_pascal(x); }
int call_scope(int x) { return with_scope(x); }
"""
    for limit in ("0", "1", "2", "LIMIT", "VALUE(1)"):
        source.write_text(
            f"#define LIMIT 1\n#define VALUE(x) x\n#pragma inline_max_auto_size({limit})\n" + body,
            encoding="ascii",
        )
        output = run(compiler, *FLAGS, "-O4,p", "-inline", "auto", "-Cpp_exceptions", "off",
                     "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        if "illegal #pragma" in output:
            raise ValueError(f"{compiler}: rejected inline limit {limit}\n{output}")
        results[f"{source.name} {limit}"] = obj.read_bytes()

    cases = (
        ("increment.cpp", CPP_SOURCE, (14, 19)),
        ("copy_word.c", COPY_SOURCE, (32, 32, 36, 36, 19)),
        ("copy_byte.c", "#pragma legacy_struct_alignment on\n" + COPY_SOURCE, (34, 34, 38, 38) * 4 + (19,)),
        ("copy_fp.c", "#pragma ppc_no_fp_blockmove off\n" + COPY_SOURCE, (50, 54, 19)),
    )
    for name, content, expected in cases:
        source = work / name
        source.write_text(content, encoding="ascii")
        obj = source.with_suffix(".o")
        run(compiler, *FLAGS, "-O2", "-inline", "on", "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        header = obj.read_bytes()
        if header[:6] != b"\x7fELF\x01\x02" or header[16:20] != b"\x00\x01\x00\x14":
            raise ValueError(f"{compiler}: {name} produced something other than a big-endian PowerPC object")
        comment = source.with_suffix(".comment")
        run(objcopy, f"--dump-section=.comment={comment}", obj, cwd=work)
        if not comment.read_bytes().startswith(b"CodeWarrior\x0f\x04\x00\x00\x01"):
            raise ValueError(f"{compiler}: incorrect CodeWarrior object version for {name}")
        section = source.with_suffix(".text")
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        code = section.read_bytes()
        if len(code) != 4 * len(expected) or tuple(byte >> 2 for byte in code[::4]) != expected:
            raise ValueError(f"{compiler}: unexpected instructions for {name}\n{code.hex(' ', 4)}")
        results[name] = code
    results["ipa.c"] = check_ipa(compiler, objcopy, nm, work)
    check_spe_options(compiler, work)
    results.update(check_scalar_spe(compiler, objcopy, work))
    results.update(check_spe_frames(compiler, objcopy, work))
    results.update(check_interrupt_frames(compiler, objcopy, work))
    results.update(check_spe_assembly(compiler, objcopy, work))
    results.update(check_spe_c(compiler, objcopy, work))
    source = work / "conversions.c"
    source.write_text(
        "double fewer(int a, unsigned b, int c) { return (double)a + (double)b + (double)c; }\n"
        "double several(int a, unsigned b, int c, unsigned d, int e) {\n"
        "    return (double)a + (double)b + (double)c + (double)d + (double)e;\n"
        "}\n"
        "float following(int a) { return (float)a; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for opt in ("-O0", "-O4,p"):
        run(compiler, *FLAGS, opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {opt}"] = section.read_bytes()

    source = work / "addresses.c"
    source.write_text(
        "extern volatile int words[64];\n"
        "void copy_words(volatile int* dst) { dst[0]=words[0]; dst[1]=words[0]; dst[2]=words[0]; }\n"
        "volatile int* word_address(void) { return words + 7; }\n"
        "volatile int at_address : 0x8000F018;\n"
        "int read_address(void) { return at_address; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    relocations = source.with_suffix(".rela.text")
    for opt in ("-O0", "-O4,p"):
        run(compiler, *FLAGS, opt, "-sdata", "0", "-sdata2", "0", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", f"--dump-section=.rela.text={relocations}", obj, cwd=work)
        results[f"{source.name} {opt}"] = (section.read_bytes(), relocations.read_bytes())

    source = work / "deferred_specializations.cpp"
    source.write_text(
        "extern void consume(const int&);\n"
        "template<class T> struct Wrapper;\n"
        "template<> struct Wrapper<int> {\n"
        "    static void first(int value) { consume(value); }\n"
        "    static void second(int) { consume(12); }\n"
        "};\n"
        "template<> struct Wrapper<char> {\n"
        "    static void first(char value) { consume(value); }\n"
        "    static void second(char) { consume(24); }\n"
        "};\n"
        "void entry(int value) {\n"
        "    Wrapper<int>::first(value);\n"
        "    Wrapper<char>::second(value);\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for mode in ("off", "file"):
        run(
            compiler, *FLAGS, "-O4,p", "-inline", "noauto", "-ipa", mode,
            "-func_align", "4", "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} -ipa {mode}"] = obj.read_bytes()

    source = work / "inline_string_sequence.cpp"
    source.write_text(
        "extern void observe(const char*);\n"
        "extern void wide(const wchar_t*);\n"
        "extern void observe_int(const int&);\n"
        "inline void unused() { observe(\"unused\"); }\n"
        "inline void used() {\n"
        "    sizeof(\"discarded\");\n"
        "    observe(\"one\"); wide(L\"wide\"); observe(\"one\");\n"
        "}\n"
        "void (*anchor)() = used;\n"
        "void entry() { observe_int(0); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for mode in ("off", "file"):
        run(
            compiler, *FLAGS, "-O4,p", "-inline", "noauto", "-ipa", mode,
            "-func_align", "4", "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} -ipa {mode}"] = obj.read_bytes()

    source = work / "literal_pooling.cpp"
    literals = (
        "extern void text(const char*);\n"
        "extern void wide(const wchar_t*);\n"
        "extern void single(float);\n"
        "extern void twice(double);\n"
        'void first() { text("alpha"); wide(L"beta"); single(1.25f); twice(8.5625); }\n'
        'void second() { text("alpha"); wide(L"beta"); single(1.25f); twice(8.5625);\n'
        '    text("gamma"); wide(L"delta"); single(3.14f); twice(1.33333333333); }\n'
    )
    obj = source.with_suffix(".o")
    for mode in ("off", "file"):
        for pool in ("off", "on"):
            for override, options in (
                ("", ("-str", "noreuse,nopool,readonly")),
                ("", ("-str", "noreuse,nopool,noreadonly")),
                ("#pragma pool_strings off\n", ("-str", "noreuse,nopool", "-rostr")),
            ):
                source.write_text(override + literals, encoding="ascii")
                run(
                    compiler, *FLAGS, "-O4,p", "-inline", "noauto", "-ipa", mode, "-pool", pool,
                    *options, "-func_align", "4", "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
                )
                results[f"{source.name} -ipa {mode} -pool {pool} {' '.join(options)}"] = obj.read_bytes()

    source = work / "literal_initializers.cpp"
    source.write_text(
        'static const char* const first = "alpha";\n'
        'static const wchar_t* const second = L"beta";\n'
        "const char* one() { return first; }\n"
        "const wchar_t* two() { return second; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for mode in ("off", "file"):
        run(
            compiler, *FLAGS, "-O4,p", "-inline", "noauto", "-ipa", mode, "-str", "reuse,pool,readonly",
            "-func_align", "4", "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} -ipa {mode}"] = obj.read_bytes()

    derived_declarations = (
        "struct Base { int x; };\n"
        "struct Derived : Base {};\n"
        "extern Derived make();\n"
        "extern void use(const Derived*);\n"
    )
    for name, initializer in (
        ("derived_single", "Derived one() { static Derived d = make(); return d; }\n"),
        ("derived_global", "Derived d = {make()}; Derived empty = {};\n"),
        ("derived_member", "struct Holder { Derived field; }; Holder h = {make()};\n"),
        ("derived_static_array", "void one() { static Derived d[3] = {make(), make()}; use(d); }\n"),
        ("derived_auto_array", "void one() { Derived d[2][2] = {{make()}, {make(), make()}}; use(d[0]); }\n"),
    ):
        source = work / f"{name}.cpp"
        source.write_text(derived_declarations + initializer, encoding="ascii")
        obj = source.with_suffix(".o")
        for mode in ("off", "file"):
            run(
                compiler, *FLAGS, "-O4,p", "-inline", "noauto", "-ipa", mode,
                "-func_align", "4", "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
            )
            results[f"{source.name} -ipa {mode}"] = obj.read_bytes()

    source = work / "derived_invalid.cpp"
    for initializer in ("Derived d = {{1}};", "Derived d[2] = {{}, {}};", "Derived d = 1;"):
        source.write_text(derived_declarations + initializer + "\n", encoding="ascii")
        proc = subprocess.run(
            (compiler, *FLAGS, "-c", source.name), cwd=work, capture_output=True, text=True, timeout=30,
        )
        output = proc.stdout + proc.stderr
        if proc.returncode != 1 or "(10174) illegal initialization" not in output:
            raise ValueError(f"{compiler}: unexpected diagnostic for {initializer}\n{output}")
        results[f"{source.name} {initializer}"] = output

    source = work / "addresses_across_calls.c"
    source.write_text(
        "struct Queue { int count, next, values[6]; };\n"
        "extern struct Queue queue;\n"
        "extern volatile int status;\n"
        "extern void visit(int*);\n"
        "int next_value(void) {\n"
        "    int count = queue.count;\n"
        "    visit(queue.values);\n"
        "    return count + queue.next;\n"
        "}\n"
        "int read_status(void) {\n"
        "    int before = status;\n"
        "    visit(queue.values);\n"
        "    return before + status;\n"
        "}\n"
        "int read_pointer(int* p) {\n"
        "    int before = *p;\n"
        "    visit(p);\n"
        "    return before + *p;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2,p", "-O4,p", "-O4,s"):
        run(
            compiler, *FLAGS, opt, "-ipa", "off", "-sdata", "0", "-sdata2", "0",
            "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "counted_loop.c"
    source.write_text(
        "int count_loop(volatile int* p) {\n"
        "    int i;\n"
        "    for (i = -7; i < 32767; ++i) *p += i;\n"
        "    return *p;\n"
        "}\n"
        "int signed_bounds(volatile int* p) {\n"
        "    int i;\n"
        "    for (i = -70001; i < -69980; ++i) *p ^= i;\n"
        "    return *p;\n"
        "}\n"
        "unsigned unsigned_bounds(volatile unsigned* p) {\n"
        "    unsigned i;\n"
        "    for (i = 0x88008001u; i < 0x88008033u; ++i) *p ^= i;\n"
        "    return *p;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-O4,s", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "induction_products.c"
    source.write_text(
        "extern void observe(unsigned);\n"
        "void variable_stride(unsigned count, unsigned stride) {\n"
        "    for (; count > 0; --count) observe(stride * (count - 1));\n"
        "}\n"
        "void fixed_stride(unsigned count) {\n"
        "    for (; count > 0; --count) observe(13 * (count - 2));\n"
        "}\n"
        "void reversed_subtraction(unsigned count, unsigned stride) {\n"
        "    for (; count > 0; --count) observe(stride * (3 - count));\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "block_scopes.c"
    source.write_text(
        "int outer_local(int x) { int value = x + 1; return value; }\n"
        "int nested_local(int x) {\n"
        "    if (x) { int value = x + 1; x = value; }\n"
        "    return x;\n"
        "}\n"
        "int nested_static(int x) {\n"
        "    if (x) { static int value = 4; x += value; }\n"
        "    return x;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opts in (("-O0",), ("-O1",), ("-O4,p", "-pragma", "global_optimizer off")):
        run(compiler, *FLAGS, *opts, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {' '.join(opts)}"] = obj.read_bytes()

    source = work / "count_split_loop.c"
    source.write_text(
        "int count_split_loop(volatile int* p) {\n"
        "    int i;\n"
        "    for (i = 0; i < 70001; ++i) *p ^= i;\n"
        "    return *p;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-O4,s", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "unsigned_wraparound.c"
    source.write_text(
        "unsigned loop(volatile unsigned* p) {\n"
        "    unsigned i;\n"
        "    for (i = 0xffff8000u; i <= 0xffffffffu; ++i) *p ^= i;\n"
        "    return *p;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for opt in ("-O0", "-O1", "-O4,s", "-O4,p"):
        run(compiler, *FLAGS, opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {opt}"] = section.read_bytes()

    source = work / "integer_limits.c"
    source.write_text(
        "int unsigned_max(unsigned value) { return value <= 0xffffffffu; }\n"
        "int signed_max(int value) { return 2147483647 < value; }\n"
        "int signed_min(int value) { return value >= (-2147483647 - 1); }\n"
        "int signed_min_reversed(int value) { return (-2147483647 - 1) > value; }\n"
        "int volatile_max(volatile unsigned* value) { return *value <= 0xffffffffu; }\n"
        "#pragma stdc_fenv_access on\n"
        "#pragma stdc_fp_contract off\n"
        "int float_divide(float a, float b) { return (unsigned)(a / b) <= 0xffffffffu; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for opt in ("-O0", "-O1"):
        run(compiler, *FLAGS, opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {opt}"] = section.read_bytes()

    source = work / "float_propagation.c"
    source.write_text(
        """\
double staged(double x, double y) {
    double a = x * 5.0;
    double b = y * 7.0;
    double c = a * b;
    return c + b;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(compiler, *FLAGS, "-fp_contract", "off", "-Cpp_exceptions", "off", "-O4,p", "-c", source, "-o", obj, cwd=work)
    for name in (".text", ".sdata2"):
        section = source.with_suffix(name)
        run(objcopy, f"--dump-section={name}={section}", obj, cwd=work)
        results[f"{source.name} {name}"] = section.read_bytes()

    source = work / "float_environment.c"
    source.write_text(
        "int defaults(void) { return 2 * __option(stdc_fenv_access) + __option(stdc_fp_contract); }\n"
        "#pragma stdc_fp_contract on\n"
        "int contract(void) { return 2 * __option(stdc_fenv_access) + __option(stdc_fp_contract); }\n"
        "#pragma stdc_fenv_access off\n"
        "int relaxed(void) { return 2 * __option(stdc_fenv_access) + __option(stdc_fp_contract); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-fp_contract", "off", "-O2", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "self_assignment.c"
    source.write_text(
        """\
extern double low(double), high(double);

double classify(int x) {
    x &= 0x7fffffff;
    if (x <= 0x3fe921fb)
        return low(0.0);
    if (x >= 0x7ff00000)
        return high(1.0);
    return 2.0;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".strtab")
    run(compiler, *FLAGS, "-fp_contract", "off", "-Cpp_exceptions", "off", "-O4,p", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.strtab={section}", obj, cwd=work)
    results[f"{source.name} .strtab"] = section.read_bytes()

    source = work / "pointer_balance.c"
    source.write_text(
        """\
char* index_product(char* base, int offset, int index, int scale) {
    return base + offset + index * scale;
}

char* loaded_base(char** base, int offset, int index) {
    return *base + offset + index;
}

char* loaded_offset(char* base, const int* offset, int index) {
    return base + *offset + index;
}

#pragma ppc_dont_balance_exp on

char* unbalanced_offset(char* base, const int* offset, int index) {
    return base + *offset + index;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-O2", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "precolor_move.c"
    source.write_text(
        """\
int update(signed char* p, int kind) {
    signed char* slot = p;
    int value = *p;
    int limit = 8;
    if (kind == 3) {
        slot = p + 1;
        value = p[1];
    }
    if (kind == 2)
        limit = 7;
    if (value < limit)
        *slot = value + 1;
    return value;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-Cpp_exceptions", "off", "-O4,p", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    for name, value_type, processor, extra_flags, statement, repeats in (
        ("integer", "int", "gekko", (), "hot += *out;", 24),
        ("floating", "double", "gekko", (), "hot += *out;", 24),
        ("vector", "vector unsigned int", "7400", ("-vector", "on"), "*out = hot;", 48),
        ("toc", "int", "gekko", ("-sdata", "0", "-sdata2", "0"), "hot += first; hot += second;", 12),
    ):
        source = work / f"local_usage_{name}.c"
        source.write_text(
            f"typedef {value_type} RegValue;\n"
            "extern volatile int first, second;\n"
            "void store(RegValue a, RegValue b, volatile RegValue* out) {\n"
            "    RegValue hot = a;\n"
            "    RegValue warm = b;\n"
            + f"    {statement}\n" * repeats
            + "    *out = warm;\n"
            "    *out = hot;\n"
            "}\n",
            encoding="ascii",
        )
        obj = source.with_suffix(".o")
        section = source.with_suffix(".text")
        run(
            compiler, "-nodefaults", "-proc", processor, "-fp", "hard", "-Cpp_exceptions", "off",
            *extra_flags, "-O0", "-c", source, "-o", obj, cwd=work,
        )
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[source.name] = section.read_bytes()

    source = work / "relocated_index.c"
    source.write_text(
        """\
extern unsigned char records[];
extern void publish(void* dest, const void* data, unsigned size);

void slice(void* dest, unsigned index) {
    publish(dest, records + index, 256);
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(compiler, *FLAGS, "-Cpp_exceptions", "off", "-O0", "-c", source, "-o", obj, cwd=work)
    results[source.name] = obj.read_bytes()

    source = work / "compare_zero.c"
    source.write_text(
        "extern void a(void); extern void b(void);\n"
        "void equal(unsigned x) { if (x == 0) a(); else b(); }\n"
        "void greater(unsigned x) { if (x++ > 0) a(); else b(); }\n"
        "void at_most(unsigned x) { if (x <= 0) a(); else b(); }\n"
        "void nonzero(unsigned x) { if (x == 32768) a(); else b(); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-O0", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "record_zero.c"
    source.write_text(
        """\
int decode(unsigned short *dst, const signed char *src, unsigned n) {
    if (!src)
        return 0;
    if (!n)
        return -1;
    if (dst)
        *dst = (unsigned char)*src;
    if (!*src)
        return 0;
    return 1;
}

extern int yes(void), no(void);

int added_zero(unsigned x) {
    if ((x + 40) != 0)
        return yes();
    return no();
}

int compare_nonzero(const signed char *p) {
    if (*p == 11)
        return yes();
    return no();
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "spr_aliases.c"
    source.write_text(
        "asm void transfer(void) {\n"
        "    nofralloc\n"
        "    mtgqr2 r3\n"
        "    mtgqr5 r4\n"
        "    mfgqr2 r5\n"
        "    mfgqr5 r6\n"
        "    blr\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    run(compiler, *FLAGS, "-O0", "-c", source, "-o", obj, cwd=work)
    run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
    results[source.name] = section.read_bytes()

    source = work / "asm_branch_to_next.c"
    source.write_text(
        """\
asm int branch_to_next(void) {
    nofralloc
    li r3, 7
    b done
done:
    blr
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O3", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "tail_calls.c"
    source.write_text(
        "extern int step(int); extern int other(int);\n"
        "int direct(int x) { return step(x); }\n"
        "int indirect(int (*f)(int), int x) { return f(x); }\n"
        "int multiple(int x) { if (x) return step(x); return other(x); }\n"
        "int conditional(int x) { if (x) return step(x); return 0; }\n"
        "int nested(int x) { return step(other(x)); }\n"
        "int framed(int x) { volatile int y = x; return step(y); }\n"
        "int asm_call(int x) { asm { bl other } return step(x); }\n"
        "#pragma optimization_level 1\n"
        "int level_one(int x) { return step(x); }\n"
        "#pragma optimize_tail_call off\n"
        "#pragma optimization_level 2\n"
        "int disabled(int x) { return step(x); }\n"
        "#pragma optimize_tail_call on\n"
        "int enabled(int x) { return step(x); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for opt in ("-O0", "-O2", "-O4,p"):
        run(compiler, *FLAGS, opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        relocations = run(objdump, "-r", obj, cwd=work).split("RELOCATION RECORDS FOR [")
        text_relocations = next((record for record in relocations if record.startswith(".text]:")), "")
        results[f"{source.name} {opt}"] = (section.read_bytes(), text_relocations.strip())

    source = work / "symbol_order.c"
    source.write_text(
        "extern int step(int value);\n"
        "extern int unnamed(int);\n"
        "int first(int x) { return step(x); }\n"
        "int second(int x) { return step(x); }\n"
        "int leaf(int x) { return x + 1; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt, exceptions in (("-O0", "on"), ("-O2", "off")):
        run(compiler, *FLAGS, "-Cpp_exceptions", exceptions, opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "address_register.c"
    source.write_text(
        "extern void cleanup(void);\n"
        "extern void (*exit_hook)(void);\n"
        "void install(void) { exit_hook = cleanup; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O1", "-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", "-sdata", "8", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "restore_order.c"
    source.write_text(
        "extern unsigned int lock(void);\n"
        "void save(unsigned int *p) { *p = lock(); }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(compiler, *FLAGS, "-Cpp_exceptions", "off", "-O4,p", "-c", source, "-o", obj, cwd=work)
    results[source.name] = obj.read_bytes()

    source = work / "rotate_masks.c"
    source.write_text(
        "typedef union { double value; unsigned int words[2]; } Bits;\n"
        "double with_sign(double x, double y) {\n"
        "    Bits a, b;\n"
        "    a.value = x; b.value = y;\n"
        "    a.words[0] = (a.words[0] & 0x7fffffffu) | (b.words[0] & 0x80000000u);\n"
        "    return a.value;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    for side, expression in (("right", "x & 0xff00ff00u"), ("left", "0xff00ff00u & x")):
        source = work / f"split_mask_{side}.c"
        source.write_text(f"unsigned int split(unsigned int x) {{ return {expression}; }}\n", encoding="ascii")
        obj = source.with_suffix(".o")
        for opt in ("-O0", "-O2", "-O4,p"):
            run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
            results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "bitfield_access.c"
    source.write_text(
        "struct Fields {\n"
        "    unsigned open : 2;\n"
        "    unsigned io : 3;\n"
        "    unsigned buffer : 2;\n"
        "    unsigned kind : 3;\n"
        "    unsigned orientation : 2;\n"
        "    unsigned binary : 1;\n"
        "};\n"
        "unsigned kind(struct Fields *p) { return p->kind; }\n"
        "unsigned orientation(struct Fields *p) { return p->orientation; }\n"
        "void put(struct Fields *p, unsigned v) { p->orientation = v; }\n"
        "unsigned both(struct Fields *p) { return p->kind + p->orientation; }\n"
        "unsigned volatile_read(volatile struct Fields *p) { return p->orientation; }\n"
        "void volatile_write(volatile struct Fields *p, unsigned v) { p->orientation = v; }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", "-align", "powerpc", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "zero_width_bitfields.c"
    obj = source.with_suffix(".o")
    for alignment in ("powerpc", "packed", "mac68k", "mac68k4byte"):
        source.write_text(
            f"#pragma options align={alignment}\n"
            "struct Narrow { unsigned int bits : 1; unsigned char : 0; char tail; };\n"
            "struct Wide { unsigned short bits : 3; unsigned long : 0; char tail; };\n"
            "unsigned int layout[] = {\n"
            "    sizeof(struct Narrow), (unsigned long)&((struct Narrow *)0)->tail,\n"
            "    sizeof(struct Wide), (unsigned long)&((struct Wide *)0)->tail\n"
            "};\n",
            encoding="ascii",
        )
        run(compiler, *FLAGS, "-O0", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {alignment}"] = obj.read_bytes()

    source = work / "small_switch.c"
    source.write_text(
        "extern int a(void), b(void), c(void), d(void), next(void);\n"
        "int ordered(int v) {\n"
        "    switch (v) { case 9: return a(); case -3: return b();"
        " case 7: return c(); default: return d(); }\n"
        "}\n"
        "int side(void) {\n"
        "    switch (next()) { case 9: return a(); case -3: return b();"
        " case 7: return c(); default: return d(); }\n"
        "}\n"
        "int unsigned_case(unsigned v) {\n"
        "    switch (v) { case 0: case 1: return a();"
        " case 0xfffffffeu: case 0xffffffffu: return b(); default: return c(); }\n"
        "}\n"
        "int one(int v) { switch (v) { case 9: return a(); default: return d(); } }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "switch_declaration.cpp"
    source.write_text(
        "extern int read_value();\n"
        "extern void effect();\n"
        "void local(int n) { switch (n) { default: int a = 1; case 2: break; } }\n"
        "void empty(int n) { switch (n) { default: case 2: break; } }\n"
        "void call() { switch (read_value()) { default: case 2: break; } }\n"
        "void load(volatile int* p) { switch (*p) { default: case 2: break; } }\n"
        "void branch(int n) { switch (n) { default: effect(); case 2: break; } }\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opts in (
        ("-O0",), ("-O1",), ("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",),
        ("-O0", "-pragma", "opt_optimizecontrolflow on"),
        ("-O2", "-pragma", "opt_dead_assignments off"),
    ):
        run(compiler, *FLAGS, "-Cpp_exceptions", "on", "-func_align", "4",
            *opts, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {' '.join(opts)}"] = obj.read_bytes()

    source = work / "pointer_aliases.c"
    source.write_text(
        """\
struct Words { unsigned high, low; };

unsigned overlapping(struct Words *dst, struct Words *other, unsigned high) {
    dst->high = high;
    other->high = 2;
    return dst->high;
}

unsigned adjacent(struct Words *dst, unsigned high) {
    dst->high = high;
    dst->low = 2;
    return dst->high;
}

unsigned read_only(const struct Words *src, unsigned *dst) {
    unsigned first = src->high;
    *dst = 3;
    return first + src->high;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "cse_reaching_defs.c"
    source.write_text(
        """\
struct Buffer { unsigned length; unsigned other; unsigned cursor; };
extern int first(unsigned x);
extern int second(unsigned x);

unsigned branch_defs(unsigned a, unsigned b, int condition) {
    unsigned result;
    if (condition)
        result = first((a * b) + 1);
    else
        result = second((b * a) + 3);
    return result + a * b;
}

unsigned branches(struct Buffer *p, int condition) {
    unsigned result;
    if (condition)
        result = (p->cursor ^ 3) + p->length;
    else
        result = (p->other ^ 1) + p->length;
    return result * p->length;
}

unsigned after_kill(unsigned a, unsigned b, int condition) {
    unsigned result = (a ^ b) * (a + 1);
    if (condition)
        b = 3;
    return result + (b ^ a);
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "cse_materialization.c"
    source.write_text(
        """\
struct Buffer {
    unsigned length;
    char *start;
    char *cursor;
};

extern void send(char *p, unsigned n);
extern void reset(struct Buffer *p);

unsigned transmit(struct Buffer *p, unsigned n) {
    unsigned available;
    reset(p);
    available = p->length - (p->cursor - p->start);
    if (n <= available)
        send(p->cursor, n);
    else
        send(p->cursor, available);
    return available;
}

int guarded(unsigned cond, const int *p) {
    return cond && *p == (*p + 1) * (*p - 1);
}

unsigned ordered(volatile struct Buffer *p) {
    unsigned available;
    available = p->length - (p->cursor - p->start);
    send(p->cursor, available);
    return available;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "cse_nested_pointer.c"
    source.write_text(
        """\
struct Window {
    const unsigned char *begin;
    const unsigned char *end;
    int position;
};

int count_newlines(const struct Window *window, int undo, int binary) {
    int position = window->position + (window->end - window->begin);

    if (undo >= 3) {
        undo -= 2;
        position -= undo;
    } else {
        undo = 0;
    }

    if (!binary) {
        int count = window->end - window->begin - undo;
        const unsigned char *cursor = window->begin;
        while (count--) {
            if (*cursor++ == '\\n')
                position++;
        }
    }
    return position;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O2", "-O3"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "store_forwarding.c"
    source.write_text(
        """\
struct Fields { unsigned position, initial, remaining, mask; };

void overwrite(struct Fields *p) {
    p->remaining = p->initial;
    p->remaining -= p->position & p->mask;
}

void protected_stores(volatile struct Fields *p) {
    p->remaining = p->initial;
    p->remaining = p->mask;
}

unsigned indexed(unsigned *p, unsigned i, unsigned x, unsigned y) {
    p[i] = x;
    p[i] += y;
    return p[i];
}

unsigned mixed_symbols;
unsigned other_symbol;
unsigned absolute(unsigned x) {
    unsigned a;
    mixed_symbols = x;
    a = other_symbol;
    mixed_symbols = a;
    return a;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O2", "-O4,p"):
        run(compiler, *FLAGS, "-Cpp_exceptions", "off", opt, "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "constant_bitfield_insert.c"
    source.write_text(
        """\
struct Fields { unsigned state : 3; unsigned rest : 29; };
struct Middle { unsigned prefix : 8; unsigned field : 6; unsigned rest : 18; };
struct Low { unsigned prefix : 26; unsigned field : 6; };
struct Wide { unsigned prefix : 2; unsigned field : 20; unsigned rest : 10; };
void clear_top(struct Fields *p) { p->state = 0; }
void clear_middle(struct Middle *p) { p->field = 0; }
void fill_top(struct Fields *p) { p->state = 7; }
void fill_low(struct Low *p) { p->field = 63; }
void fill_wide(struct Wide *p) { p->field = 0xfffff; }
void partial_top(struct Fields *p) { p->state = 2; }
void clear_volatile(volatile struct Fields *p) { p->state = 0; }
unsigned assign_value(struct Fields *p) { return p->state = 0; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "equality_ranges.c"
    source.write_text(
        """\
extern int hit(void);
extern int miss(void);

#define TEST(name, type, expression) \\
    int name(type x) { if (expression) return hit(); return miss(); }
TEST(adjacent, unsigned, x == 1 || x == 2)
TEST(reordered, unsigned, x == 3 || x == 1 || x == 2)
TEST(gap, unsigned, x == 1 || x == 2 || x == 4)
TEST(repeated, unsigned, x == 1 || x == 2 || x == 1)
TEST(narrow, unsigned char, x == 1 || x == 2 || x == 3)
TEST(signed_range, int, x == -2 || x == -1)
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(compiler, *FLAGS, "-O4,p", "-func_align", "4", "-c", source, "-o", obj, cwd=work)
    results[f"{source.name} -O4,p"] = obj.read_bytes()

    source = work / "bounded_intervals.c"
    source.write_text(
        """\
extern int hit(void), miss(void), alternate(void);
int inclusive(int x) {
    if (x >= 3 && x <= 4) return hit();
    return miss();
}
int exclusive(unsigned x) {
    if (x <= 12 && x > 8) return hit();
    return miss();
}
int from_switch(int x) {
    switch (x) {
        case 3: case 4: return hit();
        case 1: return miss();
        default: return alternate();
    }
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for options in (
        ("-O0",),
        ("-O1",),
        ("-O4,p",),
        ("-O4,s",),
        ("-O0", "-pragma", "opt_optimizecontrolflow on"),
        ("-O4,p", "-pragma", "opt_optimizecontrolflow off"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = obj.read_bytes()

    source = work / "narrow_arguments.c"
    source.write_text(
        """\
extern int hit(void), miss(void);
extern int take_int(int x);
int short_bounds(unsigned short x) {
    if (x >= 65000 && x <= 65530) return hit();
    return miss();
}
int forward_short(unsigned short x) { return take_int(x); }
int add_bytes(unsigned char x, unsigned char y) { return x + y; }
int mask_byte(unsigned char x) { return x & 0x7b; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O2", "-O4,p", "-O4,s"):
        run(
            compiler, *FLAGS, opt, "-func_align", "4",
            "-pragma", "opt_optimizecontrolflow off", "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "integer_equality.c"
    source.write_text(
        """\
int byte_eq(unsigned char x) { return x == 1; }
int short_eq(signed short x) { return x == -1; }
int halfword_max(unsigned short x) { return x == 65535; }
int word_eq(unsigned int x) { return x == 65535U; }
int register_eq(int x, int y) { return x == y; }
int byte_ne(unsigned char x) { return x != 1; }
int halfword_ne(unsigned short x) { return x != 65535; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "wide_comparison_mask.c"
    source.write_text(
        """\
int equal_mask(unsigned long long x, unsigned long long y) { return -(x == y); }
int unequal_mask(unsigned long long x) { return -(x != 0x100000000ULL); }
int equal_zero(unsigned long long x) { return x == 0; }
int equal_one(unsigned long long x) { return x == 1; }
int equal_shifted(unsigned long long x) { return x == 0x7abc000000000000ULL; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O1", "-O2", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {opt}"] = obj.read_bytes()

    source = work / "xor_constants.c"
    source.write_text(
        """\
extern void opaque(void);
unsigned from_low(unsigned x) {
    unsigned a = 0x1234;
    opaque();
    return x ^ a;
}
unsigned from_high(unsigned x) {
    unsigned a = 0x789a0000;
    opaque();
    return a ^ x;
}
unsigned from_two_low(void) {
    unsigned a = 0x34, b = 0x9a;
    opaque();
    return a ^ b;
}
unsigned from_two_high(void) {
    unsigned a = 0x12340000, b = 0x789a0000;
    opaque();
    return a ^ b;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(
        compiler, *FLAGS, "-O4,p", "-func_align", "4",
        "-pragma", "opt_propagation off", "-pragma", "opt_common_subs off",
        "-c", source, "-o", obj, cwd=work,
    )
    results[f"{source.name} -O4,p"] = obj.read_bytes()

    source = work / "conditional_assignments.c"
    source.write_text(
        """\
int big_endian;
int check_byte_order(void) {
    unsigned char bytes[4];
    int result = 0;
    big_endian = 1;
    bytes[0] = 0x12;
    bytes[1] = 0x34;
    bytes[2] = 0x56;
    bytes[3] = 0x78;
    if (*(unsigned*)bytes == 0x12345678) {
        big_endian = 1;
    } else if (*(unsigned*)bytes == 0x78563412) {
        big_endian = 0;
    } else {
        result = 1;
    }
    return result;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for options in (
        ("-O0",),
        ("-O1",),
        ("-O2",),
        ("-O3",),
        ("-O4,p",),
        ("-O4,s",),
        ("-O4,p", "-pragma", "opt_optimizecontrolflow off"),
        ("-O4,p", "-pragma", "opt_propagation off"),
        ("-O4,p", "-pragma", "peephole off"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = obj.read_bytes()

    source = work / "address_update.c"
    source.write_text(
        """\
typedef unsigned int Word;
extern void use(void *);
extern Word visit(Word **, Word *);
void run_update(Word *base, Word *p, Word *q) {
    Word **slot = (Word **)((char *)base + (base[1] & ~7u) - 4);
    Word *current = *slot;
    if (current) {
        *current = p[2];
        *slot = q;
        *slot = (Word *)visit(slot, current);
        use(slot);
    } else {
        *slot = p;
    }
}
void run_indexed(char *base, Word offset) {
    char *p = base + offset;
    *(Word *)p &= ~4u;
    *(Word *)(p - 4) = offset;
}
Word run_indexed_live(char *base, Word offset) {
    char *p = base + offset;
    *(Word *)p |= 4u;
    return *(Word *)p + *(Word *)base;
}
double run_indexed_float(char *base, Word offset) {
    double *slot = (double *)(base + offset);
    double value = *slot;
    *slot = value + 1.0;
    use(slot);
    return value;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for options in (("-O2",), ("-O4,p",), ("-O4,p", "-pragma", "opt_common_subs off")):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = obj.read_bytes()

    source = work / "memory_references.c"
    source.write_text(
        """\
void change(double*);
double inspect_then_square(double x) {
    unsigned high = ((unsigned*)&x)[0];
    if (high < 0x3e400000 && (int)x == 0)
        return x;
    return x * x;
}
double inspect_then_mutate(double x) {
    unsigned high = ((unsigned*)&x)[0];
    if (high < 0x3e400000)
        change(&x);
    return x * x;
}
double inspect_then_replace_high_word(double x, unsigned high) {
    ((unsigned*)&x)[0] = high;
    return x * x;
}
double inspect_then_store(double x, unsigned high) {
    if (high)
        ((unsigned*)&x)[0] = high;
    return x * x;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O0",),
        ("-O1",),
        ("-O2",),
        ("-O3",),
        ("-O4,p",),
        ("-O4,s",),
        ("-O4,p", "-pragma", "peephole off"),
        ("-O4,p", "-pragma", "opt_propagation off"),
    ):
        run(
            compiler, *FLAGS, *options, "-fp_contract", "off", "-func_align", "4",
            "-c", source, "-o", obj, cwd=work,
        )
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    floating_operands = (
        (
            "floating_operands",
            "off",
            """
static const double upper = 3.1415926535897931160E+00;
static const double lower = 1.2246467991473531772E-16;
double right_nested(double x) { return upper - (x - lower); }
double left_nested(double x) { return (x - lower) - upper; }
double sum_right(double x) { return upper + (x - lower); }
double sum_left(double x) { return (x - lower) + upper; }
double volatile_nested(double x, const volatile double *v) { return v[0] - (x - v[1]); }
extern double next_double(void);
double right_call(double x) { return upper - (next_double() - x); }
""",
        ),
        (
            "fused_operands",
            "on",
            """
extern double first_call(void), second_call(void), third_call(void);
double fused_no_calls(double x, const volatile double *v) { return v[0] * (x - v[1]) + v[2]; }
float fused_added_complex(float x, const volatile float *v) { return v[0] * v[1] + (x - v[2]); }
double fused_left_call(double x, const volatile double *v) { return first_call() * (x - v[0]) + v[1]; }
double fused_right_call(double x, const volatile double *v) { return v[0] * (x - second_call()) + v[1]; }
double fused_two_calls(double x, const volatile double *v) { return v[0] * (x - second_call()) + third_call(); }
float fused_subtracted(float x, const volatile float *v) { return v[0] - v[1] * (x - v[2]); }
""",
        ),
        (
            "cast_operands",
            "off",
            """
static const double upper = 1.23456789, lower = 0.9536128;
double repeated_integer_casts(int a, int b, unsigned low_a, unsigned low_b, double x) {
    if (((unsigned)a | ((low_a | -low_a) >> 31)) > 0x7ff00000U ||
        ((unsigned)b | ((low_b | -low_b) >> 31)) > 0x7ff00000U)
        return upper;
    if (((unsigned)b | low_b) == 0)
        return lower;
    if (((unsigned)a | low_a) == 0)
        return upper - x;
    return x + lower;
}
double repeated_int_to_float(int a, double x) {
    if ((double)a > x) return (double)a;
    return upper + (double)a;
}
""",
        ),
    )
    for name, contract, body in floating_operands:
        source = work / f"{name}.c"
        source.write_text(body, encoding="ascii")
        obj = source.with_suffix(".o")
        sections = (".text", ".strtab") if name == "cast_operands" else (".text",)
        for options in (
            ("-O0",), ("-O1",), ("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",),
            ("-O4,p", "-pragma", "scheduling off"),
            ("-O4,p", "-pragma", "peephole off"),
        ):
            run(
                compiler, *FLAGS, *options, "-fp_contract", contract, "-func_align", "4",
                "-c", source, "-o", obj, cwd=work,
            )
            for section in sections:
                output = source.with_suffix(section)
                run(objcopy, f"--dump-section={section}={output}", obj, cwd=work)
                results[f"{source.name} {' '.join(options)} {section}"] = output.read_bytes()

    source = work / "size_inlining.c"
    source.write_text(
        """\
extern unsigned visit(unsigned x);

static inline unsigned sum_visits(unsigned count) {
    unsigned i;
    unsigned total = 0;
    for (i = 0; i < count; i++)
        total += visit(i ^ count);
    return total;
}

unsigned count_visits(unsigned n) {
    return sum_visits(n) + 3;
}

unsigned count_extra_visits(unsigned n) {
    return sum_visits(n);
}

static inline unsigned adjust(unsigned x) {
    if (x > 3)
        return x + 1;
    return x * 8;
}

unsigned known_adjustment(void) {
    return adjust(7) + 3;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(
        compiler, *FLAGS, "-Cpp_exceptions", "off", "-O4,s", "-inline", "auto", "-c", source,
        "-o", obj, cwd=work,
    )
    results[source.name] = obj.read_bytes()

    source = work / "inline_saved_uids.c"
    source.write_text(
        """
static inline double abs_value(double x) { return __fabs(x); }
extern double next_value(double);
double division_arg(double y, double x) { return next_value(abs_value(y / x)) + 1.2345678900987; }
double object_arg(double x) { return next_value(abs_value(x)) + 2.3045678076802; }
double nested_call(double x) { return next_value(abs_value(next_value(x))) + 0.7590142272517; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O1", "-O2", "-O3", "-O4,p"):
        run(
            compiler, *FLAGS, opt, "-fp_contract", "off", "-inline", "auto", "-func_align", "4",
            "-c", source, "-o", obj, cwd=work,
        )
        for section in (".text", ".strtab"):
            output = source.with_suffix(section)
            run(objcopy, f"--dump-section={section}={output}", obj, cwd=work)
            results[f"{source.name} {opt} {section}"] = output.read_bytes()

    source = work / "compare_and_shift.c"
    source.write_text(
        """
unsigned folded_shift(int x) { return (unsigned)(x >> 1) << 20; }
int sign_bits(int x) { return (x >> 9) << 4; }
int retained_shift(int x, int* p) {
    int r = x >> 2;
    *p = r;
    return r << 19;
}
int reused_compare(int x, int y) {
    if (x <= 0) {
        if (((x & 0x7fffffff) | y) == 0) return 1;
        if (x < 0) return 2;
    }
    return 3;
}
int different_compare(int x, int y) {
    if (x <= 5) {
        if (y == 0) return 1;
        if ((unsigned)x <= 5) return 2;
    }
    return 3;
}
extern int external(void);
int compare_with_call(int x) {
    if (x <= 0) {
        if (external()) return 1;
        if (x < 0) return 2;
    }
    return 3;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O0",), ("-O1",), ("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",),
        ("-O4,p", "-pragma", "peephole off"), ("-O4,p", "-pragma", "scheduling off"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "while_labels.c"
    source.write_text(
        """
double braced(volatile int* p) {
    while (*p) {
        if (*p == 1) continue;
        --*p;
    }
    return 0.125;
}
double unbraced(volatile int* p) {
    while (*p) --*p;
    return 0.25;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O0", "-O1", "-O2", "-O3", "-O4,p", "-O4,s"):
        run(compiler, *FLAGS, opt, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        for section in (".text", ".strtab"):
            output = source.with_suffix(section)
            run(objcopy, f"--dump-section={section}={output}", obj, cwd=work)
            results[f"{source.name} {opt} {section}"] = output.read_bytes()

    source = work / "volatile_prologue.c"
    source.write_text(
        """
extern double next(double);
double saved_register(double x, double y) {
    union { double value; int words[2]; } bits;
    bits.value = x;
    if ((bits.words[0] & 0x7fffffff) >= 0x3ff00000) return x;
    return next(x) * y;
}
double leaf(double x, double y) {
    union { double value; int words[2]; } bits;
    bits.value = x;
    if ((bits.words[0] & 0x7fffffff) >= 0x3ff00000) return x;
    return (x + y) * x + y;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O4,p",), ("-O4,s",), ("-O4,p", "-pragma", "scheduling off"),
    ):
        run(
            compiler, *FLAGS, *options, "-fp_contract", "off", "-func_align", "4",
            "-c", source, "-o", obj, cwd=work,
        )
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "expression_balance.c"
    source.write_text(
        """
int shifted_sum(int a, int b) { return a + ((b >> 20) - 1023); }
int reversed_sum(int a, int b) { return ((b >> 20) - 1023) + a; }
int plain_sum(int a, int b) { return a + (b - 1023); }
int sum_then_offset(int a, int b) { return (a + b) - 1023; }
int compound_sum(int a, int b) { a += b - 1023; return a; }
int shared_scale(int a, int b) { return (a * 499) + (b * 499); }
int different_scale(int a, int b) { return (a * 499) + (b * 601); }
int large_offset(int a, int b) { return (a + b) + 0x12345678; }
int high_offset(int a, int b) { return (a + b) + 0x12340000; }
extern int next(void);
int with_call(int a) { return a + (next() - 1023); }
int with_load(int a, volatile const int* p) { return a + (*p - 1023); }
unsigned masks(unsigned a, unsigned b) { return a ^ ((b << 4) ^ 0x12); }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O0",), ("-O1",), ("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",),
        ("-O4,p", "-pragma", "ppc_dont_balance_ir on"),
        ("-O4,p", "-pragma", "ppc_dont_balance_exp on"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "integer_factoring.c"
    source.write_text(
        """
int doubled(int x) { return x + x; }
int tripled(int x) { return x + x + x; }
unsigned doubled_with_carry(int x, unsigned low) { return x + x + (low >> 31); }
long long doubled_wide(long long x) { return x + x; }
int common_factor(int x, int a, int b) { return a * x + b * x; }
int through_pointer(const int* p) { return *p + *p; }
int volatile_pair(const volatile int* p) { return *p + *p; }
double floating_point(double x) { return x + x; }
extern int next_value(void);
int with_calls(void) { return next_value() + next_value(); }
int times_five(int x) { return x * 5; }
int times_negative_three(int x) { return x * -3; }
int times_negative_65537(int x) { return x * -65537; }
unsigned times_high_bit(unsigned x) { return x * 0x80000000U; }
int times_signed_minimum(int x) { return x * (-2147483647 - 1); }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O0",), ("-O1",), ("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",),
        ("-O4,p", "-pragma", "peephole off"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    logical_masks = (
        "int sign_or(int a, int b) { return (a || b) ? -1 : 0; }\n"
        "int sign_and(int a, int b) { return (a && b) ? -1 : 0; }\n"
        "int logical(int a, int b) { return (a || b) ? 1 : 0; }\n"
        "int inverse_sign(int a, int b) { return (a || b) ? 0 : -1; }\n"
        "int inverse_logical(int a, int b) { return (a && b) ? 0 : 1; }\n"
        "int sign_call(int a, int (*b)(void)) { return (a || b()) ? -1 : 0; }\n"
        "int inverse_ordered(double a, double b) { return (a < b) ? 0 : -1; }\n"
        "int floating_value(double a, double b) { return a >= b; }\n"
        "int inverse_float(double a, double b) { return !(a < b); }\n"
        "long long wide_mask(int a, int b) { return (a || b) ? -1LL : 0LL; }\n"
    )
    for suffix, flags, extra in (
        (".c", (), ""),
        (
            ".cpp", ("-bool", "on"),
            "int sign_bool(bool a) { return a ? -1 : 0; }\n"
            "int inverse_bool(bool a) { return a ? 0 : -1; }\n",
        ),
    ):
        source = work / f"logical_masks{suffix}"
        source.write_text(logical_masks + extra, encoding="ascii")
        obj = source.with_suffix(".o")
        section = source.with_suffix(".text")
        for options in (
            ("-O0",), ("-O1",), ("-O2",), ("-O4,p",), ("-O4,s",),
            ("-O4,p", "-pragma", "scheduling off"),
        ):
            run(compiler, *FLAGS, *flags, *options, "-c", source, "-o", obj, cwd=work)
            run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
            results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "conditional_assignment.cpp"
    source.write_text(
        """
extern int next_value();
extern int first, second;

void choose_global() {
    if (next_value() & 0x4000)
        first = 0;
    else
        second = 0;
}

int choose_local(int a, int b) {
    if (next_value() & 0x4000)
        a = 0;
    else
        b = 0;
    return a + 2 * b;
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (("-O0",), ("-O2",), ("-O4,p",), ("-O4,s",)):
        run(compiler, *FLAGS, *options, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "conditional_masks.cpp"
    source.write_text(
        """
extern int next_value();

int choose(bool a, bool b) {
    if (a && b) {
        if (next_value() & 0x4000)
            a = false;
        else
            b = false;
    }
    if (a) return 1;
    if (b) return 2;
    return 0;
}

unsigned contiguous_low(int x) { return x ? 2u : 0u; }
unsigned disjoint_low(int x) { return x ? 5u : 0u; }
unsigned contiguous_high(int x) { return x ? 0x40000u : 0u; }
unsigned complementary_mask(int x) { return x ? 0u : 5u; }
unsigned signed_immediate(int x) { return x ? -2u : 0u; }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (("-O2",), ("-O3",), ("-O4,p",), ("-O4,s",)):
        run(compiler, *FLAGS, "-bool", "on", *options, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "destructor_vtables.cpp"
    source.write_text(
        """
#pragma force_active on
extern void free_mem(void*);
inline void operator delete(void* p) throw() { free_mem(p); }
extern int counter;
extern void marker();
struct Base { virtual ~Base() {} virtual void foo(); };
struct Derived : Base { ~Derived() {} virtual void foo(); };
struct MoreDerived : Derived { ~MoreDerived() {} virtual void foo(); };
struct Implicit : Derived { virtual void foo(); };
struct Marker : Base { ~Marker() { marker(); } };
struct Counter : Base { ~Counter() { ++counter; } };
struct Fields : Base { int field; ~Fields() { field = 42; } };
extern void observe(Base*);
struct Escape : Base { ~Escape() { observe(this); } };
struct Dispatch : Base { ~Dispatch() { foo(); } virtual void foo(); };
struct Qualified : Base { ~Qualified() { Base::foo(); } };
struct Member { ~Member() { marker(); } };
struct WithMember : Base { Member member; ~WithMember() {} };
struct WithVirtual : virtual Base { ~WithVirtual() {} virtual void foo(); };
struct Other { virtual ~Other() {} virtual void bar(); };
struct Multiple : Base, Other { ~Multiple() {} virtual void foo(); };
void Base::foo() {}
void Derived::foo() {}
void MoreDerived::foo() {}
void Implicit::foo() {}
void Dispatch::foo() {}
void WithVirtual::foo() {}
void Other::bar() {}
void Multiple::foo() {}
void construct() {
    observe(new MoreDerived);
    observe(new Implicit);
    observe(new Marker);
    observe(new Counter);
    observe(new Fields);
    observe(new Escape);
    observe(new Dispatch);
    observe(new Qualified);
    observe(new WithMember);
    observe(new WithVirtual);
    observe(new Multiple);
}
""",
        encoding="ascii",
    )
    obj, section = source.with_suffix(".o"), source.with_suffix(".text")
    for opt in ("-O0", "-O4,p"):
        run(compiler, *FLAGS, "-bool", "on", "-func_align", "4", opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        text = section.read_bytes()
        symbols = run(nm, "--defined-only", "--format=posix", "--print-size", obj, cwd=work)
        relocations = []
        for line in run(objdump, "--reloc", "--section=.rela.text", obj, cwd=work).splitlines():
            fields = line.split(maxsplit=2)
            if len(fields) == 3 and fields[1].startswith("R_PPC_"):
                relocations.append((int(fields[0], 16), fields[1], fields[2]))
        destructors = [line.split() for line in symbols.splitlines() if line.startswith("__dt__")]
        if len(destructors) != 15:
            raise ValueError(f"{compiler}: expected 15 destructors with {opt}, found {len(destructors)}")
        for name, _, address, size in destructors:
            start = int(address, 16)
            end = start + int(size, 16)
            fixups = tuple(
                (offset - start, kind, target)
                for offset, kind, target in relocations if start <= offset < end
            )
            if opt == "-O0":
                result = tuple(target for _, _, target in fixups if target.startswith("__vt__"))
            else:
                result = (text[start:end], fixups)
            results[f"{source.name} {opt} {name}"] = result

    unwind_source = """
extern void might_throw();
struct Base { int field; ~Base(); };
struct Middle { int field; ~Middle(); };
struct Extra { int field; ~Extra(); };
struct First { int field; ~First(); };
struct Second { int field; ~Second(); };
struct Third { int field; ~Third(); };
struct Fourth { int field; ~Fourth(); };
struct Outer : BASES {
    First first;
    Second array[2];
    Third third;
    Fourth last;
    ~Outer() { might_throw(); }
};
void destroy(Outer* object) { object->~Outer(); }
"""
    unwind_cases = {
        "direct": unwind_source.replace("BASES", "Base, Middle"),
        "virtual": unwind_source.replace("BASES", "virtual Base, Middle, virtual Extra"),
        "nested": """
extern void might_throw();
struct Base { int field; ~Base(); };
struct Member { int field; ~Member(); };
struct Local { int field; Local(); ~Local(); };
struct Outer : Base { Member member; ~Outer(); };
Outer::~Outer() {
    if (field < 0)
        return;
    Local a;
    if (field > 0) {
        Local b;
        might_throw();
    }
    might_throw();
}
""",
    }
    for suffix, fixture in unwind_cases.items():
        source = work / f"destructor_unwind_{suffix}.cpp"
        source.write_text(fixture, encoding="ascii")
        obj = source.with_suffix(".o")
        flags = (*FLAGS, "-func_align", "4", "-O4,p")
        run(compiler, *flags, "-Cpp_exceptions", "on", "-c", source, "-o", obj, cwd=work)
        sections = (".text", "extab", "extabindex")
        dumps = [source.with_suffix(f".{name.lstrip('.')}") for name in sections]
        run(objcopy, *(f"--dump-section={name}={path}" for name, path in zip(sections, dumps)),
            obj, cwd=work)
        fixups = tuple(
            line.split() for line in run(objdump, "--reloc", "--section=.relaextab", obj, cwd=work).splitlines()
            if "R_PPC_ADDR32" in line
        )
        if not fixups:
            raise ValueError(f"{compiler}: {source.name} has no destructor unwind relocations")
        results[source.name] = (tuple(path.read_bytes() for path in dumps), fixups)
        if suffix == "nested":
            results[f"{source.name} object"] = obj.read_bytes()
        run(compiler, *flags, "-Cpp_exceptions", "off", "-c", source, "-o", obj, cwd=work)
        headers = run(objdump, "--section-headers", obj, cwd=work)
        if any(name in headers.split() for name in sections[1:]):
            raise ValueError(f"{compiler}: {source.name} emitted disabled unwind tables")
        if suffix == "nested":
            run(compiler, *FLAGS, "-func_align", "4", "-O4,s", "-inline", "off",
                "-Cpp_exceptions", "on", "-c", source, "-o", obj, cwd=work)
            results[f"{source.name} size without inlining"] = obj.read_bytes()

    source = work / "call_unwind.cpp"
    source.write_text(
        """\
extern void plain();
extern void declared_nothrow() throw();
extern "C" void c_linkage();
struct V { virtual void f() throw(); void g() throw(); };
struct Guard { Guard(); ~Guard(); };
void known_nothrow() {}
void external_stop(V* v) { Guard guard; declared_nothrow(); }
void external_call(V* v) { Guard guard; plain(); }
void external_c(V* v) { Guard guard; c_linkage(); }
void inline_known(V* v) { Guard guard; known_nothrow(); }
void virtual_call(V* v) { Guard guard; v->f(); }
void devirtual_call(V* v) { Guard guard; v->V::f(); }
void member_call(V* v) { Guard guard; v->g(); }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for opt in ("-O4,p", "-O4,s"):
        for pragma in ((), ("-pragma", "optEH off"), ("-pragma", "optEH2 on")):
            run(compiler, *FLAGS, opt, "-inline", "off", "-func_align", "4",
                "-Cpp_exceptions", "on", *pragma, "-c", source, "-o", obj, cwd=work)
            results[f"{source.name} {' '.join((opt, *pragma))}"] = obj.read_bytes()

    source = work / "file_ipa_catch_all.cpp"
    source.write_text(
        "extern void may_throw();\n"
        "int test() {\n"
        "    try { may_throw(); } catch (...) { return 1; }\n"
        "    return 0;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    run(compiler, *FLAGS, "-O4,p", "-ipa", "file", "-Cpp_exceptions", "on", "-c", source,
        "-o", obj, cwd=work)
    results[source.name] = obj.read_bytes()

    source = work / "unrecognized_pragma.cpp"
    source.write_text(
        "#pragma warn_illpragma on\n"
        "#pragma optEH2 on\n"
        "int identity(int x) { return x; }\n",
        encoding="ascii",
    )
    output = run(compiler, *FLAGS, "-msgstyle", "parseable", "-c", source.name,
                 "-o", source.with_suffix(".o"), cwd=work)
    lines = output.splitlines()
    if [line for line in lines if line.startswith(">")] != [">(10186) illegal #pragma"]:
        raise ValueError(f"{compiler}: unexpected pragma diagnostic\n{output}")
    results[source.name] = lines[1:]

    duplicate_typedefs = {
        "duplicate_typedef.c": (
            "typedef int Count;\n"
            "typedef int Count;\n"
            "typedef void (*Callback)(Count);\n"
            "typedef void (*Callback)(int x);\n"
            "int identity(Count value) { return value; }\n",
            ("Count", "Callback"),
        ),
        "duplicate_typedef.cpp": (
            "typedef int Count;\n"
            "typedef int Count;\n"
            "struct Holder { typedef int Value; typedef int Value; };\n"
            "int identity(Count value) { return value; }\n",
            ("Value",),
        ),
    }
    strict = ("-pragma", "ANSI_strict on")
    for name, (contents, aliases) in duplicate_typedefs.items():
        source = work / name
        source.write_text(contents, encoding="ascii")
        messages = [f">(10122) identifier '{alias}' redeclared" for alias in aliases]
        cases = [
            ((), messages, 0),
            (("-w", "off"), [], 0),
            (strict + ("-w", "off"), messages, 1),
        ]
        if source.suffix == ".c":
            cases.append((("-pragma", "cpp_extensions on"), [], 0))
        for flags, expected, status in cases:
            process = subprocess.run(
                (compiler, *FLAGS, *flags, "-msgstyle", "parseable", "-c", source.name,
                 "-o", source.with_suffix(".o")),
                cwd=work, capture_output=True, text=True, timeout=30,
            )
            output = process.stdout + process.stderr
            if process.returncode != status or [
                line for line in output.splitlines() if line.startswith(">")
            ] != expected:
                raise ValueError(f"{compiler}: unexpected typedef diagnostic\n{output}")
            results[f"{name} {flags}"] = (process.stdout, process.stderr)

    source = work / "conflicting_typedef.c"
    source.write_text("typedef int Count;\ntypedef unsigned Count;\n", encoding="ascii")
    for processor, prefix in (("gekko", "(10249) "), ("750", "")):
        process = subprocess.run(
            (compiler, "-nodefaults", "-proc", processor, "-fp", "hard", "-msgstyle",
             "parseable", "-c", source.name, "-o", source.with_suffix(".o")),
            cwd=work, capture_output=True, text=True, timeout=30,
        )
        output = process.stdout + process.stderr
        if process.returncode != 1 or f">{prefix}identifier 'Count' redeclared" not in output:
            raise ValueError(f"{compiler}: unexpected conflicting typedef diagnostic\n{output}")
        results[f"{source.name} {processor}"] = (process.stdout, process.stderr)

    source = work / "strict_diagnostics.cpp"
    source.write_text(
        "#pragma warn_illpragma on\n"
        "#pragma not_a_pragma on\n"
        "void jump() { goto end; int initialized = 1; end:; }\n",
        encoding="ascii",
    )
    process = subprocess.run(
        (compiler, *FLAGS, *strict, "-msgstyle", "parseable", "-c", source.name,
         "-o", source.with_suffix(".o")),
        cwd=work, capture_output=True, text=True, timeout=30,
    )
    output = process.stdout + process.stderr
    if process.returncode != 1 or [
        line for line in output.splitlines() if line.startswith(">")
    ] != [
        ">(10186) illegal #pragma",
        ">(10211) illegal jump past 'initialized' declaration/initializer",
    ]:
        raise ValueError(f"{compiler}: unexpected strict-mode diagnostic\n{output}")
    results[source.name] = (process.stdout, process.stderr)
    results.update(check_void_delete_diagnostics(compiler, work))
    results.update(check_template_actions(compiler, objcopy, nm, work))
    results.update(check_reference_cast_diagnostics(compiler, objcopy, work))
    results.update(check_return_diagnostics(compiler, work))
    results.update(check_preprocess_output(compiler, work))
    results.update(check_c99_language(compiler, objcopy, work))
    results.update(check_backend_diagnostics(compiler, objcopy, work))
    results.update(check_reference_initializers(compiler, objcopy, work))

    source = work / "jump_warnings.cpp"
    jump_warnings = (
        "#define GO(label) goto label\n"
        "struct Local { Local(); ~Local(); };\n"
        "extern void may_throw();\n"
        "void jump() {\n"
        "    int first = 1;\n"
        "\t  GO(out);\n"
        "    int second = 2; Local local;\n"
        "out:;\n"
        "}\n"
        "void no_warning() { goto out; int skipped; out:; }\n"
        "void branch(int n) { " + " " * 72 +
        "switch (n) { default: int value = 0; case 2: break; } }\n"
        "void handler() {\n"
        "    goto inside;\n"
        "    try { may_throw(); } catch (int) { inside: may_throw(); }\n"
        "}\n"
    )
    expected = [
        f">(10211) illegal jump past '{name}' declaration/initializer"
        for name in ("local", "local", "second", "first", "value")
    ] + [">(10577) illegal jump into try/catch block"]
    for eol in ("\n", "\r\n"):
        source.write_bytes(jump_warnings.replace("\n", eol).encode("ascii"))
        output = run(compiler, *FLAGS, "-O4,p", "-Cpp_exceptions", "on", "-msgstyle", "parseable",
                     "-c", source.name, "-o", source.with_suffix(".o"), cwd=work)
        lines = output.splitlines()
        if [line for line in lines if line.startswith(">")] != expected:
            raise ValueError(f"{compiler}: unexpected jump diagnostics\n{output}")
        results[f"{source.name} {eol!r}"] = [
            "Compiler|Warning" if line.endswith("|Compiler|Warning") else line for line in lines
        ]

    source = work / "immediate_reuse.c"
    source.write_text(
        """
typedef struct File {
    int handle;
    unsigned mode:7;
    unsigned flags:3;
    unsigned rest:22;
    int offset;
} File;
extern int process(File*);
extern void use(int);
extern void barrier(void);
int clear_for_close(File* f) {
    int x = process(f);
    f->flags = 0;
    f->handle = 0;
    if (f->offset) use(x);
    return x;
}
void clear_for_flush(File* f, int offset) {
    f->mode = 0;
    f->offset = offset;
    f->handle = 0;
}
void clear_with_call(File* f) { f->flags = 0; barrier(); f->handle = 0; }
void clear_with_volatile(File* f, volatile int* p) { f->flags = 0; *p = 1; f->handle = 0; }
void clear_other_bit_pattern(File* f) { f->flags = 3; f->handle = 3; }
void use_lis(File* f) { f->offset = 0x12340000; f->handle = 0x12340000; }
void overwrite(File* f, int a) { f->flags = 0; f->offset = a; f->handle = 0; use(7); }
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for options in (
        ("-O0",), ("-O2",), ("-O4,p",), ("-O4,s",), ("-O4,p", "-pragma", "scheduling off"),
    ):
        run(compiler, *FLAGS, *options, "-func_align", "4", "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {' '.join(options)}"] = section.read_bytes()

    source = work / "temporary_immediate_reuse.cpp"
    source.write_text(
        """\
struct Values {
    float extra, x, y, z, w;
    float a() const { return x; }
    float b() const { return y; }
    float c() const { return z; }
    float d() const { return w; }
};
bool same(const Values& a, const Values& b) {
    return a.a() == b.a() && a.b() == b.b() && a.c() == b.c() && a.d() == b.d();
}
""",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    section = source.with_suffix(".text")
    for opt in ("-O2,p", "-O4,p"):
        run(compiler, *FLAGS, opt, "-ipa", "file", "-inline", "auto",
            "-Cpp_exceptions", "off", "-fp_contract", "off", "-func_align", "4",
            "-c", source.name, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{source.name} {opt}"] = section.read_bytes()

    source = work / "local_declaration_weight.c"
    source.write_text(
        "void declaration_weight(int x) {\n"
        "    int local = x + 7;\n"
        "}\n",
        encoding="ascii",
    )
    obj = source.with_suffix(".o")
    for option in ("on", "off"):
        run(
            compiler, *FLAGS, "-O0", "-pragma", f"global_optimizer {option}",
            "-c", source, "-o", obj, cwd=work,
        )
        results[f"{source.name} global_optimizer {option}"] = obj.read_bytes()

    zero_trip = (
        "int f(volatile int* p) {\n"
        "    int i;\n"
        "    for (i = 70001; i < 70000; ++i) *p ^= i;\n"
        "    return *p;\n"
        "}\n"
    )
    for name, body, opt in (
        ("zero_trip.c", zero_trip, "-O4,s"),
        ("empty_loop_pass_off.c", "#pragma opt_removeemptyloops off\n" + zero_trip, "-O4,s"),
        ("empty_loop_pass_on.c", "#pragma opt_removeemptyloops on\n" + zero_trip, "-O2"),
        (
            "one_trip.c",
            "int f(volatile int* p) {\n"
            "    int i = 70000;\n"
            "    do { *p ^= i; ++i; } while (i < 70001);\n"
            "    return *p;\n"
            "}\n",
            "-O4,s",
        ),
        (
            "empty_counted.c",
            "int f(void) {\n"
            "    int i;\n"
            "    for (i = -70001; i < -64000; i += 3) {}\n"
            "    return i;\n"
            "}\n",
            "-O4,s",
        ),
        (
            "zero_trip_volatile_test.c",
            "int f(volatile int* p) {\n"
            "    int i;\n"
            "    for (i = 70001; (*p, i < 70000); ++i) *p ^= i;\n"
            "    return *p;\n"
            "}\n",
            "-O4,s",
        ),
    ):
        source = work / name
        source.write_text(body, encoding="ascii")
        obj = source.with_suffix(".o")
        section = source.with_suffix(".text")
        run(compiler, *FLAGS, opt, "-c", source, "-o", obj, cwd=work)
        run(objcopy, f"--dump-section=.text={section}", obj, cwd=work)
        results[f"{name} {opt}"] = section.read_bytes()
    check_pch(compiler, objcopy, work)
    check_source_encoding(compiler, objcopy, work)
    results.update(check_unconditional_branch_ranges(compiler, objcopy, work))
    results.update(check_auto_aggregate_initialization(compiler, work))
    results.update(check_unrolled_loop_guard(compiler, work))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=COMPILER)
    parser.add_argument("--reference", type=Path, help="compare instruction bytes with stock Wii 1.0RC1")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--objdump", default="llvm-objdump")
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="wii-rc1-") as directory:
            work = Path(directory)
            compiler = args.compiler.resolve(strict=True)
            results = check(compiler, args.objcopy, args.nm, args.objdump, work)
            if args.reference:
                stock = args.reference.resolve(strict=True)
                reference = check(stock, args.objcopy, args.nm, args.objdump, work)
                for name in results:
                    if results[name] != reference[name]:
                        raise ValueError(f"{name}: output differs from stock RC1")
                check_stock_pch_macros(compiler, stock, work)
                check_pch_include_state(compiler, stock, args.objcopy, work)
                check_stock_pch_declarations(compiler, stock, args.objcopy, args.nm, work)
                check_stock_pch_static_data(compiler, stock, args.objcopy, args.nm, args.objdump, work)
                check_stock_pch_functions(compiler, stock, args.objcopy, args.nm, work)
                check_stock_pch_inline_locals(compiler, stock, args.objcopy, work)
                check_stock_spe_pch(compiler, stock, args.objcopy, work)
                check_aligned_pch(compiler, stock, args.objcopy, work)
                check_stock_pch_initializers(compiler, stock, args.objcopy, args.nm, args.objdump, work)
                check_stock_pch_templates(compiler, stock, args.objcopy, args.nm, work)
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"{error}\n{error.output}")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"{error}\n")
    print("Wii 1.0RC1 source compiler smoke checks passed")


if __name__ == "__main__":
    main()
