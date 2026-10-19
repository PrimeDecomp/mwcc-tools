# Near misses in the other versions

`docs/nearmiss-2.6.md`'s companion for GC_2_0 (2.0p1 follows it), GC_2_5 and GC_2_7: functions exact in 2.6 whose
version-specific code does not match yet, with what differs, what was tried and the leads left. The same rules: read
an entry before working on its function, append what you try, remove the entry when the function goes exact.
Functions inexact in 2.6 itself are 2.6's ledger's. From the October 17, 2026 version round (agents V20A, V20B, V25,
V27).

## GC_2_0

None left (NM5, October 17; was five); every row but the three version.dll import thunks (as in 2.6) is compared.
Made exact in NM5: gather_instruction_alias (case 2 sets `alias_choice` inside its two inner ifs, so their branches
reach the store with worst_case_ptr still in eax; the fUpdatesPtr tail reads `pcode->alias` with no `alias` local),
computeglobalusedefinfo and computeglobaldefuseinfo (2.6's head with a `block` local, then `decl-order --swaps`),
computeusedeflists (each clear loop's two stores as one comma statement, as 2.6's loop has them),
gen_negated_condition_gpr (2.0's EEQU peephole sets `output->reg = tmpReg` in its `value == mask` case, a bug fixed
in 2.5). Alias.c and UseDefChains.c now link in 2.0 and 2.0p1.

- (ParseLocationSet and gen_AND differ as in 2.6: 2.6's ledger.)

## GC_2_5

- **CMach_StructLayoutBitfield** (CMachine.c, 2.5's whole-function form), 83.2%: every instruction matches but for
  register allocation and the encoding size that follows (1345 bytes, the original's 1329). The original keeps
  `align` in ebp and spills `basesize_bits` to [esp+8]; ours the reverse, which also spills the CSE temporary of
  `tbitfield->bitlength` (the original keeps it in cl from the overflow test through the fit and grow tests). Tried:
  `decl-order --swaps` (kept its moves of `required_alignment` and `room`); `basesize_bits` or `align` as int; the
  in-unit test as a static or inline helper; the fit test as a helper; a `bitlength` local;
  `copts.old_style_bitfields` read directly; four spellings of the overflow test (the if/else assignment kept); the
  fit branch inverted (kept); `byte + curbfoffset` (kept). Leads: the new-unit and grow tests as inline helpers with
  parameters; `align` by another expression.
  NM5 (Oct 16): decl-order --swaps on today's state: none better than 83.2.

## GC_2_7

- **MakeNamedSymbolTableEntry** (BE_symbol.c), 97.7%, the only function keeping the unit from linking in 2.7: in the
  tail loop of the inlined BE_SymbolHash, ebx and ecx are swapped over six instructions (the other two users of the
  hash, AddSymbolTableEntry and CreateDataPoolAnchorSymbol, are exact). Tried: `decl-order --swaps`; early return
  against if/else and a separate test of the search result (97.7); the search written in with `break`; the bucket
  in a local; a separate copy of the hash in the search; operand order in the comparisons; `short` for `sclass`.
  Leads: the hash as a macro; other types for its locals that keep the other two exact; the search taking the hash.
  NM5 (Oct 16, NB work/mn1..mn3.py, each with AddSymbolTableEntry and CreateDataPoolAnchorSymbol checked): the
  step written `(hash >> 27) ^ (hash << 5)` (84.3, costs the other two), the character first, without parentheses
  (97.7); `i` as int (93.6) or `i`/`length` as UInt32 (97.7); `length` declared first (64.5); a char pointer walk
  (29.6); `SInt32 hash` (89.7); in the search, the hash as an initialized local, as a separate statement before
  the loop (with `node` declared before or after it), the bucket in `node` before a `for (;;)`: 97.7; in
  MakeNamedSymbolTableEntry, the new node in a second local (three positions), the found node in its own local or
  block, `if ((node = ...) != NULL)` (96.9). Class: renaming only (ebx/ecx in the inlined search hash's remainder
  loop: the shift-xor temporary and the character).
- Workarounds kept with TODOs (DWARF.c, 2.7 only): `(SInt32)offset` casts in the entry-symbol calls of
  emit_variable_entry, emit_array_type, emit_function_type and emit_member_pointer_type (2.7 passes the converted
  copy of `offset`; the original expression is unknown); PatchSibling written out in end_function_entries (with the
  call MWCC inlines the function into tweakDWARF_file, where 2.7 calls it); a 2.7-only `state` local in
  tweakDWARF_file's line-symbol loop.
