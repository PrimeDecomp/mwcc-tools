# Source-node observations (experimental)

`source-nodes` inspects immutable reads of selected frontend expression fields,
ordinary assignment emission and actual clone/substitution calls.

This API is implemented in the working tree. Eleven focused host parser tests
pass on Windows and Linux; native producer/runtime validation is pending. Use the
[current status](../oracle-source-guided-status.md) for accepted milestones.

## Capture and query

Build current tools and the Rust producer before using these commands:

```text
mwcc-tools oracle compile-trace SOURCE --source-nodes --version GC_2_7 -- COMPILER_FLAGS > capture.json
mwcc-tools source-nodes inspect capture.json --json
mwcc-tools source-nodes snapshot capture.json SNAPSHOT_ID
mwcc-tools source-nodes write capture.json EMISSION_ID
mwcc-tools source-nodes clone capture.json INVOCATION_ID
```

`--source-nodes` also enables arena capture. Backend tracing is optional.
`inspect` lists parsed observations and IDs. The other commands validate raw
source records and arena lifetimes from the same capture, then return
`{ "value": ..., "observations": ... }`. An unsupported or incomplete query
has indeterminate evidence and may return a null value.

| Query | What it establishes |
|---|---|
| `snapshot` | Selected tagged fields and child observations at one recorded read, with allocation receipts. |
| `write` | An observed assignment's root, LHS, RHS, statement and owning frontend function. |
| `clone` | One actual recursive call's input, input after the call and returned output, with nesting/mode context. |

## Interpret the result

Every read gets a new snapshot ID, even when its address and bytes are unchanged.
A storage lifetime and a snapshot are separate identities. Equal addresses cannot
join observations across a reset or establish that contents stayed unchanged.

The capture reads supported tagged fields, not all bytes of an ENode.
An Object leaf records its Object pointer without its byte offset; it cannot
identify an array element by itself. Unsupported tags or bounded-walk limits
produce opaque observations.

An assignment is stage-local compiler context. It is not automatically a C++
declaration initializer, the first executed write or proof of runtime dominance.
A clone edge records what the compiler called and returned; it does not establish
semantic equivalence or complete original-source ancestry.

Validated checkpoints cover earlier prefixes. Missing records, invalid nesting,
counter mismatches and uncertain allocation lifetimes prevent a complete answer.
Graph fingerprint format 4 does not accept source-node captures yet.

The [source-write audit](../oracle-source-origin-audit.md) describes the remaining
declaration, lowering, scalar-replacement and reconstruction edges.
