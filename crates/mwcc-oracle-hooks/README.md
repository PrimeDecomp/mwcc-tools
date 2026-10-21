# The oracle's Rust hooks

This `no_std` crate builds the i686 Windows COFF objects that the oracle's link adds to the original compiler: the
common hooks, a version-specific frontend and the trace writer. Rust supplies all 31 hooks of each version's
`oracle.hooks` and both output APIs. The same hooks also build for `wasm32-unknown-unknown` as LLVM IR, for the
WebAssembly oracle (the source checkout's `docs/wasm.md`). The source checkout's `docs/oracle/producer.md` has the
current exports, imports and validation.

## Hooks

The 26 common wrappers:

| Symbol | Records |
|---|---|
| `fn_00460c90` | `lc.call` |
| `fn_00507070` | `ra.graph`, `ra.node`, `ra.order`, backend `pv.graph.*`; `ra.temporaries`, `ra.value` (names) |
| `fn_00506f50` | `ra.select`; `st.precolor.*` (stages) |
| `fn_004f7fb0` | `fr.begin`, `fr.layout`, `fr.local`, `fr.temp`, `fr.end` |
| `schedule_block` | `sc.block`, `sc.i`, `sc.after` |
| `select_ready_coloring_node` | `sc.node`, `sc.pick` |
| `fn_00436390` | `bk.stmt` |
| `COptimizer_Optimize` | raw/optimized block lists and codegen/optimized stage snapshots |
| `Peephole_MergeAdjacentBlocks` | `bk.merge`, `bk.pair`, merged block lists |
| `fn_00454030` | optimizer dump-file activation |
| `create_temp_object` | `ce.object`, `lc.temp`, preserving the caller's return address |
| `fn_004601f0` | `lc.use` |
| `fn_00460780` | `lc.pass`, `lc.part` |
| `IRO_CommonSubs` | `ce.pass`, `ce.l`, `ce.end` |
| `move_common_sub` | `ce.move`, `ce.temp` |
| `IroDump_Print` | `ce.replace`, `pp.copy`, `pp.expression`, `pp.dead`, optimizer dump forwarding |
| `fn_00506cd0` | allocation entry: origin reset, CSE registers, before/after snapshots |
| `generate_inline_statements` | ancestry of calls in expanded statement bodies |
| `inline_statement` | nested statement context for inline-decision source lines |
| `CInline_AddFunctionPrecNode` | inline-definition queue changes (`tp.queue kind=0`) |
| `CInline_AddSpecialization` | template-specialization queue changes (`tp.queue kind=2`) |
| `CTemplateNew_InstantiateInlineTemplateObject` | template body requests (`tp.need`) |
| `CTemplateNew_InstantiateFunction` | template-function instantiation (`tp.func`) |
| `fn_0050f240` | deferred emission (`tp.emit`), preserving the original caller EIP |
| `generate_inline_code` | saved-body code generation (`tp.gen`) |
| `setup_inline_locals_and_arguments` | formal/actual bindings (`lc.arg`) |

The five frontend wrappers (GC 2.6 uses 2.7's names):

| Observer | GC 1.3 | GC 1.3.2 | GC 2.7 |
|---|---|---|---|
| Inline decision | `can_inline` | `fn_0055ed70` | `fn_00560c80` |
| Body cost | `fn_0055e540` | `fn_0055e540` | `fn_0055e540` |
| Expression cost | `fn_0055e600` | `fn_0055e600` | `fn_0055e600` |
| Call expansion | `inline_call_expression` | `fn_0055f350` | `fn_00561260` |
| Deferred-node dispatch | `CInline_DispatchNextDeferredNode` | `fn_0055a7f0` | `fn_0055c730` |

Four more common wrappers observe regular-call arguments, and eight arena wrappers observe
`CompilerTools_ResetPool`, `freeaheap`, `freelheap`, `CompilerTools_ResetGallocAndPoolMemory`, `releasegheap`,
`releaseheaps`, `initheaps` and `CompilerTools_InitHeaps` (the opt-in `MWCC_ORACLE_SOURCE_ARENAS` stream; the source
checkout's `docs/oracle/source-arenas.md`). The native link adds guarded probe islands at instructions of the
original (allocations, counters, coalescing, expressions); the source checkout's `docs/oracle/provenance.md` describes
them.

Support exports, not hooks (checked separately, not in `oracle.hooks`): `OracleRust_LinkName`,
`OracleRust_PrintInstruction`, `OracleRust_Snapshot`, `OracleRust_DescribeExpression`, `cse_vregs`,
`OracleRust_OriginFind`, `OracleRust_OriginWalk`, `OracleRust_PrintChain`, `OracleRust_InlineLine`,
`OracleRust_BackendProbe`, `OracleRust_CounterProbeV6`, `OracleRust_ExpressionProbeV1`, `OracleRust_SourceProbeV1`,
`OracleRust_ArgumentForceGpr` and `OracleRust_ArenaProbeV1`. The frontend object imports the origin, chain, inline-line
and link-name functions from the common object.

The tracer's records (`MWCC_ORACLE_NAMES`, `MWCC_ORACLE_COPYPROP`, `MWCC_ORACLE_PEEPHOLE`, `MWCC_ORACLE_ALIAS`, `MWCC_ORACLE_IRO_PHASES`
and the stages' `st.presched` / `st.precolor.*`; the source checkout's `docs/oracle/trace.md`) add `pc.opcode` and
`pc.object` to every instruction record, `ra.value` to the graph and `inl.decide` to each inline decision. The rest
comes from probes in gc2's sources (`OracleProbe_Expand`, `OracleProbe_Presched`, `OracleProbe_DumpPhase`,
`OracleProbe_CopyUse`, `OracleProbe_CopyRemove`, the four `OracleProbe_Peephole*` and the five `OracleProbe_Alias*`), so only the WebAssembly and
source-built oracles have them.

## ABI

* The original compiler still allocates. The wrappers keep the C wrappers' call order, operand-byte guards and trace
  format. Compiler pointers stay raw; packed fields and embedded x86 operands are read without making Rust
  references. No allocator, unwinding or Rust-owned value crosses the ABI.
* Layouts are the compiler's, not MSVC's defaults: `OracleNode` is 28 bytes with alignment 2; `Object` is 68 bytes;
  object lists have alignment 2 and temporary lists alignment 1; `Origin` is 12 bytes with alignment 2;
  `TemplFuncInstance` is 16 bytes with alignment 2. `CPrecNode.kind` is byte 34.
* Naked x86 entries keep `create_temp_object`'s original caller address and `IroDump_Print`'s variadic argument
  pointer.
* Environment queries go through a volatile function pointer, because LLVM otherwise merges repeated `getenv` calls.
  String length and appending are byte loops, because LLVM's `strcat` folding calls `strlen`, which the compiler
  inlines and does not export.
* The trace object owns `OracleTrace_Printf` and `OracleTrace_Enabled` and calls the compiler's C stdio. It is a
  separate object so that LLVM cannot optimize its variadic callers through the entry stub's fixed signature (there
  is no LTO through the original linker). It opens the file lazily, in append mode, falls back to stderr when the
  path is absent or the open fails, and flushes each call. MSL's `FILE` is 84 bytes in every version, so stderr is
  byte 168 of `__files`.

