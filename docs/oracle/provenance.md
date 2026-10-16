# Retail coalescing provenance (development API)

Build the current native tools and a fresh Rust producer with `configure --oracle`.
These commands are newer than the pinned Rust migration release in the kits.

```text
mwcc-tools oracle compile-trace --oracle-root ORACLE_ROOT --version GC_2_7 --backend SOURCE.cpp -- COMPILER_FLAGS > capture.json
mwcc-tools provenance inspect capture.json --json > coalescing.json
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

The format-1 inspection result retains raw inputs alongside typed decisions:
`rejected`, `already_same_root`, `merged` or `indeterminate`. It validates branch
sites, EFLAGS/outcomes, the ordered short-circuit path, physical-register window
bypass, candidate counts, root ordering and observed parent writes. Malformed or
contradictory records fail parsing. Incomplete captures or paths remain
indeterminate. Report-wide coverage stays conservative when `--link` filters the
display.

`decisions` describes coverage of the captured coalescing passes. A satisfied value
means those decisions were fully observed; it is not a source-search objective.
The separate `lifecycle` result remains indeterminate: creation, number reuse,
allocation categories, expression origins and graph-only lifetimes are not yet
covered. Pass/candidate IDs and raw instruction/register addresses are observation
IDs, not stable cross-compilation identities. These records alone cannot prove a
temporary was eliminated or explain its original C++ expression.

## Validation

`tools/migration/verify_backend_probes.py` runs each selected input with probes
disabled/enabled, checks stock-object equality and unchanged existing traces, then
independently invokes the native explanation command to validate every candidate
path. Full outputs, identities, flags, hashes and elapsed times are retained in a
private output directory. It also accepts `--from-ninja`, `--source` and
`--project-only` for complete game units. This is a development audit; Python is
not a product runtime dependency. A successful trace comparison is not a measured
overhead guarantee or a claim that every rare rejection branch has a live fixture.

Continue with creation/reuse/category instrumentation and lifecycle anchors before
using absence predicates. The complete implementation scope remains in the
[source-guided roadmap](../oracle-source-guided-search.md).
