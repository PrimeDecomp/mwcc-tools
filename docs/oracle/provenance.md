# Backend birth, reuse and coalescing provenance (development API)

Build the current native tools and a fresh Rust producer with `configure --oracle`.
These commands are newer than the pinned Rust migration release in the kits.

```text
mwcc-tools oracle compile-trace --oracle-root ORACLE_ROOT --version GC_2_7 --backend SOURCE.cpp -- COMPILER_FLAGS > capture.json
mwcc-tools provenance inspect capture.json --json > provenance.json
mwcc-tools provenance inspect capture.json --link RenderParticles__11CElementGenFv
```

Use `--from-ninja PROJECT/build.ninja` to recover the existing unit's ordered flags
and working directory. `provenance inspect` accepts raw traces and compile-trace
JSON. `--link` selects an exact mangled name. The default compilation still checks
object equality against the stock compiler; keep that check when investigating a
new producer. `--backend` is opt-in and is isolated from inherited environment
settings. Corpus `--all-traces` also enables it.

## Observations and explanations

The native linker checks the complete audited retail coalescer and its relocation
layout, widens and redirects its local branches, and inserts observation calls. It
preserves the original instructions and uses their existing relocations for data
addresses. A byte/layout mismatch fails linking. Each probe saves general registers,
EFLAGS, x87 state, MXCSR and XMM registers. The retail algorithm continues to decide
which instructions to coalesce.

Format-1 `pv.coalesce.*` records contain pass and candidate observation IDs, the
function link name, active class, counts, physical threshold, window bounds,
instruction identity/operands/flags, branch site/outcome and saved inputs. The
candidate filter is the retail copy flag, rather than an assumed opcode list.
Checks include instruction flags, destination kind/class, roots, interference,
four short-circuit window bounds and the GPR special-register guard. The latter
name describes the observed condition; its higher-level meaning is unconfirmed.
A merge record reads the union-parent entry **after** the retail write.

The format-2 inspection result retains raw inputs alongside typed decisions:
`rejected`, `already_same_root`, `merged` or `indeterminate`. It validates branch
sites, EFLAGS/outcomes, the ordered short-circuit path, physical-register window
bypass, candidate counts, root ordering and observed parent writes. Malformed or
contradictory records fail parsing. Incomplete captures or paths remain
indeterminate. Report-wide coverage stays conservative when `--link` filters the
display.

`decisions` describes coverage of the captured coalescing passes. A satisfied value
means those decisions were fully observed; it is not a source-search objective.
The separate aggregate `lifecycle` result remains indeterminate: semantic
categories, complete expression/inline origins and graph/lifetime bindings remain
incomplete. Pass/candidate IDs are observation IDs. These records alone cannot
prove a temporary was eliminated.

## Births, counter changes and expression context

The producer also observes every audited direct counter write: 570 sites in
GC 1.3, 571 in GC 1.3.2 and 578 in GC 2.7, plus two function-context sites per
version. Most writes are inline increments, including address, call, instruction
selection, struct-move and spill paths. The remaining writes initialize counters,
rewind scratch counters, restore high-water marks or reset after allocation.

Each original memory-write instruction is replaced with an equal-sized CALL/NOP
sequence. Its call island executes that exact relocated instruction and observes
the result. Original sections, instruction offsets and branch targets stay fixed.
Writes that depend on ESP, unknown widths/address forms, missing sites and changed
bytes fail construction. The function-exit island executes the original stack
adjustment after discarding its own return slot, preserving its flag effects.

`history` in the format-2 result includes functions, births, counter changes,
expression observations, orphan writes and gaps. `history.counter_observations`
reports completeness **within the declared direct-counter scope**. Counter
discontinuities, missing function ends, orphan writes and unsupported schemas do
not become successful captures. Each function has its own coverage result.

`Birth.id` is a capture-local function/sequence pair. `generation` counts earlier
births using the same class/number within that function. `registers[].values`
retains all generations. One-based `trace_position` fields preserve the
interleaving of births, counter changes, expression observations and coalescing
checks. Restoring a high-water mark is not recorded as a birth.
Do not bind every graph use to the latest generation or compare these IDs directly
across source variants: semantic correspondence still needs anchors and ambiguity
handling. Births remain in the history even if no final instruction uses them.

Each birth records the physical threshold, original retail creation site, pipeline
phase, source line and available statement-root expression. Expression byte
spellings use hex in the trace; inspection retains bytes and optional UTF-8 text.
The bounded spellings have `complete=false`, and statement roots are not claimed
to be the exact backend leaf expression. `category=null` means the semantic
allocation category is still unknown. Existing frontend provenance can provide
additional evidence; these fields do not silently invent it.

Sites are canonical addresses in the pinned retail image, not necessarily the
relocated execution address. Typed phases name code generation, optimization,
allocation and their following boundaries. Rust callers can use
`provenance::inspect` for captures/raw traces, `provenance::parse` for raw traces,
and `value_history` directly. Capture inspection skips unrelated rendered-report
data rather than building a second full JSON analysis tree.

Audit a pinned retail image independently with the native command:

```text
mwcc-tools provenance audit-counters --root ORACLE_ROOT --version GC_2_7 > counter-audit.json
```

The format-1 audit records image/metadata hashes, direct PE references, access
classification, indexed accesses, undecoded/unresolved sites and function guards.
Its `lifecycle_coverage_complete` remains false: the direct-reference audit does
not prove the absence of all computed aliases or bulk writes. A metadata container
or unit name is a link location, not necessarily the semantic function's name.

## Validation

`tools/migration/verify_backend_probes.py` runs each selected input with probes
disabled/enabled, checks stock-object equality and unchanged existing traces, then
independently invokes the native explanation command to validate every candidate
path and direct-counter history. Full outputs, identities, flags, hashes and elapsed times are retained in a
private output directory. `--case` selects fixture IDs. It also accepts `--from-ninja`, `--source` and
`--project-only` for complete game units. This is a development audit; Python is
not a product runtime dependency. A successful trace comparison is not a measured
overhead guarantee or a claim that every rare rejection branch has a live fixture.

The scratch-reuse fixture preserves a compiler mechanism rather than physical
register choices: with `-O0`, it produces 239 births, one rewind and twelve reused
numbers on all supported versions/hosts. The complete Echoes particle unit records
6,356 births and 4,325 partial expression observations with stock-equivalent output
on both hosts.

Continue with graph-phase/lifetime bindings, categories and complete origins before
using absence predicates. The complete implementation scope remains in the
[source-guided roadmap](../oracle-source-guided-search.md).
