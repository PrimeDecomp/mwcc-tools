# Value numbering: `oracle.trace`'s `views.value_numbering` and `views.why_not_cse`

```sh
mwcc-tools call oracle.trace project=PROJECT unit=UNIT function=NAME views.why_not_cse=12,15
mwcc-tools call oracle.trace project=PROJECT unit=UNIT function=NAME views.value_numbering=true views.alias=true
```

The backend's value numbering (`removecommonsubexpressions`, ValueNumbering.c) removes a load or an operation that
repeats an available value of its extended basic block: it runs before the allocation, at both optimization levels
it has, and once more after it. A literal or a field loaded twice through one register, the second load surviving,
is its decision. These views come from probes of gc2's sources, so GC 2.6 and 2.7 in process only (the module
`build/<version>/wasm/mwcc.wasm`; the document's `module` names the one that ran).

`backend.value_numbering` lists each pass (`function`, `aggressive`: the level at which compares and the `li` / `lis`
of temporaries are candidates too, `removed`) and its `decisions`, one for each load and candidate operation:

| Field | |
|---|---|
| `instruction`, `line`, `flags` | The instruction (virtual registers before the allocation), its source line and its PCode flags. |
| `block`, `extended_block` | Its block, and the first block of its extended basic block: only that block's values are available to it. |
| `outcome` | `reused` (an available value's, `other`), `forwarded` (the value the store `other` wrote), `kept` (a new available value) or `skipped` (volatile, or it writes more than one register). |
| `reason` | Why it was kept: no value of its opcode is available; those that are have other operands (another base register's value, another offset or object); one has its operands but the memory's value changed since (`killed_by`: the store or call that changed it last); one has its operands but other flags (`fIsConst` on one of them: a const-qualified constant against a plain one, for instance); one has its operands but their registers hold other values now. |
| `other`, `other_line` | The value it reused or was forwarded from, or the nearest available value it did not match. |
| `alias` | Its alias's index in `backend.alias`'s table of the same function. |

`views.why_not_cse: "A,B"` answers, for each pass, what became of line B's loads and operations against line A's:
`reused line A's value`, or the reason it was kept, what killed the memory's value, and whether line A is in
another extended basic block. A line with no load or candidate operation in a pass says so: its instruction is a
store, a call, or an operation the pass does not number (an `li` of a real register, a record bit, a carry).

`views.alias` gives each table `gather_alias_info` left (the scheduler and the global optimizer each gather one):
the aliases (an object, a part of one with its offset and size, or a set and its members) and each access's alias.
Two accesses may alias when their aliases overlap, which is what orders a load after a store in the scheduler and
what kills an available load in the value numbering. `streams.alias_queries` adds every query and its answer
([trace.md](trace.md)).

`views.stages: "value numbering,copy propagation"` gives the PCode after each stage of the debug listing whose name
has one of these (`backend.stages`: `stage`, `function`, `lines`; `listing` for all of them): before and after each
value numbering pass, copy propagation, the allocation and the scheduler. A pass that changed nothing is not listed.

`views.flags`, with `views.pcode`, adds each instruction's flags to the PCode before the allocation
(`lfs %f33, r0, sym  ; fIsRead fIsConst`): the value numbering only matches two instructions whose flags are the same.
