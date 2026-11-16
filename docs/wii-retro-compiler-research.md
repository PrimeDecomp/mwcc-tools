# Wii_Retro compiler research

These notes record the compiler comparisons, patch experiments, and Echoes validation.
The current build, patches, and plan are in the [main guide](wii-retro-compiler.md).

## Native relink and validation

The build validates the stock SHA-1 and splits it into relocatable COFF objects. Clang builds the replacement
`CMach_GetObjectAlignment` and the destroy-loop, vtable-emission and register-order passes
from C sources.
The seven instruction-selection edits retain the tested operand-width branches in the reused RC1 code.
The build redirects the pass calls through COFF relocations. LLD links the executable from these objects,
preserving the CRT and exception table order and rebuilding the imports and base relocations.

With Clang and LLD 23.1.1 the native PE has SHA-1 `d3d17e4e5bc7102b68f9f1e4bcb92f4b16a8c0cd`.
All 898 R3ME01 Wii RC1 objects match the three-patch compiler byte for byte with the configured flags.
Forcing file IPA for every C++ entry grows the exact function count from 12,758 to 12,801 in the
18,016-function report, with zero exact losses. All 30 register-order cases that previously favored
ordinary inlining now match with file IPA in R32J01, R3ME01 and R3MP01. See
[the register-order pass](#file-ipa-and-inlined-local-register-order) for the checks.
The earlier three-patch relink matched the binary patch chain in both flag configurations and
reproduced all three original DOL hashes with the `CStaticGeometryMap` diagnostic object.

## Reproduce the original binary patch probes

Apply the three patchers in order. They write to `build/Wii_Retro_probe/`. The result works with `sjiswrap`
and needs no Clang installation at patch time.

```sh
uv run --no-sync python -I gc2/tools/wii_patch_struct_copy.py build/compilers/Wii/1.0RC1 build/Wii_Retro_probe
uv run --no-sync python -I gc2/tools/wii_patch_pretest.py build/Wii_Retro_probe build/Wii_Retro_probe
uv run --no-sync python -I gc2/tools/wii_patch_template_vtables.py build/Wii_Retro_probe build/Wii_Retro_probe
```

The in-place patch chain SHA-1 is `4f04833b22247059750b91ffa4eedb4c90cc29aa`. The struct-copy and pretest steps give
`fbb0c00006afad9e60a82a020caba0078d4d4a5b` and `89bb1b1e631f474b2cadf190ba2c88ce30ef2141`, respectively.
Clang 23.1.1 reproduces both embedded pass payloads and their COFF relocations using the flags in the patcher
docstrings.

The detached Echoes `wii-retro-probe` worktree at `d7fc9ef33` has intentionally remapped R3ME01 splits and symbols.
Its sibling `wii-retro-probe-reports` holds `report-pretest-compiler.json` and `report-tv.json`. Regenerate
`build.ninja` before installing an experimental compiler. The configure step recreates `Wii/1.0RC1_retro`, and
replacing the executable at the same path alone can leave stale objects. Force the affected objects to rebuild,
or compile to fresh paths and use copies of the linker response file for a single-unit relink.

## What Echoes' research established

From the Echoes project docs: [Trilogy-compiler-struct-copies.md] and [Trilogy-compiler-destroy-pretest.md]
(`research/` in the Echoes collab project).

**The target is a Wii-line compiler that keeps GC 3.x's struct-copy rule.**

- **From the Wii line (RC1):**
  - the instruction scheduler;
  - switch lowering: a `cmplwi` range check, where GC 3.x emits a `cmpwi` chain;
  - prologue and constant shapes: an `stw` for each register, and `addis` + `cmplwi`, where GC 3.x uses `stmw` and
    `lis/subi` + `cmpw`;
  - folded range checks (`subi; clrlwi; cmplwi`).

  On game code RC1 is identical to 1.0a and 0x4201_127. No GC 3.0a5.2 option or pragma reproduces the Wii scheduling:
  136 runs were tried.
- **From GC 3.x:** a 4-byte struct whose declared alignment is below its size (GXColor, CResInfo, `pair<int,
  CHintState>`, CDamageVulnerability's records...) is copied one byte at a time (4 `lbz`/`stb`), unless the compiler
  knows the source is word-aligned. A stack slot or a value it can forward from a register (the word just stored) is
  copied as one word. Every GC 3.x build (3.0a3 to 3.0a5.2) does this. Every Wii build from 1.0RC1 to 1.5 copies with
  one word.
- **RC1 still has the old rule behind a pragma.** `#pragma legacy_struct_alignment on` brings the byte copies back.
  It also changes the stack layout: aggregates get 4-byte instead of 8-byte alignment, and 1-byte locals are packed.
  The target does not have that layout. The pragma also reloads a value byte by byte where the target forwards the
  stored word. The pragma is new in the Wii line: 3.0a5.2 has no pragma RC1 lacks. So `Wii_Retro`'s copy rule is
  most likely RC1's legacy copy path, separated from the legacy stack layout and with GC 3.x's word-forwarding check.
- **A second difference, not yet placed:** in the `rstl::destroy(begin, end)` loop, the `if (begin != end)`
  pretest is dropped when the loop body makes a call, and kept when it does not. Every Wii compiler we have keeps it.
  Whether 3.0a5.2 drops it has not been checked.
- **Dates.** Corruption's `main.dol` (2007-07-27) already has the same copy behaviour, and its SDK version strings
  are the same as Trilogy's. The compiler is therefore older than July 2007: a Wii-line build from roughly a year
  after 3.0a5.2, about ten months before RC1. RC1 is our nearest neighbour on the Wii side.

So `Wii_Retro` is RC1 with a small number of changes, not an even mix of the two versions. RC1 is the base, and
3.0a5.2 is the reference for the parts that changed.

## What mwcc-tools adds

- **Both edge versions are newer than gc2's code base.** Every source file name in 3.0a5.2's asserts (146) also
  appears in RC1's (173). Only 98 appear in 2.7's. So 3.0a5.2 already has the newer front end and IR (`CIRStream`,
  `CPrepLexer`, `CTemplateFuncInst`, `CExprGen`, `IroType`, `CodeMotionPPC`, `Spill`...). gc2's 2.7 sources cover only
  the code that changed little.
- **RC1 adds** a switch-lowering group (`SWCH_Cell`, `SWCH_DecisionTree`, `SWCH_Interface`, `SWCH_Rules`,
  `SWCH_ValueSet`), `LocalCSE`, `OptimizerHelpers`, VLE and the plugin and build-agent layers. The `SWCH_*` group is
  the likely home of the Wii switch lowering.
- **RC1 has CodeView** (NB11: module table and publics; `gc2/tools/mw/wii.py` reads it), so it has names and unit
  boundaries from the start. 3.0a5.2 has none.
- **Host compiler.** 3.0a3 onwards and every Wii build were linked by mwld x86 3.2, and RC1's CodeView names its
  compiler "Metrowerks CodeWarrior C/C++ x86 V3.2". We do not have that compiler: `build/compilers` has Pro 4, 5, 5.3
  and 6 only. Without it, nothing can be matched byte for byte.
- **Precedent.** `gc2/config/GC_2_0p2` is a compiler with no original (`derived_from: GC_2_0`). It models what
  BFBB's code shows and is built from the sources alone. `Wii_Retro` follows that pattern.

## The acceptance suite

Each function is from R3ME01 and is compiled by the Echoes build with `-lang c++` and Retro's flags. The suite goes
into `config/Wii_Retro/acceptance.json`, with the target bytes of each function and the source revision it was
taken from.

| Function | Checks | RC1 today |
|---|---|---|
| `CTevCombiners::SetTevRegisterColor` | byte copy alone (72 bytes) | word copy |
| `CGraphics::SetCopyClear` | byte copy from a reference | word copy |
| `CGraphics::SetClearColor` | the stored word is forwarded, not reloaded | matches (the pragma breaks it) |
| `CGX::SetChanMatColor` | by-value GX argument copied byte-wise after a word compare | word copy |
| `CCubeRenderer::SetupCGraphicsStates` | `const GXColor` loaded byte-wise from `.sdata2` | word copy |
| `CCubeRenderer::DrawUnsortedGeometry` | stack slots 4 apart, frame 0x20 | matches (the pragma breaks it) |
| `GetAimPosition` | frame 0x90 | matches (the pragma breaks it) |
| `~CCameraColliderGroup`, `~CBallCamera` | no destroy pretest when the body calls | pretest |
| `~CScriptTeamAiMgr` | pretest kept when the body makes no call | matches |

**No losses.** As well as the suite, keep the 195 units that match on both G2ME01 and R3ME01 with no
version-specific code (1,212 functions; RC1 matches 1,162 of them). `Wii_Retro` must match every function RC1
matches there, and every function RC1 matches in Echoes' R3ME01 build. The target is the whole R3ME01 build:
the 71 functions where the original has more byte copies than our build, and the destructors listed in the
destroy-pretest page.

## Phase 1: names, units and a diff map

The layouts and diff map are under [gc2/research](../gc2/research/README.md). Register the layouts as build
configurations when the source work starts.

1. **RC1.** Use `wii.py` to generate `config/Wii_1_0RC1/` from CodeView: symbols, splits from the module table,
   `functions.json` and bindings. The units are the modules, so the boundaries are exact.
2. **3.0a5.2.** Carry the names over by relocation-masked remapping (`mwcc-tools remap`, `port_version.py`), the way
   1.3.2 got 1.3's.
   - Main source: backward from RC1, since it is the same code base.
   - Secondary: forward from 2.7 through 3.0a3, 3.0a3.x and 3.0a5. Short hops leave less unmatched.
3. **Diff map** (`docs/wii-rc1-differences.md`, like gc132-differences.md): the functions that differ between
   3.0a5.2 and RC1, per unit, with sizes. It guides the decompilation. It does not bound `Wii_Retro`, which is an
   intermediate build and can have code neither edge version has.

## Phase 2: find and probe the behaviours in RC1

This phase de-risks the plan before most of the decompilation.

1. **The struct copy.** Find where RC1 reads `legacy_struct_alignment`: the option's pragma table entry, then its
   readers. Its readers separate into:
   - the copy-lowering path, which picks byte or word moves (most likely in the backend's struct move);
   - the stack-layout path, which sets aggregate alignment and packs locals.

   Find 3.0a5.2's copy lowering by the diff map and compare the two. The aim is the exact rule 3.0a5.2 uses to
   choose a word move (stack slot, forwarded register value).
2. **Probe by patching.** Patch the RC1 executable so the copy path takes the legacy branch and the stack layout
   does not. This needs no decompilation and no host compiler. Run the suite.
   - If `SetTevRegisterColor`, `SetCopyClear`, `SetChanMatColor` and `DrawUnsortedGeometry` all match and nothing
     regresses, the model is confirmed. What remains is `SetClearColor`'s forwarding, which then has to come from
     3.0a5.2's rule.
   - If not, the readers are entangled and the decompilation must separate them.
3. **The destroy pretest.** Check the destroy pair with 3.0a5.2 (an Echoes R3ME01 unit compiled with
   `compiler=GC/3.0a5.2`).
   - 3.0a5.2 drops the pretest: it is a GC 3.x behaviour, and its home is found by comparing the two versions'
     loop code (loop inversion or guard elimination, `IroLoop`/`IroJump`/`IrOptimizer`).
   - 3.0a5.2 keeps it: it belongs to neither edge version, and it has to be modelled from the R3ME01 evidence,
     the way GC_2_0p2 was from BFBB's.
4. **Look for more.** Diff the suite's neighbours under the patched RC1. A difference that remains in a function
   whose source is trusted (matched on GC, no version code) is a third behaviour. The inliner is a candidate:
   mwcc-debugger.md has RC1 inlining traces with residuals that are still unexplained.

### Results (2026-11-14)

**Where the pragma acts.** `legacy_struct_alignment` is `copts+0xA1` (0x74F2F9). The pragma table's second field
is the option's offset. RC1 reads the field in 11 places:
- `CMach_AllocationAlignment`: the alignment of what is allocated, which sets the stack layout. Without the
  pragma, a small aggregate's alignment is raised to its size, up to 8. With it, the declared alignment is used.
- `CMach_GetObjectAlignment`, a function that exists only in RC1. For an object that is not a local, the
  pragma gives the declared alignment, and no pragma gives `CMach_AllocationAlignment`. Locals always go through
  `CMach_AllocationAlignment`.
- The copy-width sites, three in `gen_COND`, two in `gen_ASS` and two in `Intrinsics`. Each one is
  `legacy ? CMach_GetQualifiedTypeAlign(type, 0) : CMach_AllocationAlignment(type, 0)`.
- `CMach_SetBackEndOptions` and `CMach_GetBackEndOptions`, which only save and restore the field.

3.0a5.2 has no such field. The byte its `CMach_AllocationAlignment` tests (0x70F231, `copts+0x249`) is
`optimizesize`. Its other readers (the inliner, loop transformations, spill costs, `StructMoves`) are the ones
`optimizesize` has in 2.7's sources. So 3.0a5.2 allocates like RC1 without the pragma, and its byte copies come
from a different operand-alignment rule. That rule is the part to decompile.

**The patch** (`gc2/tools/wii_patch_struct_copy.py`): the seven copy-width sites and `GetObjectAlignment`'s
non-local path take the legacy branch, and `CMach_AllocationAlignment` does not.

Echoes R3ME01 at `origin/main` 8deb345f5, stock RC1 against patched RC1:
- 12,866 → 12,892 of 18,016 functions exact: 32 gained, 6 lost; 89 improved, 22 regressed.
- The suite: `SetTevRegisterColor`, `SetCopyClear`, `SetChanMatColor`, `SetChanAmbColor` and
  `SetupCGraphicsStates` become exact. `SetClearColor` and `DrawUnsortedGeometry` stay exact, so the stack layout
  is unchanged. Units: CHintManager 70.3 → 96.7%, CStateMachine 82.4 → 99.7%, CRasterFont 82.3 → 97.3%, CPakFile
  76.9 → 88.2%.
- The 6 lost functions are `CScriptColorModulate::SetTargetFlags`, `CScriptDoor::SetShieldAlpha`,
  `CPortalTransition::Update`, `CCinematicCamera::Think`, `CGrenchlerTail::PreRender` and `CPlayer::PreRender`.
  All six copy `CModelFlags`. The target copies its first 4 bytes as one word, and the patch copies them byte by
  byte. These 4 bytes are a field Echoes has not identified, declared `uchar x0_[4]`.

**The patch is 3.0a5.2's rule, and the 6 losses come from the source.**
- 3.0a5.2's `gen_ASS` has RC1's shape: each operand's alignment, then the smaller of the two. An operand with no
  object gets `CMach_GetTypeAlign(type, 0)` (the declared alignment). An object gets a function that is, instruction
  for instruction, RC1's `CMach_GetObjectAlignment` under the pragma. So the patch reproduces 3.0a5.2's operand
  alignment.
- Compiled by 3.0a5.2 itself, `CPortalTransition` and `CScriptColorModulate` byte-copy where the patch does, and
  `CDamageVulnerability::Initialize` copies like the target.
- Two variants that give up the declared alignment for one operand do worse:
  - the destination: 12,889 exact, 1 lost;
  - the source: 12,879 exact, 6 lost.

  Pointer-path functions that the first patch matches stop matching (`CDamageVulnerability::Initialize`,
  `CSortedListManager`, CPakFile's sorts).
- With `x0_` declared word-aligned as `union { uchar x0_[4]; uint x0w_; };`, the patch matches all six functions,
  and no function is lost: **12,866 → 12,898 exact, 32 gained, 0 lost**. With the stock compiler the header change
  alone changes nothing (12,866 either way, the same functions).
  - A plain `uint` or `int` breaks `CPortalTransition::Update`'s stack layout, under stock RC1 too.
  - A `float` breaks three of the four.
  - `uchar x0_[4] __attribute__((aligned(4)))` breaks three of the four.
  - A 4-aligned struct of 4 bytes works like the union.

So the copy rule of the Prime games' Wii compiler is GC 3.x's: RC1 under `legacy_struct_alignment` for operand
alignment, and RC1 without it for allocation. This is the first behaviour of `Wii_Retro`. The rstl header change
belongs in Echoes' rstl, with the patched compiler.
Moving Echoes to the patched compiler: [wii-retro-echoes.md](wii-retro-echoes.md).

**The destroy pretest.** GC 3.0a5.2, 3.0a5 and 3.0a3 keep the `if (begin != end)` check in the calling case, like
RC1. So no edge version drops it. Echoes ruled out the source: its unguarded range-destruction policy is only on
types whose destructor calls, and removing the guard from the source gains 47 functions and loses 42. So it is a
second behaviour of the original compiler.

The guard (`cmplw rA,rB; beq exit`) comes just before the loop's `b test`, and `test` is `cmplw rA,rB; bne body`,
with `exit` right after it. The target drops the guard when both:
- it compares the same registers as the loop's test, and
- the loop body makes a call.

Our R3ME01 objects against the target, per function:
- Same registers, body calls: 302 dropped, 4 kept.
- Same registers, no call: 14 kept, 3 dropped.

All the exceptions are in functions with more than one such loop, where ours don't pair one to one with the target's.
Of the guards the target keeps with the same registers, 20 have a call-free body of one instruction, and 3 are calling
loops with an explicit source guard (`WriteAnimData` ×2, `CScriptTrigger::AddInhabitant`).
- Whether the registers are the same follows from whether `begin` is used after the loop. In a vector destructor,
  `begin` is the vector's data pointer, which is freed after the loop. When the body makes no call, `begin` stays in
  `r3` for that free, and the loop counts in its own register (`r4`). When the body calls, the pointer is loaded
  again for the free. `begin` and `cur` then share one register, so the guard's compare is the loop test's.
- So the register condition alone does not decide it: the call does.

Measured by deleting the guard from our compiled objects after the patched compiler
(`gc2/tools/wii_drop_pretest.py`, on R3ME01 with the patched RC1 and the `CModelFlags` union):
- Same registers and the body calls (750 guards): 12,898 → 12,952 exact, 54 gained, 0 lost; 150 improved,
  3 regressed. Gained include `~CCameraColliderGroup`, `~CGuiFrame`, `~CModel`, `~CPakFile`, `~CMapWorld`, `~CWorld`
  and `CToken` vectors. Regressed: `AddInhabitant` (explicit guard, kept in the target), `~CGenericFSM2State` and a
  `reserve`, each by 1 point or less.
- Also when the body makes no call (770 guards): 55 gained, 3 lost, 18 regressed.

The deleted guard is 8 bytes, `cmplw` and `beq` (`~CCameraColliderGroup` matches exactly with it gone), not 12 as
the Echoes CBallCamera page says.

Which pass does this is not known. It acts after the registers are known, or on values it knows are equal, and only
on loops whose body calls. Neither edge version has it, so `Wii_Retro` adds it. The search for where is Phase 3's.

**In the compiler.** `gc2/tools/wii_patch_pretest.py` adds the rule to RC1 as a pass in a new section (`.retro`,
source `gc2/tools/wii_retro/drop_pretest.c`, built with clang for i686). `assemblefunction` calls it before
`optimizefinalbranches`. It deletes the `cmpl` and `bt` from the PCode blocks, then recomputes the offsets.
- The section also holds RC1's base relocations with the pass's added. `sjiswrap` loads the compiler at another
  base, and without them the pass crashed.
- RC1 layouts it relies on:
  - PCodeBlock: as in 2.7 (`pcodeCount` at 0x28).
  - PCode: `op` at 0x28 and operands from 0x2c, 14 bytes each. A register's number is at +4, an immediate or a
    label at +2.
  - Operand kinds: register 0, immediate 2, label 6.
  - Opcodes: as in 2.7 up to `blrl` (20); `cmpl` is 85.
  - `pcbasicblocks` is at 0x751c88.

Applied after `wii_patch_struct_copy.py`, on R3ME01 with the `CModelFlags` union: 12,898 → 12,952 exact, 54 gained,
0 lost. Every function's match is the same as with the object post-pass.

**A third difference: copying a struct member by member.** `CRevolutionMotionProcessor::Update` (source in rstl,
`src/Kyoto/Input/CRevolutionMotionProcessor.cpp`) does `mStatus = status`. `WPADFSStatus` is an SDK C struct,
0x32 bytes, 2-byte aligned, with a `DPDObject obj[4]` array.
- The target copies it member by member, each load next to its store: `lhz`/`sth` for `button`, `lha`/`sth` for the
  `s16` fields, `lbz`/`stb` for the bytes. The array is a block copy in pairs (`lhz r6; lhz r0; sth r0; sth r6`), the
  shape of 2.7's `move_block_via_load_store_sequence`. The frame is 0x50.
- GC 3.0a3, 3.0a5 and 3.0a5.2, RC1 and the patched RC1 load every member (the array as 16 halfwords) before storing
  any. In `Update` that runs out of registers and spills, giving a 0x90 frame. 3.0a5.2 and the patched RC1 give the
  same code for this function: 78.3% under stock RC1, 71.0% under the patch.
- GC 2.7 copies it the way the target does, in C++: member by member from the implicit `operator=`, with the array
  as a block move, though with words where the target uses halfwords. In C, 2.7 copies the whole struct as one block.
  So the target keeps 2.x's copy for this case, and 3.x's operand alignment.
- RC1's `IRO_DecomposeAssignments`, `IRO_SerializeAllAssignments` and `IRO_RecomposeAssignments` are not involved.
  They are switched by `copts` bytes 0x74f51f, 0x74f51c and 0x74f520, and skipping them changes nothing.
- It is narrower than "2.x's copies". In R3ME01, 70 functions where we load 8 or more times in a row from one base
  register, such as `CDamageVulnerability::MakeImmuneToRadiusDamage`'s 23-word copy, look the same in the target.
  Only 4 differ, and 3 of those (`MoveGroundCollider_New`, `CSlideShow::OnMessage`, `LoadDynamicLight`) differ from
  the target in other ways too. So `Update` is the only clean case.
- Not settled: what makes the target copy member by member here. Candidates are copy assignment against copy
  construction (`MakeImmuneToRadiusDamage` is a copy construction), or C structs. The code to find is where RC1 and
  3.0a5.2 expand a C++ implicit copy into pieces that are all loaded first.

**A fourth difference: when template instances' vtables are emitted.** Echoes' resource factories return
`rs_new X(in)` through `TToken`, which instantiates `TObjOwnerDerivedFromIObj<X>::GetNewDerivedObject`. That
constructs the owner, so its vtable and virtual destructor are needed. Five R3ME01 units (`CSpatialPrimitive`,
`CStaticGeometryMap`, `CDecalDataFactory`, `CCharacterFactoryBuilder`, `CAllFormatsAnimSource`) match function by
function, but fail to link on the order of the functions in the object.
- The target puts `~TObjOwnerDerivedFromIObj<X>` right after `GetNewDerivedObject`, before the file's later
  functions. Every compiler we have puts it at the end of the file: GC 2.6, 2.7, 3.0a3 and 3.0a5.2, and Wii
  0x4201_127, 1.0, RC1, 1.0a, 1.1 and 1.3. 2.7 parses the Wii source once its template friend of `auto_ptr` is
  removed.
- In RC1, `CParser_ParseDeclaration` ends each top-level declaration with `CMid_GlobalDeclarationActions`
  (0x47c120), which only works through the queue of inline and deferred functions. Vtables (`CClass_Finalize`,
  0x552c20) and class template members (`CTempl_Instantiate`, 0x5c0fe0) wait for the end of the file (`fn_0047bf90`).
  The destructor is reached only through the vtable, so it comes last.
- Running both after every declaration puts the destructor where the target has it, but it also moves
  `vector::reserve` up next to the constructor that uses it, where the target leaves it at the end. It also loses
  `CCharacterFactoryBuilder`'s `CanBuild` and `GetResourceIdByName`, whose inline definitions follow the vtable. So the
  target emits a template instance's vtable and virtual functions straight away, and leaves other classes' vtables and
  templates without a vtable for the end of the file.
- `-ipa file` alone (a per-file flag in Echoes) gives the target's order for `CSpatialPrimitive`, but not the
  other four.

`gc2/tools/wii_patch_template_vtables.py` adds this as a pass in a new section (`.retrovt`, source
`gc2/tools/wii_retro/template_vtables.c`), which replaces the call to `CMid_GlobalDeclarationActions`. After the
declaration's actions, it repeats until neither step does anything:
- It runs `CClass_Finalize` with its two class lists (0x756ff4 and 0x751d7c, nodes `{next, class, ...}`) cut
  down to class template instances, then restores them.
- For each instance that has a vtable (`TypeClass` 0x1a; template instance flag 0x800 in the flags at 0x22), it
  instantiates the referenced members as `CTempl_Instantiate` does (0x5c1140, then 0x47c360). Class templates link at
  0x30 with instances at 0x42; an instance links the next at 0x30, with `is_specialized` at 0x41.
- Then it runs the declaration's actions again.

Results, applied after `wii_patch_struct_copy.py` and `wii_patch_pretest.py`:
- The five units: the destructor is placed as in the target in all five. `CDecalDataFactory` and
  `CAllFormatsAnimSource` get the target's whole order. `CSpatialPrimitive` and `CCharacterFactoryBuilder` get it with
  `-ipa file`.
- Without `-ipa file`, the two `reserve` instances left for the end of the file come out in the opposite order to
  the target's. In `CStaticGeometryMap` this also happens with `-ipa file`. That file explicitly instantiates
  `vector<pair<uint,int>>` at its end, which may be the cause.
- R3ME01 (`CModelFlags` union): 12,952 → 12,953 exact, 0 lost, 2 improved, 0 regressed. Complete units go from 603
  to 607 (`CBasicSwarmData`, `CGameHint`, `CHintState`, `CIngSpotData`), and `CPatterned` goes from 27.2% to 32.5%.

### Follow-up probes (2026-11-16)

**`CStaticGeometryMap`.** All three Wii targets put the `vector<pair<uint,TEditorId>>::reserve` body before
`vector<pair<uint,int>>::reserve`. The probe source explicitly instantiates the latter class at the end of the
file. With the three-patch compiler and `-ipa file`, all ten R3ME01 functions match individually, but the
reserves are reversed. The link also retains three extra class members, `begin`, `end` and `data`, totaling
0x24 bytes of code. With DOL alignment the file grows by 0x20. Explicitly instantiating only `reserve` with IPA
off restores the DOL size and still reverses the two reserves.

In a scratch copy of the probe source, removing the guessed `CopyMappings` and the explicit class instantiation
and adding an ordinary reference below the resource factory gives the target order with the three-patch compiler
and the default non-IPA flags. The linker strips the diagnostic caller and retains the weak reserve body.

```cpp
void ReferenceScanReserve(rstl::vector< rstl::pair< uint, int > >& v, int count) {
  v.reserve(count);
}
```

Replacing only the original extracted `CStaticGeometryMap.o` in each existing link response file reproduced the
original DOL in all three versions. Each original-object control also reproduced its DOL.

| Version | Exact target functions in the unit | Relinked DOL SHA-1 |
| --- | ---: | --- |
| R32J01 | 9 / 9 | `442947ba57dce414917feab0e75a8227690b3e4b` |
| R3ME01 | 10 / 10 | `2375606f4e9429a699cfa02728b0bb1176421226` |
| R3MP01 | 10 / 10 | `077712e46eb7cf2488942f337d215636edbc972d` |

The same diagnostic source works with the existing compiler patch chain across the Wii builds. It establishes
that ordinary implicit instantiation can produce the target layout. R3ME01 has live references to the second
reserve in `CPlayerTargeting` and `CAnimSourceReaderBase`. The G2ME01 and G2MP01 geometry units retain the same
vector type's copy constructor. The original geometry source that instantiated the vector is still unknown.

**`CRevolutionMotionProcessor::Update`.** The originals for R32J01, R3ME01 and R3MP01 all interleave the member
loads and stores with a 0x50 frame. The R3ME01 probe already emits scalar copy pairs and two-halfword array
groups with `-schedule off`, so the existing assignment expansion can produce that structure. To isolate the
stage that causes the spills, bypass each `scheduleinstructions` call in a scratch copy of the three-patch
compiler. The call at `0x46B3E4` runs before register allocation, and the call at `0x46B335` runs after it.

| R3ME01 compiler probe | Frame | Function match |
| --- | ---: | ---: |
| Three-patch RC1 | 0x90 | 70.94% |
| Skip post-allocation scheduling | 0x90 | 69.11% |
| Skip pre-allocation scheduling | 0x50 | 76.17% |
| `-schedule off` | 0x50 | 67.75% |
| `#pragma block_assign on` | 0x80 | 70.21% |

The pre-allocation scheduler accounts for the larger frame. Skipping it still changes the copy order and the
surrounding schedule, so it is a diagnostic. Compare how the scheduler handles dependencies and register
pressure in the copy block before changing the compiler.

### File IPA and inlined-local register order

The Echoes TU-matching work reported that applying `-ipa file` to Retro's Wii units gained 633 exact R3ME01
functions and lost 60. With three experimental Trilogy RSTL features disabled it gained 717 and lost 52. Of
those 52 losses, 42 are register-only differences in inlined RSTL loops.

**Native controls.** With file IPA, the three-patch compiler gives the two pointer locals in
`CSimplePool::ObjectIsLive`'s inlined tree search `r6` and `r7` in the opposite order from the target.
`CSpatialPrimitive::CSpatialPrimitive(CInputStream&)` swaps `r28` and `r29`. Without file IPA both function
bodies match on R3ME01, and the two `CSpatialPrimitive` reserves trade places. Stock Wii 0x4201_127, 1.0,
1.0a and 1.3 give the same per-function mismatches under file IPA. GC 3.0a5.2 emits the same swapped
`ObjectIsLive`. RC1's `-inline bottomup`, `nobottomup`, `noauto`, `deferred` and `level=4` leave both
residuals in place. Skipping the call-graph expansion at 0x558B5B produced substantially less inlined code in
all four control units, including the pool and spatial cases.

**RC1's local lists.** `CIRStream_UnpackCode` (0x52A950) reconstructs the lists at offsets 0x0A
(arguments) and 0x0E (locals) in the unpacked record. The list reader (0x52ACC0) appends in serialized
order. `CFunc_AppendLocal` (0x510BB0) prepends to `locals` (0x758120). The inline argument setup routine
at 0x55A740 adds the callee's locals with this helper. The call-graph inliner sets a flag at 0x719068.

- `ObjectIsLive` responds to order during call-graph body unpacking (call at 0x5C0AD6). Reverse the
  pointer/reference objects (type tag 12, `VarInfo+8 == 0`) among their positions in the locals list after
  unpacking, while the call-graph flag is set. This makes both `ObjectIsLive` and `ObjectUnreferenced`
  exact on R3ME01. Reversing the whole list also corrects the `ObjectIsLive` register swap, but moves its
  iterator stack slots. The 208-byte result of the pointer probe occurs once in each original DOL, at
  `0x80355B14` (R32J01), `0x803571EC` (R3ME01) and `0x80359A90` (R3MP01).
- `CSpatialPrimitive` responds to order when inline arguments and callee locals are installed (call
  at 0x5593EA to 0x55A740). Reverse the eligible scalar and pointer/reference objects (tags 1, 2, 3
  and 12, `VarInfo+8 == 0`) among their positions for the duration of that call, while the call-graph
  flag is set. The constructor becomes exact and the factory stays exact. All three Wii DOL hashes
  match when only this object is replaced in the link. Reversing the full list at this site also
  fixes the constructor and introduces a register difference in the factory.

**Register allocation trace.** A hook at 0x5B3F42 recorded the GPR operands in PCode before color
selection and the assigned colors before the rewrite to physical registers. The instrumented and
uninstrumented compilers produced byte-identical objects in all 13 comparisons.

- For both pool functions, the recorded opcode stream, block boundaries and GPR operands agree with
  the no-IPA result after swapping virtual registers 35 and 36. The graph visits 36 before 35 and
  assigns them `r6` and `r7` in both modes. The call-graph pass expands `find_node` into `find` first.
  The original `n` and `needle` are then cloned again along with `find`'s locals, changing which value
  receives each virtual register. In the exact `rstl::list<CAreaListItem>` destructor, the broad
  unpacking reversal makes the same kind of upstream change to `it` and `cur` and creates an
  `r29`/`r30` mismatch.
- On the path to `CSpatialPrimitive`, file IPA clones the vector stream constructors after their
  locals lists have acquired three generated pointers. Ordinary inlining sees the same constructors
  with `i` and `count` and without those pointer locals at that call site. Moving the named scalar
  locals ahead of the generated pointers during clone setup, preserving order within both groups,
  matches the constructor and factory in R32J01, R3ME01 and R3MP01 and preserves the order of their
  two reserve instances. The renderer destructor and `SAdditiveFlinchState::Start` stay exact on R3ME01.
- `CFunc_AppendLocal` at 0x55A9AE also makes it possible to distinguish locals cloned during the
  call-graph pass from other numbered temporaries. At the next clone, moving the previously cloned
  register-eligible locals behind the other locals and reversing them matches the constructor and
  both pool functions on R3ME01. The constructor stays exact and `ObjectIsLive` occurs once in each
  of the other two original DOLs. The renderer destructor regresses in all three builds, and
  `SAdditiveFlinchState::Start` regresses on R3ME01. Keeping the previously cloned locals in their
  existing relative order still fixes the constructor but leaves both pool functions mismatched.

**R3ME01 regression sweep.** Rebuilt all 898 Wii RC1 entries from the probe's Ninja config for each run.
File IPA was enabled for the C++ entries in every run. Other compiler inputs came from the same existing
build. The report covers 18,016 target functions with `functionRelocDiffs=data_value`.

| Probe | Exact functions | Gained | Lost |
| --- | ---: | ---: | ---: |
| Three-patch RC1 baseline | 12,758 | | |
| Reverse all locals at call-graph body unpacking | 11,455 | 21 | 1,324 |
| Reverse only pointer/reference locals at call-graph body unpacking | 12,638 | 16 | 136 |
| Reverse all callee locals during inline argument setup | 12,648 | 31 | 141 |
| Reverse only scalar and pointer/reference callee locals during inline argument setup | 12,682 | 31 | 107 |
| Reverse only generated pointer/reference locals at call-graph body unpacking | 12,757 | 0 | 1 |
| Move named scalar callee locals before generated pointer/reference locals, preserving group order | 12,765 | 12 | 5 |
| Move callee locals generated during the graph pass after other locals, preserving group order | 12,725 | 7 | 40 |
| Move previously cloned callee locals after other locals and reverse the moved group | 12,698 | 31 | 91 |

**Inline expansion groups.** RC1 has two ways to add locals during inline setup, both in the routine at
0x55A740. For a formal argument that needs a local, RC1 builds an assignment and renames and prepends
the formal at 0x55AB35–0x55AB4F. Once argument setup is complete, it walks the callee's local list and
renames and prepends each object at 0x55A9A0–0x55A9B8. Each prepend reverses the order of the objects
visited by that walk. The two callers at 0x5593EA and 0x55A24E both unpack the callee before setup.

An observation hook tracked the first and most recent setup invocation for each numbered local across
copies. Seven instrumented translation units compiled byte-identically to their controls. The two
origins distinguish dependencies that the earlier one-bit provenance probe grouped together:

- In `ObjectIsLive`, the `n`, `needle` and `result` declarations share a first and recent clone as
  they enter from `find`. The corresponding list in `CWeaponMgr::GetIndex` has the same shape.
- In `SAdditiveFlinchState::Start`, eight formal-argument locals from one earlier call arrive at the
  outer constructor through eight distinct later clones. Reversing by the first clone breaks the
  function. Reversing by the most recent clone preserves it and keeps the renderer destructor exact.
- In `CSpatialPrimitive`, the three extra pointers in each vector stream constructor include two
  materialized formals and one cloned source local. Moving just the materialized formals past
  the constructor's own named scalar locals fixes the constructor on R3ME01. Moving just the
  cloned source local leaves the original swap.

The full sweep for reversal within the most recent clone group reached 12,689 exact functions
(41 gained, 110 lost), fixing both pool functions, two `CWeaponMgr` functions, and two
`CActor` functions. It leaves the spatial constructor mismatch. Split-phase probes were also
insufficient. Bypassing only the recursive descent at 0x558AB1 still runs the expansion pass for
each body in the call-graph list, but leaves inlining differences in the spatial and weapon cases.
Calling ordinary expansion before initial packing fixes the camera shaker destructor and introduces
a mismatch in the flinch control. Moving it to file-IPA code generation after bypassing graph
expansion fixes both pool functions and leaves large differences in several other controls.

Comparing the existing forced-file-IPA and configured sweeps with objdiff's decoded operands
identified 164 cases where the modes differ only in GPR operands and exactly one mode matches.
Ordinary inlining matches 30 and file IPA matches 134. Seven translation units contain both kinds
of result, so selecting a single mode for each file would leave some matches unresolved.

**Native register-order pass.** [`inline_locals.c`](../gc2/tools/wii_retro/inline_locals.c)
records local origins during the call-graph walk. Before each inline setup it applies two rules:

1. Place the callee's own named scalars ahead of pointers cloned from nested calls. Preserve the
   relative order within both sets. Include materialized formals only if they are pointers to
   `void`, as in the vector placement-construction path. Moving typed formals breaks the exact
   `CGameHintInfo`, `CMapWorldInfo` and `CPlayerEnergyDrain` controls.
2. Within a previously cloned group, reverse the named same-pointee pointer declarations when
   the group contains a const pointer and at least two mutable pointers. Apply this on the next
   copy of the group. This fixes the inlined tree searches and preserves the different loop in
   `CResFactory::EraseFromLoadList`.

The partition runs before the group reversal. Reversing first and excluding the reversed
members from partitioning regresses exact vector insertion controls in `CMapWorldInfo` and
`CPlayerEnergyDrain`. The pass selects locals by clone provenance and native types.

| File-IPA probe | Exact functions | Gained | Lost |
| --- | ---: | ---: | ---: |
| Three-patch RC1 baseline | 12,758 | | |
| Partition cloned pointers originating from declarations | 12,776 | 18 | 0 |
| Partition only pointer-to-void formals | 12,759 | 1 | 0 |
| Reverse qualified same-pointee groups | 12,765 | 7 | 0 |
| Native relink with both inline-local rules | 12,784 | 26 | 0 |
| Native relink with inline-local and reference-order passes | 12,786 | 28 | 0 |
| Native relink with iterator ordering added | 12,788 | 30 | 0 |
| Native relink with deferred inline-formal ordering added | 12,791 | 33 | 0 |
| Native relink with nested copy ordering added | 12,801 | 43 | 0 |

The combined pass changed 65 of the 898 objects with file IPA enabled for C++.
Ninety functions improved and 18 previously inexact functions scored lower. The PE prototype
and native relink produced byte-identical outputs on all 898 entries.

The inline-local pass fixes 15 of the 30 cases where the register-only comparison favored
ordinary inlining. Four of the residual functions also have matching pre-color PCode under a
one-to-one renaming of virtual GPRs. They are `CModelData::LockTextures`,
`CScriptTrigger::ClearInhabitants`, `CFrameDelayedKiller::FlushAllocationsForFrame` and
`CMetroidAreaCollider::SphereCollisionCheck_Cached`. The exact file-IPA controls
`CModelData::Touch` and `CMetroidAreaCollider::SphereCollisionCheckBoolean` have the same
property.

**Source reference order.** RC1 assigns virtual registers to named locals before generated
temporaries. `LockTextures` loses both named `model` references during copy propagation.
The cached collision check loses the named `owner` reference after propagating through
`tree`. The surviving temporaries acquire a later register rank. The
[`reference_order.c`](../gc2/tools/wii_retro/reference_order.c) pass records the references at
linearization and tracks their replacements after copy propagation. During virtual register
assignment, an eligible single-use alias lends its original local slot and name to a live
temporary of the same pointee type. Direct const aliases keep RC1's existing order, as required
by the exact `CSwarmBasics::CachePose` and `CCoinGameMode::EndGame` controls. A chained const
alias such as `owner` keeps the earlier slot.

The reference pass changes five forced-file-IPA objects. Three functions improve and two
previously inexact functions score lower. All 898 objects remain byte-identical with the
configured flags. The cached collision check is exact in all three Wii builds. The
`LockTextures` instruction bodies match all three original DOLs with branch displacements
masked. R32J01 and R3MP01 objdiff reports differ on the unnamed target of two calls to
`CModel::UnlockTextures`. Both calls reach the verified `UnlockTextures` body in each
DOL at `0x80376224` and `0x8037A51C`, respectively.

**Optimizer-generated iterators.** In `FlushAllocationsForFrame`, common-subexpression
elimination and scalarization add pointer locals in opposite orders under the two inlining
modes. The scalarized iterator in both functions is updated with a load through the previous
iterator value. That operation distinguishes it from integer address increments in vector
loops. Reordering all such pointer-chasing iterators behind every optimizer temporary made
both functions exact and regressed four controls. Limiting the move to common-subexpression
temporaries preserves those controls and fixes the delayed allocation loop. The trigger loop
also has a live hoisted integer temporary ahead of the iterator under ordinary inlining.

The native pass records which optimizer pass created each temporary and identifies the
pointer-chasing assignments immediately before delinearization. At virtual register assignment,
it moves the iterators behind the other register-eligible temporaries up through the last live
common-subexpression or hoisted integer temporary. The move runs only when the function has a
live common-subexpression temporary. Both trigger and allocation loops match all three Wii
builds. This change affects six forced-file-IPA objects. Four functions improve and three
previously inexact functions score lower. Configured outputs remain byte-identical. Across the
three passes, all 134 cases that favor file IPA stay exact and 19 of the 30 that favor ordinary
inlining are now exact under file IPA.

**Materialized inline arguments.** `CSaveWorldIntermediate::InitializePump` and
`CMemoryCard::InitializePump` contain a copied group of three same-pointee pointer formals.
`CMediumAllocPool::ClearPuddles` has a copied group with a declared integer counter, a
materialized integer formal and a materialized pointer formal. Reversing these groups at
inline setup changes exact controls. The pass records each group during its second inline
copy and reverses only the surviving register-eligible locals at virtual register assignment.
The group is scoped to that copy. The three functions match in R3ME01, and the same rule fixes
the inexact entries in R32J01 and R3MP01. The 319-object forced-file-IPA regional sweeps preserve
all exact functions in both builds. In R3ME01, 24 functions improve, two previously inexact
functions score lower, and the 898 configured outputs stay byte-identical.

**Nested inline copies.** Seven of the remaining cases involve the ordinary RSTL
`uninitialized_copy_n` template. Its declaration order in the reconstructed headers
comes from `RSTL_COPY_N_COUNTER_FIRST`. R32J01 declares the counter in the loop after both
pointers. R3ME01 and R3MP01 declare it before both pointers. Forcing the R32J01 source
order through an include overlay makes all seven R3ME01 functions exact. Two one-iterator
copy loops also match with a named source iterator. One copies decal vertices. The other
copies floats inside `CInputStream::Get<SLdrConnection>`. Applying the shared header
changes globally regresses eight exact R3ME01 file-IPA functions, including nested copies
in vector reserve and assignment paths.

The native pass detects the copy-loop shape when a constructor copies an inline group.
The group has one declared counter, a materialized integer argument, and two materialized
same-pointee pointer arguments. The one-iterator form includes the materialized arguments
in the reordering. In the two-iterator form, the declared locals participate when the counter
precedes both iterators in the clone. The const-source copies in `reserved_vector` retain
their order. If another expansion copies the marked group, code generation applies the
same ordering when the enclosing function returns an object in memory. This covers
`CAnimSourceReader::VClone` and `CInputStream::Get<SLdrConnection>`. The ordering is
temporarily applied to live virtual register candidates. The source headers are shared
across the tests, and code generation selects the rule from the native types, declaration
order and result ABI.

All 30 audited cases that previously favored ordinary inlining now match with file IPA
in R3ME01, and all 134 file-IPA controls stay exact. The 30 cases also match R32J01 and
R3MP01 using extracted objects or instruction comparisons against the original DOLs with
relocations masked. Across the 898 R3ME01 objects, the final compiler changes 92 outputs
under forced file IPA. It improves 132 functions and lowers the scores of 25 previously
inexact functions. The configured outputs remain byte-identical. In the 319-object
regional sweeps, the work after the initial inline-local pass gains eight exact
functions in R32J01 and twelve in R3MP01. Both sweeps have zero exact losses.

Both pool function bodies occur once in each original DOL. The comparison for
`ObjectUnreferenced` masks the linker's branch displacement. The spatial constructor and
factory match, and replacing the spatial object in each reference link reproduces the original
DOL SHA-1. All tests used the same shared RSTL sources.

| Wii build | `ObjectIsLive` | `ObjectUnreferenced` | DOL SHA-1 with spatial object |
| --- | ---: | ---: | --- |
| R32J01 | `0x80355B14` | `0x803556E0` | `442947ba57dce414917feab0e75a8227690b3e4b` |
| R3ME01 | `0x803571EC` | `0x80356DB8` | `2375606f4e9429a699cfa02728b0bb1176421226` |
| R3MP01 | `0x80359A90` | `0x8035965C` | `077712e46eb7cf2488942f337d215636edbc972d` |

## Phase 3: decompile both edge versions

The long-term goal, done like the 2.x versions in gc2:

1. **RC1 first.** It has names. Start from gc2's 2.7 text where a unit still exists ("Start from the reference's
   verbatim text, then apply the target version's changes", matching-guide.md §1.1). New units (CIRStream, the SWCH
   group, LocalCSE...) are decompiled from scratch.
2. **3.0a5.2 as a version of the same sources,** with `VERSION_` macros as for `VERSION_GC_1_3_2`, guided by the diff
   map.
3. **Order.** The units Phase 2 names come first: the struct move, the stack-frame layout, the loop guard. Next come
   the units every compile goes through (the backend, the scheduler, the IR optimizer), and the driver, plugin and
   build-agent layers last.
4. **How a unit is checked.** By bytes, if the V3.2 x86 host compiler is found (search the decomp.dev archive, the
   Internet Archive and CodeWarrior for Windows discs; confirm it by compiling one RC1 function). Otherwise by
   function equivalence (`prove.py`), with an executable built from the sources alone that reproduces RC1's output on
   Echoes' Wii build.

## Phase 4: `Wii_Retro`

1. The native build uses `config/Wii_Retro/config.json` (`derived_from: Wii_1_0RC1`). As Phase 3 replaces
   more RC1 code, move its changes into the shared compiler sources as `VERSION_` code, as GC_2_0p2 does.
2. The native relink uses the existing RC1 units with the source replacements above. The ABI of the
   replacements follows RC1's data layouts. Building the whole compiler from source follows Phase 3.
3. **Make it usable in mwcc-tools.** It has no original, so it cannot be recognised by hash the way other compilers
   are. Decide how a project names it, then add the WebAssembly module and the oracle hooks (wasm.md), so it builds
   in process and traces.
4. **Echoes:** switch `retro_mw_version` for R32J01, R3ME01 and R3MP01, then check the suite, the no-loss set, and
   all 447 image hashes.

## Decision points

- **After Phase 2's patch:** whether RC1's legacy copy path is separable from its stack layout. If it is, the copy
  rule is a small, local change. If not, the decompilation of the struct move and frame layout is the critical path.
- **The destroy pretest:** settled. It belongs to neither edge version (see Phase 2's results).
- **Host compiler found or not:** whether Phase 3 aims for matching or for equivalence.

[Trilogy-compiler-struct-copies.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-struct-copies.md
[Trilogy-compiler-destroy-pretest.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-destroy-pretest.md
