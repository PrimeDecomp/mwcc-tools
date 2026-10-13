#!/usr/bin/env python3
"""python tools/oracle.py [options] SOURCE -- [COMPILER FLAGS...]: why the compiler assigned the registers it did,
laid out the stack frame as it did and which calls it inlined, for one source file.

The source is compiled with the oracle executable (build/VERSION/oracle/mwcceppc.exe, built by `python configure.py
--version VERSION --mode link --oracle && ninja build/VERSION/oracle/mwcceppc.exe`), which is the original compiler
with trace wrappers around the register allocation, the frame layout and the inliner (config.json "oracle"); the
object it writes is compared with the original compiler's. The trace is read back here:

  * the PCode before and after the allocation of each function gives the register assignment, liveness and the
    calls each register lives across;
  * the interference graph, the stack the simplification leaves (the order of the colouring) and what select gave
    each node are traced, and select and the simplification are replayed on them to say why each register got its
    colour;
  * the frame's locals and temporaries, by offset;
  * the inliner's events give, for each call it considered, the decision and the reason: the options in force, the
    callee's body, the estimated cost against the threshold.

Options (anywhere before `--`; the compiler's flags after `--`, or after SOURCE):
  --version V       the compiler version (default: the --from-ninja rule's mw_version, else GC_1_3_2)
  --function NAME   only this function (a substring of its name or of its link name)
  --from-ninja FILE take the compiler flags, the version and the directory to compile in from the build.ninja that
                    builds SOURCE (a decomp project such as Prime: `--from-ninja ../prime/build.ninja
                    src/Collision/CMaterialList.cpp`); no compiler flags may be given with it
  --raw             print the trace lines too (only those of the functions --function selects, if given)
  --pcode           print the PCode the allocator was given, per block, with the virtual register numbers
  --schedule        explain the instruction scheduler's picks, per block
  --inline-cost C   only the inliner's decisions about callee C (name or link name), with their cost broken down
  --near N          every inline decision whose cost is within N of the threshold, with the callee's link name
  --full-cost       estimate the whole cost of the inline decisions over the threshold too (the compiler stops there),
                    with the calls at the inline depth limit and the sensitive terms (calls whose body being there
                    or not would flip the decision, or move it by 2 or less)
  --exchange A,B    why virtual registers A and B were coloured in that order (r41,r42)
  --override P=R    compile with file R in place of P (a header or SOURCE), the checkout untouched; repeatable
  --json            print the report as JSON
  --no-check        do not compile with the original compiler to compare the objects

docs/oracle.md has the details, and the Python entry points (analyze_source, analyze_file)."""
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
    def __init__(self, pointer, opcode, flags, operands, line=None):
        self.pointer, self.opcode, self.flags, self.operands, self.line = pointer, opcode, flags, operands, line

    def text(self, names, limit=None):
        operands = [o.text() for o in self.operands]
        if limit and len(operands) > limit:  # (a call: the registers it clobbers are listed as operands)
            operands = operands[:3] + [f"... {len(operands) - 3} more"]
        return f"{names.get(self.opcode, 'op' + str(self.opcode))} " + ", ".join(operands)


class Snapshot:
    def __init__(self, function, link=None):
        self.function = function
        self.link = link or function  # (the link name: Think__10CPowerBombFfR13CStateManager)
        self.blocks = []  # [(pointer, index, weight, [successor pointers], [Instruction])]
        # (on the before snapshot: the colouring rounds and the stack frame traced during the allocation, see
        # parse_trace)
        self.rounds = []
        self.frame = None
        self.layout = None  # (fr.layout: linkage, outgoing, locals, above, size)
        self.schedule = []  # [ScheduledBlock]: the scheduling before the allocation, then after it (--schedule)


class ScheduledBlock:
    """One block's list scheduling (sc.*): its instructions before, the dependency graph, each pick with what was
    ready, and the order it left."""

    def __init__(self, pointer, index, virtual):
        self.pointer, self.index, self.virtual = pointer, index, virtual
        self.instructions = []
        self.nodes = {}  # {instruction pointer: {"latency", "height", "latest", "preds", "succ": [(pointer, latency)]}}
        self.picks = []  # [{"cycle", "ready": [{"i", "earliest", "latest", "height", "count", "check", "rank"}], "pick"}]
        self.after = []


class Round:
    """One colouring of one register class: the graph before the simplification, the stack it left and what select
    gave each node (a class is coloured again, with spill code added, when select fails)."""

    def __init__(self, cls, k, start, count):
        self.cls, self.k, self.start, self.count = cls, k, start, count
        self.nodes = {}  # {index: {"p", "degree", "flags", "neighbors"}}
        self.by_pointer = {}
        self.order = []  # [(index, spill cost)], the order select colours in
        self.mask = self.mask_after = None
        self.ok = None
        self.colors = {}  # {index: (colour, flags)}


def parse_trace(lines):
    """[(function, before Snapshot, after Snapshot, position)] and the inliner's events in order, with the position of
    each among the allocations (the function's events come before its allocation). The before snapshot also holds the
    colouring rounds (`rounds`) and the stack frame (`frame`) of that allocation."""
    allocations, inliner = [], []
    current = None
    pending = {}
    rounds = []
    frame = layout = None
    scheduled = None
    schedule_pending = []  # (the scheduling before an allocation comes before its ra.before)
    for line in lines:
        line = line.rstrip("\n")
        m = re.match(r"ra\.(before|after) function=(\S*)(?: link=(.*))?$", line)
        if m:
            current = Snapshot(m.group(2), m.group(3))
            pending[m.group(1)] = current
            if m.group(1) == "before":
                rounds = []
                current.schedule, schedule_pending = schedule_pending, []
            continue
        if line.startswith("sc."):
            m = re.match(r"sc\.block p=(\w+) index=(-?\d+) virtual=(\d+)", line)
            if m:
                scheduled = ScheduledBlock(m.group(1), int(m.group(2)), m.group(3) != "0")
                if scheduled.virtual or not allocations:
                    schedule_pending.append(scheduled)
                else:
                    allocations[-1][1].schedule.append(scheduled)
                continue
            if scheduled is None:
                continue
            m = re.match(r"sc\.i p=(\w+) op=(-?\d+) flags=(\w+) n=(\d+) line=(-?\d+) ops=(.*)", line)
            if m:
                scheduled.instructions.append(Instruction(m.group(1), int(m.group(2)), int(m.group(3), 16),
                                                          [Operand(bytes.fromhex(x)) for x in m.group(6).split()],
                                                          int(m.group(5))))
                continue
            m = re.match(r"sc\.node i=(\w+) latency=(-?\d+) height=(-?\d+) latest=(-?\d+) preds=(-?\d+) succ=(.*)", line)
            if m:
                succ = [(s.split(":")[0], int(s.split(":")[1])) for s in m.group(6).split(",") if s]
                scheduled.nodes[m.group(1)] = {"latency": int(m.group(2)), "height": int(m.group(3)),
                                               "latest": int(m.group(4)), "preds": int(m.group(5)), "succ": succ}
                continue
            m = re.match(r"sc\.pick cycle=(\d+) ready=(\S*) pick=(\w+)", line)
            if m:
                ready = []
                for item in filter(None, m.group(2).split(",")):
                    i, earliest, latest, height, count, check, rank = item.split(":")
                    ready.append({"i": i, "earliest": int(earliest), "latest": int(latest), "height": int(height),
                                  "count": int(count), "check": int(check), "rank": int(rank)})
                scheduled.picks.append({"cycle": int(m.group(1)), "ready": ready,
                                        "pick": None if int(m.group(3), 16) == 0 else m.group(3)})
                continue
            m = re.match(r"sc\.after order=(.*)", line)
            if m:
                scheduled.after = [x for x in m.group(1).split(",") if x]
                scheduled = None
            continue
        if current is not None:
            m = re.match(r"ra\.block p=(\w+) index=(-?\d+) weight=(-?\d+) flags=(\w+) succ=(.*)", line)
            if m:
                successors = [s for s in m.group(5).split(",") if s]
                current.blocks.append((m.group(1), int(m.group(2)), int(m.group(3)), successors, []))
                continue
            m = re.match(r"ra\.i p=(\w+) op=(-?\d+) flags=(\w+) n=(\d+)(?: line=(-?\d+))? ops=(.*)", line)
            if m:
                raws = [bytes.fromhex(x) for x in m.group(6).split()]
                current.blocks[-1][4].append(Instruction(m.group(1), int(m.group(2)), int(m.group(3), 16),
                                                         [Operand(r) for r in raws],
                                                         int(m.group(5)) if m.group(5) is not None else None))
                continue
            if line == "ra.end":
                current = None
                if "after" in pending and "before" in pending:
                    pending["before"].rounds = rounds
                    allocations.append((pending["before"].function, pending["before"], pending["after"],
                                        len(inliner)))
                    pending = {}
                continue
        m = re.match(r"ra\.graph cls=(\d+) k=(-?\d+) start=(\d+) count=(\d+)", line)
        if m:
            rounds.append(Round(int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))))
            continue
        m = re.match(r"ra\.node v=(\d+) p=(\w+) deg=(-?\d+) flags=(\w+) color=(-?\d+) n=(.*)", line)
        if m and rounds:
            node = {"p": m.group(2), "degree": int(m.group(3)), "flags": int(m.group(4), 16), "color": int(m.group(5)),
                    "neighbors": [int(x) for x in m.group(6).split(",") if x]}
            rounds[-1].nodes[int(m.group(1))] = node
            rounds[-1].by_pointer[m.group(2)] = int(m.group(1))
            continue
        m = re.match(r"ra\.order cls=(\d+) v=(.*)", line)
        if m and rounds:
            for item in filter(None, m.group(2).split(",")):
                pointer, cost = item.split(":")
                rounds[-1].order.append((rounds[-1].by_pointer.get(pointer, pointer), int(cost)))
            continue
        m = re.match(r"ra\.select cls=(\d+) mask=(\w+) after=(\w+) ok=(\d+) v=(.*)", line)
        if m and rounds:
            r = rounds[-1]
            r.mask, r.mask_after, r.ok = int(m.group(2), 16), int(m.group(3), 16), m.group(4) == "1"
            for item in filter(None, m.group(5).split(",")):
                pointer, color, flags = item.split(":")
                r.colors[r.by_pointer.get(pointer, pointer)] = (int(color), int(flags, 16))
            continue
        if line == "fr.begin":
            frame, layout = [], None
            continue
        m = re.match(r"fr\.layout linkage=(-?\d+) outgoing=(-?\d+) locals=(-?\d+) above=(-?\d+) size=(-?\d+)", line)
        if m:
            layout = dict(zip(("linkage", "outgoing", "locals", "above", "size"), map(int, m.groups())))
            continue
        m = re.match(r"fr\.(local|temp) name=(\S+) offset=(-?\d+) size=(-?\d+) reg=(-?\d+)(?: used=(-?\d+))?", line)
        if m and frame is not None:
            frame.append({"kind": m.group(1), "name": m.group(2), "offset": int(m.group(3)), "size": int(m.group(4)),
                          "reg": int(m.group(5)), "used": m.group(6) != "0"})
            continue
        if line == "fr.end":
            # (the frame is laid out after the allocation of the function it belongs to)
            if allocations and frame is not None:
                allocations[-1][1].frame = frame
                allocations[-1][1].layout = layout
                if layout is not None:
                    # (a local's offset from r1: the linkage area, the outgoing arguments, then its offset)
                    for entry in frame:
                        if entry["used"] and entry["reg"] == 0:
                            entry["r1"] = layout["linkage"] + layout["outgoing"] + entry["offset"]
            frame = layout = None
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


