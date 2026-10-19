# Source discovery: specialize a caller-proven helper branch

This follows the [three bounded V6 source experiments](oracle-source-discovery-envmodulate.md).
Those candidates reduced three EnvModulate Normalize pairs to two, but did not
reach the requested maximum of one. This separate run starts with the factored
source and tests one further exact rewrite. Earlier results and analysis identities
remain unchanged.

## Source justification and guarded observation

In the factored source, the caller uses:

```cpp
if (scale == CTevCombiners::kTS_Scale1) {
  CGraphics::SetTevOp(kTS_Stage0, CGraphics::EnvModulate());
} else {
  SetParticleTevScale(scale);
}
```

The exact pinned helper body is:

```cpp
static inline void SetParticleTevScale(CTevCombiners::ETevScale scale) {
  if (scale == CTevCombiners::kTS_Scale1) {
    CGraphics::SetTevOp(kTS_Stage0, CGraphics::kEnvModulate);
  } else {
    sParticleModulatePass.SetColorScale(scale);
    CGraphics::SetTevOp(kTS_Stage0, sParticleModulatePass);
  }
}
```

The caller's else proves that its non-volatile enum value differs from scale1.
The by-value argument does not change that value. The candidate substitutes only
the helper's else body, through the same public operations in the same order:

```cpp
} else {
  sParticleModulatePass.SetColorScale(scale);
  CGraphics::SetTevOp(kTS_Stage0, sParticleModulatePass);
}
```

The unchanged header defines `SetColorScale(scale)` as
`mColorOp.SetScale(scale)`, and `SetScale(scale)` as `mScale = scale`. There are no
other helper-else operations or conditional writes. The class header declares
neither a shadowing `SetParticleTevScale` nor `sParticleModulatePass` member. No
global/type/ABI declaration, header or noRota expression changes. No extra object
or reference storage is created, and no GetValue, public setter or SetTevOp side
effect moves across another operation.

Native argument-objective evaluations on both the clean and factored captures
show that lowered SetTevOp occurrence1, context ordinal1 owns exactly one
EnvModulate Normalize pair. Both evaluations are satisfied with independent event
totals and required HA/LO relocations. Their observed call ID511 and low birth
54:361 are capture-local evidence. This observation supports testing the remaining
path; it is not an automatic original-source/inline ancestry binding. The finite
source rewrite then isolates the source helper guard rather than guessing a
source role from those numeric IDs.

The original helper remains unchanged for its other users. Historical experiments
using direct private-member writes or modified headers are not the recipe here.
The checker requires exact baseline/candidate bytes, the full helper body and
the original public setter bodies under pinned header hashes. This is a bounded
source justification, not unrestricted semantic equivalence or gameplay testing.

## Contract and evidence

Baseline source SHA-256:
`83dc98de2f60b9b497d107b3e1a550ece5e6aadb7620605ef5374976d3345d83`.
Candidate source SHA-256:
`befd794b75d121959be981c03010d3300151479ec70b07fb1f930e114d6a62af`.

The fresh run uses the same private 127-header tree, sealed V6 GC2.7 producer and
frozen analyzer SHA-256
`012fd34b713cd8f16ed55558e8efc0c727b24df09df3313b149ba09e1d23f9ff`.
Its `source_transformation` contract asks for at most one guarded EnvModulate pair
and retains at least one HA(kind6) and LO(kind4) relocation with zero addend.
The optional target diagnostic compares against the retained **clean-source
baseline object**, not the retail object. That diagnostic must not be reported
as a retail match.

Evidence is local under
`build/source-guided/source-origin-bridge/source-discovery/follow-up/`:
the exact source pair, `closed-proof.json`, `check_closed.py`, native plan and
`search/` input/capture/checker/calibration/verification artifacts. Initial
remaining-argument evaluations are under the parent `remaining-pair/` directory.
No game checkout, sealed producer or earlier experiment is written.

