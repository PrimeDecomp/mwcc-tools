# Source-node observations (experimental)

The `source_nodes.*` methods inspect reads of selected frontend expression fields, assignment emission and actual
clone/substitution calls.

```sh
mwcc-tools call oracle.trace source=SOURCE version=GC_2_7 flags='["COMPILER_FLAGS"]' streams.source_nodes=true \
    capture_path=capture.json
mwcc-tools call source_nodes.inspect capture=capture.json
mwcc-tools call source_nodes.snapshot capture=capture.json id=SNAPSHOT_ID
mwcc-tools call source_nodes.write capture=capture.json id=EMISSION_ID
mwcc-tools call source_nodes.clone capture=capture.json id=INVOCATION_ID
```

`streams.source_nodes` also enables [arena capture](source-arenas.md); backend tracing is optional. `inspect` lists
the observations and their IDs. The other methods validate the raw source records and arena lifetimes, and return
`{ "value": ..., "observations": ... }` as their `document`; an unsupported or incomplete query has indeterminate
evidence and may have a null value.

| Query | What it establishes |
|---|---|
| `snapshot` | Selected tagged fields and child observations at one recorded read, with allocation receipts. |
| `write` | An observed assignment's root, LHS, RHS, statement and owning frontend function. |
| `clone` | One recursive call's input, input after the call and returned output, with nesting/mode context. |

## Interpreting

- Every read gets a new snapshot ID, even with unchanged address and bytes. Equal addresses cannot join observations
  across a reset or show that contents stayed unchanged.
- Only supported tagged fields are read, not all of an ENode. An Object leaf records its Object pointer without its
  byte offset, so it does not identify an array element. Unsupported tags and walk limits give opaque observations.
- An assignment is stage-local compiler context: not a C++ declaration initializer, the first executed write or
  proof of dominance. A clone edge records what the compiler called and returned, not semantic equivalence or
  complete ancestry.
- Missing records, invalid nesting, counter mismatches and uncertain lifetimes prevent a complete answer.
  Fingerprint format 4 does not accept source-node captures.

The remaining declaration, lowering, scalar-replacement and reconstruction edges are described in the
[source-origin audit](../oracle-source-origin-audit.md).
