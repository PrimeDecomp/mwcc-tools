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
  `0x62b860` after switch lowering and can combine the two comparisons for an
  interior range.
- RC1's class-specific liveness pass calls `0x63a9e0` at `0x63a491` before
  building the interference graph. It restricts entry live-ins to physical
  registers and walks blocks forward, masking live-outs by live-ins and local
  definitions. A debugger trace confirms that at a join, the first predecessor
  link supplies its source block's live-out and later links supply the
  destination block's live-out. In `fwide` this removes a false interference
  edge, so the result coalesces into r4 and is moved to r3 at the shared return.
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
  group kills. Local materialization uses an enclosing comma expression
  where needed to keep definitions ahead of their uses.
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
  `CCallGraph_Compute` at `0x5C0720` builds reference edges. The final pass at `0x47CE20`
  calls `CInline_CallGraphInlining` at `0x558900` and emits surviving definitions through
  `CMid_GenerateCGNode` at `0x47CFF0`. RC1's `CIRStream_PackCode` at `0x52ABA0`
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
- `CTempl_Parse` at `0x511070` recognizes friend templates before parsing class
  declarations. Its friend-class handler at `0x5F0C10` resets template parameter
  nesting for new namespace-scope declarations and registers the class with
  `CClass_AddFriend`. Template candidate creation at `0x5F4C00` passes a suppression
  flag to `CTemplTool_InstantiateType` at `0x54AB90` and drops the candidate if
  substitution fails. RC1 enables `new_mangler` by default, so template function
  instances encode the return type after the parameter list. `#pragma new_mangler off`
  restores the older spelling, and class member template instances have weak
  external linkage in a named class with external linkage.