def mask_text(cls, mask):
    """The registers of a colour mask, runs as ranges (r0, r3..r12)."""
    parts, run = [], []
    for c in list(range(32)) + [None]:
        if c is not None and mask & (1 << c):
            run.append(c)
            continue
        if run:
            parts.append(reg_name(cls, run[0]) if len(run) == 1 else f"{reg_name(cls, run[0])}..{reg_name(cls, run[-1])}")
            run = []
    return ", ".join(parts) or "none"


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
        virtual = [w for w in who if w >= PHYSICAL]
        if not virtual:
            if fixed and fixed[-1] != c - 1:
                flush()
            fixed.append(c)
            continue
        flush()
        label = ",".join(f"%{w}" for w in virtual)
        parts.append(f"{reg_name(cls, c)}({label}{',fixed' if len(virtual) != len(who) else ''})")
    flush()
    return " ".join(parts)


def simulate_simplify(r, register_count=None):
    """Coloring.c's simplification replayed on the traced graph: [(index, reason)] in the order the nodes are pushed
    (the stack select takes is the reverse), and whether it gives the traced stack. A node is pushed when its degree
    (what is left of it) is below K; when none is, the spill candidate is the one with the lowest spill cost / degree
    (the costs are those ra.order gives; a temporary the spilling of an earlier colouring made, a node at or past the
    first colouring's REGISTER_COUNT, costs the most)."""
    degree = {i: n["degree"] for i, n in r.nodes.items()}
    done = {i for i, n in r.nodes.items() if n["flags"] & 6}
    cost = {i: c for i, c in r.order if isinstance(i, int)}
    pushes = []
    # (the neighbour whose removal took each node's degree below K, and the scan pass each push was made in)
    unlocked_by = {}
    passes = [0]

    def push(i, reason):
        reason["pass"] = passes[0]
        if i in unlocked_by:
            reason["unlocked_by"] = unlocked_by[i]
        pushes.append((i, reason))
        for n in r.nodes[i]["neighbors"]:
            if n in degree:
                degree[n] -= 1
                if degree[n] == r.k - 1 and n not in done:
                    unlocked_by[n] = i
        done.add(i)

    def low_degree():
        while True:
            passes[0] += 1
            changed, remaining = False, []
            for i in sorted(r.nodes):
                if i in done:
                    continue
                if degree[i] < r.k:
                    push(i, {"why": "degree", "degree": degree[i], "k": r.k})
                    changed = True
                else:
                    remaining.insert(0, i)  # (the compiler's list is built by prepending)
            if not changed:
                return remaining

    remaining = low_degree()
    while remaining:
        best, best_cost = None, None
        for i in remaining:
            if register_count is not None and i >= register_count or not degree[i]:
                c = float("inf")
            else:
                c = cost.get(i, 0) / degree[i]
            if best is None or c < best_cost:
                best, best_cost = i, c
        push(best, {"why": "spill candidate", "degree": degree[best], "k": r.k, "cost": cost.get(best, 0)})
        remaining = low_degree()
    simulated = [i for i, _ in reversed(pushes)]
    return dict(pushes), simulated == [i for i, _ in r.order]


def replay_select(r):
    """Coloring.c's select replayed on the traced stack: for each node, the colours its neighbours held when it was
    coloured, the colours free in the class's mask (with the colours claimed so far) and the colour that gives (the
    lowest free one; when none is, a claimed non-volatile register, or a spill). Neighbours are given the colours the
    trace says they took, so one disagreement does not carry over."""
    mask = r.mask or 0
    actual = {i: c for i, (c, _) in r.colors.items()}
    coloured = set()
    out = {}
    for position, (i, _) in enumerate(r.order):
        taken = {}
        for n in r.nodes.get(i, {}).get("neighbors", []):
            if n < PHYSICAL:
                c = n
            elif n in coloured:
                c = actual.get(n, -1)
            else:
                c = r.nodes.get(n, {}).get("color", -1)
            if 0 <= c < PHYSICAL:
                taken.setdefault(c, []).append(n)
        taken_mask = sum(1 << c for c in taken)
        free = mask & ~taken_mask
        mask_then = mask
        got = actual.get(i, -1)
        if free:
            predicted = (free & -free).bit_length() - 1
            how = "lowest free"
            agrees = predicted == got
        else:
            predicted = got
            how = "claimed" if got >= 0 else "spilled"
            agrees = got < 0 or not (mask & (1 << got)) and got not in taken
            if got >= 0:
                mask |= 1 << got
        coloured.add(i)
        out[i] = {"position": position, "taken": taken, "mask": mask_then, "free": free, "predicted": predicted, "how": how, "agrees": agrees, "color": got}
    return out


def definitions(snapshot):
    """{(class, register): (block index, Instruction)} of the first instruction that defines each register."""
    out = {}
    for _, index, _, _, instructions in snapshot.blocks:
        for instruction in instructions:
            for op in instruction.operands:
                if op.is_register and op.defines:
                    out.setdefault((op.cls, op.reg), (index, instruction))
    return out


