"""python -m unittest discover tests: tools/colouring.py on small graphs, the register field decoder and, when the
GC_2_7 oracle is built, a compile of a small function."""
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import colouring  # noqa: E402
import oracle  # noqa: E402


def graph(k, mask, edges, coalesced=(), colours=None):
    """A Round of the GPR class with K colours and the colour MASK, from EDGES ([(a, b)]: a node a physical register
    below 32, or a virtual one); COALESCED {node: what it was coalesced into}."""
    r = oracle.Round(4, k, 32, 64)
    r.mask = mask
    nodes = {}
    for a, b in edges:
        for x, y in ((a, b), (b, a)):
            if x >= 32:
                nodes.setdefault(x, set()).add(y)
    for i, neighbours in nodes.items():
        r.nodes[i] = {"p": hex(i), "degree": len(neighbours), "flags": 4 if i in coalesced else 0,
                      "color": dict(coalesced).get(i, -1), "neighbors": sorted(neighbours)}
    order, _ = colouring.simplify(r)
    r.order = [(i, 0) for i in order]
    r.colors = {i: (c, 2) for i, c in (colours or {}).items()}
    return r


class FakeAllocation:
    def __init__(self, r, names=None):
        self.r, self.names, self.function = r, names or {}, "f"

    def last_round(self, cls):
        return self.r

    def register_count(self, cls):
        return None

    def name(self, cls, v):
        return self.names.get(v)


# (three values live across calls: each interferes with the volatile colours r0 and r3 and with the others)
ACROSS = [(a, b) for a in (32, 33, 34) for b in (0, 3)] + [(32, 33), (32, 34), (33, 34)]


class Replay(unittest.TestCase):
    def test_claims_from_r31_down(self):
        r = graph(8, 0b1001, ACROSS)
        result = colouring.replay(r, "GPR")
        # (scanned 32, 33, 34 in one pass; the stack is coloured from 34)
        self.assertEqual(result["order"], [34, 33, 32])
        self.assertEqual(result["colour"], {34: 31, 33: 30, 32: 29})
        self.assertEqual(result["claims"], [31, 30, 29])

    def test_used_register_is_skipped(self):
        r = graph(8, 0b1001, ACROSS + [(32, 31)])
        self.assertEqual(colouring.preused(r, "GPR")[0], {31})
        self.assertEqual(colouring.replay(r, "GPR")["colour"], {34: 30, 33: 29, 32: 28})

    def test_move_changes_the_order(self):
        r = graph(8, 0b1001, ACROSS)
        result = colouring.replay(r, "GPR", edits=[("move", 32, "after", 34)])
        self.assertEqual(result["order"], [32, 34, 33])
        self.assertEqual(result["colour"][32], 31)

    def test_move_before_the_lowest_stays_virtual(self):
        r = graph(8, 0b1001, ACROSS)
        graph_, rename = colouring.edited(r, [("move", 34, "before", 32)])
        self.assertEqual(rename, {34: 32, 32: 33, 33: 34})
        self.assertEqual(colouring.replay(r, "GPR", edits=[("move", 34, "before", 32)])["colour"],
                         {33: 31, 32: 30, 34: 29})

    def test_order_edit(self):
        r = graph(8, 0b1001, ACROSS)
        result = colouring.replay(r, "GPR", edits=[("order", (34, 32, 33))])
        self.assertEqual(result["order"], [33, 32, 34])

    def test_drop_lowers_the_degree(self):
        # (32 interferes with 33, 34 and 35; with K=3 it is pushed in the second pass, after them)
        r = graph(3, 0b1001, [(32, 33), (32, 34), (32, 35)])
        self.assertEqual(colouring.replay(r, "GPR")["order"][0], 32)
        dropped = colouring.replay(r, "GPR", edits=[("drop", 33)])
        self.assertEqual(dropped["order"], [35, 34, 32])

    def test_verify_agrees_with_its_own_trace(self):
        r = graph(8, 0b1001, ACROSS + [(32, 31)])
        r.colors = {i: (c, 2) for i, c in colouring.replay(r, "GPR")["colour"].items()}
        allocation = FakeAllocation(r)
        allocation.link = "f"
        allocation.rounds = lambda cls: [r]
        self.assertTrue(colouring.verify_function(allocation, "GPR")["ok"])


class Search(unittest.TestCase):
    def setUp(self):
        # (32 interferes with a coalesced argument 34 (in r6): with it, its degree is K and it is pushed after 33)
        edges = [(32, 0), (32, 3), (32, 33), (32, 34), (33, 0), (33, 3), (34, 6)]
        self.r = graph(4, 0b1001, edges, coalesced={34: 6})

    def test_force_and_search_find_the_drop(self):
        problem = colouring.Problem(FakeAllocation(self.r), "GPR", {32: 30, 33: 31})
        self.assertEqual(problem.base["colour"][32], 31)
        self.assertEqual(problem.base["wrong"], [32, 33])
        self.assertTrue(colouring.force(problem)["exact"])
        found = colouring.search(problem, kinds=("drop",), iterations=200)
        self.assertEqual(found["singles"][0], (0, ("drop", 34)))
        self.assertEqual(found["exact"][0][1], (("drop", 34),))

    def test_renumbered(self):
        self.assertEqual(colouring.renumbered([51, 32, 44, 45]), [51])
        self.assertEqual(colouring.inverted([33, 32, 34], [32, 33, 34]), 1)


class Fields(unittest.TestCase):
    def test_decoder(self):
        self.assertEqual(colouring.register_fields(0x7c0802a6), [("GPR", "D", 0, "d")])  # mflr r0
        self.assertEqual(colouring.register_fields(0x5718063e),  # clrlwi r24, r24, 24
                         [("GPR", "D", 24, "u"), ("GPR", "A", 24, "d")])
        self.assertEqual(colouring.register_fields(0x7c7f1b78),  # mr r31, r3
                         [("GPR", "D", 3, "u"), ("GPR", "A", 31, "d"), ("GPR", "B", 3, "u")])
        self.assertEqual(colouring.register_fields(0x80010024),  # lwz r0, 0x24(r1)
                         [("GPR", "D", 0, "d"), ("GPR", "A", 1, "u")])
        self.assertEqual(colouring.register_fields(0x38600000), [("GPR", "D", 3, "d")])  # li r3, 0
        self.assertEqual(colouring.register_fields(0xc03f0010),  # lfs f1, 0x10(r31)
                         [("FPR", "D", 1, "d"), ("GPR", "A", 31, "u")])
        self.assertIsNone(colouring.register_fields(0x4e800020))  # blr

    def test_shape_ignores_registers(self):
        self.assertEqual(colouring.shape(0x7c7f1b78), colouring.shape(0x7c9e2378))  # mr r31,r3 / mr r30,r4


SOURCE = """extern int f(int);
int g(int a, int b, int c) {
    int x = f(a);
    int y = f(b);
    int z = f(c);
    return x + y + z + a + b + c;
}
"""


@unittest.skipUnless((ROOT / "build/GC_2_7/oracle/mwcceppc.exe").exists(), "the GC_2_7 oracle is not built")
class Compiled(unittest.TestCase):
    def test_verify_a_compile(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "g.c"
            source.write_text(SOURCE)
            compiled = colouring.compile_source(str(source), ["-O4,p", "-proc", "gekko", "-nodefaults"],
                                                version="GC_2_7")
        allocation, = colouring.allocations(compiled, "g")
        result = colouring.verify_function(allocation, "GPR")
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["claims"])


if __name__ == "__main__":
    unittest.main()
