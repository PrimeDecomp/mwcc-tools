# Bounded V6 source discovery: EnvModulate call structure

This investigation tests a small new source family on the clean full
`CElementGen.cpp`, using the guarded V6 call-argument observations. It keeps the
original scalar `noRota` source unchanged. It is separate from the earlier
[function-entry shared-pointer experiment](oracle-source-guided-case-envmodulate.md)
and the [promoted-array experiments](oracle-source-guided-case-promoted-flags.md).

## Contract and prior experiments

The baseline is source SHA-256
`0a216aa15cd1f169e67ff43bdec4867f55ae5f1f8da4cbc883ce1b6ec60dfab3`.
The source and 127 header dependencies are materialized from the existing
address-reuse search's retained input blobs. The game checkout is not an input
working directory for these compilations. The sealed V6 Windows producer is
`call-argument-audit/windows-producer4/rust-producer-hdo17756`; the analyzer is
the frozen final CLI with SHA-256
`012fd34b713cd8f16ed55558e8efc0c727b24df09df3313b149ba09e1d23f9ff`.

The native search asks for at most one function-wide guarded Normalize pair for
`kEnvModulate__9CGraphics`, while retaining at least one HA(kind6) and LO(kind4)
relocation with zero addend. This is deliberately stronger than the previously
successful two-pair function-entry pointer. The configured `source_transformation`
checker admits only the unchanged baseline or three byte-exact candidates under
pinned header bodies. It is a finite rewrite justification, not unrestricted
semantic equivalence or a gameplay test. No fixed addresses, altered symbol
declarations, changed calling convention or CTevPass copies are introduced.

Prior work already tested forwarding wrappers with reference/pointer/const-pointer
parameters, a pointer/integer round trip, a function-entry shared pointer, broad
intrinsic/cast/loop aliases, and many scalar/array declaration forms. Historical
scripts also include ABI-changing declaration/parameter experiments; those are
excluded from this family. The two pointer-array recipes in the earlier native
plan were never reached after its winner; their presence is not test evidence.

The matched `Render2D` example does not justify redeclaring `kEnvModulate` as a
reference. `kEnvPassthru` is already a reference-global whose stored pointer is
loaded via SDA. `kEnvModulate` is an object. Changing that ABI would select a
different mechanism by changing the program's declaration contract. These
experiments instead retain that object and its pure accessor.

## Finite source family

- **Late reference:** immediately after the existing texture `Load`, bind
  `const CTevCombiners::CTevPass& oracleEnvPass = CGraphics::EnvModulate();` and
  use that reference at the two original direct calls.
- **Late pointer:** at the same position, bind
  `const CTevCombiners::CTevPass* const oracleEnvPass = &CGraphics::kEnvModulate;`
  and dereference it at those two calls. The earlier winner declared its pointer
  at function entry. Both cache the object's address, not its mutable contents.
- **Factored direct call:** move the trivial enum `scale = kTS_Scale1` declaration
  just outside the `mXTAD` conditional, keep the same conditional GetValue/switch,
  then choose the original direct SetTevOp versus SetParticleTevScale once at the
  common continuation. Null `mXTAD` retains scale1; every non-null case selects
  the same scale and same call after the same GetValue. No observable operation
  is moved across another call.

The helper body, all headers, the texture access, subsequent UV calls and all
`noRota` code remain unchanged. The implementation records exact recipe anchors,
candidate hashes and the local proof scope under
`build/source-guided/source-origin-bridge/source-discovery/`.

Argument-specific results select the exact qualified `SetTevOp` link, a declared
lowered call occurrence and lowered-context ordinal1. That is an observed
argument-creation scope, not a recovered C++ parameter/source-write lifetime.
Call factoring can change occurrence structure, so an equal ordinal across
variants must not silently become source ancestry. Pair counts are kept separate
from complete temporary elimination, source lifetime identity and final retail
matching.

## Initial three-variant results

