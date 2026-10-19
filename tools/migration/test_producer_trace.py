"""Ensure producer parity cannot hide numeric changes while remapping pointers."""
import unittest
from verify_producer import graph_trace, full_trace, trace_line_endings


def sample(block, ins, obj, label):
    def raw(kind, value, object_):
        return (bytes([kind, 0]) + value.to_bytes(4, "little") + object_.to_bytes(4, "little") + bytes(2)).hex()
    difference = (bytes([7, 1]) + (-12).to_bytes(2, "little", signed=True)
                  + label.to_bytes(4, "little") + block.to_bytes(4, "little")).hex()
    operands = " ".join([raw(3, 4, obj), raw(4, label, 0), raw(2, 0xdeadbeef, 0),
                         "000401002300000000000000", difference])
    return ("st.codegen function=f link=f\n"
        f"st.block p={block:08x} index=1 weight=2 flags=0005 succ={block:08x},\n"
        f"st.i p={ins:08x} op=1 flags=deadbeef n=5 line=-1 ops={operands} \n"
        f"sc.node i={ins:08x} latency=2 height=3 latest=4 preds=0 succ={ins:08x}:7,\n"
        f"sc.pick cycle=5 ready={ins:08x}:6:7:8:1:1:9, pick={ins:08x}\n"
        f"sc.after order={ins:08x},\n"
        "fr.local name=deadbeef:value offset=1 size=2 reg=3 used=1\n")


