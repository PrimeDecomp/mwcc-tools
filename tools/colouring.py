#!/usr/bin/env python3
"""python tools/colouring.py COMMAND [options] SOURCE -- [COMPILER FLAGS...]: what-if experiments on the register
colouring of one function, replayed from the oracle's trace (Coloring.c's simplification and select), to find the
smallest change of the interference graph that gives the target's registers.

Commands:
  verify   replay the simplification and select (with the registers select claims predicted, not read back from the
           trace) and check they give the traced order and colours, for every function (or --function's); exits 1 on
           any disagreement. The other commands trust the replay only where verify agrees.
  target   the target's register for each virtual register: our object and the target's are paired instruction by
           instruction, and each register field of ours is traced back to the virtual register it holds
  force    whether the graph as it is can give the target's colours: the nodes are coloured in the traced order with
           the ones that claim non-volatile registers reordered to claim the target's; if that gives the target, the
           numbering or the degrees decide (search them), otherwise the graph itself must change
  search   the smallest change of the graph that gives the target's colours: each single change first (a node
           numbered elsewhere: a local declared elsewhere, a temporary made a named local; removing a node: a value
           the source no longer computes into a register of its own; a degree one higher or lower; an edge removed),
           then a simulated annealing over a renumbering with graph edits, the solutions tightened to the fewest
           nodes moved and edits
  replay   colour the graph with the edits given (--order, --move, --drop, --degree, --unlink, --link) and show what
           changes
  remap    pair the virtual registers of two compiles (two --load prefixes, or SOURCE and --override variants) by
           their instructions, and show the difference of their graphs (nodes renumbered out of order, nodes and
           edges on one side only)

Options (anywhere before `--`):
  --function NAME   the function (a substring of its name or link name; verify: all functions without it)
  --from-ninja FILE, --version V, --override P=R   as oracle.py takes them (the compile, and the target asm)
  --class C         the register class (GPR, FPR or VR; default GPR)
  --target-asm FILE the target's asm (default: the one --from-ninja's project builds)
  --want V=R        the target register of virtual register V (repeatable; replaces or completes what `target` derives)
  --save PREFIX     keep the trace and the object (PREFIX.trace, PREFIX.o) to --load them later
  --load PREFIX     use a saved trace and object instead of compiling (twice for remap)
  --iterations N    search: the annealing steps (default 20000; about 2-3 ms each on a function of 500 nodes)
  --seed N          search: the random seed (default 1); another seed can find other solutions
  --restarts N      search: the independent annealing runs the iterations are shared between (default 10)
  --fixed V,...     search: virtual registers not to renumber (a value the source cannot make a named local)
  --nodes V,...     search: renumber exactly these (default: the claimers, the registers wrong and the untargeted
                    nodes holding a non-volatile register)
  --top N           search: the solutions shown (default 8)
  --kinds LIST      search: the graph edits tried besides the renumbering, from drop,degree,unlink,link (default
                    drop,degree,unlink); `none` for a renumbering only
  --order V,...     replay: these virtual registers renumbered in that order (lowest first), the fewest of them
                    moved as declarations move
  --move V:before|after:W, --drop V, --degree V:+1, --unlink A,B, --link A,B   replay: the other edits
  --json            print the result as JSON

docs/oracle/colouring.md has the details."""
import argparse
import bisect
import itertools
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle  # noqa: E402

PHYSICAL = oracle.PHYSICAL
CLASS_NUMBERS = {name: number for number, name in oracle.CLASSES.items()}
# (Registers.c: Coloring_ClaimGPRColor and Coloring_ClaimFPRColor claim r31..r14, Coloring_ClaimVRColor v31..v20)
CLAIM_FLOOR = {"GPR": 14, "FPR": 14, "VR": 20}
# (how far from where the pairs around it put it an instruction moved by the scheduling is looked for)
WINDOW = 48


class Allocation:
    """One function's allocation from a trace: the snapshots before and after it, its colouring rounds, the names of
    its virtual registers (from the ce.vreg lines, when traced) and our object's words for it."""

    def __init__(self, function, before, after, names, words=None):
        self.function, self.before, self.after = function, before, after
        self.link = before.link
        self.names = names  # {(class, register): name}
        self.words = words

    def rounds(self, cls):
        return [r for r in self.before.rounds if oracle.CLASSES.get(r.cls) == cls]

    def last_round(self, cls):
        rounds = self.rounds(cls)
        return rounds[-1] if rounds else None

    def register_count(self, cls):
        rounds = self.rounds(cls)
        return rounds[0].count if rounds else None

    def name(self, cls, register):
        return self.names.get((cls, register))


def compile_source(source, flags=(), from_ninja=None, version=None, overrides=(), save=None):
    """compile_trace with the ce.vreg names traced (MWCC_ORACLE_CSE=2); SAVE keeps the trace and the object."""
    compiled = oracle.compile_trace(source, flags, from_ninja, version, overrides, check=False, cse=True)
    if save:
        Path(f"{save}.trace").write_text("\n".join(compiled["lines"]) + "\n")
        Path(f"{save}.o").write_bytes(compiled["object"])
    return compiled


def load_saved(prefix):
    lines = Path(f"{prefix}.trace").read_text().splitlines()
    obj = Path(f"{prefix}.o")
    return {"lines": lines, "object": obj.read_bytes() if obj.exists() else None}


def allocations(compiled, function=None):
    """[Allocation] of the functions FUNCTION selects in a compile_trace result (or a load_saved one)."""
    lines = compiled["lines"]
    records = oracle.cse_trace(lines)
    out = []
    for name, before, after, _ in oracle.parse_trace(lines)[0]:
        if not oracle.matches(function, name, before.link):
            continue
        names = {}
        for local, (register, cls, noregister) in records.get(before.link, {}).get("vregs", {}).items():
            if not noregister:
                names.setdefault((oracle.CLASSES.get(cls), register), local)
        words = oracle.function_words(compiled["object"], before.link) if compiled.get("object") else None
        out.append(Allocation(name, before, after, names, words))
    return out


# --- the graph and its edits ---

