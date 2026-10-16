# Frontend aggregate-to-scalar provenance

The development Rust producer records the actual temporary returned when the
frontend scalarizer replaces an aggregate part. Enable `--backend` and inspect
the retained capture with the native API:

```text
mwcc-tools oracle compile-trace candidate.cpp --backend --version GC_2_7 -- COMPILER_FLAGS
mwcc-tools provenance inspect capture.json --link RenderParticles__11CElementGenFv --json
```

This requires a current producer and analyzer. The pinned migration release does
not contain these extensions. GC 1.3, 1.3.2 and 2.7 have separately guarded retail
sites. The internal counter callback is V5; old common hook objects must be rebuilt
in a new producer directory. See the [machine-code audit](../oracle-backend-provenance-audit.md#frontend-aggregate-scalarization).

## What is captured

`pv.scalar.begin`, `binding`, `gap` and `end` use schema 1 and coverage scope
`guarded_temp_returns_v1`. Each binding records the original aggregate Object,
VarInfo, type and byte-preserving source name; its selected byte and bit interval;
the actual returned temporary Object, VarInfo and type; and the guarded creation
site. Array bindings also record the original element type when known. Raw
addresses identify objects within a capture; they are not source identities across
compilations.

The observed order is function begin, frontend scalarization passes, backend
births, then function end. Function completion supplies independent pass/binding
totals and first/last pass IDs. A begin record advertises capability; it does not
claim completed capture. Missing passes, summaries, unsupported schemas, gaps,
overflow or uncertain function context remain indeterminate. In particular, an
unobserved scalarizer pass does not establish a complete zero-pass result.

The Rust module is `mwcc_oracle_core::scalarization`. Typed inspection exposes
`Report.scalarization.passes` and derived `backend_values`, with an independent
evaluation for each join. `object_origin` requires the same function ID, exact
function Object and link, complete matching totals, and all relevant scalar passes
ending before the first backend birth. It joins the returned Object and VarInfo
to one backend birth. Duplicate creation identities, reused backend generations
and conflicting context do not acquire a unique binding from their names.

`Binding::array_element` recognizes only a complete, aligned whole element with
matching source and temporary types and byte/bit bounds. Partial fields, blocked
parts, bitfields and unknown element metadata do not establish an array element.
This association proves an observed creation relationship. It does not identify
the declaration, original leaf expression, inline ancestry, or full lifetime.

## Real promoted-flag example

In the full outside-loop-store `RenderParticles` experiment, V5 observes two
scalarizer passes and 79 bindings. The `noRota` interval at bytes 4–7, bits 32–63
creates one scalar temporary. That exact Object and VarInfo join to birth
`54:209`, GPR 162. The older `COpt_ArrayToRegister` nodes 589 and 590 remain
degree-zero and absent from instructions. Selecting those old nodes cannot prove
anything about initialization of the live scalar.

The stock object is unchanged from the V4 experiment:
`943ba6ad056e1c6aecc71c49df3b3f5d9733f5196804aa632f8c511b548d6e14`.
The current Windows typed proof covers this full function at GC 2.7 and nine small
cases across all three supported versions. Six small cases establish the live
array-element-to-scalar join; the three original-promotion controls do not invent
one. Raw and typed evidence is retained under
`build/source-guided/scalar-origin-audit/`.

The live flag computation is not a constant `LI`. The narrow
`scalarized_array_initialization` predicate therefore remains indeterminate for
these real cases. It selects an exact whole array element using `name`,
`element_index` and `element_size`, then applies the supported constant-initializer
recognizer with `constant` and optional `source_line`. Missing copies or unused
promotion nodes are not accepted as evidence of successful initialization.

`scalarized_array_definition` handles observed computations beyond constant LI.
It selects a definition ordinal of the exactly bound backend register in the
captured preallocation stream, then checks its producer, any actual copy merge,
graph visibility and final assigned destination. Ordinals refer to compiler
records, not C++ source or execution order. This separate scope permits useful
mechanism queries without claiming that an arbitrary definition is a source
initializer. See the [objective contract](source-search.md#objectives-and-validity).

In the full GC 2.7 flag example, observed definition zero is the copy from GPR 172
to 162. The actual coalescer merges it, and the non-LI opcode-103 producer defines
the recorded final register, GPR 27. This is the useful observed-definition
relationship; automatic source-initializer recognition remains open.

The [promoted-flag case](../oracle-source-guided-case-promoted-flags.md) records
the source edit, stock checks and worse final matching score. No edit was applied
to the game.

## State grouping

Relevant-state fingerprints use format 4 (format 3 introduced scalarization). They retain the whole function's
scalarization evidence, including creations without a backend graph node,
independent totals, coverage, relative event order and pointer equality
relationships. Raw pointer values, pass IDs and absolute trace positions are
normalized. A second possible source creation must change the state even when
the selected allocation graph is unchanged: it can make the source selector
ambiguous. Unsupported normalization yields an explicit error and no grouping
fingerprint. Saved runs bind their analyzer executable; do not combine their
fingerprints with results from a different analyzer.

The complete capture also supplies preallocation definitions, CFG and coalescer
decisions. These are relevant even when an extra copy disappears before the graph:
it changes an observed-definition ordinal. The fingerprint API therefore requires
the complete `Compilation` alongside the graph and provenance, and checks the
exact report/trace digest binding.
