# Wii 1.0RC1 and GC 3.0a5.2 compiler layouts

These layouts guide the [Wii 1.0RC1 source port](../README.md#wii-10rc1-source-port)
and the Wii_Retro native relink. GC 3.0a5.2 provides the comparison reference for the
compiler of the Prime games' Wii builds, which sits between GC 3.0a5.2 and Wii 1.0RC1. The plan is in
[../../docs/wii-retro-compiler.md](../../docs/wii-retro-compiler.md).

- `Wii_1_0RC1/`: Wii 1.0RC1 (`build/compilers/Wii/1.0RC1/mwcceppc.exe`), from its CodeView (`tools/wii_layout.py`).
  - `functions.json`: each function's name, address, size, CodeView module and unit.
  - `symbols.txt` and `splits.txt`: in decomp-toolkit's formats, as under `config/`.
  - `units.json`: the module-to-unit table, and the pieces of a unit's section outside its `splits.txt` range.

  A module that has a source here is named by that source (`src/optimizer/IroLoop.c`). The others are named
  `wii/<module>`. CodeView names no static function, so a static is `fn_<address>`.
  - `names_from_2_7.json`: names for 49 of RC1's statics, matched from 2.7 (`tools/wii_chain_names.py`).
- `GC_3_0a5_2/`: GC 3.0a5.2, which has no symbols. Its names are carried over from RC1 by relocation-masked
  matching (`tools/wii_remap.py`): 5557 of RC1's 6073 functions are found. Each function's `how` says how it was
  found: `exact`, `propagated` (call graph), `order` (by position between found functions: a guess) or `fuzzy`
  (byte similarity: a guess). A function with no RC1 counterpart is `new`, or `new+between` when it lies between two
  functions of one module and takes that module. An RC1 static keeps its RC1 name, so `fn_005bb650` in 3.0a5.2 is
  at another address.
- `wii-rc1-differences.md`: the functions that differ between the two, per module (`tools/wii_diff_report.py`).

Regenerate the RC1 layout with:

```sh
python tools/wii_layout.py Wii_1_0RC1 build/compilers/Wii/1.0RC1/mwcceppc.exe --write
```

The [current build](../../docs/wii-retro-compiler.md#build-wii_retro) relinks RC1 with recovered C routines
and compatibility patches. The following binary patchers reproduce the earlier experiments.

`tools/wii_patch_struct_copy.py` writes a copy of RC1 that copies structs as the Prime games' Wii compiler does,
without RC1's legacy stack layout. Two more patchers add passes to a copy of RC1, built from `tools/wii_retro/` and
added by `tools/wii_retro_pe.py`:
- `tools/wii_patch_pretest.py` leaves out the loop pretests that compiler leaves out. `tools/wii_drop_pretest.py`
  does the same to compiled objects, for testing.
- `tools/wii_patch_template_vtables.py` emits class template instances' vtables and virtual functions after each
  declaration, not at the end of the file.

The [compiler research notes](../../docs/wii-retro-compiler-research.md) record their effects on Echoes.

## Source-port references

- The Windows and WebAssembly RC1 oracles use the GC 2.7 source bindings to locate
  46 shared hook functions. The WebAssembly linker checks their signatures. Both
  instrumented builds check RC1's option and inline-function layouts.
  The arena allocators at `0x46EB70` through `0x46EC30` round requests up to a
  multiple of eight. `InitNameHash` starts with `0x2000` bytes. The expression
  statement probe uses the native store of the expression pointer at `0x509CAC`.
- RC1's `CodeGen_Generator` stores `gFunction` at `0x46A55A` and enters its normal
  epilogue at `0x46B3C1`. Its counter table starts at `0x758278`. The optimized local
  allocation path at `0x46D8B0` starts with arguments and then visits locals.
  GC 2.7's exception-register and frontend-temporary category writes have no
  corresponding instruction in that path. The RC1 oracle reports those category
  gaps independently of counter writes it can verify.
- RC1's `call_function` dispatches on `Registers_ClassForType`
  (`0x590530`). Hardware floats and Gekko paired singles share the FPR
  counter write at `0x5fcd3e`. Scalar GPR results use `0x5fd244`, and
  the two GPR allocations for a wide scalar use `0x5fce22` and
  `0x5fce37`. Small aggregates returned in GPRs use a separate path,
  with one write at `0x5fd314` or two at `0x5fd2a2` and `0x5fd2b7`.
  Native execution confirmed the branches for pointers, enums, wide
  integers, both aggregate sizes, paired singles, and ordinary hardware
  and software floating-point results. Gekko soft float and soft double
  use the scalar and paired GPR paths respectively. RC1 also has an
  alternate soft-double write at `0x5fcebd` behind the mode byte at
  `0x74f2c0`, which was clear in the Gekko and e500 controls.
- The register map also covers both result words and the sign-extension
  temporaries in `I8_gen_ADD` (`0x51a650`), and the GPR allocations
  selected by the operand-kind jump table in `Coerce_to_register`
  (`0x5ae490`). Native controls exercised the two wide-add result writes,
  both signed narrow addends, the unsigned zero fill, and the indirect
  load with an immediate offset.
- `coerce_to_register_pair` (`0x5ade10`) allocates an address GPR at
  `0x5ae12e` before allocating any missing destination registers for an
  indexed load. GC 2.7 uses the low result register as its address in the
  same fixture. Native RC1 fixtures also exercised the FPR counter writes
  for immediate and indexed loads (`0x5adde6`, `0x5addf9`) and their AltiVec
  counterparts (`0x5adcd6`, `0x5adcea`).
- RC1 counts integer-to-floating conversions while expanding TOC references.
  With hardware floating point and at least four conversions,
  `setup_floating_conversion` (`0x5acfb0`) allocates two stack slots and
  writes `0x43300000` to the upper word of each. Signed and unsigned
  conversions share the slots and alternate between them. GC 2.7 emits
  the upper-word store for each conversion. The three-, four-, and five-cast
  fixtures agree with the corresponding stock compilers at `-O0` and `-O4,p`.
- RC1's `coerce_to_addressable_before` (`0x5ae870`) can insert address setup
  before a spill reload. The native spill path passes the target instruction
  at `0x58fd22`. For global symbols with no displacement, RC1 keeps an address
  register per symbol and block, and leaves the low relocation on the memory
  instruction. When combining the same address with a constant, the lower-half
  helper (`0x5af430`) can reuse a second register. The repeated-load, symbol
  displacement, and absolute-address smoke fixtures match stock code and
  relocations at `-O0` and `-O4,p`.
- The value-numbering call handler (`0x5b5330`) kills written register
  values and invalidates memory aliases. Before register coloring, its table
  sweep (`0x5b6540`) keeps entries classified by `is_location_independent`
  (`0x5a8450`) whose instructions write only virtual registers. The predicate
  recognizes relocatable `addi`, high-adjusted `addis`, and SPE immediate
  splats in addition to the existing cases. At `0x5b34bd` the stock pass has
  replaced the high and low portions of a global address after a call with
  copies of the pre-call values, while the load after the call remains.
  The address, volatile-load and indirect-load smoke controls compare full
  objects against stock RC1.
- The soft-float immediate loader is `load_immediate_SFPE` (`0x5afb90`).
  Native single- and double-precision fixtures both reached its temporary
  GPR allocation at `0x5afbba`.
- A size-optimized loop from −7 to 32,767 reaches
  `insert_load_immediate_before` (`0x5afce0`) before `mtctr`. A loop
  starting at 5 with stride 2 and multiplier 16,385 reaches both
  `insert_load_immediate_after` (`0x5afda0`) and `insert_add_immediate_after`
  (`0x5afc20`). When the immediate needs both words, these helpers allocate a
  GPR for the high word above optimization level 1. The GC 2.7 controls
  match their stock compiler's instructions.
- The native loop-bound decoder at `0x64cc40` recognizes `lis` and `addi`
  when the `addi` base is the register written by `lis`. The native bound
  search at `0x64c8a0` considers definitions inside the preheader as well as
  incoming definitions. The source port reads the reaching definitions at
  the end of the preheader. With `-O4,s`, the 70,001-iteration loop and the
  split signed and unsigned bound fixtures match stock RC1 instruction bytes.
  The 70,001-iteration fixture is compiled separately because putting it after
  another counted loop changes stock RC1's register assignment.
- Stock `IRO_RemoveAllEmptyLoops` (`0x62ea80`) tries removing the empty body
  at `0x62e7b0`, then tests zero- and one-trip counts at `0x62e3a0`. For the
  split-bound zero-trip fixture, only the initializer and return expression
  remain after call site `0x58e078`. The later dead-store pass removes the
  initializer. The source port proves the first test along a unique entry
  path and preserves any side effects preceding it. For a finite empty loop
  with a single induction update, it computes and stores the final value.
  The finite-loop proof requires a representable exit value. Two unsigned
  controls that wrap therefore still differ from stock.
- RC1 runs `IRO_RemoveAllEmptyLoops` by default at `-O3` and `-O4` and exposes
  `#pragma opt_removeemptyloops`. Native `CMach_SetBackEndOptions` loads
  the pragma value from byte `0x35` at `0x4f041f`. The source reader and writer
  use that byte in the IPA function record. The Windows loop matrix improved
  from 58 to 95 matches out of 115, with stock GC 2.7 matching all 115 controls.
  The pragma override matches stock for the zero-trip fixture with `-O0` and
  `-O2` when enabled and with `-O4,s` when disabled. At `-O3` with the pragma
  disabled, the source optimizer still removes this fixture's loop.
- Stock RC1 folds integer comparisons against the type's minimum and maximum
  in `DoTransformations11` (`0x616a50`). At `-O1`, the comparison against
  `0xffffffffu` is present at `0x611d66` and becomes a constant at `0x611d6d`.
  The source uses the recovered `IRO_MaxValueOfIntegerType` (`0x5d5090`) and
  `IRO_MinValueOfIntegerType` (`0x5d5130`) and retains comparisons with volatile
  accesses or observable floating-point effects. This keeps the loop starting
  at `0xffff8000u` with `i <= 0xffffffffu` unconditional even through wraparound.
  The loop now matches the stock instruction bytes at all six tested
  optimization settings from `-O0` through `-O4,s` and `-O4,p`.
- Stock RC1 chains jumps at call site `0x58d72c`, then rebuilds the flow graph
  when `IRO_RemoveLabels` (`0x61f810`) deletes a label. This exposes the two
  assignments in Corruption's `TRKInitializeEndian` to
  `GeneratePossibleCondAss` (`0x620a20`). That helper accepts a join with
  incoming edges from outside the two arms. It checks whether the first
  assignment kills a value the condition reads, and requires a temporary
  for an overlap or for observable effects in the condition. Without
  common-subexpression elimination the branch stays in place when a
  temporary would be necessary. The fixture matches complete stock objects
  at all six optimization settings and with the three tested overrides.
- When the loop has no adjacent fallthrough predecessor, stock RC1
  `CreatePreHeader` (`0x5e6010`) places the new block immediately before the
  first loop block in code order. It branches to the header if the first
  block is elsewhere. This keeps the entry branch in Corruption's `strcmp`
  eligible for inversion around a goto. All previously matching inputs in
  the Corruption corpus still match, and `nubinit.c` now matches at the
  project's `-O4,p` setting.
- At `-O3` and `-O4`, stock RC1 runs `rewrite_memory_references`
  (`0x64f030`) before optimizing loops. Its direct forwarding path replaces
  loads whose reaching stores cover the whole object and agree on how to
  copy the value. The helper at `0x64f940` can rename a store operand's
  single-use producer when both instructions are in the same block and
  no intervening memory access aliases the store. Stock uses this pass to
  avoid reloading a spilled floating-point parameter even when an integer
  read still needs the stack slot. The source now emits the same instruction
  bytes for that case and for controls with calls and partial writes.
  The optimized Corruption `s_frexp.c` and `s_ldexp.c` objects now match.
- RC1 runs `IRO_DoTransformations` after expression regeneration. Its
  ternary rewrite at `0x611f90` folds integer `0`/`1` and `0`/`-1` arms
  through `IRO_LogicalizeExpr` and `IRO_LogicalNotExpr`. The negation path
  preserves short-circuit evaluation and ordered floating-point comparisons.
  For a comparison used as a value, `gen_condition_gpr` at `0x51dd0d`
  emits `GETCRBIT` with a virtual condition-register field.
  `pcode_expand_pseudo_ops` (`0x5a6ed0`) expands it to `mfcr` and `rlwinm`
  after register coloring. Corruption's `fclose` now matches the stock
  return sequence.
- RC1 refreshes expression references at the end of `IRO_RegenerateExpressions`
  (`0x61fd6e`). Conditional assignments to different destinations can insert
  a temporary for a condition with side effects. The refresh marks both reads
  and the defining assignment before the final transformation removes unused
  expressions. The six Echoes inputs that failed in `BuildExpr` now compile
  with common-subexpression elimination enabled.
- Stock RC1 `FactorExpression` (`0x60e380`) replaces repeated integer
  addends such as `x+x+x` with `x*(1+1+1)`. Constant folding and another
  transformation pass then lower powers of two to shifts. `IRO_IsPow2`
  (`0x5d0190`) checks the entire 64-bit constant, which distinguishes
  `0x80000000U` from the sign-extended result of `-2147483647 - 1`.
  `gen_MUL` (`0x528eb0`) and `shift_left_immediate` (`0x520030`) also
  use shifts and additions or subtractions for constants one away from
  a power of two on the performance path. Corruption's `e_fmod.c` now
  matches at `-O4,p`. The general case of factoring mixed sums still differs
  in some configurations.
- RC1 `fp_binary_operator` (`0x51fb20`) evaluates both operands before
  coercing either to a floating-point register if the right operand has
  no call. `fp_multiply_add` (`0x51f750`) follows the same rule when neither
  of its last two operands has a call. Their call paths still coerce each
  operand immediately in the native evaluation order. Corruption's
  `k_cos.c` and `k_sin.c` now match. The instructions and relocations in
  `e_atan2.c` match. Stock `CodeGen_IsSubableExpression` (`0x4647f0`)
  excludes integer-to-integer casts from common subexpression elimination.
  The source previously allocated two unnecessary temporaries for those
  casts in `e_atan2.c`, shifting generated constant names by four IDs.
  The last offset came from allocating a fresh UID for an inline parameter.
  Stock `CIRStream_UnpackCode` restores the parameter's saved UID through
  `0x52ad9c`. The inliner assigns generated names to the parameters it
  materializes at `0x55ab35`, then assigns names to saved locals at `0x55a9a0`.
  Restoring that order and retaining the saved UIDs makes `e_atan2.c` match
  at Corruption's `-O4,p` settings. Source PCH revision 5147 stores these UIDs
  and the reader still accepts revision 5146. The expression inliner also
  skips `ST_NOP` as stock does at `0x55a3b1`, which lets a stock PCH inline
  a body that contains local declarations and emit the same instructions.
- The native loop alignment pass at `0x59c710` requests instruction alignment
  on the size-optimized path (`0x59c7b8` through `0x59c7d8`). For the Gekko
  backend this is four bytes, so the source skips eight-byte loop padding in
  size mode. Eleven of the twelve counted-loop fixtures match stock at `-O4,s`.
  GC 2.7 matches stock across the separate integer-comparison control matrix.
- In stock RC1, `gen_VECTORCONST` (`0x515ea0`) handles both AltiVec splats
  and 64-bit SPE constants. A `vec_perm` mask containing bytes 1 through
  16 exercises the AltiVec VR and GPR writes at `0x51603e` and `0x515f2f`
  in stock and in the source oracle. The SPE path reaches `load_imm_vector`
  (`0x5afef0`) with `-proc e500 -spe_vector` and an initializer such as
  `(__ev64_u32__){0x12345678U, 0x90abcdefU}`. The helper loads the two words
  into GPRs and packs them with `EVMERGELO`. `SPEOpcodes.def` records 272
  instruction names from opcode slots `0x1b8`–`0x2c7` in the
  table at `0x6e3d10`. The mnemonic table starting at `0x6d3a5e` also exposes
  `evnot` as `EVNOR` with a duplicate source. The e500 smoke fixtures
  check instruction bytes, the SPE `machine` overrides, and scaled
  displacement diagnostics. The assembler enables double-precision SPE
  instructions on e500v2 and fused single-precision instructions on Zen.
  Processor selection enables these instruction sets without a vector
  option. `-spe_vector` and `-spe_addl_vector` set a separate backend
  preference from `-vector` and publish `__SPE__` as 1 on SPE targets or 0
  on other targets that accept the switches. Gekko ignores both switches.
  Zen accepts the additional mode. Other processors that accept the
  switches warn and fall back to SPE mode. The native assembly IR records
  `+A` as read/write accumulator 4048 and `+E` as read/write SPEFSCR 512.
  These implicit operands appear in `.irobj` for instructions such as
  `evmhossfa`, `evmhosmi`, and `evmhosmia`.
  Assembly embedded in C also matches stock at `-O2`. For that fixture,
  stock RC1 spills the scalar argument at `-O0` and schedules the scalar
  addition between the SPE load and move at `-O4,p`.
  The eight SPE type descriptors at `0x6c40f8` have size and alignment 8,
  with four halfword lanes, two word or single-precision lanes, or one
  doubleword lane. Native `CMach_GetCustomType` at `0x4f1b20` resolves the
  SPE token range `0x113`–`0x11a` to those descriptors on e500 and
  resolves token `0x115` to `__vec2x32float__` on Gekko. The source parser
  already routes these tokens through this function. The stock compiler
  moves one SPE argument per GPR in r3–r10, with r3 for the
  result. Stack arguments occupy eight bytes on eight-byte boundaries.
  Pointer copies use `EVLDHX`/`EVSTDHX` for halfword lanes,
  `EVLDDX`/`EVSTDDX` for doubleword lanes, and `EVLDWX`/`EVSTDWX` for
  word, float, and opaque types. Native functions preserve nonvolatile r31
  with the doubleword forms when holding an address across a call.
  The source port handles SPE literals, copies, one-GPR arguments and
  results, and eight-byte stack arguments. `gen_VECTORCONST` uses
  `EVSPLATI`, `EVSPLATFI`, or `load_imm_vector`. The source oracle maps
  the four allocations inside `load_imm_vector` to the native counter
  writes at `0x5aff0e`, `0x5aff1e`, `0x5aff4c`, and `0x5affb9`.
  Program IPA uses type tags 56–62 for the seven lane layouts. Stock
  writes the unsigned word tag (58) for the opaque type as well. The
  source compiler replays `.irobj` files from both compilers, and stock
  links files from both, in the SPE literal and assembly IPA smoke cases.
  The same type tags let the stock PCH reader import saved SPE inline
  bodies. PCH fixtures also cover typedefs, prototypes, and static data.
  Scalar SPE floating point uses `-fp efpu` (alias `spfp`) or `-fp dpfp`.
  In stock RC1, `-proc e500 -fp efpu` lowers scalar float addition
  to `EFSADD` in GPRs and uses a runtime helper for double addition.
  `-proc e500v2 -fp dpfp` lowers double addition to `EFDADD` after
  combining r3/r4 and r5/r6 with `EVMERGELO`, then splits the result
  back into r3/r4. Both modes enable SPE automatically. The source
  uses the existing software-float calling convention, selects EFS/EFD
  operations within `SFPE_PPC_EABI.c`, and uses indexed SPE loads and
  stores for DPFP memory operands. It also mirrors the Zen fused forms
  selected by `-spe_addl_vector`. Program IPA preserves the two pragma
  switches at bytes `0x9a` and `0x9b` of the function options record.
  DPFP now assigns one packed GPR to local doubles and uses EABI register
  pairs at call and return boundaries. `_move_assigned_argument` selects
  the pack path near `0x46c418`. The call path packs the return value with
  `EVMERGELO` at `0x5fcee1`, with the register allocation at `0x5fcebd`.
  `SFPE_gen_TYPCON` also packs the `float`-to-`double` helper's returned
  pair at `0x592d57` after allocating one GPR at `0x592d32`.
  SPFP double fallback calls still differ in tail-call and frame emission.
  Stock `init_stack_globals` at `0x5a6918` starts with eight-byte GPR save
  slots in DPFP mode and four-byte slots otherwise. The source follows
  `compute_frame_sizes` at `0x5a59a0` for ordinary SPE functions and uses
  eight-byte slots when an instruction touches the upper GPR lane or
  DPFP is active. SPE targets use 16-byte stack alignment. The frame smoke
  cases compare exact code for ordinary calls, DPFP calls, and assembly
  that modifies an upper lane.
- RC1's interrupt frame sizing at `0x59ff30` scans physical PCode writes in
  leaf handlers. Calling handlers save the volatile GPRs, CTR, XER, CR, and LR.
  Both stock and source GC 2.7 keep the fixed special-register area in the leaf
  control. RC1 recognizes `save_spe` in both `#pragma interrupt` and
  `__declspec(interrupt)` and sets flag `0x1000` at `0x468094` and `0x469435`.
  Frame sizing at `0x5a5a86` uses that flag to select eight-byte GPR slots on SPE
  processors. The prologue at `0x5a0da0` enables SPE when it needs r0 as a
  temporary, and the spill and reload paths use displacement or indexed SPE
  instructions. Critical, machine-check, and debug handlers use their own
  saved-return registers and return instruction, as selected at `0x5a05b6`.
  `GetInterruptRoutineFlags` at `0x4c37e0` reads flags directly from the function
  object. The program IPA function node carries those flags after the definition
  and reference records. The function options byte `0xb7` controls 16-byte stack
  alignment in the stock IPA backend via `CMach_SetBackEndOptions` at `0x4f08a1`.
  The Gekko and e500 `__alloca`, large-local, and explicitly realigned interrupt
  fixtures match stock. The nonvolatile GPR path selects wide instructions
  according to vector use at `0x5a4060` and `0x5a4380`, independently of the
  interrupt save slot width. Large local offsets round the linkage area to the
  frame alignment at `0x5a273c`. The RC1 move peephole at `0x65b140` leaves
  side-effecting loads and moves intact, including the aligned-leaf stack restore.
- The forward peephole at `0x5b7d88` and `0x5b857d` finds a reaching
  definition for the other operand and checks that its inputs survive until the
  OR. It can copy the definition into the OR before inserting the second field
  at `0x5b7e3b` and `0x5b8630`. A full-mask rotation requires complementary
  masks from the same input with the same shift.
  The stock global optimizer at `0x5b342d` runs the backward peephole before
  value numbering. Its dead-instruction sweep uses all register classes, so
  both original masks disappear while the copied mask and insert survive.
  `CodeGen_Generator` keeps the argument moves and body in the same block
  after `0x46a7df`, including when scheduling is enabled. The original also
  recalculates liveness after deletions at `0x46b180` and `0x46b19d` until both
  passes around register coloring stabilize. The source reproduces these
  sweeps. The remaining early backward patterns are installed at `0x654d37`.
- The forward pass at `0x5b9133` calls the immediate reuse helper at
  `0x5b9840` for `LI` and `LIS`. It redirects read-only uses of a duplicate
  immediate load to the earlier register within the block. Volatile,
  side-effecting, and argument-initialization instructions stop the scan,
  as do writes to the registers carrying the shared value. This lets the
  backward pass discard the later load after folding a bitfield clear.
  Corruption's `file_io.c` now matches stock at its project settings.
- The `gen_AND` paths at `0x526920` and `0x527060` recognize a contiguous
  mask in each 16-bit half. They emit two masked rotates and an OR with
  peephole optimization enabled, or a masked rotate and an insert otherwise.
- `CClass_AccessMember` at `0x551c11` passes the declared bit-field type
  through to the access expression. This preserves the allocation-unit width
  for loads and read-modify-write stores and lets repeated reads of different
  fields in the same word reuse a load. GC 2.7 narrows these accesses.
- `IRO_OptimizeSwitches` at `0x61cbf0` groups adjacent switch values with
  the same destination and lowers switches that need fewer than eight
  comparisons. The emitter at `0x61e6b0` tests wider ranges first and uses
  source order for equally wide ranges. It evaluates selectors with side
  effects once. Stock runs `IRO_SimplifyBoundedIntervalConditionals` at
  `0x62b860` before propagation and again after switch lowering. Its
  matcher at `0x62bef0` looks for consecutive bounds on one variable
  with a shared failure path.
  The rewrite at `0x62b8e0` adjusts strict bounds and emits the unsigned
  comparison `(x - lower) <= (upper - lower)`. The pragma
  `opt_optimizecontrolflow` enables the pass by default at `-O1` and above.
  Stock saves the flag at offset `0x5a` in the IPA function options.
  Corruption's optimized MetroTRK `mainloop.c` now matches all compared ELF
  sections, including the single range check for event types 3 and 4.
- The scope marker path in `LinearizeExpr` at `0x5daba9` checks the
  enabled optimizer passes before emitting block and local declaration
  markers. With those passes enabled, a case label can fall through a
  removed local initialization without a marker obstructing jump cleanup.
  `IRO_RemoveRedundantJumps` at `0x61f870` preserves loop-ending gotos
  and rebuilds the flowgraph after changing a transfer. The default
  options at `0x58d30f` enable dead assignment elimination at `-O2`.
- RC1's equality-chain pass at `0x62c0e0` follows fallthrough blocks and
  checks each successor against the original branch destination. It can
  end the chain at an inverted branch whose fallthrough is that
  destination. The comparison matcher at `0x5ce460` also accepts a variable
  read through a value-preserving integer conversion. The range test at
  `0x62c330` requires every value between the extrema to appear and
  rejects an upper bound whose increment wraps. The rewrite at
  `0x62c150` retains the last branch and compares the offset from the
  lower bound after conversion to unsigned. `IRO_NewIntConst` at
  `0x5cb480` truncates the offset to the original operand width.
  Calls at `0x58ddf8` and `0x58de50` run both range simplifiers before
  copy and expression propagation on each optimizer pass. In the
  second pass of stock `_fseek`, the bounded interval rewrite rebuilds the
  flowgraph. Expression propagation sees the new block and copies the
  buffer-length subtraction into its local use. The following CSE
  pass assigns new temporaries to the field loads and subtraction in
  stock order. Corruption's `FILE_POS.c` object now matches byte for
  byte at `-O2`, `-O3` and the project's default `-O4,p`.
- The `RLWIMI` peephole at `0x6586c0` uses a reaching `LI` to compute
  the rotated bits under the insert mask. A zero insert uses the
  complemented mask with `RLWINM` or becomes `LI 0` if it overwrites
  the whole register. An all-ones insert confined to one halfword can
  use `ORI` or `ORIS` when the instruction is nonvolatile, has no side
  effects, and leaves the condition register unchanged. Both stock
  pattern tables install the rule. A debugger trace of a three-bit
  field clear reaches it before value numbering.
- When optimizing with peephole enabled, `move_assigned_argument` calls
  `extendgpr` at `0x46ca39` for an unsigned byte or a halfword argument
  assigned to a register. It marks the new instruction as an argument
  initializer. The late forward pass can replace that extension with a
  move from the incoming register, at `0x5b8bba` for zero extensions and
  `0x5b908f` for signed halfwords. The forward pass can also remove a
  mask directly at `0x5b805a` if preceding definitions prove it redundant.
  The optimized Corruption `wstring.c` object and the unsigned-short
  bounds test with `opt_optimizecontrolflow off` now match stock.
- When `gen_condition_gpr` lowers integer equality to `SUBF`, a right
  integer constant of at most four bytes becomes the subtrahend at
  `0x51e127`. Other operands take the reverse-order emission at
  `0x51e193`. Size-optimized inequality uses the same ordering at
  `0x51e326` and `0x51e39f`. An unsigned halfword equal to `0xffff`
  uses `XORI` at `0x51d566`.
- At `0x529a6d` and `0x529aaa`, `gen_MONMIN` checks both comparison
  operands for 64-bit integer types before choosing the single-register
  negated-compare path. A wide comparison uses `I8_gen_condition` and
  negates the resulting boolean, so the high words participate.
- `CABI_TransDestructor` at `0x562ed0` scans the destructor body with
  `0x566880` before adding the destructor calls for members and bases. The
  scan uses `CExpr_HasFuncCall` for expressions and keeps the vtable writes
  for call, assembly, and catch statements. Empty bodies, assignments, and
  generated member and base destruction alone do not need those writes.
  The smoke test compares vtable relocations at `-O0` and destructor code
  and relocations at `-O4,p`, covering calls with and without `this`.
- RC1 builds destructor unwind actions at `0x563b70`. The destructor
  body starts with actions for the full subobject list. A generated
  destructor call supplies its current base or member as the boundary
  for the scan. Stock skips through that entry in declaration order, so
  the emitted actions can reference a member destroyed by an earlier
  call. The source preserves this order, including for arrays and
  virtual bases. The smoke test compares `extab` and its relocations
  for both ordinary and virtual inheritance.
  `CExcept_RegisterLocalObject` at `0x4a2b70` adds a temporary action for
  an initialized or non-POD automatic C++ object. The constructor call
  precedes destructor registration at `0x5489e3` and `0x5489f8`. RC1
  removes the temporary actions at `0x49fac7` after the destructor body
  has used their stack transitions to attach subobject actions.
  The jump check at `0x50f160` reports the destination's actions in stack
  order, including initializer-only entries and any shared tail if the
  destination starts outside the source scope. It reports the name of
  the skipped declaration under warning 10211 and entering a handler
  under warning 10577. The source reference points at the branch token
  or the macro invocation that produced it. The smoke test compares the
  warnings, token ranges, and source excerpts for both LF and CRLF.
  `CError_ANSIErrorWarning` at `0x45b590` makes these diagnostics errors
  under `ANSI_strict`, even when warnings are disabled. The unsupported
  pragma diagnostic stays a warning in that mode.
  The unwind-variable walk at `0x5d35b0` keeps member and base pointers,
  condition flags, and delete operands eligible for registers. Destructible
  local objects and exception bookkeeping storage require an address.
  `IRO_FindAllVars` records these operands, and `IRO_UpdateVars` updates
  existing records. The nested destructor object at `-O4,p` now matches
  stock, including the generated table names. With `-O4,s`, stock also
  materializes constructor results during the inlineability analysis at
  `0x601dc3`, before the optimizer's variable walk.
  `call_function` at `0x5fcc9b` checks whether a known callee can throw
  and suppresses exception recording for that call after its arguments
  have been evaluated. `CExcept_CanThrowException` at `0x49f600` checks
  the function's `FUNC_NOTHROW` flag for direct calls. An external
  C++ declaration with `throw()` still produces a cleanup range if
  that flag is clear. Runtime setup marks both built-in delete operators
  non-throwing at `0x458c0e` and `0x458c5c`. The internal `optEH2` setting can cache a
  non-throwing result for unmangled functions outside the runtime allowlist.
  The Wii pragma table exposes `optEH` and reports unsupported names at
  the pragma identifier with warning 10186. The nested-local fixture
  compares all 13 stock unwind relocations and the ranges that select them.
- `CDecl_TypedefDeclarator` at `0x536fa0` reports diagnostic 10122 at
  the declarator's final token when an equivalent typedef is repeated.
  The diagnostic is enabled in C with `cpp_extensions` off and in C++
  when the declaration is in a class scope. Stock also checks that the
  previous and new types and qualifiers agree before applying this rule.
  `ObjGen_Setup` enables message numbers for Gekko at `0x4c7ce2`.
  `Msg_EmitCoreV` checks that setting at `0x45a841` before prefixing
  the diagnostic text. The `750` and `e500` targets leave that setting off.
  The format helper retries when the required length exceeds its buffer
  at `0x45af09`, leaving a single byte clipped at exactly 256 bytes.
- `CExpr_New_DELETE_Node` reports warning 10480 after completing a void
  pointee type at `0x5c43fd`. This precedes the scalar and array branches.
  The warning applies to qualified void pointers and retains warning
  severity under `ANSI_strict`.
  Native template instance creation at `0x5f4e2b`, inline member scheduling
  at `0x47ef83`, and member references at `0x47daff` capture the caller
  before replaying the definition. `CError_ErrorMessage` calls the related
  diagnostic walker at `0x45e9a0`. The walker reports 10544 for functions
  and 10543 for classes, from the innermost call site through the original
  point of instantiation. The driver can hide warning 10480 while keeping
  those notes. The source keeps the captured contexts for the current
  translation unit without extending the PCH records for pending actions.
- `CMid_ObjectAddrRef` queues a function template action at `0x47dcc9`.
  `CMid_GlobalDeclarationActions` drains it at the declaration boundary
  through `0x47d800`, which calls `CTempl_GenFuncInstance` at `0x47d8d3`.
  The direct reference walk queues repeated references. The IPA walk
  visits each object once. In a caller that uses `A`, `B`, then `A`, this
  makes the direct instantiation order `A`, `B` and the IPA order `B`, `A`.
  The instantiation note for `A` still points at its first use. Direct
  code generation emits the caller before the newly generated non-inline
  instances, then emits those instances in reverse materialization order.
  A forward-declared template can be scheduled again after a definition
  becomes available. Out-of-line class members can still be generated by
  the end-of-file pass.
  Under IPA, an unused inline definition does not schedule the templates
  referenced by its body. A live reference to that inline function scans
  the saved body and registers the referenced inline function at
  `0x47da51`, after the caller enters the call graph at `0x47e839`.
  In direct mode, a free non-inline template referenced by a saved inline
  body is queued when code generation visits the body.
  An ordinary static definition can instantiate a template before IPA
  removes both definitions from the object.
- `CRTTI_StaticCast` uses explicit reference conversion for the same type
  at `0x5ff197` and for related classes at `0x5ff122`. The explicit
  reference branch of `CExpr_Convert` calls `CExpr_IsLValue` and emits
  warning 10142 at `0x5f80fc` when the operand is an rvalue. Stock
  suppresses a second error in the same expression when `-w error`
  promotes the warning and the conversion also rejects the operand.
- Reference initialization at `0x543c70` tries direct binding first.
  The class rvalue check at `0x543ea0` follows the right side of comma
  expressions and inspects the call or temporary behind an indirection.
  A cast of a register-returned class can require a copy out of the
  temporary made by the cast. When a constructor or class-returning
  function already takes a temporary destination, class initialization
  at `0x5c8800` can retarget that call to the reference's local storage.
  At `-O3` and above, the memory-reference pass at `0x64f030` can remove
  the store into the cast's original temporary after forwarding its
  only read. The deletion at `0x64f708` follows checks for aliasing and
  remaining uses. When the final block is the epilogue, the local-store
  path at `0x64f779` accepts worst-case aliasing if no use is reachable from
  the store. At `0x64fb60` it checks instruction order and reaching
  definitions, so an earlier call can count as an indirect use of the
  object without keeping the later store live.
  Argument setup at `0x5fe975` sets the extension flag
  for narrow integer arguments, including prototyped destructor calls.
- Static reference initialization records relocatable non-class lvalue
  addresses at `0x542640` and appends runtime assignments at `0x542779`.
  Class temporaries use static storage and register their destructor before
  assigning the reference. Function-scope initialization uses the guard
  object created at `0x567e90` and named at `0x56a180`. A class temporary
  owns the guard when its construction is the first runtime expression.
  The translation-unit initializer at `0x595840` starts its filename
  suffix at the final backslash in the input path and replaces periods
  with underscores.
- A function-scope static in an implicitly weak function or an inline
  function without the `static` storage class gets a weak link name at
  `0x50d826`. The
  mangler at `0x56a2a0` concatenates `@LOCAL@`, the enclosing function's
  mangled name, and the original variable name separated by an at sign.
  The second variable and subsequent variables append `@0`, `@1`, and
  so on, regardless of whether the variable names repeat. The frontend
  keeps the source identifier. Hidden function-name strings keep private
  numbered symbols and use the ordinary unique ID counter.
- The narrow and wide literal branches at `0x53e16d` and `0x53e1d3`
  assign a per-function sequence in externally linked inline and template
  bodies. Folded expressions can leave gaps in that sequence. The body
  walk at `0x55b2c0` runs after simple optimization, before inline packing.
  For each surviving literal, `0x541ae0` allocates a numbered weak array
  and `0x56a060` gives it an `@STRING@` name based on the enclosing function.
  Subsequent literals append `@0`, `@1`, and so on. Distinct occurrences
  keep their own arrays even when their contents are equal. Referenced
  arrays are emitted in reverse definition order after ordinary data.
  The IR writer at `0x52cc45` packs the sequence after the literal bytes.
  A stock `.irobj` puts the referenced arrays at the end of its graph in
  emission order. During replay each input retains its own data order.
  The raw PCH payload stores the expression sequence at offset `+8`.
- RC1's `CodeGen_InitBackEndOptions` at `0x44be50` overrides the frontend
  string-pooling option for read-only literals. The branch at `0x44c412`
  checks the backend read-only preference at `+0x03` and uses the inverse
  of its byte at `+0x2e` as the pooling flag. The resulting setting also
  enables floating-point pooling unless the backend byte at `+0x2f` is
  set. A later `#pragma pool_strings` changes only the string flag.
- The float rewrite at `0x596c09` uses a separate pool when data pooling
  is disabled and the driver enabled floating-point pooling. The lookup
  at `0x599c90` compares target-endian bytes and appends new values to
  `@floatBase0` or `@doubleBase0`. The first value chooses the small
  read-only section when its size is strictly below the threshold.
  The C initializer path checks the active IPA mode at `0x541a28`.
  When IPA collects definitions, literal addresses target individual data
  objects. Ordinary backend string rewrites can still use the string pool.
- RC1 tests a class's aggregate flag at `0x5554a0`. The class trait pass
  clears that decision for bases, virtual functions, a destructor, a
  user-declared constructor or nonpublic data members. Outside an array,
  the initializer paths at `0x542df0` and `0x544fe0` accept compatible
  class expressions directly, including in a single braced initializer.
  An empty list for a non-aggregate class records an empty runtime
  initializer even when the class has no constructor.
- Non-aggregate class arrays take the separate path at `0x546fa0`. It
  initializes each supplied element directly via `0x548910`. A local
  array does not copy a zero-filled aggregate first. For a function-scope
  static array, the guard is emitted before the array. Nested braces that
  attempt to initialize the class's members still report error 10174 at
  the offending opening brace.
- RC1's `CParser_ParseDeclaration` calls `CMid_GlobalDeclarationActions`
  (`0x47c120`) at the common exit at `0x44f43e`, including after the
  template, namespace, using and linkage branches. This runs deferred
  inline bodies before parsing the next declaration. If consecutive
  explicit class specializations share a queue, their bodies are parsed
  in reverse class order and consume unique IDs in a different order.
  The two-specialization smoke case compares complete objects, including
  the generated data names, with file IPA enabled and disabled.
- All function-scope dynamic initializers share the guard path at
  `0x548ce7`. Static scalar and aggregate data is emitted before
  appending its runtime assignment. Class initialization appends the
  constructor and destructor registration separately. A materialized
  reference temporary inherits the owner's qualifiers, including weak
  linkage.
- The inline dispatcher at `0x559f50` sends expression-capable bodies
  to `0x55a1d0`, where it combines the expanded expressions with commas.
  Calls from an expression statement use the same path. Statement-only
  bodies use `0x5592e0` with a continuation label. For a non-void
  statement-only call, the branch at `0x55a116` creates a result temporary
  even if the caller discards the value.
- Value initialization of a POD class without a constructor builds a
  temporary cleared by `__clear` at `0x56fe2d`. The native intrinsic
  dispatcher at `0x5801b0` recognizes `__clear` and `__copy` by object.
  `clear_block` at `0x5bb580` selects individual stores for 1, 2, and 4
  bytes, a store sequence through 16 bytes (64 outside size optimization),
  or a counted loop. For an empty class, local initialization evaluates the
  initializer without copying it into the destination (`0x543770`).
- The global aggregate initializer at `0x54a6f0` discards an all-zero
  data buffer with no relocations unless `explicit_zero_data` is enabled.
  Runtime initialization is independent of this choice. The data emitter
  selects BSS from the null buffer and still emits the initializer code.
  The default small constant section and its BSS partner are read-only
  in `InitElfSections` (`0x4c6153` supplies `SHF_ALLOC`).
  `ObjGen_GetAddressing` at `0x4c6cb0` uses `is_safe_const` at
  `0x5c05a0` when an object has no section yet. A reference can pick a
  const section before a runtime initializer requires write access.
  `PPC_EABI_SetSection` overrides the const classification for
  `__vt__`, `__RTTI__`, `__FUNCTION__`, and the `@STRING@` link-name
  prefix (`0x4c5a4f` through `0x4c5bcd`).
  The permission check at `0x4c5510` emits 33058 and adds the requested
  permissions and ELF flags to the section already selected.
  With `-O4,p -ipa file`, combining `Value empty = Value();` and
  `Value runtime = {source()};` for a one-member aggregate still diverges
  in exception-table symbol numbers. Stock delinearization of the non-void
  comma expression at `0x5dc800` takes the branch through
  `create_temp_object` at `0x50ff40`.
- `CExpr_DirectBindToReference` at `0x5f9330` resolves overloaded functions
  against the referenced function type and diagnoses invalid non-const
  bindings before constructing a recovery temporary. Global non-class
  references try this binding during relocation analysis at `0x542673`,
  then try again for runtime initialization if a relocation is unavailable.
- The `AND` case of constant propagation (`0x6462f1`) follows `LI` and
  `LIS` definitions. Contiguous masks become `RLWINM` with a zero shift.
  Disjoint masks use `ANDI` or `ANDIS` and define CR0. The stock formatter
  (`0x5a8716`) and copy helper (`0x59b848`) reserve six operand slots so a
  later compare fold can add a CR0 definition to the five-operand `RLWINM`.
  The left-hand disjoint `LIS` branch at `0x64683c` reads a stale register
  when storing the immediate. Repeated stock builds of `x ? 0x50000u : 0u`
  emit different masks. The source port uses the reaching `LIS` immediate.
- The constant propagation pass at `0x646890` folds XOR inputs from
  `LI` and `LIS`. Comparisons with zero, one, and
  `0x7abc000000000000` reach the move, `XORI`, and `XORIS` rewrites.
- `machinespecific_isCSEop` at `0x5a6ca0` uses the code-generation
  `first_temporary_register` and `last_temporary_register` bounds
  for immediate `LI` and `LIS` candidates in aggressive value numbering.
  Relocatable `LIS` operands bypass the mode and register tests. In the
  inlined comparisons in Echoes' `CloseEnough.cpp`, the zero loads target
  three registers below the first code-generation temporary, so stock
  keeps all three loads.
- Wii's `CFunc_ErrorCheck` at `0x50ec90` diagnoses missing return
  values before destructor cleanup and inlining. It checks whether the
  final statement is reachable using the statement and exception edges
  at `0x50efd0`. A bare return before the end gets an additional check at
  the closing brace. The `warn_missingreturn_as_error` pragma updates
  both missing-return options, including when it resets them (`0x444040`).
- The driver option at `0x688fa9` sets the C99 preference byte without
  changing whether a translation unit uses C or C++. The `c99` and
  `c9x` pragmas update the same compiler option and can restore its
  command-line setting.
- The preprocessor starts with file and line markers disabled. The
  directive callbacks at `0x4e7230` and `0x4e72c0` retain pragmas as text
  and macro directives as comments when enabled. The file marker writer
  at `0x4e7720` uses blank lines for gaps below eight lines and reissues
  `#line` for larger gaps or backward jumps. Command-line definitions are
  one level deeper than the preprocessor panel.
- The uninitialized-variable diagnostic at `0x5d2926` uses the local
  object's declaration token. File IPA preserves the declaration location
  when it reconstructs the optimized function body, even with debug
  information disabled.
- The `RLWINM` branch at `0x6457b1` rotates and masks the sign-extended
  immediate from a reaching `LI`. It rewrites the instruction when the
  result fits a single `LI` or `LIS`. This also folds the packing shift in
  returns of small zero-initialized aggregates.
  When both XOR inputs come from eligible `LIS` definitions, stock
  combines them at `0x6477d7`, then continues the left-immediate rewrite
  at `0x646bf1` with the shortened operand list. The source preserves
  the resulting code, including the `xoris r3,r0,0x1234` seen after an
  opaque call in the two-constant regression.
- Function-level assembly disables peephole optimization and sets the
  optimization level to one around `assemblefunction` at `0x4bff7c`.
  This bypasses late branch optimization, preserving explicit branches
  to the next instruction in Corruption's `TRKRestoreExtended1Block`.
  The optimized `mpc_7xx_603e.c` object now matches stock.
- RC1's class-specific liveness pass calls `0x63a9e0` at `0x63a491` before
  building the interference graph. It restricts entry live-ins to physical
  registers and walks blocks forward, masking live-outs by live-ins and local
  definitions. A debugger trace confirms that at a join, the first predecessor
  link supplies its source block's live-out and later links supply the
  destination block's live-out. In `fwide` this removes a false interference
  edge, so the result coalesces into r4 and is moved to r3 at the shared return.
- The graph builder at `0x638510` rebuilds class-specific liveness and
  interference until the coalescer at `0x638a50` stops merging registers. It
  retains the register equivalences across passes. The coalescer can merge
  virtual registers outside the frontend temporary range. It keeps the lower
  register number and skips the merge if any call pins the higher register.
  In the size build of `FILE_POS.c`, the buffer pointer and loop pointer in
  the out-of-line `_ftell` have no interference edge and merge into one GPR.
- The target's return-register setup at `0x5910b0` always adds the stack
  pointer before adding any return-value registers. The class-specific pass
  at `0x63a4a4` marks these registers live at the final block. In `qsort`,
  the stack pointer edges raise the degrees of the two heap index registers
  enough to delay pushing them onto the coloring stack.
- Before marking strength-reduction candidates, `0x667200` splits a product
  of an invariant factor and an induction variable plus or minus an integer
  offset. The helper at `0x6673f0` builds two products and preserves the
  offset expression's addition or subtraction. The candidate scan can
  reduce the induction product and hoist an invariant offset product.
  `0x667120` restores the old tree when neither product is marked. In
  Corruption's `qsort.c`, this turns `member_size * (l - 1)` into a
  loop-carried product and a fixed offset. The default and `-O3` objects
  match stock.
- Local declarations at `0x50dc7c` and `0x50d976` mark the enclosing
  declaration block for automatic and static variables, respectively.
  Restoring a nested marked block at `0x510c40` inserts an `ESCOPE` start
  marker before its statements and an end marker after them. Both carry
  the keep flag. The optimized IR omits the markers. When the markers
  reach `expandTOCreferences` at `0x595c00`, their flags select a dynamic
  frame with r31 as the frame pointer. The instruction selector for
  `ESCOPE` at `0x52a770` emits no instructions. The unoptimized build of
  `qsort.c` and its build with the global optimizer disabled match stock.
- RC1 allocates a virtual condition field in each scalar comparison selector.
  In `e_sqrt.c`, value numbering at `0x5b4c01` copies the field from the
  first signed comparison with zero, so two branches can read it across an
  intervening comparison. The interference builder at `0x638da0` starts
  with the real register count for the current class.
- The backward peephole at `0x659f80` folds an arithmetic right shift into
  a masked rotate when the mask excludes all sign-fill bits. It bypasses the
  shift's GPR result and removes the shift when its value is dead.
  Stock folds the shift in `e_sqrt.c` before coloring, while the two GPR
  operands still differ.
- The RC1 while parser at `0x50a7e0` allocates a separate entry-test label
  at `0x50aee4` for expression conditions. The label follows the continue
  target at the end of the body. The three while loops in `e_sqrt.c` account
  for the shift from `@194` to the stock floating constant name `@197`.
  The optimized `e_sqrt.c` object now matches stock.
- RC1's `make_distinct_virtual_alias` at `0x5bfab0` allocates a named
  virtual alias even for ordinary writable pointer parameters. In
  `TRKDispatchMessage` it allocates `@87` before `create_switch_table`
  allocates the emitted `@88`. The virtual object records the pointee type
  and starts at offset `0x07ffff00`. Ordinary writable pointer aliases join
  the conservative pointer set. `0x5bec90` resolves nested membership and
  records the enclosing sets on virtual objects. When an instruction writes
  through a virtual object, `0x5be660` invalidates values in those sets,
  including aliases reached through other pointers. The source now matches
  `s_modf.c` and retains the `mem_funcs.c` and `__init_cpp_exceptions.cpp`
  matches. RC1's pointer propagation at `0x6655a0` still has target-specific
  paths to recover.
- The forward store peephole at `0x5b9bd0` looks for later accesses to the
  same address. It replaces eligible loads with copies of the stored value
  and removes an earlier store if the value is overwritten without an
  intervening observation. In `__prep_buffer`, both stores to offset 40
  reach the global optimizer at `0x5b33f0`. Stock deletes the earlier store
  at `0x5ba1a4` before local value numbering. The source `buffer_io.c`
  object now matches stock.
- RC1's aggressive copy propagation checks each use at `0x644630`,
  substitutes the source register when it still reaches that use, and
  tracks rejected uses for the removal callback at `0x6445f0`. Static zero
  initialization lets the first candidate numbered zero propagate without
  immediately removing its move. The backward move rule at `0x65b140`
  can also retarget an earlier definition before value numbering. In
  `strcpy` it folds two moves into one, which lets copy propagation and
  register allocation eliminate the remaining move. The source `string.c`
  object now matches stock.
- The branch peephole at `0x65e100` follows a zero comparison through the
  condition field to the integer instruction that produced its operand.
  Before register coloring (caller `0x46b40a`), it can switch the branch
  to CR0 and set the record bit on the producer. The comparison is removed
  or converted to a condition-field copy. In `__mbtowc_noconv`, the call
  at `0x65e4e1` sets the record bit on the sign extension after the byte
  load at offset `0x48`. The source `mbstring.c` object now matches stock.
- RC1's `IRO_CommonSubs` at `0x62eba0` groups expressions with
  `IRO_ExprsSameSemantically` at `0x62f5e0`. `0x62f380` computes availability
  per group using `IRO_GetUExprKills`. The local scan at `0x62f0c0` links
  repetitions inside a block. `0x62ee50` and `0x62ef50` find definitions
  through predecessors. The rewrite at `0x62ec00` can materialize
  a shared expression before the defining statement. After
  `MWEnterCriticalSection` in `CircleBuffer.c`, this puts the read or
  write pointer load ahead of the start-pointer load before either
  scheduling pass. `CircleBuffer.c` matches stock. The source now groups
  expressions before computing availability and assigns one temporary to
  definitions that reach the same use. An unsigned multiply with reversed
  operands in the two branches and a field load in both branches each
  reuse the value at the join. A write on one path prevents reuse there.
  The pass still uses the source's per-expression kill rules to build
  group kills. The rewrite walks each block in statement order. It
  copies a nested temporary assignment ahead of its containing
  statement and replaces the nested assignment with a read of the
  temporary. In `ftell`, this keeps the buffer-pointer definition live
  when the pass replaces the enclosing pointer subtraction with a
  different temporary. The generated `ftell` instructions match stock
  at the Corruption project's default optimization level.
- The RC1 compiler's translation-unit defaults at `0x4426a6` disable
  `stdc_fp_contract` and enable `stdc_fenv_access`. The CSE eligibility
  check uses those options at `0x5cbf70`. Both propagation passes call
  the recursive floating-point effects check at `0x5cbe30` before
  accepting assignments, from `0x60ba35` for expression propagation and
  `0x60bae8` for copy and constant propagation. These rules prevent CSE
  from pulling the local `t` loads out of the floating-point computation
  in `e_pow.c` at line 228 and preserve the stock order of constants in
  `.sdata2`. The function's generated instructions still differ.
- Compound assignment propagation at `0x6084d0` checks the equivalent
  binary operation's CSE eligibility through `0x608bf0`. For an eligible
  operation, the original requires a single use at the same or a shallower
  loop depth. This keeps the two tests of the masked integer in `s_cos.c`,
  `s_sin.c`, and `s_tan.c` on one definition and preserves stock numbering
  of the floating-point constant symbols. All three objects match stock.
- RC1 balances arithmetic trees at `0x5954d0` during TOC expansion above
  optimization level two. It uses the operation weights at `0x5982d0`
  to reassociate multiplication, addition, AND, XOR and OR, and factors
  two addends with the same constant multiplier. Both
  `ppc_dont_balance_ir` and `ppc_dont_balance_exp` disable this pass.
  The stock program IPA options store `ppc_dont_balance_ir` at byte `0xb1`.
  Corruption's optimized `e_log.c` now matches stock.
- In `combine` at `0x5aef2a`, an indexed GPR pair plus a signed 16-bit
  constant becomes a base-plus-displacement operand. The base is the
  sum of the pair. For a full-width offset with both high and low parts,
  the high addition uses a separate virtual register.
- In `combine` at `0x5aec58`, RC1 adds the two registers of the indexed
  operand first, unless expression balancing is enabled and the preceding
  instruction's first operand is one of those registers. In that case it
  computes the sum of the other two registers first. The pragma
  `ppc_dont_balance_exp` controls this choice and is preserved at byte
  `0xbb` in program IPA function options. In `__va_arg.c`, the address
  calculation now matches stock. The backward move rule at `0x65b140`
  also runs before register coloring, where it retargets the earlier of
  the two pointer copies. The optimized `__va_arg.c` object now matches
  stock with the project's flags.
- RC1's `canmergeblocks` at `0x59af00` rejects a predecessor whose last
  instruction is volatile. In `e_acos.c`, that is the paired-single
  register save at the end of the prologue. The final scheduler leaves
  the prologue separate from the floating-point argument spill and the
  first comparison. The optimized `e_acos.c` and `e_log10.c` objects
  now match stock.
- The non-coloring allocator classifies types at `0x590530`. Native
  GPR, FPR, and vector allocation at `0x46d200`, `0x46cf60`, and
  `0x46ccd0` set the local usage cutoff to one tenth of the total use
  count for the class, with a minimum of two. The GPR total also includes
  TOC entries, whose own cutoff is three halves of the local cutoff.
  Arguments use a fixed cutoff of two. At `-O0`, RC1 counts 53 uses for
  `__va_arg.c` and requires five uses for register allocation of a local.
  This places four locals on the stack. `__va_arg.c`, `e_fmod.c`, and
  `s_floor.c` now match stock at `-O0` with the other project flags.
- In `combine` at `0x5aebd9` and `0x5aec14`, RC1 uses a fresh register
  when combining a non-stack displacement with a register. This restores
  the use of `r0` for the relocated address in the `TRK_memcpy` arguments
  in `dolphin_trk.c` at `-O0`. The `e_log10.c` object matches stock at
  `-O0` with this rule.
- Local declarations carry an expression with wire tag `0x42` and scope
  kind 5. The parser emits one before initialization at `0x50dc76`.
  The stream writer at `0x52cdde` packs its operand and then its scope
  kind. The optimizer at `0x5d886b` gives the operand flag `0x400000`,
  allowing it to count toward register allocation without making the
  local address-taken. In `__TRK_copy_vectors`, the four declaration
  references bring the GPR total to 43 and the cutoff to four. The
  inlined destination pointer still has three uses and stays on the
  stack. Corruption's `dolphin_trk.c`, `dispatch.c`, `file_io.c`,
  `string.c`, `wstring.c`, and `s_ldexp.c` now match stock at `-O0`.
  Scope markers currently contribute to allocation and survive the
  program IPA stream. Their debug information emission remains to be
  recovered.
- `CMach_StructLayoutPackedBitfield` at `0x4f2117` closes the previous
  allocation unit at the first unused byte when it encounters a zero-width
  field, then aligns the offset for that field. This affects both the next
  member's offset and the enclosing type's size.
- The RC1 `compare_immediate` path at `0x51b321` uses `cmpwi` for unsigned
  equality tests against zero and rewrites unsigned `> 0` and `<= 0` as
  inequality and equality. It emits a comparison before later peephole passes.
  GC 2.7 retains the earlier unsigned comparison and record-bit selection.
- The inline assembler's mnemonic lookup synthesizes `mf<spr>` and `mt<spr>`
  at `0x584467` after searching the fixed mnemonic table. The aliases use the
  named special register's operand format and encoding, including Gekko's GQRs.
- RC1 counts calls during TOC expansion and checks the epilogue's predecessors
  at `0x46b5f0`. If all calls are in tail position and frame sizing confirms no
  frame is needed, the pass at `0x46b520` converts direct and indirect calls to
  branches and drops the epilogue return when every path ends in a tail call.
  Command-line optimization levels 2 through 4 enable the pass. Function IPA
  options store the per-function setting at byte `0x67`.
- `PPC_EABI_SetSection` at `0x4c5880` resolves declaration section IDs
  directly from the section records. It leaves ELF symbol creation to the
  definition or relocation that needs it. `get_current_pseudo_section_info`
  at `0x4c3810` looks for the five matching sections and both small-data
  thresholds. The IPA writer and reader preserve the thresholds per record.
- RC1 builds objects for named parameters during declaration parsing at
  `0x5105bb`, including in prototypes. Local allocations at `0x45689e`
  and `0x50dacf` consume IDs from the same counter as compiler-generated
  names. Function definitions reuse the objects from the parameter scope.
- The pre-coloring peephole at `0x65c860` checks for two relocations to
  the same object. It uses the low-half destination for both instructions
  when the high-half register is dead after the addition and there are no
  intervening reads of either register or writes to the destination.
- When `mergeblocks` moves instructions out of the epilogue, RC1's
  `0x59aea0` marks each moved instruction volatile. The scheduler preserves
  their order, including the GPR and link-register restores.
- `CParser_ParseDeclSpec` handles `__declspec(align(...))` at `0x4529c3`.
  `CTTool_Alignment2Quals` at `0x558590` encodes powers of two from 1 through
  8192 in qualifier bits 24–27. RC1 stores tentative definitions in object flags
  instead, as checked at `0x5ec7d1`. The native encoding flows through the
  program IPA type and local records and the stock PCH reader. Tests restore
  a 32-byte aligned typedef, 64-byte aligned static data, and a critical SPE
  interrupt declaration from a precompiled header. Source PCH revision 5146
  identifies raw records with the new encoding so older caches can be regenerated.
- RC1's `CPrep_InsertSpecialMacros` and `CodeGen_InsertSpecialMacros` start at
  `0x449CA0` and `0x4654A0`. The driver handles the `Ai` and `Fa` optimization flags in
  `TargetSetOptFlags` at `0x435850`. `CInline_CheckInlineability` at `0x55D210`
  uses the auto-inline statement and local-storage limits in its speed path.
  The statement count at `0x55d302` includes standalone calls, including
  Pascal calls. Stock skips `ESCOPEBEGIN`, `ESCOPEEND`, and `EINFO`, which
  the port represents with `ESCOPE`. The `inline_max_auto_size` handler
  at `0x446bd0` reads the limit after macro expansion.
  With `-O4,s`, the call-site check at `0x55dad0` compares the estimated
  sizes before and after expansion. The fast estimate at `0x55f833`
  applies when the callee costs more than ten units and the call costs
  less than half the amount over ten. For `_ftell`, the callee costs
  76 units, the call expression costs one, and the call overhead costs
  six. This leaves a weak `_ftell` called from both `ftell` and `_fseek`.
  The source applies this fast estimate to calls without numeric literal
  arguments.
  `FILE_POS.c` now matches at `-O4,s`. The estimator that expands a copy
  of smaller callees and applies argument-dependent savings remains to
  be recovered.
- `CMach_GetObjectAlignment`, `CMach_AllocationAlignment`, and `move_block` start at
  `0x4F4AA0`, `0x4F4BB0`, and `0x5BD1F0`. The source smoke test compares an 8-byte
  aggregate copy with the default options, legacy struct alignment, and floating-point moves enabled.
- The 750 model initializes timing at `0x6628E0` using a default record, 222 explicit
  records, and 38 overrides for Gekko. Those 260 explicit records agree with their
  corresponding entries in the GC 2.7 source table.
- The ELF `.comment` header writer at `0x5687F0` emits format revision 15 and compiler
  version 4.0.0.
- RC1 registers `PrecompilerCheckCompressed` (`0x470610`) and
  `PrecompilerExpand` (`0x470120`) as preprocessor callbacks. A stock header
  starts with little-endian magic `0xBEEFFACE` and revision `0x042e`. Its
  `0x50da`-byte header records the raw graph's expanded size, stored size, and
  file offset at `0x32`, `0x36`, and `0x3a`. A tag of `0x00` through `0xdf`
  gives a literal length of tag plus one. A tag of `0xe0` through `0xff`
  gives a zero run of one through 32 bytes. Expansion preserves adjacent
  sections and updates the offsets that follow the raw graph. The source
  transport produces expanded images that stock RC1 reads. The declaration
  reader translates stock records to source types and objects.
- RC1's raw relocation loop at `0x471490` adds the graph base to a
  little-endian pointer word. Its patch stream uses big-endian absolute
  positions and signed seven-bit halfword deltas. The builtin relocations at
  `0x4713b0` instead read a count and builtin index followed by that many raw
  positions, terminated by a zero count. RC1 fills the 82-entry index table
  at `0x47b350`. Indices 19–24 are the imaginary and complex floating types,
  34–45 are vector types, 46–53 are SPE types, and 54–81 are runtime objects.
  Stock builtin slots contain zero before relocation. The declaration reader
  indexes those slots using the patch table and resolves builtin types and
  argument-list sentinels to the compiler's existing instances. The older
  source-format header uses its original builtin order.
- Stock PCH loading invokes `CodeGen_ReadPrecompilerData` at `0x4c0e10` before
  materializing saved static data. The reader reconstructs ELF sections from the
  revision-2 backend block, remaps section attribute indices and negative
  declaration selectors, and restores the five default section roles. The source
  PCH uses four previously reserved words at `0xd8` for the backend block and the
  raw object-section fixup table. The recovered reader can also load a backend
  block emitted by stock RC1.
- The stock macro-table merge at `0x471130` walks `0x800` buckets from header
  offset `0x20da`. A macro record stores its next link and name at `0x00` and
  `0x04`, replacement pointer and byte count at `0x08` and `0x0c`, and argument
  count and names at `0x1c` and `0x20`. The count is one greater than the number
  of parameters for a function-like macro. Name strings start at `0x0a` in hash
  name records. Stock replacements use token encodings. For example, `#x` is
  `01 80 0c` and `x ## y` is `01 80 14 ee 7f 02 c0 14` for the first and second
  parameters. `decodemacrotoken` at `0x4e24a0` reads a little-endian token word.
  Bit 14 supplies the leading-space flag and bit 15 introduces a separate
  flag byte. Flag `0x20` introduces a NUL-terminated token spelling and flag
  `0x04` identifies a parameter reference. The source preprocessor uses a
  separate byte encoding for these operations. Its stock PCH macro reader
  reconstructs that encoding from the stock token stream for `-E`.
  Bit 0 of the macro's flags marks a variadic definition. Bit 1 preserves
  GNU variadic behavior at the definition site, including removal of the
  comma in `, ## __VA_ARGS__` when the argument is empty.
- The stock root namespace uses `0x400` buckets at header offset `0x40da`.
  The first four words of an entry are its next link, hash name, next object
  in the overload list, and first object. This agrees with the source
  `NameSpaceName` layout in the `Point` and `square` fixtures. The objects
  addressed by those entries use stock layouts. An `Object` stores a 32-bit
  section selector at `0x04`, its namespace, hash name and type at `0x08`,
  `0x0c` and `0x10`, and its storage class at `0x18`. `CMangler_GetLinkName`
  (`0x56a3e0`) caches the function link name at `0x44` and the data link name
  at `0x48`. A typedef stores its type and qualifiers at `0x04` and `0x08`.
  Type records have a byte for the kind and a 32-bit size at `0x02`. The function
  type (kind 7) has its argument list at `0x06`, exception specifications at
  `0x0a`, return type at `0x0e`, return typedef at `0x12`, return qualifiers at
  `0x16`, and flags at `0x1a`. Argument records have a separate typedef field
  at `0x14`, followed by storage class at `0x18` and the array flag at `0x1a`.
  Bit `0x40000000` marks C++ linkage, and bit `0x200` marks
  compiler-owned intrinsics. A controlled empty PCH contained 324 intrinsic
  declarations and four default `operator new`/`delete` declarations. The
  source reader keeps the runtime symbols from compiler initialization and
  loads user typedefs and external declarations in saved overload order.
  Pointer and array types use kinds 12 and 13, with their element type at
  `0x06` and qualifiers at `0x0a`. The declaration reader uses the existing
  backend selector map for saved custom sections.
- The stock C aggregate constructor `CDecl_NewStructType` (`0x5577c0`)
  allocates `0x12` bytes with kind 5 and alignment at `0x0e`. A recursive
  `struct Packet` fixture has its name and member chain at `0x06` and `0x0a`.
  Its member records contain next, type and name pointers at `0x00`, `0x04`
  and `0x08`, followed by a typedef pointer before the byte offset at `0x10`
  and qualifiers at `0x14`. Byte `0x10` of the type record distinguishes
  structs from unions. Stock `CDecl_NewBitfieldType` (`0x5574d0`) allocates
  a `0x0c`-byte kind-8 record with its base type at `0x06` and width at `0x0b`.
  The finished layout stores the bit offset at `0x0a`. The reader installs
  the saved byte and bit offsets and reuses in-progress type records when
  a recursive member refers back through a pointer.
- Stock C++ classes use kind 6 and populate the class-list root at header
  offset `0x72`. `CDecl_DefineClass` (`0x530090`) allocates a `0x30`-byte
  record. The namespace and class name are at `0x06` and `0x0a`, followed by
  base, virtual base, and data member lists at `0x0e`, `0x12`, and `0x16`.
  The class namespace records the enclosing namespace at `0x00`, owner
  type at `0x0c`, and name list or table at `0x18`. The class member list and
  namespace refer to the same `ObjMemberVar` objects. Those records place
  the member type, qualifiers, and byte offset at `0x10`, `0x14`, and `0x18`.
  The class namespace also contains compiler-generated method declarations.
  The class-list entries at `0x72` pair a next pointer with a class pointer
  at `0x04`. The reader creates stable type and namespace identities before
  installing the declarations that refer to them. Namespace aliases share
  the original scope. Using-directive lists use `(next, namespace)` records.
  A using-declaration for a function has a `DALIAS` object with its target
  at `0x40`, the access class at `0x44`, and the byte offset at `0x48`.
  A using-declaration for a data member stores the base class and original
  member at `0x1c` and `0x20`. The reader rebuilds the shared frontend's
  base access path from those class references.
- A stock method extends the function type with its owner at `0x22`, vtable
  slot at `0x26`, function ID at `0x2a`, and static flag at `0x2e`.
  A member-pointer record (kind 11) holds its target and class at `0x06` and
  `0x0a`. Its function target uses ordinary `TypeFunc` storage and the
  `FUNC_PTM` calling convention flag. `CClass_IsTrivialCopyClass` (`0x555480`)
  tests bit `0x20000` of the class flags at `0x22`.
- `CABI_AddVTable` (`0x567d70`) allocates the `0x10`-byte stock vtable
  descriptor at class offset `0x1a`. The descriptor stores the vtable
  object at `0x00`, byte size at `0x04`, and object vptr displacement at
  `0x0c`. The reader derives the owner from the object's class namespace
  and restores the deferred emission callback for compiler-owned vtables.
  Base records store byte and vtable displacements at `0x08` and `0x0c`.
  A direct-base record uses bytes `0x10` and `0x11` for access and the
  virtual flag. The virtual-base list uses `0x10` for its override flag.
- The stock `FuncArg` default-expression pointer at `0x08` addresses a
  `0x2e`-byte expression. Its kind and cost occupy the first two bytes,
  followed by the result type at `0x04`, flags at `0x08`, and payload at
  `0x10`. The tested tags agree with the program IPA expression numbering.
  In particular, `0x34` is an integer with the high word first, `0x35` is a
  double, `0x37` is a string, `0x04` dereferences a child expression, and
  `0x3b` holds an object reference. The string stores a byte count and data
  pointer at `0x10` and `0x14`. `CInit_RewriteString` (`0x541a00`) reads the
  Pascal string flag at `0x1c`. A call (`0x39`) stores its function expression,
  argument chain, and type at `0x10`, `0x14`, and `0x18`. A temporary (`0x44`)
  stores its type, ID, and destructor flag at those same offsets. A null
  check (`0x3c`) for object allocation stores the tested and conditional
  expressions followed by a temporary ID, which its `0x3d` child refers
  to. A member-function literal (`0x52`) refers to the method object at
  `0x10` and uses the same source lowering as an imported IPA literal.
  The reader decodes defaults after installing their scopes and declarations
  so that saved references reuse the compiler's existing objects.
- Stock RC1 initializes `cclass_friends` from header offset `0x82`
  at `0x470dc7`. Each record holds its next link, owner type, function
  list and class list at `0x00`, `0x04`, `0x08`, and `0x0c`.
  Both lists have eight-byte records with a next link and the friend object
  or type. The declaration reader reuses the decoded identities and attaches
  both lists through `CDecl_AddFriend`. The control header combines a
  forward-declared friend class, a free function, and a qualified member.
- Stock RC1 initializes `cmid_staticdata` from header offset `0x6a`
  at `0x470d49`. Each entry in that list has a next link at `0x00` and
  an object at `0x04`. The data object points at its definition from
  `0x4c`. At `0x47be2c` and `0x47be3b` the middle layer reads that
  definition's byte buffer, relocation list, byte size, and read-only byte
  at `0x0c`, `0x10`, `0x14`, and `0x18`. A relocation stores next,
  object, offset and addend, in that order. A stock PCH containing
  `static int* link = &external[3];` records the `external` object and
  addend 12. A saved zero-initialized static has a null byte buffer. A
  generated string object can have a saved definition without setting the
  defined flag in its symbol. Stock file IPA emits the saved definitions.
  The ordinary stock command-line fixture leaves the four named statics
  undefined. The source loader passes the saved bytes and relocation
  targets to the same data emitter used by text headers in both modes.
  Source-format PCH files preserve the read-only byte in the high bit of
  the stored byte count, with the record size unchanged.
- With C mode enabled, an uninitialized file-scope `static int counter;`
  appears under the `cmid_tentative` root at header offset `0x7e` instead
  of the static-data root. The root is another eight-byte object list.
  Stock sets bit `0x0200` in the object flags for a tentative definition
  at `0x47e2fc`. The reader attaches the list to the source frontend's
  existing tentative-definition state so the consumer can supply a later
  initializer. A stock control preserves one tentative object in BSS
  while completing two others with data supplied by the consuming source
  file. Source RC1 PCH files preserve this state in their reserved word
  at `0x9c` and omit definitions already completed in the archived header.
- A C++ archive with `static Badge loaded(12);` saves its constructor
  and destructor-registration expressions under header offset `0x76`.
  In the control each 24-byte list record starts with the next link,
  expression pointer, and owning object at `0x00`, `0x04`, and `0x08`.
  The reader restores the expression list in order and uses the regular
  source startup emitter. The stock file IPA output and source outputs
  register a startup function in `.ctors`. The controls exercise ordered
  scalar dependencies followed by construction and an array with a
  separately generated destructor helper.
- Stock installs `cmid_thunklist` from header offset `0x86`.
  `CMid_DefineThunkObject` (`0x47e3d0`) builds an eight-byte list
  entry for each thunk. The thunk object has qualifier bit `0x800000`
  and points from `0x40` to four words holding the target, this
  adjustment, return adjustment, and constructor displacement.
  The multiple-inheritance and covariant-return controls leave this
  root empty. Both stock and source compilation generate the required
  thunks while consuming those headers. Stock also leaves the root empty
  with a saved object or startup expression, explicit export, and both
  file and program IPA options at PCH creation.
- A stock symbol with `Q_INLINE_DATA` stores an integer, enum, or pointer
  constant in high-word, low-word order at `0x40` and `0x44`. Floating
  constants instead store a pointer to the eight-byte value at `0x40`.
  The source loader restores these values for folding and emits storage
  when the program takes their addresses.
- A saved inline function has a function package at object offset
  `0x40`. Native `0x4beb00` reads the inline size at package offset
  `0x08` and the 208-byte program IPA options at `0x0c`. The package's
  first word points to a packed body. The body holds the code pointer,
  byte count, local count, label count, type count, and reference count
  in its first six words. Its eight-byte reference entries start at
  `0x1a` and contain a relocated pointer, kind byte, flags byte, and
  16-bit call count. The exporter converts these pointers to program
  IPA reference records while copying the same packed code bytes.
  The control `inline int saved(int x) { return x * 7; }` has one local,
  three references, and 99 code bytes. The PCH loader resolves the table
  against the saved declarations and passes the code through the shared
  program IPA decoder before building the source inliner's function body.
  Controls with a declared member, a member defined inside its class,
  and a member defined outside the class isolate raw function flag
  `0x800000` to the in-class definition among those controls. Native
  function template definitions also set this marker. The source loader
  strips it before interpreting its own function flags. Changing `-inline`
  leaves the saved native option block unchanged. Source-format PCH files
  preserve the consumer's inlining policy when they carry an imported body.
  The exception fixture also checks the emitted `extab` bytes. The stock
  RC1 catch record clears the two reserved bytes before its first 32-bit
  field. A GC 2.7 control agrees on field positions and relocations and
  has nonzero bytes in the same reserved field.
- The function template root at header offset `0x7a` points to records
  with a parameter list at `0x08`, a root-list link at `0x0e`, a token
  count at `0x16`, and a token pointer at `0x1a`. The function symbol
  points from its usual body slot at `0x40` to the template record, and
  the template record points back to the symbol at `0x2e`. The list link
  agrees with the namespace overload fixture and the native declarations
  at `0x51356b` and `0x5ee96a`. The source reader shares these records
  with the existing template lookup and instantiation paths.
- The stock inline-caller and explicit-specialization controls both
  point from the function template at `0x32` to a saved instance.
  In the 28-byte instance record, `0x00` points to the generic template,
  `0x08` to the arguments, `0x10` to the next instance, and `0x18`
  to the instantiated symbol. The symbol points back at `0x48`.
  Native `CTempl_GenFuncInstance` at `0x5c17d0` uses the same symbol slot
  and sets the instantiated byte at `0x0c`. The explicit specialization
  sets the byte at `0x0d`. The reader reconnects the symbol and arguments
  before decoding packed functions that can refer to that instance.
  A member-function template clone points to the original template
  at `0x00` and to the enclosing class instance's embedded template
  header at `0x04`. Its function instance repeats the enclosing
  instance pointer at `0x04`.
- Stock template parameters put their ID at `0x08` and their type-specific
  payload at `0x0c`. The ID stores a 16-bit index, an 8-bit nesting index,
  and an 8-bit parameter kind. Dependent type kind 10 puts the subtype at
  `0x06` and the payload at `0x08`. The payload holds the parameter ID,
  or a type followed by a hash name for a qualified type or an expression
  for an array bound. A parameter-reference expression uses kind `0x4c`,
  subtype `0` at `0x2c`, and the parameter ID at `0x10`, as confirmed
  by the native argument index reader at `0x5f4330`.
- Deferred body tokens occupy `0x1c` bytes each. The stock keyword table
  at `0x6a00b8` gives the lexical IDs to translate before handing the
  stream to the source parser. Stock literal token subtypes 2, 3, and 4
  identify ordinary, Pascal, and wide strings. Wide tokens hold
  little-endian code units, which the reader converts to the target order.
  The controls compare code and string data from source text, the stock
  PCH, and a source-format PCH rewritten from the stock PCH.
- The stock class-template declaration and definition controls both have
  a class-template root at header offset `0x62`. Native
  `CTemplClass_MakeRealTemplate` at `0x5efda0` links these classes through
  `0x30` and copies their template parameters at `0x3c`. The instance
  list, partial-specialization owner and specialization list are at
  `0x42`, `0x46` and `0x4a`. Native `CTemplClass_NewInstance` at
  `0x5f1450` writes a template pointer at instance offset `0x34`
  and links the instance into the owning template's list. That reference
  points to the class's embedded template header, which native
  `CTemplTool_Template2TemplClass` at `0x551b70` subtracts `0x34` from.
  An uninstantiated instance has zero alignment until the frontend lays
  out its members.
- A stock nested class-template proxy has flags `0x300`, an empty
  namespace under the instantiated outer class, the original nested
  template's embedded header at `0x34`, and the outer instance's
  embedded header at `0x38`. Stock groups nested instances on the
  original template's list. The source reader assigns each instance to
  its own proxy and uses the original template's layout and namespace.
  Controls cover the same inner argument under two outer arguments and
  a third level of nesting.
- A class definition with a dependent data member has an action at
  `0x4e`. Native
  `CTemplClass_AppendTemplateAction` at `0x5f19e0` confirms the action
  list link at `0x00`. Native `CTemplClass_RegisterObjectDef` at
  `0x5f1690` writes the declared object at `0x10` and action kind 6
  at `0x1a` of the 28-byte action. A dependent base action refers to
  the preceding concrete base record, preserving where the instantiated
  base is inserted. Native `CScope_ParseUsingDeclaration` at `0x4f53e0`
  registers a dependent using action and inserts a symbol with storage
  class `0x166`. The source instantiator binds that name through the action.
  For dependent enums, type kind 10 subtype 8 wraps
  the enum type and the saved initializer on each affected enumerator.
  The source reader turns those initializers into actions consumed by
  the existing class instantiator.
- Native `CTemplClass_DefineMember` at `0x5f1330` fills a 48-byte
  deferred member record with an optional parameter list at `0x00`,
  a token count at `0x14` and a token pointer at `0x18`. Stock object
  offset `0x4c` points to that record for an ordinary class-template
  method, and `0x50` does so for a deferred static data definition.
  A member of an already instantiated class uses the
  `ObjectTemplated` layout with its original declaration pointer at
  `0x60`. An independently templated member points from its function
  template record to the enclosing class's embedded template header.
  Source-format rewrites retain method parameter names alongside the
  deferred tokens so the instantiator can resolve them when it parses the body.
- The C enum fixture uses type kind 4, with the namespace, enumerator list,
  builtin underlying type and name at `0x06`, `0x0a`, `0x0e` and `0x12`.
  Its namespace entries share the same enumerator objects as the type's
  list. An enumerator stores next, name and type pointers at `0x02`, `0x06`
  and `0x0a`, followed by a high word and a low word at `0x0e` and `0x12`.
  Type-tag objects hold the type pointer at `0x02` in both the C and C++ fixtures.
  The source loader preserves one `ObjEnumConst` per stock offset for both
  the enum type and namespace entries.
- The reader restores the previously seen file list from header offset
  `0xce`. Its chain stores a hash name at `0x00` and the next file at `0x04`
  and passes each record through `0x4712d0` and
  `preprocessor_insert_unique_file`. Disk file records point to include
  metadata at `0x2a`. Its once-only byte at `0x0e` is set for both
  `#pragma once` and `#import`. The byte at `0x0f` distinguishes quoted
  and system includes. Stock `preprocessor_lookup_file` at `0x44d570`
  falls back to a matching basename for a previously seen once-only file
  with the same include kind and an unconditional signature. The source
  reader applies these checks after resolving the include. It also reads
  named-macro guard signatures such as `#n10TEST_GUARD$#f` and reevaluates
  them against live macro definitions. Other conditional expressions run
  through the regular preprocessor when the header is opened.
- For file IPA, `CMid_DefineFunction` at `0x47E4E0` queues definitions and
  applies `COpt_SimpleOptimizer` before packing, including the non-inline
  branch at `0x47E7E0`. `CCallGraph_Compute` at `0x5C0720` builds reference
  edges. The inlining pass at `0x558900` skips the graph with inlining
  disabled. Its visitor at `0x5589A0` processes callees first and loads a
  body only when it references an inline candidate. It simplifies and
  repacks the body when expansion changes it.
- The final pass at `0x47CE20` runs `CExcept_CallGraphExceptionOptimizer`
  at `0x49EFA0` after inlining, before emitting surviving definitions
  through `CMid_GenerateCGNode` at `0x47CFF0`. With `optEH` enabled, the
  exception pass loads potentially throwing bodies and propagates explicit
  throws, rethrows, and unknown throws through direct calls. It repeats
  when it proves a function cannot throw and removes exception actions from
  bodies proven safe to simplify. Both the exception pass and code
  generation allocate fresh labels when unpacking a body.
- RC1's `CIRStream_PackCode` at `0x52ABA0`
  builds a reference table while encoding the body. `CCallGraph_Compute` consumes kind-1
  object entries, with a 16-bit saturating direct-call count and an address flag in bit 0.
  The source port builds those object entries from its shared `CI_FuncData` bodies.
  Stock program IPA writes an `.irobj` sidecar and
  an ELF placeholder with no generated code. `CIRStream_ExportCallGraph` at `0x4BD620`
  writes a 16-byte header with the little-endian magic `0xBACEECAB` and revision
  `0x00010002`. The third word is `cirstream_typeid - 0x3F`. `CIRStream_CheckHeader`
  at `0x4BD9C0` checks the length and magic. The importer at `0x4BD770` also checks
  the revision and a trailing magic value. `CIRStream_PackUInt32` stores values below
  255 in one byte and larger values as `FF` followed by a little-endian word. After the
  header, the exporter packs the main-file descriptor with `0x4BEF10`, then a compact
  length and a `CodeGen_WritePrecompilerData` block (`0x4C2C90`), then a compact node
  count and the graph nodes. For an ordinary main file, the descriptor starts with a zero
  file index, a compact path length (including its implicit terminator), the path bytes,
  and two kind bytes. Kind 0 follows with 260 bytes of path storage, a 16-bit field,
  eight bytes of file metadata, an optional include descriptor, and two flag bytes.
  The backend block starts with nine little-endian words: revision 2, a flag, six block-relative
  offsets, and the total size. Its subsections contain 124-, 24-, and 44-byte records,
  a 48-byte record for the five default section selectors, a length-prefixed string pool,
  and 16-byte file records. The offsets and total size are aligned to four bytes relative
  to the block start. `CodeGen_ReadPrecompilerData` at `0x4C0E10` reloads the block.
- `CCallGraph_Renumber` at `0x5C0EC0` assigns zero-based IDs in graph order. At `0x4BDCB0`,
  a node prefix starts with a compact tag. Tags 0 and 1 encode an ordinary node and its root flag.
  A tag of 2 or higher identifies an implicit compiler object by `tag - 1`. Ordinary nodes
  continue with two object bytes and a section selector, then their namespace, name, type,
  and definition. The function definition encoder at `0x4BEB00` writes the compact code size,
  the inline size, 208 bytes of options and flags, the compact reference count, the code,
  and compact local, label and type counts. Each reference begins with a 16-bit call count,
  a flag byte and a kind byte. A kind-1 object reference then carries a compact graph node ID.
  `CIRStreamTools.c` and `CIRStreamCallGraph.c` contain the source codec for the envelope and
  these record headers. `uv run python tools/wii_rc1_irobj_smoke.py PATH_TO_STOCK_RC1` checks
  byte-for-byte typed graph round-trips against stock output, including a custom section and a
  graph with 300 nodes. `CIRStream_ImportCallGraph` materializes declarations, reconstructs data
  relocations and thunks, and validates function references against graph IDs.
  `CIRStream_ExportCallGraph` regenerates the node stream from those types and definitions.
  The exporter takes serialized backend state, file descriptors, and function code as inputs.
  With the stock linker installed beside the compiler, the smoke test runs the stock program IPA
  pipeline on a rewritten graph and checks the edited initializer and resolved pointer in the ELF.
- `CIRStream_UnpackType` at `0x5E8D00` uses compact type tags. Tags from `0x3F` index the
  translation unit's type table. Tag 34 refers to a type entry in a packed body's reference
  table. Tags 35 and 36 introduce unqualified and qualified pointers. Tag 37 introduces
  an array with compact signed size, element type, compact qualifiers, a second signed field,
  and three bytes. Tags 38–40 introduce enum, struct and class types. Tags 41 and 42 introduce
  function and member function types via the common decoder at `0x5E96E0`. A new function
  type gets a table entry before reading its return type and arguments. `CIRStream_UnpackNameSpace`
  at `0x5EAB10` uses tags 0–4 for null, root, an existing index, a named child, or a class's namespace.
  The source codec reads and writes types and namespaces within one graph. `CIRStream_GetTypeID`
  reserves IDs before descending into compound types. Pointer wrappers compare structurally,
  while each bit field gets a new ID. The codec retains additional RC1 fields when re-exporting
  imported types and builds function-body type patches through the caller's reference table.
  Class namespaces get an identity before the reader descends into the enclosing scope. This
  preserves a nested enum referenced by a data member of the enclosing class. The graph codec
  reproduced all 898 stock sidecars from the configured Echoes R3ME01 C and C++ corpus.
  The stock tests check a direct call with a forward reference, a recursive C aggregate with
  a bit field and an array, signed enum values, nested C++ namespaces, member functions, and a
  multiple-inheritance dispatch thunk.
- `CIRStream_UnpackCode` at `0x52A950` reads three source locations, the argument and
  local tables, two function flags, and a tagged statement stream. Its source locations use the
  body's file patches plus little-endian byte offset and line words. Local and graph object patches
  preserve object identity across expressions. `CIRStream_UnpackCode` reconstructs shared compiler
  statements, expressions and labels for ordinary stock bodies. RC1's `EINFO` payload at wire tag
  `0x42` stays attached to a no-op statement. The stock backend's `gen_INFO` at `0x52A770` returns
  immediately. The smoke test checks calls, local bindings, branches, switch cases, string and
  floating-point literals, and source locations in an included file. The action marker `0x18`
  sets the exception chain for subsequent statements. The action list ends at `0x17`.
  The C++ fixture verifies a destructor action, its local object, a catch label, and the active
  catch record. Implicit object IDs 1–24 resolve through the shared runtime objects, following
  the stock resolver at `0x4BDE70`.
- `CIRStream_PackCode` at `0x52ABA0` writes statements and expressions into a new body and
  builds fresh object, type, name, and label indices. The writer preserves source and exception
  records and computes call counts and address references from the rebuilt expressions.
  It reuses the original file descriptors, whose records may introduce file IDs for later
  functions. The writer rebuilt bodies in all 898 stock Echoes R3ME01 sidecars in the corpus.
  Re-importing and rewriting each rebuilt sidecar produced the same bytes on the second pass.
  Stock RC1's program IPA reader accepted a rebuilt body with an edited return expression and
  produced the same PowerPC text as an independent stock compile of the edited C source.
- The inline assembly path at `0x52B2B5` reads a byte block followed by object, label, and type
  patches. The codec retains the stock instruction bytes and writes fresh patch references,
  clearing the original pointer values. The stock link test checks a branch to a local label
  and an instruction using a local register variable. Wire expression `0x52` is a symbolic
  member-function pointer literal created at `0x556C3F`. Its operand names the member function
  whose pointer data will be materialized later. The stock link test assigns a member-function
  pointer and checks the generated text. The source consumer translates assembly operands and
  invokes the function-level assembler for stock assembly definitions. It materializes symbolic
  member-function pointers through the frontend's existing data emitter. The source and stock
  replays emitted the same pointer data and relocation in the nonvirtual fixture.
- The source driver routes `-ipa program -c` through the deferred graph. The port of
  `CodeGen_WritePrecompilerData` writes section, attribute, source-file, and small-data
  configuration records from backend state. Body export retains the initial no-op statement
  used as the list head by the stock code generator. Function alignment uses a power-of-two
  exponent in the wire options. For inline assembly, the source writer maps the opcodes verified
  against stock RC1's `opcodeinfo` table and emits the native operand layout and patch offsets.
  The stock interoperability test links fresh C and C++ sidecars with a cross-unit call and
  checks the text, custom section contents, and a data relocation. It also compares code for
  an inline assembly block containing a local register variable and a branch, and for a
  C++ static initializer. The source compiler emitted sidecars for all 898 configured
  Echoes R3ME01 C and C++ units. The graph reader re-encoded each source-emitted sidecar
  byte for byte. `CodeGen_PragmaPop` at `0x4C68F0` bounds its restore loop by the saved
  section count. The source restore follows that bound so sections added within a pragma
  scope retain their attributes.
- The custom section payload passed to `CodeGen_AddCustomSection` at `0x4C0D00` is a signed
  little-endian word. Positive values select attributes directly. Negative values select a
  per-declaration section set, and zero uses the backend defaults. The source reader resolves
  section names through the attribute records and the string table. The stock test checks
  `.more` and its linked `.morebss`. The compiler registers imported sections with the live
  backend before emitting the associated functions and data.
- The function options block uses the field offsets in `CMach_GetBackEndOptions` at `0x4F09A0`.
  Independently varying stock flags and pragmas identified optimization, CPU, alignment,
  small-data thresholds, and loop limits. The reader maps those scalars to the shared options
  structure. The source driver batches sidecars before linking, resolves external names across
  units, and runs the shared inliner. The native and source replays inline the same cross-unit
  call in the smoke test. RC1's small-class return logic at `0x4F1DA0` uses the trivial-copy
  bit in the serialized class flags. Preserving this bit lets the reader recover the return
  convention without the class's constructor and destructor declarations. The source consumer
  replayed all 898 stock Echoes R3ME01 sidecars to PowerPC objects.
- Program IPA opens a sidecar's existing file specification with `FSpOpenDF`. The
  compatibility `COS_FileOpen` wrapper reconstructs the specification through
  `HOpenDF`, which failed in the Wasm host even for a valid sidecar. The Wasm
  smoke test replays sidecars emitted by both stock and the source-built module.
- The conditional and unconditional branch ranges are little-endian words at offsets
  `0xa8` and `0xac` of the function options block. Stock reads them in
  `CMach_SetBackEndOptions` at `0x4F07F4` and `0x4F0800`. The defaults are
  `0x10000` and `0x04000000`. The `bc_range` and `b_range` pragmas double
  numeric arguments at `0x466E87` and `0x466F57` before checking their limits.
  Zero is a valid setting in both fields, including when both are zero. A local
  relative `b` checks its displacement against `-(b_range / 2)` through
  `b_range / 2 - 1` at `0x59D637`. A local `bl`, an immediate, and an external
  relocation bypass that check. The backend resets both ranges when disabling
  compression and requires custom ranges to appear after enabling it.
- `CMach_GetBackEndOptions` at `0x4F09C2` reads the peephole setting from
  `0x74F4CA` and writes it to byte `0x12`. `CMach_SetBackEndOptions` at
  `0x4F02B7` restores it. Compiling a function-local static with `-opt peep`
  and `-opt nopeep` toggles the byte and changes the guard code in the linked
  program. The smoke test compares the full PowerPC text in both directions
  with the opposite setting on the link command line. Sidecars emitted before
  the flag was saved contain zero at this offset, which stock reads as off.
- `#pragma ppc_opt_bclr_bcctr` changes byte `0x9e` in the same options record.
  Stock uses this flag to replace a conditional branch to a shared return with
  a conditional branch to the link register when possible.
- `CTempl_Parse` at `0x511070` recognizes friend templates before parsing class
  declarations. Its friend-class handler at `0x5F0C10` resets template parameter
  nesting for new namespace-scope declarations and registers the class with
  `CClass_AddFriend`. Template candidate creation at `0x5F4C00` passes a suppression
  flag to `CTemplTool_InstantiateType` at `0x54AB90` and drops the candidate if
  substitution fails. RC1 enables `new_mangler` by default, so template function
  instances encode the return type after the parameter list. `#pragma new_mangler off`
  restores the older spelling, and class member template instances have weak
  external linkage in a named class with external linkage.
