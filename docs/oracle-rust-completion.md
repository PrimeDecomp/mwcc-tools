# Rust migration acceptance audit

The implementation covers the migration plan's host tools, oracle-specific kit
consumers, executable construction and all 31 in-process observation hooks.
The production boundary is native commands plus versioned JSON. Python imports,
argument spelling and human report prose are not compatibility requirements.

The original compiler, historical linker/runtime and external build tools remain
external. This migration observes the original compiler; it does not replace its
allocator, optimizer or code generator. C hook sources remain an explicit rollback.

## Replacement inventory

| Scope | Native replacement | Acceptance evidence |
|---|---|---|
| Compile/trace/analyze/report | `oracle`, `oracle compile-trace`, `oracle analyze-trace`; `runner`, `trace`, `document` library modules | Schema-6 fixtures, format-1 compilation/check/provenance, raw/optional records, failure/timeout/override/limit/fallback checks; Windows/Linux live comparisons |
| Allocation what-if tools | `colouring verify`, `target`, `force`, `search`, `replay`, `remap` | Seeded CPython RNG contract, graph edits/claims/decoding, saved traces/manifests, unsupported spills and mismatch fixtures; separate audited benchmarks |
| TU/layout/inlining tools | `inline-diff`, `data-diff`, `inline-window`, `header-impact` | Synthetic and live relocation/layout/weak-symbol/threshold/header-override comparisons on both hosts |
| Kit consumers | `oracle run-unit`, `oracle regdiff`, `colouring unit`, `inline-calibrate` | Structured explanations/checks, real units/candidates/header experiments, saved sessions and both calibration modes; migrated kit docs and tests |
| PE/COFF and executable construction | `pe`, `implib`, `rsrc2res`, `split`, `link`, `compare` | Checked binary fixtures and failure cases; complete eight-version verification/nine-gate construction matrices on both hosts |
| Binary investigation/metadata | `locate`, `bindfrom`, `funcmatch`, binary `remap`, `derive-splits` | Synthetic ambiguity/conflict/relocation cases and full version metadata/derivation comparisons |
| Disassembly/comparison | `disasm`, `fdiff`, `nearest` | 7,884 decoder inputs and complete live reports; pinned Capstone 5.0.9 notices |
| Build/configuration/acquisition | `configure`, `transform-dep`, `download`, `verify`, `gates` | Synthetic configuration cases, 26 complete real graphs per host, UTF-8 dependency tracking/quoting and generated compilation; cached/fresh acquisition and process-tree/error checks |
| In-process producer/output | `mwcc-oracle-hooks`, native `build-hooks`, default `configure --oracle` | Three i686 COFF objects; 26 common/five frontend wrappers, shared helpers and both trace-output APIs; layout/runtime ABI, ownership, trace and stock-object checks |
| Distribution | `package-release`, native kit installer/manifests/source bundles | Deterministic ZIP assembly, matching notices/lockfile, source/executable/protocol identities, extraction/standalone execution and checked compilation |

Native runtime commands and generated Ninja rules do not invoke Python. The main
build workflow and shell gate wrapper use native commands. Python remains useful
for independent migration verification, the frozen reference, and unrelated kit
installation/research/game workflows.

## Final producer selection

`configure --oracle` selects linking and the Rust producer for GC 1.3, 1.3.2 and 2.7.
`--producer c` explicitly selects the retained C implementation. Ninja regeneration
records either selection; invalid/conflicting choices preserve the prior graph.
Oracle instrumentation in explicit compare mode is rejected. The earlier Python
interface could silently ignore `--oracle` in GC 1.3's default compare mode; the
native interface makes its meaning explicit.

Windows proof: `build/rust-migration/default-producer-final/rust-producer-5qmlwrcn`.
Linux proof: `/home/henrique/.cache/mwcc-rust-default-producer-final/rust-producer-kirik8ta`.
Each fresh build uses the actual default, compiles no C observation sources, passes
definition ownership checks and matches the frozen producer on twelve fixture
compilations per version. Independent fixture-log and current-source object audits
pass for all three versions on both hosts. The two hosts' 26 real configuration
comparisons also pass with explicit C selection for the frozen C reference graphs.

`build/rust-migration/default-producer-final/image-equivalence.json` records all
six new images' equality with the earlier full-corpus proofs after normalizing
only the PE timestamp. Every linked Rust object is byte-identical. The previous
2,313-canary-per-version runs and available Prime/Echoes corpus evidence therefore
remain attached to identical producer code. The final default-selection run repeats
the fixture checks; it does not claim another canary run. Earlier proof trees and
measured benchmark executables remain unchanged.

Current workspace validation passes 81 offline tests, formatting and all-target
Clippy on Windows and Linux. Existing target-specific producer ABI/lint evidence
remains applicable because the producer sources and emitted objects are unchanged.
Remote CI execution has not been observed; the workflow contains the corresponding
tests, target checks, native construction and release archive audits.

## Consumer and release evidence

Prime and Echoes native kit previews first used source pin `e91a881`. Both archives
contain the native API guide, checked Windows/Linux host assets and a complete Git
source bundle. Tests importing legacy oracle internals were replaced by native
artifact/install/protocol tests. The Windows suites pass 69/52 tests. Linux native
contract tests pass for both kits; an initial surrounding-worktree Git discovery
failure was fixed by inspecting bundles outside that checkout. Historical research
and unrelated kit workflows are retained.

Independent archive/pin/payload/install audits on both hosts also compile Prime's
35-function `CGuiFrame` and Echoes' 136-function `CElementGen` units with explicit
stock equality, producing identical cross-host objects and leaving all copied
inputs unchanged. Final artifacts use code pin `9e67176bcb7310559bb74455026f6643fd505e40`. Both native packages and both final kit archives pass their extraction/install/protocol checks; the final kit audits repeat real-unit stock equality on Windows and Linux using the fresh default producers. Final identities are in [the release manifest](oracle-rust-release-manifest.json) and [status document](oracle-rust-status.md).

The [benchmark report](oracle-rust-benchmarks.md) contains audited parsing,
allocation analysis, replay, seeded search, compilation, complete-command and RSS
measurements. It records the slower small-input cases and higher analysis memory
use as well as the faster components. No performance threshold replaces a
correctness gate. macOS execution and unavailable historical input revisions are
not claimed; the retained manifests identify the available, often larger corpora.

Public publication, replacement of live game-worktree overlays and new whole-game
matching claims are separate from delivering the native implementation and new
local kit archives. Source bundles make local release pins directly usable.

## Deferred scope

The [source-guided graph-search roadmap](oracle-source-guided-search.md) remains
separate: full backend/coalescing provenance and graph-directed C++ variant search
come first, followed by mechanism-preserving reduction and matched-source indexing.
None of those extensions was implemented as part of this migration.