def analyze(function, before, after, names):
    """The register allocation of one function as data (what --json prints and the report is made from)."""
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
    defined = definitions(before)
    removed = [i for _, _, _, _, ins in before.blocks for i in ins if i.pointer not in after_by_pointer]
    inserted = [i for _, _, _, _, ins in after.blocks for i in ins if i.pointer not in before_pointers]
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

    # (the last colouring of each class is the one that stuck; the earlier ones failed and spilled)
    rounds = {}
    for r in before.rounds:
        rounds.setdefault(CLASSES.get(r.cls, f"class{r.cls}"), []).append(r)
    result = {"function": before.function, "link": before.link, "blocks": len(before.blocks),
              "instructions": sum(len(b[4]) for b in before.blocks), "classes": {},
              "removed": [i.text(names) for i in removed], "inserted": [i.text(names) for i in inserted],
              "frame": before.frame, "layout": before.layout}
    virtual = sorted(r for r in counts if r[1] >= PHYSICAL and r[0] in ("GPR", "FPR", "VR", "CR"))
    for cls in ("GPR", "FPR", "VR", "CR"):
        mine = [r for r in virtual if r[0] == cls]
        last = rounds.get(cls, [None])[-1]
        if not mine and last is None:
            continue
        info = {"rounds": [], "vregs": []}
        for r in rounds.get(cls, []):
            info["rounds"].append({"k": r.k, "mask": r.mask, "ok": r.ok, "nodes": len(r.nodes),
                                   "spilled": [i for i, (c, f) in r.colors.items() if f & 1]})
        pushes, simplify_agrees = simulate_simplify(last, rounds[cls][0].count) if last else ({}, None)
        select = replay_select(last) if last else {}
        if last:
            info.update({"k": last.k, "mask": last.mask, "order": [i for i, _ in last.order],
                         "simplify_replay_agrees": simplify_agrees,
                         "select_replay_agrees": sum(s["agrees"] for s in select.values()),
                         "select_replayed": len(select)})
        numbers = sorted({r[1] for r in mine} | (set(last.nodes) if last else set()))
        for number in numbers:
            register = (cls, number)
            node = last.nodes.get(number) if last else None
            colors = sorted(colors_of(register))
            u, d = counts.get(register, (0, 0))
            entry = {"vreg": number, "class": cls, "phys": colors[0] if len(colors) == 1 else colors or None,
                     "defs": d, "uses": u, "live_blocks": len(ranges.get(register, ())),
                     "across_call": len(across_call.get(register, ())),
                     "first_call_block": across_call[register][0][0] if register in across_call else None}
            if register in defined:
                block, instruction = defined[register]
                entry["def"] = {"block": block, "line": instruction.line if (instruction.line or 0) > 0 else None,
                                "instruction": instruction.text(names)}
            if node is not None:
                entry["degree"] = node["degree"]
                entry["neighbors"] = [{"vreg": n, "phys": n if n < PHYSICAL else
                                       last.colors.get(n, (last.nodes.get(n, {}).get("color", -1),))[0]}
                                      for n in node["neighbors"]]
                entry["graph_flags"] = node["flags"]
            if register not in counts:
                entry["in_pcode"] = False  # (a node of the graph that no instruction mentions any more)
            if node is not None and node["flags"] & 4:
                entry["state"] = "coalesced"
                entry["coalesced_into"] = node["color"] if node["color"] >= 0 else None
                if not colors and entry["coalesced_into"] is not None:
                    # (Coloring.c: a coalesced node's colour is its representative's)
                    into, seen = entry["coalesced_into"], set()
                    while into >= PHYSICAL and into not in seen:
                        seen.add(into)
                        into = last.colors.get(into, (last.nodes.get(into, {}).get("color", -1),))[0]
                    entry["phys"] = into if 0 <= into < PHYSICAL else None
            elif last and number in last.colors and last.colors[number][1] & 1:
                entry["state"] = "spilled"
            elif not colors and last and number in last.colors and last.colors[number][0] >= 0:
                entry["state"] = "colored"
                entry["phys"] = last.colors[number][0]
            elif not colors:
                entry["state"] = "spilled"
            elif register not in mapping and register in coalesced:
                entry["state"] = "coalesced"
                entry["coalesced_into"] = coalesced[register][1]
            elif len(colors) > 1:
                entry["state"] = "split"
            else:
                entry["state"] = "colored"
            if number in select:
                s = select[number]
                entry["order"] = s["position"]
                entry["select"] = {"how": s["how"], "free": s["free"], "predicted": s["predicted"],
                                   "agrees": s["agrees"], "taken": {str(c): w for c, w in s["taken"].items()},
                                   "mask": s["mask"], "color": s["color"],
                                   "flags": last.colors.get(number, (0, 0))[1]}
            if number in pushes:
                entry["pushed"] = pushes[number]
            for rnd in info["rounds"][:-1]:
                if number in rnd["spilled"]:
                    entry["spilled_in_earlier_round"] = True
            info["vregs"].append(entry)
        result["classes"][cls] = info
    return result


def explain(analysis):
    a = analysis
    out = [f"== {a['function']} ({a['link']}): register allocation =="]
    out.append(f"   {a['blocks']} blocks, {a['instructions']} instructions; the allocator removed {len(a['removed'])} "
               f"and inserted {len(a['inserted'])}")
    for cls, info in a["classes"].items():
        vregs = info["vregs"]
        out.append(f"-- {cls}: {len(vregs)} virtual registers")
        if "k" in info:
            failed = info["rounds"][:-1]
            out.append(f"   K={info['k']} colours, the class's mask {mask_text(cls, info['mask'])}; "
                       + (f"{len(failed)} earlier colouring(s) failed and spilled "
                          + ", ".join(f"%{v}" for r in failed for v in r["spilled"]) + "; " if failed else "")
                       + f"select replayed: {info['select_replay_agrees']} of {info['select_replayed']} agree"
                       + ("" if info["simplify_replay_agrees"] else
                          " (the simplification's replay gives another order: the pushes below are approximate)"))
            if info["order"]:
                out.append(f"   colouring order (traced; first coloured first): "
                           + " ".join(f"%{v}" for v in info["order"][:40])
                           + (f" ... {len(info['order']) - 40} more" if len(info["order"]) > 40 else ""))
            else:
                out.append("   nothing was left to colour (every node was coalesced)")
        else:
            out.append("   (no colouring trace for this class)")
        unused = [v for v in vregs if not v.get("in_pcode", True) and v["state"] == "colored"]
        if unused:
            out.append(f"   {len(unused)} graph nodes no instruction uses any more (each took the lowest free "
                       "colour): " + " ".join(f"%{v['vreg']}" for v in unused))
        for v in sorted((v for v in vregs if v not in unused), key=lambda e: (e.get("order", 1 << 30), e["vreg"])):
            phys = v["phys"]
            if v["state"] == "spilled":
                state = "SPILLED (kept in memory; every use and def was rewritten)"
            elif v["state"] == "coalesced":
                into = v.get("coalesced_into")
                state = (f"-> {reg_name(cls, phys) if isinstance(phys, int) else '?'} (coalesced"
                         + (f" into {reg_name(cls, into) if into < PHYSICAL else '%' + str(into)})" if into is not None
                            else ")"))
            elif v["state"] == "split":
                state = "-> " + " / ".join(reg_name(cls, c) for c in phys) + " (split by spilling)"
            else:
                state = f"-> {reg_name(cls, phys)}" + ("" if v.get("in_pcode", True) else " (no instruction uses it)")
            line = f"   %{v['vreg']:<4} {state}   {v['defs']} defs, {v['uses']} uses, live in {v['live_blocks']} blocks"
            if "def" in v:
                d = v["def"]
                line += f"; defined in block {d['block']}" + (f" line {d['line']}" if d["line"] and d["line"] > 0 else "") \
                        + f": {d['instruction']}"
            out.append(line)
            reasons = []
            if v["across_call"]:
                reasons.append(f"live across {v['across_call']} call(s) (first in block {v['first_call_block']}): the "
                               "registers a call clobbers are its neighbours")
            if "pushed" in v:
                p = v["pushed"]
                if p["why"] == "degree":
                    reasons.append(f"coloured #{v['order'] + 1}: the simplification took it when its degree was "
                                   f"{p['degree']} < K={p['k']}")
                else:
                    reasons.append(f"coloured #{v['order'] + 1}: no node had a degree below K={p['k']}; it was the "
                                   f"spill candidate (spill cost {p['cost']} / degree {p['degree']} the lowest)")
            if "select" in v:
                s = v["select"]
                taken = {int(c): w for c, w in s["taken"].items()}
                if taken:
                    reasons.append("colours its neighbours held then: " + describe_taken(cls, taken))
                if cls == "GPR" and 0 in taken.get(0, ()) and not v["across_call"]:
                    reasons.append("r0 is a neighbour: r0 cannot be a base register (an address operand of r0 reads "
                                   "as 0), so registers used as addresses interfere with it")
                if s["how"] == "lowest free":
                    reasons.append(f"free in the mask: {mask_text(cls, s['free'])}; the lowest is "
                                   f"{reg_name(cls, s['predicted'])}")
                elif s["how"] == "claimed":
                    reasons.append(f"no colour of the mask was free: {reg_name(cls, s['color'])} was claimed (a "
                                   "non-volatile register, saved in the prologue; it joins the mask from here on)")
                else:
                    reasons.append("no colour was free and none could be claimed: spilled, and the class coloured "
                                   "again")
                if not s["agrees"]:
                    reasons.append(f"REPLAY DISAGREES: the replay gives {reg_name(cls, s['predicted'])}, the trace "
                                   f"{reg_name(cls, s['color'])}")
            if v.get("spilled_in_earlier_round"):
                reasons.append("spilled by an earlier colouring of the class")
            for reason in reasons:
                out.append(f"          because: {reason}")
    if a["removed"]:
        out.append("-- removed (coalesced or dead):")
        out.extend(f"   {t}" for t in a["removed"][:12])
        if len(a["removed"]) > 12:
            out.append(f"   ... {len(a['removed']) - 12} more")
    if a["inserted"]:
        out.append("-- inserted by the allocator (spill code and the like):")
        out.extend(f"   {t}" for t in a["inserted"][:16])
        if len(a["inserted"]) > 16:
            out.append(f"   ... {len(a['inserted']) - 16} more")
    return out