The native run ends `bounded_search_exhausted` after five stock-verified captures:
original/private calibration, followed by one capture for each candidate. Every
hard constraint and configured closed-source check passes. Each candidate reaches
two global EnvModulate Normalize pairs, so the requested maximum of one remains
**unsatisfied**. None received independent winner verification. This is a measured
source/graph experiment, not a `verified_success` or a RenderParticles matching fix.

| Source | Global Env pairs | First lowered SetTevOp argument1 pairs | GPR graph nodes | noRota stored degree | Env-half neighbors of noRota | SetTevOp calls |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Clean baseline | 3 | 1 | 555 | 159 | 6 | 5 |
| Late reference | 2 | 0 | 554 | 158 | 4 | 5 |
| Late pointer | 2 | 0 | 554 | 158 | 4 | 5 |
| Factored direct call | 2 | 1 | 553 | 157 | 4 | 4 |
| Earlier function-entry pointer, separate retained case | 2 | Not evaluated here | 554 | 156 | 2 | Not evaluated here |

The native first-argument objective independently validates complete observed
argument ownership and the required symbolic relocations. Zero pairs within that
argument means zero creations on the selected guarded Normalize path in its
episode subtree. It does not mean zero pairs in the function or absence of every
argument temporary. Factoring instead reduces the actual observed SetTevOp call
count while retaining a materialization in its first selected argument.

All three current candidates retain two address-result nodes confirmed merged to
physical r4. The late reference/pointer result nodes have stored degrees 8 and 7;
the baseline has three degree 7 result nodes. The previous function-entry pointer
has a different retained outcome: one address result remains virtual with degree 4
and lacks a noRota edge. The later declaration therefore does not reproduce that
earlier graph state. `noRota` remains a uniquely named, instruction-visible object
whose observed category is before the coalescing window in all current variants.
Its raw register changes from 55 to 56 for the late declarations; that number is
not the selector or the success condition.

All four current format-4 fingerprints are available and distinct. Equal pair
counts or headline graph metrics do not establish equal full relevant state;
the harness does not merge these candidates. A separate exact comparison does
find that the late pointer and late reference have identical complete numbered
node observations and identical full stock-verified objects, SHA-256
`b6fc4393724906582f9d199a2a3c16c761a534bb70a1c9141f82c4fe324e68f9`.
The fingerprint retains more of the observed histories than those final objects
and node fields. This closes that small syntax family for the present graph goal
without claiming identical whole compiler histories or source lifetimes. The
comparison is retained as `late-forms-comparison.json`.

Native ELF comparison reports changed RenderParticles instruction bytes for all
three candidates. The baseline, late reference and late pointer are 6,668 bytes;
the factored case is 6,648 bytes. No
retail percentage, final-register success or semantic lifetime equivalence is
claimed from these graph changes. In particular, the known earlier entry-pointer
graph improvement made the final match worse; the new deltas are not rankings by
retail matching quality.

## Evidence

`private-inputs.json` records the retained source/snapshot identities, private
header paths/hashes, effective flags and excluded prior families. `plan.json`,
`closed-rewrites.json`, `check_closed.py` and `closed-variants/` preserve the
bounded source proof. The native `search/` retains original/private calibration,
exact before/after input scans and blobs, stock checks, objective and checker
results, grouping uncertainty and any independent winner verification. No sealed
producer or historical experiment is overwritten.

`commands-and-hashes.json` records the exact native search command, analyzer,
checker and closed-proof hashes, complete captured Snapshots and reconstructible
per-capture native compile requests. These reconstructed CLI requests do not
claim to be the original raw compiler subprocess argv with its generated temporary
output paths. `summary.json` records the source/object/input/fingerprint identities,
native hard/checker/objective results, actual materialization roots and uniquely
anchored noRota graph facts. `argument-evaluations/` contains the native contracts,
commands, first-argument results and native ELF comparisons. All evidence remains
local under `build/source-guided/source-origin-bridge/source-discovery/`.
