#!/usr/bin/env python3
"""python tools/oracle.py [options] SOURCE [COMPILER FLAGS...]: why the compiler assigned the registers it did and
which calls it inlined, for one source file.

The source is compiled with the oracle executable (build/VERSION/oracle/mwcceppc.exe, built by `python configure.py
--version VERSION --oracle && ninja build/VERSION/oracle/mwcceppc.exe`), which is the original compiler with trace
wrappers around the register allocation and the inliner (config.json "oracle"); the object it writes is compared with
the original compiler's. The trace is read back here:

  * the PCode before and after the allocation of each function gives the register assignment (the operands of an
    instruction that is in both, position by position), the interference graph (liveness over the blocks computed from
    the before-PCode, an edge where a register is defined while another is live), what was spilled or coalesced, and,
    for each virtual register, which registers its neighbours took and which constraint (a call it lives across) applies;
  * the inliner's events give, for each call it considered, the decision and the reason: the options in force, the
    callee's body, the estimated cost against the threshold.

Options:
  --version V       the compiler version (default GC_1_3_2)
  --function NAME   only this function (a substring of its name)
  --from-ninja FILE take the compiler flags and the directory to compile in from the build.ninja that builds SOURCE
                    (a decomp project such as Prime: `--from-ninja ../prime/build.ninja src/Collision/CMaterialList.cpp`)
  --raw             print the trace lines too (only those of the functions --function selects, if given)
  --pcode           print the PCode the allocator was given, per block, with the virtual register numbers
  --no-check        do not compile with the original compiler to compare the objects"""
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PHYSICAL = 32

CLASSES = {4: "GPR", 3: "FPR", 2: "VR", 1: "CR", 0: "SPR"}
VOLATILE = {"GPR": set(range(3, 13)), "FPR": set(range(0, 14)), "VR": set(range(0, 20)), "CR": {0, 1, 5, 6, 7}}


def opcode_names(version):
    """{number: name} of the PC_ opcodes: plain numbers, or BY_VERSION(older, newer) for the later ones."""
    names = {}
    pick = 0 if version in ("GC_1_0", "GC_1_1", "GC_1_1p1", "GC_1_2_5", "GC_1_2_5n") else 1
    for line in (ROOT / "include/compiler/PCode.h").read_text().splitlines():
        m = re.match(r"#define PC_(\w+) (\d+)", line)
        if m:
            names.setdefault(int(m.group(2)), m.group(1).lower())
            continue
        m = re.match(r"#define PC_(\w+) BY_VERSION\((\d+), (\d+)\)", line)
        if m:
            names[int(m.group(2 + pick))] = m.group(1).lower()
    return names


class Operand:
    def __init__(self, raw):
        self.kind, self.arg = raw[0], raw[1]
        self.effect = int.from_bytes(raw[2:4], "little")
        self.reg = int.from_bytes(raw[4:6], "little")
        self.value = int.from_bytes(raw[2:6], "little", signed=True)
        self.raw = raw

    @property
    def is_register(self):
        return self.kind == 0

    @property
    def cls(self):
        return CLASSES.get(self.arg, f"class{self.arg}")

    @property
    def uses(self):
        return self.is_register and bool(self.effect & 1)

    @property
    def defines(self):
        return self.is_register and bool(self.effect & 2)

    def text(self):
        if self.is_register:
            prefix = {"GPR": "r", "FPR": "f", "VR": "v", "CR": "cr", "SPR": "spr"}.get(self.cls, "?")
            if self.reg >= PHYSICAL and self.cls in ("GPR", "FPR", "VR", "CR"):
                return f"%{prefix}{self.reg}"
            return f"{prefix}{self.reg}"
        if self.kind == 2:
            return f"#{self.value}"
        return {3: "sym", 4: "label", 6: "-"}.get(self.kind, f"k{self.kind}")


class Instruction:
    def __init__(self, pointer, opcode, flags, operands):
        self.pointer, self.opcode, self.flags, self.operands = pointer, opcode, flags, operands

    def text(self, names):
        return f"{names.get(self.opcode, 'op' + str(self.opcode))} " + ", ".join(o.text() for o in self.operands)


