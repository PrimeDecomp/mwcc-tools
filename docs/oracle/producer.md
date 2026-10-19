# The Rust producer and its observer ABIs

`mwcc-tools configure --oracle --root ORACLE_ROOT --version VERSION` configures an oracle with the Rust producer
(`--producer c` selects the C hooks); then build its generated target. Keep built producer trees immutable: a new
observer needs a new build root.

## Native surface

`producer::validate` checks the complete common COFF surface against the constants in `producer.rs`: `HOOKS`,
`SUPPORT` and `IMPORTS` (with `ARGUMENT_HOOKS`, `ARENA_HOOKS` and `SOURCE_HOOKS` among them), and
`TRACE_EXPORTS`/`TRACE_IMPORTS` for the separate Rust trace writer. The frontend object exports five hooks per
version. Unexpected compiler runtime helpers fail the build instead of being linked silently.

- **Call arguments:** `_OracleRust_CounterProbeV6`, `_OracleRust_ArgumentForceGpr` and four regular-call
  wrappers (`_FunctionCalls_GenerateCall`, `_assign_argument_locations`, `_load_argument_registers`,
  `_store_argument_on_stack`). Linking renames the original definitions to `__retail` and redirects their callers
  to the wrappers.
- **Arenas:** `_OracleRust_ArenaProbeV1` and the eight reset/release/initialization wrappers. The linker provides
  `_OracleRust_ArenaGlobals` (12 bytes: relocated first pool descriptor pointer, local lock pointer, version). Each
  arena island passes a 36-byte descriptor: that globals pointer, canonical site, action, pair, pool and size/result
  selectors. Islands preserve registers, flags and FXSAVE state; allocation commits run their original descriptor
  stores before the observer; the failure-callback island restores the retail stack before calling the original
  callback. See [source arenas](source-arenas.md).
- **Source nodes:** `_OracleRust_SourceProbeV1` and three wrappers (frontend ownership, mode changes, recursive
  cloning). A guarded statement-store island records assignment emission. See [source nodes](source-nodes.md).

Link-name lookup can allocate compiler hash entries, so frontend inline and template observers resolve every link
before emitting the first fragment of a multipart trace line.

## Counter descriptor

48 bytes, 4-byte aligned, on the i686 target (compile-time size/alignment/offset assertions):

| Byte offset | Field |
| --- | --- |
| 0, 4, 8 | Counter, counter-table and physical-threshold pointers |
| 12, 16 | Signed selector and observation role |
| 20, 24, 28 | Initial-range, coalescing-first and coalescing-last pointers |
| 32, 36 | Object-binding and operand-binding selectors |
| 40, 44 | Version-specific data-link offset and origin-binding selector |

Counter call islands run the original relocated instruction and preserve the machine state. Regular-call wrappers
use the verified calling convention: two raw words for GenerateCall, **six** for location assignment, one for
register loading or stack storage. ForceGPR forwards three raw words through a separate retail jump thunk.
Assignment does not use the reconstructed C prototype; the upper bits of partially initialized words are forwarded
intact.

## Construction guards

`call_argument_probe::install` validates the original PE, symbol addresses and sizes, the complete split bodies
and their relocation identities (offset, kind, target name/value/section/type/storage/auxiliary records, raw
addend) before installing argument observations. A matching opcode with a different helper target is
insufficient. The guarded helpers (GenerateCall, reverse evaluation, register loading, stack storage, location
assignment, PCode append, call/intrinsic dispatch, ForceGPR and Normalize or their containing body), their
addresses, sizes and hashes are in `call_argument_probe.rs`; each producer writes them to
`build/VERSION/oracle/arguments/guards.json`.

The constructor changes only the REL32 target/addend at register-loader offset `+0x2ee`, after the audited CALL at
`+0x2ed`. GC 1.3 targets the named ForceGPR helper; later versions target the guarded container at `+0xd00`
through the retail thunk. Section sizes, other bytes, other relocations and public symbols stay fixed.

At runtime each 48-byte argument context is bound to its source-list node. Calls, contexts, episodes and
same-block PCode walks are bounded; unsupported locations, inconsistent identities, block changes and exceeded
limits emit gaps. Only tag-defined operand fields are read, and callee names come from the cache without calling
the mangler. See [call arguments](call-arguments.md).

## Checks

- `tests/fixtures/backend/call_arguments.cpp` covers a symbolic address, a global reference, a nested call and
  register, stack, floating-point and two-word argument lowering.
- The ignored test `guarded_argument_retarget_preserves_other_bytes_and_rejects_corruption`
  (`crates/mwcc-oracle-core/tests/call_argument_probe.rs`, with `MWCC_ORACLE_TEST_ROOT`) checks the preserved layout
  and rejects byte, relocation-target, addend and symbol-value corruptions.