def frame_report(analysis):
    """The locals and the compiler's temporaries in the stack frame by their offset from r1 (the linkage area, the
    outgoing arguments, then the locals area), and the locals kept in registers."""
    frame, layout = analysis["frame"], analysis.get("layout")
    if frame is None:
        return []
    out = [f"== {analysis['function']}: stack frame =="]
    if layout:
        out.append(f"   frame {layout['size']} bytes before the register saves: linkage {layout['linkage']}, outgoing "
                   f"arguments {layout['outgoing']}, locals {layout['locals']}"
                   + (f", above them {layout['above']}" if layout["above"] else ""))
    memory = sorted((e for e in frame if e["used"] and e["reg"] == 0), key=lambda e: (e["offset"], e["kind"]))
    if memory:
        out.append("   in memory (offset from r1, offset in the locals area, size, name):" if layout else
                   "   in memory (offset in the locals area, size, name):")
        for e in memory:
            r1 = f"{e['r1']:>5} " if "r1" in e else ""
            out.append(f"      {r1}{e['offset']:>5} {e['size']:>5}  {e['name']}"
                       f"{' (temporary)' if e['kind'] == 'temp' else ''}")
    registers = [e for e in frame if e["used"] and e["reg"] != 0]
    if registers:
        out.append("   in registers: " + ", ".join(f"{e['name']}=reg {e['reg']}" for e in registers))
    if not memory and not registers:
        out.append("   nothing in the frame")
    return out


def cost_terms(nodes, above=()):
    """The calls of inline functions in a cost breakdown, each as (path, kind, cost): the path is the link names of the
    bodies it is in (from the body's own calls down) and its callee's; kind is "body" (costed as its body: cost), "limit"
    (at the inline depth limit) or "no body" (an inline function whose body is not there yet: costed as a call)."""
    out = []
    for node in nodes:
        if node["what"] == "body":
            here = above + (node["of"],) if node.get("of") else above
            if node.get("of"):
                out.append((here, "body", node["cost"]))
            out.extend(cost_terms(node["parts"], here))
            continue
        for mark, link in node.get("calls", ()):
            if mark:
                out.append((above + (link,), "limit" if mark == "!" else "no body", None))
        out.extend(cost_terms(node["parts"], above))
    return out


def limit_paths(nodes):
    """The calls of a cost breakdown at the inline depth limit, each as the callees' link names from the body's own
    calls down to it."""
    paths = [path for path, kind, _ in cost_terms(nodes) if kind == "limit"]
    return [list(path) for path in dict.fromkeys(paths)]


def tree(paths, suffix=""):
    """PATHS (sequences of link names) sorted, each with the prefix it has in common with the one before left out
    and indented instead."""
    out, previous = [], ()
    for path in sorted(tuple(p) for p in paths):
        common = 0
        while common < min(len(path), len(previous)) - 1 and path[common] == previous[common]:
            common += 1
        out.append(("  " * common) + ("> " if common else "") + " > ".join(path[common:]) + suffix)
        previous = path
    return out


def call_list(text):
    """inl.expr's calls= field: [(mark, link)], mark "!" at the inline depth limit, "?" with no body yet, else ""."""
    return [(c[0], c[1:]) if c[:1] in ("!", "?") else ("", c) for c in text.split("|")] if text else []


def body_costs(events):
    """{callee link name: the smallest cost its body was estimated at} over a whole trace (with --full-cost): what an
    inline function costed as a call (its body not there yet) would cost once its body is there."""
    out, bodies = {}, {}
    for kind, f in events:
        if kind == "inl.cost" and int(f.get("nest", 0)) > 0:
            bodies.setdefault(int(f["nest"]), []).append(int(f.get("cost", 0)) - int(f.get("running", 0)))
        elif kind == "inl.expr":
            costs = bodies.pop(int(f.get("nest", 0)) + 1, [])
            for (mark, link), cost in zip([c for c in call_list(f.get("calls")) if not c[0]], costs):
                out[link] = min(cost, out.get(link, cost))
    return out


def sensitive_terms(nodes, bodies):
    """The calls of a cost breakdown whose cost would change if the callee's body were (or were not) there: an inline
    function with its body is costed as that body, and as a plain call (1) otherwise; one without its body yet as 1,
    and as its body (BODIES, from body_costs) once that is there. Each is (path, kind, delta); a call at the depth
    limit is in limit_paths instead."""
    out = []
    for path, kind, cost in cost_terms(nodes):
        if kind == "body":
            out.append((path, kind, 1 - cost))
        elif kind == "no body" and path[-1] in bodies:
            out.append((path, kind, bodies[path[-1]] - 1))
    return out


def sensitivity(record, bodies, small=2):
    """A decision's sensitive terms (sensitive_terms) that would flip it on their own, or change its cost by at most
    SMALL: each a dict with path, kind, delta, flips and count."""
    t, statements = record.get("threshold"), record["breakdown"]["statements"]
    out = {}
    for path, kind, delta in sensitive_terms(statements, bodies):
        if record["inline"]:
            flips = record["cost"] + delta > t
        else:
            flips = not record.get("depth_limited") and record["full_cost"] + delta <= t
        if flips or abs(delta) <= small:
            key = (path, kind, delta)
            if key not in out:
                out[key] = {"path": list(path), "kind": kind, "delta": delta, "flips": flips, "count": 0}
            out[key]["count"] += 1
    return sorted(out.values(), key=lambda s: (not s["flips"], abs(s["delta"])))


def sensitivity_text(s):
    what = ("its body is costed; as a plain call" if s["kind"] == "body" else
            "costed as a call (no body yet here); with its body")
    return (f"{' > '.join(s['path'])}  [{what}: {s['delta']:+d}]" + ("  FLIPS THE DECISION" if s["flips"] else "")
            + (f"  (x{s['count']})" if s["count"] > 1 else ""))


def inlining_decisions(events, bodies=None):
    """The decisions of the inliner, each with its reason, with how many times each was made (in the order of the
    first). BODIES (body_costs of the whole trace) adds each decision's `sensitivity` with --full-cost."""
    if bodies is None:
        bodies = body_costs(events)
    decisions, seen = [], {}
    cost = None

    def add(record):
        depth = record.pop("depth", None)
        line = record.pop("line", None)
        breakdown = record.pop("breakdown", None)
        terms = record.pop("sensitivity", None)
        key = tuple(sorted(record.items()))
        if breakdown is not None and key not in seen:
            record["breakdown"] = breakdown
        if terms is not None and key not in seen:
            record["sensitivity"] = terms
        if key not in seen:
            record["count"] = 0
            seen[key] = record
            decisions.append(record)
        seen[key]["count"] += 1
        for field, value in (("depths", depth), ("lines", line)):
            if value is not None:
                values = seen[key].setdefault(field, [])
                if value not in values:
                    values.append(value)

    # (inl.expr comes after its operands' and inl.cost after its statements', each with its nesting: what is pending
    # one level deeper belongs to the next line one level up)
    pending = {}
    begins = []
    for kind, f in events:
        if kind == "inl.expr":
            nest = int(f.get("nest", 0))
            node = {"kind": "breakdown", "what": f.get("what", "?"), "type": int(f.get("type", -1)), "cost": int(f.get("cost", 0)),
                    "parts": pending.pop(nest + 1, []), "calls": call_list(f.get("calls"))}
            # (the bodies one deeper are those of the calls costed as their bodies, in order: name them)
            for part, (_, link) in zip([p for p in node["parts"] if p["what"] == "body"],
                                       [c for c in node["calls"] if not c[0]]):
                part["of"] = link
            if "link" in f:
                # (a call: the callee's link name, and whether the compiler made it, a constructor or a destructor)
                flags = int(f.get("flags", "0"), 16)
                node.update(link=f["link"], implicit=bool(flags & 0x100), ctor=bool(flags & 0x1000),
                            dtor=bool(flags & 0x2000), candidate=f.get("cand") == "1")
                if f.get("limit") == "1":
                    node["depth_limit"] = True
            pending.setdefault(nest, []).append(node)
        elif kind == "inl.cost":
            nest = int(f.get("nest", 0))
            statements = pending.pop(nest, [])
            if nest > 0:
                # (a callee's body costed inside an expression: it is one of that expression's parts; the bodies
                # costed before it in the same expression are its siblings, not its statements)
                bodies = [i for i, p in enumerate(statements) if p["what"] == "body"]
                siblings = statements[:bodies[-1] + 1] if bodies else []
                statements = statements[len(siblings):]
                pending[nest] = siblings + [{"kind": "breakdown", "what": "body", "type": -1,
                                             "cost": int(f.get("cost", 0)) - int(f.get("running", 0)),
                                             "nstmts": int(f.get("nstmts", 0)), "parts": statements}]
                continue
            cost = dict(f)
            if statements or "nest" in f:
                cost["breakdown"] = {"running": int(f.get("running", 0)), "nstmts": int(f.get("nstmts", 0)),
                                     "statements": statements}
        elif kind == "inl.can":
            name = f.get("function", "?")
            result = f.get("result") == "1"
            depth = f.get("depth")
            record = {"kind": "decision", "event": "decision", "callee": name, "link": f.get("link", name), "inline": result,
                      "depth": int(depth or 0),
                      # (the inlined bodies the call is in, outermost first: the inliner goes over the function
                      # once per depth, so a call met at depth N came from a body expanded at depth N - 1)
                      "chain": tuple(f["chain"].split("|")) if f.get("chain") else ()}
            if int(f.get("line", -1)) > 0:
                record["line"] = int(f["line"])
            if f.get("inline_bit") == "0" and not result:
                reason = "not declared inline (and not a method defined in its class)"
            elif result and f.get("always") == "1":
                reason = "inline always (-inline all): every candidate is accepted"
            elif f.get("body") == "0":
                reason = "no body is available at this point (declared later, or never defined here)"
            elif f.get("auto") == "1":
                if cost is not None and f.get("inline_bit") != "0":
                    c, t = int(cost.get("cost", 0)), int(cost.get("threshold", 0))
                    record.update(cost=c, threshold=t)
                    if "full" in cost:
                        record["full_cost"] = int(cost["full"])
                        record["depth_limited"] = cost.get("limit") == "1"
                    if cost.get("breakdown") and cost["breakdown"]["statements"]:
                        record["breakdown"] = cost["breakdown"]
                        paths = limit_paths(cost["breakdown"]["statements"])
                        if paths:
                            record["limit_paths"] = tuple(tuple(p) for p in paths)
                        if "full_cost" in record:
                            terms = sensitivity(dict(record, inline=result), bodies)
                            if terms:
                                record["sensitivity"] = terms
                    reason = f"estimated cost {c} {'is within' if result else 'exceeds'} the threshold {t}"
                    if record.get("depth_limited") and record["full_cost"] <= t:
                        reason = (f"a call in it is at the inline depth limit, which makes the estimate {c} (the "
                                  f"threshold {t} plus one, and what follows); without it the whole cost is "
                                  f"{record['full_cost']}, within the threshold")
                    elif "full_cost" in record and record["full_cost"] != c:
                        reason += f" (whole cost {record['full_cost']}"
                        if record.get("depth_limited"):
                            reason += ", without the calls at the inline depth limit, which also keep it"
                        reason += ")"
                    if result and f.get("nstmts") and int(f["nstmts"]) > 0:
                        reason += f" ({f['nstmts']} statements)"
                else:
                    reason = "auto-inline decision"
            else:
                reason = (f"depth {depth} with {f.get('nstmts')} statements "
                          f"({'within' if result else 'over'} the limit for this depth)")
            record["reason"] = reason
            # (limit: kept, and a call at the inline depth limit keeps it whatever its size; known with --full-cost)
            record["outcome"] = ("inlined" if result else
                                 "limit" if record.get("depth_limited") and record["full_cost"] <= record["threshold"]
                                 else "kept")
            add(record)
            cost = None
        elif kind == "inl.end":
            record = {"kind": "expansion", "event": "expansion", "callee": f.get("function", "?"), "expanded": f.get("expanded") != "0"}
            if begins:
                b = begins.pop()
                record["link"] = b.get("link", record["callee"])
                if int(b.get("line", -1)) > 0:
                    record["line"] = int(b["line"])
            add(record)
        elif kind == "inl.begin":
            begins.append(f)
    return decisions