class Snapshot:
    def __init__(self, function):
        self.function = function
        self.blocks = []  # [(pointer, index, weight, [successor pointers], [Instruction])]


def parse_trace(lines):
    """[(function, before Snapshot, after Snapshot)] and the inliner's events in order, with the position of each
    among the allocations (the function's events come before its allocation)."""
    allocations, inliner = [], []
    current = None
    pending = {}
    for line in lines:
        line = line.rstrip("\n")
        m = re.match(r"ra\.(before|after) function=(.*)", line)
        if m:
            current = Snapshot(m.group(2))
            pending[m.group(1)] = current
            continue
        if current is not None:
            m = re.match(r"ra\.block p=(\w+) index=(-?\d+) weight=(-?\d+) flags=(\w+) succ=(.*)", line)
            if m:
                successors = [s for s in m.group(5).split(",") if s]
                current.blocks.append((m.group(1), int(m.group(2)), int(m.group(3)), successors, []))
                continue
            m = re.match(r"ra\.i p=(\w+) op=(-?\d+) flags=(\w+) n=(\d+) ops=(.*)", line)
            if m:
                raws = [bytes.fromhex(x) for x in m.group(5).split()]
                current.blocks[-1][4].append(Instruction(m.group(1), int(m.group(2)), int(m.group(3), 16),
                                                         [Operand(r) for r in raws]))
                continue
            if line == "ra.end":
                current = None
                if "after" in pending and "before" in pending:
                    allocations.append((pending["before"].function, pending["before"], pending["after"],
                                        len(inliner)))
                    pending = {}
                continue
        if line.startswith("inl."):
            fields = dict(re.findall(r"(\w+)=(\S+)", line))
            inliner.append((line.split()[0], fields))
    return allocations, inliner


def liveness(snapshot):
    """Interference edges, def/use counts and the live-across-call sets per register, over the before-PCode."""
    blocks = {b[0]: b for b in snapshot.blocks}
    use, define = {}, {}
    for pointer, _, _, _, instructions in snapshot.blocks:
        u, d = set(), set()
        for instruction in instructions:
            for op in instruction.operands:
                if op.is_register and op.uses and (op.cls, op.reg) not in d:
                    u.add((op.cls, op.reg))
            for op in instruction.operands:
                if op.is_register and op.defines:
                    d.add((op.cls, op.reg))
        use[pointer], define[pointer] = u, d
    live_in = {p: set() for p in blocks}
    live_out = {p: set() for p in blocks}
    changed = True
    while changed:
        changed = False
        for pointer, _, _, successors, _ in reversed(snapshot.blocks):
            out = set()
            for s in successors:
                out |= live_in.get(s, set())
            new_in = use[pointer] | (out - define[pointer])
            if out != live_out[pointer] or new_in != live_in[pointer]:
                live_out[pointer], live_in[pointer] = out, new_in
                changed = True
    edges = {}
    across_call = {}
    ranges = {}
    counts = {}
    position = 0
    for pointer, index, _, _, instructions in snapshot.blocks:
        live = set(live_out[pointer])
        for instruction in reversed(instructions):
            defs = {(o.cls, o.reg) for o in instruction.operands if o.is_register and o.defines}
            uses = {(o.cls, o.reg) for o in instruction.operands if o.is_register and o.uses}
            is_call = any(o.kind == 3 for o in instruction.operands) and len(defs) > 12
            for register in defs | uses:
                counts.setdefault(register, [0, 0])
                counts[register][0 if register in uses else 1] += 1
            if is_call:
                # (what is live while the callee runs: live after the call, without what the call defines)
                for register in live - defs:
                    across_call.setdefault(register, []).append((index, instruction.pointer))
            for d in defs:
                for other in live:
                    if other != d and other[0] == d[0]:
                        edges.setdefault(d, set()).add(other)
                        edges.setdefault(other, set()).add(d)
            live = (live - defs) | uses
            position += 1
            for register in live:
                ranges.setdefault(register, set()).add(index)
    return edges, across_call, ranges, counts


