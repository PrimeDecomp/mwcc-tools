# Call argument provenance

`provenance.inspect` (`trace_path=CAPTURE link=NAME`) gives `Report.call_arguments` in its `document`; Rust:
`call_arguments::parse(trace, &value_history)`, which validates the argument stream against the same trace's
births. It is separate from call-result provenance: a void call can still normalize an address, create registers
and emit argument moves. With GC 2.6/2.7 in process the call-argument context stream is absent or partial
([wasm.md](../wasm.md#probes)); use `MWCC_IN_PROCESS=0`.

The scope is `guarded_regular_call_arguments_v1`. An argument's `ordinal` is the zero-based **lowered
argument-context order** (`ordinal_kind=lowered_argument_context_order`): hidden parameters and compiler adjustments
mean it is not automatically a C++ parameter index.

## Records

| Records | Meaning |
| --- | --- |
| `pv.call.begin/end` | Regular-call generation, parent call/expression, raw call/output identities, return type, cached qualified callee name, event totals |
| `pv.argument.bind` | Lowered context and source-list node, type, flags, evaluated bit, planned GPR/second-GPR/FPR/vector slots and stack offset |
| `pv.argument.begin/end` | A balanced register, stack, dispatch or scalar-GPR-force episode, its parent, birth cutoffs and PCode endpoints |
| `pv.argument.operand` | Tagged operand at dispatch return, before/after GPR forcing, or helper return |
| `pv.argument.birth` | A backend birth owned by the innermost active episode |
| `pv.argument.instruction` | The bounded PCode interval newly linked when the episode ends |
| `pv.argument.gap` | Unsupported or incomplete observation |

Function end records report `call_count`, `argument_count`, `argument_episodes`, `argument_births`,
`argument_operands` and `argument_instructions`; a missing record family is not a known zero.
`FunctionSummary.event_totals` reconciles the emitted counts. A query for one completed call can stay known when an
unrelated call has a gap; function coverage is then indeterminate.

## Interpreting

- Only tag-defined operand fields are read; unevaluated zeroed storage is not a GPR0 result.
- Planned registers and helper-return operands are observations at those stages, not the final ABI location. A
  helper can emit a move without rewriting its operand.
- PCode intervals include nested work within one block; they are not unique instruction ownership. Block changes,
  unsupported forms, missing events and capacity limits leave coverage indeterminate.
- Callee names come from the compiler's cached link name; an absent cache stays unknown. Raw Object/ENode pointers
  do not establish source or inline ancestry.

## The `call_argument_address_pairs` objective

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

- `callee` is the exact qualified link observed in the capture; `call` is the zero-based occurrence of it in the
  function's call-begin order. An unknown earlier callee makes the occurrence ambiguous; a missing call or argument
  is indeterminate.
- Select class 4 (GPR) and require the symbol's relocations in the contract
  ([source search](source-search.md#objectives-and-validity)).
- The count covers the argument's episode subtree, including nested calls' arguments, so moving a pair into a
  wrapper does not give zero. Both births must form an adjacent Normalize pair with complete symbol identity.
- The evaluator needs the selected allocation graph: a function that allocates no virtual GPRs stays
  indeterminate even with complete call observations.

Fingerprint format 4 includes the argument records, totals, ownership, stages, operands and PCode intervals, with
pointer equality relationships kept and addresses normalized.

The guarded helpers and the fixture audit are in [producer.md](producer.md). Connecting these observations to source
writes and inline leaves is open ([source-origin audit](../oracle-source-origin-audit.md)).
