# In-process Rust producer

This `no_std` crate produces three i686 Windows COFF objects, linked by the historical linker: common hooks, a version-specific frontend and trace output. Together they replace all 31 observation wrappers, nine shared helpers and both output APIs. The common object contains these 26 wrappers:

| Symbol | Records |
|---|---|
| `fn_00460c90` | `lc.call` |
| `fn_00507070` | `ra.graph`, `ra.node`, `ra.order` |
| `fn_00506f50` | `ra.select` |
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

`OracleRust_LinkName`, `OracleRust_PrintInstruction` and `OracleRust_Snapshot` provide mangled-name lookup, raw PCode records and allocation/stage snapshots. `OracleRust_DescribeExpression` supplies binding expression spelling, and `cse_vregs` supplies CSE register records. The separate frontend imports `OracleRust_LinkName`, `OracleRust_OriginFind`, `OracleRust_OriginWalk`, `OracleRust_PrintChain` and `OracleRust_InlineLine` to share the common object's ancestry table and statement context. These nine exports are support functions, not retail hooks; they are checked separately and do not belong in `oracle.hooks`.

The retail compiler still performs allocation. The Rust wrappers preserve the C wrapper's call order, operand-byte guards and trace formatting. Compiler pointers stay raw; packed fields and embedded x86 operands are read without creating Rust references. No allocator, unwinding or Rust-owned values cross the ABI. The trace object owns `OracleTrace_Printf` and `OracleTrace_Enabled`, forwarding formatting and flushing to the original compiler's C stdio.

From a Rust migration checkout:

```sh
rustup target add i686-pc-windows-msvc
cargo build --release --locked
mwcc-tools configure --version GC_1_3_2 --oracle
ninja build/GC_1_3_2/oracle/mwcceppc.exe
```

Use the built `target/release/mwcc-tools` executable if it is not on PATH. `--oracle` selects link mode and Rust hooks by default. It builds `oracle/compiled/rust/hooks.obj`, `oracle/compiled/rust/frontend.obj` and the separate writer at `oracle/OracleTrace.obj`, compiling neither `Hooks.c` nor `OracleTrace.c`. Use `--producer c` with `--oracle` for the retained C rollback, or `--producer rust` to state the selection explicitly. Regeneration retains either choice and all object source dependencies. The original compiler/linker/runtime remain external components. The earlier `--rust-hooks` spelling remains an optional alias.

`mwcc-tools build-hooks OUTPUT` builds the object separately, checking its exact exports, expected C imports and supported relocations before replacing any previous output. `RUSTC` or `--rustc FILE` can select the compiler. The target's precompiled Rust core library is needed even though no Rust runtime is linked.

`mwcc-tools build-hooks OUTPUT --trace-shim` builds the separate writer using the crate's `trace-output` feature. It has exactly two exports and five original-runtime imports (`getenv`, `fopen`, `vfprintf`, `fflush`, `__files`). A naked x86 entry captures the original variadic argument pointer. The output object is separate from its Rust variadic callers, so LLVM cannot optimize those calls using the fixed entry-stub signature. There is no cross-object LTO through the historical linker. File opening remains lazy and cached, uses append mode, falls back to stderr after an absent path or failed open, and flushes each call even after a formatting error. MSL `FILE` is 84 bytes on all three versions, placing stderr at byte 168 of `__files`.

Host Cargo builds deliberately contain no producer code. Check the actual target explicitly:

```sh
cargo clippy --locked -p mwcc-oracle-hooks --target i686-pc-windows-msvc -- -D warnings
cargo clippy --locked -p mwcc-oracle-hooks --target i686-pc-windows-msvc --features trace-output -- -D warnings
mwcc-tools build-hooks build/rust-hooks.obj
mwcc-tools build-hooks build/rust-trace.obj --trace-shim
python tools/migration/verify_link.py --binary target/release/mwcc-tools --oracle-root /path/to/frozen/oracle --oracle --rust-hook build/rust-hooks.obj --keep build/rust-abi.json
python tools/migration/verify_producer.py --binary target/release/mwcc-tools --oracle-root /path/to/frozen/oracle --output build/producer-validation --canaries /path/to/canaries
```

The independent verifier compiles C layout probes from the frozen producer and runs 448 direct ABI/guard cases for each version. `OracleNode` has size 28 and alignment **2**, including fields at offsets 12/16/18/20/22/24/26. The Rust alignment must not be inferred from modern MSVC defaults. Layouts are checked for GC 1.3, 1.3.2 and 2.7.