def reg_name(cls, number):
    return {"GPR": "r", "FPR": "f", "VR": "v", "CR": "cr"}.get(cls, "r") + str(number)


def preference(cls, across_call):
    """The registers in the order the allocator takes them: what a call preserves, from the top, when the register
    lives across a call; else the volatile ones from the bottom, then the preserved ones."""
    if cls == "GPR":
        nonvolatile = list(range(31, 13, -1))
        return nonvolatile if across_call else [0, *range(3, 13), *nonvolatile]
    if cls == "FPR":
        nonvolatile = list(range(31, 13, -1))
        return nonvolatile if across_call else [*range(0, 14), *nonvolatile]
    if cls == "VR":
        nonvolatile = list(range(31, 19, -1))
        return nonvolatile if across_call else [*range(0, 20), *nonvolatile]
    return list(range(8))


def order_text(cls, across_call):
    p = {"GPR": "r", "FPR": "f", "VR": "v", "CR": "cr"}.get(cls, "r")
    if cls == "CR":
        return "cr0..cr7"
    if across_call:
        return f"{p}31 down to {p}14"
    return {"GPR": "r0, r3..r12, then r31 down", "FPR": "f0..f13, then f31 down",
            "VR": "v0..v19, then v31 down"}[cls]


def describe_taken(cls, taken):
    """The registers neighbours hold, with the virtual registers that hold them; runs of registers only the machine
    holds (the registers a call clobbers, parameter registers) are given as a range."""
    parts, fixed = [], []

    def flush():
        if fixed:
            first, last = fixed[0], fixed[-1]
            parts.append(reg_name(cls, first) if first == last else f"{reg_name(cls, first)}..{reg_name(cls, last)}")
            fixed.clear()

    for c, who in sorted(taken.items()):
        virtual = [w for w in who if w[1] >= PHYSICAL]
        if not virtual:
            if fixed and fixed[-1] != c - 1:
                flush()
            fixed.append(c)
            continue
        flush()
        label = ",".join(f"%{w[1]}" for w in virtual)
        parts.append(f"{reg_name(cls, c)}({label}{',fixed' if len(virtual) != len(who) else ''})")
    flush()
    return " ".join(parts)


