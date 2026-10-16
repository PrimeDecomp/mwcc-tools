# Development Rust producer and V6 observer ABI

Build a fresh producer from the current sources with `mwcc-tools configure
--oracle --root ORACLE_ROOT --version GC_2_7`, then build its generated target.
Repeat for GC 1.3 and GC 1.3.2 when checking all supported development observers.
Keep retained producer trees immutable: a new observer needs a new build root.
The kit's pinned migration release may predate these development capabilities.

## Native surface and descriptor

`producer::validate` checks the complete common COFF surface: 30 hook definitions,
13 support exports and exactly 60 imports. The common object therefore exports
43 symbols. The separately built frontend object exports five hooks per compiler
version; the separate Rust trace writer exports two symbols and imports five C
runtime symbols. These inventories are code in `producer.rs`, not inferred from
the presence of a few familiar exports. Unexpected compiler runtime helpers fail
the build instead of being silently linked.

V6 requires `_OracleRust_CounterProbeV6`,
`_OracleRust_ArgumentForceGpr` and all four regular-call wrappers:

```text
_FunctionCalls_GenerateCall
_assign_argument_locations
_load_argument_registers
_store_argument_on_stack
```

The native linker rejects older observer objects. These four names are supplied
by `producer::ARGUMENT_HOOKS` only on the Rust path. The configured C hook list and
C rollback producer remain unchanged. Linking renames the original definitions
to `__retail` and redirects their callers to the wrappers.

The counter descriptor remains 48 bytes, aligned to four bytes, on the i686
producer target. V6 changes the observer capability and routing rather than
adding an argument pointer to each counter descriptor.

| Byte offset | Field |
| --- | --- |
| 0, 4, 8 | Counter, counter-table and physical-threshold pointers |
| 12, 16 | Signed selector and observation role |
| 20, 24, 28 | Initial-range, coalescing-first and coalescing-last pointers |
| 32, 36 | Object-binding and operand-binding selectors |
| 40, 44 | Version-specific data-link offset and origin-binding selector |

The hook has compile-time size/alignment/offset assertions. Counter call islands
execute the original relocated instruction and preserve the saved machine state
as described in [backend provenance](provenance.md). Regular-call wrappers use
the verified C calling convention: two raw words for GenerateCall, **six** for
location assignment, and one for either register loading or stack storage. The
audited scalar ForceGPR call forwards three raw words through a separate retail
jump thunk. In particular, assignment does not use the reconstructed C prototype,
and the upper bits of partially initialized incoming words are forwarded intact.

## Construction guards

`call_argument_probe::install` validates the pinned original PE, symbol address
and size metadata, complete supplied split bodies, and relocation identities
before installing argument observations. Each relocation identity includes its
offset, kind, target name/value/section/type/storage/auxiliary records and raw
addend. A matching opcode with a different helper target is insufficient.

| Guarded helper | GC 1.3 | GC 1.3.2 | GC 2.7 | Bytes |
| --- | --- | --- | --- | --- |
| GenerateCall | `5267c0` | `527320` | `5281b0` | `6a6` |
| Reverse evaluation | `526e70` | `5279d0` | `528860` | `12b` |
| Register loading | `526fa0` | `527b00` | `528990` | `6e7` |
| Stack storage | `527690` | `5281f0` | `529080` | `38d` |
| Location assignment | `527a20` | `528580` | `529410` | `60f` |

Addresses and sizes above are hexadecimal canonical retail coordinates. Guards
also cover PCode append and regular-call/intrinsic dispatch, plus ForceGPR and
Normalize in GC 1.3 or their complete containing body in later versions. There
are nine guarded bodies in GC 1.3 and eight in each later version. Full hashes
and relocation digests are defined in `call_argument_probe.rs` and retained in
`build/VERSION/oracle/arguments/guards.json` in each new producer.

The constructor changes only the REL32 target/addend at register-loader offset
`+0x2ee`, following the audited CALL opcode at `+0x2ed`. GC 1.3 targets the named
ForceGPR helper; later versions target the guarded container at `+0xd00` through
the retail thunk. Original section sizes, other bytes, other relocation targets
and public symbol locations remain fixed. Existing counter and coalescer probes
are installed separately, after these argument guards.

Runtime validation binds each returned 48-byte argument context to its actual
source-list node. Calls, contexts, episodes and same-block PCode walks have
explicit bounds. Unsupported locations, inconsistent identities, block changes
and exceeded limits emit gaps. Only initialized, tag-defined operand fields are
observed. Cached callee names are read without invoking the mangler. The scope
is [guarded regular-call arguments](call-arguments.md), not complete C++ argument
semantics, all intrinsic paths or value lifetimes.

## Reproduce the fixture audit

The committed `tests/fixtures/backend/call_arguments.cpp` is byte-identical to
the audited fixture: SHA-256
`9e60c5370527f025e13eb257a402ffee9b3cb9fa50abe3386dc69c1ff0289e50`.
It covers a symbolic address, a global reference, a nested call, and register,
stack, floating-point and two-word argument lowering. It is a standalone
development audit; it does not add cases to the standard backend manifest.

With an already built V6 producer and Python 3.11 or newer:

```text
python tools/migration/verify_call_arguments.py --binary MWCC_TOOLS --oracle-root V6_PRODUCER --output NEW_AUDIT_DIRECTORY
```

The script retains exact commands, compiler captures, objects, diagnostics,
inspection results and hashes. It compiles with observation disabled/enabled,
keeps independent stock checking enabled, and verifies unchanged existing trace
records. The address argument at lowered ordinal 1 owns two guarded Normalize
births; the reference argument owns zero. The nested call's observed block
transition remains an explicit gap. Planned GPR4 and helper operands are not
reported as proof of final instruction ownership or semantic lifetime.

On Linux, use the producer's pinned loader through `MWCC_WIBO` when required.
The script neither configures nor rebuilds the supplied producer. Use a new
output directory to preserve prior evidence.

The ignored constructor test `guarded_argument_retarget_preserves_other_bytes_and_rejects_corruption`
in `tests/call_argument_probe.rs` uses `MWCC_ORACLE_TEST_ROOT` to read existing
split originals. Across all three versions it checks the preserved layout and
rejects 91 byte, relocation-target, addend and symbol-value corruptions. It does
not modify the supplied producer.