## Result: independently verified pair-count objective

The GC2.7 run completed as `verified_success` using four stock-verified captures:
original-context and private-input calibration, the candidate, and an independent
fresh candidate compile. The independent compile passed the pair-count objective,
all nine hard gates and the exact closed-source checker. Its complete object
bytes equal the first candidate's bytes. The unchanged baseline failed only the
requested maximum-one objective; its other gates passed.

| Source | Global EnvModulate pairs | Graph nodes | Named noRota degree | Env high/low neighbors of noRota | Lowered SetTevOp calls | RenderParticles bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Clean source, earlier run | 3 | 555 | 159 | 6 | 5 | 6668 |
| Factored immediate baseline | 2 | 553 | 157 | 4 | 4 | 6648 |
| Caller-proven helper specialization | 1 | 551 | 155 | 2 | 3 | 6624 |

The one remaining pair's result is still merged into physical r4. The uniquely
named noRota graph node remains visible and before the coalescing window; this
rewrite does not demonstrate direct initialization in its final register, a
changed allocation category or repaired initialization coalescing. These are
observed graph and guarded pair-count changes, not an inferred correspondence
proving elimination of a semantic lifetime. The configured maximum-one objective
requires neither zero pairs nor disappearance of every address-related temporary.

A separate native retained-capture check finds exactly one owned pair at lowered
SetTevOp occurrence0, ordinal1 in both the factored baseline and candidate. Thus
this first argument's count stays one while the global count falls two to one.
Its exact caller-context ownership predicate and required relocations pass;
`argument-evaluation/` retains both native evaluations and commands. The earlier
occurrence1 observation motivated this recipe, but call occurrence numbers across
these different lowered programs do not establish source-call identity.

The source family is useful: factoring the duplicate outer direct call, then
specializing the helper branch already excluded by the caller, removes two
observed guarded Normalize pairs from the clean-source graph while retaining
symbolic address relocations. The private renderer source, public header bytes,
noRota code and external ABI remain as described above. The proof is specifically
for this finite source pair and these pinned helper/setter definitions.

The optional clean-target diagnostic reports 6624 candidate bytes versus 6668
clean-source bytes, 4801 differing bytes and unequal relocation lists. For the
factored baseline it reports 6648 versus 6668 bytes, 4600 differing bytes and
unequal relocation lists. These numbers compare against the clean-source object,
**not retail**; the useful graph change has not established better retail
matching, exact output equivalence or a complete RenderParticles fix.
This live proof covers Windows GC2.7 only; other compiler versions and hosts have
not been tested for this source recipe.

Exact candidate object SHA-256:
`a4ae7d5ed04b07692a720820d585b98b86596f9d85e6f0982252bdc762dc5694`.
The independent verification lives in
`search/verification/candidate-1-WjeYpE/`; its capture SHA-256 is
`ef48513afbc7219dbaa8426d0096b166b464eb622479d57a9f965351912a11f0`
and input Snapshot identity is
`1726526c1b9e5a65b2aeebdf282014d00110369c49601c81bbfc4785c07909c0`.
The candidate full-state fingerprint is
`c98f6ca1cfe711ff44b3da1f8e0d5cfcfe60a027f1d68279417cc5387b8d4e64`;
the factored baseline fingerprint is
`81a6c4cc0dc5eac14e3ec8834929c15b208d6a35d7c6b9758781df92c7ae6421`.
These belong to the frozen analyzer identified above, not a resumed newer build.

`summary.json` records the authoritative terminal status, counts, checker output,
graph observations, full independent evaluation and clean-target diagnostics.
`commands-and-hashes.json` records all four capture hashes, exact input Snapshots,
compiler/stock identities, ordered flags, environment identities, checker/proof
hashes and the native search command. Its compiler commands reconstruct equivalent
native requests; they do not claim to preserve internal generated temporary output
paths. `search/progress.json` and the independent `complete.json` remain the
original native harness evidence.