def explain(function, before, after, names):
    out = []
    after_by_pointer = {i.pointer: i for _, _, _, _, ins in after.blocks for i in ins}
    before_pointers = {i.pointer for _, _, _, _, ins in before.blocks for i in ins}
    mapping = {}
    for _, _, _, _, instructions in before.blocks:
        for instruction in instructions:
            other = after_by_pointer.get(instruction.pointer)
            if other is None:
                continue
            for a, b in zip(instruction.operands, other.operands):
                if a.is_register and b.is_register and a.cls == b.cls and a.reg >= PHYSICAL and b.reg < PHYSICAL:
                    mapping.setdefault((a.cls, a.reg), set()).add(b.reg)
    edges, across_call, ranges, counts = liveness(before)
    removed = [i for _, _, _, _, ins in before.blocks for i in ins if i.pointer not in after_by_pointer]
    # (a register move the allocator deleted: its destination took the register of its source)
    coalesced = {}
    for instruction in removed:
        if names.get(instruction.opcode) in ("mr", "fmr") and len(instruction.operands) >= 2:
            d, src = instruction.operands[0], instruction.operands[1]
            if d.is_register and src.is_register and d.reg >= PHYSICAL:
                coalesced[(d.cls, d.reg)] = (src.cls, src.reg)

    def colors_of(register, seen=()):
        if register[1] < PHYSICAL:
            return {register[1]}
        if register in mapping:
            return set(mapping[register])
        if register in coalesced and register not in seen:
            return colors_of(coalesced[register], seen + (register,))
        return set()

    inserted = [i for _, _, _, _, ins in after.blocks for i in ins if i.pointer not in before_pointers]
    total_before = sum(len(b[4]) for b in before.blocks)
    out.append(f"== {function}: register allocation ==")
    out.append(f"   {len(before.blocks)} blocks, {total_before} instructions; the allocator removed {len(removed)} "
               f"and inserted {len(inserted)}")
    virtual = sorted(r for r in counts if r[1] >= PHYSICAL and r[0] in ("GPR", "FPR", "VR", "CR"))
    for cls in ("GPR", "FPR", "VR", "CR"):
        mine = [r for r in virtual if r[0] == cls]
        if not mine:
            continue
        out.append(f"-- {cls}: {len(mine)} virtual registers")
        for register in mine:
            colors = sorted(colors_of(register))
            neighbours = sorted(edges.get(register, ()))
            if not colors:
                state = "SPILLED (kept in memory; every use and def was rewritten)"
            elif register not in mapping:
                source = coalesced[register]
                state = f"-> {reg_name(cls, colors[0])} (coalesced: its move from "                         f"{reg_name(cls, source[1]) if source[1] < PHYSICAL else '%' + str(source[1])} was deleted)"
            elif len(colors) == 1:
                prefix = {"GPR": "r", "FPR": "f", "VR": "v", "CR": "cr"}[cls]
                state = f"-> {prefix}{colors[0]}"
            else:
                state = "-> " + " / ".join(str(c) for c in colors) + " (split by spilling)"
            line = f"   %{register[1]:<4} {state}"
            u, d = counts.get(register, (0, 0))
            line += f"   {d} defs, {u} uses, live in {len(ranges.get(register, ()))} blocks"
            out.append(line)
            reasons = []
            if register in across_call:
                where = across_call[register][0][0]
                reasons.append(f"live across {len(across_call[register])} call(s) (first in block {where}): only "
                               "non-volatile registers survive a call")
            taken = {}
            for other in neighbours:
                other_colors = colors_of(other)
                for c in other_colors:
                    taken.setdefault(c, []).append(other)
            if colors:
                across = register in across_call
                order = preference(cls, across)
                free = [c for c in order if c not in taken]
                chosen = colors[0]
                if taken:
                    reasons.append("registers its neighbours hold: " + describe_taken(cls, taken))
                if free and chosen == free[0]:
                    reasons.append(f"the first free register in the allocator's order ({order_text(cls, across)}) "
                                   f"is {reg_name(cls, chosen)}")
                elif free:
                    reasons.append(f"{reg_name(cls, chosen)} is not the first free register of "
                                   f"{order_text(cls, across)} ({reg_name(cls, free[0])} was free): it was "
                                   f"chosen for another reason (a move it was coalesced with, a pairing or a "
                                   f"parameter register)")
            for reason in reasons:
                out.append(f"          because: {reason}")
        # (the order of the colouring is not traced: the highest-numbered register first is Coloring.c's select order,
        # and a greedy colouring in it is checked against what the allocator did)
        simulated, tried, agree = {}, 0, 0
        for register in sorted(mine, key=lambda r: -r[1]):
            colors = colors_of(register)
            if len(colors) != 1:
                simulated[register] = min(colors) if colors else None
                continue
            taken = set()
            for other in edges.get(register, ()):
                if other[1] < PHYSICAL:
                    taken.add(other[1])
                elif simulated.get(other) is not None:
                    taken.add(simulated[other])
            free = [c for c in preference(cls, register in across_call) if c not in taken]
            simulated[register] = free[0] if free else None
            tried += 1
            agree += simulated[register] in colors
        names_in_order = [f"%{r[1]}" for r in sorted(mine, key=lambda r: -r[1])]
        out.append(f"   {cls} colouring order (assumed, not traced: highest-numbered first): "
                   + " ".join(names_in_order[:40]) + (f" ... {len(names_in_order) - 40} more"
                                                      if len(names_in_order) > 40 else ""))
        out.append(f"      a greedy colouring in that order gives the allocator's register for {agree} of {tried} "
                   f"virtual registers")
    if removed:
        out.append("-- removed (coalesced or dead):")
        for instruction in removed[:12]:
            out.append(f"   {instruction.text(names)}")
        if len(removed) > 12:
            out.append(f"   ... {len(removed) - 12} more")
    if inserted:
        out.append("-- inserted by the allocator (spill code and the like):")
        for instruction in inserted[:16]:
            out.append(f"   {instruction.text(names)}")
        if len(inserted) > 16:
            out.append(f"   ... {len(inserted) - 16} more")
    return out