class TraceParity(unittest.TestCase):
    def setUp(self):
        self.left = sample(0x1000, 0x1100, 0x1200, 0x1300)
        self.right = sample(0x8000, 0x8100, 0x8200, 0x8300)

    def test_only_typed_pointer_fields_are_remapped(self):
        self.assertEqual(graph_trace(self.left), graph_trace(self.right))

    def test_complete_output_includes_template_and_unknown_lines_in_order(self):
        extra = ("tp.emit caller=004123ab from=f expanding=g link=h\n"
                 "future.event value=deadbeef\n\n")
        left, right = self.left + extra, self.right + extra
        self.assertEqual(full_trace(left), full_trace(right))
        self.assertEqual(len(full_trace(left)), len(left.splitlines()))
        for old, new in [("caller=004123ab", "caller=004123ac"), ("expanding=g", "expanding=h"),
                         ("value=deadbeef", "value=feedbeef"), ("\n\n", "\n")]:
            self.assertNotEqual(full_trace(left), full_trace(right.replace(old, new)))
        self.assertNotEqual(full_trace(left), full_trace(self.right + "future.event value=deadbeef\n" + extra.splitlines()[0] + "\n\n"))
        self.assertNotEqual(trace_line_endings(left), trace_line_endings(left.replace("\n", "\r\n")))
        self.assertEqual(trace_line_endings("a\r\nb\nc"), ["\r\n", "\n", ""])

    def test_numbers_order_and_names_remain_observable(self):
        for old, new in [("weight=2", "weight=3"), ("flags=deadbeef", "flags=feedbeef"),
                         ("000401002300", "000401002400"), ("0200efbeadde", "0200eebeadde"),
                         ("030004000000008200000000", "03000400000000820000abcd"),
                         ("0701f4ff", "0701f5ff"),
                         (" ops=", " ops= "),
                         ("cycle=5", "cycle=6"), ("latest=4", "latest=5"),
                         ("name=deadbeef:value", "name=feedbeef:value")]:
            self.assertIn(old, self.right)
            with self.subTest(field=old):
                self.assertNotEqual(graph_trace(self.left), graph_trace(self.right.replace(old, new)))
        reordered = self.right.replace("sc.after order=00008100,", "sc.after order=00008300,00008100,")
        self.assertNotEqual(graph_trace(self.left), graph_trace(reordered))

    def test_pointer_relationships_and_null_are_preserved(self):
        changed = self.right.replace("pick=00008100", "pick=00008000")
        self.assertNotEqual(graph_trace(self.left), graph_trace(changed))
        self.assertNotEqual(graph_trace(self.left), graph_trace(self.right.replace("pick=00008100", "pick=00000000")))

    def test_block_statements_pairs_and_function_scopes(self):
        def blocks(a, b, name):
            return (f"bk.stmt line=-1 before=00000000:-1 after={a:08x}:0\n"
                f"bk.stmt line=8 before={a:08x}:0 after={b:08x}:1\n"
                f"bk.list phase=raw link={name}\n"
                f"bk.block p={a:08x} index=0 count=0 flags=8000 succ=1 pred=0\n"
                f"st.codegen function=display link={name}\n"
                f"st.block p={b:08x} index=1 weight=2 flags=0001 succ=\n"
                f"bk.merge prologue=128 link={name}\n"
                f"bk.pair a={a:08x} b={b:08x} counts=0+1 why=empty\n")
        a = blocks(0x1000, 0x2000, "f") + blocks(0x1000, 0x3000, "g")
        b = blocks(0x8000, 0x9000, "f") + blocks(0xa000, 0xb000, "g")
        self.assertEqual(graph_trace(a), graph_trace(b))
        for old, new in [("line=8", "line=9"), ("counts=0+1", "counts=1+1"),
                         ("why=empty", "why=call"), ("flags=8000", "flags=0000"),
                         ("before=00008000:0", "before=00009000:0"),
                         ("prologue=128", "prologue=0")]:
            self.assertNotEqual(graph_trace(a), graph_trace(b.replace(old, new)))

    def test_scalar_cse_and_propagation_are_compared_without_normalization(self):
        trace = ("lc.use node=65530 kind=255 offset=-1 type=255 size=-2147483000 var=value\n"
            "lc.part blocked=1 noregister=-128 bits=-1..31 type=5 size=4 uses=2 var_size=8 var=value\n"
            "ce.object name=temporary caller=004123ab\n"
            "ce.l i=65530 kind=1 op=255 a=-1 b=-1 line=-1 expr=65535 text=value\n"
            "ce.temp name=temporary path=comma\n"
            "ce.vreg name=temporary reg=-32760 class=255 noregister=128\n"
            "pp.copy at=65530 line=-1 tree=(15#65530 value ?)\n")
        self.assertEqual(graph_trace(trace), trace.splitlines())
        for old, new in [("offset=-1", "offset=1"), ("noregister=-128", "noregister=128"),
                         ("caller=004123ab", "caller=004123ac"), ("path=comma", "path=replace"),
                         ("expr=65535", "expr=32767"), ("class=255", "class=-1"),
                         ("tree=(15#65530 value ?)", "tree=(15#65530 value ..)")]:
            with self.subTest(field=old):
                self.assertNotEqual(graph_trace(trace), graph_trace(trace.replace(old, new)))

    def test_inline_decisions_chains_and_order_remain_exact(self):
        trace = ("inl.begin function=ptr_004123ab depth=2 body=1 kind=3 line=-1 link=f\n"
            "inl.can function=f result=128 depth=2 inline_bit=1 always=0 auto=2 limit=100 body=1 nstmts=3 line=-1 link=f chain=outer|inner\n"
            "inl.end function=f expanded=1\n")
        self.assertEqual(graph_trace(trace), trace.splitlines())
        for old, new in [("ptr_004123ab", "ptr_004123ac"), ("result=128", "result=-128"),
                         ("kind=3", "kind=4"), ("line=-1", "line=1"),
                         ("chain=outer|inner", "chain=inner|outer"), ("chain=outer|inner", "chain="),
                         ("expanded=1", "expanded=0")]:
            with self.subTest(field=old):
                self.assertNotEqual(graph_trace(trace), graph_trace(trace.replace(old, new)))
        self.assertNotEqual(graph_trace(trace), graph_trace("\n".join(reversed(trace.splitlines()))))


if __name__ == "__main__":
    unittest.main()