def edited(r, edits=()):
    """A copy of Round R with EDITS applied, and the renaming they made ({old index: new index}). An edit is a tuple:
    ("move", v, "before" | "after", w): v renumbered next to w, the nodes between shifting by one (as moving a
    declaration does); ("order", (v, ...)): the nodes given renumbered in that order, by moving the fewest of them; ("drop", v): the node removed, each neighbour's degree one lower; ("degree", v,
    delta): its degree changed; ("unlink", a, b) / ("link", a, b): an edge removed / added (both degrees change). The
    renumberings apply in the order given, before the other edits; the numbers stay whole and virtual."""
    rename = {}
    if any(e[0] in ("move", "order") for e in edits):
        slots = sorted(i for i in r.nodes if i >= PHYSICAL)
        sequence = list(slots)
        for e in edits:
            if e[0] == "order":
                # (the fewest nodes moved, each just after the one before it in the order (the first, before the
                # first that stays), as moving their declarations does)
                moved = set(renumbered(e[1]))
                staying = next((x for x in e[1] if x not in moved), None)
                for k, v in enumerate(e[1]):
                    if v in moved:
                        sequence.remove(v)
                        at = sequence.index(e[1][k - 1]) + 1 if k else sequence.index(staying)
                        sequence.insert(at, v)
            elif e[0] == "move":
                _, v, where, w = e
                sequence.remove(v)
                sequence.insert(sequence.index(w) + (where == "after"), v)
        rename = {v: slot for v, slot in zip(sequence, slots) if v != slot}
    new = oracle.Round(r.cls, r.k, r.start, r.count)
    new.mask, new.mask_after, new.ok = r.mask, r.mask_after, r.ok

    def name(i):
        return rename.get(i, i)

    # (copy on write: the node dicts are shared with R and replaced, never changed in place)
    new.nodes = dict(r.nodes)
    new.order, new.colors = r.order, r.colors
    if rename:
        touched = set()
        for v in rename:
            touched |= {n for n in r.nodes[v]["neighbors"] if n in r.nodes}
        for i in touched:
            new.nodes[i] = dict(r.nodes[i], neighbors=[name(n) for n in r.nodes[i]["neighbors"]])
        moved = {v: new.nodes.pop(v) for v in rename}
        for v, node in moved.items():
            new.nodes[name(v)] = node
        for i, node in new.nodes.items():
            if node["flags"] & 4 and node["color"] in rename:  # (coalesced into a renumbered node)
                new.nodes[i] = dict(node, color=rename[node["color"]])
        new.order = [(name(i), cost) for i, cost in r.order]
        new.colors = {name(i): c for i, c in r.colors.items()}
    for e in edits:
        kind = e[0]
        if kind == "drop":
            v = name(e[1])
            node = new.nodes.pop(v)
            for n in node["neighbors"]:
                if n in new.nodes and v in new.nodes[n]["neighbors"]:
                    new.nodes[n] = dict(new.nodes[n], neighbors=[x for x in new.nodes[n]["neighbors"] if x != v],
                                        degree=new.nodes[n]["degree"] - 1)
            new.order = [(i, c) for i, c in new.order if i != v]
        elif kind == "degree":
            v = name(e[1])
            new.nodes[v] = dict(new.nodes[v], degree=new.nodes[v]["degree"] + e[2])
        elif kind in ("unlink", "link"):
            a, b = name(e[1]), name(e[2])
            delta = -1 if kind == "unlink" else 1
            for x, y in ((a, b), (b, a)):
                if x not in new.nodes:
                    continue
                neighbors = list(new.nodes[x]["neighbors"])
                if kind == "unlink":
                    neighbors = [n for n in neighbors if n != y]
                elif y not in neighbors:
                    neighbors.append(y)
                new.nodes[x] = dict(new.nodes[x], neighbors=neighbors, degree=new.nodes[x]["degree"] + delta)
    return new, rename


def simplify(r, register_count=None):
    """The stack select colours (first coloured first) and the pushes, as Coloring_SimplifyGraph leaves them."""
    pushes, _ = oracle.simulate_simplify(r, register_count)
    return list(pushes)[::-1], pushes


def claim_gaps(r, cls):
    """The registers the trace shows select claimed, in order, and the ones those claims skipped (already used when
    the colouring began: Coloring_ResetGPRColors restores them from the registers bound before the allocation)."""
    floor = CLAIM_FLOOR.get(cls, 14)
    mask = r.mask or 0
    claims = []
    for i, _ in r.order:
        colour = r.colors.get(i, (-1, 0))[0]
        if colour >= floor and not mask >> colour & 1:
            claims.append(colour)
            mask |= 1 << colour
    skipped, above = set(), 31
    for colour in claims:
        skipped |= set(range(colour + 1, above + 1))
        above = colour - 1
    return claims, skipped


def preused(r, cls):
    """The non-volatile registers already used when select starts: those an instruction names (a physical node of the
    graph that is someone's neighbour, such as r31 holding the frame) and those the traced claims skipped. With no
    claim traced the second part is unknown (`inferred` says so)."""
    floor = CLAIM_FLOOR.get(cls, 14)
    named = {n for node in r.nodes.values() for n in node["neighbors"] if floor <= n < PHYSICAL}
    claims, skipped = claim_gaps(r, cls)
    return named | skipped, not claims


def select(r, order, used, cls):
    """Coloring_SelectColors on ORDER: each node takes the lowest colour of the mask its neighbours do not hold; when
    none is free, a non-volatile register is claimed, the highest one not USED yet, and joins the mask. Returns
    ({index: colour}, [the indices that could not be coloured], [the colours claimed, in order])."""
    floor = CLAIM_FLOOR.get(cls, 14)
    used = set(used)
    mask = r.mask or 0
    colour, spilled, claims = {}, [], []
    for i in order:
        taken = 0
        for n in r.nodes[i]["neighbors"]:
            if n < PHYSICAL:
                c = n
            elif n in colour:
                c = colour[n]
            else:
                c = r.nodes.get(n, {}).get("color", -1)
            if 0 <= c < PHYSICAL:
                taken |= 1 << c
        free = mask & ~taken
        if free:
            colour[i] = (free & -free).bit_length() - 1
            continue
        claim = next((c for c in range(31, floor - 1, -1) if c not in used), None)
        if claim is None:
            spilled.append(i)
            continue
        used.add(claim)
        claims.append(claim)
        mask |= 1 << claim
        colour[i] = claim
    return colour, spilled, claims


def final_colours(r, colour):
    """COLOUR (select's) with the coalesced nodes given their representative's colour."""
    out = dict(colour)
    for i, node in r.nodes.items():
        if i in out or not node["flags"] & 4:
            continue
        into, seen = node["color"], set()
        while into is not None and not (isinstance(into, int) and into < PHYSICAL) and into not in seen:
            seen.add(into)
            into = out.get(into, r.nodes.get(into, {}).get("color"))
        if isinstance(into, int) and 0 <= into < PHYSICAL:
            out[i] = into
    return out


def replay(r, cls, register_count=None, edits=(), used=None):
    """Simplify and select R with EDITS: {"order", "colour" (by original index), "spilled", "claims", "pushes"}."""
    graph, rename = edited(r, edits)
    order, pushes = simplify(graph, register_count)
    if used is None:
        used = preused(r, cls)[0]
    colour, spilled, claims = select(graph, order, used, cls)
    back = {new: old for old, new in rename.items()}
    colour = final_colours(graph, colour)
    return {"order": [back.get(i, i) for i in order], "colour": {back.get(i, i): c for i, c in colour.items()},
            "spilled": [back.get(i, i) for i in spilled], "claims": claims,
            "pushes": {back.get(i, i): p for i, p in pushes.items()}}


# --- verify ---

def verify_function(allocation, cls):
    """Whether the replay gives the traced order and colours of ALLOCATION's last colouring of CLS: a dict with
    `order_agrees`, `colour_disagreements` ([(index, replayed, traced)]), the claims, and `inferred` (no claim traced,
    so which non-volatile registers were already used is a guess)."""
    r = allocation.last_round(cls)
    if r is None:
        return None
    count = allocation.register_count(cls)
    result = replay(r, cls, count)
    traced = [i for i, _ in r.order]
    used, inferred = preused(r, cls)
    disagree = [(i, result["colour"].get(i), r.colors.get(i, (-1, 0))[0]) for i in traced
                if r.colors.get(i, (-1, 0))[0] >= 0 and result["colour"].get(i) != r.colors[i][0]]
    spilled_traced = [i for i in traced if r.colors.get(i, (-1, 0))[1] & 1]
    return {"function": allocation.function, "link": allocation.link, "class": cls, "nodes": len(r.order),
            "rounds": len(allocation.rounds(cls)), "order_agrees": result["order"] == traced,
            "colour_disagreements": disagree, "claims": result["claims"], "traced_claims": claim_gaps(r, cls)[0],
            "preused": sorted(used), "inferred": inferred, "spilled": result["spilled"],
            "traced_spilled": spilled_traced,
            "ok": result["order"] == traced and not disagree and result["spilled"] == spilled_traced}