def inlining_report(decisions, links=False):
    out = []
    for d in decisions:
        times = f"  (x{d['count']})" if d["count"] > 1 else ""
        if any(d.get("depths", ())):
            times += "  (depth " + ", ".join(str(x) for x in sorted(d["depths"])) + ")"
        if d.get("lines"):
            times += "  (line " + ", ".join(str(x) for x in sorted(d["lines"])) + ")"
        if d["event"] == "decision":
            out.append(f"   {d['callee']}: {'INLINE' if d['inline'] else 'call kept'} - {d['reason']}{times}"
                       + (f"\n      [{d['link']}]" if links and d.get("link") else "")
                       + (f"\n      inside: {' > '.join(d['chain'])}" if links and d.get("chain") else ""))
        elif d["expanded"]:
            out.append(f"      {d['callee']}: expanded in place{times}")
        else:
            out.append(f"      {d['callee']}: could not be expanded here (its body is not an expression-only body at "
                       f"this stage){times}")
    return out


def near_threshold(analyses, after_last, margin):
    """The automatic inliner's decisions whose cost is within MARGIN of the threshold, in every function (and after
    the last one): the close calls a change of a header can flip. The estimate stops once over the threshold, so a
    kept call's cost is where it stopped, not its whole cost."""
    out = []
    groups = [(a["function"], a["link"], a["inlining"]) for a in analyses] + [(None, None, after_last or [])]
    for function, link, decisions in groups:
        for d in decisions:
            if d["event"] == "decision" and "cost" in d and abs(d["cost"] - d["threshold"]) <= margin:
                n = {"kind": "near", "function": function, "function_link": link, "callee": d["callee"],
                     "link": d.get("link"), "inline": d["inline"], "cost": d["cost"],
                     "threshold": d["threshold"], "margin": d["threshold"] - d["cost"], "count": d["count"],
                     "lines": d.get("lines", []), "depths": d.get("depths", []), "chain": list(d.get("chain", ())),
                     "outcome": d["outcome"],
                     "limit_paths": [list(x) for x in d.get("limit_paths", ())],
                     "sensitivity": d.get("sensitivity", [])}
                if "full_cost" in d:
                    n["full_cost"] = d["full_cost"]
                    n["full_margin"] = d["threshold"] - d["full_cost"]
                    n["depth_limited"] = d.get("depth_limited", False)
                out.append(n)
    return out


def near_report(near, margin):
    out = [f"== inline decisions within {margin} of the threshold (cost, threshold, decision, callee link name, in "
           "which function) =="]
    for n in near:
        where = n["function"] or "(after the last function)"
        lines = f"  line {', '.join(str(x) for x in sorted(n['lines']))}" if n["lines"] else ""
        if n["depths"]:
            lines += f"  depth {', '.join(str(x) for x in sorted(n['depths']))}"
        whole = ""
        if "full_cost" in n and n["full_cost"] != n["cost"]:
            whole = f"  (whole cost {n['full_cost']}" + (", DEPTH LIMIT" if n.get("depth_limited") else "") + ")"
        if n["chain"]:
            whole += "\n          inside: " + " > ".join(n["chain"])
        if n["limit_paths"]:
            whole += "\n          at the depth limit [limit +1]:"
            whole += "".join("\n             " + line for line in tree(n["limit_paths"]))
        for term in n["sensitivity"][:4]:
            whole += "\n          sensitive: " + sensitivity_text(term)
        if len(n["sensitivity"]) > 4:
            whole += f"\n          ... {len(n['sensitivity']) - 4} more sensitive terms (--json: near[].sensitivity)"
        out.append(f"   {n['cost']:>4} {n['threshold']:>4}  {n['outcome'].upper():<7}  {n['link']}  in "
                   f"{where}{lines}" + (f"  (x{n['count']})" if n["count"] > 1 else "") + whole)
    if not near:
        out.append("   none")
    return out


def enode_names():
    """{number: name} of the ENode types (1.3 and later: include/compiler/enode.h with its 1.3 insertions)."""
    text = (ROOT / "include/compiler/enode.h").read_text()
    body = text[text.index("typedef enum ENodeType {"):text.index("} ENodeType;")]
    names = []
    for line in body.splitlines()[1:]:
        line = re.sub(r"/\*.*?\*/", "", line).strip()
        if line.startswith("#") or not line:
            continue
        names.extend(n.strip() for n in line.split(",") if n.strip())
    return dict(enumerate(names))


def cost_report(decision, types):
    """What a decision's cost estimate is made of: one per statement, then what each statement's expression added
    (a callee body costed inside one is shown under it)."""
    b = decision.get("breakdown")
    if not b:
        return []
    out = [f"         {b['running']:>4}  the running cost when this body was reached" if b["running"] else None,
           f"         {b['nstmts']:>4}  one per statement ({b['nstmts']})"]

    def walk(nodes, indent):
        for node in nodes:
            if node["what"] == "body":
                plain = f"  [as a plain call: {1 - node['cost']:+d}]" if node.get("of") else ""
                out.append(f"         {node['cost']:>4}  {'  ' * indent}the body of {node.get('of', 'a callee')}: "
                           f"{node['nstmts']} statement(s){plain}, and:")
                walk(node["parts"], indent + 1)
                continue
            call = ""
            if node.get("link"):
                call = f"  [{node['link']}{', compiler-made' if node['implicit'] else ''}]"
            for mark, link in node.get("calls", ()):
                if mark == "!":
                    call += f"  [limit +1: {link} AT THE INLINE DEPTH LIMIT]"
                elif mark == "?":
                    call += f"  [{link}: no body yet, costed as a call]"
            out.append(f"         {node['cost']:>4}  {'  ' * indent}{types.get(node['type'], node['type'])}: "
                       f"{node['what']}{call}")
            walk(node["parts"], indent + 1)

    walk(b["statements"], 0)
    total = b["running"] + b["nstmts"] + sum(n["cost"] for n in b["statements"])
    if "full_cost" in decision:
        out.append(f"         {total:>4}  in all, the whole cost (the compiler's estimate: {decision['cost']}), against "
                   f"the threshold {decision.get('threshold')}"
                   + ("; a call at the inline depth limit (adding nothing here) keeps it anyway"
                      if decision.get("depth_limited") else ""))
    else:
        out.append(f"         {total:>4}  in all, against the threshold {decision.get('threshold')}"
                   + (" (the estimate stops once over it; --full-cost goes on)"
                      if total > (decision.get("threshold") or 1 << 30) else ""))
    return [line for line in out if line is not None]


