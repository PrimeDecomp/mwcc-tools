# Near-miss ledger: GC_2_6's inexact functions

Snapshot of October 16, 2026, 13:30 (counts updated after the S_BACK/S_FRONT spot fixes). Taken from `build/GC_2_6/compare.json` and `tools/status.py GC_2_6 --todo`:
4018/4077 functions exact; 2806/4080 linked (68.8%, 52.0% of bytes).

Sources read: AGENTS.md, docs/review-2.6.md, docs/audit-2.6.md, docs/audit-data-2.6.md, HANDOFF.md, and every agent
REVIEW*.md (mwcc-agent-kit and the scratchpad agents). Also the near-miss prompts (NM/N3/R4), the AGENTS.md copies in
older agent trees, the source TODOs, and the agents' work files (variant specs, decl-order logs). "Differs" is
today's `tools/fn.py diff`. Most variant runs in the work dirs kept no scores, so they appear as "(result not
recorded)". Anything marked "(inferred)" is a reading of the diff, not something a source states. S_BACK and S_FRONT
(the October 16 spot fixes) are folded into the entries; changes they found that cost exact functions are at the end.

Round names, oldest first: rewrite round 1 (Oct 15 ~19:00, per-unit agents), NM = near-miss round 1, R_ = rewrite
round 2, N3 = near-miss round 3, R4 = near-miss round 4, then the review rounds V/W/X/Y/Z/H/RB (scratchpad, Oct 16
early), then review round Y (agent-kit Y_*) and RB_DECL/RB_INL (Oct 16 ~12:00).

## Counts

