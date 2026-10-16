# Comparing observed allocation graphs

`provenance compare` compares the actual captured `before_simplify` graphs,
including nodes absent from instructions. Select each function, register class
and allocation round explicitly:

```text
mwcc-tools provenance compare baseline.json candidate.json --link RenderParticles__11CElementGenFv --class 4 --first-round 0 --second-round 0 --json
mwcc-tools provenance compare first.trace second.trace --link example__Fv --second-link smaller_example__Fv --class 4 --version GC_2_7 --json
```

Inputs accept raw traces or compiler-capture envelopes. Two JSON captures with
the same compiler version can supply the version automatically. Raw traces or
missing version metadata require `--version`; an explicit version must agree
with any available capture metadata. Cross-version comparisons are not supported.
`--second-link` defaults to `--link`; class defaults to 4 and rounds to 0. There
is no implicit selection of the last round. Wrong selections are errors.

The Rust API is `source_guided::correspondence::compare`, with independent
`Selection { link, class, round, boundary: Boundary::BeforeSimplify }` arguments
and a compiler `Version`. Result format 1 retains:

- Both selected graph identities, actual physical-register boundaries and every
  node, including ordered raw neighbor lists, stored degree, unique adjacency
  degree, visibility, flags, color, allocation category and birth binding.
- Conditional correspondence hypotheses from aligned instructions and complete
  observed named-object/type/word-part or Normalize-symbol/part/displacement
  anchors. Graph-only nodes participate through provenance anchors.
- Repeated or missing anchors, ambiguous birth generations, nodes contributed to
  several graph nodes, competing targets and inverse collisions. Every vote is
  retained; a larger vote count does not erase conflicting evidence.
- An injective conditional mapping, mapped neighbor multiplicity changes and
  unclassified boundary edges. A boundary edge touches an unmatched node and is
  not called an added or removed edge. Differences are directed neighbor records;
  an undirected graph can record both directions.
- Each capture's confirmed coalescer parent writes and value-specific root
  chains, separately from hypotheses between captures.

Selected graph observations, whole-function graph counts and direct-counter
capture must be complete before any mapping is admitted. Physical boundaries
must agree. Incomplete captures produce an indeterminate coverage result with
no accepted mapping while preserving the observations that are available.
Instruction hypotheses use the selected class's actual physical boundary rather
than treating every register below 32 as physical.

Named-object anchors count matching complete observed births over the **whole
function**, including later generations and rounds. Normalize anchors include
symbol bytes, high/low part, displacement, Operand kind, datatype and storage;
their function-wide guarded capture must be complete. An anchor must identify
one birth and one graph node in each capture, with complete node binding, to
contribute a hypothesis. Repeated symbols, shadowed names and raw-number reuse
remain explicit. Unknown origins do not acquire a source identity from their
register number.

Source names and instruction alignment are conditional correspondence, not
semantic lifetime proof. These results do not prove that two expressions have
the same meaning, that an unmatched value was eliminated, or that an invisible
node stopped affecting the allocator. `lifetime_correspondence` and `elimination`
therefore remain indeterminate. A many-to-one instruction collision is not
reported as an observed merge; actual merges come only from confirmed writes in
the individual capture. Missing or ambiguous merge bindings remain visible in
the recorded root-chain evidence.

This API complements the historical instruction-only `remap` projection without
changing its report format. Search, reduction, corpus queries and retained objective evaluation use this
comparison as their shared graph-change evidence. Their scored added/removed
edges use only this conditional injective mapping; raw multiplicity and boundary
records and the full structured comparison remain available. Incomplete
observed-boundary coverage is a hard gate, while semantic correspondence and
elimination stay indeterminate.

The real clean `RenderParticles` capture and the verified named-pointer source
variant were compared independently on Windows/Linux; their complete structured
results agree. The selected GPR graphs contain 555 and 554 nodes, with 292
conditional injective pairs. The graph-only capable named-object anchor for
`noRota` maps register 55 to 55 and exposes the observed stored-degree change
159 to 156. The changed temporary neighbors are outside the conditional mapped
subgraph, so they remain unclassified boundary records rather than claimed
removed edges. General elimination remains indeterminate. Local proof artifacts
are under `build/source-guided/graph-correspondence-audit/`.