def schedule_compare(best, other, cycle, virtual):
    """select_ready_coloring_node's comparison of the best so far (the pick, when explaining one) with another ready
    instruction: (whether the other wins, the criterion, said of the pick and of the other)."""
    if best["latest"] <= cycle and other["latest"] > cycle:
        return False, f"the pick must issue by this cycle (latest {best['latest']}), this one need not"
    if best["latest"] > cycle and other["latest"] <= cycle:
        return True, f"this one must issue by this cycle (latest {other['latest']}), the pick need not"
    if best["count"] != other["count"]:
        return other["count"] > best["count"], (f"the pick makes {best['count']} dependent(s) ready, this one "
                                                 f"{other['count']}")
    if best["height"] != other["height"]:
        return other["height"] > best["height"], (f"the pick's height (the longest latency path to the end of the "
                                                   f"block) is {best['height']}, this one's {other['height']}")
    if virtual and best["rank"] != other["rank"] and best["rank"] >= 0:
        return other["rank"] < best["rank"], (f"the pick's opcode rank is {best['rank']}, this one's "
                                               f"{other['rank']} (the lower wins)")
    return False, "a tie: the pick comes first in the block"


def analyze_schedule(blocks, names):
    """The scheduling of a function's blocks as data: per block, each pick with the instructions it was chosen over
    and why, the instructions that were not ready and why, and select replayed from the traced candidates."""
    out = []
    for block in blocks:
        text = {i.pointer: i.text(names, limit=12) for i in block.instructions}
        position = {i.pointer: n for n, i in enumerate(block.instructions)}
        line = {i.pointer: i.line for i in block.instructions}
        preds = {}
        for pointer, node in block.nodes.items():
            for successor, latency in node["succ"]:
                preds.setdefault(successor, []).append((pointer, latency))
        issued = {}
        picks, agree = [], 0
        for pick in block.picks:
            cycle = pick["cycle"]
            ready = [r for r in pick["ready"] if r["earliest"] <= cycle and r["check"] != 0]
            predicted = None
            for r in ready:
                if predicted is None or schedule_compare(predicted, r, cycle, block.virtual)[0]:
                    predicted = r
            predicted_pointer = predicted["i"] if predicted else None
            agree += predicted_pointer == pick["pick"]
            if pick["pick"] is None:
                continue
            winner = next(r for r in pick["ready"] if r["i"] == pick["pick"])
            issued[pick["pick"]] = cycle
            others = []
            for r in pick["ready"]:
                if r["i"] == pick["pick"]:
                    continue
                if r["earliest"] > cycle:
                    waits = [(issued.get(p, 0) + latency, p) for p, latency in preds.get(r["i"], [])
                             if issued.get(p, 0) + latency == r["earliest"]]
                    why = f"not ready until cycle {r['earliest']}" + (
                        f" (it waits {r['earliest'] - issued.get(waits[0][1], 0)} cycle(s) for "
                        f"#{position.get(waits[0][1], '?')} {text.get(waits[0][1], '?')})" if waits else "")
                elif r["check"] == 0:
                    why = "the machine cannot issue it this cycle (its unit is busy)"
                else:
                    wins, criterion = schedule_compare(winner, r, cycle, block.virtual)
                    why = criterion if not wins else f"lost to an earlier comparison ({criterion})"
                others.append({"i": r["i"], "position": position.get(r["i"]), "why": why})
            picks.append({"cycle": cycle, "i": pick["pick"], "position": position.get(pick["pick"]),
                          "line": line.get(pick["pick"]), "instruction": text.get(pick["pick"], "?"),
                          "latest": winner["latest"], "height": winner["height"], "count": winner["count"],
                          "rank": winner["rank"], "agrees": predicted_pointer == pick["pick"], "over": others})
        out.append({"block": block.index, "virtual": block.virtual, "instructions": len(block.instructions),
                    "cycles": (block.picks[-1]["cycle"] + 1) if block.picks else 0,
                    "replay_agrees": agree, "replayed": len(block.picks), "picks": picks,
                    "before": [text[i.pointer] for i in block.instructions],
                    "edges": [{"from": position.get(p), "to": position.get(s), "latency": latency}
                              for p, node in block.nodes.items() for s, latency in node["succ"]]})
    return out


def schedule_report(function, schedule):
    out = [f"== {function}: scheduling =="]
    for b in schedule:
        moved = sum(1 for n, p in enumerate(b["picks"]) if p["position"] != n)
        out.append(f"-- block {b['block']} ({'before' if b['virtual'] else 'after'} the allocation): "
                   f"{b['instructions']} instructions in {b['cycles']} cycles, {moved} moved; select replayed: "
                   f"{b['replay_agrees']} of {b['replayed']} picks agree")
        for n, p in enumerate(b["picks"]):
            out.append(f"   cycle {p['cycle']:>3}: #{p['position']:<3} {p['instruction']}"
                       + (f"   (line {p['line']})" if p.get("line") and p["line"] > 0 else "")
                       + ("" if p["agrees"] else "   REPLAY DISAGREES"))
            for o in p["over"]:
                out.append(f"                 over #{o['position']}: {o['why']}")
    return out


def explain_exchange(analysis, first, second):
    """Why two virtual registers of one class were coloured in the order they were and took the registers they did:
    when the simplification pushed each (and the neighbour whose removal let it), their degrees, neighbours and spill
    costs, and what select found free for each."""
    cls = "GPR"
    numbers = []
    for text in (first, second):
        m = re.match(r"%?(r|f|v|cr)?(\d+)$", text.strip())
        if not m:
            return [f"--exchange: {text} is not a virtual register (45, r45, %r45, f33)"]
        if m.group(1):
            cls = {"r": "GPR", "f": "FPR", "v": "VR", "cr": "CR"}[m.group(1)]
        numbers.append(int(m.group(2)))
    info = analysis["classes"].get(cls)
    if not info:
        return [f"--exchange: no {cls} allocation in {analysis['function']}"]
    by = {v["vreg"]: v for v in info["vregs"]}
    a, b = (by.get(n) for n in numbers)
    if a is None or b is None:
        return [f"--exchange: {cls} %{numbers[0]} or %{numbers[1]} is not in {analysis['function']}"]
    out = [f"== {analysis['function']}: {cls} %{a['vreg']} and %{b['vreg']} =="]
    for v in (a, b):
        p = v.get("pushed", {})
        out.append(f"   %{v['vreg']} -> {reg_name(cls, v['phys']) if isinstance(v['phys'], int) else v['phys']}: "
                   f"coloured #{v.get('order', -1) + 1}, degree {v.get('degree')} in the graph, "
                   f"{len(v.get('neighbors', []))} neighbours, {v['defs']} defs, {v['uses']} uses"
                   + (f", spill cost {p['cost']}" if p.get("cost") is not None else ""))
        if p:
            how = (f"pushed in simplification pass {p.get('pass', '?')} when its degree was {p['degree']} < K="
                   f"{p['k']}" if p["why"] == "degree" else
                   f"pushed as the spill candidate (cost {p['cost']} / degree {p['degree']} the lowest)")
            if p.get("unlocked_by") is not None:
                how += f", once %{p['unlocked_by']} was removed"
            out.append(f"      {how}")
        s = v.get("select")
        if s:
            out.append(f"      select: neighbours held "
                       + (describe_taken(cls, {int(c): w for c, w in s["taken"].items()}) or "nothing")
                       + f"; free {mask_text(cls, s['free'])}; {s['how']} -> {reg_name(cls, s['color'])}")
    shared = sorted({n["vreg"] for n in a.get("neighbors", [])} & {n["vreg"] for n in b.get("neighbors", [])})
    interfere = any(n["vreg"] == b["vreg"] for n in a.get("neighbors", []))
    out.append(f"   they {'interfere' if interfere else 'do not interfere'}; common neighbours: "
               + (" ".join(f"%{n}" if n >= PHYSICAL else reg_name(cls, n) for n in shared) or "none"))
    if a.get("order") is not None and b.get("order") is not None:
        early, late = (a, b) if a["order"] < b["order"] else (b, a)
        pe, pl = early.get("pushed", {}), late.get("pushed", {})
        out.append(f"   %{early['vreg']} is coloured before %{late['vreg']} because the simplification pushed it later "
                   f"(the stack is coloured from the top):")
        if pe.get("pass") == pl.get("pass") and pe.get("why") == pl.get("why") == "degree":
            out.append(f"      both were pushed in pass {pe.get('pass')}, which takes the nodes in increasing number: "
                       f"%{late['vreg']} first, %{early['vreg']} after it")
            out.append(f"      to swap them, %{late['vreg']} would need the higher number (be created after "
                       f"%{early['vreg']}: define it later), or one of them would have to be pushed in another pass "
                       f"(a different degree: interfere with one value more or fewer)")
        else:
            out.append(f"      %{late['vreg']} was pushed in pass {pl.get('pass')} and %{early['vreg']} in pass "
                       f"{pe.get('pass')}: a node is pushed in the first pass in which its remaining degree is below "
                       f"K, so %{early['vreg']} needed more of its neighbours removed first")
            out.append(f"      to swap them, change the degrees: make %{early['vreg']} interfere with fewer values, or "
                       f"%{late['vreg']} with more")
        if interfere and early.get("select", {}).get("how") == "lowest free":
            out.append(f"      as they interfere, %{early['vreg']} took the lowest free register and "
                       f"%{late['vreg']} had to take another")
    return out


SCHEMA_VERSION = 5


