# Call argument provenance

Development producers expose `Report.call_arguments` through `provenance.inspect`
(`trace_path=CAPTURE link=NAME`), in its `document`. This is separate from call-result provenance: a
void call can still normalize an address, create registers and emit argument
moves. The native `call_arguments::parse(trace, &value_history)` API validates
the observed argument stream against the same trace's register births.

The scope is `guarded_regular_call_arguments_v1`. An argument's `ordinal` is
zero-based **lowered argument-context order**, declared by
`ordinal_kind=lowered_argument_context_order`. Hidden parameters and compiler
adjustments mean this is not automatically a C++ parameter index.

## Observations and completeness

Schema-1 records describe:

| Records | Meaning |
| --- | --- |
| `pv.call.begin/end` | Actual regular-call generation, parent call/expression, raw call/output identities, return type, cached qualified callee name, independent event totals |
| `pv.argument.bind` | Exact lowered context and source-list/node association, type, flags, evaluated bit, planned GPR/second-GPR/FPR/vector slots and stack offset |
| `pv.argument.begin/end` | Balanced register, stack, dispatch or guarded scalar-GPR-force episode, parent episode, birth cutoffs and PCode endpoints |
| `pv.argument.operand` | Tagged operand at dispatch return, before/after guarded GPR forcing, or helper return |
| `pv.argument.birth` | Exact backend birth ID/class/register owned by the most specific active argument episode |
| `pv.argument.instruction` | Bounded newly linked PCode interval observed when the episode ends |
| `pv.argument.gap` | Unsupported or incomplete observation |

Function begin/end records declare `call_arguments`. End records independently
report `call_count`, `argument_count`, `argument_episodes`, `argument_births`,
`argument_operands` and `argument_instructions`. Missing entire record families
cannot become known zero counts merely because the remaining scopes balance.

`FunctionSummary.event_totals` separately reconciles the emitted event counts.
It does not assert complete observation of every call. A query for one completed
call can remain known when an unrelated call has a PCode interval gap; overall
function coverage still remains indeterminate. Unknown earlier callee identities
continue to make an occurrence selector ambiguous.

Only tag-defined operand fields are read. Unevaluated zeroed context storage is
not a GPR0 result. Planned argument registers and helper-return operands are
observations at those stages; they do not establish the final ABI location. A
helper can emit a move without rewriting its operand to the destination register.
PCode intervals include nested work and describe what was newly linked within a
bounded same-block interval, not unique instruction ownership or value lifetime.
Block changes, unsupported operand/location forms, missing events and capacity
limits leave coverage indeterminate.

Callee names are read from an existing cached compiler link name. Observing a
call does not invoke the mangler to fill an absent cache. Unknown names remain
unknown even when raw callee Object/ENode identities are available. These
capture-local pointers do not establish original-source or inline ancestry.

## Query an argument's address-normalization behavior

The shared search/reduction/index objective accepts:

```json
{
  "kind": "call_argument_address_pairs",
  "callee": "SetTevOp__9CGraphicsF12ERglTevStageRCQ213CTevCombiners8CTevPass",
  "call": 0,
  "ordinal": 1,
  "symbol": "kEnvModulate__9CGraphics",
  "minimum": 0,
  "maximum": 0
}
```

Use the exact qualified callee link observed in the capture; the spelling above
is observed in the retained GC 2.7 particle/graphics examples.
`call` selects a zero-based occurrence of that callee in the selected function's
observed call-begin order. An unknown earlier callee makes that occurrence
ambiguous. A missing call or argument is indeterminate, not a zero count.

Select class 4 (GPR) and require the needed symbolic relocations in the containing
contract. The count includes the selected argument's observed episode subtree,
including arguments of nested calls evaluated within it; moving a pair into a
wrapper does not establish zero pairs. Both high and low births must belong to this subtree, form a
consistent adjacent Normalize pair and have complete symbol identity. The result
retains the callee/context, episode IDs, pair births, graph nodes, allocation roots
and confirmed physical merges. It counts this guarded creation mechanism; it
does not assert the absence of every possible argument temporary.

The shared evaluator still requires the selected allocation graph. A tiny
reference-only function that allocates no virtual GPRs can have complete call
observations and zero recorded Normalize births while this graph-bound objective
remains indeterminate because no GPR graph round exists. Inspect the typed call
history for that observation; the evaluator does not invent an empty graph.

Relevant-state fingerprint format 4 includes the argument records, independent
totals, ownership, stages, tagged operands and PCode intervals. Pointer equality
relationships to dispatches, materialization operands, births and selected graph
instructions are retained while address spellings are normalized. Unknown record
fields or unsupported representations disable grouping.

The [source-origin audit](../oracle-source-origin-audit.md) describes the separate
work needed to connect these backend observations to original source writes and
inline leaves. These call observations do not close that gap.

See [the V6 producer contract](producer.md) for the exact guarded helpers, native
hook inventory and reproducible developer fixture audit.