def verify_report(results):
    out = []
    for v in results:
        head = f"{v['function']} {v['class']}: {v['nodes']} nodes"
        if v["rounds"] > 1:
            head += f", round {v['rounds']} (earlier ones spilled)"
        status = "OK" if v["ok"] else "DISAGREES"
        details = []
        if not v["order_agrees"]:
            details.append("the simplification's replay gives another order")
        if v["colour_disagreements"]:
            details.append("colours differ: " + ", ".join(f"%{i} {c} (traced {t})"
                                                          for i, c, t in v["colour_disagreements"][:8]))
        if v["spilled"] != v["traced_spilled"]:
            details.append(f"spilled {v['spilled']} (traced {v['traced_spilled']})")
        claims = (" claims " + ",".join(map(str, v["claims"])) if v["claims"] else "")
        guess = " (the registers used before the colouring are a guess: no claim traced)" if v["inferred"] and \
            v["claims"] else ""
        out.append(f"{status:9} {head}{claims}{guess}" + ("; " + "; ".join(details) if details else ""))
    return out


# --- the target's registers ---

# (the register fields of a PowerPC instruction word: D (bits 21..25), A (16..20), B (11..15), C (6..10))
SHIFTS = {"D": 21, "A": 16, "B": 11, "C": 6}
INT_LOADS = {32, 33, 34, 35, 40, 41, 42, 43}
INT_STORES = {36, 37, 38, 39, 44, 45}
FLOAT_LOADS = {48, 49, 50, 51, 56, 57}
FLOAT_STORES = {52, 53, 54, 55, 60, 61}
UPDATE_FORMS = {33, 35, 37, 39, 41, 43, 45, 49, 51, 53, 55, 57, 61}
X_ARITHMETIC = {266, 10, 138, 234, 202, 40, 8, 136, 232, 200, 104, 235, 75, 11, 491, 459}
X_LOGICAL = {28, 60, 444, 412, 316, 476, 124, 284, 24, 536, 792, 824, 26, 922, 954}
X_INT_LOADS = {23: False, 55: True, 87: False, 119: True, 279: False, 311: True, 343: False, 375: True, 534: False,
               790: False}
X_INT_STORES = {151: False, 183: True, 215: False, 247: True, 407: False, 439: True, 662: False, 918: False}
X_FLOAT_LOADS = {535: False, 567: True, 599: False, 631: True}
X_FLOAT_STORES = {663: False, 695: True, 727: False, 759: True, 983: False}
X_CACHE = {86, 54, 278, 246, 1014, 470, 982}
# (the A-form floating point instructions of opcodes 59 and 63 (and the paired singles of 4) by their 5-bit
# extended opcode: the fields they read besides D)
A_FORM = {18: "AB", 20: "AB", 21: "AB", 22: "B", 23: "ACB", 24: "B", 25: "AC", 26: "B", 28: "ACB", 29: "ACB",
          30: "ACB", 31: "ACB", 10: "ACB", 11: "ACB", 12: "AC", 13: "AC", 14: "ACB", 15: "ACB"}
FLOAT_X = {72: "B", 40: "B", 264: "B", 136: "B", 12: "B", 14: "B", 15: "B"}
PAIRED_X = {40: "B", 72: "B", 136: "B", 264: "B", 528: "AB", 560: "AB", 592: "AB", 624: "AB"}


def register_fields(word):
    """The register fields of an instruction WORD: [(class, field, register, role)], role "d" (written), "u" (read)
    or "du"; None for an instruction this does not decode (branches, the condition register, the prologue's lmw/stmw).
    An A field of 0 that reads as the value 0 (addi, addis, loads and stores) is left out."""
    op = word >> 26
    d, a, b, c = ((word >> s) & 31 for s in (21, 16, 11, 6))
    values = {"D": d, "A": a, "B": b, "C": c}
    out = []

    def add(cls, field, role):
        out.append((cls, field, values[field], role))

    def base(update=False):
        if a or update:
            add("GPR", "A", "du" if update else "u")

    if op in (7, 8, 12, 13):
        add("GPR", "D", "d")
        add("GPR", "A", "u")
    elif op in (14, 15):
        add("GPR", "D", "d")
        base()
    elif op in (10, 11):
        add("GPR", "A", "u")
    elif op in (24, 25, 26, 27, 28, 29, 21):
        add("GPR", "D", "u")
        add("GPR", "A", "d")
    elif op == 20:
        add("GPR", "D", "u")
        add("GPR", "A", "du")
    elif op == 23:
        add("GPR", "D", "u")
        add("GPR", "B", "u")
        add("GPR", "A", "d")
    elif op in INT_LOADS or op in INT_STORES or op in FLOAT_LOADS or op in FLOAT_STORES:
        add("FPR" if op in FLOAT_LOADS or op in FLOAT_STORES else "GPR", "D",
            "d" if op in INT_LOADS or op in FLOAT_LOADS else "u")
        base(op in UPDATE_FORMS)
    elif op == 31:
        xo, xo9 = (word >> 1) & 0x3ff, (word >> 1) & 0x1ff
        if xo9 in X_ARITHMETIC:
            add("GPR", "D", "d")
            add("GPR", "A", "u")
            if xo9 not in (234, 202, 232, 200, 104):
                add("GPR", "B", "u")
        elif xo in X_LOGICAL:
            add("GPR", "D", "u")
            add("GPR", "A", "d")
            if xo not in (824, 26, 922, 954):
                add("GPR", "B", "u")
        elif xo in (0, 32):
            add("GPR", "A", "u")
            add("GPR", "B", "u")
        elif xo in X_INT_LOADS or xo in X_INT_STORES or xo in X_FLOAT_LOADS or xo in X_FLOAT_STORES:
            table = next(t for t in (X_INT_LOADS, X_INT_STORES, X_FLOAT_LOADS, X_FLOAT_STORES) if xo in t)
            add("FPR" if table in (X_FLOAT_LOADS, X_FLOAT_STORES) else "GPR", "D",
                "d" if table in (X_INT_LOADS, X_FLOAT_LOADS) else "u")
            base(table[xo])
            add("GPR", "B", "u")
        elif xo in X_CACHE:
            base()
            add("GPR", "B", "u")
        elif xo in (339, 19, 83):  # (mfspr, mfcr, mfmsr)
            add("GPR", "D", "d")
        elif xo in (467, 144, 146):  # (mtspr, mtcrf, mtmsr)
            add("GPR", "D", "u")
        else:
            return None
    elif op in (59, 63):
        xo5, xo = (word >> 1) & 31, (word >> 1) & 0x3ff
        if op == 63 and xo in (0, 32):
            add("FPR", "A", "u")
            add("FPR", "B", "u")
        elif xo5 >= 18 and xo5 in A_FORM:
            add("FPR", "D", "d")
            for f in A_FORM[xo5]:
                add("FPR", f, "u")
        elif op == 63 and xo in FLOAT_X:
            add("FPR", "D", "d")
            add("FPR", "B", "u")
        else:
            return None
    elif op == 4:
        xo6, xo5, xo = (word >> 1) & 63, (word >> 1) & 31, (word >> 1) & 0x3ff
        if xo6 in (6, 38, 7, 39):  # (psq_lx, psq_lux, psq_stx, psq_stux)
            add("FPR", "D", "d" if xo6 in (6, 38) else "u")
            base(xo6 in (38, 39))
            add("GPR", "B", "u")
        elif xo in PAIRED_X:
            add("FPR", "D", "d")
            for f in PAIRED_X[xo]:
                add("FPR", f, "u")
        elif xo in (0, 32, 64, 96):
            add("FPR", "A", "u")
            add("FPR", "B", "u")
        elif xo5 in A_FORM:
            add("FPR", "D", "d")
            for f in A_FORM[xo5]:
                add("FPR", f, "u")
        else:
            return None
    else:
        return None
    return out


def shape(word):
    """WORD with its register fields and what differs between two compiles of the same code (oracle.instruction_token)
    masked: equal shapes are the same instruction up to registers."""
    fields = register_fields(word)
    if fields is None:
        return oracle.instruction_token(word)
    for _, field, _, _ in fields:
        word &= ~(31 << SHIFTS[field])
    return oracle.instruction_token(word)