def analyze_file(trace, function=None, link=None, version="GC_1_3_2"):
    """The analyses of a trace file (or of its lines) without compiling: one dict per allocated function that FUNCTION
    (a substring of the name or of the link name) and LINK (the exact link name) select, as --json prints them
    (`schema_version` says which fields to expect), with the inliner's decisions made before it and, when the trace
    has them, the stack frame and the scheduling."""
    lines = Path(trace).read_text().splitlines() if isinstance(trace, (str, Path)) else list(trace)
    allocations, inliner = parse_trace(lines)
    names = opcode_names(version)
    bodies = body_costs(inliner)
    out, start = [], 0
    for name, before, after, upto in allocations:
        events = inliner[start:upto]
        start = upto
        if not matches(function, name, before.link) or (link and before.link != link):
            continue
        analysis = analyze(name, before, after, names)
        analysis["schema_version"] = SCHEMA_VERSION
        analysis["inlining"] = inlining_decisions(events, bodies)
        analysis["schedule"] = analyze_schedule(before.schedule, names)
        out.append(analysis)
    return out


def similar_functions(allocations, wanted, limit=12):
    """The traced functions whose name or link name looks like WANTED: (name, link)."""
    import difflib
    wanted = wanted.lower()
    # (the share of WANTED's words found in the name or the link name, then how alike the short name is)
    words = re.findall(r"[a-z0-9_]{2,}", wanted)
    scored = []
    for name, before, _, _ in allocations:
        both = (name + " " + before.link).lower()
        found = sum(w in both for w in words) / len(words) if words else 0.0
        score = (found, difflib.SequenceMatcher(None, wanted, name.lower()).ratio())
        scored.append((score, name, before.link))
    scored.sort(key=lambda s: (-s[0][0], -s[0][1]))
    seen, out = set(), []
    for _, name, link in scored:
        if (name, link) not in seen:
            seen.add((name, link))
            out.append((name, link))
        if len(out) == limit:
            break
    return out


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


def matches(wanted, function, link):
    """--function: a substring of the function's name or of its link (mangled) name."""
    return not wanted or wanted in function or wanted in (link or "")


def raw_for(lines, wanted):
    """The trace lines of the functions --function selects: the inliner events before each, its allocation snapshots,
    the colouring between them and its stack frame after."""
    out, group, name, after_done = [], [], None, False

    def close():
        if name is not None and matches(wanted, *name):
            out.extend(group)

    for line in lines:
        if after_done and not line.startswith("fr."):
            close()
            group, name, after_done = [], None, False
        group.append(line)
        m = re.match(r"ra\.(before|after) function=(\S*)(?: link=(.*))?$", line)
        if m:
            name = (m.group(2), m.group(3))
            if m.group(1) == "after":
                after_done = None  # (set at its ra.end)
        elif line == "ra.end" and after_done is None:
            after_done = True
        elif line == "fr.end" and after_done:
            close()
            group, name, after_done = [], None, False
    if after_done:
        close()
    return out


def ninja_rule(path, source):
    """The cflags, the mw_version and the source as spelled of the rule of a build.ninja that compiles SOURCE (relative
    to the build.ninja's directory, or absolute; / or \\, any case on Windows)."""
    text = re.sub(r"\$\r?\n\s*", "", Path(path).read_text())
    base = Path(path).resolve().parent

    def key(p):
        return os.path.normcase(os.path.normpath(base / p))

    wanted = key(source)
    for m in re.finditer(r"^build [^\n]*: mwcc\w* (\S+)[^\n]*\n((?:  [^\n]*\n)+)", text, re.M):
        if key(m.group(1).replace("$:", ":")) == wanted:
            block = m.group(2)
            flags = re.search(r"^  cflags = (.*)$", block, re.M)
            version = re.search(r"^  mw_version = (.*)$", block, re.M)
            if version is None:
                version = re.search(r"^mw_version = (.*)$", text, re.M)
            if flags:
                return (shlex.split(flags.group(1), posix=True),
                        version.group(1).strip().replace("\\", "/") if version else None, m.group(1).replace("$:", ":"))
    raise OracleError(f"{path} has no mwcc rule for {source}")


OPTIONS = {"--version": True, "--function": True, "--from-ninja": True, "--raw": False, "--pcode": False,
           "--no-check": False, "--json": False, "--schedule": False, "--inline-cost": True, "--exchange": True,
           "--override": True, "--near": True, "--full-cost": False, "--help": False, "-h": False}


def split_arguments(argv):
    """This script's options, SOURCE and the compiler's flags. The compiler's flags come after `--`, or (the older
    form) after SOURCE; this script's options can come anywhere before `--`, after SOURCE too (the compiler's flags
    have a single dash)."""
    options, positional, flags = [], [], []
    rest = list(argv)
    while rest:
        token = rest.pop(0)
        if token == "--":
            flags.extend(rest)
            break
        name = token.split("=", 1)[0]
        if name in OPTIONS:
            options.append(token)
            if OPTIONS[name] and "=" not in token and rest:
                options.append(rest.pop(0))
        elif positional:
            flags.append(token)
        elif token.startswith("-"):
            sys.exit(f"unknown option {token} before SOURCE: the compiler's flags go after SOURCE or after --")
        else:
            positional.append(token)
    return options, positional, flags


def object_symbols(data):
    """{name: (section index, value, size)} of an ELF32 big-endian object's function symbols."""
    import struct
    if data[:4] != b"\x7fELF":
        return {}
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, _ = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out = {}
    for _, kind, _, _, offset, size, link, _, _, entsize in headers:
        if kind != 2:  # (SHT_SYMTAB)
            continue
        strings = headers[link]
        for at in range(offset, offset + size, entsize or 16):
            name_offset, value, symbol_size, info, _, index = struct.unpack_from(">IIIBBH", data, at)
            if info & 0xf != 2:  # (STT_FUNC)
                continue
            start = strings[4] + name_offset
            name = data[start:data.index(b"\0", start)].decode("latin-1")
            out[name] = (index, value, symbol_size)
    return out


def object_differences(mine, theirs):
    """What differs between two objects: the sections, and for code sections the functions whose bytes differ."""
    sys.path.insert(0, str(ROOT / "tools"))
    from corpus import elf_sections
    a, b = elf_sections(mine), elf_sections(theirs)
    out = []
    for name in sorted(set(a) | set(b)):
        if a.get(name) == b.get(name):
            continue
        if name not in a or name not in b:
            out.append(f"section {name} is only in the {'original' if name in b else 'oracle'}'s object")
            continue
        out.append(f"section {name} differs")
    sa, sb = object_symbols(mine), object_symbols(theirs)
    for name in sorted(set(sa) | set(sb)):
        x, y = sa.get(name), sb.get(name)
        if x is None or y is None:
            out.append(f"function {name} is only in the {'original' if x is None else 'oracle'}'s object")
            continue

        def body(data, symbol):
            import struct
            shoff, = struct.unpack_from(">I", data, 0x20)
            shentsize, = struct.unpack_from(">H", data, 0x2E)
            header = struct.unpack_from(">IIIIIIIIII", data, shoff + symbol[0] * shentsize)
            return data[header[4] + symbol[1]:header[4] + symbol[1] + symbol[2]]

        if x[2] != y[2] or body(mine, x) != body(theirs, y):
            out.append(f"function {name} differs ({x[2]} bytes against {y[2]})")
    return out


def apply_overrides(overrides, flags, source, workdir, temp):
    """--override PATH=REPLACEMENT: compile with REPLACEMENT in place of PATH (relative to the directory compiled in)
    without touching the checkout. SOURCE itself is replaced by compiling the replacement, with SOURCE's directory
    searched first for its includes; a header is copied under a temporary directory searched just before the include
    directory (-i/-I) it is found in. The new flags and source."""
    flags = list(flags)
    for k, item in enumerate(overrides):
        if "=" not in item:
            raise OracleError(f"--override {item}: give PATH=REPLACEMENT")
        path, replacement = item.split("=", 1)
        replacement = Path(replacement).resolve()
        if not replacement.exists():
            raise OracleError(f"--override: {replacement} does not exist")
        target = (workdir / path).resolve()
        if target == (workdir / source).resolve():
            flags = ["-i", str(target.parent)] + flags
            source = str(replacement)
            continue
        for n in range(len(flags) - 1):
            if flags[n] not in ("-i", "-I"):
                continue
            root = (workdir / flags[n + 1]).resolve()
            try:
                relative = target.relative_to(root)
            except ValueError:
                continue
            copy = Path(temp) / f"override{k}" / relative
            copy.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(replacement, copy)
            flags[n:n] = [flags[n], str(Path(temp) / f"override{k}")]
            break
        else:
            raise OracleError(f"--override: {path} is not under an include directory of the flags (-i/-I), so it cannot be "
                     "replaced without changing the checkout")
    return flags, source


class OracleError(SystemExit):
    """A compile that could not be done (no rule for the source, a bad override, no oracle, the compiler failed): the
    command line prints it and exits; a script can catch it."""