## Building

```sh
rustup target add i686-pc-windows-msvc
cargo build --release --locked
mwcc-tools configure --version GC_1_3_2 --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

`--oracle` links the Rust hooks (`oracle/compiled/rust/hooks.obj`, `oracle/compiled/rust/frontend.obj` and the writer
`oracle/OracleTrace.obj`). `--producer c` builds the C hooks (`src/oracle/Hooks.c`, `OracleTrace.c`) instead.

`mwcc-tools build-hooks OUTPUT` builds one object and checks its exports, imports and relocations before replacing
the output; `--trace-shim` builds the writer (the crate's `trace-output` feature), `--frontend-version V` a frontend
(the mutually exclusive `frontend-gc13`, `frontend-gc132`, `frontend-gc26` and `frontend-gc27` features), and
`--wasm` the IR for the WebAssembly oracle. `RUSTC` or `--rustc FILE` selects the compiler. The target's precompiled
core library is needed, though no Rust runtime is linked.

Host builds contain no producer code; lint the real target:

```sh
cargo clippy --locked -p mwcc-oracle-hooks --target i686-pc-windows-msvc -- -D warnings
cargo clippy --locked -p mwcc-oracle-hooks --target i686-pc-windows-msvc --features trace-output -- -D warnings
```

The C layout probes and differential harnesses that check these layouts against the compiler are in the source
checkout's `tools/migration/abi`.