def pcode_stream(allocation):
    """The instructions after the allocation that carry registers of the GPR and FPR classes, in block order:
    [(instruction, [(class, physical, virtual, role)])], calls left out (they name every volatile register)."""
    before = {i.pointer: i for _, _, _, _, ins in allocation.before.blocks for i in ins}
    out = []
    for _, _, _, _, instructions in allocation.after.blocks:
        for ins in instructions:
            registers = [o for o in ins.operands if o.is_register]
            if len(registers) > 4 or not any(o.cls in ("GPR", "FPR") for o in registers):
                continue
            original = before.get(ins.pointer)
            virtual = [o for o in original.operands if o.is_register] if original else [None] * len(registers)
            ops = []
            for o, v in zip(registers, virtual):
                if o.cls not in ("GPR", "FPR"):
                    continue
                role = ("d" if o.defines else "") + ("u" if o.uses else "")
                ops.append((o.cls, o.reg, v.reg if v is not None and v.cls == o.cls else o.reg, role or "u"))
            out.append((ins, ops))
    return out


def target_votes(allocation, theirs):
    """For each virtual register, the target's registers its fields have at the instructions paired with ours:
    {(class, virtual): Counter(target register)}, and the pairing's statistics. Our words are paired with the PCode
    after the allocation by the physical registers they name (in any order: the scheduling after the allocation moves
    instructions within their blocks; the prologue and epilogue are not in the PCode), and with the target's words by
    their shape (the same instruction up to registers)."""
    import difflib
    ours = allocation.words or []
    stream = pcode_stream(allocation)
    our_fields = [register_fields(w) for w in ours]
    # (as sets: mr is or rA,rS,rS, with its source in two fields)
    a = [tuple(sorted({(cls, reg) for cls, _, reg, _ in f})) if f else None for f in our_fields]
    b = [tuple(sorted({(cls, phys) for cls, phys, _, _ in ops})) for _, ops in stream]
    to_pcode = {}
    for x, y, size in difflib.SequenceMatcher(None, a, b, autojunk=False).get_matching_blocks():
        to_pcode.update((x + k, y + k) for k in range(size) if a[x + k] is not None)
    # (what the scheduling moved out of line: the nearest unpaired instruction naming the same registers, around
    # where the pairs on either side put it)
    taken = set(to_pcode.values())
    paired = sorted(to_pcode.items())
    for x in range(len(a)):
        if a[x] is None or x in to_pcode:
            continue
        at = bisect.bisect_left(paired, (x, -1))
        expected = paired[at - 1][1] + (x - paired[at - 1][0]) if at else x
        candidates = [y for y in range(max(0, expected - WINDOW), min(len(b), expected + WINDOW + 1))
                      if y not in taken and b[y] == a[x]]
        if candidates:
            y = min(candidates, key=lambda y: abs(y - expected))
            to_pcode[x] = y
            taken.add(y)
    to_target = {}
    shapes = difflib.SequenceMatcher(None, [shape(w) for w in ours], [shape(w) for w in theirs], autojunk=False)
    for x, y, size in shapes.get_matching_blocks():
        for k in range(size):
            to_target[x + k] = y + k
    votes, held = {}, {}
    for index, fields in enumerate(our_fields):
        if not fields or index not in to_pcode or index not in to_target:
            continue
        their_fields = register_fields(theirs[to_target[index]])
        if their_fields is None or len(their_fields) != len(fields):
            continue
        ops = stream[to_pcode[index]][1]
        for (cls, _, reg, role), (_, _, target, _) in zip(fields, their_fields):
            holders = {v for c, p, v, r in ops if c == cls and p == reg and set(role) & set(r)}
            if len(holders) != 1:
                continue
            virtual = holders.pop()
            if virtual >= PHYSICAL:
                votes.setdefault((cls, virtual), Counter())[target] += 1
                held[(cls, virtual)] = reg
    return votes, {"words": len(ours), "target_words": len(theirs), "paired_with_pcode": len(to_pcode),
                   "paired_with_target": len(to_target), "held": held}


def derive_targets(allocation, theirs, cls, wants=()):
    """The target register of each virtual register of class CLS, with the evidence: {"targets": {virtual:
    register}, "votes", "split" (the ones whose fields disagree), "conflicts" (two interfering nodes, or a node and a
    physical neighbour, given one register: a wrong pairing, or a want to check), "stats"}. WANTS ({virtual:
    register}) override what is derived."""
    votes, stats = target_votes(allocation, theirs)
    targets, split = {}, {}
    for (c, virtual), counter in votes.items():
        if c != cls:
            continue
        (best, n), = counter.most_common(1)
        targets[virtual] = best
        if n != sum(counter.values()):
            split[virtual] = dict(counter)
    targets.update(dict(wants))
    r = allocation.last_round(cls)
    held = stats.pop("held")
    conflicts, renumbered = [], []
    if r is not None:
        # (after a colouring that spilled, the last round's nodes need not be numbered as the PCode's registers)
        ours = final_colours(r, {i: r.colors[i][0] for i, _ in r.order if r.colors.get(i, (-1, 0))[0] >= 0})
        renumbered = sorted(v for (c, v), p in held.items() if c == cls and v in ours and ours[v] != p)
        for i, node in r.nodes.items():
            if i not in targets or node["flags"] & 4:
                continue
            for n in node["neighbors"]:
                if n < PHYSICAL and targets[i] == n:
                    conflicts.append((i, n))
                elif n > i and targets.get(n) == targets[i] and not r.nodes.get(n, {}).get("flags", 0) & 4:
                    conflicts.append((i, n))
    return {"targets": targets, "votes": {v: dict(c) for (k, v), c in votes.items() if k == cls}, "split": split,
            "conflicts": conflicts, "renumbered": renumbered, "stats": stats}


def target_words_for(allocation, target_asm):
    words = oracle.target_words(Path(target_asm), allocation.link)
    if not words:
        raise oracle.OracleError(f"{target_asm} has no function {allocation.link}")
    return words


# --- what-if: force, search, replay ---

class Problem:
    """One function's colouring of one class with the target's registers: the round, the registers used before the
    colouring, the target of each virtual register and the replay without edits."""

    def __init__(self, allocation, cls, targets):
        self.allocation, self.cls = allocation, cls
        self.round = allocation.last_round(cls)
        if self.round is None:
            raise oracle.OracleError(f"{allocation.function}: no {cls} colouring traced")
        self.count = allocation.register_count(cls)
        self.used = preused(self.round, cls)[0]
        self.floor = CLAIM_FLOOR.get(cls, 14)
        self.targets = {v: t for v, t in targets.items() if v in self.round.nodes}
        self.base = self.run(())

    def run(self, edits):
        result = replay(self.round, self.cls, self.count, edits, self.used)
        dropped = {e[1] for e in edits if e[0] == "drop"}
        result["wrong"] = sorted(v for v, t in self.targets.items() if result["colour"].get(v) != t and
                                 v not in dropped)
        return result

    def score(self, result):
        """The registers still wrong (None when a node is left uncoloured: a spill, not modelled)."""
        return None if result["spilled"] else len(result["wrong"])

    def label(self, v):
        if isinstance(v, int) and v < PHYSICAL:
            return oracle.reg_name(self.cls, v)
        name = self.allocation.name(self.cls, v)
        node = self.round.nodes.get(v, {})
        extra = name or ("coalesced" if node.get("flags", 0) & 4 else None)
        return f"%{v}" + (f" ({extra})" if extra else "")


