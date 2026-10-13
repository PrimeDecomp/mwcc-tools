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
  --json            print the report as JSON
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
    def __init__(self, pointer, opcode, flags, operands, line=None):
        self.pointer, self.opcode, self.flags, self.operands, self.line = pointer, opcode, flags, operands, line

    def text(self, names):
        return f"{names.get(self.opcode, 'op' + str(self.opcode))} " + ", ".join(o.text() for o in self.operands)


class Snapshot:
    def __init__(self, function, link=None):
        self.function = function
        self.link = link or function  # (the link name: Think__10CPowerBombFfR13CStateManager)
        self.blocks = []  # [(pointer, index, weight, [successor pointers], [Instruction])]
        # (on the before snapshot: the colouring rounds and the stack frame traced during the allocation, see
        # parse_trace)
        self.rounds = []
        self.frame = None


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
    frame = None
    for line in lines:
        line = line.rstrip("\n")
        m = re.match(r"ra\.(before|after) function=(\S*)(?: link=(.*))?$", line)
        if m:
            current = Snapshot(m.group(2), m.group(3))
            pending[m.group(1)] = current
            if m.group(1) == "before":
                rounds = []
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
            frame = []
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
            frame = None
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

    def push(i, reason):
        pushes.append((i, reason))
        for n in r.nodes[i]["neighbors"]:
            if n in degree:
                degree[n] -= 1
        done.add(i)

    def low_degree():
        while True:
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
              "frame": before.frame}
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
    """The locals and the compiler's temporaries in the stack frame, by offset (in the locals area: the offset from r1
    adds the linkage area, 8 bytes, and the outgoing arguments), and the locals kept in registers."""
    frame = analysis["frame"]
    if frame is None:
        return []
    out = [f"== {analysis['function']}: stack frame =="]
    memory = sorted((e for e in frame if e["used"] and e["reg"] == 0), key=lambda e: (e["offset"], e["kind"]))
    if memory:
        out.append("   in memory (offset in the locals area, size, name):")
        out.extend(f"      {e['offset']:>5} {e['size']:>5}  {e['name']}{' (temporary)' if e['kind'] == 'temp' else ''}"
                   for e in memory)
    registers = [e for e in frame if e["used"] and e["reg"] != 0]
    if registers:
        out.append("   in registers: " + ", ".join(f"{e['name']}=reg {e['reg']}" for e in registers))
    if not memory and not registers:
        out.append("   nothing in the frame")
    return out


def inlining_decisions(events):
    """The decisions of the inliner, each with its reason, with how many times each was made (in the order of the
    first)."""
    decisions, seen = [], {}
    cost = None

    def add(record):
        depth = record.pop("depth", None)
        key = tuple(sorted(record.items()))
        if key not in seen:
            record["count"] = 0
            seen[key] = record
            decisions.append(record)
        seen[key]["count"] += 1
        if depth is not None:
            depths = seen[key].setdefault("depths", [])
            if depth not in depths:
                depths.append(depth)

    for kind, f in events:
        if kind == "inl.cost":
            cost = f
        elif kind == "inl.can":
            name = f.get("function", "?")
            result = f.get("result") == "1"
            depth = f.get("depth")
            record = {"event": "decision", "callee": name, "inline": result, "depth": int(depth or 0)}
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
                    reason = f"estimated cost {c} {'is within' if result else 'exceeds'} the threshold {t}"
                    if result and f.get("nstmts") and int(f["nstmts"]) > 0:
                        reason += f" ({f['nstmts']} statements)"
                else:
                    reason = "auto-inline decision"
            else:
                reason = (f"depth {depth} with {f.get('nstmts')} statements "
                          f"({'within' if result else 'over'} the limit for this depth)")
            record["reason"] = reason
            add(record)
            cost = None
        elif kind == "inl.end":
            add({"event": "expansion", "callee": f.get("function", "?"), "expanded": f.get("expanded") != "0"})
    return decisions