- 50 inexact functions in 27 sources after NM5 (October 17: vformatpcode, CPrec_GetTypePatch, DoTransformations24,
  canoptimizevectorconst, gen_CONDASS and I8_insert_bitfield exact, and AddNodeAndSuccessorsRecursively exact again
  through a neighbour's parameter copy; 4027/4077 exact; PCodeInfo links in every version). No source with a row
  links while it has an inexact function, and every one of the 27 is all-exact apart from these.
- 13 units are one function from linking: SpillCode (insertspillcode 91.1), VectorArraysToRegs (computeaddilist
  94.8), CPrepTokenizer (tquot 97.7), CDecl (CDecl_AddFunctionMember 99.3), CInline (CInline_UnpackIFunctionData
  92.2), CodeMotion (isloopinvariant 97.3), FunctionCalls (analyze_arguments 92.5), IroPropagate (IRO_CopyAndConstantPropagation 99.7), IroUnrollLoop (UnrollWhileLoop 89.1),
  ConstantPropagation (propagateconstantstoblock 98.9), and since NM5 IroTransform (DoTransformations11 96.0), TOC
  (rewrite_opassign 66.3) and Operands
  (I8_extract_bitfield 83.8).
- The lowest are rewrite_opassign 66.3, adjustforward 68.1, compress_data_area 73.1, statement 73.6,
  GeneratePossibleCondAss 78.0 and gen_COND 80.8 (NM5: 78.3 -> 80.8).
- 2 functions differ in size from the original. These are not register-only: adjustforward +32 and gen_ASS +31
  (NM5 fixed CPrec_GetTypePatch's +64, DoTransformations24's -80 and canoptimizevectorconst's +32). Smaller size gaps: rewrite_opassign -16, gen_COND -16, I8_gen_DIV_MOD -16,
  CPrec_DumpColorSymbolTable -16, GeneratePossibleCondAss -16, UnrollWhileLoop +4, CDecl_AddFunctionMember +5,
  CPrep_PragmaOnce +4, I8_extract_bitfield +1.
- Most of them are recorded as checked equivalent to the original statement by statement (IDA/Ghidra pseudocode),
  so the remaining work is matching the original's code shape, not fixing behaviour. Real bugs found earlier while
  matching, and already fixed: gen_ASS (`copts.gprfloatcopy`), user_assign_check (`explicit` tested on the Object's
  qual), GeneratePossibleCondAss (two, round 4). I8_extract_bitfield reproduces a dead `offset == 0` path that 2.6
  has.

## Recurring patterns (each function's "Pattern" line has the details)

1. **One register or one register pair swapped, with everything else equal.** This is the commonest pattern. Each
   function was checked equivalent, and decl-order, int/SInt32 and split/merge searches have been run on most of
   them without success.
   - InstrSelection: gen_TYPCON, gen_AND (gen_CONDASS went exact in NM5: `outputReg` reused for the fsel block's
     OBJECT_REG, the parameter's extra uses give it ebx).
   - Elsewhere: insertspillcode, analyze_arguments, isloopinvariant, propagateconstantstoblock, unrollloop,
     BackEnd_WriteElf, ADDI_LU_SU, InlineAsm_ScanAssemblyOperands,
     generate_traceback, CPrec_GetArgListPatch, CInline_UnpackIFunctionData, DestructorReturnTransform,
     ParseLocationSet, ExtractLoopInfo, pragma_on_off_reset (in an inlined strcmp).
   - NM5 lead (vformatpcode, exact): a swap that moves elsewhere when you fix it (a "seesaw": fixing one temporary's
     register swaps a pair in another region) settled only when two variables shared across regions were split at
     once: the 'T' case's value and the 's' case's loop counter each became a case-block local. Each split alone
     gave 99.3 (the swap moved to the other case). Search such splits in combination (every pair of regions that
     share a variable), not one at a time: work/vf8.var in NB generated 144 combinations.
2. **Registers rotated or renumbered across the whole function.** This is a larger version of pattern 1:
   computeaddilist, compute_interrupt_frame, compress_data_area, I8_extract_bitfield,
   CMach_StructLayoutPackedBitfield, I8_ShiftRightImmediate, statement (in the inlined helper copies), MatchPTF,
   user_assign_check, CPrep_PragmaOnce.
3. **2.6 reloads a value after a store, or keeps it in a stack slot, as if its address were taken.** Ours keeps it
   in a register. The cause is unknown in every case. Two of these already carry `volatile`/pointer workarounds
   elsewhere (scheduleblock's `PCodeBlock** pb = &block`, Peephole's block-local `instr` copies).
   - rewrite_opassign (spills `expr`; NM5: the number of locals switches it, one or three extra give 2.6's
     reloads of `expr`; I8_extract_bitfield's reloads of `output` flip the same way; see the entries).
   - (Coerce_to_register was one: exact again once load_store_register, which it calls, took Pro7's `Opcode`
     parameter in place of `short`: H2_BE.)
   - (AddNodeAndSuccessorsRecursively and DoTransformations24 are exact since NM5. DoTransformations24 was a copy of
     its parameter: MWCC keeps a parameter that is the source of a copy it propagates in memory and reloads it after
     every store, so `IROLinear* nd = expr;` at the top reproduced 2.6's reloads exactly. In small pro6 tests the
     effect needs a parameter (a local's copies never cost a reload) that is not given a register (one used a lot
     gets one), it goes with `#pragma opt_propagation off`, and it goes again with more copies (three block-local
     copies, or four assignments of one, no longer reload). Look for this first when 2.6 reloads a parameter; the
     two workarounds above may be the same thing.)
   - IRO_CopyAndConstantPropagation.
   - assign_local_memory (reloads the global `local_data_size`).
   - scannew (a call result spilled and reloaded).
   - CPrec_DumpColorSymbolTable (`cprec_builtin` reloaded in the inlined SetupBuiltIn).
   - I8_extract_bitfield (locals updated in their slots).
   - gen_ASS (a reload of a spilled value).
   - The Peephole near-misses (RLWINM_RLWIMI_STW/STH, ADDI_L_S) are the same pattern.
4. **Stack-frame layout: a local's slot, or locals the original has and ours lacks.**
   - tquot (`start_pos` at [esp] vs [esp+4]: 2.6 has one more unreferenced spill).
   - InitElfFile (257-byte `filename` at esp+7, ours esp+4, unexplained).
   - GeneratePossibleCondAss (8 bytes: two locals missing).
   - adjustforward (+32).
   - gen_COND (slots permuted).
   - RebuildPossibleCondExpression and RebuildPossibleLogicalExpression (two slots swapped).
   - DoTransformations11 (stack vs register for two locals).
   - CDecl_AddFunctionMember (spill choice).
   - CPrep_PragmaOnce (a flag spilled where 2.6 keeps it in bl).
   - InlineAsm_TranslateIRtoPCodePPC (a short local spilled).
5. **Inlining differs: 2.6 inlines more or less than ours.**
   - (CPrec_GetTypePatch was one; exact in NM5. Its inlined CPrec_GetTypeBitfieldPatch writes CPrec_AppendData out
     by hand, so it has a branch and is inlined as statements, and that keeps AppendAlign and AddressHashVal out of
     line one level down. See the note after this list.)
   - I8_gen_DIV_MOD: I8_log2n's result goes through a stack temporary.
   - gen_MODULO: the inlined ispowerof2's register copy is missing.
   - pragma_on_off_reset: a copy of `bits` in the inlined PopOption is missing.
   - MatchPTF: inlining is suspected (inferred).
   - Deferred-unit coupling: gen_AND and gen_SHR share the inlined `shift_and_mask`, and gen_AND reaches 99.4 only
     at gen_SHR's expense. CPrec's helpers also couple.
6. **Integer width and type.**
   - InlineAsm_TranslateIRtoPCodePPC: a 16-bit loop counter where 2.6's is 32-bit.
   - CMach_ParseGlobalRegisterVariable: was a 16-bit compare of `storageclass` with the `TK_` defines where 2.6
     sign-extends; the `(int)` casts took it back to 99.4 (October 16), and it is now pattern 1.
   - (canoptimizevectorconst went exact in NM5: a `(SInt8)` cast of the char counter keeps IRO from
     strength-reducing the rows, pointer casts keep the walk pointers from being copy-propagated.)
   - unrollloopconditional: loop-entry form. 2.6 enters at the step, which points to an int/unsigned bound or
     counter-type question, as in AltiVec's advance_clock (inferred).
7. **Workarounds already in the source (TODO) that still fall short:** BackEnd_WriteElf (per-function
   `#pragma sym off`), I8_ShiftRightImmediate (`#pragma sym off`), CMach_StructLayoutPackedBitfield (a spilled-slot
   mask), CDecl_AddFunctionMember (the `x = x |` form). See each function.

**Inlined helpers: expression or statements (NM5).** The front end inlines a helper in one of two ways (as read
in 2.6's own CInline.c, whose front end is close to the host compiler's: CInline_CanInline,
CInline_InlineFunctionExpression, CInline_ExtractInlineFunction). A helper whose
body is only expression statements and a final `return` (CI_CanInline6) is pasted into the caller's expression. A
helper with any branch or loop (CI_CanInline3) is expanded as statements, with a new local for its result when the
call sits inside an expression. Expansion goes level by level over the whole function (level 1: callees of at most
10 statements, level 2: 7, level 3: 3), and statements expanded at one level are scanned at the next. So the
helper's form decides how deep its own callees sit, and so which of them are inlined, as well as the caller's
temporaries, registers and stack slots. Constant branches (`if (1)`, `if (0 && ..)`, an empty `if`, `do {} while
(0)`) are folded before the body is stored and do not count. A helper that 2.6 inlined differently, with the same
code standalone, can be a helper with a real branch written out (CPrec_GetTypeBitfieldPatch's hand-written
AppendData). tquot's stack slots take 2.6's order when both prepcurstringchar calls are expanded as statements;
only a branch that changes code did that so far.

Groups that share one cause, so a fix may move several:
- CPrec's three functions (the helper shapes and inlining).
- The InstrSelection group: gen_AND with gen_SHR; gen_COND, gen_ASS and gen_CONDASS share helpers.
- StackFrame's four (the frame globals and the `update_*_align` inlining).
- IroExprRegeneration's three (stack slots of two locals in the two Rebuild* functions).
- LoopOptimization's unrollloop and unrollloopconditional (the same walkers).
- Operands' I8 bitfield pair.

## Per unit (nearest to linking first, as status.py --todo orders them)

## src/backend/SpillCode.c (does not link: 1 inexact)
### insertspillcode (0x0057c390): 91.1%
- Differs: registers only; same size (545 bytes) and instructions. esi and edx are swapped throughout: in the
  paired-single marking pass (the `effect & 0x4000` temporary in edx where 2.6 has esi; the `0x800`/0 ternary value,
  its `or` with edi and the `or word ptr [node+0x16]` in esi where 2.6 has edx), and in the second loop (`flag` in edx
  where 2.6 has esi; the `op` walker `lea [instr+0x24]` / `add 0xc` in esi where 2.6 has edx). Checked equivalent
  against IDA: same statements, the marking pass tests effect 0x4000 then 0x8000 and ours computes them in that order
  (Y_BE1 REVIEW.md / docs/review-2.6.md).
- Tried (newest first):
  - NM5 (Oct 16, NB work/isc_gen.py, isc_gen2.py, isc3.py): with vformatpcode's fix in mind (split shared
    variables in combination), the first pass's counter as a fresh `j` (UInt32/int; function, outer-block, block-for
    or instr-for level) x `arg` at those levels x the second loop's `flag`/`op`/`i` as block locals (352 variants):
    best 91.1 (most 87-88). Loop 1 walking with `op` under all 720 declaration orders: 79.1. The first pass's
    `block`/`instr`/`i` split into fresh variables (21 combinations, function or outer-block level): best 91.1.
    Class: renaming only (esi/edx, consistently: loop 1's `effect & 0x4000` temporary and the second ternary/or
    result; loop 2's `flag` and the `op` walker).
  - Y_BE1 review (Y_BE1 REVIEW.md "Open", work/isc.c): declaration orders and `--swaps`, a separate first-loop
    counter, block-local `arg`, `op` reused, the flags through a local, nested ifs, the ternaries swapped, the loop as
    an inlined helper; isc.c variants `ig_arg` (index `interferencegraph` through `arg->data.reg.reg`), `eff_args`,
    `macro`, `nested`, `flag_before_i`, `nextfirst` (results not recorded; none matched). The statement order was
    changed to Pro7's `flag = FALSE; op = instr->args;` because IDA shows the original stores `flag` first; the old
    swapped order scored 94.9 but is the wrong statement order, so 91.1 is accepted as the truer source.
  - R4 (R4_BackOne work/sp1..sp12.c; result not recorded, none matched): declaration permutations of
    `flag`/`op`/`i` (p012..p210, declswap); `swapor`, `flagvar`, `opvar`, `argreg`; effect-test spellings (`eff`,
    `eff_first`, `effint`, `effint_first`, `cast16`, `noparen`, `ne0`, `twostmt`); the marking pass as a `static` /
    `static inline` helper (with and without keeping locals); 24 ternary-shape combinations (axp..dzq); the ternary
    arm/temporary types (UInt16, short, SInt32, UInt32, UInt8, bool); nested ifs and macro forms (`nest2`, `nest3`,
    `macro`, `nest_macro`); per-function `-sym off` (`symoff`); block-local `arg`/`flag`/`op` (`argblock`,
    `flagblock`, `opblock`, `bothblock`, `all`); `flag` types (Boolean, UInt32, SInt32, short, char, unsigned int,
    UInt8) and `i` types (int, SInt32, UInt16, short, unsigned int); a separate first-loop counter `j` (UInt32,
    unsigned int, int, SInt32; declared first/after i/last); comma forms, `flag` init at top, `for`-head forms.
  - N3 (N3_OneLeft work/isc*.v, try_sc.py; results not recorded, none matched): 96 variants of 4-declaration orders
    with `op` first / `flag` first, also under `-sym off` (isc.v); `op` reused (isc2); the swap/eq/ne0/argreg/paren/
    two-statement/nested-if tests (isc3); PC_OP_IS_ANY_REGISTER vs written-out and nested forms, with and without a
    pointer (isc4); a separate counter `j` of int/UInt32/SInt32/short/UInt16/SInt16 at three positions, and `i` types
    (isc5); declaration subsets (isc6); `flag` types and TRUE (isc7). The deferred-neighbour lever: the previous
    function generated, `assign_spill_locations`, given 1-3 dummy locals, a `(void) 0;` in two places, and
    `#pragma sym off` around it, each with both `flag`/`op` orders (try_sc.py).
  - Rewrite round 2 (R_RegAlloc_work/isc*.c, isc11.out, flagtry.py): about 1000 generated variants (isc11: every
    one 94.9 or 79.1 with the then-current base); placement of `arg`'s declaration, `direct`; `static inline`/`static`
    helper forms; `flag` as Boolean and first; `swap`/`ne`/`eq`/`argreg`/`u16`; types of `block`/`arg`/`flag`/`op`
    (int, UInt32, SInt32, short); 23 declaration orders (isc7); block-local `flag`/`op`/`arg` (isc9); `forarg`,
    `plus`. Whole-unit flag sweep: `-sym off`, `-O4,s`, `-O3`, `-O2` with deferred (flagtry.py; result not recorded;
    unit stays `-O4,p` deferred `-sym on`). The prompt for N3 summarized "2880 combinations tried".
- Leads: nothing recorded beyond the levers already tried (N3 prompt: "the previous function in a deferred unit,
  per-function `#pragma sym`", both done).
- Blocked by: nothing recorded. While it is lifted the unit's three statics stay global with bindings
  (`_rTEMP_for_*`, `_last_unused_vreg_before_spilling`) and the invented static name stays (Y_BE1 REVIEW.md).
- Pattern: register pair swapped (esi/edx); equivalent checked: yes (IDA). Real bug noted in the unit (not this
  function): the original's insert_store paired-single path inserts the addi twice (kept, commented).

## src/optimizer/VectorArraysToRegs.c (does not link: 1 inexact)
### computeaddilist (0x00577a50): 94.8%
- Differs: registers only, same size and instructions. The block index (`[eax+0x1c]`, used as `[eax+ebx*4]`) is in ebp where 2.6 has ebx. The `&instr->args[...]` pointer (`lea +0x3c`) is in ecx where 2.6 has ebx. `reg` (`movsx word [eax+0x28]`, compared against `n_real_registers[GPR]` and used as an index) is in ebp where 2.6 has ecx. Checked equivalent (AGENTS.md near-miss round 4 "Left ... each checked equivalent"; Y_BE1 REVIEW.md).
- Tried (newest first):
  - NM5 (agent NC, Oct 16): a local copy of the parameter `arrays` (first or last): 94.8. Class: renaming only.
  - Review round Y, re-diffed (Y_BE1 REVIEW.md / docs/review-2.6.md "Open"). Tried: block-local `reg`/`node`/`l` as in Pro7; the walkers merged; `short` reg (95.8, but adds a `movsx`) and `SInt32` reg; pointer arithmetic for `addi`; `defID` set apart; Pro7's `&&` chain with the `op` local (Pro7's chain 90.9, against the current nested ifs with the lookup as a statement); decl-order --swaps; the six statics made `static` with `#pragma pack(2)` at the end of the file (still 8/9, kept as the intended shape once it links). Y_BE1 work/v_computeaddilist.c `pro7`.
  - R4_OptOne (near-miss round 4), work/caddi*.c, generators gen_caddi*.py. Results not recorded per variant; the function stayed at 94.8.
    - caddi: Pro7's body and `pro7_lfunc`; `op` declared at function or block scope, crossed with int/SInt32/short/SInt16.
    - caddi2: block, op-first and reg-first scopes × or/ro orders × two condition forms × int/SInt32/short, plus `noreg`.
    - caddi3: eight condition shapes A-H (op local, comma in the condition, separate `reg =` statement, lookup inside the `&&`, no `op` local ...) × int/SInt32/short × function or block scope.
    - caddi4: `#pragma sym on` with the current body and with A_short/A_int; `reuse_A_*`.
    - caddi5: 1-11 dead locals, last or first.
    - caddi6: nesting and scope combinations (n1/n2/c1-c3 × fn/body/addi scopes × int/SInt32).
    - caddi7: `flip`, `nest`, `nest2`, `notlt`.
    - caddi8: merges `reg`=`defID`, `reg`=`useID`, `l`=`node`, `l`=`list`.
    - caddi9: `op` computed before the test (`pre_op`, `pre_op2`, `pre_op3`), and `pro7_arr`, `pro7_sep`, `pro7_arrnull`.
  - N3_GlobOpt (near-miss round 3), results not recorded:
    - work/cal.c: `reg` as SInt32 or short, no reg, reg first, reg or op block-scoped, both block-scoped (two orders).
    - work/cal2.c: 48 Pro7-shaped variants, block or top scope × int/short/SInt32 × decl orders × with or without `op`.
    - work/cal3.c: 1-8 dead locals, first or last.
    - work/cal4.c: `nested`, `oneline_cast`, `oneline_swapcmp`, `ifarray`, `ifarray2`, `opfromreg`, `opcomma`, `opinif`.
    - so5.py/cal5.c: per-function `#pragma sym on`.
  - NM_backend (near-miss round 1), work/vav.c (result not recorded): `addi` by pointer arithmetic in four spellings (`ptr`..`ptr4`); the `naddsinblock[...]` test as `!= 0` or `> 0`.
  - Earlier: the Oct 14 audit had it at 54.4 (IDA-pseudocode body with 1.x use-def accessors; audit-2.6.md). It was rewritten from Pro7 to reach 94.8 before near-miss round 3 (prompt_N3_GlobOpt.md).
- Leads: nothing recorded.
- Blocked by: nothing recorded outside the function. Once it is exact, the unit's six statics are to become `static`, with `#pragma pack(2)` at the end of the file (AGENTS.md). Until then they stay global, with bindings `_ADDIs` .. `_converted_arrays` for lifted code.
- Pattern: register assignment (block index, `op` pointer and `reg` rotate among ebx/ecx/ebp); equivalent checked: yes.

## src/frontend/CPrepTokenizer.c (does not link: 1 inexact)
### tquot (0x004d4e50): 97.7%
- Differs: one stack slot: 2.6 keeps `start_pos` at [esp], ours at [esp+4] (the store and the three `push [esp+..]` of it differ). Everything after that is the same code shifted by one byte (ours is 1 byte longer up to a label, so the alignment padding differs: our `mov eax, eax` against 2.6's `lea eax, [eax]`). Earlier notes describe it as "2.6 has a second, unreferenced spill before `start_pos`" (AGENTS.md near-miss front-end round; prompt_R4_Front.md).
- Tried, newest first:
  - NM5 (Oct 16; work/tq/v1-v12.c, pcs*.c): class **local** (one stack slot: the store of `start_pos` and its three pushes use [esp+4] where 2.6 uses [esp]; the rest is the same code shifted by the 1-byte-longer store, plus one padding instruction). Found: the unreferenced slot goes with `start_pos` being spilled (made a global: frame 8, no hole), and `start_pos`'s declaration position does not move it (first, last, initialised at its declaration: 97.7; assigned after `at_linestart`: 97.2; after the `cprep_nostring` return: 95.9). It takes 2.6's place, [esp], when *both* prepcurstringchar calls are expanded as statements (a private copy with a real branch; one call alone does not), but the branch changes code (82.1) and nextchar too (96.6). Code-neutral branches in prepcurstringchar (`if (str == str)`, a FALSE local flag, a test of the just-cleared `prepnextstringchar_foundnl`, two equal arms) fold away and change nothing (97.7). Also tried, no match: two block-scoped `ch64`s (94.5), block `save_pos` (82.8), `char*` save_pos (86.2) or start_pos (97.7), save_pos assigned before/at the top/after the skip (68.0/77.7/78.9), the else restoring `start_pos` (85.1), the 'p' test inverted (95.9), `(void)&dummy` at every declaration position (97.7: an address-taken unused local gets no slot), a dead copy of start_pos (97.7), prepcurstringchar's locals swapped/`int t`/`StringPtr save_pos`/inline/initialised (97.7/94.3/79.4/97.7/97.7), CInt64_SetLong written as if/else (93.4).
  - Y_PREP REVIEW.md, review-2.6.md (round Y): Pro7's declaration order (82.8); `size` declared after `save_pos` (kept: it is needed); decl-order --swaps; block-scoped `save_pos`, `ch64` and `offset`; a function-level `offset` at every position (v_tquot.c: off_after_type/string_ptr/c/save_pos/size/start_pos/ch64, blockch64, blocksave); `int c`; a dead store to `t` (adv/v_tq.c "tunused"). None matched.
  - R4_Front work (near-miss round 4; results not recorded):
    - tquot_var.c: cint_ch, offset_top, offset_first, startpos_first, ch64_first, retc, ret_direct, c_int, a dummy local, `char*` save_pos.
    - gen_tquot2: symoff, ch64_two, t_use, startpos_char, inline_skip.
    - gen_tquot3: ternaries (tern, tern_c, type_tern), startpos_late.
    - gen_tq4: prepcurstringchar written out by hand (manual1, manual2).
    - gen_tq5 / tq_v5.c: every subset of the `(start_pos)` call arguments replaced by a copy `str`, with `str` declared first or last.
    - tq_perm.c: ~120 declaration permutations (d0_*/d1_*) and symon.
  - N3_CPrec work (near-miss round 3; results not recorded): t1.txt `start_pos` typed SInt32, StringPtr or int at each of 8 declaration positions, plus blockoff2; t3.txt pro7order, ifelse, startpos2; t2.txt prepcurstringchar's shape (swap, int, noT).
  - NM_frontend work (near-miss round 1; results not recorded): tq*.c / mv_tquot.c covered these variants:
    - nosp, noch64;
    - renames c->t, save_pos<->start_pos;
    - hoist0-7, pro7, savepos_block;
    - blkA/blkB/nodecl combinations;
    - size/c/offset as `int`;
    - ret/retdirect, type_ternary;
    - set1/set2/sp_char/swap_c combinations;
    - volatile, volatile2.
- Leads: audit-2.6.md said to run decl_order.py (done, see above). Nothing else recorded.
- Blocked by: nothing recorded. It is the unit's last inexact function: the relink differs only inside tquot's .text, and .data/.bss are byte-identical.
- Pattern: stack slot of a local (start_pos [esp] vs [esp+4]); size: 1 byte more before a label, absorbed by the padding; equivalent checked: yes (Y_PREP).

## src/frontend/CPrep.c (does not link: 2 inexact)
### CPrep_PragmaOnce (0x0043a410): 79.6%
- Differs: 431 bytes against the original's 427. Ours spills the inlined CPrep_DefinePragmaOnceMacro/CPrep_PragmaOnceName `flag` (`prep_file->isDefault`) to [esp+4] and tests it with `cmp byte ptr [esp+4], 0`. 2.6 keeps it in `bl` (`test bl, bl`). The registers are renumbered throughout the copy loop: 2.6 indexes `__lower_map` through eax, ours through edx (inlined CPrep_ToLower). The alignment padding also differs (`nop`, `mov eax, eax`). The literals @4691/@4692 are unplaced only because the function is inexact.
- Tried, newest first:
  - NM5 (Oct 16; work/pon1.py): class **local** (the flag spilled to [esp+4] where 2.6 keeps it in bl; 2.6 needs one register fewer in the copy loop because its inlined tolower reuses eax for `(unsigned char)c` (`movzx eax, al`) where ours takes edx; registers renamed from there). tolower spelled eight ways in a private copy (ternary with and without the `(int)` casts, if/return, `c & 0xFF`, a `char` version, an `unsigned char` local, `c == -1 ? c : ..`, written out at the call): 71.9-79.6, none better.
  - S_FRONT (Oct 16): the inlined CPrep_PragmaOnceName calls <ctype.h>'s inline `tolower` in place of the private CPrep_ToLower (same body); still 79.6.
  - Y_PREP REVIEW.md, review-2.6.md (round Y): Pro7's text for the function and its helpers (prep_try.txt: pro7 77.1, 430 bytes); `return`/else shapes; a local flag/file; the helpers' parameter types (int/char); the helpers' local orders; PopOption `SInt32`/PushOption `char` (Pro7's, kept); MSL's exact tolower text; five CPrep_ToLower spellings (`c & 0xFF`, a `char` parameter, an if/return, a `c = (unsigned char)c` local). None matched.
  - R4_Front work (results not recorded):
    - gen_pon: pro7, pro7tl, tolower, flagint.
    - Pragma variant files: the flag as Boolean/int/char/UInt8/short in every pair ("cur_X_Y", "best_X_Y"); eq0, alleq0, nested, elseif, notnot, cast, neq0u, plain, tern; permutations of PragmaOnceName's locals buf/myname/src/dst/len ("t_buf_myname_...").
    - AGENTS.md (round 4) records that reordering an inlined helper's locals moves this function's registers. It did not make it exact.
  - N3_CPrec work (results not recorded): p1.txt, seven CPrep_ToLower spellings (a..g); p2.txt, a dummy declaration before or after the helper.
- Leads: audit-2.6.md suggests running decl_order.py on it. Running it is not recorded for this function.
- Blocked by: it is adjacent in generation order to pragma_on_off_reset, and the two share the inlined strcmp/CPrep_ToLower helpers (Y_PREP). This and pragma_on_off_reset are the unit's only link blockers: .data and .bss are byte-identical in the relink.
- Pattern: local spilled where 2.6 keeps it in a register (flag in bl); register renumbering; size: 4 bytes more.

### pragma_on_off_reset (0x0043a5c0): 91.9%
- Differs: the three inlined `strcmp` copies use edx/ecx where 2.6 uses ecx/edx, and the reverse. In the inlined CPrep_PopOption, 2.6 copies `bits` (`mov ebx, ebp`) before `and ebx, 0x1fff` and loads `coptssave` into ebp; ours masks `ebp` in place and loads `coptssave` into ecx. So 2.6 keeps `bits` alive. Ours is 2 bytes shorter at that point, and the alignment padding differs.
- Tried, newest first:
  - NM5 (Oct 16; work/po1-po3.py): class **local** (the inlined strcmp temporaries in ecx/edx in the other phase; in the inlined PopOption 2.6 computes `id` into a fresh register (`mov ebx, ebp; and ebx, 0x1fff`) and loads `coptssave` into ebp, ours masks ebp in place and loads into ecx; 2 bytes). PopOption's parameter as `int`, `long` (header changed with it for the test), with `bits` as UInt32/SInt32/short/unsigned: 89.9-91.9. A dummy function before or after it (the deferred neighbours): no change, so the strcmp phase is not coupling. The phase is decided by the inlined PopOption: without the "reset" case (diagnostic) every strcmp temporary takes 2.6's register. PopOption reshaped (work/po5.c: `while` loop, nested ifs, `char*` copts: identical code; break + `if (!opt)`, the id test first, a `UInt8* save` local: 85-95, costing exact functions); its other inlined callers pass constants and stay exact under every form. Lead: the `bits & OPT_OFFSET_MASK` argument is what differs from those callers; 2.6 keeps `id` in a register of its own after the loop.
  - Y_PREP REVIEW.md, review-2.6.md (round Y):
    - the parameter as int/SInt32/short/unsigned/long;
    - Pro7's `UInt32 bits` (prep_try.txt: pro7 91.0, 540 bytes);
    - the five CPrep_ToLower spellings;
    - Pro7's text for the function and its helpers, `return`/else shapes, a local flag;
    - the helpers' parameter types and local orders;
    - PopOption SInt32 / PushOption char.
  - The source comment says the parameter is `int`: Pro7 has `UInt32 bits`, and 2.6's preppragma tests the bits it passes here as the same int. With `UInt32` it matches only with a cast on the test. AGENTS.md lists `pragma_on_off_reset(UInt32)` among "fakematches" that were a wrong type one level out.
  - R4_Front work (results not recorded):
    - gen_po: flagcopy, noret, noret_top.
    - gen_poor: flagint, flaglate, nest, nestout, popexp.
    - variant files: im0..im0123 (imex if-chain shapes), pop_imex/pop_r/pop_r2/pop_imex2/r_is_imex, bitsflag_decl_first, symoff.
  - N3_CPrec work: p2.txt, dummy1/dummy1after (results not recorded).
- Leads: audit-2.6.md suggests running decl_order.py on it. Running it is not recorded.
- Blocked by: it is coupled with CPrep_PragmaOnce (adjacent in generation order, shared inlined helpers), and through its parameter type with its caller preppragma.
- Pattern: register pair swapped (ecx/edx in inlined strcmp); missing register copy (`bits` kept by 2.6 in inlined PopOption).

## src/frontend/CDecl.c (does not link: 1 inexact)
### CDecl_AddFunctionMember (0x00536910): 99.3%
- Differs: 1080 bytes against the original's 1075, and only at the CLASS_ABSTRACT flag update. 2.6 has `or word ptr [ebp+0x2a], 8` (`tclass->flags |= CLASS_ABSTRACT`). Ours loads, ORs and stores (`mov ax / or eax, 8 / mov [..], ax`) because the source deliberately spells it `tclass->flags = tclass->flags | CLASS_ABSTRACT`. With `|=` on both flag updates, as 2.6 has, MWCC spills `tclass` instead of the inlined MakeTypeMemberFunc's `method`, and the function scores 76.9.
- Tried, newest first:
  - NM5 (Oct 16; work/afm/v1-v2.c): class **local** (one region: `tclass->flags = tclass->flags | CLASS_ABSTRACT` is load/or/store where 2.6 has `or word ptr [ebp+0x2a], 8`; size +5). With `|=` (76.9) `tclass` is spilled to its parameter slot instead of the inlined MakeTypeMemberFunc's `method` ([esp+4] in 2.6), frame 0x18 against 0x20; the extra use of `tclass` in the `=|` form is what keeps it in ebp. With `|=`: the two flag updates swapped (76.9), the conversion pair swapped (76.9), `decle->has_vtable` before MakeFunctionVirtual (76.6), the assert as `tclass->nspace == cscope_current` (76.6), a `(void)0` in the IsZero branch (76.9), the `lex_order_count` store after CParser_NewFunctionObject (75.5). Lead not tried: count the uses the spill heuristic sees (2.6's MakeTypeMemberFunc/AddThisPointerArgument inline as statements; their parameter copies are the competing variables).
  - RB_DECL / Y_DECL REVIEW.md, review-2.6.md (round Y adversary): with `|=`, 39 more variants: MakeTypeMemberFunc in Pro7's shape, an assigning `declinfo->thetype = TYPE(tfunc = ...)`, assert operand order, `!= NULL` spellings, memclrw(galloc), `flag == 0`, all 31 combinations of five toggles, and Pro7's `=|` on both. They scored 67.7-98.5, and the spill choice (`tclass` against the inlined `method`) never flips. Before that, about 70 combinations: parameter/local types, declaration orders, MakeTypeMemberFunc and AddThisPointerArgument shapes, if/else inversion, extra uses of `tclass`. Pro7's `!tclass->sominfo` in AddThisPointerArgument scored 91.1 and lost six callers. The Y_DECL work scripts are afm_try*.py, afm_brute.py and adv/afm2-3.py.
  - R4_Front work (near-miss round 4): afm*.c variants. Recorded:
    - `UInt8` vs `AccessType` access;
    - `sominfo == NULL` vs `!`, `mtheclass` cast, flagsfirst `=|`, ctorflags `!= 0`;
    - void1/void2, tc_copy, cast;
    - oreq_tc/oreq_findname/oreq, bools, both, static;
    - v_* combinations of conv/pure/tc1/tc2.
    - Not recorded: the result of each. The R4 round raised it from 70.9 to 98.5: AGENTS.md says a helper's local retyped `TypeFunc* method` did it.
  - N3_Front work: gen_afm.py (labels and results not recorded).
- Leads: nothing recorded beyond the above.
- Blocked by: the inlined MakeTypeMemberFunc (its `method` local competes with `tclass` for a register). The unit links once this function is exact: the relink --only differs only in its bytes.
- Pattern: spill choice (which local is spilled); source-level workaround in place (`= |` for `|=`); size: 5 bytes more.

## src/frontend/CInline.c (does not link: 1 inexact)
### CInline_UnpackIFunctionData (0x0055ca40): 92.2%
- Differs: ecx and edx exchanged throughout (33 lines), the same instructions otherwise: the inlined CInline_SetObjectSFlags' byte (`mov dl/cl, [ebx+0xc]`), the deftoken stores, the label/object table loads. One esi/ebx swap at 0x455. The jump tables are unplaced only because the function is inexact.
- Tried, newest first:
  - NM5 (Oct 16; work/uif/v1.c): class **renaming only** (ecx/edx exchanged for every short-lived temporary, alternating the way 2.6's do but in the other phase; plus esi/ebx and ebx/ecx in the last loop). The five global stores moved after the loops (61.9), the deftoken stores swapped (90.7), SetObjectSFlags before the name/type/qual stores (88.9), `last->next = NULL` before `last->object` (91.6), the loop initialisers swapped (91.9). The same ecx/edx phase difference is in pragma_on_off_reset's and CPrep_PragmaOnce's inlined strcmp copies; a dummy function generated next to pragma_on_off_reset does not move it there, so the phase is decided inside the function. It is decided at the last loop (work/uif2-uif6.py): with `cinline_first_stmt = firstStmt->next;` moved after the `cinline_label_trans` loop (98.9) or before the second statement loop (98.8), every register in the function is 2.6's and only that store is out of place; the loop's assert split into a store and `CError_ASSERT(4045, *cinline_label_trans->labelptr)` also gives 2.6's registers everywhere but in that loop (98.4, with a reload 2.6 lacks). With the store in its place: the loop as `for`, `for` with the store as initialiser, `if` + `do/while`, `for (;;)` with a break, `!= NULL` in the assert, an if/CError_FATAL, the store in a block, a trailing `(void)0` or `return`: 91.5-92.2. Lead: 2.6's last loop or the statements around the store differ in some way that leaves the code the same; look at what the loop's preheader holds.
  - RB_INL / Y_INL REVIEW.md, review-2.6.md (round Y), and the source TODO at CInline.c:3720:
    - decl-order --swaps;
    - the counter as int/SInt32/short, or one counter per loop;
    - `sflags` typed char/SInt8/int/short in CI_Var and in CInline_SetObjectSFlags;
    - that helper `inline`;
    - Pro7's reuse of `object` for the new Object (73%);
    - `#pragma sym off` for the function (90.5);
    - gc1's separate `cursor` walker for the first statement loop, and gc1's declaration order (92.2/86.8);
    - Boolean flag parameters.
  - Y_INL work: uifd_B.txt (a variant body; result not recorded); adv/v_sym.py (sym off).
  - R4_Front work: ui_v*.c variants: symoff, voidload, deadlocal, i0, last0, obj0, obj_object, objparam, i_s32, i_short, flags_first, objparam_first (results not recorded).
  - N3_Front work: gen_uif.py / gen_uif3.py (labels and results not recorded).
- Leads: the source TODO notes that 2.6 has a variable of its own for the new Object (Pro7 reuses `object`) and stores `value` before `flags`. Nothing else recorded.
- Blocked by: nothing recorded. It is the unit's only inexact function and the only difference in the relink (33 bytes).
- Pattern: register pair swapped (ecx/edx throughout); equivalent checked: not stated.

## src/optimizer/CodeMotion.c (does not link: 1 inexact)
### isloopinvariant (0x00571290): 97.3%
- Differs: registers only, in the memory case's write-use loop (+0x2b2..0x2f6): ebx and edx are swapped, for the `list->id*10` index and for the `Uses` / `loop->memberblocks` base. Same size. The switch's jump table `@1735` (lbl_005D1970) is not placed while the function is inexact. Relink: everything but these 17 bytes is the original's, data and .bss included (Y_BE1). The statements were re-checked against IDA's pseudocode of 0x571290: the r2/r13/_FP_/_CALLER_SP_ tests, the folded read-def loop and the write-use loop are as ours. Equivalent (Y_BE1 REVIEW.md).
- Tried (newest first):
  - NM5 (agent NC, Oct 16): local copies of each parameter and of every pair (30 variants, first or after the
    declarations): all 97.3. Class: renaming only (ebx/edx exchanged in the memory case's write-use loop: the
    `list->id*5` index and the `Uses`/`memberblocks` bases).
  - Review round Y (Y_BE1 REVIEW.md, docs/review-2.6.md):
    - decl-order --swaps;
    - Pro7's flag spelling `(flags == 0) & 0x40` (96.6), against the current `!pcode->flags & fIsConst`;
    - `(int)` casts dropped.
  - Y_BE1 work/v_isloopinvariant.c `pro7`.
  - R4_OptOne (near-miss round 4), results not recorded:
    - isli.c: 1-9 dead locals, first or last;
    - isli2.c: `blk`, `fn2`, `fn2first`;
    - isli3.c: `eq0`, `nested`;
    - isli4.c: per-function `sym off`;
    - isli5.c: `i` as SInt32/short/UInt32/unsigned int, the flags as Boolean/SInt32/char, `ret_bool`;
    - isli6.c: a separate `list2` walker for the read-def loop (`m1`, `m12`, `reg`, `m2reg`, `m1reg`, `all`);
    - isli7.c: a `bitvectorgetbit` macro with the operands swapped, unsigned, or as `%`, in the write loop or everywhere (`swap`, `all_swap`, `u`, `all_u`, `mod`, `all_mod`).
  - N3_GlobOpt (near-miss round 3), results not recorded:
    - so1.py: `#pragma sym off` on the function;
    - ili.c: Pro7's dead read-def loop, no dead loop, `dead_false`, `dead_flagsonly`;
    - ili2.c: `i` as SInt32 or short, `flagsBool`, `usesnested`, `memflags`, `usesobjlocal`;
    - ili3.c: a second list variable declared at the top or first (`l2top`, `l2first`), the same for the dead loop (`d2top`, `d2first`), `blockuses`, `blockdefs`;
    - ilb.c: `dead1`-`dead4`, `void0`.
  - NM_globalopt (near-miss round 1), work/ili*.c and ili_spec*.py, results not recorded:
    - the write loop as `while`, nested ifs, `!= 0`, flags tested first, `else return`;
    - `i` as sint32 or short, two lists, a `for` over `i`;
    - flag parameter types bool/char/sint32/short;
    - `use_ptr`, `mem_tmp`, `swap_vec_param`, `d1`-`d3`;
    - "also try changing the earlier dead-defs loop condition forms" (ili_spec5.py).
  - Rewrite round 1 (CodeMotion dir; AGENTS.md "CodeMotion.c ... are Pro7's"). The first Pro7 body was 79.4%, 11 bytes short (work/il.diff). Variant sets, results not recorded:
    - il.var A-E;
    - il2.var `swap`, `list2`, `i32`, `short`;
    - il3.var `nest`, `use`;
    - il4.var 1-6 dummy locals (d) and p1-p6;
    - il5.var SInt32 flag parameters, `bool`, `ui`, `op_last`, `list_last`.
    - Found then: r13 is treated like r2, compared as `(int) reg == 2`.
  - Oct 14 audit: 13.3% (1.x body; audit-2.6.md).
- Leads: nothing recorded.
- Blocked by: headers are PENDING (Alias.h's AliasType0..2 and PCode.h's fPCodeFlag20000/40000 are placeholders; LoopDetection.h's Loop members are invented). Not shown to affect the code. The unit is deferred, so a variant here can break a neighbour (AGENTS.md: check the whole unit).
- Real bug (the original's, kept): the memory case tests `!pcode->flags & fIsConst`, which by precedence is always 0, so the read-def loop never runs. 2.6 still computes it at run time.
- Pattern: register pair swapped (ebx/edx in one loop); jump table unplaced (follows from inexact); equivalent checked: yes.

## src/backend/TOC.c (does not link: 1 inexact)
### rewrite_opassign (0x004dfa60): 66.3%
- Differs: ours is 771 bytes against the original's 787. 2.6 reloads `expr` (`[esp+0x2c]`) and `reuse` (`[esp+4]`; the ledger used to call it `define`) from the stack after every store, as if address-taken, and swaps esi/edi throughout (2.6: `define`, the EMUL path's second `tmp` and `promo_left` in esi; the CSE temporary of `expr->data.diadic.left`, `commontype` and the shift path's `tmp` in edi). Ours keeps them in registers. Class: local (the reload regions: 0x60-0x6e, 0xab-0xc4) plus a consistent esi/edi renaming elsewhere.
- Tried (newest first):
  - NM5 (October 2026):
    - Host compiler: TOC.c, Operands.c and InstrSelection.c built with pro53 or pro5 lose every function (pro6 is right).
    - Found the switch that makes 2.6 reload `expr` after every store: it depends on the number of locals and
      no-code statements in the function, not on their names, types or places. One extra unused `int` local (or
      three; not two, four or 5..39), or one `(void)0;`, or one call of an empty `static inline` function anywhere
      in the body gives exactly 2.6's reloads of `expr` (771 -> 782 bytes, 58 -> 53 differing lines). Global
      declarations before the function change nothing. The same kind of switch flips I8_extract_bitfield (Operands)
      and gen_SHR: a host-compiler state (heap or table layout) the original reached with a different set of locals
      or nodes, not a statement of the source. Not adopted (a dead local).
    - Neither the esi/edi swap nor the reloads of `reuse` (at [esp+4]) move with that switch, with any declaration
      order (40 random orders and `decl-order --swaps` with the extra local), with renamed locals, with
      `(void)&reuse`, `volatile reuse`, or `(ENode*)` casts on lalloc. A real address-of reproduces `reuse`'s
      reloads (probes compiled with pro6 alone, NA agent's work/mini: an address-taken local is reloaded after every
      store, a spilled one is not; and once any local's address escapes, to a call or a global, every spilled value
      is reloaded after each store, while a dead `&x`, `if (0) f(&x)` or `p = &x` does nothing). 2.6's code has no
      `lea` of a stack address in this function, so no local escapes there: the cause is something else that makes
      the host treat stores as aliasing the stack.
    - `#pragma optimization_level 0..3`, `opt_*` off, `optimize_for_size` around the function: no change at all (the
      unit is deferred).
    - Wii 1.0RC1's code (0x598f00) also reloads `expr` after each store but keeps `reuse` in a register.
  - Pro7's `promote_type` as `static inline`: 60.7 -> 66.3. Also tried: Pro7's declaration order and block scopes, and a declaration-order search. `(void)&expr` reproduces expr's reloads (not adopted: it is a fakematch). Variant files: ro.c block/block_pro7order, ro2.c addr_expr, ro3.c ad/ad_swap. (Y_TOC REVIEW.md; source TODO)
  - R4: the type of the `exprtype` parameter (UInt8/int/short/SInt32/char/UInt16), and `expr` typed as `void*`/`char*`/`EMemberInfo*`/`Object*` and cast at each use. Results not recorded. The prompt said to "try only new ideas, time-box". (R4_Back2 rogen.py/rogen2.py)
  - Near-miss round 1 / 3: no source tried flipped the one spill choice (2.6 spills `expr`, keeps `left_sub` in ebx). Tried: declaration order and scope, split or case-scoped `tmp`, dead locals, a named copy of `expr`, gc1's whole body, inline promotion, `-sym off`. Matching would take eight extra uses of `left_sub`. NM_TOC variants: blk/blk_ord, vol/addr. (AGENTS.md TOC bullet; NM_TOC work)
  - Rewrite round 1: a declaration-order search found nothing better than 60.7. (dec_ro.txt)
- Leads: an extra local (one, or three) gives `expr`'s reloads: look for a real one the original had (a block-local, a split of `tmp`), which might also be what makes `reuse` address-taken. Something that takes `reuse`'s address (an inline helper taking `ENode**`?) is still the only mechanism known to reload it.
- Blocked by: nothing recorded.
- Behaviour: checked equivalent path by path against IDA, Ghidra and Binja. (AGENTS.md TOC bullet; Y_TOC REVIEW.md)
- Pattern: reload after store (as if address-taken, host-compiler state depends on the locals' count); register pair swapped (esi/edi); size 16 bytes less; equivalent checked: yes.

## src/backend/BE_elf.c (does not link: 2 inexact)
### BackEnd_WriteElf (0x004db860): 99.5%
- Differs: one register. The `ELFfile.section_list` temp (`mov X, [_ELFfile+0x8]; mov eax, [X]`) is edi in ours and esi in 2.6.
- Tried (newest first):
  - NM5 (Oct 16, NB work/we1.py): `section` split into fresh block locals in each of its three regions (the
    symtab stores, the section-list loop, the header loop), in every combination, with and without the shstrtab
    loop's `s` merged back into `section`, and with the header loop's `index` or the final `offset` split (40):
    all 99.5. The unit compiled with pro5 or the real pro53 (not pro6): 63.6. Class: renaming only (the
    `ELFfile.section_list` temporary in edi where 2.6 has esi).
  - Y_ELF: decl-order `--swaps`, direct symtab stores, loop, declaration and scope variants, and Wii's helper split. Variant set bew_v.c: last/fs2/helper/direct/ifless/declorder/blockreloc/ptr. The adversarial review tried 4 natural spellings to replace `sym off`; none worked. `is_big_endian`/`WriteSectionData` written in place gave 82.7-96.0. (Y_ELF REVIEW.md, work/bew_v.c)
  - R4: the section-list loop with a block variable `s`/`sec` (first loop, both loops), `section = list; for (section = section->next ...)`, a comma init, a while loop, `!= NULL`, the `allrelocations` test dropped, a symtab block, a nested `kind < 10` test, a reloc local, for->while, the `rela` computation, the reloc block, and `e_shoff` chained/offset/length forms. Results not recorded beyond the function staying at 99.5. (R4_Back2 wegen*.py, we.diff 99.5)
  - Rewrite round 2: per-function `#pragma sym off` took it from 85.6 to 97.0. `-sym off` for the whole file cost five others (later recorded as eight). With symbols on it is 86.7 and 8 bytes short. (AGENTS.md rewrite round 2; source TODO)
- Leads: the source TODO says "what in 2.6's source makes [the -sym off register choice] so is not yet found".
- Blocked by: nothing recorded. The unit's statics stay global (bound) while this function and InitElfFile are lifted.
- Pattern: one register (esi/edi) for a temp; per-function `sym off` workaround in place; equivalent checked: yes (AGENTS.md near-miss round 4 "Left ... each checked equivalent").

### InitElfFile (0x004dc290): 91.7%
- Differs: frame only. 2.6 puts the 257-byte `filename` buffer at esp+7 (the frame's top, esp+7..0x108). Ours puts it at esp+4, so every buffer reference is off by 3 (`lea eax,[esp+4]` vs `[esp+7]`, `[esp+ebx+5]` vs `+8`).
- Tried (newest first):
  - NM5 (Oct 16, NB work/ief1..ief5.py): measured pro6's frame layout with probe bodies: locals are placed
    top-down and every local of 4 bytes or more is 4-aligned (char[n] for n = 8..261 lands at frame_top -
    round4(n); a later `char` goes into the rounding hole above the array, `char a; char fn[257]` puts a at 3).
    2.6's buffer is at frame_top - 257, so the original's 257-byte object was not 4-aligned. Struct and union
    wrappers (`{ UInt8 len; char name[256]; }`, `{ char s[257]; }`), `#pragma pack(1)`, `options align=packed`
    and `mac68k` around the type, an array of 1-byte structs, `char fn[1][257]`, `volatile`, a pointer copy, a
    block scope, unsigned char, extra char/short locals before it: all at esp+4. The unit with pro5 or the real
    pro53: 78.4. Class: local (the buffer's offset: 6 instructions, `lea`/`movsx`/stores 3 bytes apart).
  - Y_ELF: 16 more declarations, sizes and scopes, as gen_ief.py variants: uchar, dead `char c` before, dead `short c; char d`, a 3-byte second array before or after, length as char/short/int, sizes 255 (Str255)/256/258/259, `filename[2][257]`. Also per-function `#pragma sym off` with the reset at two positions (does nothing). Earlier in Y_ELF: a struct, a length-plus-name struct, pack(1), `align=packed`, declaration order, an inline p2cstr helper. The adversarial review left the frame "unexplained". (Y_ELF REVIEW.md, work/adv/gen_ief.py, s4.py)
  - R4: unsigned/signed char, buffer last/middle in the declarations, `length` as short/int/char, a dummy SInt32, no `length` local; a struct wrapper, a {len; s[256]} struct, `[256]` plus a pad char, `256 + 1`; sizes 256/258/259/260/263, `[256]` plus `extra`; pragmas pack(1), pack(2), `options align=packed`, `align=mac68k`, `optimize_for_size on`, `optimization_level 1`. Results not recorded (the function stayed at 91.7). (R4_Back2 iegen*.py)
  - Rewrite round 2: added 2.6's local name buffer with the 255-character check (error 543), taking it from 79.5. (audit-2.6.md; prompt_R_BEelf.md)
- Leads: AGENTS.md (round 4): "MWCC x86 4-aligns every local array of 4+ bytes (pro5/pro53/pro6); InitElfFile's `filename[257]` at esp+7 is unexplained."
- Blocked by: nothing recorded.
- Pattern: stack slot of a local (array at a non-4-aligned offset); equivalent checked: yes (AGENTS.md near-miss round 4 "Left").

## src/optimizer/FunctionCalls.c (does not link: 1 inexact)
### analyze_arguments (0x005294f0): 92.5%
- Differs: register pair swapped through the whole function. The `used_regs` parameter (loaded at +7, used in the clear loop and every `or [used_regs + n], eax`) is in ebx where 2.6 has esi. The `type` pointer (`[eax+6]`, every `cmp [type+2]` and `cmp byte [type+6], 0x13`) is in esi where 2.6 has ebx. Same size and instructions. `_@1276` (the CError_FATAL file literal) is unplaced while the function is inexact. Statement by statement equal to IDA (Y_ELF REVIEW.md); equivalent.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): local copies of each parameter and pair (42 variants): `used_regs`, `funcref`,
    `resultHasFloats`, `has_varargs` 92.5 (no change); any copy of `arg_expr` or `arg` 80.6-81.9 (16 bytes short).
    Class: renaming only (`used_regs` ebx/esi against `type`).
  - Review round Y adversarial (Y_ELF REVIEW.md "Adversarial review"): retried the swap in 8 variants, none closer. Y_ELF work/adv/aa.c: `type_block`, `type_block2`, `type_after_info`, `type_from_info`, `clear_while`, and `clear_int`/`clear_short`/`clear_char` for the clear-loop counter (results 59-92.5).
  - Review round Y (Y_ELF REVIEW.md, docs/review-2.6.md):
    - decl-order --swaps;
    - Pro7's local order (81.1);
    - block-scoped `spilledVectorFlag`;
    - an array parameter for `used_regs`;
    - a `while` clear loop;
    - block-scoped or late `type`;
    - int/short/char counters (59-92.5).
    - Y_ELF work/aa_v.c `blocktype`, `blockspill`, `u32arr`.
  - R4_OptOne (near-miss round 4), results not recorded:
    - aa.c: `blocktype`, `regs_first`, `regs_last`;
    - aa2.c: 1-9 dead locals, first or last;
    - aa3.c: `si_a1`, `si_a2`, `s_a1`, `s_a2`, `split`;
    - aa4.c: `voidtype1`-`voidtype3` (`type` as void*), `typetwice`, `tmpnode`;
    - aa5.c: parameter types: `used_regs` as int*/SInt32*/UInt32[]/long*/unsigned int*, `resultHasFloats` as char*/UInt8*/int*, `has_varargs` as Boolean/int/UInt8;
    - aa6.c: `typelate`, `infoinv`, `chain`.
  - N3_GlobOpt (near-miss round 3), results not recorded:
    - aa.c: `typeblock`, `typeblock_top`, `typeblock_init`, `usedregs_arr`, `rclass_int`, `rclass_char`;
    - aa2.c: v001-v111, 7 combinations of three toggles;
    - aa3.c: `used_regs` as SInt32*, `type_first_use`, `while_break`;
    - so2.py: per-function `#pragma sym off`.
  - NM_globalopt (near-miss round 1), aa*.c and aa_spec*.py, results not recorded:
    - hoist0-hoist10 (declaration hoists);
    - `hf_first`, `type_after_make`, `param_int_hv`;
    - `type`/`spilledVectorFlag` block-scoped, `rclass` as int or char;
    - counter types SInt32/int/unsigned int/unsigned long/long long, `hf_char`, `hf_bool_int`;
    - comma-expression forms (`mk_comma`, `skip_comma`, `type_comma`, `init_chain`, `hf_false`, `pair_comma`);
    - `displ`/`arg_size` as int, each of the gpr/fpr/vr counters as SInt32 or short, `spilledVectorFlag` as char or int;
    - `comma_hf`, `int_cast`, `char_cast`, `init_comma`, `loop_after_hf`.
  - Rewrite (AGENTS.md "The back-end global optimizer is Pro7's", Oct 15): rewritten from Pro7 plus IDA's 2.6 pseudocode, 37.7 to 92.5. Found then: IDA drops the code after `CError_FATAL(398)` (treated as noreturn), but 2.6 keeps Pro7's aggregate branch after it; IDA also drops pushed arguments now and then (`has_varargs`).
- Leads: nothing recorded.
- Blocked by: nothing recorded for the function. Unit-level, the relink also reports .bss 20 bytes against a 32-byte range: 12 unreferenced bytes before IroMalloc, unexplained (Y_ELF REVIEW.md). Headers are PENDING: InstrSelection.h's `Operand.immOffset`/`immediate` are `int`, where AGENTS.md records Pro7's `SInt32` as needed by Operands' combine.
- Pattern: register pair swapped (parameter `used_regs` against local `type`, esi/ebx); equivalent checked: yes.

## src/backend/Operands.c (does not link: 1 inexact)
### I8_extract_bitfield (0x004e1000): 83.8%
- Differs: ours is 1340 bytes against the original's 1339. The registers are permuted (esi/edi, edi/ebx, ebx/esi). 2.6 keeps `lo1`, `lo2` and `sh` in stack slots and updates `bitlength -= 32` in its slot where the statement is (ours propagates `bitlength - 32` into the following uses and moves the update down). 2.6 tail-merges the first signed path's srawi into its sibling (ours does not), reloads nothing at the end (ours reloads `output` after each store), and does not merge the second path's srawi with the third case's. Class: structural (tail merges) plus registers.
- Tried (newest first):
  - NM5 (October 2026):
    - The locals' count switch (see TOC.c's rewrite_opassign): 2, 4 or 6..25, 27..39 extra unused `int` locals put
      it in another state (1340 -> 1332 bytes, 83.8 -> 84.0): the reloads of `output` at the end go (2.6 has none),
      and the first signed path's srawi tail-merges into its sibling like 2.6's; but the second path's srawi then
      merges into the third case's tail, which 2.6 does not do. Not adopted (dead locals). In that state:
      `decl-order --swaps` finds nothing; the integer-type greedy search takes `hi` to short (85.2, not kept).
    - Operand orders (`bitlength + offset`, `(bitlength + offset) & 31`, `(bitlength + tmp) & 31`, `64 - end`), all 16
      combinations: none better (83.8 at best).
    - Pro7 extract_bitfield's style (an else-if chain, each signed path with its own `srawi regHi`, no gotos): 73.3
      (the duplicated srawi calls are not tail-merged).
    - Else-if in place of `goto done; } if (offset == 0)`, an inverted `offset != 0` test: 83.8, 82.7.
  - Y_TOC: short `reg`/`regHi` (worse); `short_tmp`. (Y_TOC REVIEW.md, work/i8e.c; source TODO)
  - R4: one shared `srawi` after an if/else that picks the source register, in the first, second or both signed paths (eb_var first/both/second). Also flipping each local between int and SInt32, all int, and all SInt32. 81.0 -> 83.8 by the end of R4 (which change gave it is not recorded). (R4_Back2 ebgen.py, ebtypes.py, eb.diff)
  - N3: a variable-merge search (`reg_into_end`, `offset_into_lo1` and so on) and ext/ext_v1 variants: 81.0. (N3_ISel work/ext*.c, I8_extract_bitfield.diff)
  - Rewrite round 1 / audit: the 1.x-style goto body was replaced; it was 61.9 before. (audit-2.6.md)
- Leads: 2.6 keeps `bitlength` unpropagated: IroPropagate's IsPropagatableExpr refuses a non-regable variable, so a local whose address is taken (or that the host thinks is) would explain the in-place update; the extra-locals state fixes the end reloads and the first tail merge, so the original likely has more locals than ours (find real ones: separate variables where ours reuses `tmp`, `lo`/`lo2`).
- Blocked by: nothing recorded.
- Real bug in the original: the offset >= 32 branch tests `offset == 0` (the `cmp` reads `offset`'s slot), which cannot hold there, so the srawi-only path is dead. `tmp == 0` was probably meant. Ours reproduces this, with a comment. (Y_TOC REVIEW.md)
- Behaviour: checked against IDA. The only difference is that 2.6 shares one srawi between paths. (Y_TOC REVIEW.md)
- Pattern: register permutation; locals updated in their stack slots (2.6) vs propagated (ours); tail-merge differences; host state switch by the locals' count; size 1 byte more; equivalent checked: yes.

## src/optimizer/LoopOptimization.c (does not link: 2 inexact)
### unrollloopconditional (0x00574db0): 88.0%
- Differs:
  - The body-copy loop: ours loads `instr = firstInstr->nextPCode` (`mov edi, [ebx]`) before jumping to the test. 2.6 keeps `firstInstr` in the walker's register and enters the loop at its step (`jmp` to `mov ebx, [ebx]`), so it has one instruction fewer there. Later branch targets shift by 2 and one alignment pad differs (`nop` against `lea eax, [eax]`).
  - Registers rotate in several later loops: edi/edx at +0x337, and the edx/edi/ebp, ecx/edx/esi walkers in the successor/predecessor list loops at +0x405..0x613.
  - Equivalent (Y_BE1 REVIEW.md).
- Tried (newest first):
  - NM5 (agent NC, Oct 16): the body-copy loop's shape is settled: 2.6 walks it in firstInstr's register and enters at
    the step, which is `instr = firstInstr; while ((instr = instr->nextPCode) && !(instr->flags & fIsBranch))` (the
    loop then matches instruction for instruction), but the whole frame comes out 8 bytes smaller (0x48 against
    0x50: two unreferenced slots of 2.6's frame go), so the score drops to 69.3, as Y_BE1 saw. The same with a
    separate `bodyInstr` walker in all 19 declaration places, with `firstInstr` walking itself, or with `instr` for
    both (firstInstr gone): 69.3 or worse; decl-order --swaps on the while form: 69.3. So 2.6 has the while form plus
    two more locals (or temporaries) that keep slots without being read; not found. Class: local (this loop's walker
    and entry) plus renaming in the later loops.
  - Review round Y adversarial (Y_BE1 REVIEW.md):
    - a separate `bodyInstr` walker in three declaration positions (88.9);
    - `firstInstr` itself as the walker (88.0);
    - `while ((instr = instr->nextPCode) && ...)` and the `for(;;)` equivalent (69.3).
    - Y_BE1 work/adv/v_ulc.c `body_instructionCount`, `body_instr`, `body_firstInstr`, `copyvar`; v_ulc2.c A/Ab/B/Bb/C/Cb/D/Db.
  - Review round Y (Y_BE1), the "earlier" tries listed:
    - a target-block local;
    - the walker reused, or `firstInstr` as the walker;
    - `while` loops for the copy (66-69);
    - `op`/`opID` hoisted;
    - decl-order --swaps.
    - The body is Pro7's text (work/v_unrollloopconditional.c `pro7`).
  - R4_Iro (near-miss round 4), results not recorded:
    - v15.c: `fi` (firstInstr as the walker in a `for`), `fi2` (the same as a `while`), `instrfirst` (`instr = ...; firstInstr = instr;`), `noFI`;
    - v16.c: `#pragma sym off` on the function, and with the reset at the end;
    - v18.c: the block-local `op`/`opID` shared at function scope (`shared`, `shared_top`).
  - Oct 14 audit: 88.0 "shape" (audit-2.6.md).
- Leads: nothing recorded. Ninji's placeholder local names (blockA, instr28, instr6, reg25, label24) remain: cosmetic only.
- Blocked by: nothing recorded. The unit is deferred, so neighbours may couple (AGENTS.md).
- Pattern: loop entry form (preloaded walker against entry at the step: 1 instruction fewer in 2.6); register rotation in later loops; equivalent checked: yes.

### unrollloop (0x005755c0): 98.1%
- Differs: one register. `factor` (loaded `[esp+4]`, used by `idiv` and `imul`) is in edi where 2.6 has esi. Equivalent (Y_BE1 REVIEW.md, "one register, `factor`'s copy").
- Tried (newest first):
  - NM5 (agent NC, Oct 16): a local copy of the parameter `loop` (first or last): 98.1. Class: renaming only
    (the in-loop copy of `factor`: edi where 2.6 has esi; nothing else differs).
  - Review round Y (Y_BE1 REVIEW.md): decl-order --swaps; one or two walkers; `SInt32 factor`. Y_BE1 work/v_unrollloop.c `pro7`. Two walkers for Pro7's one `instr` are needed: Pro7's single `instr` is 97.1, and that and the plain assert account for the rest (Y_BE1).
  - R4_Iro (near-miss round 4; AGENTS.md round 4 "a fresh one where Pro7 reuses (unrollloop's walkers)") took it from 97.1 to 98.1 with separate walkers (`bodyInstr`, `blockInstr`). Variant sets (results not recorded except the outcome):
    - v9.c: `assert`, `nextvar`, `while`, `fi_loop`, `blockinstr` and assert combinations;
    - v10.c: 16 walker-name pairings (instr/nextInstr/pc/pc2);
    - v11.c: block-scoped walkers (`blk_*`);
    - v12.c: the factor arithmetic and types (`swapmul`, `nested`, `notmod`, `factor_i`, `sfactor`, `si`, `short`);
    - v13.c: declaration-order permutations of the walkers;
    - v14.c: 0-9 dead PCode* locals after `pc2` (d) or before `newBlock` (t);
    - v34.c: forms of the factor search loop (`nested`, `while`, `notcond`, `cont`, `cmpswap`, `gt1`, `predec`);
    - sym_unrollloop.c: `sym off` in two placements.
  - Oct 14 audit: 97.1, with literal opcodes 82..85 and `oalloc(0x2c)` for Pro7's PC_CMP.. and `sizeof(PCodeBlock)` (since replaced by the names).
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: single register (edi/esi for `factor`); equivalent checked: yes.

## src/optimizer/IroPropagate.c (does not link: 1 inexact)
### IRO_CopyAndConstantPropagation (0x004702d0): 99.7%
- Differs: one reload. At +0x64a the original reloads the spilled `linear` from `[esp+0xc]` before `linear->u.node = enode` (the store `[eax+0x20]`, right after `linear->type = IROLinearOperand`). Ours reuses the register. The only other differences are the alignment padding after it (`nop`, `lea eax,[eax]` against `mov eax,eax`) and the jump displacements it shifts. Same size. The relink differs only inside this function's .text (Y_IRO REVIEW.md).
- Tried (newest first):
  - NM5 (agent NC, Oct 16): the parameter-copy rule found for DoTransformations24 (a parameter that is the source of
    a propagated copy is kept in memory) does not carry over: `linear` is a local, and small pro6 tests show copies of
    a spilled local never cost a reload. Tried, all 99.7 (no change): a block-local copy `IROLinear* nd = linear`
    around the three stores (blk3), around the last two (blk2), and declared before `enode` with all its uses
    (blkall). Small tests could not reproduce a spilled loop walker at all (pro6 always gives it a register). Class:
    local (one extra `mov eax,[esp+0xc]` before the `u.node` store, and the padding it shifts).
  - Y_IRO adversarial review (Y_IRO REVIEW.md / docs/review-2.6.md 4757; work/adv/cacp_v.txt): comma expressions (comma1, comma2), `chain`, `tmpl`, and a block-local walker copy. No change. Held as a register-only TODO.
  - Y_IRO review (REVIEW.md 9; work/cacp.txt, cacp2.txt, v.txt):
    - store variants: `uchar`/`char` (a byte-typed store), `swap` (the stores in another order), `nopfirst`;
    - `enode` placement: `fnlevel_first`, `fnlevel_both`, `fnlevel_top`, `blockdecl` (`enode` at function scope or declared apart);
    - a block-local `newass` (v.txt "block"): 97.5;
    - Pro7's declaration order: 90.6. `newass` at function scope in 2.6's order is needed for the rest;
    - a decl-order search: nothing better.
  - R4_OptOne (prompt_R4_OptOne.md; work/icp*.c):
    - `comma`, `comma2`, `chain`, `node_cast`;
    - `type_ptr`, `dowhile`, `order_nop_after_enode`, `enode_tmp`;
    - static inline setter helpers `SetNode2` (h1, h2, h3);
    - `enode` declared first, after `linear`, or last (fn_first, fn_afterlinear, fn_last);
    - per-function `#pragma sym off` (symoff).
    - Results not recorded. The function is listed in AGENTS.md's round-4 "Left (register or frame only, each checked equivalent)".
  - NM_misc, near-miss round 1 (work/ip*.c): `comma`, `comma2`, `diadic`, `cast`, `tmp`, `order`, `label`, `vol`, `monadictmp`, `nodeflags`, `addr`, `enodeaddr`, `nodeptr`. Results not recorded.
  - Audit, Oct 14 (audit-2.6.md 3537): 97.5 then. "The body is Pro7's."
- Leads: audit-2.6.md 3547: "try an enum-typed field or Pro7's local order". Pro7's local order was tried (90.6). An enum-typed field (`IROLinear.type` as an enum, so the store through it forces a reload) is not recorded as tried. The `uchar`/`char` byte-store variants are the nearest attempt.
- Blocked by: nothing recorded outside the function. It is the only function keeping the unit from linking (review-2.6.md 213).
- Pattern: reload after store (as if address-taken); equivalent checked: yes (Y_IRO "Equivalent", AGENTS.md round-4 Left list).

## src/backend/StackFrame.c (does not link: 4 inexact)
### compute_interrupt_frame (0x004f6490): 92.9%
- Differs: registers only, same size. In the need_link_register branch the first counter/`count`/`offset` are in
  esi/ecx/edi where 2.6 has edi/esi/ecx; `save_slot_size[GPR]` and `n_scratch_registers[GPR]` loads are in edx/eax
  where 2.6 has eax/edx (the instruction order of the two loads also differs by one byte); in the masking loop the
  `1 << reg` value and the counter are swapped (esi/edi).
- Tried (newest first):
  - NM5 (Oct 16, NB work/cif1.py, cif2.py): the three statements before the need-LR loop (`count`, `offset`,
    `size`) in all 6 orders x 120 declaration orders: 92.9; each of `rclass`/`count`/`i`/`reg` split into fresh
    block locals of the need-LR branch, the else branch or both (45 combinations): best 92.9; decl-order --swaps:
    none better. Note the inlined need_link_register at the top already differs (its temporary in esi where 2.6 has
    edi), while generate_traceback's inlined copy matches. Class: renaming only.
  - Z_FRAME review (Oct 16): checked equivalent statement by statement against IDA's pseudocode (need_link_register inlined as makes_call then the three use_helper_function calls; the effect test `kind == PCOp_REGISTER && effect & EffectWrite`; masks 0x801/1; the dead `count` in the need-LR branch is the original's too). The functions and statics made `static` (Pro7's): no change, 92.9.
  - R4 (R4_Frame work/cif_v.c, cif_v2.c, sf_929.c): reading `save_slot_size[rclass]` at each use with the
    round-up written `(size + slot - 1) & ~(slot - 1)` instead of a `size` local and ALIGN: raised it from 81.5 to
    92.9 (kept; source comment at StackFrame.c:2129 says ALIGN or a `size` local give other code). Also `symoff`
    (per-function `-sym off`), `arr`, `m1`..`m4` (result not recorded).
  - N3 (N3_StackFrame work/cif*.c, ts_cif.c, cif_do.txt): decl-order: "best 81.9: no order does better"; variants
    `wo`, `gt`, `lwo`, `noloc` (written-out vs inlined update_frame_align, comparison spellings), `r`, `s`, `t`,
    `mask`, `direct`, `direct_noc`, `sz_s32_i`, `p0`..`w11` (twelve per-site alternatives); a type sweep of `rclass`
    (int, SInt32, short, UInt32, char, UInt8, SInt16), `count`, `i`, `offset`, `size`, `reg` (result not recorded).
- Leads: nothing recorded.
- Blocked by: nothing recorded. The function's name is invented (TODO in the source).
- Pattern: register assignment (three-way rotation esi/edi/ecx, eax/edx swap); equivalent checked: no record.

### compress_data_area (0x004f71a0): 73.8%
- Differs: registers only, plus a one-byte shift. `r0` and the computed limit live in ecx where 2.6 has ebx; the
  `frame_alignment - 1` mask is edx where 2.6 has eax (and our `mov edx, [frame_alignment]` comes after the first
  `add`, one byte later); the list-merging pointers (`local_objects[1]`, `[2]`, `local_objects_tail[1]`) are in
  ebp/esi/ebx/edx where 2.6 has ecx/edx/ebp/esi. No missing or extra instructions shown.
- Tried (newest first):
  - NM5 (Oct 16): decl-order --swaps on today's state: none better than 73.8. Class: renaming only (as Differs).
  - Z_FRAME adversary (Oct 16, work/adv/v/compress_data_area.txt): IDA reads the `r0` choice as `else if (parameter_area_size >= 0) r0 = parameter_area_size; else r0 = 0;`: that order 72.1, `> 0` 72.1, ternaries `>= 0 ? p : 0` 72.3 and `< 0 ? 0 : p` 73.8 (kept the if/else). The list merge and limit formula checked against IDA: same. Registers only (the `mov eax, [mem]` short form explains the one-byte shift).
  - Z_FRAME review (Oct 16, work/var_cda.txt): Pro7's `r7` local with 2.6's terms (`r7 = ALIGN(r0 + interrupt_save_size + 8, fa)`, and `- 8` then `8 + r7`), Pro7's nested `if (r0 < 0)`: all 71.9 (402 bytes); `r0` reused for the inner ALIGN: 73.8. With the statics `static` it is 73.8 (was 73.1). Pro7's own text says "doesn't quite match" here too.
  - R4 (R4_Frame work/cda_v.c): `i`, `r7`, `r7b` variants (result not recorded).
  - N3 (N3_StackFrame work/cda.c, cda2.c, ts_cda.c, cda_do.txt): decl-order: "best 75.4: no order does better";
    variants `r7a`, `r0re`, `r7b` (renamed/reused limit variable), `symoff` (per-function `-sym off`); type sweep of
    `r0` (int, short, UInt32, char, UInt8, SInt16) and `i` (SInt32, short, UInt32, char, UInt8, SInt16) (result not
    recorded).
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: register assignment (several locals rotated); equivalent checked: no record.

### generate_traceback (0x004f83a0): 98.4%
- Differs: registers only, in the vector-argument block. 2.6 loads `TYPE_FUNC(func->type)->args` into esi and copies
  it to ecx (the varargs walk) and later to edi (the vec-count walk) and tests esi; ours loads into ecx, copies to
  eax and tests eax.
- Tried (newest first):
  - NM5 (Oct 16, NB work/gt1..gt4.py): the walker assignments in four forms (`scan = ...; args = scan`, the
    reverse, both chained) x all six orders of `args`/`scan`/`arg`: best 98.4 (Pro7's order 92.1, 755 bytes);
    every subset of the vector block's 7 locals declared in the block (127): 98.4; the count walk as `for`/
    `for (; args; ...)`/`while (args)`/`while (arg)` x the varargs walk as `while`/`for` x `vec_count` UInt8/int/
    short/char (64): 98.4; a dummy local at every position: 98.4; decl-order --swaps: none better. Class: renaming
    only (2.6 keeps `args` in esi and copies it to ecx/edi; ours keeps it in eax).
  - Z_FRAME review (Oct 16, work/var_gt.txt): Pro7's `args = ...; scan = args;` 92.1; plus Pro7's `while (args)` count walk 97.0; Pro7's block-local declarations (`int vec_count`) 95.9, with `UInt8 vec_count` 97.0; the fields assigned directly (`buf->funcsize = CTool_EndianConvertWord32(funcsize)`, `work = buf->name`) 90.6 (2.6 writes through the cursor).
  - R4 (R4_Frame sf_929.c vs StackFrame.c.bak; gt_v.c): the block-local declarations of the vector branch
    (`extra`, `is_varargs`, `vec_count`, `args/scan/arg`, `type`) hoisted to function level (kept: 97.2 -> 98.4,
    inferred from the backup diff); `symoff` (result not recorded). The second walker `arg` as a fresh variable where
    Pro7 reuses one was one of round 4's "split locals" fixes (AGENTS.md near-miss round 4).
  - N3 (N3_StackFrame work/gt*.c, gt_do.txt): decl-order: "best 97.2: no order does better"; `for` vs `while`
    varargs walk; `A`/`A2`/`A3` (args then `scan = args`), `B`/`Bw` (one walker `scan` for both loops), gc1's body
    (`gc1`, `gc1b`), `blk`, `blk2`, `order1` (result not recorded).
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: register of a copied local (load/copy order); equivalent checked: no record.

### assign_local_memory (0x004f9dc0): 83.8%
- Differs: registers plus one extra instruction in the original. In the `local_data_size + (ALIGN(size, align) +
  ALIGN_REMAINDER(local_data_size, align)) < local_data_limit` test 2.6 reloads `local_data_size` into a register
  (`mov edi, [_local_data_size]; add edi, eax`) where ours adds the memory operand (`add edx, [_local_data_size]`);
  the rest is register renaming (`obj->type` pointer, size temporaries, the new ObjectList node in edi vs ebp) and a
  label padding difference (`lea eax,[eax]` vs `mov eax,eax`). Same size.
- Tried (newest first):
  - NM5 (Oct 16, NB work/alm1..alm3.py): ALIGN/ALIGN_REMAINDER redefined before the function in four operand
    orders x two remainder forms: 83.8 or 82.2 (the latter costs seven other StackFrame functions); casts on `align`
    in the remainder and/or the size ALIGN (SInt32/int/long/short/UInt32, 29): best 89.3 but 545 bytes (the casts
    break the CSE of ALIGN(local_data_size)); the test as `L + (S + R)`, `(S + R) + L`, `(R + S) + L`, `L + R + S`,
    `L + S + R`, `R + (L + S)`, the remainder written out, `limit > ...`: 83.8 at best (MWCC reorders the operands
    whatever the source order). decl-order --swaps: none better. 2.6 evaluates the remainder (ALIGN(lds) - lds) before
    ALIGN(size); ours the reverse. Class: local (the limit test, about 10 instructions: operand evaluation order,
    and so one load of local_data_size more), then renaming.
  - Z_FRAME adversary (Oct 16, work/adv/v/alm*.txt): 2.6 evaluates the limit test as Pro7 writes it, `local_data_size + (ALIGN_REMAINDER(local_data_size, align) + ALIGN(size, align))` (IDA: ALIGN(lds) kept for the store, remainder, then ALIGN(size), added, then lds reloaded and added): Pro7's operand order now in the source (83.8, unchanged; ours still computes ALIGN(size) first). Pro7's `return` after the first arm 82.5; `align` SInt32 81.4, int 79.9, SInt16 83.8; the sum without parentheses 80.5; `limit > ...` 83.2; `(SInt32)align` in the size ALIGN only 87.7 but 534 bytes (it then computes the remainder first, as 2.6 does, and ALIGN(size) before it out of order).
  - Z_FRAME review (Oct 16): statics made `static` with `#pragma pack(2)` at the end of the file (the unit's .bss now relinks byte for byte): no change, 83.8.
  - R4 (R4_Frame work/alm_v.c): `symoff`, `swapdecl` (result not recorded).
  - N3 (N3_StackFrame work/alm*.c, ts_alm.c): Pro7's body (`pro7`); chained assignments (`chain`, `a`..`d`);
    update_frame_align written out vs inlined, with `align` short/SInt32/int and the operands reversed (`wo`,
    `wo_int`, `int`, `wo_rev`); the inline helper's parameter as signed char/SInt16/unsigned short, inlined or
    written out (`inl_sc`, `inl_s16`, `inl_ush`, `wo_*`); four spellings of the limit test (`x1` sum first,
    `x2` `limit > ...`, `x3` ALIGN minus size for the remainder, `x4` a `(SInt32)` cast); `align` type sweep (int,
    SInt32, UInt32, char, UInt8, SInt16) (results not recorded).
  - N3 (AGENTS.md near-miss round 3): StackFrame's `update_*_align` helpers take `int` (Pro7 `SInt32`), which fixed
    other StackFrame functions.
- Leads: (inferred) the original reloading `local_data_size` into a register before the add suggests the operand
  order or a temporary in the limit test; the x1-x4 spellings already cover the obvious orders.
- Blocked by: nothing recorded.
- Pattern: missing/extra instruction (reload of a global), register assignment; equivalent checked: no record.

## src/optimizer/IroLoop.c (does not link: 2 inexact)
### RearrangeInvarianceInAddressExpr (0x004a49b0): 99.5%
- Differs: one instruction-order difference at the function head. The original stores `mov [esp],0` (the `elist = NULL`/`result` initialisation) before loading `FirstAddendLinear` into esi; ours loads esi first. Same size.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): on Pro7's head (94.2: loop 2's `scanlist`/`elist` swap ebx/esi), loop 2's init as
    `for (elist = NULL, scanlist = ...)`, as a separate `elist = NULL;` before the `for`, and both assignments before
    a bare `for (;`: all 94.2; parameter copies of `nd` and `list` (first or after the declarations): 99.5. Class
    (current text): local (the order of the `elist = NULL` store against `result = NULL` and the first load).
  - Y_IRO review (REVIEW.md 51; source TODO at IroLoop.c 2459): Pro7's statement order (`elist = NULL; result = NULL; for (scanlist = ...)`) fixes the head but swaps the second loop's `scanlist`/`elist` registers: 94.2. Every declaration order, loop-header spelling and split variable for the second loop was tried at 94.2. Work files: work/p7r.c, try1.c, adv/p7loop.c.
  - R4_Iro, near-miss round 4 (work/s1-s5.py, s32.py; v1-v5.c, v32.c; sym_RearrangeInvarianceInAddressExpr.c):
    - head spellings of the `result`/`elist` initialisation: separate statements in both orders, comma inits in the `for`, chained `elist = result = NULL` and `result = elist = NULL`, `scanlist` assigned before the `for`, a `while` loop, `result = NULL` moved before `IRO_InitList` (v1 v1-v7, v2 a-d, v32 a-g);
    - split variables for loops 2 and 3: `scanlist2`/`scanlist3`, `elist2`, `scannd2`/`scannd3`, crossed with both heads (v3, 96 combinations);
    - 1-8 dummy `int` locals, with and without the Pro7 head (v4);
    - every permutation of the declarations with the split second-loop variables (v5);
    - `#pragma sym off` and a variant of it (symoff, symoff2).
    - Results not recorded. The function stayed at 99.5.
  - N3_Misc, near-miss round 3 (prompt_N3_Misc.md: "94.2 now Pro7's text"; work/ri.c-ri9.c, merge_/split_RearrangeInvarianceInAddressExpr.c): the variant sets ri a-f, g-i, j-n; p1end/p1start/p2end/p2start/onlyprev/onlyscan; v0-v11; p0-p23; f0s0-f11s2; ri8 a-g; e/s split combinations (ri9). Also merges `result`<->`scannd` and `elist`<->`scanlist`, and splits of `scannd`, `result`, `scanlist` and `elist` (seg/rest). Results not recorded. The function was back at 99.5 by round 4.
  - NM_misc, near-miss round 1, on the earlier non-Pro7 text with `entry`/`previousEntry`/`tailNode`/`combinedNode` (work/ri.c-ri6.c):
    - statement reorders of the initialisation (a-d);
    - 720 declaration permutations (ri2 p0-p38...);
    - swapinit/sepinit and second-loop `scan2`/`prev2` splits (ri4);
    - `entry2` combinations (ri5) and v0-v39 (ri6).
    - Results not recorded.
  - Rewrite round 2 (prompt_R_Iro.md): 99.5 then.
- Leads: nothing recorded beyond the above.
- Blocked by: nothing recorded. With ExtractLoopInfo it keeps IroLoop from linking.
- Pattern: block/instruction order (scheduling of one store); register pair swapped in the Pro7-order variant; equivalent checked: not recorded.

### ExtractLoopInfo (0x004a4f40): 93.1%
- Differs:
  - esi/edi swapped from the induction-step block on (about +0x3b0 to +0x711), with ebp/edi at +0x544.
  - The byte counter local (flag28, `inc byte ptr`) sits at `[esp+0x18]` where the original has `[esp+0x14]` (one stack slot).
  - Same size. Otherwise only unplaced string references.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): Y_IRO's Pro7 text (eli_v.txt `pro7`, `x8` renamed `unused`) with a local copy of
    `fnode`: 65.5, the same as without; a copy on the current text: 93.1. Class: renaming only (esi/edi from the
    induction block on, one byte local one slot off).
  - Y_IRO adversarial review (REVIEW.md 75; work/adv/eli_v.txt, eli_v2.txt):
    - checked against IDA statement by statement: the `if (ind23)` test IDA drops is in the bytes, and the `||` of "Induction Used in loop", the `IRO_RenumberInts()` call and the unsigned `> 0` step test all hold;
    - Pro7's text with 2.6's changes (pro7, pro7cnt): 65.5, confirmed;
    - merging the earlier pass's split variables back (`ind` into `scanind`, `scannd2` into `scannd`, `right2` into `right`, every combination): 87.9-92.3.
    - The splits stay as the closest known (TODO).
  - Y_IRO review (REVIEW.md 52; work/v2.txt `inv`/`invdecl`, v_reorder.txt `p7_fnlist`/`p7_blocklist`/`repo`): Pro7's text plus 2.6's changes 65.6. Without the dead `hi` store (TODO at IroLoop.c 2221): 92.7. decl-order finds nothing better.
  - R4_Iro, near-miss round 4 (92.7 -> 93.1):
    - spellings of the `intval.hi == 0` test (s6/v6: plain field, `!hi`, `(SInt32)` cast, a CInt64_GetULong conjunction);
    - `(hi = ...) == 0` with `SInt32`/`UInt32 hi`, declared at the end or first, and `CTool_EndianConvertWord32` (s35/v35). The dead `hi` store kept from this is 93.1 (inferred from Y_IRO's "92.7 without it");
    - `ind` vs `scanind` in the induction block, `scannd`/`scannd2` placement in the `if (ind23)` loops and the later walk, and `counter2++` before or after the advance (s7/v7, 24 combinations);
    - an `indnd` split of `nd21` in the induction block, declared at four places (s33/v33: top, afterind, end, first);
    - `#pragma sym off` (sym_ExtractLoopInfo.c).
    - Individual results not recorded.
  - N3_Misc, near-miss round 3 (85.1 -> 92.7; work/eli.c, merge_/split_ExtractLoopInfo.c, g_all_ExtractLoopInfo.c, IroLoop_pre_greedy.c):
    - `scanind` splits against each of nd21/newnd/left19/tmp18/scannd/tmp/left/right (eli.c);
    - merges of the flag28/29/30 booleans, `counter2`/`i` into `counter27`, `scanind` into `ind23`, and the walkers into nd21/left19/right18 (merge_);
    - splits of flag29/30, ind23, obj, nd21, scanfnode, left19, right18, scannd (split_);
    - a greedy split/merge search.
    - The resulting split variables (`ind`, `right2`, `scannd2`, `right18` reused for the swapped compare) are what the source keeps. Log not recorded.
  - Audit, Oct 14 (audit-2.6.md 2059): 84.9, "Pro7 body with a spilled local: shape problem".
- 2.6 changes found: `IRO_RenumberInts()` first; `!Assigned || Used` (Pro7 `&&`); the IfNot condition read as `u.label.cond`.
- Leads: audit-2.6.md 2079: "Then the CheckLoopAddress/ExtractLoopInfo spills". The source TODO says the extra variables are "an earlier pass's search toward 2.6's registers, not evidence of 2.6's source". A rewrite from Pro7's text that finds 2.6's real locals is open.
- Blocked by: nothing recorded. IroLoop.h's IROLoop has placeholder/invented members (`x8`, TODO). Not linked to this function in the sources.
- Pattern: register pair swapped (esi/edi); stack slot of a local; equivalent checked: yes against IDA statement by statement (Y_IRO adversarial review).

## src/backend/CMachine.c (does not link: 2 inexact)
### CMach_ParseGlobalRegisterVariable (0x004bf290): 99.4%
- Differs: one register pair in the two struct copies near the end: 2.6 copies `reginfo` to `declinfo->reginfo`
  through cx and reads it back through ax for `scan->reginfo`; ours uses ax then cx. Same size (2035) and saved
  registers.
- Tried (newest first):
  - NM5 (Oct 16, NB work/pgr1.py): a fresh `RegInfo* info` for the new node (first, after `scan`, last, or a
    block local): 99.4. Class: renaming only (the two 2-byte struct-copy temporaries: eax/ecx).
  - Z_FRAME review (Oct 16): ObjRegInfo's members (common.h) as UInt8/UInt8 98.7, SInt8/SInt8 99.4, char/UInt8 98.7, UInt8/char 99.4 (kept `char`).
  - Spot fixes, Oct 16 (bisect of the 99.4 -> 94.6 drop): the cause was the storage-class range test. R4's
    `storage < STORAGE_REGISTER` compared with enumerators; after the RB_DECL rename it compared `SInt16 storageclass`
    with the `TK_` defines, which drops the `movsx` (and a callee-saved register). `(int) declinfo->storageclass <
    TK_REGISTER || (int) declinfo->storageclass > TK_EXTERN` (RB_DECL's catalogued shape) and a local enum both give
    99.4; the `(int)` form is in the source. Then for the last pair: `scan->reginfo = declinfo->reginfo = reginfo`
    (99.4), `scan->reginfo = reginfo` (99.6, but 2.6 reads `declinfo->reginfo` back, so not it), the copy after the
    other field stores (99.4), the `declinfo->reginfo` store before `galloc` (99.4), `decl-order --swaps` (no order
    better), and without the per-function `#pragma sym off` (92.0: still needed).
  - R4: per-function `#pragma sym off`: 92 -> 99.4, still in the source (TODO: the whole unit loses with -sym off).
    Variants grv_v info/info2/blk and m1 (one.c). Results not recorded. (AGENTS.md near-miss round 4; R4_Frame work)
  - N3: a greedy variable-merge search: 73.9 -> 78.9 (`reg_into_count`), no gain in round 2. (N3_Misc greedy_cm.txt)
  - NM round 1: variants a/b/c, 73.9. (NM_optimizer2 pgrv.c, pgrv.diff)
  - Rewrite round 1: written from the disassembly (a 2.6 addition with no Pro7 body; was 0.0). (audit-2.6.md)
- Leads: the types of `ObjRegInfo` (`char` members; 2.6 may have `RegClass`/`SInt8`) or of `RegInfo.reginfo`; the
  struct copy's register follows the type MWCC copies it as.
- Blocked by: nothing recorded.
- Pattern: register pair swapped (pattern 1); equivalent checked: yes at R4.

### CMach_StructLayoutPackedBitfield (0x004c04a0): 87.9%
- Differs: registers only, the same size. The register permutation runs through the whole function: cl/bl, ebx/ecx, cl/dl, edx/ebx, ecx/edi, esi/ebx, ebx/esi.
- Tried (newest first):
  - NM5 (Oct 16): decl-order --swaps on today's state: none better than 87.9. Class: renaming only.
  - Z_FRAME review (Oct 16): not retried (time); the rest of CMachine went to Pro7's text with no effect on it.
  - R4: shapes of the unnamed-zero padding mask: `u` copied before or inside the test, `(u = (unitsize - 1) & ...)`, `int u = unitsize - 1; u &= ...` (genpbf a-e), mask1-3 locals, and a `CMach_BitfieldPadding` helper for the padding and/or the unit padding with `basesize` or `unitsize` (h/h2/h3, helper's types int/SInt32/UInt16/len/eq0). 83.5 -> 87.9. The kept spelling `if (align < (u = unitsize) && basesize) { if ((unitsize = (unitsize - 1) & cmach_structoffset)) padding = u - unitsize; }` reproduces 2.6's mask computed in unitsize's spilled slot (source TODO). (R4_Frame genpbf*.py, pbf_v.c; prompt_R4_Frame 83.5)
  - N3: `available` as SInt16, nested ifs, mask locals, the padding written with `basesize` in place of `unitsize` (inl..inl4). A greedy merge search crashed with no result. (N3_Misc cm3.py, cm4.py, greedy_cm.txt)
  - NM round 1: variants v001-v111 (with keep0), b1-b5, h1a-h2c: 83.5. (NM_optimizer2 slpb*.c, slpb.diff)
  - Rewrite round 1: written for 2.6 (invented name, TODO) from the disassembly. (prompt_CMachine.md; audit-2.6.md, as fn_004c04a0)
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: register permutation; workaround spelling for a spilled-slot mask in place; equivalent checked: yes (AGENTS.md near-miss round 4 "Left ... CMachine's 2").

## src/optimizer/IroUnrollLoop.c (does not link: 1 inexact)
### UnrollWhileLoop (0x004a2c60): 89.1%
- Differs:
  - Registers only now: the frame is the original's (`sub esp, 0x98`) and every address-taken slot matches; the clearest difference is the inlined BuildLoopExitTest copies, where 2.6 keeps `LoopNode->last->type` in bl across IRO_NewLinear and ours spills it to [esp+0x10] (and reloads `_IRO_NumLinear` for the index). Alignment pads differ; 3243 bytes against 3239.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): BuildLoopExitTest (inlined three times) with a local copy of `type`, of `list`, or `nd`
    declared apart: 89.1; local copies of each parameter and pair of UnrollWhileLoop (30): 89.1, `unrollFactor`
    71.8-81.4. Class: structural (register allocation differs from +0x132 on, with one byte spilled and 4 bytes of
    encodings more).
  - Z_IRO adversary (Oct 16): the reviewer's order had result1/result2 swapped. PatternMatchLoop(scan, loop, ind, &unrollFactor, &result1, &result2, &pattern) pushes &pattern, &result2, &result1: in the original all three are `lea r, [esp+0x74]` at 0, 4 and 8 bytes pushed, so pattern 0x74, result2 0x70, result1 0x6c, and list (BuildOrigIterationCount_DoWhile's `lea [esp+0x68]` after one push) 0x64..0x6c. MWCC lays these out ascending in declaration order (checked: list,result2,result1,pattern gives 0x6c/0x68/0x70 at the call, list,result1,result2,pattern gives 0x6c/0x6c/0x6c, the original's shape 8 bytes lower), so the declaration order is list, result1, result2, pattern (Pro7: result1, result2, pattern, list). Applied; still 69.4 (the 8-byte frame shift dominates the score). With that order fixed, `fn.py decl-order` over the register locals (they have no evidence either way: Pro7's order is 72) took it 69.4 -> 87.4 and the frame to the original's 0x98 (the "two more spill slots" were register pressure, not missing locals), and `--swaps` -> 89.1 (applied). decl-order on the result finds nothing more.
  - Z_IRO (Oct 16): now 69.4 (was 76.3) by evidence, not by score: the original's address-taken locals sit at the top of its frame in the order list (0x64), result2 (0x6c), result1 (0x70), pattern (0x74..0x98), which is declaration order list, result2, result1, pattern (ours declared result1, result2, list, pattern and laid them out that way); with that order the frame is still 8 bytes short, so 2.6 has two more spill slots below 0x64 (fn19, kept in [esp+0x1c], is one of the values 2.6 spills and ours keeps in ebx). The "missing stores" of the old entry are alignment of the diff, not missing statements; calls and references are identical. Tried on top: splitting indvar (2nd/3rd use), loopExitTest and lastlabelnode into fresh variables, alone and in all combinations (46-69); all 24 orders of the four address-taken locals (66-76; pattern before list costs 300+ bytes of 32-bit displacements); a decl-order --dry-run over the rest found nothing.
  - R4_Iro, near-miss round 4 (prompt_R4_Iro.md: "low: check for a missing branch"): every permutation of the `result1`/`result2`/`list`/`pattern` declarations (s8/v8, 24 variants) and `#pragma sym off` (sym_UnrollWhileLoop.c). Results not recorded. No new exact in the round (HANDOFF.md 154). Whether a missing branch was looked for is not recorded.
  - N3_Misc, near-miss round 3 (prompt_N3_Misc.md "a frame 8 bytes larger"; work/greedy_unroll.txt): greedy split/merge search. Round 1: 76.3 -> 76.8 (`preAlignTemp_into_loopend`). Round 2: no gain (`saveTail2_into_unrolledFinalValue`). The 76.8 merge is not in the current source (76.3) (inferred: reverted).
  - Rewrite round 2 (prompt_R_Iro.md: 76.6; audit-2.6.md 2150/2154: no source before then): written from Pro7 into the deferred file.
- Leads: the frame-size and "missing stores" leads are settled (register order). Left: the spilled `type` byte in the inlined BuildLoopExitTest copies (try the copies' locals, or how `LoopNode->last->type` is read).
- Blocked by: nothing recorded. It is the last inexact row of its unit.
- Pattern: register allocation (one byte spilled in the inlined exit tests); size +4; equivalent checked: no.

## src/frontend/CExpr2.c (does not link: 2 inexact)
### scannew (0x004b63b0): 86.5%
- Differs: 2.6 keeps one call result in a stack slot ([esp+4]: stored at 0x476, reloaded into ecx before each `mov [ecx+0x12], eax` store and for the return value). Ours keeps it in edi. Ours also uses `ebp` once where 2.6 uses `ebx`, and the edi/ebx and esi/edi roles are swapped across the function. 2.6's reloads make its tail about 7 bytes longer, so the branch targets shift. audit-2.6.md read the reloads as "two extra `mov [ecx+0x12], eax` stores (a statement repeated in both branches?)". The diff shows they are reloads of the spilled pointer before the stores (inferred from the diff).
- Tried, newest first:
  - NM5 (Oct 16): `fn.py decl-order --swaps --dry-run`: 86.5, no order better. Class **local** (2.6 keeps `newESalloc` in [esp+4] and reloads it at its three uses in the inlined CExpr_NewExceptionSafeInit; with it in memory, 2.6 has a register free for `args2` and the inlined makediadicnode's flag temporary (edi); ours keeps newESalloc in edi; the rest renamed). Lead: 2.6 has one more value live there (the spill is pressure, not an address taken).
  - Z_FE adversary (Oct 16): `firstarrayexpr` made `static` (exact functions place it; lifted code reaches it through the placement) and its binding deleted: no change, 86.5. Diff checked for wrong references: none (only the spill of the call result and the jump table).
  - Z_FE (Oct 16): `(void)&newESalloc;` after the call (the "address-taken" reading of the spill): no change, 86.5. The function is Pro7's text but for `placement_flag` folded into an if/else (Pro7's two ifs: 85.2).
  - S_FRONT (Oct 16): `array_bound` renamed `firstarrayexpr` (Pro7); no code change, still 86.5.
  - R4_FrontB work (near-miss round 4; results not recorded):
    - sn.txt: pro7, pro7else.
    - sn2.txt: each local (expr, newESalloc, tempobj_expr, etemp_expr, indirect_expr, ass, cond) initialised to NULL in one or two places (null1_*/null2_*).
    - sn3.txt: every one- and two-element subset of tempobj/newESalloc/tempobj_expr/etemp_expr/indirect_expr/ass/cond moved into a block scope at `if (args2)` (blk_*).
  - N3_Front (near-miss round 3): decl-order, best 86.7, "no order does better" (do_scannew.txt).
- Leads: audit-2.6.md said "Fix scannew first: the two missing stores look like a statement duplicated in 2.6's branches". That reading is probably wrong, see Differs (inferred).
- Blocked by: nothing recorded.
- Pattern: local spilled to a stack slot by 2.6 and reloaded, where ours keeps it in a register; register renumbering.

### user_assign_check (0x004bb2d0): 97.5%
- Differs: registers only, in one region (0x4e8-0x5da) and at the `assign_node` load (0x70c). 2.6 has ebx/esi/edi where ours has esi/ebx/ebx, and 2.6 has esi for `assign_node` where ours has ebp. The jump tables are unplaced only because the function is inexact.
- Tried, newest first:
  - NM5 (Oct 16): `fn.py decl-order --swaps --dry-run` again over today's text: 97.5, no order better. Class **renaming only** (0x4e8-0x5da: the TypeFunc of `r26` in esi where 2.6 has ebx, the objectref node in ebx where 2.6 has esi, `r26->type`'s TypeFunc in ebx/edi; `assign_node` in ebp where 2.6 has esi). Not tried this round beyond that.
  - Z_FE adversary (Oct 16): `user_std_match` made `static` (binding deleted), ComparisonValues/ArgMatch members renamed: no change, 97.5. Diff: no wrong reference.
  - Z_FE (Oct 16): the natural `IS_TYPE_CLASS(expr->rtype)` for `IS_TYPE_CLASS(r18b = expr->rtype)`: 97.2 (kept the assignment; TODO-grade). Pro7's whole text (types converted): 75.8.
  - S_FRONT (Oct 16): `bestComparison` renamed `user_std_match` (Pro7); no code change, still 97.5.
  - R4_FrontB (near-miss round 4) found and fixed a real bug: `explicit` was tested on the TypeFunc, not on the Object's `qual` (AGENTS.md round 4; HANDOFF.md). The score went from 90.7 to 97.5 (inferred from the prompt's 90.7 against the current 97.5).
  - R4_FrontB work, uac*.txt (results not recorded):
    - r18b, r18b2, r14, r25, arg/arg2/arg3, ref_r18b, rt_temp, expr_r14, r15b_inline, r13_r17, ce, exprvar/exprvar2, inassert, r17, r16, r14b;
    - pro7order, pro7order_types, pro7order_intb, flags_after;
    - 18 combinations of IS_TYPE_REFERENCE assignment × r15b/r14/r25 reuse × converted_expr through r14;
    - the types of `chk`/`is_const`/`is_volatile` (int, pro7 UInt16+Boolean, u16u32, intb, u32), each with chk and expr->flags tests.
  - N3_Front: decl-order, best 90.7, "no order does better" (do_user_assign_check.txt).
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: register renumbering (esi/ebx/edi, ebp/esi); real bug found and fixed (explicit qual).

## src/backend/Peephole.c (does not link: 2 inexact)
### adjustforward (0x00501930): 68.1%
- Differs: size 3886 bytes, the original's 3854 (32 more). The frame is the same size (0x68) but our locals sit
  8 bytes higher (`[esp+0x40]` for 2.6's `[esp+0x38]`, `[esp+0x20]` for `[esp+0x1c]` ...), so slots are laid out in
  another order; a mask/shift value 2.6 keeps in edi (`cmp edi, 0x1f`) we keep in a stack slot (`cmp [esp+0x24],
  0x1f`); `opcode` is in si where 2.6 has di; several label paddings (`lea eax,[eax]`) differ; and in the
  rotate-mask computation operand loads are ordered differently (337 '~', 19 '-', 17 '+' lines).
- Tried (newest first):
  - NM5 (Oct 16, NB work/af1.py): Pro7's text without the `a` pointer: 60.2 (3822 bytes); with `short reg1` 67.2;
    the current text with `short reg1`: 63.2 (3857 bytes, the nearest size yet); decl-order --swaps: none better.
    The frame's low slots are 4-8 bytes higher in ours (`instr` 0x40 vs 0x38, `flag1` 0x20 vs 0x1c, `flag2` 4 vs 0,
    `reg1` 0x30 vs 0x28, `val3` 0x44 vs 0x40; `val2`/`val4` agree), so ours has two slots below them that 2.6
    lacks; 2.6 keeps the inlined canmergemasks' shift in edi where ours spills it to [esp+0x24]. Class: structural
    (frame layout, spills, label padding).
  - R4 (R4_PeepAsm work/af_gen.py, af_spec.py, af*.diff, Peephole.af1-3): reading the tested operands through one
    pointer `a = scan->args` computed at the top of each scan step (kept: 61.8 -> 67.8 -> 68.1; source comment at
    Peephole.c:2428); variants `all`, `conds`, `conds_first` (a in every read / only in conditions / before
    `opcode`), and `a` declared `inblock`, `inblock_last`, `inloop`. A state reading `scan->args[2]` / `[1]` in the
    find_def_backwords calls instead of `op->data.reg.reg` scored 74.8 (af3.diff) but was reverted to `op` (reason
    not recorded; mapping of diffs to states inferred from timestamps).
  - Audit (docs/audit-2.6.md): judged "register and short/int types in the mask computation; not a wrong body";
    next steps listed decl_order/types (not recorded as done).
- Leads: decl_order and integer types of the mask computation's locals (`valA`, `valB`, `start`, `end`, `val2..4`,
  the `short` flags) (docs/audit-2.6.md "Next: decl_order/types on adjustforward"); the 74.8 state above.
- Blocked by: nothing recorded.
- Pattern: stack slot layout of locals, size: 32 bytes more, register assignment; equivalent checked: no record.

### ADDI_LU_SU (0x00505b70): 98.0%
- Differs: one sum's operand loads in the other order: in the `instr->args[2] + defInstr->args[2]` computation
  2.6 loads `instr->args[2].data.imm.value` (`[ebx+0x3e]`) first into ecx, then the spilled `defInstr`
  (`[esp+0x14]`) and its value into edi, `lea edx, [ecx+edi]`; ours loads `defInstr` first (`lea edx, [edi+ecx]`).
- Tried (newest first):
  - NM5 (Oct 16, NB work/alu1.py): FITS_IN_SHORT vs FITS_IN_SHORT2 x both sum orders in it x both orders of the
    `== 0` sum x the else as `=` (both orders) or `+=` (24): best 98.0 (current). Class: local (the first sum's two
    immediate loads in the other order, 4 instructions).
  - Z_CG (review, Oct 16): the 0xba load is FITS_IN_SHORT's sum (ebx = `defInstr`, [esp+0x14] = the spilled `instr`); Pro7's `instr + def` order in FITS_IN_SHORT (94.5), plus `def + instr` in the else assignment (96.0), the `== 0` sum swapped (97.0), both (92.5), a `PCode* t = instr` copy for the defininginstruction store (98.0, no change). (work/v_lusu.c)
  - R4 (R4_PeepAsm work/lusu_spec.py, lusu_var.c): `copyF` (a `PCode* t = instr` copy used in FITS_IN_SHORT) and
    `whole` (every use through a `p = instr` copy) (result not recorded; neither kept).
- Leads: (inferred) swapping the operands of the FITS_IN_SHORT sum or of the later `instr + defInstr` sums, since
  only the load order differs; not recorded as tried.
- Blocked by: nothing recorded.
- Pattern: operand load order (register swap); equivalent checked: no record.

## src/optimizer/ConstantPropagation.c (does not link: 1 inexact)
### propagateconstantstoblock (0x00575cd0): 98.9%
- Differs: one register pair, edi/ebp swapped, in the RLWINM mask computation (+0x3f2..0x44f: the `args[4]` value against the mask temporary, both if/else arms). The jump table `@828` (lbl_005D19AC, issingleoperand's inlined switch) is unplaced while the function is inexact. Checked equivalent (AGENTS.md near-miss round 4 "Left ... each checked equivalent").
- Tried (newest first):
  - NM5 (agent NC, Oct 16): the RLWINM mask as a shared helper (it is the same expression as ismaskedoperand's):
    `rlwinmmask(PCode*)` 98.0, `rlwinmmask(SInt32 mb, SInt32 me)` with a local or two returns 97.8, each with
    `loadSize` or a `mask2` local; a local copy of `block`: 98.7. Class: renaming only (edi/ebp in the mask
    computation).
  - Review round Y adversarial (Y_ELF REVIEW.md / docs/review-2.6.md "Open"):
    - Pro7's `mask2` as a separate UInt32 in five positions (98.0);
    - `i` reused (89.4).
    - Y_ELF work/adv/cp.c `use_i`, `mask2_after_mask`, `mask2_end`, `mask2_first`, `mask2_block`, `mask2_pro7pos`.
  - Review round Y (Y_ELF): Pro7's `mask2`, `int` or `UInt32`, any position (98.0). work/pc_v.c `mask2`, `mask2_int`, `mask2_first`, `opcode`. The current source reuses `loadSize` for Pro7's `mask2` (TODO in the source, "as found"). The review judged this fakematch-grade until there is a better story: the only evidence is the register pair.
  - R4_OptOne (near-miss round 4) took it from 98.0 to 98.9; that the `loadSize` reuse came from this round is inferred. Variants, results not recorded:
    - pcb.c: `mask2` merged into loadSize, `i`, value1 or a block local; `v3` as SInt32/int/UInt32;
    - pcb2.c: `swapand`, `m2first`, `m2last`;
    - pcb3.c: `loadSize` as UInt32/SInt32/unsigned int/short/UInt16/long;
    - pcb4.c: `direct`, `ifelse`, `maskfirst`;
    - pcb5.c: per-function `sym off`;
    - pcb6.c: a-d.
  - N3_GlobOpt (near-miss round 3): decl-order, "best 98.0: no order does better" (work/pcb_decl.txt); so3.py, per-function `#pragma sym off` (result not recorded).
  - NM_globalopt (near-miss round 1), pcb_spec*.py (results not recorded):
    - in the ismaskedoperand helper: `mask_first`, `cst_noelse`, `mask_int_helper`;
    - `mask2` as int or SInt32;
    - the compare written `(mask & mask2) == mask` and `mask == (mask2 & mask)`.
  - Rewrite (AGENTS.md "The back-end global optimizer is Pro7's", Oct 15): from a 1.x body (34.0) to Pro7's plus 2.6's changes (98%). Shapes that were needed:
    - `for (instr = NULL, list = ...)` (Pro7's split form is 98.4);
    - `changed = propagatedconstants = TRUE`;
    - `immAddend = value1 = 0` (split form is 98.8);
    - issingleoperand `static inline` (proven by the data).
- Leads: nothing recorded beyond the review's "a better story" for the `loadSize`/`mask2` reuse.
- Blocked by: nothing recorded for the function. The unit's statics stay global and bound while it is lifted (the relink reports them COMMON). The statics share names with Peephole's, so they are `#define`d to `constprop_*` names. The .data cut is "plausible but unproven until it links" (Y_ELF).
- Pattern: register pair swapped (edi/ebp in the mask computation); equivalent checked: yes.

## src/frontend/CPrec.c (does not link: 3 inexact)
### CPrec_DumpColorSymbolTable (0x00517100): 92.3%
- Differs: 3586 bytes against the original's 3602. In the inlined CPrec_SetupBuiltIn, 2.6 reloads `cprec_builtin` from memory (`mov eax, dword ptr [_cprec_builtin]`) where ours reuses the register (`mov eax, ecx`). The prompt calls this "SetupBuiltIn's reload". Registers are renumbered over the rest of the function (`cprec_glist`/`cprec_header` loads in edi/edx/esi/ebx swapped), and the alignment padding differs.
- Tried, newest first:
  - NM5 (Oct 16; work/prec/hash1.c, sb1.c): class **local** plus renaming (in the inlined SetupBuiltIn's NewAddrPatch call 2.6 reloads `cprec_builtin` for the second argument with no store in between, ours reuses the register; registers renumbered over the rest; 16 bytes). SetupBuiltIn with an `SInt32` counter (92.2), the arguments taken from the array and `~x` directly (90.6), a trailing `(void)0` (92.3); AddressHashVal as Pro7's `&v` form or on its own parameter's address (89.5/91.8, and both cost 33 exact functions). Lead: the reload means 2.6 saw a store or a statement boundary between the two argument loads (NewAddrPatch's arguments set up as separate statements?).
  - Z_FE (Oct 16): not re-attempted (92.3). Related: Pro7's GetObjNameSpacePatch as a static function makes GetObjBasePatch 34.9 (static) / 48.4 (static inline): the written-out form stays.
  - R4_FrontB (near-miss round 4): only a diff was saved (CPrec_DumpColorSymbolTable.diff); no variants recorded.
  - N3_CPrec work (near-miss round 3; results not recorded):
    - e18: SetupBuiltIn shapes (sint32x, short, tmp, ptrloop, idxfirst);
    - e19: symoff;
    - e20: SetupBuiltIn/DumpColor combinations (sb1-3, dc1-3, xfirst);
    - e22: InitAddressHashTable (h1, noinline_dw, sbcount);
    - e23: one to three dummy functions inserted before it.
- Leads: nothing recorded.
- Blocked by: it inlines CPrec_SetupBuiltIn/CPrec_SetupBuiltInArray (their shape decides the reload).
- Pattern: missing reload (2.6 reloads a global, ours reuses a register); register renumbering; size: 16 bytes less.

### CPrec_GetObjectPatch (0x005192d0): 95.0%
- Differs: around the two CPrec_NewPointerPatch calls at 0x28f-0x2b0, 2.6 copies a pointer (`mov ebx, edi` … `add ebx, 4`) where ours computes `lea eax, [esi+4]`. A stack reference differs ([esp+8] in ours against [esp+4]). There is an extra `mov eax, eax` of padding in 2.6, some register renumbering (`cprec_offset` in esi against edi), and the jump tables are unplaced.
- Tried, newest first:
  - NM5 (Oct 16; work/prec/dac1-dac2.c): class **local** (2 regions: the copy of `p` kept in a stack slot sits at [esp+8] where 2.6's is at [esp+4], the same unreferenced-slot order as tquot; and in the inlined GetDefArgCtorInfoPatch 2.6 copies `p` (`mov ebx, edi`) and adds 4 for `&p->default_arg` where ours uses `lea eax, [esi+4]`, with registers renamed around it). GetDefArgCtorInfoPatch with `SInt32 p` (94.1), AppendData written out (95.0, same code), a separate result variable (94.3), a `void**` walker (94.1), the declaration split (95.0). Not tried: the shapes of the other helpers inlined here as statements.
  - Z_FE (Oct 16): not re-attempted; the unit's file top and Header names changed (code-neutral), still 95.0.
  - R4_FrontB work: gop.txt (swap, ptrp, sepp; results not recorded).
  - N3_CPrec work (results not recorded):
    - e8: dummy locals d1-d5, pfirst, ptr;
    - e9: the definition order of CPrec_GetDefArgCtorInfoPatch and CPrec_GetInlineXRefPatch relative to it (both_after, defarg_after, xref_after, swap);
    - e10: symoff/symon;
    - e5/e6: GetDefArgCtorInfoPatch shapes (ptr, ptrsep, appendalign2, mixed, retvoid, twovars, ptrint, charp, objfirst).
  - The R_CPrec Pro7 rewrite (rewrite round 2) took it from 45.8 (gc1 body) to 94.1.
- Leads: nothing recorded.
- Blocked by: possibly the inlined helpers (GetDefArgCtorInfoPatch / InlineXRefPatch) (inferred from the e5/e6/e9 targets).
- Pattern: extra register copy in 2.6 (pointer copy + add against lea); stack offset; register renumbering.

### CPrec_GetArgListPatch (0x0051dff0): 97.9%
- Differs: one register: `lst->...` at 0xcd is loaded and used in edx in ours, in ebx in 2.6 (4 lines). Same size.
- Tried, newest first:
  - NM5 (Oct 16; work/prec/al1-al3.c): class **renaming only** (`lst->name` for the inlined NamePatch in edx where 2.6 has ebx, 4 lines). `lst->name && includeNames` (operands swapped) gives 2.6's registers exactly (99.0) but tests the two in the other order; nested ifs, `!= NULL`, `!= 0`, `== TRUE`, `!(!a || !b)`, an else on the `!includeNames` store, AppendData written out: 96.9-97.9. Pro7's `FuncArg*` locals 95.8; `next` for the last NewAddrPatch 96.6. NamePatch's two statements swapped: 99.5 here but costs 20 exact functions. Lead: something makes 2.6 keep the loaded name as a variable across the test (a callee-saved register) while testing `includeNames` first.
  - Z_FE (Oct 16): not re-attempted (97.9).
  - R4_FrontB work (results not recorded):
    - al: CPrec_NamePatch inlined by hand (direct), as a static helper (np2), or as an inline helper (np3);
    - al2: 16 combinations of four toggles (f/n/a/l);
    - al3: next/ap/ap2.
  - N3_CPrec work (results not recorded):
    - e24: symoff (with GetTypeFuncPatch);
    - e25: c0n1..c4n2, for;
    - e26: blk1, reuse_next, reuse_ap, nested_name, reuse_first;
    - e27: char/schar.
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: one register (edx vs ebx).

## src/backend/InlineAsmPPC.c (does not link: 2 inexact)
### InlineAsm_TranslateIRtoPCodePPC (0x004ad650): 89.5%
- Differs: registers, a narrower counter and a spill. The SPR lookup loop counter is `short` in ours (`movsx edx,
  di`, `cmp di, 4`) where 2.6 uses a 32-bit counter (`inc ecx; cmp ecx, 4`, no movsx); in the
  warn_def_reserved_reg test ours stores the 16-bit register number to `[esp+4]` and reloads it with movsx (two
  extra instructions) where 2.6 keeps it in cx; the rest is register renaming (edx/ecx, eax/edx) and padding.
  Jump tables unplaced only because the function is inexact.
- Tried (newest first):
  - NM5 (Oct 16, NB work/tr1..tr4.py): `int i` declared at every function-level position, with `r20` also at
    every position (141 variants): every `int i` form 85.4 (1955 bytes, `r20` spilled); the SPR counter as an
    existing `reg`/`newargcount`/`buffersize`/`extra_args`/`r20`: 46.1-85.4; `int i` x `save` as SInt32/short/int or
    no local x the regHi pair's and r20's types (36): best 85.5. The `short i` form stays (89.5). Class: local (the
    SPR loop's counter width, the short spill of `dest->data.reg.reg` in the warning test) plus renaming.
  - Z_FRAME adversary (Oct 16, work/adv/v/tr*.txt, tr_i.diff): IDA checked statement by statement (cases, the psq opcode list PSQ_L/LU/LX/LUX/ST/STX, the warning test): no behaviour difference. Pro7's `int reg, regHi` locals for retain_GPR_pair 89.5 (kept: Pro7's text); `short save` 89.2; Pro7's `argcount += extra_args` loop bound 86.3; `int i` 85.4 at 1955 bytes: with it the SPR loop is 2.6's exactly (`inc ecx; cmp ecx, 4`), but `r20` is spilled to [esp] where 2.6 keeps it in edi and `src->u.reg.object` is cached in edi (2.6 reloads it); `r20` as short/SInt16/UInt16/SInt32 with `int i` 85.4-85.5; decl-order on the `int i` variant: no order better than 85.4. Lead: the `int i` form is right; find what frees edi for r20 (2.6 keeps info, extra_args, index, reg on the stack).
  - Z_FRAME review (Oct 16): not retried (time); Pro7's dead InlineAsm_GetMnemonic added before InlineAsm_OpcodeSize: no effect.
  - R4 (R4_PeepAsm work/tr_spec.py, tr_var.c, tr.diff, tr2.diff): `inti` (the SPR loop `short i` as `int`),
    `inti_reg` (also `r20` merged into `reg`), `reg` (r20 merged only); one of them gave 85.4 with 1955 bytes
    (tr2.diff; which one not recorded); none kept.
  - Rewrite round 1 (InlineAsmPPC work/tr_var*.c, symon.txt/symoff.txt): `save`, `i_top`, `r20_top`, `both`,
    `both2`; `ishared`/`iblock` (one shared or a block-local `i`); `eff`, `ishort`, `rev`, `r20init`; `int_i`; the
    SPR counter `i` as SInt32, unsigned int, char, UInt16, long and `r20` as short/long; the function-level `int i`
    declaration at six positions (`at_pc` ... `at_reg`); block-local `i` short vs int (`blk_short` kept). Whole unit
    `-sym on` vs `-sym off` (76.9 vs 76.7 at the time; unit built `-sym off`). Results otherwise not recorded.
  - Near-miss round 4 (AGENTS.md): per-function `#pragma sym off` did nothing in InlineAsmPPC.
- Leads: nothing recorded. (inferred) the original's 32-bit counter vs our `short i` is the visible type
  difference, yet `int i` variants were tried and were worse in the recorded runs.
- Blocked by: nothing recorded. The unit as a whole was checked at run time (asm test files compiled identically at
  -O0, -O4, -O4,s; AGENTS.md).
- Pattern: loop counter type (16 vs 32 bit), spill of a short local (extra store/reload), register assignment;
  equivalent checked: yes at unit level (run-time test).

### InlineAsm_ScanAssemblyOperands (0x004af0d0): 92.5%
- Differs: registers only, same size (4658). esi and edi are swapped throughout the function (operand-buffer
  addresses `lea [esp+0x54]`, the two flags set to 1 and tested, the expression pointers, the hi/lo `sar 0xf; and 1;
  add` rounding), plus a few esi/edx and edx/edi swaps. 118 '~' lines, no '+'/'-'. Literals and jump tables unplaced
  only because the function is inexact.
- Tried (newest first):
  - NM5 (Oct 16, NB work/sao1.py): decl-order --swaps: none better; every subset of hoisting the case-block
    locals to function level (the three `value`s merged, `hi`/`lo`, `mask`/`i`, `cr`, `effect2`; 31): best 92.5.
    The swap starts at the `&value` address temporary in case '%' and runs through the inlined isregisteroperand's
    two flags. Class: renaming only (esi/edi throughout).
  - Z_FRAME adversary (Oct 16, work/adv/v/sao.txt): `effect` as Pro7's short 91.5 (4674 bytes), UInt16 91.6, SInt32/unsigned int 92.5 (kept int).
  - Z_FRAME review (Oct 16): not retried (time).
  - R4 (R4_PeepAsm work/sao_spec.py, sao_var.c, sao.diff): 1-5 dummy `int` locals after `t` (`d1`..`d5`), a dummy in
    case 'S''s block (`block`) (result not recorded; none kept). R4 prompt gave 91.8, sao.diff 92.5.
  - Rewrite round 1 (InlineAsmPPC work/so_var.c, symon.txt/symoff.txt): `i`, `post`, `eff2`, `cr`, `mask`, `hilo`
    variants (result not recorded); whole unit `-sym on` 85.4 vs `-sym off` 85.9 at the time.
  - Near-miss round 4 (AGENTS.md): per-function `#pragma sym off` did nothing in InlineAsmPPC.
- Leads: nothing recorded.
- Blocked by: nothing recorded. Unit checked at run time (AGENTS.md).
- Pattern: register pair swapped (esi/edi); equivalent checked: yes at unit level (run-time test).

## src/optimizer/IroPointerAnalysis.c (does not link: 2 inexact)
### ParseLocationSet (0x0047a1b0): 97.8%
- Differs: registers only, same size and frame (0x70).
  - esi/edi swapped around the heap-block / extended-param parts (+0x454 to +0x555), and ecx/ebp at +0x373.
  - At +0x8fa 2.6 copies a call result (CreateExtendedParam's) to `thing` through a second register (`mov ebp,eax; mov ebx,ebp`), where ours uses one.
  - Plus padding (`lea` against `nop`).
- Tried (newest first):
  - NM5 (agent NC, Oct 16): the heap-block case's `hb`: initialised at its declaration, assigned before `flag37 = 0`,
    declared at each of the 13 function-level places, or replaced by `thing`: all 97.8. A local copy of every ADT
    helper parameter (IroPointerAnalysisADTs.c, 282 variants judged by inlining the file under `#line`): none moves
    this function (`others:` never lists it); local copies of its own parameters: 97.8 or worse (`rtype` 73.8-97.3,
    `createNewParamObjects` 89.3). Class: renaming only (the two IRO_malloc results of the heap-block case in
    esi/edi swapped, ecx/ebp at +0x373) plus the one extra copy at +0x8fa (local).
  - Z_IRO (Oct 16): no attempt on the body. The unit's statics are now defined (Pro7's order, `static`, 2.6's .bss layout confirmed by the comparison's placements) and all its functions have Pro7's `static` linkage: no change to this function.
  - S_FRONT (Oct 16): raw CError ids replaced by their names; no code change, still 97.8.
  - Current shape (source TODO at IroPointerAnalysis.c 5899): `#pragma sym off` for this function took it 93.6 -> 97.8. "Not exact either way."
  - R4_IroPA, near-miss round 4 (work/pls*.spec/.var, mg_*.var, ho_pls.var, pls_symon.diff):
    - combinations of where `kind`/`thing` are declared (function vs block in the `obj` branch) and `thing = &tkintconst` written inline;
    - `ep2` copies of CreateExtendedParam's result (block-local, function-level before/after, a `thing2` copy, `(void)0` after), and a `PALocalVar* lv` copy;
    - `ep` declared at function or block level, with `thing = ep = ...` or `ep = ...; thing = ep;`;
    - sym off on and off;
    - all pairwise merges of the locals (`field`/`tmp`, `block`/`restriction`, the five Boolean flags);
    - hoisting each block local (`local53`, `hb78`, `obj97`, `list98`...) to each declaration position.
    - Results not recorded. The unit went to 194/196 in the round (HANDOFF.md).
  - N3_IroPA, near-miss round 3:
    - pragma sweep over opt_lifetimes, propagation, common_subs, dead_assignments, dead_code, loop_invariants, strength_reduction, global_optimizer, optimize_for_size, peephole, scheduling, sym, unroll_loops, dont_reuse_strings, register_coloring, each on and off (prag_ParseLocationSet.txt);
    - sym on/off (ParseLocationSet_sym.txt; symscan.out: per-function sym off elsewhere did not change it);
    - 1-8 dummy locals at the start and in parameter position (dead_ParseLocationSet.txt);
    - int hoists (pls.txt);
    - ep0-3 with loc variants (pls2);
    - un-hoisted Pro7 form against hoisted/vlocal/clocal/tlocal at function or block level (pls3);
    - `blkep`, `blkep2`, `cast` (pls4);
    - a `PAHeapBlock* hb` at 13 positions, and `loc` likewise (pls5);
    - `ep2` sep/chain/castchain at function, obj and if level, `restr` reuse, `kindafter` (pls6);
    - a sanity variant.
    - Results not recorded.
  - Audit, Oct 14 (audit-2.6.md 2085): 92.1.
- Leads: none beyond the TODO's description ("2.6 copies CreateExtendedParam's result to `thing` through a second register, and swaps the heap block's registers"). AGENTS.md round-4 general advice for this kind: parameter or local typed as another pointer type and copied (`void* p` + typed local).
- Blocked by: nothing recorded.
- Pattern: register pair swapped; extra register copy of a call result; equivalent checked: yes (AGENTS.md round-4 "Left (register or frame only, each checked equivalent)").

### MatchPTF (0x00496090): 88.8%
- Differs: register allocation differs through most of the function (155 differing lines), same size (3374) and frame (0x28). The values pushed from `[eax]`, `[eax+4]`, `[eax+8]`, `[eax+0xc]` (likely the inlined PartialTransferFunction_Copy, inferred) and the walkers (ebx/edi/ebp/esi rotate) get different registers; branch displacements shift by 2.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): local copies of each of its parameters and pairs (30): 88.9. A local copy of each
    parameter of every function in IroPointerAnalysisADTs.c, the helpers MatchPTF inlines included (282 variants,
    the ADTs file inlined under `#line`, which reproduces the build): 88.9 throughout except ParamMappingFunction_Copy
    `dest` 88.0, ParamMappingFunction_AddFunctionAction `refcon` 88.4, ParamMappingFunction_AddAll `dest` 87.2; many
    break exact neighbours. Class: renaming only (walkers and the copied fields rotate through ebx/edi/ebp/esi; the
    2-byte displacement shifts are the encodings that follow).
  - Z_IRO (Oct 16): no attempt on the body. The unit's statics are now defined (Pro7's order, `static`, 2.6's .bss layout confirmed by the comparison's placements) and all its functions have Pro7's `static` linkage: no change to this function.
  - R4_IroPA, near-miss round 4 (work/mp.spec, mp.var, mg_MatchPTF.var, so_MatchPTF.var, st_MatchPTF.var, ptfc.py/ptfc.var/ptfc2.var):
    - `ptfCopy`/`mapCopy` created through block-local copies or assigned inside the Copy call;
    - `PointsToFunction_SortByExtendedParamNum(initial = ...)`;
    - `if ((result = PointsToFunctions_Match(...)))` (mp.var, combinations);
    - merging `ptfCopy` into `tgtPTF`/`ptf` and `mapCopy` into `map`;
    - `#pragma sym off` with and without reset;
    - `static`;
    - variants of the inlined PartialTransferFunction_Copy: base, ctxfield, tfirst, asrt_rev, tern, ifnot, sigswap, extra, sym, retfirst, symoff_inline, symoff_both.
    - Results not recorded.
  - N3_IroPA, near-miss round 3:
    - decl-order: "MatchPTF: 88.9; best 88.9: no order does better" (work/mp_decl.log);
    - `noresult`, `ret1`, `noinitial` (mp_v.txt);
    - 1-8 dummy locals (dead_MatchPTF.txt);
    - the full pragma on/off sweep (prag_MatchPTF.txt);
    - sym on/off (MatchPTF_sym.txt).
    - Results not recorded except decl-order.
  - Audit, Oct 14 (audit-2.6.md 2085): 88.9.
- Leads: audit-2.6.md 2114: "Then the near-misses: MatchPTF and ParseLocationSet first" (no specific idea). The body is large for Pro7's MatchPTF, so it likely inlines helpers (PartialTransferFunction_Copy, ParamMappingFunction_Copy, MatchPTFHelper/action) (inferred). The shape of those inlined helpers is the likely lever (inferred from R4's ptfc variants).
- Blocked by: possibly the inlined helpers' shapes (PartialTransferFunction_Copy, defined elsewhere in the file) (inferred).
- Pattern: register allocation throughout; inlining difference suspected (inferred); equivalent checked: yes (AGENTS.md round-4 "Left (register or frame only, each checked equivalent)").

## src/optimizer/IroTransform.c (does not link: 1 inexact)
### DoTransformations11 (0x0045c540): 96.0%
- Differs: register/spill choice, same size (2720) and frame (0x20).
  - The original keeps a variable initialised to 0 at +0x40 in a stack slot (`[esp+0xc]`) where ours uses ebp.
  - Conversely, ours spills the `left->u.monadic`/`type` value to `[esp+4]` and reloads it, where the original holds it in ebp (one load fewer at +0x2b3 and +0x300).
  - Plus padding and shifted jumps.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): `changed` as long, SInt32, short (96.0) or UInt8 (96.7, 32 bytes short); `compare` as
    long; local copies of `nd` (first, last, or only for the first test, the shape that fixed DoTransformations24):
    96.0; decl-order --swaps: 96.0. Class: local (2.6 keeps `type` in ebp and `changed` in [esp+0xc]; ours the
    reverse, so ours spills `type` to [esp+4] and reloads it once).
  - Z_IRO (Oct 16): the function is now `static` and the PickCommonsub*/Replace* helpers sit in Pro7's place: no change (96.0). Its two 2.6 changes (the `nd->stmt &&` test before the division-by-zero warning, the bitfield branch setting `tmp2` from TYPE_BITFIELD's bitlength) are real (the jump table and pushes match).
  - R4_Iro, near-miss round 4 (prompt: 97.7; work/s19-s21.py, v19-v21.c, sym_DoTransformations11.c):
    - `tmp2 = type->size * 8` (typesize);
    - 40 random declaration orders, `Boolean changed`, `Boolean compare` (v20);
    - `8 * type->size` forms, `compare = 0` init, `changed = FALSE`, `tmp1` from `nd->rtype` in two orders, `changed` set late (v21);
    - `#pragma sym off`.
    - Results not recorded.
  - N3_Misc, near-miss round 3 (work/d11*.py, s_/m_DoTransformations11.c, greedy_tr.txt):
    - `compare` computed as an if or with `!= 0` (d11 a-c);
    - `tmp2 = tmp1` or `type->size * 8` or `left->u.monadic->rtype->size * 8` (d11b, d11c);
    - a `type2` copy in three forms, and `tmp1` from `nd->rtype` directly (d11d);
    - 36 splits of `changed`;
    - merges right/left, compare/changed, tmp1/tmp2. Greedy: current 97.7, best 96.8 (`compare_into_changed`), nothing applied.
  - Audit, Oct 14: 95.5.
  - Note: R4's prompt had 97.7 and it is 96.0 now. The cause of the drop is not recorded (inferred: a later source change or the stricter reference-aware scoring).
- Leads: nothing recorded.
- Blocked by: nothing recorded.
- Pattern: spill choice (stack slot against register for two locals); equivalent checked: not recorded.

## src/optimizer/IroExprRegeneration.c (does not link: 3 inexact)
### GeneratePossibleCondAss (0x004a8f40): 78.0%
- Differs:
  - The original's frame is 8 bytes larger (`sub esp,0x58` against 0x50). The audit names two locals the original keeps in its frame that ours lacks (`[esp+0x14]`, `[esp+0x30]`); there is an extra `mov [esp+0x30],eax` at +0x13f.
  - Registers differ throughout (179 lines).
  - The original has extra loads at +0x157/+0x16b/+0x677 and a `jmp` at +0x6e9.
  - Ours has an extra `mov eax,_stsignedint`.
  - 2314 bytes against the original's 2330.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): a local copy of `fnode` (the audit's "parameter copy" lead): 77.6, 32 bytes short. Class:
    structural.
  - Z_IRO adversary (Oct 16): Pro7's text for the head (the `IRO_NodeTable[fnode->succ[0]]` tests, `node2->last` and `IRO_NodeTable[node2->succ[0]]` written out, no succ/succ0/succnode/node2last/last) 74.4 (same size): MWCC's CSE gives the same 16-bit [esp+0x18] slot, so that slot alone does not prove a named `succ0`; but the original's `mov ebx, esi` after the first load is the `node1 = node2 = ...` chain, which only the current head has, and the current head stays (78.0). The original copies [esp+0x14] into r29's slot (`mov eax,[esp+0x14]; mov [esp+0x30],eax`) and later reads `->dom`/`->first` through [esp+0x14]: that is `(r29 = succnode)` copy-propagated; written so, 76.6 (r29succnode; with Pro7's `node2->succ[0] == node1->succ[0]` as well, 75.7 and 43 bytes short). Not kept (no better match, and the current spelling is equivalent).
  - Z_IRO (Oct 16): the "two locals we lack" are there: the original keeps `node2->succ[0]` as a 16-bit value at [esp+0x18], `IRO_NodeTable[succ0]` at [esp+0x14] and `node2->last` at [esp+0x24], which is what the R4 locals succ0/succnode/node2last stand for (so they are the original's locals, not fakematches); `fnode->last` is loaded to edx and copied (`last`). The `index = IRO_NumLinear++` (post-increment, Pro7 pre-) is confirmed in the disassembly. No reference differs. `static`: no change.
  - R4_Iro, near-miss round 4 (work/s22.py, s23.py; v22.c, v23.c; gpca*.diff):
    - fixed the `r24` kills walk (`IRO_WalkTree(r24, Regen_GetExprKills)`, was `r20`), a real bug (AGENTS.md round 4);
    - `kill_hdr`: `node2last`, `succ0` and `succnode` locals for the header tests, now in the source (inferred from s22 and the current declarations);
    - `kill_hdr_ord`: the `r16`/`r15` store order;
    - `kill_hdr_ord_f`, `kill_ord_f`: the `r19flag` spelling;
    - `succnode`, `succnode2`, `nosucc` (v23).
    - No new exact (HANDOFF.md 154). The round-4 Left list says "two extra spills".
  - N3_Misc, near-miss round 3 (work/greedy_regen.txt, gpca.c, merge_/split_GeneratePossibleCondAss.c): greedy split/merge 77.9 -> 83.6 over 7 rounds (r19_2_seg 80.1, r19_s_into_left 82.6, r19_into_r18nd 83.0, r28_into_r23 83.2, r15_into_left 83.3, left_2_rest 83.6). Not in the current source (78.0) (inferred: dropped, possibly when R4 fixed the kills bug). Also splits of r24, r16, node1, node2, r19, r15, r20, r19flag, r31, cond, r28 and r14 (gpca.c).
  - Audit, Oct 14 (audit-2.6.md 3501): 77.9, "the original keeps two more locals in its frame ([esp+0x14], [esp+0x30]), a known blocker". AGENTS.md "Known blockers: GeneratePossibleCondAss' frame has two locals we lack". Already listed in the Alias round's AGENTS copy.
- Leads: audit-2.6.md 3514: "look for the two extra locals (a parameter copy or a split variable)". N3's greedy splits reached 83.6 and could be a starting point (inferred).
- Blocked by: nothing outside the function recorded.
- Real bugs: the kills walk on `r24` (fixed R4). In the same unit, `logicalType` tested `copts.no_static_dtors` for `booltruefalse` (fixed R4).
- Pattern: stack frame (two locals missing, 8 bytes); register allocation throughout; size: 16 bytes less; equivalent checked: no (behaviour bug found and fixed in R4).

### RebuildPossibleLogicalExpression (0x004a9b40): 93.5%
- Differs: same size (3442) and frame (0xc0).
  - Two locals sit in other stack slots: the original has `[esp+0xa8]` where ours has `[esp+0x58]`, and a flag at `[esp+0x88]` where ours has `[esp+0x90]`.
  - The order of a `test esi,esi` against a `cmp [flag],0` pair is swapped.
  - Alignment pads differ, which shifts jump displacements throughout.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): `somelab` declared at each of the 25 places: 93.5 (its slot does not follow the
    declaration); a dead `int`, a dead pointer, a copy of `nd`: 93.5. A local copy of the parameter `fnode` changes
    nothing here but makes AddNodeAndSuccessorsRecursively, generated after it, exact (kept, with a TODO: the copy has
    no evidence in this function). Class: local (`somelab` at [esp+0xa8] where ours has [esp+0x58], one flag's slot,
    one test pair in the other order).
  - Z_IRO (Oct 16): references and calls all agree (checked by name); `static` (Pro7's linkage): no change.
  - R4_Iro, near-miss round 4 (work/s25.py/v25.c, sym_RebuildPossibleLogicalExpression.c, rple.diff): `CLabel* somelab` declared last or first; `#pragma sym off`. Results not recorded.
  - N3_Misc, near-miss round 3 (greedy_regen.txt; merge_/split_RebuildPossibleLogicalExpression.c): greedy split/merge. Current 93.5, best 92.9 (`compareVal_1_seg`). No gain.
  - Audit, Oct 14 (audit-2.6.md 3501): 93.5, "differ in spills. The bodies are Pro7's."
- Leads: AGENTS.md's stack-slot rule ("function-level locals are laid out first in declaration order, block-locals after them ascending in source order") applies to the two misplaced slots (inferred). Not recorded as tried.
- Blocked by: nothing recorded.
- Pattern: stack slot of a local (two locals); instruction order; equivalent checked: not recorded.

### RebuildPossibleCondExpression (0x004ab050): 97.6%
- Differs: same size (3552) and frame (0xf0). Two stack slots swapped (`[esp+0x78]` and `[esp+0x7c]`) and scratch registers rotated around +0xce, +0x3fc-0x432 and +0x4cb.
- Tried (newest first):
  - NM5 (agent NC, Oct 16): a local copy of `fnode`: 97.6; local copies of every parameter of every function of the
    unit: none moves it. Class: renaming only (two slots exchanged, scratch registers rotated).
  - Z_IRO (Oct 16): references and calls all agree; `static`: no change.
  - R4_Iro, near-miss round 4 (work/s24.py/v24.c, sym_RebuildPossibleCondExpression.c, rpce.diff): the successor-label test (`r30 = IRO_NodeTable[r24->succ[0]]` then `succ[1]`) written as goto, `&&`-chain, nested ifs or else-if; `#pragma sym off`. Results not recorded.
  - N3_Misc, near-miss round 3 (greedy_regen.txt; merge_/split_RebuildPossibleCondExpression.c): greedy 97.6 -> 98.1 (`r30_into_pred2`) -> 98.2 (`r24_1_seg`); round 3 no gain (`tmp_3_seg`). Not in the current source (97.6) (inferred: reverted, perhaps as an overlapping-lifetime merge, which AGENTS.md warns a search accepts at 98%).
  - Audit, Oct 14 (audit-2.6.md 3501): 97.6, "differ in spills".
- Leads:
  - Recorded: the N3 greedy steps (`r30` merged into `pred2`, `r24` split) gave 98.2 and are worth rechecking for correctness.
  - Inferred: declaration order for the two swapped slots (AGENTS.md's slot-order rule).
- Blocked by: nothing recorded.
- Pattern: stack slots of two locals swapped; scratch registers rotated; equivalent checked: not recorded.

## src/frontend/CFunc.c (does not link: 2 inexact)
### statement (0x004f1360): 73.6%
- Differs: same size (9168 bytes). About 640 instructions differ, almost all in register assignment: the inlined CFunc_AppendStatement copies load `sourceoffset`, `sourcefilepath`, `cexcept_dobjstack`, `curstmt` and `curstmtvalue` into different registers (ebx/ecx/ebp/edx against ebp/esi/edx/ebx), and so on through the function. There are three one-sided instructions per side: a `mov edi, ebx` against a `mov edi, ebp` at another place, and padding. No wrong references.
- Tried, newest first:
  - NM5 (Oct 16): read only. Class **renaming only** in the inlined CFunc_AppendStatement copies (the five stores' temporaries take ebp/esi/edx/ebx in 2.6, ebx/ecx/ebp/edx in ours, and so on per copy). CFunc_AppendStatement is a pure-expression helper (inlined as an expression); its form is shared with many exact callers, so changing it was not tried.
  - Z_FE adversary (Oct 16): `fn.py decl-order statement --swaps` over its seven locals: best 74.1 (stmt, stmt2, block, expr, label, name, subthing), not exact, not written. The diff's only non-register lines are two spill slots swapped ([esp+0x24]/[esp+0x28]); no wrong reference.
  - Z_FE (Oct 16): CFunc.c rebuilt from Pro7's text (Pro7's CFunc_AppendStatement/newlabel/CFunc_NewDeclBlock/ifstatement/makeifstatement, all exact now). Pro7's own statement() text over them: 72.4; the previous Pro7-shaped body (label chains `stmt->label = newlabel(); label = stmt->label;`, `!copts.old_argmatch` first, `return` after the asm error) kept: 73.6. `curstmt->flags |= StmtFlag_8` is in ParseFuncDef, not here.
  - S_FRONT (Oct 16): none of its own code (create_temp_node/create_temp_node2 now pass TRUE/FALSE to CExpr_NewETEMPNode); still 73.6.
  - R4_FrontB work: st.txt "chain" (`label = stmt->label = newlabel();`; result not recorded).
  - Round 1 rewrite: the gc1 body with gc1 helpers (NewScope, RestoreBlock, ScopedBody, AppendStmt, NewLabel, FindLabel, IsLabel, CondJump) was replaced by Pro7's CFunc_AppendStatement/newlabel/CFunc_NewDeclBlock. That took it from 61 to 75% (AGENTS.md audit correction; audit-2.6.md).
- Leads: audit-2.6.md: "Rewrite statement() from Pro7 with Pro7's helpers (CFunc_NewDeclBlock, CFunc_RestoreBlock, CFunc_AppendStatement, newlabel, findlabel, checklabel), keeping 2.6's additions read from the disassembly; delete the gc1 helpers". Partly done. Whether statement() itself is now Pro7's whole text is not recorded.
- Blocked by: the shape of the inlined helpers (CFunc_AppendStatement and friends), which many exact callers share (inferred).
- Pattern: register renumbering throughout (inlined helper copies); same size.

### DestructorReturnTransform (0x004f5770): 97.3%
- Differs: at the start (the inlined NeedsDestruction / dobjstack comparison), 2.6 holds `stmt2->dobjstack` in ebp and copies `ecx = edi`; ours holds it in ecx and copies `ebp = ecx`. That is six lines, registers only.
- Tried, newest first:
  - NM5 (Oct 16; work/drt1-drt2.py): class **renaming only** (the outer `stmt1->dobjstack != stmt2->dobjstack` loads: 2.6 keeps stmt2's in ebp, the inlined NeedsDestruction's `ea2`, and copies stmt1's into ecx for the scan; ours keeps stmt2's in ecx and copies it to ebp). The comparison swapped, NeedsDestruction's assignments or declarations reordered, the scan started from `stmt2->dobjstack`, `ea1 == scan`: all 97.3; `ea2` assigned in the for initialiser 96.7; the outer test moved into NeedsDestruction as an early return (89.8).
  - Z_FE adversary (Oct 16): no attempt of its own; the unit's other changes (Pro7's dead CFunc_GetGlobalCompilerState/SetGlobalCompilerState/TempTransDestroy/DestroyReverse added, statics made `static`, CFunc_DefaultArg static): still 97.3 (unit.py check; status.py rounds it 97.4). Still Pro7's text verbatim.
  - Z_FE (Oct 16): after CFunc.c went to Pro7's text (deferred neighbours changed), 97.3 with Pro7's text and with the old body alike; nothing else tried.
  - R4_FrontB work, drt..drt7 (results not recorded):
    - the inlined NeedsDestruction helper's shape: vals, vals_stmt, swapcmp, nested, locals, cmpstmt, cmpstmt_late, ea1late, ea1while, stmt, stmt2, stmtarg, stmtarg2, helper, helper_stmt, loop2stmt, loop2stmt_ea2;
    - same, swapassign, declorder/declorder2, scanfirst, for2, scan_ea1 (repeated over several bases).
  - N3_Front work: gen_drt.py / gen_drt2.py (direct, ea2scan, scanwhile); decl-order best 97.3, "no order does better" (do_DestructorReturnTransform.txt).
- Leads: nothing recorded.
- Blocked by: the inlined NeedsDestruction (inferred from the variants' target).
- Pattern: register pair swapped (ecx/ebp) in an inlined helper.

## src/backend/InstrSelection.c (does not link: 7 inexact)

Unit-wide context (applies to every function below):
- Rewritten from Pro7 in rewrite round 1 (AGENTS.md "InstrSelection.c is Pro7's (October 15)"): deferred
  (`-inline auto,deferred`), 65/83 exact; the near-miss round 1 (NM_InstrSelection) took it to 75/83; N3_ISel and
  R4_ISel added no new exact functions here (HANDOFF.md: "R4_ISel (no new exact; gen_ASS is_gpr_float_copy inline
  helper TODO)"). AGENTS.md lists all eight as "Left", and near-miss round 3 calls them register-only, "each
  equivalent statement by statement" (AGENTS.md near-miss round 3, "InstrSelection's 8").
- R4_ISel tried a per-function `#pragma sym off` on seven of them (work/symoff_{gen_AND,gen_ASS,gen_CONDASS,gen_COND,
  gen_MODULO,gen_TYPCON,I8_gen_DIV_MOD}.c; result not recorded). Only I8_ShiftRightImmediate kept it.
- N3_ISel ran `merge.py` (merge every pair of same-typed locals) on gen_AND, gen_ASS, gen_CONDASS, gen_MODULO,
  gen_TYPCON, I8_gen_DIV_MOD and I8_ShiftRightImmediate (work/m_*.c; result not recorded).
- NM_InstrSelection had `greedy_types.sh` + `typevars.py`, which change one local's integer type at a time
  (int<->SInt32, short->int/SInt32 ...) and keep the best each round (functions it ran on: not recorded).
- Deferred coupling is recorded for this unit: "variants of gen_AND flip gen_SHR, which shares the inlined
  `shift_and_mask`" (AGENTS.md InstrSelection bullet).
- Header lead not specific to these functions: `Operand.immOffset`/`immediate` are `int` in InstrSelection.h where Pro7
  has `SInt32`. Per review-2.6.md (Operands), switching changes nothing in 2.6 except Operands' Coerce_to_register.

### I8_gen_DIV_MOD (0x00444920): 96.2%
- Differs: 16 bytes short (1348 vs 1364). 2.6 assigns the inlined I8_log2n's ternary into `shift`'s stack slot in each arm (`je; mov [esp+8], eax; jmp; mov [esp+8], 0; mov eax, [esp+8]`); ours computes the value in eax and stores it once. As a result `shift` and `tmpreg2` swap slots ([esp+0xc]/[esp+8] against 2.6's [esp+8]/[esp+0xc]). One alignment pad differs. The static `used_regs` is not placed (data only). Class: local (the ternary, 0x108-0x11a) plus the two slots renamed.
- Tried (newest first):
  - NM5 (October 2026): `int shift` makes the inlined I8_log2n's arms assign `shift` directly (2.6's `je; mov
    [shift], eax; jmp; mov [shift], 0` shape), but `shift` then lives in edi and the frame grows by 8: 78.0. `short
    shift`: 72.9. With `int shift`, `decl-order --swaps` and the type greedy search on the other locals: nothing.
    I8_log2n reshaped (if-return, if/else, no initializer): it is exact and an if-return breaks I8_gen_MUL and
    I8_gen_ADD. The locals'-count switch: no effect.
  - R4_ISel: `I8_log2n` reshaped, since it is inlined here (inferred): l2.c ifret, ifelse, ternflip, `SInt32 bit`, a
    separate assignment; l3.c an `SInt32` return with int/SInt32 `bit`, an ifret form, a cast arm. dm_v.c: `shift` as
    int, short, long long, UInt32; `I8_getbit` inlined by hand into the condition (`inl`); the test split into
    `if (EINTCONST) shift = I8_log2n(..); else shift = 0; if (shift > 0)` (`nest`). Per-function `#pragma sym off`
    (symoff_I8_gen_DIV_MOD.c). Results not recorded; nothing kept.
  - N3_ISel: dm1.c: `shift` int and short; `shift = EINTCONST ? I8_log2n(..) : 0; if (shift > 0)` (`sep`). dm2.c:
    `tmpreg1/2` hoisted to function level as short, int or SInt32, before or after `shift`, with `int shift`.
    lg.c: I8_log2n as ifret, ifelse, an inverted ternary. merge.py merges (is_signed/is_signed_right, left/right,
    opleft/opright). Results not recorded.
  - NM_InstrSelection: dmA.py hoisted `tmpreg1/tmpreg2` (short and int) to each function-level declaration position.
    dm2.txt shows one variant at 78.5 (frame 0x58); dm.txt has the kept 96.2.
  - Rewrite round 1: the Pro7-based body (var_results.txt: rewrite 72.4 against the old repo body's 77.6), then
    raised to 96.2 by the near-miss round 1 (inferred from dm.txt).
- Leads: the arms writing `shift` directly is what `int shift` gives (the int/SInt32 conversion is what keeps a temporary in ours); with it `shift` must also be spilled, as in 2.6: look for what else is live there in 2.6 (the tmpregs at function level, `constval` use).
- Blocked by: possibly `I8_log2n`'s shape (an inlined callee; it is also called from gen_MUL-side code at line 5503)
  (inferred). Nothing else recorded.
- Pattern: inlining difference (inlined ternary result through a stack temporary); stack slot of a local; size: 16
  bytes less. Equivalent checked: no specific record (near-miss round 3's "each equivalent statement by statement"
  covers the eight).

### I8_ShiftRightImmediate (0x00446b70): 89.1%
- Differs: same size (1022). Registers are permuted throughout: edx/ecx for the first `movsx`, esi/edi for `32 -
  shift`, and the ebx/ebp/esi/edi assignment of `tmpreg1..4` in the `shift <= 63 && unk` path. There, 2.6 does
  `inc [used_virtual_registers+0x10]` before loading the fourth register and storing it to [esp+8]; ours stores first
  and then increments. The prologue and frame match: shift at [esp+4] after `#pragma sym off`.
- Tried (newest first):
  - NM5 (October 2026): the natural shape (block-local `short tmpreg1..4 = ALLOC_GPR()` per case, `shift = value
    & 63`, no `m1`), with and without the `#pragma sym off`: 69.7 (int/SInt32 tmpregs 77.7); `-sym` makes no
    difference with block-local tmpregs. The locals'-count switch: no effect.
  - R4_ISel:
    - Added `#pragma sym off` around the function. The source TODO says 2.6's frame and registers are `-sym off`'s while
      the unit is `-sym on`. This took it from 81.2 to 89.1 (srI_p.out "(current): 81.2" before, srB_p.out
      "(current): 89.1" after).
    - Full decl-order permutations of m1/tmpreg1-4/shift: 720 with SInt32 and 720 with int (srB_p, under sym off:
      best 89.1, no gain), and 1440 with int types without sym off (srI_p: best 81.9).
    - srB_v.c: `m1 = shift - 1` folded into the call argument, moved before it, recomputed, or assigned inside the
      `unk` branch's argument.
    - srI_v.c: int `shift`, `shift = value & 63`, both, `value` used directly (`param`), short tmpregs, short tmpregs
      with int shift.
    - srI_v3.c: B/Bint/C/Cint, adding `m1` (SInt32 or int) and an `s32` local, with and without sym off.
    - Results other than the permutation runs are not recorded.
  - N3_ISel: sr.c (v0-v2), sr2.c (`shift` long/int/UInt32, `& 63` in one expression, `shift` declared first, `value`
    as the variable), sr3.c (`value &= 63` copy, `shift = value &= 63`, int tmpregs, a dummy `shiftm1`, `!shift`),
    sr4.c (block-local tmpregs, three placements), sr5.c (an `m1`/`shiftm1` local as SInt32/int, with orders), sr6.c
    (48 combinations: int-cast or short tmpregs x declaration order 123/231 x {separate, `& 63` at once, copy after,
    m1} x SInt32/int/long `shift`), sri_new.c (a static `shift_right_immediate` helper: out_first, intreg, shortreg,
    shiftonce), srimm.c (a, b), m_I8_ShiftRightImmediate.c (tmpreg merges). Results not recorded.
  - NM_InstrSelection: srA.py (`value` as the variable, one expression, int `shift`, each with int/SInt32 tmpregs).
    sr.txt 81.2, sr2.txt 67.8 (shift in a register), sr3.txt 45.0 (a frame of 8).
  - Rewrite round 1: var_results.txt rewrite 42.3 against the old body's 73.0.
- Leads: the source TODO says the remaining difference is "the allocation of the shift temporaries". No untried idea
  is recorded.
- Blocked by: nothing recorded. The unit is built `-sym on`; this function needs `#pragma sym off` (TODO).
- Pattern: register permutation (temporaries); order of the ALLOC_GPR increment and the spill store. Equivalent
  checked: no specific record.

### gen_COND (0x0044dcd0): 80.8%
- Differs: 16 bytes short (5873 vs 5889). The stack slots of several spilled locals are permuted (`expr1` [esp+0xc] against 2.6's [esp+0x10], `cond` [esp+0x10] against [esp+8], ...), registers differ throughout, and in the `has_const == 1` case 2.6 keeps `reg1` in a stack slot ([esp+0xc]) and writes `outputReg` into that slot for the final PC_MR, where ours keeps it in esi. The isel switch's case order and the fsel block's `final_reg` are fixed (NM5). Class: structural (spill choices and slots) plus registers.
- Tried (newest first):
  - NM5 (October 2026), kept (78.3 -> 80.8):
    - The isel block's `switch (condOp.regOffset)` cases in gen_CONDASS's order (ENOTEQU/EEQU, EGREATEREQU/ELESS,
      ELESSEQU/EGREATER): a real difference, 2.6's first block sets bit 2 (ours set bit 1 there): 78.3 -> 78.5.
    - Pro7's block-local `int final_reg` back in the fsel block (ours had merged it into `reg1`), Pro7's
      `if (op1.optype >= OpndType_IndirectGPR_ImmOffset) ENSURE_GPR(...)` for the expanded Coerce_to_register test
      in the abs block, and `int reg1; SInt32 reg2; int reg3;`: 80.8. (All 32 combinations of final_reg, the abs
      text and int/SInt32 for reg1..3 measured; this is the best.)
    - Tried after: Pro7's declaration order (`cond` first): no change (spill slots are not by declaration order);
      `decl-order --swaps`: 81.2 at best (moving `reg2`; not kept); the integer-type greedy search: 84.1 only with
      `short` fneg/fsel registers (not Pro7's, not kept); the locals'-count switch (0..4 dummies, (void)0): no effect.
  - R4_ISel:
    - decl-order: "best 75.9: no order does better; nothing written" (cond_do.out).
    - cond_v.c: the types of final_reg/fsel_reg (SInt32, short or UInt32; one, both or all).
    - cond_v2.c: `final_reg = outputReg ? outputReg : ALLOC_FPR()` assigned inside the emitpcode argument, as
      int/short/SInt32, and a separate tmp before or after.
    - cond_v3.c: `reg1 = ALLOC_GPR()` against `reg2 = ALLOC_GPR(); reg1 = reg2;`, chained `reg1 = reg2 =`, and the
      `emitpcode(PC_MR, reg2 = outputReg, reg1)` spellings.
    - cond_v4.c: `final_reg` merged into reg1 (int/SInt32), reg2 or reg3.
    - Per-function `#pragma sym off`.
    - Results not recorded beyond cond_do.out.
  - N3_ISel: cond1.c (a `tmp_reg` copied to final_reg; reg1/reg2/reg3 used for the FSEL result); cond3.c (hoisting to
    the start or end of the declarations); cond4.c (final_reg replaced by reg1 through tmp/reg2/reg3); cond5.c (six
    isel0 variants of the dst/reg handling). Results not recorded.
  - NM_InstrSelection: condA/B/C.py tried the soft-float check spelled as a block-local Type, CError_ASSERT, with
    `return`, as an else-chain, as `if (copts.soft_float) { Type* type = left->rtype; if
    (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(type)) ... }`, a function-level `type` assigned in the condition, an
    `is_float_type` call, and nested ifs. gcd.txt shows 81.4 then.
  - Rewrite round 1: var_results.txt rewrite 38.3 against the old body's 78.3. The R4 prompt listed it at 81.0 and it
    is 78.3 now. The drop (inferred) came from the later reference-aware scoring in fn.py diff or from deferred
    neighbours; not recorded.
  - Audit (pre-rewrite, docs/audit-2.6.md:975): "mostly Pro7 bodies built non-deferred; re-measure after the deferred
    rebuild" (since done).
- Leads: the remaining shape is a spill choice: 2.6 spills `reg1` in the case-1 and default isel blocks; look for what else is live there in 2.6 (another variable in the `emitpcode(PC_MR, reg2 = outputReg, reg1)` spellings, Pro7's has `reg2`). The other decompiler-shaped spots of this body (`type->type == TYPEVOID`, `flags & 1`, the expanded GEN_NODE_TO_FPR) compile the same as Pro7's macros.
- Blocked by: nothing recorded.
- Pattern: stack slot of locals (permuted); spill/reload of allocated registers (2.6 spills where we keep a register);
  register permutation; size: 16 bytes less. Equivalent checked: no specific record.

### gen_TYPCON (0x0044fa20): 99.3%
- Differs: same size (2585). In the first Gecko float block (0x20f-0x27c) ebx and ebp are swapped: 2.6 keeps `gprTemp` (the ALLOC_GPR result) in ebx and the sign-extended `gqr` (computed in ax, `movsx ebp, ax`, used by the li and the oris) in ebp; ours the reverse. A jump table (@8647) is not placed (data only). Class: renaming only (ebx/ebp for gprTemp and the gqr extension, over 8 instructions).
- Tried (newest first):
  - NM5 (October 2026): the first Gecko block's temporaries block-local, every order of fprTemp/gprTemp/gqr and
    short/int/SInt32 for gqr and gprTemp (54 variants): best 99.4 with `int gqr; short gprTemp;` block-local
    (registers then right; 2.6 computes gqr in ax and sign-extends it into ebp, ours computes it in ebp and
    sign-extends gprTemp), not kept. Block-local copies of only gqr, only gprTemp, fprTemp: 99.3 at best. The block
    using `reg`/`tmp` for gprTemp or fprTemp, `gqr2` (int, with or without `(short)` casts) for gqr: 96.9..98.0. The
    locals'-count switch (0..4 dummies): no effect.
  - Z_CG (review, Oct 16): IS_TYPE_VECTOR_STYPE (the `stype` copy macro) replaced by plain IS_TYPE_VECTOR: same code (9 differing lines before and after), so the macro was not needed; the TODO is gone.
  - R4_ISel: tc_v.c tried the integer types of gprTemp, gqr, reg, tmp and fprTemp (short/SInt32/int). tc_v2.c split
    `gqr` into gqr1/gqr2 for the source and destination parts with types. tc_v3.c tried int/SInt32/short `gqr` with
    `(short)` casts. tc_v4.c tried g1/g2 int or short with or without a cast. tc_v5.c tried the gqr selection as one
    nested ternary or as ifs, with int. tc_v6.c tried new fpr1/gpr1 locals (int/SInt32/short) for the Gecko block, plus
    a per-function `#pragma sym off`. Results not recorded. The score went from 97.8 at R4 start (R4 prompt; NM
    tc.txt) to 99.3 now (inferred: R4's work; the exact change is not recorded).
  - N3_ISel: tc1.c (types of gqr, gprTemp, fprTemp and reg); tc2.c (`tmp` replaced by reg, gprTemp, fprTemp or stype in
    the first or second float block); tc3.c (625 combinations of which variable holds each of four temporaries);
    tc4.c (120 permutations); tc5.c (nested, ifs, gqr_first, plus); tc6.c (block-local g/f/q temporaries in several
    orders); m_gen_TYPCON.c (24 merges). Results not recorded.
  - NM_InstrSelection: tcA.py (`tmp` block-local per float block, tmp merged into fprTemp or reg); tcB.py (tmpblock,
    Gecko variables block-local in two layouts). tc.txt shows 97.8.
  - Rewrite round 1: var_results.txt rewrite 46.7 against the old body's 78.4.
- Leads: nothing recorded.
- Blocked by: nothing recorded. The stype copy is a TODO macro workaround (IS_TYPE_VECTOR_STYPE).
- Pattern: register pair swapped (one block). Equivalent checked: no specific record.

### gen_ASS (0x004505f0): 83.5%
- Differs: 31 bytes longer (3177 vs 3146).
  - In the EINDIRECT/conditional-assign (0x4c) entry, 2.6 loads `expr`'s operands in another order and keeps them in
    edi/ebp; ours reloads `expr` from its slot (extra `mov eax, [esp+0x84]; mov ebp, [eax+0xe]`).
  - The `type` pointer (`stunsignedlong`/`stunsignedlonglong`) and `opright`'s base sit in edi in 2.6, where ours keeps
    a stack slot ([esp+0xc]) and reloads it (`push [esp+0xc]; mov eax, [esp+0x10]` against `push edi`).
  - esi/edi are swapped from the prologue on (outputReg in esi in 2.6). 2.6 has extra `mov edx, esi` copies.
  - Several alignment pads (`mov eax, eax`/`lea eax, [eax]`) are present only in 2.6, and the `al`/`dl`/`bl` byte
    copies of `[esi+0x25]` differ.
  - @8947 is not placed.
- Tried (newest first):
  - NM5 (October 2026): the locals'-count switch (0..4 dummy locals, (void)0): no effect. Read against Pro7: the
    2.6 additions (soft float and I8 first, the gprfloatcopy path reassigning `type`, the paired-single copy
    effects) are where the differences start; not resolved.
  - Z_CG (review, Oct 16): IDA's pseudocode checked branch by branch against the source (no behavioural difference: soft float, I8, register variable with the paired-single copy, float/gprfloatcopy, vector, Gecko paired single, GPR, struct with the small-struct return); the `IS_TYPE_VECTOR_STYPE` macro (assigning a dummy `stype` local) replaced by plain IS_TYPE_VECTOR (same code, same 191 differing lines), magic numbers named. Pro7's early returns for the if/else chain: 81.3 (work/v_ass.c). The register difference starts in the prologue: 2.6 keeps `right` in edi and `outputRegHi` in esi (copied to edx for each callee push); ours spills `right`.
  - R4_ISel: ass_v.c tried the gprfloatcopy condition as a ternary, `!= 0`, `!!`, assigned to `flag` (two forms),
    plain, nested (two forms) and with no initialisation. ass_h.c tried the test as a helper `is_gpr_float_copy`
    (int/Boolean x static/static inline/inline/if-return). Kept: `static inline int` with the if-return body, a TODO
    with an invented name (83.3 -> 83.5; HANDOFF.md, source TODO at line 1351: "2.6 computes this test ... as a value
    (1/0 then a test) after jumping straight to the FPR path on the first two conditions: an inlined helper").
    Also tried:
    - ass_v2.c: `flag`'s type (Boolean, char, short, SInt32, UInt8, unsigned int), function-level, block-level or
      uninitialised.
    - ass_v3.c: PCodeArg a/b at function level; `right` declared first.
    - ass_v5.c: no `stype`; `right`/`left` loaded in other orders (c1-c3).
    - ass_v6.c: a separate `Type* gtype` for the integer-copy type, block-level or function-level, first or last.
    - ass_v7.c: helper linkage variants.
    - A per-function `#pragma sym off`.
    - gen_OR (the function after gen_ASS in the binary) given dummy locals, `(void)0` or a `type` assigned first
      (or_v.c), aimed at deferred coupling (inferred).
    - Results not recorded.
  - N3_ISel: ass1.c (`goto done` for the returns); ass2.c (`flag` placement: B_end, B_first, B_noelse); ass3.c
    (returns everywhere, ret_vi); ass4.c (nostype, PCodeArg a/b at the top, OBJECT_REG for Registers_GetVarInfo);
    ass5.c (`(short)`/`(int)` casts on the outputRegHi argument; a `hi` copy); ass6.c (tern, plain, flagassign x2);
    ass7.c (vi/align/op2 block-local, vi first, right first or last); m_gen_ASS.c (12 merges). Results not recorded.
  - N3 merge / fn.py diff change: comparing references exposed a real bug: gen_ASS tested `copts.logopts` where 2.6
    tests `copts.gprfloatcopy` (AGENTS.md "fn.py diff compares references"; HANDOFF.md). Fixed.
  - NM_InstrSelection: assA.py (a local `itype` for the gprfloatcopy integer type, with int `flag`). ass.txt shows 83.0.
  - Rewrite round 1: var_results.txt rewrite 46.6 against the old body's 81.2.
- Leads: the helper TODO says 2.6 jumps straight to the FPR path on the first two conditions, then computes the rest
  as a value. The helper's exact form (or how many of the conditions it holds) is unresolved (source TODO).
- Blocked by: possibly gen_OR, through deferred coupling (inferred; or_v.c tried it).
- Pattern: reload of a spilled value where 2.6 keeps a register; register permutation; block order (operand load
  order); size: 31 bytes more. Real bug found: `copts.gprfloatcopy` (fixed). Equivalent checked: no specific record.

### gen_AND (0x00451440): 99.4%
- Differs: same size (2247). One register: at 0x7f6, in the `left` side's 0xFFFF test, the temporary for `left->data.intval.lo` is ebx in ours and ecx in 2.6, over four instructions (2.6 uses ecx for it in the `right` side's test too). Class: renaming only (ecx/ebx, 4 instructions).
- Tried (newest first):
  - NM5 (October 2026): the 0xFFFF test as a `static inline` helper (SInt32/UInt32/int parameter, either operand
    order) for both tests or the left one: 94.6 (not inlined the same way). The locals'-count switch (0..4 dummy
    locals, (void)0 statements): no effect on gen_AND, but it flips gen_SHR (deferred coupling).
  - Z_CG adversarial: Pro7's `FITS_IN_USHORT(x.lo)` for the two `(lo & 0xFFFF) == lo` tests (98.8; right only 99.0). (Z_CG work/adv/v_and.c)
  - R4_ISel: and_v.c tried the 0xFFFF fit test as swapped operands, `0xFFFFu`, a `(UInt32)` cast, a FITS_IN_USHORT
    macro and long, plus a per-function `#pragma sym off`. Results not recorded.
  - N3_ISel: and1.c tried `type = expr->rtype` first (a), assigned in the soft-float test (b), and c. and2.c tried the
    soft-float check as `if (copts.soft_float) { Type* t | type = expr->rtype; if (IS_TYPE_FLOAT_NOT_VEC2X32FLOAT(..))
    ... }` (blk, blk_type, blk_noret). and3.c tried 0xFFFF test spellings (swap, ushort, ushortcast, uns, ul). Also
    m_gen_AND.c (merges of left/right, first/last) and IS_andblk.c. Results not recorded. The current source has the
    blk_type form.
  - NM_InstrSelection: andA.py (typefirst, inner, inner_type, blockt); andB.py (swapped 0xFFFF operands on both sides,
    right only or left only; an unsigned-short cast macro); andC.py (Pro7's early returns instead of the else-if
    chain, fully or partially). AGENTS.md records 98.5, or 99.4 at the cost of gen_SHR, which shares the inlined
    `shift_and_mask`. and.txt/and3.txt are the diffs.
  - Rewrite round 1: var_results.txt rewrite 83.2 against the old body's 64.1.
- Leads: nothing recorded beyond the coupling with gen_SHR.
- Blocked by: gen_SHR, which shares the inlined `shift_and_mask` (AGENTS.md). gen_SHR is exact now and gen_AND is at
  99.4 (inferred: the 99.4 state no longer costs gen_SHR).
- Pattern: one register (ebx/ecx); deferred coupling with gen_SHR. Equivalent checked: no specific record.

### gen_MODULO (0x00452490): 97.7%
- Differs: same size (2024).
  - After the inlined `ispowerof2` (getbit), 2.6 copies the result into eax before the test (`mov eax, ebp; test eax,
    eax`); ours tests ebp directly. This shifts the next branches by 2 bytes, and our `nop` pad at 0x17f is absent in
    2.6.
  - Later, ebx/ebp are swapped for an ALLOC_GPR result (0x292-0x2f5), ebp/ecx for a parameter reload at 0x30b, and
    ebp/ebx for two tmpregs at 0x738/0x744 and their pushes.
- Tried (newest first):
  - NM5 (October 2026): `int tmp` (gen_DIV's type for the same `(tmp = ispowerof2(...))`, where 2.6 has the same
    copy): gives 2.6's `mov eax, ebp; test eax, eax` copy, but outputRegHi then takes ebx where 2.6 has ebp, the
    signed path's final emitpcode is not tail-merged, and the registers move throughout: 91.7 (2040 bytes).
    `decl-order --swaps` on it: nothing; the type greedy search on the other locals: 94.2 with short tmpregs (not
    kept); the greedy search from the current state reverts to `SInt32 tmp`. The locals'-count switch: no effect.
  - R4_ISel: mod_v.c (`!= 0`, an `(int)` cast on the assignment, int or short `tmp` with `!= 0`); mod_v2.c (int inner
    `tmp`, every tmpreg int, mixed); mod_v3.c (int `tmp`, inner `tmp` renamed `tmp2`, the outer `tmp` reused, the test
    nested); mod_v4.c (int `tmp` with `#pragma sym off`, and `tmp`/`value` declared at each of 8 positions); per-function
    `#pragma sym off`. Results not recorded.
  - N3_ISel: mod1.c (`tmp` int/short/UInt32, `!= 0`); mod2.c (inner tmp int, inner reuses the outer, all int, value
    int); mod3.c (the power-of-2 `return` as an else-if chain, with int types); m_gen_MODULO.c (121 merges). Results
    not recorded.
  - NM_InstrSelection: modA.py (an else-if chain, Pro7's declaration order, both); modB.py (block tmpregs/finalReg
    hoisted to the start or end of the function's declarations). mod*.txt are diffs; AGENTS.md records 97.7.
  - Rewrite round 1: var_results.txt rewrite 77.9 (the old body did not compile in that harness).
  - Rewrite round 1 also found that 2.6 excludes 0 in gen_DIV/gen_MODULO's power-of-2 test (a 2.6 shape, not a bug;
    AGENTS.md).
- Leads: `int tmp` is probably the original's type (gen_DIV, exact, has it and the copy); what else differs then is the register of `outputRegHi`, which 2.6 shares with `tmp` (ebp): look for the declaration or use that lets them coalesce. The copy of the inlined `ispowerof2` result before its test resembles the AGENTS.md shape "`if ((x = f()) && ...)` where 2.6 copies a result to a register before the test".
- Blocked by: possibly the inlined `ispowerof2`'s own shape (inferred). Nothing recorded.
- Pattern: missing register copy of an inlined result; register pair swapped (temporaries). Equivalent checked: no
  specific record.

## Changes blocked by exact functions

Changes toward Pro7 that cost an exact function, kept out for now (from the October 16 spot fixes):

- makediadicnode(ENode*, ENode*, ENodeType) (Pro7's): CExpr's pointercompare drops 100 -> 98.7, so the parameter stays
  `UInt8`. Tried: Pro7's direct `right = CExpr_SafeClassPointerCast(...)` in place of the `expr` local (98.7 either
  way). Lead: 2.6's makediadicnode takes a byte type other than ENodeType, or pointercompare's `nt` is not ENodeType.
- `Object.sclass` as `UInt16`: CFunc_SetupLocalVarInfo drops to 94.5 (and CFunc_LocalDataDeclarator, which inlines
  it, to 95.3); it is `SInt16`. With `SInt16`, `== TK_REGISTER`, `(UInt16)sclass ==`, `== (SInt16)TK_REGISTER` and
  `(int)sclass ==` are all exact; `== 257U` gives 94.5. The plain Pro7 spelling is kept.
- `IRO_IsPow2(IROLinear*, SInt32*)` (Pro7's, with its callers' `SInt32` locals; H2_OPT, October 16): DoTransformations
  100 -> 88.3, UpdateUseForOtherSelfAssignment 100 -> 96.7, BuildOrigIterationCount 100 -> 97.1. 2.6's parameter and
  the locals are `int` (IroUtil.h says so).
- BitVectors.h's lengths as Pro7's `int` (bitvectorcopy, changed, intersect, union; H2_OPT, October 16): LoopDetection's
  computedominators 100 -> 47.7, so 2.6's are `SInt32`. bitvectorchanged's return as Pro7's `int` is exact and now in.
  H2_OPT adversary, one at a time: bitvectorinitialize -> computedominators 92.7; bitvectorcopy -> computedominators
  96.0 and LiveVariables' computeglobalinout 91.0; bitvectorchanged -> 95.7; bitvectorintersect -> 45.2;
  bitvectorunion -> computeglobalinout 91.0. bitvectorintersectionisempty's `int` (Pro7's) as `SInt32` changes nothing.
- H2_DRV (Driver headers): `License_Checkout`'s third parameter as Pro7's `Boolean` (Pro7 marks it a guess,
  `Boolean flag`): License_Checkout and UCBCheckoutLicense (which passes its `UInt32 autoCheckin = flags & 1`) both
  stop being exact, so 2.6 takes an `int`. CLLicenses.h keeps `int autoCheckin` with a comment.

- Back end and assembler (header review, October 16): StackFrame.h's update_out_param_align/update_in_param_align/
  update_frame_align with Pro7's `SInt32 align` cost set_out_param_displ, assign_arguments_to_memory and
  compute_frame_sizes (compute_interrupt_frame 92.9 -> 80.6; re-checked by H2_BE's adversary); can_add_displ_to_local's
  Pro7 `SInt32 displ` costs itself. Both stay `int`. CodeGen_Generator with Pro7's `((InlineAsm*)stmt->expr)->flags`
  test (no local) is not exact: 2.6 keeps the instruction in a local `ia`.
- H2_BE adversary: PCodeInfo.h's opcodes are Pro7's `typedef enum Opcode` now. The first review measured the enum
  without Pro7's `PC_INVALID = -1`: an unsigned 2-byte enum, 56 exact functions lost. With it (signed, as Pro7) only
  Select_Gecko_Load_Store/gecko_load_store_register and insert_load_spilled_register changed, all exact again with
  Pro7's types (`Opcode` return and local; SpillCode's Pro7 `Opcode opcode`). Still blocked: PCode.h's
  makepcode/emitpcode with Pro7's `Opcode op` cost Intrinsics' vector_intrinsic_abss (99.5: `output->reg` stored
  through a reloaded `output`), so they stay `short op`. Not tried: InstrSelection.c's statics fp_binary_operator,
  binary_immediate and or_xor_immediate with Pro7's `Opcode opcode` (the unit's own source); PCode.op/vformatpcode stay
  `short` as in Pro7.
- (H2_FE) `CError_FATAL` as Pro7's `do { CError_Internal(__FILE__, line); } while (0)`: I8_gen_ADD (InstrSelection)
  and CExcept_ScanTryBlock (CException) lose their match (the do/while is a statement of its own); 2.6's macro is the
  bare call.
- (H2_FE) `CExpr_SearchExprTree(ENode*, CExprSearchCB, int count, ...)` (Pro7's `int`): the function itself goes from
  262 bytes to 430; it stays `SInt32`. (H2_FE adversary: re-tried, similarity 1.0 -> 0.46.)
- (H2_FE) `CInline_AddTemplateFunctionAction(Object*, TemplateFunction*, TemplFuncInstance*)` (Pro7's): rechecked
  after the CExpr2.h merge, CTempl_GetCreateFuncInstance still loses its match (797 bytes against 793); the
  parameters stay `void*` (TODO in CInline.h).

Spot-fix checks that ended exact (kept for the record): load_address with `SInt32` `immOffset` (no volatile;
`op->immOffset == 0 && op->object == NULL`, where Pro7's `!op->immOffset && !op->object` gives 95.7);
CMach_CalcFloatDiadicBool with Pro7's `Float` parameters (no `volatile double`); CMach_CalcFloatMonadic as Pro7's
`return CMach_CalcFloatConvert(type, fval)` (our old body with `Float` was 92.7); is_illegal_abs_section_name needs
ObjGen to include <ctype.h> (83.9 with `tolower` implicit); ustrcmp, ustrncmp, ExecuteLinker, WildCardMatch and
OS_SpecToStringRelative call ctype.c's `tolower` (`-D_MSL_USE_INLINE=0`; inline `tolower` gave 32-98%).
- tokens.h's `TK_*` as Pro7's `typedef enum EToken` (the same values; October 16, after round H2): every function
  but TOC.c's createfloatconstant is unchanged; that one goes 100 -> 98.8 (its only token is `obj->sclass =
  TK_STATIC`, so it flips with the declaration, and its body carries a workaround: the `UInt32* value` copy that keeps
  `data` in its slot). Pro7's body for it, with 2.6's additions, is 57.7 under the enum. Lead for the matching round:
  re-derive createfloatconstant, then retry the enum. tokens.h keeps the `#define`s.