def force(problem):
    """The replayed order with the nodes whose target is a non-volatile register reordered by that register, highest
    first (select claims r31 down): {"order", "colour", "wrong", "exact"}. Exact means the graph can give the target
    and only the order (the numbering, the degrees) is wrong; otherwise the graph itself must change."""
    order = list(problem.base["order"])
    targets = problem.targets
    claimed = [i for i in order if targets.get(i, -1) >= problem.floor]
    ranked = iter(sorted(claimed, key=lambda i: -targets[i]))  # (sorted is stable: ties keep the replayed order)
    forced = [next(ranked) if targets.get(i, -1) >= problem.floor else i for i in order]
    graph, _ = edited(problem.round)
    colour, spilled, _ = select(graph, forced, problem.used, problem.cls)
    colour = final_colours(graph, colour)
    wrong = sorted(v for v, t in targets.items() if colour.get(v) != t)
    return {"order": forced, "colour": colour, "wrong": wrong, "spilled": spilled, "exact": not wrong and not spilled}


def candidate_edits(problem, kinds):
    """The graph edits search may add to a renumbering, around the registers the replay gets wrong (KINDS of): the
    removal of a coalesced node next to a wrong one (a value no longer computed into a register of its own, such as an
    argument), a degree one higher or lower for a node taking part in the claims, an edge between a wrong node and
    such a node removed (or, with `link`, added)."""
    r = problem.round
    wrong = set(problem.base["wrong"])
    colour = problem.base["colour"]
    movers = {i for i in r.nodes if i >= PHYSICAL and not r.nodes[i]["flags"] & 4 and
              (i in wrong or colour.get(i, -1) >= problem.floor or problem.targets.get(i, -1) >= problem.floor)}
    edits = []
    if "drop" in kinds:
        near = {n for v in wrong for n in r.nodes[v]["neighbors"] if n >= PHYSICAL}
        edits.extend(("drop", n) for n in sorted(near) if r.nodes.get(n, {}).get("flags", 0) & 4)
    if "degree" in kinds:
        for v in sorted(movers):
            edits.extend([("degree", v, 1), ("degree", v, -1)])
    if "unlink" in kinds:
        for v in sorted(wrong):
            edits.extend(("unlink", min(v, n), max(v, n)) for n in r.nodes[v]["neighbors"] if n in movers)
    if "link" in kinds:
        for v in sorted(wrong):
            edits.extend(("link", min(v, n), max(v, n)) for n in sorted(movers)
                         if n != v and n not in r.nodes[v]["neighbors"])
    return list(dict.fromkeys(edits))


def claimers(problem, result=None):
    """The first node of each non-volatile target register in the order that gives them (the targets highest first;
    among the nodes of one register, as RESULT (the replay without edits) orders them)."""
    position = {v: k for k, v in enumerate((result or problem.base)["order"])}
    out, seen = [], set()
    for v in sorted((v for v, t in problem.targets.items() if t >= problem.floor and v in position),
                    key=lambda v: (-problem.targets[v], position[v])):
        if problem.targets[v] not in seen:
            seen.add(problem.targets[v])
            out.append(v)
    return out


def search_nodes(problem, fixed=()):
    """The nodes search renumbers: the claimers, the nodes coloured wrong and the nodes holding a non-volatile
    register with no target (not the coalesced ones, nor FIXED)."""
    nodes = set(claimers(problem)) | set(problem.base["wrong"])
    nodes |= {v for v, c in problem.base["colour"].items() if c >= problem.floor and v not in problem.targets}
    return sorted(v for v in nodes if v >= PHYSICAL and not problem.round.nodes[v]["flags"] & 4 and v not in fixed)


def inverted(order, original):
    """The pairs of ORDER in the other order than in ORIGINAL (how far a renumbering is from the source's)."""
    rank = {v: k for k, v in enumerate(original)}
    ranks = [rank[v] for v in order]
    return sum(1 for k, x in enumerate(ranks) for y in ranks[k + 1:] if x > y)