def inlining_report(decisions):
    out = []
    for d in decisions:
        times = f"  (x{d['count']})" if d["count"] > 1 else ""
        if any(d.get("depths", ())):
            times += "  (depth " + ", ".join(str(x) for x in sorted(d["depths"])) + ")"
        if d["event"] == "decision":
            out.append(f"   {d['callee']}: {'INLINE' if d['inline'] else 'call kept'} - {d['reason']}{times}")
        elif d["expanded"]:
            out.append(f"      {d['callee']}: expanded in place{times}")
        else:
            out.append(f"      {d['callee']}: could not be expanded here (its body is not an expression-only body at "
                       f"this stage){times}")
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
    """The cflags and the mw_version of the rule of a build.ninja that compiles SOURCE."""
    text = re.sub(r"\$\r?\n\s*", "", Path(path).read_text())
    for variant in dict.fromkeys((source, source.replace("/", "\\"), source.replace("\\", "/"))):
        m = re.search(r"^build [^\n]*: mwcc\w* " + re.escape(variant) + r"\b[^\n]*\n((?:  [^\n]*\n)+)", text, re.M)
        if m:
            block = m.group(1)
            flags = re.search(r"^  cflags = (.*)$", block, re.M)
            version = re.search(r"^  mw_version = (.*)$", block, re.M)
            if version is None:
                version = re.search(r"^mw_version = (.*)$", text, re.M)
            if flags:
                return (shlex.split(flags.group(1), posix=True),
                        version.group(1).strip().replace("\\", "/") if version else None)
    sys.exit(f"{path} has no mwcc rule for {source}")


OPTIONS = {"--version": True, "--function": True, "--from-ninja": True, "--raw": False, "--pcode": False,
           "--no-check": False, "--json": False, "--help": False, "-h": False}


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
    arguments = parser.parse_args(options)
    if len(positional) != 1:
        parser.error("one SOURCE is needed")
    arguments.source = positional[0]
    if arguments.from_ninja:
        if extra:
            sys.exit(f"--from-ninja takes the compiler's flags from {arguments.from_ninja}; these were given too: "
                     + " ".join(extra))
        flags, project = ninja_rule(arguments.from_ninja, arguments.source)
        workdir = Path(arguments.from_ninja).resolve().parent
        inferred = None
        if project:
            m = re.match(r"GC/(\d+)\.(\d+)(?:\.(\d+))?(\w*)$", project)
            if m:
                inferred = "GC_" + "_".join(x for x in m.group(1, 2, 3) if x) + m.group(4)
        if arguments.version and inferred and arguments.version != inferred:
            print(f"WARNING: {arguments.from_ninja} builds {arguments.source} with {project} ({inferred}), not "
                  f"{arguments.version}; using {arguments.version} as asked", file=sys.stderr)
        arguments.version = arguments.version or inferred or "GC_1_3_2"
    else:
        flags, workdir = extra, Path.cwd()
        arguments.version = arguments.version or "GC_1_3_2"
    oracle = ROOT / f"build/{arguments.version}/oracle/mwcceppc.exe"
    if not oracle.exists():
        sys.exit(f"{oracle} does not exist: python configure.py --version {arguments.version} --oracle, then ninja "
                 f"build/{arguments.version}/oracle/mwcceppc.exe")
    # -MD, -MMD and -c are the project's; the object goes to a temporary file
    flags = [f for f in flags if f not in ("-MD", "-MMD", "-c")]
    differences = []
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
                if check is False and other.exists():
                    differences = object_differences(obj.read_bytes(), other.read_bytes())
    allocations, inliner = parse_trace(lines)
    names = opcode_names(arguments.version)
    results, start = [], 0
    for function, before, after, upto in allocations:
        events = inliner[start:upto]
        start = upto
        if matches(arguments.function, function, before.link):
            results.append((function, before, after, events))
    if arguments.json:
        import json
        document = {"oracle": str(oracle.relative_to(ROOT)), "version": arguments.version, "identical": check,
                    "differences": differences, "functions": []}
        for function, before, after, events in results:
            analysis = analyze(function, before, after, names)
            analysis["inlining"] = inlining_decisions(events)
            document["functions"].append(analysis)
        if inliner[start:] and not arguments.function:
            document["inlining_after_last_function"] = inlining_decisions(inliner[start:])
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
    for function, before, after, events in results:
        if events:
            print(f"== {function} ({before.link}): inlining ==")
            print("\n".join(inlining_report(inlining_decisions(events))))
        if arguments.pcode:
            print("\n".join(pcode_report(function, before, names)))
        analysis = analyze(function, before, after, names)
        print("\n".join(explain(analysis)))
        print("\n".join(frame_report(analysis)))
    if inliner[start:] and not arguments.function:
        print("== inlining after the last allocated function ==")
        print("\n".join(inlining_report(inlining_decisions(inliner[start:]))))
    if not results and not inliner:
        print("no trace: the source produced no function code" if not allocations else "no function matches")


if __name__ == "__main__":
    main()