def inlining_report(events):
    """The decisions of the inliner, each with its reason."""
    out = []
    cost = None
    for kind, f in events:
        if kind == "inl.cost":
            cost = f
        elif kind == "inl.can":
            name = f.get("function", "?")
            result = f.get("result") == "1"
            depth = f.get("depth")
            if f.get("inline_bit") == "0" and not result:
                reason = "not declared inline (and not a method defined in its class)"
            elif result and f.get("always") == "1":
                reason = "inline always (-inline all): every candidate is accepted"
            elif f.get("body") == "0":
                reason = "no body is available at this point (declared later, or never defined here)"
            elif f.get("auto") == "1":
                if cost is not None and f.get("inline_bit") != "0":
                    c, t = int(cost.get("cost", 0)), int(cost.get("threshold", 0))
                    reason = f"estimated cost {c} {'is within' if result else 'exceeds'} the threshold {t}"
                    if result and f.get("nstmts") and int(f["nstmts"]) > 0:
                        reason += f" ({f['nstmts']} statements)"
                else:
                    reason = "auto-inline decision"
            else:
                reason = (f"depth {depth} with {f.get('nstmts')} statements "
                          f"({'within' if result else 'over'} the limit for this depth)")
            out.append(f"   {name}: {'INLINE' if result else 'call kept'} - {reason}")
            cost = None
        elif kind == "inl.end":
            if f.get("expanded") == "0":
                out.append(f"      {f.get('function')}: could not be expanded here "
                           f"(its body is not an expression-only body at this stage)")
            else:
                out.append(f"      {f.get('function')}: expanded in place")
    return out


def collapse(lines):
    """Consecutive identical lines once, with how many times."""
    out = []
    for line in lines:
        if out and out[-1][0] == line:
            out[-1][1] += 1
        else:
            out.append([line, 1])
    return [l if n == 1 else f"{l}  (x{n})" for l, n in out]


def launcher(executable):
    """The command that runs a Windows executable here: itself on Windows, else wibo (build/tools/wibo, which the
    build downloads, or one on the PATH)."""
    if sys.platform == "win32":
        return [str(executable)]
    local = ROOT / "build/tools/wibo"
    found = str(local) if local.exists() else shutil.which("wibo")
    if not found:
        sys.exit("the compilers are Windows executables: wibo (build/tools/wibo, or on the PATH) runs them here")
    return [found, str(executable)]


def pcode_report(function, before, names):
    """The PCode the allocator was given, per block, with the virtual register numbers (%r60 is virtual register 60 of
    the GPR class) that the register report names."""
    out = [f"== {function}: PCode before the allocation =="]
    for pointer, index, weight, successors, instructions in before.blocks:
        out.append(f"   block {index} (weight {weight}) -> " + (", ".join(
            str(next((b[1] for b in before.blocks if b[0] == s), s)) for s in successors) or "exit"))
        for instruction in instructions:
            operands = [o.text() for o in instruction.operands]
            if len(operands) > 12:  # (a call: the registers it clobbers are listed as operands)
                operands = operands[:3] + [f"... {len(operands) - 3} more (the registers the call clobbers)"]
            out.append(f"      {names.get(instruction.opcode, 'op' + str(instruction.opcode))} " + ", ".join(operands))
    return out


def raw_for(lines, wanted):
    """The trace lines of the functions whose name contains WANTED: their allocation snapshots, each with the inliner
    events that came before it."""
    out, pending, section = [], [], None
    for line in lines:
        m = re.match(r"ra\.(before|after) function=(.*)", line)
        if m:
            section = [m.group(2), []]
        if section is None:
            pending.append(line)
            continue
        section[1].append(line)
        if line == "ra.end":
            if wanted in section[0]:
                out.extend(pending + section[1])
            pending, section = [], None
    return out