The frame probe checks 33 sizes, offsets, widths, enum values and alignments. In particular, `Object` is 68 bytes with name/type/var-info/uid at 10/14/42/46; names are inline at byte 10 of `HashNameNode`, `Type.size` is a signed 32-bit integer at byte 2, and `VarInfo.reg`/`used` are a signed short at 38 and an unsigned byte at 35. Object lists have alignment 2; temporary lists have alignment **1** with the same next/object offsets. The direct harness checks null and non-local entries, missing names/types/info, signed values, byte promotion, an odd-address temporary list, and observation after the retail call. Positive retail-code operand guards and frame-size reads are covered by live trace comparisons.

The PCode/scheduler probe checks another 76 layout values, including the instruction prefix, twelve-byte operands, blocks/edges, scheduler nodes/edges, callback location and object alias/link fields. Eighteen differential runtime scenarios execute the frozen C helper bodies and Rust helpers against the same packed records. The harness compares every format string and argument byte, string contents, retail/mangler/environment call counts, alias/name fallback behavior, signed/unsigned fields, zero/negative/multiple operands, empty/nonempty snapshots, gated scheduling, a single DAG dump across repeated picks, null picks and retail instruction reordering. The frozen helper source hash is retained. Positive machine/rank operand guards and callback results are covered by live scheduling traces.

The block/stage probe checks eighteen additional layout/flag values, including the packed statement line at byte 22, signed block counts, byte-sized `Boolean`, call mask `0x108`, branch flag `1` and unconditional opcode `0`. Forty-two differential scenarios compare frozen C and Rust, including every merge reason, reason priority, 100/101 limits, null/empty/negative states, independent block/stage options, options changed by retail, post-retail block changes and prologue values 128/255. Environment queries and retail calls are recorded in order along with trace arguments. The harness verifies that its fixtures actually reach all ten merge explanations.

The scalarization/CSE group adds 92 layout, width and enum probes and 88 differential scenarios. The cases cover expression spelling and its length/depth guards, scalar-use return values and argument forwarding, signed `char` fields, post-retail parts and options, register-list filtering, nullable names/types, CSE passes and propagation, dump-file activation and mixed integer/double/64-bit variadic forwarding. Naked x86 entries preserve the temporary creator's original caller address and the dump function's incoming variadic argument pointer. The synthetic retail scalarization function is kept out of line so both implementations reach the same temporary-creation call instruction. The harness compares caller addresses exactly.

Environment queries go through a volatile function pointer: LLVM otherwise merges repeated `getenv` calls. The differential cases compare query order and change the CSE option between its first and second queries. Local volatile byte loops implement string length/appending because LLVM's constant `strcat` optimization introduces `strlen`, which the retail compiler inlines and does not export. The object requires exactly 41 existing C imports, with no Rust runtime helpers. Origin reset uses the original compiler's exported `memset`.

The allocation/origin group adds 104 layout/enum probes and 120 differential cases. `Origin` is twelve bytes with alignment two; its 65,536 slots retain the original 75% admission limit and linear probing. Tests cover collisions/wraparound, duplicate updates, saturation including rejected updates, depth 200/201, all unary/binary/conditional/call walker kinds, nullable inputs and guarded invalid pointers. Chain printing remains outermost-first, bounded to 32 entries even for cycles. Allocation resets ancestry before CSE/snapshots, preserves statement context, and observes post-retail PCode. Statement expansion captures parent/link before retail, reads new statements afterward, honors the returned last statement and byte arguments 128/255, and nested inline statements restore the outer source line. Both implementations run on the same records; the harness checks actual table indices and exact trace/callback ordering.

The integrated verifier builds through native Ninja rules, checks that migrated definitions belong only to Rust, compares full objects/diagnostics and trace output under normal/all trace options, and optionally runs the complete canary corpus. Every output line is retained, including template events, unknown records and blank lines. Frame, scalarization, CSE, propagation and inline records stay exact, including expression text, creator call-site addresses, source lines and ancestry chains. Graph/PCode identities and typed object/label pointer fields are normalized; register numbers, immediates, flags, padding, costs, callback decisions, whitespace, line endings and record order stay exact. Seven checker regression tests confirm those distinctions. Positive frame, PCode, scheduling, block, scalarization, CSE, propagation, inline-record and non-empty ancestry-chain coverage is required. The synthetic compile fixtures do not emit `lc.temp`; the direct ABI harness covers that path. `audit_producer_logs.py` revalidates retained fixture/corpus outputs without recompiling, selecting the current manifest's successful logs after resumed runs. Game-derived outputs remain in ignored local directories.

`verify_trace_writer.py` compares the frozen C writer and Rust in 24 fresh-process scenarios per version. It probes FILE layout and checks raw C/Rust variadic arguments, promoted values, doubles/NaN payloads, 64-bit integers, long messages, lazy/cached open failures, empty/literal-zero/8-bit paths, errors and reentrant getenv/fopen/format/flush callbacks. Select the supported private loader with `--wibo` or `MWCC_WIBO` on Linux. `verify_trace_files.py` also exercises the actual compiler stdio: absent/empty paths, literal `0`, missing parents, directories, append preservation and stderr fallback, comparing complete traces, line endings and object output. Each invocation retains a unique proof tree.