def compile_trace(source, flags=(), from_ninja=None, version=None, overrides=(), schedule=False, inline_cost=False,
                  check=True, full_cost=False):
    """Compile SOURCE with the oracle and return its trace: a dict with `lines`, `version`, `oracle`, `source` (as the
    rule spells it), `identical` (None when not checked, or the original compiler is not there) and `differences`.
    FROM_NINJA takes the flags, the working directory and the version from a project's build.ninja (FLAGS are then
    not allowed); otherwise FLAGS are the compiler's flags, run in the current directory. OVERRIDES are
    `PATH=REPLACEMENT` strings, as --override takes them. Raises OracleError."""
    warning = None
    if from_ninja:
        if flags:
            raise OracleError(f"--from-ninja takes the compiler's flags from {from_ninja}; these were given too: "
                              + " ".join(flags))
        flags, project, source = ninja_rule(from_ninja, source)
        workdir = Path(from_ninja).resolve().parent
        inferred = None
        if project:
            m = re.match(r"GC/(\d+)\.(\d+)(?:\.(\d+))?(\w*)$", project)
            if m:
                inferred = "GC_" + "_".join(x for x in m.group(1, 2, 3) if x) + m.group(4)
        if version and inferred and version != inferred:
            warning = (f"WARNING: {from_ninja} builds {source} with {project} ({inferred}), not {version}; using "
                       f"{version} as asked")
        version = version or inferred or "GC_1_3_2"
    else:
        flags, workdir = list(flags), Path.cwd()
        version = version or "GC_1_3_2"
    oracle = ROOT / f"build/{version}/oracle/mwcceppc.exe"
    if not oracle.exists():
        raise OracleError(f"{oracle} does not exist: python configure.py --version {version} --oracle, then ninja "
                          f"build/{version}/oracle/mwcceppc.exe")
    # -MD, -MMD and -c are the project's; the object goes to a temporary file
    flags = [f for f in flags if f not in ("-MD", "-MMD", "-c")]
    result = {"version": version, "oracle": str(oracle.relative_to(ROOT)), "source": source, "identical": None,
              "differences": [], "warning": warning}
    with tempfile.TemporaryDirectory() as temp:
        compiled = source
        if overrides:
            flags, compiled = apply_overrides(list(overrides), flags, source, workdir, temp)
        trace = Path(temp) / "trace.txt"
        obj = Path(temp) / "oracle.o"
        env = dict(os.environ, MWCC_ORACLE_TRACE_FILE=str(trace))
        if schedule:
            env["MWCC_ORACLE_SCHEDULE"] = "1"
        if inline_cost:
            env["MWCC_ORACLE_INLINE_COST"] = "1"
        if full_cost:
            env["MWCC_ORACLE_FULL_COST"] = "1"
        run = subprocess.run([*launcher(oracle), *flags, "-c", compiled, "-o", str(obj)], cwd=workdir, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if run.returncode:
            raise OracleError(f"{run.stdout}\nthe oracle compiler failed ({run.returncode})")
        result["lines"] = trace.read_text().splitlines() if trace.exists() else []
        if check:
            original = ROOT / f"build/compilers/GC/{version[3:].replace('_', '.')}/mwcceppc.exe"
            if original.exists():
                other = Path(temp) / "original.o"
                subprocess.run([*launcher(original), *flags, "-c", compiled, "-o", str(other)], cwd=workdir,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                result["identical"] = other.exists() and other.read_bytes() == obj.read_bytes()
                if result["identical"] is False and other.exists():
                    result["differences"] = object_differences(obj.read_bytes(), other.read_bytes())
    return result


def analyze_source(source, flags=(), from_ninja=None, version=None, overrides=(), function=None, link=None,
                   schedule=False, inline_cost=False, check=False, near=None, full_cost=False):
    """Compile SOURCE (as compile_trace does) and analyse it: the document --json prints, with `schema_version`,
    `identical`, `functions` (as analyze_file gives them), `inlining_after_last_function`, `similar` (when FUNCTION
    matches nothing) and, with NEAR, `near` (near_threshold's decisions)."""
    compiled = compile_trace(source, flags, from_ninja, version, overrides, schedule, inline_cost, check, full_cost)
    return document_for(compiled, function, link, near)[0]


def document_for(compiled, function=None, link=None, near=None):
    """analyze_source's document for a compile_trace result, with parse_trace's allocations and inliner events."""
    lines = compiled["lines"]
    allocations, inliner = parse_trace(lines)
    analyses = analyze_file(lines, function, link, version=compiled["version"])
    document = {"schema_version": SCHEMA_VERSION, "oracle": compiled["oracle"], "version": compiled["version"],
                "source": compiled["source"], "identical": compiled["identical"],
                "differences": compiled["differences"], "functions": analyses}
    last = allocations[-1][3] if allocations else 0
    after = inlining_decisions(inliner[last:], body_costs(inliner)) if inliner[last:] and not function and not link else []
    if after:
        document["inlining_after_last_function"] = after
    if function and not analyses:
        document["similar"] = [{"function": n, "link": l} for n, l in similar_functions(allocations, function)]
    if near is not None:
        document["near"] = near_threshold(analyses, after, near)
    return document, allocations, inliner


def main():
    options, positional, extra = split_arguments(sys.argv[1:])
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--version")
    parser.add_argument("--function")
    parser.add_argument("--from-ninja")
    parser.add_argument("--raw", action="store_true")
    parser.add_argument("--pcode", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--no-check", action="store_true")
    parser.add_argument("--schedule", action="store_true")
    parser.add_argument("--inline-cost")
    parser.add_argument("--exchange")
    parser.add_argument("--override", action="append", default=[])
    parser.add_argument("--near")
    parser.add_argument("--full-cost", action="store_true")
    arguments = parser.parse_args(options)
    if len(positional) != 1:
        parser.error("one SOURCE is needed")
    arguments.source = positional[0]
    near = None
    if arguments.near is not None:
        near = int(arguments.near)
    compiled = compile_trace(arguments.source, extra, arguments.from_ninja, arguments.version, arguments.override,
                             arguments.schedule, bool(arguments.inline_cost) or near is not None,
                             not arguments.no_check, arguments.full_cost)
    if compiled["warning"]:
        print(compiled["warning"], file=sys.stderr)
    arguments.version = compiled["version"]
    oracle = ROOT / compiled["oracle"]
    lines, check, differences = compiled["lines"], compiled["identical"], compiled["differences"]
    document, allocations, inliner = document_for(compiled, arguments.function, near=near)
    analyses = document["functions"]
    if arguments.json:
        import json
        if arguments.raw:
            document["raw"] = raw_for(lines, arguments.function) if arguments.function else lines
        print(json.dumps(document, indent=1))
        return
    print(f"oracle: {oracle.relative_to(ROOT)} ({arguments.version}); the original's code with trace wrappers"
          + ("" if check is None else "; object identical to the original compiler's: " + ("yes" if check else
                                                                                           "NO - the oracle differs")))
    if check is False:
        print("WARNING: the oracle's object differs from the original compiler's; the explanation may not hold")
        for difference in differences:
            print(f"   {difference}")
    if arguments.raw:
        print("\n".join(raw_for(lines, arguments.function) if arguments.function else lines))
    names = opcode_names(arguments.version)
    types = enode_names()
    if near is not None:
        print("\n".join(near_report(document["near"], near)))
        return

    def about(d):
        return d["event"] == "decision" and (arguments.inline_cost in d["callee"] or
                                             arguments.inline_cost in d.get("link", ""))

    for analysis in analyses:
        title = f"{analysis['function']} ({analysis['link']})"
        if arguments.inline_cost:
            # (only the decisions about that callee, each with what its cost estimate is made of)
            wanted = [d for d in analysis["inlining"] if about(d)]
            if wanted:
                print(f"== {title}: inlining {arguments.inline_cost} ==")
                for d in wanted:
                    print("\n".join(inlining_report([d], links=True)))
                    print("\n".join(cost_report(d, types)))
            continue
        if arguments.exchange:
            print("\n".join(explain_exchange(analysis, *arguments.exchange.split(",", 1))))
            continue
        if analysis["inlining"]:
            print(f"== {title}: inlining ==")
            print("\n".join(inlining_report(analysis["inlining"])))
        if arguments.pcode:
            before = next(b for n, b, _, _ in allocations if n == analysis["function"] and b.link == analysis["link"])
            print("\n".join(pcode_report(analysis["function"], before, names)))
        print("\n".join(explain(analysis)))
        print("\n".join(frame_report(analysis)))
        if arguments.schedule:
            print("\n".join(schedule_report(analysis["function"], analysis["schedule"])))
    last = allocations[-1][3] if allocations else 0
    if inliner[last:] and not arguments.function and not arguments.exchange:
        decisions = inlining_decisions(inliner[last:], body_costs(inliner))
        if arguments.inline_cost:
            decisions = [d for d in decisions if about(d)]
        if decisions:
            print("== inlining after the last allocated function ==")
            for d in decisions:
                print("\n".join(inlining_report([d], links=bool(arguments.inline_cost))))
                if arguments.inline_cost:
                    print("\n".join(cost_report(d, types)))
    if arguments.function and not analyses:
        print(f"no function matches {arguments.function}; the traced functions most like it (name, link name):")
        for name, link in similar_functions(allocations, arguments.function):
            print(f"   {name}  {link}")
    elif not allocations and not inliner:
        print("no trace: the source produced no function code")


if __name__ == "__main__":
    main()
