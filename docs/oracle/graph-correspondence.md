# Comparing observed allocation graphs: `provenance.compare`

Part of the [oracle documentation](../oracle.md).

`provenance.compare` compares the captured `before_simplify` graphs of two compiles, including nodes absent from
instructions. `trace.remap` ([colouring](colouring.md#traceremap)) pairs nodes by instructions alone.

```sh
mwcc-tools call provenance.compare first=baseline.json second=candidate.json link=RenderParticles__11CElementGenFv \
    class=4 first_round=0 second_round=0
mwcc-tools call provenance.compare first=first.trace second=second.trace link=example__Fv \
    second_link=smaller_example__Fv class=4 version=GC_2_7
```

* Inputs are raw traces or capture envelopes. Two captures of one version give the version; raw traces need
  `version`, and an explicit one must agree with the captures. Versions cannot be mixed.
* `second_link` defaults to `link`, `class` to 4, the rounds to 0. There is no implicit last round; a wrong
  selection is an error.
* Rust: `source_guided::correspondence::compare`, with a `Selection { link, class, round, boundary:
  Boundary::BeforeSimplify }` per side and a `Version`.

## The result (format 1)

* Both graphs' identities, physical-register boundaries and every node: raw neighbour lists, stored and unique
  degree, visibility, flags, colour, allocation category, birth binding.
* Conditional correspondence hypotheses, from aligned instructions and from anchors: complete observed
  named-object/type/word-part, guarded promoted-array element, or Normalize symbol/part/displacement. Graph-only nodes
  take part through anchors.
* Every vote, with repeated or missing anchors, ambiguous birth generations, competing targets and inverse
  collisions; a larger vote does not erase conflicting evidence.
* An injective conditional mapping, the mapped neighbour multiplicity changes, and the boundary edges (touching an
  unmatched node, not called added or removed).
* Each capture's confirmed coalescer parent writes and root chains, apart from the hypotheses between captures.

## Rules

* The selected graphs, the function's graph counts and the direct-counter capture must be complete, and the physical
  boundaries must agree, before any mapping is admitted. Otherwise coverage is indeterminate, with no mapping, and
  the available observations are kept.
* An anchor counts complete births over the whole function (all generations and rounds) and must identify one birth
  and one graph node in each capture. Repeated symbols, shadowed names and raw-number reuse stay explicit; an unknown
  origin gets no identity from its register number.
* Promoted-array anchors carry the source name, element index and size, bounds, flags and datatype. They can pair
  instruction-invisible promotion nodes across variants (`noRota[0]` 601 to 589, `noRota[1]` 602 to 590), not a
  promotion node with a live frontend scalar.
* Names and instruction alignment are conditional correspondence, not proof of the same meaning, of elimination, or
  that an invisible node stopped affecting the allocator: `lifetime_correspondence` and `elimination` stay
  indeterminate. Merges come only from a capture's confirmed writes.

Source search, reduction, corpus queries and objective evaluation score their graph changes on this mapping's added
and removed edges. On the clean `RenderParticles` capture against the named-pointer variant (555 and 554 GPR nodes,
292 pairs), the `noRota` anchor maps 55 to 55 and shows its stored degree going from 159 to 156.