Use `--existing-root DIR --skip-build --evidence-name NAME.json` for additional comparisons against an already built producer. This reads the existing executable without rewriting its build files or outputs; the evidence explicitly records that no native build was performed. It builds private validation copies of the current Rust writer and each frontend, comparing their bytes with the linked objects. The proof therefore requires the pinned Rust target even in skip-build mode. Keep distinct evidence names for fixture, project and final host-tool checks.

Use `verify_link.py --retain-builds --diagnose-abi` to retain failed link/runtime trees and print the first differing serialized bytes. Ordinary runtime harness builds still import only `ExitProcess`; diagnostic builds also import `GetStdHandle` and `WriteFile`.

`verify_c_producer.py` checks the retained C build against the frozen C source, including every non-debug section byte, BSS size, flag, symbolic relocation and public definition. Select it explicitly with `--producer c`; its guarded source remains available for rollback.

`verify_producer_sources.py DIR --binary FILE` builds all linked Rust objects afresh and compares their complete bytes with a retained runtime proof. It records all current/retained source hashes in a separate immutable proof directory. This can establish that a Rust declaration or comment cleanup leaves the already tested artifact unchanged; C/header/config differences or any changed object require new runtime evidence. The original runtime proof and its source snapshots are preserved.

The common instantiation group adds fourteen layout/width probes and 96 differential scenarios. `TemplFuncInstance` is sixteen bytes, aligned to two, with its object pointer at byte four. Cases check null and unaligned specialization records, null context, name and alias fallback, unsigned byte results and flags, signed mode bytes, queue changes and nonchanges, options/context changed by retail, trace-before-retail versus trace-after-retail ordering, and exact caller EIP through one indirect call instruction. At that checkpoint, the object had exactly 34 exports and 52 C imports, with no Rust allocator/runtime helpers.

The binding group adds 44 layout/width/enum probes and 72 differential scenarios. They cover all four binding modes, absent/disabled options, negative/zero/multiple formal counts, packed records at odd addresses, post-retail slot/formal/option changes, declared-name identity, nullable names/types, byte promotion, reference and generated-function flags, expression unwrapping and empty expression fallback. `generated` remains Boolean even though its underlying flag is `0x100`. The current object has 35 exports and 54 C imports.

The frontend object implements inline decisions, body/expression costs, call expansion and deferred-node dispatch. Default selection, packaging and the migration's final integration gates remain. The post-migration source-guided search roadmap is separate.

Each supported version's `oracle.hooks` inventory contains 31 retail wrappers. The five version-specific frontend wrappers are:

| Observer | GC 1.3 | GC 1.3.2 | GC 2.7 |
|---|---|---|---|
| Inline decision | `can_inline` | `fn_0055ed70` | `fn_00560c80` |
| Body cost | `fn_0055e540` | `fn_0055e540` | `fn_0055e540` |
| Expression cost | `fn_0055e600` | `fn_0055e600` | `fn_0055e600` |
| Call expansion | `inline_call_expression` | `fn_0055f350` | `fn_00561260` |
| Deferred-node dispatch | `CInline_DispatchNextDeferredNode` | `fn_0055a7f0` | `fn_0055c730` |

Build a frontend separately with `mwcc-tools build-hooks OUTPUT --frontend-version GC_1_3|GC_1_3_2|GC_2_7`. Its object has exactly five exports and twenty imports; unsupported versions, a wrong-version object and combined frontend/trace modes are rejected. The crate's `frontend-gc13`, `frontend-gc132` and `frontend-gc27` features are mutually exclusive, and are linted on the actual i686 target. The common and trace objects remain identical across all three versions.

`verify_frontend.py` runs 61 C layout/width/enum probes and 256 differential scenarios per version. It compares raw trace arguments, retail/helper/query ordering, byte returns, aliases/namespaces/truncation, expression descriptions, every unary/binary call-walk kind, conditional/member walks, candidate/depth markers, nested costs, full-cost state restoration, expansion ancestry and deferred parsing. Headers retain the GC 1.3 option/body layout; the later versions' explicit offset overrides are checked against the frozen C macros using distinct bytes at each candidate offset. `CPrecNode.kind` is byte 34, as independently measured after the first live fixture exposed the initial wrong offset.

`verify_frontend_switches.py` compares default, expression-only, full-cost-only, combined-cost and instantiation-only compilations, including all output lines/endings and object bytes. `verify_link.py --rust-frontend --compiled-oracle-root DIR` supplies the matching frontend object alongside the common Rust hooks and writer for independent Python/native linker comparisons.