def search(problem, kinds=("drop", "degree", "unlink"), iterations=20000, seed=1, nodes=None, fixed=(), keep=8,
           restarts=10, progress=None):
    """What gives the target's registers with the smallest change: each graph edit (candidate_edits of KINDS) and each
    node of NODES numbered alone just after another (`move`) are tried first (`singles`: the ones that leave fewer registers wrong), then a
    simulated annealing over a
    renumbering of NODES (search_nodes's by default) and a set of graph edits, RESTARTS times from the best single
    change (ITERATIONS steps in all):
    first towards no register wrong (the registers wrong, then the claims in the wrong order), then, once exact,
    towards the smallest change (the nodes moved out of their order and the graph edits, 100 each, then the pairs of
    nodes renumbered the other way round). The solutions are tightened (each swap put back, each edit removed, while
    it stays exact). Returns {"exact": [(cost, edits)] (best first, at most KEEP), "best": (wrong, edits) when none
    is exact, "singles": [(wrong, edit)], "nodes"}."""
    import math
    import random
    rng = random.Random(seed)
    nodes = list(nodes if nodes is not None else search_nodes(problem, fixed))
    extras = candidate_edits(problem, kinds)
    chain = claimers(problem)
    rank = {v: k for k, v in enumerate(nodes)}

    def edits_of(order, chosen):
        return ((("order", tuple(order)),) if order != nodes else ()) + tuple(sorted(chosen))

    def wrong_order(result):
        position = {v: k for k, v in enumerate(result["order"])}
        ranks = [position.get(v, -1) for v in chain]
        return sum(1 for k, x in enumerate(ranks) for y in ranks[k + 1:] if x > y)

    def jumped(edit):
        """The nodes of NODES a move takes its node past (0 for the other edits)."""
        if edit[0] != "move":
            return 0
        _, v, where, w = edit
        low, high = sorted((v, w))
        passed = where == ("before" if w < v else "after")  # (w itself)
        return sum(1 for x in nodes if low < x < high) + (passed and w in rank)

    def evaluate(order, chosen):
        """(registers wrong, cost), or (None, None) when a node is left uncoloured."""
        result = problem.run(edits_of(order, chosen))
        wrong = problem.score(result)
        if wrong is None:
            return None, None
        if wrong:
            return wrong, 10 ** 6 + wrong * 1000 + wrong_order(result) + 50 * len(chosen)
        return 0, (100 * (len(renumbered(order)) + len(chosen)) + inverted(order, nodes) +
                   sum(jumped(e) for e in chosen))

    def put_back(order, v):
        """ORDER with V put back where the original numbering has it among the nodes that keep their order."""
        moved = set(renumbered(order))
        rest = [x for x in order if x != v]
        at = next((k for k, x in enumerate(rest) if x not in moved and rank[x] > rank[v]), len(rest))
        return rest[:at] + [v] + rest[at:]

    def merge(order, chosen):
        """ORDER (exact) with two or three moved nodes put back together, or two put back and one node moved instead,
        while it stays exact."""
        improved = True
        while improved:
            improved = False
            moved = renumbered(order)
            for size in (2, 3):
                for group in itertools.combinations(moved, size):
                    trial = order
                    for v in group:
                        trial = put_back(trial, v)
                    if len(renumbered(trial)) < len(moved) and evaluate(trial, chosen)[0] == 0:
                        order, improved = trial, True
                        break
                if improved:
                    break
            if improved:
                continue
            for a, b in itertools.combinations(moved, 2):
                back = put_back(put_back(order, a), b)
                for u in back:
                    rest = [x for x in back if x != u]
                    for at in range(len(back)):
                        trial = rest[:at] + [u] + rest[at:]
                        if len(renumbered(trial)) < len(moved) and evaluate(trial, chosen)[0] == 0:
                            order, improved = trial, True
                            break
                    if improved:
                        break
                if improved:
                    break
        return order

    def tighten(order, chosen, limit):
        """ORDER and CHOSEN with moved nodes and swaps put back and edits removed while no more than LIMIT are
        wrong."""
        improved = True
        while improved:
            improved = False
            for v in renumbered(order):
                moved = set(renumbered(order))
                trial = put_back(order, v)
                if len(renumbered(trial)) < len(moved):
                    wrong, _ = evaluate(trial, chosen)
                    if wrong is not None and wrong <= limit:
                        order, improved = trial, True
            for k in range(len(order) - 1):
                if rank[order[k]] > rank[order[k + 1]]:
                    trial = order[:k] + [order[k + 1], order[k]] + order[k + 2:]
                    wrong, _ = evaluate(trial, chosen)
                    if wrong is not None and wrong <= limit:
                        order, improved = trial, True
            for x in sorted(chosen):
                wrong, _ = evaluate(order, chosen - {x})
                if wrong is not None and wrong <= limit:
                    chosen, improved = chosen - {x}, True
        return order, chosen

    base, _ = evaluate(nodes, set())
    singles, starts = [], []
    for edit in extras:
        wrong, _ = evaluate(nodes, {edit})
        if wrong is not None and wrong < base:
            singles.append((wrong, edit))
            starts.append((wrong, list(nodes), {edit}))
    # (and each node alone numbered just after another, every number between shifting, as moving a declaration does)
    for v in nodes:
        for edit in [("move", v, "before", nodes[0])] + [("move", v, "after", w) for w in nodes]:
            if edit[3] == v or (edit[2] == "before" and nodes[0] == v):
                continue
            wrong, _ = evaluate(nodes, {edit})
            if wrong is not None and wrong < base:
                singles.append((wrong, edit))
                starts.append((wrong, list(nodes), {edit}))
                extras.append(edit)
    singles.sort(key=lambda x: (x[0], jumped(x[1])))
    starts.sort(key=lambda x: (x[0], sum(jumped(e) for e in x[2])))
    start = (starts[0][1], starts[0][2]) if starts else (list(nodes), set())
    wrong, cost = evaluate(*start)
    best_wrong = (wrong, *start)
    exact = {edits_of(order, chosen): evaluate(order, chosen)[1] for w, order, chosen in starts if w == 0}
    for restart in range(restarts if wrong else 0):
        (order, chosen), wrong, cost = (list(start[0]), set(start[1])), best_wrong[0], evaluate(*start)[1]
        temperature = 200.0
        for step in range(iterations // restarts):
            new_order, new_chosen = list(order), set(chosen)
            roll = rng.random()
            if extras and (roll < 0.2 or len(nodes) < 2):
                new_chosen ^= {rng.choice(extras)}
            elif len(nodes) >= 2:
                i, j = rng.sample(range(len(nodes)), 2)
                if roll < 0.6:
                    new_order[i], new_order[j] = new_order[j], new_order[i]
                else:
                    new_order.insert(j, new_order.pop(i))
            else:
                break
            if not all(compatible(tuple(e for e in new_chosen if e != x), x) for x in new_chosen):
                continue
            new_wrong, new_cost = evaluate(new_order, new_chosen)
            if new_cost is None:
                continue
            if new_cost <= cost or rng.random() < math.exp((cost - new_cost) / temperature):
                order, chosen, wrong, cost = new_order, new_chosen, new_wrong, new_cost
                if wrong and wrong < best_wrong[0]:
                    best_wrong = (wrong, order, chosen)
                if not wrong:
                    exact.setdefault(edits_of(order, chosen), cost)
            temperature = max(temperature * 0.999, 0.5)
            if progress and step % 2000 == 0:
                progress(restart, step, wrong, min(exact.values()) if exact else None)
    tightened = {}
    for e, _ in sorted(exact.items(), key=lambda x: x[1])[:3 * keep]:
        order, chosen = tighten(list(next((x[1] for x in e if x[0] == "order"), nodes)),
                                {x for x in e if x[0] != "order"}, 0)
        tightened.setdefault(edits_of(order, chosen), evaluate(order, chosen)[1])
    # (the best few: two moves made one, then tightened again)
    for e, _ in sorted(tightened.items(), key=lambda x: x[1])[:2]:
        order = next((x[1] for x in e if x[0] == "order"), None)
        if order and len(renumbered(order)) >= 2:
            chosen = {x for x in e if x[0] != "order"}
            order, chosen = tighten(merge(list(order), chosen), chosen, 0)
            tightened.setdefault(edits_of(order, chosen), evaluate(order, chosen)[1])
    ranked = sorted(((c, e) for e, c in tightened.items()), key=lambda x: (x[0], len(x[1])))[:keep]
    best = None
    if not ranked:
        order, chosen = tighten(best_wrong[1], best_wrong[2], best_wrong[0])
        best = (best_wrong[0], edits_of(order, chosen))
    return {"exact": ranked, "best": best, "singles": singles[:keep], "nodes": nodes}


def compatible(edits, edit):
    """Whether EDIT can join EDITS: not twice the same edit or a node moved twice, nothing about a removed node."""
    if edit in edits or (edit[0] == "move" and any(e[0] == "move" and e[1] == edit[1] for e in edits)):
        return False
    dropped = {e[1] for e in edits if e[0] == "drop"}
    if dropped & {x for x in edit[1:] if isinstance(x, int)}:
        return False
    return not (edit[0] == "drop" and any(edit[1] in e[1:] for e in edits))


def renumbered(order):
    """The fewest nodes of ORDER (a renumbering) to move for the others to keep their order: all but a longest run
    increasing in the original numbers."""
    best = []  # (best[k]: the indices of a longest increasing run ending at k)
    for k, v in enumerate(order):
        run = max((best[j] for j in range(k) if order[j] < v), key=len, default=[])
        best.append(run + [k])
    keep = {order[k] for k in max(best, key=len, default=[])}
    return [v for v in order if v not in keep]


def describe_edit(problem, edit):
    kind = edit[0]
    if kind == "order":
        moved = renumbered(edit[1])
        return ("numbered, lowest first: " + " < ".join(problem.label(v) + ("*" if v in moved else "")
                                                         for v in edit[1]) + " (* moved: the others keep their order)")
    if kind == "move":
        return f"number {problem.label(edit[1])} just {edit[2]} {problem.label(edit[3])}"
    if kind == "drop":
        return f"remove {problem.label(edit[1])}"
    if kind == "degree":
        return f"degree of {problem.label(edit[1])} {edit[2]:+d}"
    return f"{'remove' if kind == 'unlink' else 'add'} the edge {problem.label(edit[1])} - {problem.label(edit[2])}"


def vreg_number(text):
    return int(text.strip().lstrip("%").lstrip("rfv"))


def parse_edits(arguments):
    """replay's edits: --order V,V,..., --move V:before|after:W, --drop V, --degree V:+1, --unlink A,B, --link A,B."""
    edits = [("order", tuple(vreg_number(v) for v in text.split(","))) for text in arguments.order]
    for text in arguments.move:
        v, where, w = text.split(":")
        edits.append(("move", vreg_number(v), where, vreg_number(w)))
    edits += [("drop", vreg_number(v)) for v in arguments.drop]
    for text in arguments.degree:
        v, delta = text.split(":")
        edits.append(("degree", vreg_number(v), int(delta)))
    for kind in ("unlink", "link"):
        for text in getattr(arguments, kind):
            a, b = text.split(",")
            edits.append((kind, vreg_number(a), vreg_number(b)))
    return edits


# --- remap ---

def normalized(instruction, names):
    """An instruction's text with its virtual registers' numbers left out (the physical ones kept)."""
    return re.sub(r"%(cr|[rfv])\d+", r"%\1", instruction.text(names))


def remap(first, second, version="GC_2_7"):
    """The virtual registers of allocation FIRST paired with SECOND's: their PCode before the allocation paired
    instruction by instruction (by the text without the virtual numbers), each operand voting. {"mapping": {(class,
    virtual of FIRST): virtual of SECOND}, "unsure" (the ones with several votes), "paired", "instructions"}."""
    import difflib
    names = oracle.opcode_names(version)
    a = [i for _, _, _, _, ins in first.before.blocks for i in ins]
    b = [i for _, _, _, _, ins in second.before.blocks for i in ins]
    matcher = difflib.SequenceMatcher(None, [normalized(i, names) for i in a], [normalized(i, names) for i in b],
                                      autojunk=False)
    votes, paired = {}, 0
    for x, y, size in matcher.get_matching_blocks():
        for k in range(size):
            paired += 1
            for p, q in zip(a[x + k].operands, b[y + k].operands):
                if p.is_register and q.is_register and p.cls == q.cls and p.reg >= PHYSICAL:
                    votes.setdefault((p.cls, p.reg), Counter())[q.reg] += 1
    return {"mapping": {key: counter.most_common(1)[0][0] for key, counter in votes.items()},
            "unsure": {key: dict(counter) for key, counter in votes.items() if len(counter) > 1},
            "paired": paired, "instructions": (len(a), len(b))}


def registers_in(allocation, cls):
    """The virtual registers of class CLS that an instruction names, before the allocation."""
    return {o.reg for _, _, _, _, ins in allocation.before.blocks for i in ins for o in i.operands
            if o.is_register and o.cls == cls and o.reg >= PHYSICAL}


def graph_difference(first, second, mapping, cls):
    """What changed in the interference graph of class CLS from allocation FIRST to SECOND, through MAPPING (remap's):
    {"moved": the nodes numbered out of their order (the fewest, in SECOND's numbers), "only_first", "only_second" (the
    nodes an instruction names on one side only), "edges_added", "edges_removed" (between nodes on both sides, in
    SECOND's numbers)}."""
    a, b = first.last_round(cls), second.last_round(cls)
    if a is None or b is None:
        return None
    pairs = {v: mapping[(cls, v)] for v in a.nodes if v >= PHYSICAL and mapping.get((cls, v)) in b.nodes}
    back = {w: v for v, w in pairs.items()}

    def mapped(n):
        return n if n < PHYSICAL else pairs.get(n)

    added, removed = set(), set()
    for v, w in pairs.items():
        mine = {mapped(n) for n in a.nodes[v]["neighbors"]} - {None}
        theirs = {n for n in b.nodes[w]["neighbors"] if n < PHYSICAL or n in back}
        added |= {tuple(sorted((w, n))) for n in theirs - mine}
        removed |= {tuple(sorted((w, n))) for n in mine - theirs}
    return {"moved": sorted(renumbered([pairs[v] for v in sorted(pairs)])),
            "only_first": sorted(v for v in registers_in(first, cls) & set(a.nodes) if v not in pairs),
            "only_second": sorted(w for w in registers_in(second, cls) & set(b.nodes) if w not in back),
            "edges_added": sorted(added), "edges_removed": sorted(removed)}


# --- the command line ---

OPTIONS = {"--function": True, "--from-ninja": True, "--version": True, "--override": True, "--class": True,
           "--target-asm": True, "--want": True, "--save": True, "--load": True, "--iterations": True, "--seed": True, "--restarts": True,
           "--fixed": True, "--nodes": True,
           "--top": True, "--kinds": True, "--json": False, "--move": True, "--drop": True, "--degree": True,
           "--unlink": True, "--link": True, "--order": True, "--help": False, "-h": False}
COMMANDS = ("verify", "target", "force", "search", "replay", "remap")


def split_arguments(argv):
    """This script's options, COMMAND and SOURCE, and the compiler's flags (after `--`, or after SOURCE)."""
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
        elif len(positional) >= 2:
            flags.append(token)
        elif token.startswith("-"):
            sys.exit(f"unknown option {token}: the compiler's flags go after SOURCE or after --")
        else:
            positional.append(token)
    return options, positional, flags


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in ("--function", "--from-ninja", "--version", "--target-asm", "--kinds", "--save"):
        p.add_argument(name)
    p.add_argument("--class", dest="cls")
    for name in ("--override", "--want", "--load", "--order", "--move", "--drop", "--degree", "--unlink", "--link"):
        p.add_argument(name, action="append", default=[])
    p.add_argument("--iterations", type=int, default=20000)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--restarts", type=int, default=10)
    p.add_argument("--fixed", default="")
    p.add_argument("--nodes", default="")
    p.add_argument("--top", type=int, default=8)
    p.add_argument("--json", action="store_true")
    return p


def obtain(arguments, source, flags, load=None, overrides=None, save=None):
    if load:
        return load_saved(load)
    if not source:
        raise oracle.OracleError("a SOURCE (or --load PREFIX) is needed")
    return compile_source(source, flags, arguments.from_ninja, arguments.version,
                          arguments.override if overrides is None else overrides, save)


def one_allocation(compiled, function):
    found = allocations(compiled, function)
    if len(found) > 1:
        found = [a for a in found if a.function == function or a.link == function] or found
    if len(found) != 1:
        raise oracle.OracleError(f"{function!r} matches {len(found)} functions"
                                 + (": " + ", ".join(a.link for a in found[:8]) if found else ""))
    return found[0]


def targets_for(arguments, allocation, source, cls):
    asm = arguments.target_asm
    if asm is None and arguments.from_ninja and source:
        import inline_diff
        asm = inline_diff.target_asm(inline_diff.target_directory(arguments.from_ninja), source)
    wants = {}
    for text in arguments.want:
        v, register = text.split("=")
        wants[vreg_number(v)] = vreg_number(register)
    if asm is None:
        if not wants:
            raise oracle.OracleError("no target asm (--target-asm FILE, or --from-ninja) and no --want")
        return {"targets": wants, "votes": {}, "split": {}, "conflicts": [], "renumbered": [], "stats": {}}
    return derive_targets(allocation, target_words_for(allocation, asm), cls, wants)


def target_report(problem, derived):
    cls = problem.cls
    out = [f"== {problem.allocation.function}: {cls} targets ==", f"   pairing: {derived['stats']}"]
    if derived["renumbered"]:
        out.append(f"   WARNING: {len(derived['renumbered'])} registers hold other colours in the PCode than in the "
                   "last colouring round (a spill round renumbered them): these targets cannot be trusted")
    for i, j in derived["conflicts"]:
        out.append(f"   CONFLICT: {problem.label(i)} and {problem.label(j)} interfere but both get "
                   f"{oracle.reg_name(cls, derived['targets'][i])}: a wrong pairing, or a --want to check")
    colour = problem.base["colour"]
    out.append(f"   {len(problem.targets)} registers with a target, {len(problem.base['wrong'])} wrong:")
    for v in problem.base["wrong"]:
        votes = derived["votes"].get(v)
        out.append(f"      {problem.label(v)}: ours {oracle.reg_name(cls, colour.get(v, -1))}, target "
                   f"{oracle.reg_name(cls, problem.targets[v])}"
                   + (f" (the fields disagree: {votes})" if votes and len(votes) > 1 else ""))
    untargeted = [i for i in problem.base["order"] if i not in problem.targets and colour.get(i, -1) >= problem.floor]
    if untargeted:
        out.append("   non-volatile registers with no target (no paired field): "
                   + ", ".join(problem.label(i) for i in untargeted))
    return out


def remap_command(arguments, source, flags, cls):
    if len(arguments.load) == 2:
        first, second = load_saved(arguments.load[0]), load_saved(arguments.load[1])
    elif source and arguments.override:
        first = obtain(arguments, source, flags, overrides=[])
        second = obtain(arguments, source, flags)
    else:
        raise oracle.OracleError("remap needs two --load prefixes, or SOURCE with --override (the variant)")
    a, b = one_allocation(first, arguments.function), one_allocation(second, arguments.function)
    result = remap(a, b, arguments.version or "GC_2_7")
    changed = {v: w for (c, v), w in sorted(result["mapping"].items()) if c == cls and v != w}
    difference = graph_difference(a, b, result["mapping"], cls)
    if arguments.json:
        return {"class": cls, "mapping": {v: w for (c, v), w in result["mapping"].items() if c == cls},
                "changed": changed, "unsure": {v: u for (c, v), u in result["unsure"].items() if c == cls},
                "paired": result["paired"], "instructions": result["instructions"], "graph": difference}

    def label(allocation, v):
        name = allocation.name(cls, v)
        return f"%{v}" + (f" ({name})" if name else "")

    out = [f"== {a.function}: {result['paired']} of {result['instructions'][0]} / {result['instructions'][1]} "
           f"instructions paired =="]
    back = {w: v for v, w in changed.items()}
    if difference:
        if difference["moved"]:
            out.append("   numbered out of their order (the second's numbers, from the first's): "
                       + ", ".join(f"{label(b, w)} from %{back.get(w, w)}" for w in difference["moved"]))
        for key, side, allocation in (("only_first", "only in the first", a), ("only_second", "only in the second",
                                                                                b)):
            if difference[key]:
                out.append(f"   nodes {side}: " + ", ".join(label(allocation, v) for v in difference[key]))
        for key, what in (("edges_added", "edges added"), ("edges_removed", "edges removed")):
            if difference[key]:
                out.append(f"   {what} (the second's numbers): "
                           + ", ".join(f"{label(b, x)} - {label(b, y)}" for x, y in difference[key][:40])
                           + (f" ... {len(difference[key]) - 40} more" if len(difference[key]) > 40 else ""))
    out.append(f"   {len(changed)} {cls} registers renumbered: "
               + " ".join(f"%{v}->%{w}" for v, w in list(changed.items())[:60])
               + (" ..." if len(changed) > 60 else ""))
    for (c, v), counter in sorted(result["unsure"].items()):
        if c == cls:
            out.append(f"   %{v}: several candidates {counter}")
    return out


def run_command(command, arguments, source, flags):
    """The command's report: a list of lines, a dict (--json), or (lines, ok) for verify."""
    cls = (arguments.cls or "GPR").upper()
    if command == "remap":
        return remap_command(arguments, source, flags, cls)
    compiled = obtain(arguments, source, flags, arguments.load[0] if arguments.load else None, save=arguments.save)
    if command == "verify":
        classes = [cls] if arguments.cls else ["GPR", "FPR", "VR"]
        results = [v for a in allocations(compiled, arguments.function) for c in classes
                   for v in [verify_function(a, c)] if v]
        ok = all(v["ok"] for v in results)
        if arguments.json:
            return {"functions": results, "ok": ok}
        out = verify_report(results)
        out.append(f"{sum(v['ok'] for v in results)} of {len(results)} colourings replayed exactly")
        return out, ok
    allocation = one_allocation(compiled, arguments.function)
    derived = targets_for(arguments, allocation, source, cls)
    problem = Problem(allocation, cls, derived["targets"])
    head = f"== {allocation.function}: {cls}, {len(problem.base['wrong'])} registers wrong"
    if command == "target":
        if arguments.json:
            return {"function": allocation.function, "class": cls, "targets": problem.targets,
                    "wrong": problem.base["wrong"], "votes": derived["votes"], "conflicts": derived["conflicts"],
                    "renumbered": derived["renumbered"], "stats": derived["stats"]}
        return target_report(problem, derived)
    if command == "force":
        f = force(problem)
        if arguments.json:
            return {"function": allocation.function, "class": cls, "wrong": problem.base["wrong"], **f}
        out = [head + " ==", "   the claiming nodes ordered by their target: "
               + " ".join(problem.label(i) for i in f["order"] if problem.targets.get(i, -1) >= problem.floor)]
        if f["exact"]:
            out.append("   EXACT: the graph can give the target's colours; only the order is wrong (the numbering or "
                       "the degrees: search them)")
        else:
            out.append("   still wrong: " + ", ".join(problem.label(v) for v in f["wrong"])
                       + (f"; uncoloured: {f['spilled']}" if f["spilled"] else "")
                       + ": no order of the claims gives the target; the graph must change (its edges or nodes)")
        return out
    if command == "replay":
        edits = tuple(parse_edits(arguments))
        result = problem.run(edits)
        if arguments.json:
            return {"function": allocation.function, "class": cls, "edits": edits, "wrong": result["wrong"],
                    "spilled": result["spilled"], "colour": result["colour"], "order": result["order"]}
        out = [f"== {allocation.function}: {cls} with "
               + ("; ".join(describe_edit(problem, e) for e in edits) or "no edit") + " ==",
               f"   wrong: {len(problem.base['wrong'])} -> {len(result['wrong'])}"
               + (f"; UNCOLOURED (a spill, not modelled): {result['spilled']}" if result["spilled"] else "")]
        for v in sorted(set(problem.base["colour"]) | set(result["colour"])):
            before, after = problem.base["colour"].get(v), result["colour"].get(v)
            if before != after:
                target = problem.targets.get(v)
                out.append(f"   {problem.label(v)}: {oracle.reg_name(cls, before) if before is not None else '-'} -> "
                           f"{oracle.reg_name(cls, after) if after is not None else '-'}"
                           + (f" (target {oracle.reg_name(cls, target)})" if target is not None else ""))
        return out
    if command == "search":
        kinds = () if arguments.kinds == "none" else tuple((arguments.kinds or "drop,degree,unlink").split(","))
        listed = [vreg_number(v) for v in arguments.nodes.split(",") if v.strip()]
        found = search(problem, kinds, arguments.iterations, arguments.seed, listed or None,
                       [vreg_number(v) for v in arguments.fixed.split(",") if v.strip()], arguments.top,
                       arguments.restarts,
                       progress=lambda restart, step, wrong, cost: print(
                           f"   run {restart + 1}, step {step}: {wrong} wrong"
                           + (f", smallest exact change {cost}" if cost is not None else ""), file=sys.stderr))
        if arguments.json:
            return {"function": allocation.function, "class": cls, "wrong": problem.base["wrong"],
                    "nodes": found["nodes"], "exact": [{"change": c, "edits": e} for c, e in found["exact"]],
                    "best": found["best"], "singles": found["singles"]}
        out = [head + ": " + ", ".join(problem.label(v) for v in problem.base["wrong"]) + " =="]
        if found["singles"]:
            out.append("   single changes that leave fewer wrong:")
            out.extend(f"      {wrong} wrong: {describe_edit(problem, e)}" for wrong, e in found["singles"])
        out.append("   renumbering " + " ".join(problem.label(v) for v in found["nodes"]))
        for change, edits in found["exact"]:
            out.append(f"   EXACT (change {change}):")
            out.extend(f"      {describe_edit(problem, e)}" for e in edits)
        if found["best"]:
            wrong, edits = found["best"]
            out.append(f"   no exact solution found; the best leaves {wrong} wrong:")
            out.extend(f"      {describe_edit(problem, e)}" for e in edits)
        return out
    raise oracle.OracleError(f"unknown command {command}")


def main():
    options, positional, flags = split_arguments(sys.argv[1:])
    arguments = parser().parse_args(options)
    if not positional or positional[0] not in COMMANDS:
        parser().error("a COMMAND is needed: " + ", ".join(COMMANDS))
    command, source = positional[0], positional[1] if len(positional) > 1 else None
    if command not in ("verify", "remap") and not arguments.function:
        parser().error(f"{command} needs --function")
    result = run_command(command, arguments, source, flags)
    ok = True
    if isinstance(result, tuple):
        result, ok = result
    if isinstance(result, dict):
        print(json.dumps(result, indent=1, default=str))
        ok = result.get("ok", True)
    else:
        print("\n".join(result))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