def compile_flags(arguments):
    if arguments.from_ninja:
        text = Path(arguments.from_ninja).read_text()
        text = re.sub(r"\$\r?\n\s*", "", text)
        source = arguments.source.replace("/", "\\")
        for variant in (arguments.source, source):
            m = re.search(r"^build [^\n]*: mwcc\w* " + re.escape(variant) + r"\b[^\n]*\n((?:  [^\n]*\n)+)", text, re.M)
            if m:
                block = m.group(1)
                flags = re.search(r"^  cflags = (.*)$", block, re.M)
                if flags:
                    return shlex.split(flags.group(1), posix=True), Path(arguments.from_ninja).resolve().parent
        sys.exit(f"{arguments.from_ninja} has no mwcc rule for {arguments.source}")
    return arguments.flags, Path.cwd()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version", default="GC_1_3_2")
    parser.add_argument("--function")
    parser.add_argument("--from-ninja")
    parser.add_argument("--raw", action="store_true")
    parser.add_argument("--pcode", action="store_true")
    parser.add_argument("--no-check", action="store_true")
    parser.add_argument("source")
    parser.add_argument("flags", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    oracle = ROOT / f"build/{arguments.version}/oracle/mwcceppc.exe"
    if not oracle.exists():
        sys.exit(f"{oracle} does not exist: python configure.py --version {arguments.version} --oracle, then ninja "
                 f"build/{arguments.version}/oracle/mwcceppc.exe")
    flags, workdir = compile_flags(arguments)
    # (the dashes of the flags would be taken for options of this script after SOURCE)
    flags = [f for f in flags if f != "--"]
    # -MD and -c are the project's; the object goes to a temporary file
    flags = [f for f in flags if f not in ("-MD", "-c")]
    with tempfile.TemporaryDirectory() as temp:
        trace = Path(temp) / "trace.txt"
        obj = Path(temp) / "oracle.o"
        env = dict(os.environ, MWCC_ORACLE_TRACE_FILE=str(trace))
        run = subprocess.run([*launcher(oracle), *flags, "-c", arguments.source, "-o", str(obj)], cwd=workdir, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if run.returncode:
            print(run.stdout)
            sys.exit(f"the oracle compiler failed ({run.returncode})")
        lines = trace.read_text().splitlines() if trace.exists() else []
        check = None
        if not arguments.no_check:
            original = ROOT / f"build/compilers/GC/{arguments.version[3:].replace('_', '.')}/mwcceppc.exe"
            if original.exists():
                other = Path(temp) / "original.o"
                subprocess.run([*launcher(original), *flags, "-c", arguments.source, "-o", str(other)], cwd=workdir,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                check = other.exists() and other.read_bytes() == obj.read_bytes()
    print(f"oracle: {oracle.relative_to(ROOT)} ({arguments.version}); the original's code with trace wrappers"
          + ("" if check is None else "; object identical to the original compiler's: " + ("yes" if check else
                                                                                           "NO - the oracle differs")))
    if check is False:
        print("WARNING: the oracle's object differs from the original compiler's; the explanation may not hold")
    if arguments.raw:
        print("\n".join(raw_for(lines, arguments.function) if arguments.function else lines))
    allocations, inliner = parse_trace(lines)
    names = opcode_names(arguments.version)
    shown = False
    start = 0
    for function, before, after, upto in allocations:
        events = inliner[start:upto]
        start = upto
        if arguments.function and arguments.function not in function:
            continue
        shown = True
        if events:
            print(f"== {function}: inlining ==")
            print("\n".join(inlining_report(events)))
        if arguments.pcode:
            print("\n".join(pcode_report(function, before, names)))
        print("\n".join(explain(function, before, after, names)))
    if inliner[start:] and not arguments.function:
        print("== inlining after the last allocated function ==")
        print("\n".join(inlining_report(inliner[start:])))
    if not shown and not inliner:
        print("no trace: the source produced no function code" if not allocations else "no function matches")


if __name__ == "__main__":
    main()
