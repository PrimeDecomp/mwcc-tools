# Frontend aggregate-to-scalar provenance

The Rust producer records the temporary the frontend scalarizer returns when it replaces an aggregate part. It is
part of `streams.backend`:

```sh
mwcc-tools call oracle.trace source=candidate.cpp version=GC_2_7 flags='["COMPILER_FLAGS"]' streams.backend=true \
    capture_path=capture.json
mwcc-tools call provenance.inspect trace_path=capture.json link=RenderParticles__11CElementGenFv
```

The guarded retail sites are per version; the machine-code audit is in
[oracle-backend-provenance-audit.md](../oracle-backend-provenance-audit.md#frontend-aggregate-scalarization).

## What is captured

`pv.scalar.begin`, `binding`, `gap` and `end` (schema 1, scope `guarded_temp_returns_v1`). Each binding records:

- the original aggregate Object, VarInfo, type and byte-preserving name, and the selected byte and bit interval;
- the returned temporary's Object, VarInfo and type, and the guarded creation site;
- for arrays, the element type when known.

Raw addresses identify objects within one capture only. The order is function begin, scalarizer passes, backend
births, function end; the end gives independent pass/binding totals and first/last pass IDs. Missing passes,
summaries, gaps, overflow or uncertain function context are indeterminate; an unobserved pass is not a complete
zero.

Rust: `mwcc_oracle_core::scalarization`. The report has `Report.scalarization.passes` and the derived
`backend_values`, each join with its own evaluation. `object_origin` needs the same function ID, Object and link,
matching totals, and every relevant pass ending before the first backend birth; it joins the returned Object and
VarInfo to one birth. Duplicate creation identities, reused generations and conflicting context stay unbound.

`Binding::array_element` recognizes only a complete, aligned whole element with matching source and temporary
types and bounds. Partial fields, bitfields and unknown element metadata do not qualify. The join proves a creation
relationship, not the declaration, leaf expression, inline ancestry or lifetime.

## Objectives that use it

- `scalarized_array_initialization`: an exact whole element (`name`, `element_index`, `element_size`) and the
  constant-initializer recognizer (`constant`, optional `source_line`). A non-constant computation is
  indeterminate.
- `scalarized_array_definition`: a definition ordinal of the bound register in the preallocation stream, checked
  through its producer, any copy merge, graph visibility and final destination. Ordinals are compiler record order.

See [source search](source-search.md#objectives-and-validity). Example: in GC 2.7 `RenderParticles` the `noRota`
flag (bytes 4–7) joins to one scalar birth; its definition zero is a copy that the coalescer merges, and the non-LI
producer defines the final GPR 27. The older `COpt_ArrayToRegister` nodes stay degree-zero and prove nothing about
the live scalar. The source edit and its score are in the
[promoted-flag case](../oracle-source-guided-case-promoted-flags.md).

## State grouping

Fingerprint format 4 keeps the function's scalarization evidence (including creations without a graph node),
totals, coverage, relative event order and pointer equality relationships; raw pointers, pass IDs and trace
positions are normalized. A second possible source creation changes the state even when the graph is unchanged.
Preallocation definitions, CFG and coalescer decisions are included too (an extra copy changes a definition
ordinal), so the fingerprint takes the complete `Compilation` with the graph and report and checks their digest
binding. Unsupported normalization is an error with no fingerprint.
