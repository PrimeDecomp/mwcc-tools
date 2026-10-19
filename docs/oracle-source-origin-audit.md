# Source origins: what is observed and what is open

Connecting a C++ write or initializer to a backend value needs edges through the frontend, the inliner and the IRO
optimizer. The [source-node methods](oracle/source-nodes.md) observe selected statement snapshots and clone calls;
the rest of the bridge is open. This page records what the retail code establishes, so that a result that stays
indeterminate can be understood. Addresses are retail virtual addresses; the guards are in the source-node catalog
(`source_node_guards.json`, `mwcc-tools provenance derive-guards`).

## Roles

* A **source write** is an observed assignment node with explicit LHS and RHS edges. A later assignment to a
  declared array is a source write even when it is the programmer's initial setup.
* A **declaration initializer** comes only from `CInit_InitializeAutoData`'s target Object (its only direct caller is
  `declare_local_object+4cb`, with `append_or_defer_expression_statement` as its callback) and the callback's exact
  input root. Explicit syntax, missing initializers and implicit class construction are separate states. An
  assignment-shaped ENode (`EASS`), a name or a line never establishes the role.
* With deferral on, the callback can append the **previous** deferred root before deferring the current one, so the
  active initializer does not own every statement appended during it.
* Source and inline histories can come before the backend's `pv.function.begin`. A frontend owner comes from
  `CFunc_ParseFuncDef`'s explicit function and the scope setters (`CScope_SetFunctionScope`,
  `CScope_SetMethodScope`, `CScope_RestoreScope`); it never inherits the active backend function. During
  `generate_inline_statements` the explicit function is the callee and the containing function the destination.

## What existing records cannot prove

* A birth's raw `statement` and `line` are copied from `gCurrentStatement` and can be stale outside code generation.
  Optimizer and allocation births outside a dispatch scope get `expression=0`.
* `codegen_dispatch` scopes are backend execution context (always `complete=0`), not source identity.
* `ce.l` has no linear pointer, and its indices are reused between snapshots: it cannot be joined to a scalar
  binding by index, line or spelling. A name missing from bounded expression text does not mean the object is.
* `lc.arg` keeps names and bounded text, not Object or ENode identities: a formal named `flag` cannot be joined to a
  caller's array by name.
* The origins table tracks only call-reference ENodes and callee chains, with fixed capacity and silent depth
  truncation.
* An inline helper can take the statement-expansion route and leave no `inl.begin`/`inl.end` records.

## Frontend: statements and inline clones

* `parse_statement` stores an ordinary expression statement's root at `+238e` (`mov [ebx+a],eax`; EBX is the
  Statement), before the unused-expression check. The source-node probe observes there. It covers ordinary
  expression statements only, not for-initializers and steps, conditions, return expressions or generated cleanup.
* The recursive clone/substitute `fn_00513240` (`0x692` bytes, the same normalized body in every version) allocates a
  30-byte node and copies it; its input can change during alias normalization, and its result can be a copied actual
  argument or a new conversion rather than the first shell. Input before, input after and output are separate
  observations. A 30-byte allocation alone is not an ENode (Statements are 30 bytes too).
* Its tag switch rejects tags above `0x4b`; tag 53 has three children and tag 67 four. The five switch tables (and the
  mode wrapper's and `CInit_InitializeAutoData`'s) are pinned separately, since a code hash does not cover a table.
* `CInlineInfo` is `0xa2` bytes in GC 1.3 and `0xa6` in GC 2.7; fields after the back-end options shift.

## The IRO optimizer

* In GC 1.3.2 and 2.7, the split symbols `convert_group` and `build_group` are containers of separate private
  functions with their own ABIs and returns (reconstruction at `convert_group+0x870`; visit, linearize and lower at
  `build_group+0x750`, `+0x8A0`, `+0xE00`). Observing a container's entry would miss them. GC 1.3 has a split symbol
  for each.
* Constructor-only observation misses nodes: linearization calls `oalloc(0x38)` and reconstruction `lalloc(0x1E)`
  directly. Lowering mutates in place and returns void.
* `record_var_part_use` records accesses: the first matching access becomes `part.source` and later ones are
  prepended to `part.uses`. That is access order, not definition or initialization order. `ClearZeroOperands` sets a
  subtree's type to zero and its expression to null, and a parameter aggregate gets a generated entry assignment
  after the member loop.
* After reconstruction, `DumpIR_OptimizeStatements` optimizes the statements again before backend dispatch.
* `IRO_Optimizer` (`(Object*, Statement*) → Statement*`) is in the retail split `backend/CodeGen.c.obj`.

| Structure | Size | Fields |
|---|---|---|
| `IROLinear` | `0x38` | type `+0`, nodetype `+1`, flags `+2`, index `+8`, Statement `+0xA`, Type `+0xE`, payload `+0x20`, next `+0x34` |
| `ENode` | `0x1E` | type `+0`, flags `+2`, Type `+6`, payload `+0xE` |
| `IROVarPart` | `0x2A` | as in [the backend page](oracle-backend-provenance-audit.md#frontend-aggregate-scalarization), next `+0x1E`, uses `+0x22` |
| `IROElmList` | `8` | element `+0`, next `+4` |

## Open

The edges not observed yet: each clone invocation with its parent, mode and formal/actual slots; lowering;
linearization (`linearize_expression`, ENode → IROLinear); scalar use replacement (the use list before replacement,
then the stores at the scalar pass's `+0x1C0` and `+0x214`); reconstruction (`fn_0042eb40`, IROLinear → ENode); and the
join to the backend function. An unobserved rewrite ends a path with a gap: names, lines, CSE indices or equal bytes
do not repair it. A complete path would still not prove dominance, execution, first-write status or lifetime
equivalence.
