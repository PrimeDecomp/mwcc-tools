# Wii_Retro compiler research

These notes record the evidence behind the [current build and patches](wii-retro-compiler.md).
Addresses and scores refer to the stock version or experiment named in each section.

- [Current validation](#native-relink-and-validation) and [remaining work](#remaining-work).
- [RC1 toolchain evidence](#what-mwcc-tools-adds) and [CodeWarrior host trials](#rc1-host-compiler-trials-2026-11-18).
- [Recovered native control flow](#recovered-native-control-flow).
- [Corruption Lua, LZO, and compiler scheduling](#corruption-lua-lzo-and-compiler-scheduling).
- [Ordering experiments](#ordering-compatibility-and-experiments) and [remaining differences](#remaining-order-differences-and-source-controls).
- [Destroy-loop guard folding](#destroy-loop-guard-folding-2026-11-17),
  [aggregate-copy scheduling](#aggregate-copy-scheduling-2026-11-17), and
  [register order](#file-ipa-and-inlined-local-register-order).

## Native relink and validation

The build verifies stock RC1's SHA-1, splits it into relocatable COFF objects, and compiles the
recovered routines and patches with Clang. LLD preserves CRT and exception-table order and
rebuilds imports and base relocations. Clang and LLD 23.1.1 produce
`f471ded8be5dc6d05e61c29b0bb7261f4c89c684`.

- In R3ME01, the compatibility work raises the forced-file-IPA score from 12,758 to 12,801 exact
  functions out of 18,016, with zero exact losses. All 30 audited register-order cases match
  under file IPA in R32J01, R3ME01, and R3MP01. [Register-order results](#file-ipa-and-inlined-local-register-order)
- On the Echoes PR #1992 source and flags, the refined inline-local rule restores the lost
  matches and preserves every existing exact match across all three Wii builds.
  [Echoes integration follow-up](#echoes-integration-follow-up)
- The source-recovery regressions preserve 3,072 Echoes objects across the three regions under
  configured and forced file-IPA flags. Four PCH producer/reader combinations agree. The
  diagnostic `CSpatialPrimitive.o` relinks reproduce all three original DOL hashes.
  [Validation commands](../gc2/tools/wii_retro/tests/README.md)
- The earlier three-patch relink matches the binary patch chain under both flag configurations
  and reproduces the three DOL hashes with the `CStaticGeometryMap` control.

## Corruption Lua, LZO, and compiler scheduling

The hash-verified G2MEAB prototype and RM3E01 retail DOLs both contain the Lua 5.0.1 version
string. The 32 Lua and LuaPlus source files and 41 headers also agree between the checkouts.
Both games pass LZO's `0x1080` version constant to `__lzo_init2`. The prototype enables
`LZO_TEST_DECOMPRESS_OVERRUN`, while the Echoes source imported into retail Corruption leaves
it off. The decoder is 0x400 bytes in the prototype and 0x2b4 bytes in standalone retail.
GC 2.7 with the configured prototype flags reproduces all 544 Lua and four LZO instruction
bodies after masking relocation fields.
[Lua release archive](https://www.lua.org/ftp/) · [LZO 1.08 release page](https://distro.ibiblio.org/amigolinux/download/Libs/lzo-1.08/oberhumer.comopensourcelzo.html)

All ten retail LZO function bodies match after masking relocation fields with the merged source
and the RC1 relink at `-O4,p -inline off -func_align 4`. Keeping the prototype's overrun macro
produces the much larger checked decoder. The retail initialization split contains only text.
The compiled object also emits data and the legacy `__lzo_init` helper, so a source-linked
hash requires an additional object and data ownership check.

The Lua sweep used 17 compiler versions from GC 2.6 through Wii 1.7. Options covered
inlining, IPA, optimization level and size, scheduling, peephole optimization, alignment,
small data, floating-point contraction and load/store multiple. The best RC1 profile
kept file IPA and used `-inline noauto -func_align 4 -sdata 4 -sdata2 4`. Function pairing
used known native names and unique instruction correspondences. The instruction-body
counts below are from the initial source and split inventory. They mask relocation fields
and include every function in the 30 retail Lua splits available at the time.

| Compiler under the same Lua flags | Exact instruction bodies |
| --- | ---: |
| Wii_Retro | 621 / 714 |
| Installed Wii `0x4201_127` | 632 / 714 |
| Wii_Retro with an experimental GQR0 side-effect change | 632 / 714 |
| Installed Wii `0x4201_127` with the RC1 shortcut restored | 621 / 714 |

The eleven changes affect the scheduling of paired-single register saves and restores
in functions including `math_atan2`, `luaB_ipairs` and `luaV_execute`. Both compilers use
the same 750/Gekko timing entries for those instructions. In RC1, `vformatpcode` at
`0x5a8ed0` skips `pcsetsideeffects` for an implicit GQR0 operand. The installed
`0x4201_127` executable has an unconditional jump around the shortcut. Its provenance
is unknown. Reciprocal patches gain and lose the same eleven bodies, and the trace
shows the side-effect bit is already present before final scheduling. On stock RC1,
keeping `-proc gekko` and adding `-pragma "scheduling 7400"` reproduced ten of the eleven
bodies. It missed `luaB_foreachi` and lost one previously exact Lua body, for 630 of 714
overall. The pragma changes the scheduler's CPU model without changing the code-generation
target.

Both Corruption Trilogy DOLs match stock RC1 and default Wii_Retro on all eleven Lua cases.
For example, `luaV_execute` starts at `0x80466830` in NTSC-U Trilogy and `0x80465f4c` in PAL
Trilogy. The three identical masked math bodies were identified by their calls to `atan2`,
`fmod`, and `pow`. A second control searched the original DOLs for shared functions that
match Echoes with Wii_Retro and whose instruction order changes with the GQR0 experiment.
The comparison masks only relocation fields.

| Echoes and Corruption Trilogy region | Shared bodies with the Wii_Retro schedule in both games | Also match standalone Corruption in the experiment |
| --- | ---: | ---: |
| NTSC-U `R3ME01` | 61 | 60 |
| PAL `R3MP01` | 17 | 16 |

The last column covers 60 distinct functions across the two regions. For example,
`CVectorBiquadFilter::Update` matches the default schedule at `0x80348488` in NTSC-U Echoes
Trilogy and `0x803bd5b4` in NTSC-U Corruption Trilogy. At `0x803bf0c8` in standalone
Corruption `RM3E01`, its body matches the GQR0 experiment. With the same Echoes sources and
flags, the 7400 scheduler reproduced 24 of the 60 NTSC-U standalone bodies and six of the 16
PAL controls. This identifies a scheduling behavior shared by both Trilogy games and leaves
the original compiler binary and version open.

The GQR0 experiment lost 577 exact Echoes matches on R3ME01, 295 on R32J01, and 298 on R3MP01
under `functionRelocDiffs=none`. Wii_Retro retains the RC1 formatter behavior. The relink
replaces `vformatpcode` and `pcsetsideeffects` with C, and the formatter fixture matches
stock RC1 in 1,116 cases. The scheduler's block pass, dependency construction, instruction
selection, and 7400 timing initialization relink from C. The 750/Gekko serialization callback
reads the same timing-table field in RC1 and `0x4201_127`.

The recovered scheduler produced byte-identical Echoes objects in all 1,639 configured
compilations and all 1,639 file-IPA control compilations. Linking the configured objects
reproduced the shipped DOL hashes for R3ME01, R32J01, and R3MP01.

Retail Corruption's unchanged Lua sources match 703 of 714 instruction bodies with the
recovered MSL headers and stock 750 scheduling. The headers use locale-backed wide-character
tables, the implicit C declarations evidenced by the native call sites, and an `int`-typed
`UCHAR_MAX`. The Wii Corruption Kyoto declarations also reproduce the native relocations
from `LuaMemory.cpp` to `CMemory::Free(void*)`, the unsigned `CCallStack` constructor,
and `gkUnknownType`. Corruption retail and Trilogy share these declarations. Echoes Wii
exports `CMemory::Free(const void*)`.

The stock 7400 and 7450 RC1 timing tables omit the Gekko paired-single opcodes. Their
fallback serializes them and assigns them to an integer pipeline. The stock 750 model
gives paired-single and double-precision transfers the same memory timing. Applying that
correspondence in the 7400 model, while retaining its fallback serialization, corrects
the order of the integer argument setup and register save in `luaB_foreachi`. Lua uses
the existing 7400 schedule with this model. `LuaPlus.cpp` keeps the 750 schedule.

One compiler built all 32 unchanged Lua units and three LZO units in the normal RM3E01
Ninja build. Its DOL matches the shipped SHA-1 `3e81f9b09dfb1e7c02e22b9c6c48a4a1390224b2`.
All 76 image checks passed.

## Remaining work

- Continue x86 byte matching for assignment and conditional lowering, local reassignment,
  member instantiation, local declarations, and class-instance allocation. Recover the native IR
  statement and expression visitors. [IR recovery](#ir-reference-order-and-packing)
- Investigate remaining function-order differences with native controls and shared sources.
  Several depend on reconstructed declaration and template-registration order. [Source controls](#remaining-order-differences-and-source-controls)
- Move recovered units into the shared versioned compiler sources, then add the source-only
  build, WebAssembly, and oracle support.
- Complete the Echoes integration suite, including all 447 image hashes. [Echoes guide](wii-retro-echoes.md)

## Reproduce the original binary patch probes

Apply the three patchers in order. They write to `build/Wii_Retro_probe/`. The result works with `sjiswrap`
and needs no Clang installation at patch time.

```sh
uv run --no-sync python -I gc2/tools/wii_patch_struct_copy.py build/compilers/Wii/1.0RC1 build/Wii_Retro_probe
uv run --no-sync python -I gc2/tools/wii_patch_pretest.py build/Wii_Retro_probe build/Wii_Retro_probe
uv run --no-sync python -I gc2/tools/wii_patch_template_vtables.py build/Wii_Retro_probe build/Wii_Retro_probe
```

With the current embedded vtable pass, the in-place patch chain SHA-1 is
`c3a8d7766cb5245a78768bc93f311095426b132b`. The original vtable payload produced
`4f04833b22247059750b91ffa4eedb4c90cc29aa`. The struct-copy and pretest steps give
`fbb0c00006afad9e60a82a020caba0078d4d4a5b` and `89bb1b1e631f474b2cadf190ba2c88ce30ef2141`, respectively.
Clang 23.1.1 reproduces both embedded pass payloads and their COFF relocations using the flags in the patcher
docstrings.

The original Echoes probe used remapped R3ME01 splits and symbols at `d7fc9ef33`. The game-side
guide covers compiler replacement and fresh builds. [Echoes installation](wii-retro-echoes.md#install-the-compiler)

## What Echoes' research established

The target follows the Wii line's scheduler, `cmplwi` switch range checks, individual `stw`
register saves, and folded range checks (`subi; clrlwi; cmplwi`). RC1, 1.0a, and 0x4201_127
agree on the tested game code. GC 3.x uses `cmpwi` chains and `stmw` saves. None of 136
GC 3.0a5.2 runs reproduced Wii scheduling.

The target also follows GC 3.x's struct-copy rule. It copies an underaligned four-byte
aggregate such as `GXColor` byte by byte and uses a word for a known-aligned source or a
forwarded register value. RC1's `legacy_struct_alignment` pragma restores byte copies but
also reduces stack alignment, packs one-byte slots, and reloads some forwarded values.
The relink separates operand alignment from that stack layout. Corruption's July 2007
`main.dol` already shows the same rule and the same SDK version strings as Trilogy.
[Trilogy-compiler-struct-copies.md]

The target also folds the duplicate `rstl::destroy` pretest around a loop that calls a
function. The tested stock Wii and GC compilers retain it. [Trilogy-compiler-destroy-pretest.md]

## What mwcc-tools adds

- All 146 source filenames in GC 3.0a5.2's assertions appear in RC1's 173. Only 98 also
  appear in GC 2.7. GC 3.0a5.2 already has the newer frontend and IR. RC1 adds `SWCH_*`,
  the likely home of Wii switch lowering, along with `LocalCSE`, `OptimizerHelpers`, VLE,
  and plugin and build-agent layers.
- RC1's NB11 CodeView records provide names and unit boundaries through `gc2/tools/mw/wii.py`.
  The GC 3.0a5.2 layout was inferred by comparison. [Layouts and diff map](../gc2/research/README.md)
- Stock RC1's CodeView records name `Metrowerks CodeWarrior C/C++ x86 V3.2` at file offset
  `0x369399`. Its PE header records linker version 3.2. Both fields identify the 3.2 family.
  The exact RC1 host build remains unknown.

## RC1 host compiler trials (2026-11-18)

| Distribution | Executable banner | Compiler identifier in generated CodeView |
| --- | --- | --- |
| CodeWarrior Pro 6 | 2.4, build 0131; September 8, 2000 | `Metrowerks CodeWarrior C/C++ x86 V2.4` |
| CodeWarrior Pro 7 | 2.4.5, build 199; August 24, 2001 | `Metrowerks CodeWarrior C/C++ x86 V2.4` |

The repository downloader supplied Pro 6. Pro 7's `mwcc.exe` came from
`Other Metrowerks Tools/Command Line Tools/` in `LAYOUT/PRO_7_TO.ZIP;1`.
Both used the repository's `lmgr326b.dll` stub. [Pro 7 Windows Tools disc](https://archive.org/details/CodeWarrior_Pro7)

Executable SHA-256 values

- Pro 6: `41ce15fd2c716f8d0ee5c8e8bf14aa3ad8d3eb715eace8c61cfdec70d3ff418c`.
- Pro 7: `962fa567ca629a28f9952e9c5674e8f25f97352f4e5bec1b4aff2df2ee123e05`.

The byte probe covers 22 routines from template instantiation, IR streaming, and deferred
exports. It resolves relocations and checks referenced data. The 36 flag combinations cover
`-O2`, `-O3`, `-O4,p`, and `-O4,s`, three inlining modes (`off`, `auto`, `auto,deferred`),
and `-proc x86`, `586`, and `686`. Both the C90 and C-linkage C++ adapters use debug symbols
and disable C++ exceptions. Pro 6 and 7 match two routines under `-O4,p`, `CCallGraph_Unmark`
(27 bytes) and `CCallGraph_UnmarkAndUnflag` (31 bytes).

LLD relinks of the three C90 units pass template controls and match the complete PowerPC objects
and traces for `CAnimData`, `CPlayer`, `CSequenceHelper`, and `CCharacterFactory` under configured,
file-IPA, and program-IPA flags. The trials used `-O4,p -inline auto,deferred -proc x86 -sym off
-Cpp_exceptions off`. Trial SHA-1s are `82fa31b44b5e4a54d5f5474fa28b1f60166c3b74`
(Pro 6) and `1dc0be0d13b0730ff96aeba912733e218d522a0e` (Pro 7). The local scratch directory
`build/rc1-host-compiler/` holds the 3,168 comparison records.

### Pro 8 and Windows 9.x

The Windows 9.x host tools used for RC1 x86 matching came from the archived distribution's
`CW_Windows_9.2.exe` base installer and `CW_Windows_9.4_Update.exe`. The ZIP and 7z copies
contain identical installer payloads by SHA-256.
[Metrowerks CodeWarrior for Windows 9.4 on Internet Archive](https://archive.org/details/metrowerks-code-warrior-for-windows-9.4)

Pro 8 uses `LMGR326B.dll` ordinals 189–191, which the repository stub supplies. Both Windows
9.x hosts use those ordinals from `LMGR8C.dll`. The probes use the DLL from the extracted 9.2
`Tools/Bin` directory and point `LM_LICENSE_FILE` to `license.dat` from the ZIP.
[`lmgr_stub`](../gc2/tools/lmgr_stub/lmgr_stub.def) · [Runnable host check](../gc2/tools/wii_retro/tests/README.md#local-register-assignment)

| Distribution | x86 compiler | CodeView identifier | PE linker field | RC1 functions matched |
| --- | --- | --- | --- | ---: |
| Pro 8.0 | 3.0 build 314 | V3.0 | 3.0 | 5/22 |
| Windows 9.2 | 3.2.2 build 396 | V3.2 | 3.2 | 6/22 |
| Windows 9.4 update | 3.2.5 build 428 | V3.2 | 3.2 | 6/22 |

All three `mwcc.exe`/`mwld.exe` pairs emit NB11 and ran the same 22-routine probe.
Windows 9.2 and 9.4 reproduce RC1's 3.2 identifiers and emit identical code and code
relocations across 216 paired objects. The six matches across the flag sweep are `CTempl_Instantiate`,
`CCallGraph_NewInitCodeNode`, `CCallGraph_Unmark`, `CCallGraph_UnmarkAndUnflag`,
`CCallGraph_IsDefined`, and `CCallGraph_SetIR`. Pro 8 matches the first five. One 9.x C90
configuration matches up to five routines. The 349-byte `CTempl_Instantiate` matches with
`-proc x86` and either `-O3` or `-O4,p` in all three inlining modes.

All three trial relinks passed the template controls and reproduced the same 12 PowerPC
objects per host. The Windows 9.2 and 9.4 trial executables share SHA-1
`1aa3287f197cdfd8ee9b635fbaa7cf1cc1ae536c`. The 4,752 comparison records are in the local
`build/rc1-host-compiler/attached/` scratch directory.

## The acceptance suite

These targeted cases use R3ME01 source and Retro's C++ flags. Record the source revision and
target bytes when comparing them.

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

Preserve existing RC1 matches in R3ME01 and the 195 units shared with G2ME01 (1,162 of
1,212 functions). The broader controls cover the 71 R3ME01 functions with extra native byte
copies and the destroy-pretest cases. [Trilogy-compiler-destroy-pretest.md]

## Phase 1: names, units and a diff map

The relink reads RC1's CodeView layout through `config/Wii_1_0RC1/config.json`. The GC 3.0a5.2
map carries back RC1 symbols by relocation-masked remapping. Older GC builds provide a
secondary name source. The diff map groups changes by unit. [Layout generation and diff map](../gc2/research/README.md)

## Phase 2: find and probe the behaviours in RC1

The original probes separated copy lowering from stack allocation at the readers of
`legacy_struct_alignment`. Stock comparisons and nearby game functions supplied controls
for the loop guard, template emission, scheduling, and register-order changes that followed.

### Results (2026-11-14)

**Where the pragma acts.** `legacy_struct_alignment` is `copts+0xA1` (0x74F2F9). The pragma table's second field
is the option's offset. RC1 reads the field in 11 places:
- `CMach_AllocationAlignment`: the alignment of what is allocated, which sets the stack layout. Without the
  pragma, a small aggregate's alignment is raised to its size, up to 8. With it, the declared alignment is used.
- `CMach_GetObjectAlignment` uses declared alignment for nonlocals when the pragma is enabled and
  `CMach_AllocationAlignment` when it is disabled. For locals, it uses `CMach_ArgumentAlignment`
  for entries flagged as belonging to the parameter area and `CMach_AllocationAlignment` for the rest.
- The copy-width sites, three in `gen_COND`, two in `gen_ASS` and two in `Intrinsics`. Each one is
  `legacy ? CMach_GetTypeAlign(type, 0) : CMach_AllocationAlignment(type, 0)`.
- `CMach_SetBackEndOptions` and `CMach_GetBackEndOptions`, which only save and restore the field.

3.0a5.2 has no such field. The byte its `CMach_AllocationAlignment` tests (0x70F231, `copts+0x249`) is
`optimizesize`. Its other readers (the inliner, loop transformations, spill costs, `StructMoves`) are the ones
`optimizesize` has in 2.7's sources. With `optimizesize` disabled, 3.0a5.2 uses the same allocation
policy as RC1 with the pragma disabled. Its object-alignment helper at `0x4E5180` always uses
declared alignment for nonlocals.

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
- 3.0a5.2's `gen_ASS` computes each operand's alignment and takes the smaller value, as RC1 does.
  An operand with no object gets `CMach_GetTypeAlign(type, 0)`. An object gets
  `CMach_GetObjectAlignment`, which follows RC1's alignment rule with the pragma enabled.
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
loops in `WriteAnimData` (two functions) and `CScriptTrigger::AddInhabitant`. The affected loop in
`AddInhabitant` copies list nodes while constructing the new inhabitant.
- Whether the registers are the same follows from whether `begin` is used after the loop. In a vector destructor,
  `begin` is the vector's data pointer, which is freed after the loop. When the body makes no call, `begin` stays in
  `r3` for that free, and the loop counts in its own register (`r4`). When the body calls, the pointer is loaded
  again for the free. `begin` and `cur` then share one register, so the guard's compare is the loop test's.
- So the register condition alone does not decide it: the call does.

Measured by deleting the guard from our compiled objects after the patched compiler
(`gc2/tools/wii_drop_pretest.py`, on R3ME01 with the patched RC1 and the `CModelFlags` union):
- Same registers and the body calls (750 guards): 12,898 → 12,952 exact, 54 gained, 0 lost; 150 improved,
  3 regressed. Gained include `~CCameraColliderGroup`, `~CGuiFrame`, `~CModel`, `~CPakFile`, `~CMapWorld`, `~CWorld`
  and `CToken` vectors. Regressed: `AddInhabitant` (guard kept in the target), `~CGenericFSM2State` and a
  `reserve`, each by 1 point or less.
- Also when the body makes no call (770 guards): 55 gained, 3 lost, 18 regressed.

The deleted guard is 8 bytes, `cmplw` and `beq` (`~CCameraColliderGroup` matches exactly with it gone), not 12 as
the Echoes CBallCamera page says.

**First binary patch.** `gc2/tools/wii_patch_pretest.py` adds the rule to RC1 as a pass in a new section (`.retro`,
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

### Windows 9.x alignment recovery

CodeWarrior Windows 9.2 and 9.4 reproduce all 111 bytes of `CMach_ArgumentAlignment` in
stock RC1 (`0x4F4D50`) and GC 3.0a5.2 (`0x4E5400`), including resolved calls and data
references. The compile used `-O4,p -inline auto,deferred -proc x86`, with CodeWarrior's
`__inline` spelling and a function-pointer reference to force emission of the standalone body.
The helper takes stored alignment from struct and class types. For other types it temporarily
selects PowerPC alignment and clears the old-alignment option, then restores both options.

The same builds emit the full `CMach_GetObjectAlignment` body sizes of 261 bytes for RC1
and 223 bytes for GC. `WII_RETRO_STOCK_ALIGNMENT` selects the RC1 pragma branch for this
comparison. In each executable, only three bytes differ, all encoding the register used
for the parameter-area test. The native code uses `ebp` and the reconstruction uses `ecx`.
The GC symbol map now identifies the helper at `0x4E5180` and ends the preceding
`CMach_CalcIntDiadic` at that boundary.

The relink uses the recovered argument helper at its five direct call sites and applies
the GC operand rule in the object helper. A control relink retained the native object
and argument helpers and applied the same operand-width patch. The reconstructed build
matched the control's objects and diagnostics for four Echoes units and two aggregate
fixtures under configured, file IPA, program IPA and debug flags.

Both Windows 9.x compilers reproduce the full RC1 `__memclr` and `__memcpy` helper bodies
at `0x57F9D0` (247 bytes) and `0x57FAD0` (295 bytes). They also reproduce the 269-byte GC
copy helper at `0x56A510`, which always uses declared alignment for typed operands without
an object. Wii_Retro uses that rule in the recovered C routines, replacing two byte edits.
Constant-size copy and clear fixtures match the previous byte-patched helpers with the
pragma enabled and disabled under four flag configurations. The fixtures emit the expected
byte and word stores and retain the runtime call for variable-size `__memcpy`.

The allocator remains native. Trial source matches its 52-byte switch table and 406 of 414
instruction bytes in RC1. The eight differences encode two different registers in the
size-based alignment loop. The GC trial has three additional differences from the register
and sign extension used for its struct subtype test.

#### Assignment lowering

The GC 2.7 `InstrSelection.c` builds with both Windows 9.x compilers and supplies a source
baseline for these two functions. It emits 3,376 bytes for `gen_ASS` and 6,616 bytes for
`gen_COND`, compared with RC1's 3,973 and 7,802. RC1's `gen_ASS` adds register-class setup
and calls `CMach_PassResultInHiddenArg` before selecting small-register stores or a block
move. Its two aggregate operands each choose object alignment or the pragma-controlled
type alignment, then the copy uses the smaller result. The earlier source still selects
argument or allocation alignment inside those two operand paths.

The recovered C routine preserves the scalar, bitfield, register, floating-point and
aggregate branches. It also uses the PCode range recorded by `gen_POSTINCDEC` to move an
assignment ahead of the postincrement when the native operand check allows it. The relink
removes RC1's `gen_ASS` body and its three-entry jump table, then resolves the original
calls and dispatch-table reference to the recovered function. The two assignment width
edits are applied in C. The original binary probe still applies all eight edits.

With the original alignment branch enabled for comparison, Windows 9.2 and 9.4 emit
3,975 bytes with the same 78 resolved direct calls. The stock body is 3,973 bytes and
differs in register assignment and branch layout. The Clang relink matched the preceding
byte-patched compiler in all 3,072 regional object comparisons and all four PCH combinations.
It also matched 20 targeted compilations covering conditional lvalues, bitfields, aggregate
returns, postincrement, member pointers, 64-bit integers, paired singles and Altivec. The
processor checks covered Gekko, e500, e500v2 and 7400.

#### Conditional lowering

The aggregate branch of `gen_COND` creates one temporary and limits each arm's copy width
to the temporary's type alignment. RC1 computes that limit after lowering the first arm and
reuses it for the second. GC 3.0a5.2 has the same sequence with declared alignment. The relink
now uses recovered C for the whole conditional function, including the scalar and vector cases,
hardware `fsel` and `isel`, and integer constant and absolute-value optimizations. This replaces
the last three operand-width edits in the native build.

With the stock alignment branch enabled, Windows 9.2 and 9.4 emit the full 7,802-byte body,
with calls to the same functions at 138 direct call sites and the same case groupings in all
25 switch entries. The x86 instruction bytes still differ. The relink matched the preceding
compiler in all 3,072 regional object comparisons, all four PCH combinations, and 20 focused
compilations covering control flow and scalar, vector and aggregate results. The fixtures
include emitted `fsel` and `isel` instructions on Gekko and e500.

### Destroy-loop guard folding (2026-11-17)

Stock Wii 1.0RC1, Wii 1.0, Wii 1.0a, and GC 3.0a5.2 emit one test for a plain pointer loop in a
small fixture. Each retains a second test for an explicit outer guard. Tracing Wii 1.0RC1 with
the binary patch disabled leaves its objects byte-identical. In `~CCameraColliderGroup`,
the pre-coloring PCode compares virtual `begin` with `end` at the guard and virtual `cur` with
`end` at the loop test. The guard block contains a copy from `begin` to `cur`. Coloring makes
both comparisons use `cr0, r30, r31` and eliminates the copy. The three vector loops in
`~CBallCamera` have the same pattern. In `~CScriptTeamAiMgr`, the entry pointer remains live
for the call to free the buffer. Its call-free loop still compares different registers.

A prototype that updates the PCode control-flow graph and requires a straight-line body loses
36 exact R3ME01 matches with the configured flags and 56 with forced file IPA.
`ClearHierarchy`, `~CGuiFrameModelDatabase`, and `ClearTokenList` show why. The original loops
have conditional or nested destruction and advance the iterator by a constant stride. The
`AddInhabitant` loop instead loads the next node through the current node. The two
`WriteAnimData` controls retain their guards.

The native pass runs after coloring and pseudo-op expansion at the `CodeGen_Generator` call at
0x46B1A5, before the final scheduler and branch displacement calculation. It checks that the
entry jump reaches the matching loop test, the backedge reaches the body, and the test falls
through to the guard's exit. It requires a fixed positive stride in the cursor register and
a call in the loop. The pass removes the guard-to-exit edge with RC1's `deleteedge` and removes
the compare and branch with `deletepcode`. The unchanged entry jump takes the first iteration
through the loop test, including for empty ranges. The fixed-stride rule is inferred from the
shipped objects. [Source](../gc2/tools/wii_retro/destroy_pretest.c)

The integrated build compiled 898 R3ME01 objects with each flag set and 319 each for R32J01 and
R3MP01 with file IPA. No reported function score fell. `AddInhabitant` retains the shipped
list-copy guard in all three builds and improves from 86.79% to 87.36% in R3ME01 under file IPA.
`~CCameraColliderGroup` and the call-free `~CScriptTeamAiMgr` match in all three builds,
including direct comparisons against the regional DOLs. The three `~CBallCamera` loops have
the shipped branch structure in all three builds. R3ME01 still uses a different register for two preceding cleanup
calls and scores 99.62%. The configured R3ME01 build also retains a linked-list guard in
`~CStateManager`. The shipped destructor has different inlining, and its match score is unchanged.

In a synthetic fixture, a guarded range destructor compiles with one test. A call-free store
loop and guarded linked-list traversal retain both tests. Stepping the emitted loop yields the
same destructor calls for ranges of 0, 1, 3, and 17 elements. The fixture is byte-identical to
the baseline under `-O0` and with scheduling disabled.

The historical in-place and object patches reproduce the first experiment. The native relink
uses the control-flow pass.

Both Windows 9.x hosts reproduce all 409 instruction bytes and the three-entry jump table of
`pcode_expand_pseudo_ops`. The GC 3.0a5.2 build matches with its pseudo-op numbers and diagnostic
location. The C relink expands condition-register bit and field reads to `mfcr` and `rlwinm`,
and field writes to `rlwinm` and `mtcrf`, before running the guard-folding pass. The inferred
guard rule still runs after this native-matched step. The rebuilt compiler preserves the three
`~CBallCamera` loops, the collider loop, and the `AddInhabitant` linked-list guard in all three
regional DOL comparisons.

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

### Weak and deferred function order (2026-11-17)

RC1 separates deferred definition actions from deferred function exports. The routine at
`0x47D800` pops one action from `0x70FF46` first. It processes an export from `0x70FF42` only
when the action list is empty and RC1 is outside IPA and deferred codegen. The export
contains `{next, Object*}` and calls the export routine at `0x47DE60`. Three sites prepend
nodes to the export list at `0x47DC96`, `0x47E7B3`, and `0x47E9A3`. The first is in
`CMid_ObjectAddrRef` and the other two are paths through `CMid_DefineFunction`.
The PPC generator runs at `0x46A420`. Tracing those queues at the declaration and end-of-file
boundaries and tracing the generator leaves the objects byte-identical.

#### Recovered native control flow

The standard regional object and PCH comparisons are summarized under [validation](#native-relink-and-validation).
The results below give the targeted native controls for individual recoveries.

##### Object-address references

The relink implements `CMid_ObjectAddrRef`
at `0x47D970` in C. The worker marks the object referenced, forwards aliases,
and exports inline data constants. For a function with an available packed
inline body, it sets the object's definition bit and either appends a
call-graph node in IPA or prepends an export outside IPA. Precompiled-header
generation reports diagnostic `0x2908` for such a body. In the graph path the
worker walks the packed references in order. The count is at `+0x14`, the entries
start at `+0x1A`, and kind one entries contain object pointers. Marking the
function before following those pointers prevents duplicate graph insertion
through a recursive inline reference.

When a body is unavailable, the worker records the point of instantiation
for a class-template instance member if needed. It queues a kind-three action
for an undefined compiler-generated special member and sets the object's
definition bit. A remaining function with a template-instance record queues
kind two. These paths read the function type's definition bit separately from
the object flag used by call-graph and export insertion.

The three existing reference hooks still collect ordering information around
the worker. The relink redirects the other direct calls. Traces match the
native worker's object transitions, deferred actions, exports, and appended
graph nodes under configured flags, file IPA, and program IPA. The probes
include mutual inline references, template function addresses, and syntax
errors. A synthetic alias verifies that both the alias and its target get
marked referenced.

##### Function definitions

The relink implements `CMid_DefineFunction` at
`0x47E4E0` for the assembler, both synthesized function entry points, and
`CFunc_Gen`. It restores arguments and locals from the definition record
at `+0xA` and `+0xE`, rewrites the expression tree, and saves both lists
back to the record. Inlineable definitions are packed before the reference
scan. An unreferenced, explicitly inline definition can keep the packed
body without marking the object ready for emission. Other definitions
are packed only when pending actions, IPA, or the deferred-codegen option
prevent immediate generation.

Outside IPA, a deferred definition prepends an export. Inside IPA it appends
its graph node before walking the packed references. The compatibility hook
for serialized references stays on the late-packing path at `0x47E982`.
The early-packing path at `0x47E792` uses the ordinary reference worker.
Immediate definitions expand inline calls before checking whether an object
instance is needed and whether code generation can proceed.

Native and C traces agreed on 16,852 definitions, including early and late
packing, export and graph insertion, inline assembly, deferred and automatic
inlining, and failed compilations.

##### Definition references

The relink implements the reference scan at
`0x47ED00` for function definitions and file-scope initializer expressions.
It visits each statement's cleanup chain before the statement itself. Its
expression visitor and inline assembly relocation scan prepend references
to data and function objects, keeping each object once per scan. Walking the
list in reverse discovery order and prepending new actions preserves their
discovery order within that scan.

The scan returns true for pending function work, including an existing action
for the same object. It queues streamed definitions, class-template members,
function-template instances, and implicit special members. The class-template
action stores the instance at `+0xC`, the member record at `+0x10`, and the
original declaration when present at `+0x14`. The point of instantiation
occupies `+0x8`. Data references reach the existing address-reference hook
during IPA.

Native and C traces matched on 16,872 statement scans, 33 expression scans,
and 19,004 collected references. The probes include file-scope initialization,
assembly relocations, cleanups, and diagnostics. Synthetic member records
also verify the streamed payload, both class-template scope paths, and
duplicate action suppression. Both compilers consume precompiled headers from either producer
and emit identical objects containing the template and cleanup code.

##### Initializer emission

The C routine replaces `0x47D400` and its guarded
template-data helper at `0x47D760`. It scans each file-scope expression for
deferred work and drains the actions before constructing the initialization
function. The loop uses `CMid_FreeLocalHeap` for the cleanup shared with
declaration processing, preserving token streams for pending streamed
definitions.

The routine records the initializer source locations and groups consecutive
expressions for the same template static object under one guard. Outside IPA
it expands and generates the initialization function. In IPA it packs the
function, creates an initialization graph node, and follows the packed object
references in order.

Native and C traces match across 20 calls, 143 expressions, 15 guarded groups,
six initialization graph nodes, and five direct initialization functions.
Empty units and diagnostic controls also match. Precompiled headers from
both compilers produce identical objects containing guarded
template initialization when consumed by either compiler.

##### Deferred export and generation

The C export routine replaces
`0x47DE60`. It saves the caller's backend options, restores the options
packed with the body at `+0xC`, establishes the function scope, and
unpacks the statements. It initializes the native `cgparams` record and
expands inline calls before checking whether an object instance is needed.
It restores scope, debug settings, and backend options after generation.
Explicit and automatic inline bodies stay available. Other packed bodies
are released through `CMid_FreeFuncData`.

Native and C phase traces match for 345 exports, including debug symbols,
`nosyminline`, deferred and automatic inlining, and 77 packed-body releases.
A separate deferred static-function control also matches.

The generation worker at `0x47E9D0` is also implemented in C. It restores
and updates the argument and local lists around the native expression
visitor. The late statement pass handles GCC-style assembly and returns
early after diagnostics. The worker then transforms C99 complex expressions,
visits each statement's exception cleanups through the compatibility hook,
sets the source file for debug output when required, and calls the PPC
backend.

Native and C phase traces match across 868 backend calls and 28,677 cleanup
visits. GCC assembly controls cover generation and the diagnostic return
before backend entry.
Both compilers consume either producer's precompiled headers and emit
identical template and cleanup objects. The native expression visitors,
GCC assembly expansion, IR serialization, and PPC backend are reused.

The IPA function emitter at `0x47D230` is implemented in C. Initialization
nodes bypass the ordinary object-instance query. The emitter restores each
body's backend options and function scope, gets the graph IR, and generates
the function when diagnostics allow it. It restores the caller's scope and
options afterward. Inline qualification and the automatic-inline flag control
retention of the packed body. The release check also runs when generation
is skipped.

Native and C phase traces match across 749 emitted functions and 296
packed-body releases, including initialization graph nodes and debug output.
Guarded template initialization matches across all four PCH pairs with file IPA.

##### Graph-node dispatch

The C `CMid_GenerateCGNode` replacement at
`0x47CFF0` marks each object referenced and defined before dispatching
function, virtual-thunk, or data emission. The data path follows relocation
references before declaring writable or read-only data and restores the
object's qualifiers afterward. With debug information enabled, data emission
selects the saved source location.

Native and C traces match across 958 nodes, including four thunk emissions,
145 writable data emissions, 44 read-only data emissions, and 88 relocation
references. A callback control that changes qualifiers verifies their restoration.

##### Packed-body storage

The relink implements `CMid_GetFuncData`,
`CMid_SetFuncData`, and `CMid_FreeFuncData` at `0x47F890`, `0x47F730`,
and `0x47F660`. Setting a body retains an existing `0xDE`-byte header,
releases its previous stream when the stream pointer changes, and copies
the complete record. Freeing a body releases the stream and header and
clears the object's body pointer. A repeated free is safe. The getter
returns no body for an inline builtin or intrinsic. Stream allocation and
serialization retain their native implementations. The port calls
`CIRStream_Free` to honor the stream's ownership flag.

The relink redirects 20 native getter calls, five setter calls, and two
free calls, as well as calls from the recovered C paths. Native and C traces
match for 99,358 getter calls, 27,924 stores, and 392 normal releases. Synthetic controls verify
self-assignment, same-stream updates, replacement with another borrowed
stream, header reuse, and repeated release.

##### Graph registration

The C replacements for `CCallGraph_NewNode` at
`0x5C0D00` and `CCallGraph_NewInitCodeNode` at `0x5C0CB0` preserve append
order, the object's node backlink, and the increasing node index. Nodes
are `0x20` bytes. Data and absolute objects use kind three; ordinary and
virtual functions use kind one, or kind two for virtual dispatch thunks
with qualifier `0x800000`. Inline builtins also use kind one. Initialization
nodes receive a unique `<init-code>` function in the root namespace, a
stored body, and the object root flag before registration.

All 12 native node-registration calls and the initialization helper call
reach the replacements. Native and C traces agree across 7,605 registrations
and seven initialization nodes. Synthetic controls cover all supported
object categories, list endpoints, backlinks, and index allocation. All
3,072 regional objects match the preceding compiler byte for byte.

##### Graph pruning

The relink implements `CCallGraph_RemoveNode` at
`0x5C0B50`, `CCallGraph_RemoveDeadNodes` at `0x5C0600`, the private root
predicate at `0x5C06A0`, and `CCallGraph_Renumber` at `0x5C0EC0`. Root
selection first accepts a null object or object flag `0x8`. For an
implicit-weak object, the byte at `0x74F481` and `CIMan_NeedsObjectInstance`
control selection. Other inline functions, static objects, and objects
inside an unnamed namespace are not roots. Initialization nodes are roots
independently of that predicate. The weak case precedes the linkage and
namespace checks; the flag's command-line meaning remains unverified.

Pruning marks reachable nodes through the recovered `CCallGraph_ColorUses`,
removes unmarked nodes, and renumbers survivors in list order only when
something was removed. Removal unlinks both directions of every edge,
clears the object's backlink, and preserves the next-index counter.
Edges are `0xE` bytes. Native internal checks at lines 450 and 464 require
each reciprocal edge to exist.

Native and C traces match across 3,077 root checks, 2,311 removals, and
9,077 survivor records, including synthetic fixtures. Controls exercise
reachable cycles, an unreachable self-loop, initialization roots, empty
graphs, and all four weak-root flag/instance combinations. Both compilers
consume either producer's precompiled header with
identical guarded-template output.

##### Graph construction

The C `CCallGraph_Compute` replacement at
`0x5C0720` renumbers the existing list, frees each node's old edge lists,
and rebuilds its outgoing references. Objects discovered through those
references append to the same list and are visited later in that pass.
Incoming lists are built after all outgoing lists are complete.

Function-body references retain their packed 16-bit counts and flag bit
zero, with separate edges for repeated entries. A virtual dispatch thunk
has one count-one edge to its target. Data relocations have count zero
and flag one; repeated references to the same node share one edge.
Outgoing and incoming lists both prepend. The native object-category
tables accept data, absolute objects, ordinary and virtual functions,
and inline builtins; other categories are ignored.

Native and C traces match across 25,382 outgoing and 25,382 incoming
edge records. Synthetic controls verify discovery order, repeated
rebuilds with changed references, self-references, data deduplication,
ignored object categories, and the maximum packed reference count.

##### Graph reachability and IR access

The relink implements
`CCallGraph_ColorUses` at `0x5C0DC0`, `CCallGraph_Unmark` at `0x5C0F00`,
`CCallGraph_UnmarkAndUnflag` at `0x5C0EE0`, and `CCallGraph_IsDefined` at
`0x5C0F20`. Reachability follows outgoing edges in list order, skips
already marked targets, and descends six nodes before recursing. The C
worker retains that recursion boundary. A premarked entry node still
examines its immediate edges. Definition queries test the stored body
for ordinary functions, always accept thunks, and test initialization
data for data nodes.

The C `CCallGraph_GetIR` replacement at `0x5C0AB0` unpacks the body with
mode zero and copies all `0x36` bytes of the function record. It replaces
the record's object pointer for ordinary nodes; initialization nodes
retain the unpacked object. The `CCallGraph_SetIR` replacement at
`0x5C0A60` frees the old stream before packing and installing its
replacement. The expression and statement visitors remain native; the
outer stream packer and object references are recovered below.

Native and C traces match across 1,026 reachability calls, 11,086
definition queries, 3,411 IR reads, and 1,515 IR writes, including
synthetic controls. The comparisons include 390,067 packed-reference
records. Controls cover long dependency chains, cycles, shared targets,
premarked roots and children, flag preservation, empty lists, intrinsic
and builtin functions, and all bytes of both ordinary and initialization
function records. The native recursive self-call retains a relative
branch after splitting. The trace harness redirects it to observe the same
recursion boundaries.

##### IR reference order and packing

`ir_stream.c` implements the object
reference writer at `0x52ED70`, reader at `0x52EC90`, and
`CIRStream_PackCode` at `0x52ABA0`. A nonlocal object receives its reference
index on first encounter. Temporary patch records prepend to the list at
`0x751D60`, but the finished table is written by each record's index, so
the table preserves encounter order. Reusing an object keeps its index.
The writer increments its 16-bit call count up to `0xFFFF` and accumulates
address flag one. It distinguishes entries by both object pointer and
reference kind. Locals use a separate list and index space, and null
references have their own tag.

The reader restores either a table reference or a local. Its native
internal checks are retained, including the inclusive global-index bound
at `0x52ECD2`: index equal to the reference count passes the comparison.
Synthetic storage with an extra entry verifies that behavior separately
from ordinary valid inputs. Local indices use a strict bound and require
a nonnull object.

The outer packer writes three source locations, arguments, locals, the
two function flags, and the statement stream. It allocates the stream
and reference table, records local, label, and type counts, and marks the
stream owned. The unused byte at header offset `0x19` is left untouched.
The relink redirects native calls to these C workers; graph IR updates
also use the recovered packer. The native type, expression, statement,
and assembly visitors are still reused.

**Source matches with Windows 9.x.** The source now writes the three locations explicitly,
passes signed flag bytes, and dispatches the reader directly on the signed byte at the
postincremented cursor. The writer allocates on exhaustion inside the global lookup loop,
checks the local lookup invariant before testing the object, and shares a return across
the switch arms. Windows 9.2 build 396 and 9.4 build 428 both reproduce the three RC1
functions with `-O4,p -inline off -proc x86` from a C90 adaptation of the current source.
The 768 code bytes match after resolving named operands, and all nine entries in the
two switch tables resolve to their original targets. Recompiling with the built-in type
base set to `0x38` also matches GC 3.0a5.2 for all three functions.
The comparison is available locally through
`build/rc1-host-compiler/attached/match_native.py verify`.
A relink using 9.4 for the recovered IR, template, and deferred export units also passes
the function-template traces and reproduces all 12 control objects from Echoes.

Native and C traces match across 286,400 object-reference writes,
392,640 reads, 29,538 packed bodies, and 510,021 reference-table records,
including synthetic controls. The controls cover all supported object
categories, same-pointer entries of another kind, repeated references,
count saturation, address flags, duplicate local entries, internal-error
paths, and complete packed headers and table contents. Serialized-byte
hashes also match after normalizing the four-byte host pointers explicitly
identified by inline-assembly relocation records. The traces include 316
assembly blobs and 464 such relocations. The Ninja generator includes `ir_stream.c` as an
input, and all seven build-configuration tests pass.

A separate native comparison checks GC 3.0a5.2 against RC1. The object
reference writer and reader, statement serializer, and expression
serializer have identical instruction bytes after rebasing addresses.
All 139 direct calls across those workers and the outer packer reach
corresponding functions; all 116 jump-table entries retain their relative
targets. The 22 referenced global-address pairs are consistent. The outer
packer differs only in subtracting built-in type base `0x38` in GC versus
`0x3F` in RC1. This rules out a different encounter-order rule inside
these common visitors alone. Type serialization, local construction, and
the IR supplied to the visitors remain separate questions.

A subsequent audit checks whether type packing adds object references.
The native type writers call type, namespace, string, and hashing helpers;
their direct call graph contains no object-reference writer. Instrumented
configured, file-IPA, program-IPA, debug, and diagnostic probes record
736,170 type-packing calls and 286,060 object-reference writes across 19
compilations. No object-reference write occurs inside type packing. The
probes cover 11 type kinds, including class, enum, array, function, pointer,
and member-pointer cases, with byte-identical output to the installed
compiler. Type packing therefore does not change function-reference
encounter order in these probes. The construction of the input IR remains
a separate question.

##### Deferred action dispatcher

The relink implements the `0x47D800` worker
in `deferred_exports.c`. The jump table at `0x670EA8` selects five action kinds
in numeric order. They generate a streamed function, instantiate a class-template
member, instantiate a function template, generate an implicit special member,
or parse a template friend. Each entry is `0x26` bytes with its discriminator at
`+0x24`. Kind zero checks the object's definition flag. Kinds one and two check
the function type's definition flag, and kind two also checks the specialization
flag at `+0xD` in the instance. The worker pops an action before calling its
handler so the handler can enqueue new entries. Export entries are `0x8` bytes.
The worker gates both queues on the compiler's diagnostic state and uses the
original `cmid_in_ipa_mode` flag when deciding whether it can emit an export.

The four original call sites now reach the C worker. Declaration processing
retains the cleanup and template-vtable checks around each work item, and the
function-template arm calls the existing sibling-order hook. All 3,072 configured
and forced-file-IPA objects are byte-identical to the previous build. Targeted
program-IPA and diagnostic probes also reproduce the previous results. A scratch
friend-template probe enables the native deferred-friend flag at `0x74F46F` and
exercises all five action kinds with both the original dispatcher and the C
worker. The action sequence and output bytes match with configured flags, file
IPA, and program IPA.

##### Declaration and end-of-file loops

`CMid_GlobalDeclarationActions`
at `0x47C120` first attempts to release the local heap. It drains the
`0x70FF3A` list of pending dummy functions next. Each `0x10`-byte entry holds
a function and either a constructor or a single expression. The compiler pops
the entry before generating the body and frees it afterward. The declaration
loop then drains deferred actions and exports. A dummy function queued during
this later phase waits for the next declaration or end-of-file round.

The relink uses `CMid_FreeLocalHeap` at `0x47C360` for the cleanup repeated
in the original loops. With no frontend errors, the helper checks for pending
streamed definitions before trimming temporary entries from non-global class
namespaces, discarding VLA metadata, and releasing the local heap.

The end-of-file loop at `0x47BF90` runs declaration actions, class
finalization, and template instantiation in that order. It repeats while a
phase reports progress and the compiler's diagnostic state allows another
round. The flag at `cparamblkptr+0x258` skips class finalization while generating
a precompiled header. All three calls from `CMid_Finish` now reach the C loop.

Traces match the original sequence of deferred actions, exports, heap cleanup,
class finalization, and template passes under configured flags, file IPA, and
program IPA. `CSequenceHelper` exercises four dummy constructors. A static
array fixture exercises the single-expression `__arraydtor` path. A syntax-error
variant returns the same diagnostic and exit status. The precompiled-header
probe skips class finalization and retains the template pass. Raw header files
vary across repeated builds with the previous compiler. Consuming headers
generated by either build yields byte-identical objects in all three modes.
The rebuilt compiler reproduces all 3,072 regional objects byte for byte
with configured and forced file-IPA flags. Compiling the standalone vtable
patch reproduces its embedded code and relocation table.

In configured R3ME01, the export list drains the two `CSpatialPrimitive` reserves in reverse
reference order. In `CAnimTreeAnimReaderContainer` it drains the two `VClone` dependencies
in reverse order, and later drains the three remaining weak virtuals in reverse order.
`CCharacterFactoryBuilder` similarly emits `ReleaseData` before the two factory virtuals.
File IPA reaches the shipped order in these cases through a different emission path.

An oldest-first export probe at `0x47D800` reproduces the complete common function order in
those three units with the configured flags. It reduces the units with relative order
differences from 177 to 121 among 886 readable R3ME01 object pairs. It improves 115 units by
pairwise function order and worsens 18. Of those, 63 become completely ordered and seven
lose their previously correct order. The full 898-object build leaves every function score
unchanged. `CVectorElement`, `CRealElement`, and `CFilteredObjectList` have the largest
order regressions.

Some mismatches involve the point at which RC1 can emit a function. `CBlockInstruction`
compiles `PageInvoke` before it queues the inherited virtuals at end of file. Oldest-first
puts the inherited virtuals in the shipped relative order, after `PageInvoke`. Declaring
`PageInvoke` inline in a scratch source and using file IPA reproduces the shipped order
with the existing compiler. The weak reserve for the explicit `CStaticGeometryMap`
instantiation is the only export at its drain point, so queue reversal has no effect there.

Tracing the enqueue sites ties both `CSpatialPrimitive` reserves to the definition path.
`VClone`'s two dependencies arrive from different reference callers. The three weak virtuals
at the end of `CAnimTreeAnimReaderContainer` arrive through the same `CMid_DefineData` caller
as the reversed destructor group in `CFilteredObjectList` and the two weak functions in
`CLineInstruction`. The enqueue caller alone cannot select the order.

##### Recovered class-member generation

`template_instantiation.c` implements
`CTempl_GenClassInstance` at `0x5C1140` and `CTempl_GenMemberInstance` at
`0x5C1340`, following the common design in `CTemplateNew.c` with RC1's
native fields and checks. The class worker skips extern instances during
implicit instantiation and visits the original namespace iterator in place.
It considers referenced or explicitly requested functions that are neither
defined nor compiler-generated. Data members also require the native data
predicate and must not be inline data or already defined.

The member worker requires qualifier `0x400000`, a declaration pointer at
`+0x60`, and a clear suppression byte at `+0x68`. The declaration check
retains internal source line 1217. A found template member inherits the
declaration's inline qualifier and is instantiated with a null override
for the point of instantiation. A missing declaration body warns with `0x27F9`
only for explicit instantiation. The class worker reports progress only
when generation sets the function type's definition flag or the data
object's definition flag; attempting generation alone is insufficient.
It reloads the type after generation, which may replace that pointer.

Native and C traces agree across 215,071 class-generation calls,
1,696,636 iterator visits, 1,117 declaration lookups, and 880 member
instantiations, including synthetic controls. The controls cover extern
instances, explicit instantiation, aliases, generated and defined
functions, inline and unreferenced data, missing declarations, inline
inheritance, changed type pointers, unsuccessful generation, and an
iterator that gains another member during generation. All 3,072 regional
objects remain byte-identical under configured and forced file-IPA flags.
All four PCH producer and reader combinations agree, and the seven build
configuration checks pass. Native calls and the early-vtable pass use the
C worker. The compatibility reversal around the two implicit class
instantiation calls is retained.

An additional source control explicitly instantiates one function member
and one static data member. Both original parser call sites for
`CTempl_GenMemberInstance`, at `0x5118FB` and `0x511A80`, reach the C
worker. Native and C traces and output agree under configured, file-IPA,
program-IPA, and debug flags, as well as with a later syntax error.

The GC 3.0a5.2 layout originally split this visitor at `0x5A1A50`, a cold
block reached by internal branches. The function actually spans
`0x5A19A0` through `0x5A1B2E`, before the next entry at `0x5A1B30`.
`functions.json` and `symbols.txt` now give its full `0x18F`-byte size and
remove the false function. A native instruction check covers the entire
range and finds no calls to the false entry. The complete GC visitor
uses the same member iteration design; RC1's member helper additionally
checks the template qualifier and propagates inline qualification at this
site.

Windows CodeWarrior 9.2 and 9.4 reproduce both complete native routines with
`-O4,p -inline auto,deferred`. The function type stores flags at `0x16` in GC
and `0x1A` in RC1. RC1 also adds a template-qualifier check, declaration
assertion, and inline propagation. Both class generators inline the member
generator in their function and data paths.

| Function | Wii 1.0RC1 exact bytes | GC 3.0a5.2 exact bytes |
| --- | ---: | ---: |
| `CTempl_GenClassInstance` | 511 | 399 |
| `CTempl_GenMemberInstance` | 145 | 95 |

The revised C reproduces the preceding Clang/LLD executable byte for byte.
The refreshed trace agrees with stock RC1 across 215,113 class-generation
calls and 1,696,804 member visits, including the explicit-instantiation and
parser-error controls.

##### Recovered template-instantiation loop

The relink implements
`CTempl_Instantiate` at `0x5C0FE0` as
`retro_InstantiateTemplateDefinitions` in `template_instantiation.c`.
It walks each class template's ordinary instances and then its partial
specializations, accepting class-instance flags with no specialization
flag. It then reads the function-template list and visits instances that
are neither instantiated nor specialized. Referenced, undefined primary
functions trigger generation, followed by the secondary function at
object offset `0x50` when that function is also referenced and undefined.
The instance is marked instantiated before each generation attempt.

The loop retains the original primary object across generation and reads
its secondary pointer afterward. A generation callback may change the
instance's object pointer or specialization flags; neither causes a new
entry check before the secondary test. List links are read after the
callbacks, so newly appended instances and partial specializations can
join the current pass. The function-template head is read after class
generation, allowing newly discovered function templates into that phase.

Each class-generation call and function-generation attempt is followed by
`CMid_FreeLocalHeap` at `0x47C360`, even when generation reports no
progress. That routine conditionally releases temporary compiler storage.
Its placement is retained. The early-vtable pass's alias is now named
`RC1_FreeLocalHeap` to reflect the native routine. The existing class
export-order wrappers remain around this recovered control flow.

Native and C traces match across 70 loop invocations, 48,929 class calls,
316 function calls, and 49,245 local-heap calls, including synthetic
controls. Controls exercise ordinary and partial instances, filtered
entries, extern flags, changing list heads and links, secondary functions,
replacement object pointers, callback failures, and normalized Boolean
results. All 3,072 regional objects are byte-identical to the preceding
compiler under configured and forced file-IPA flags. The four PCH
producer and reader pairs and seven build configuration checks pass.

A native GC 3.0a5.2 comparison covers the complete `0x15D`-byte loop at
`0x5A1840`. After rebasing its two global list heads and checking all
eight corresponding calls, it differs only at offsets `0xE0` and
`0x11A`: the function-type flag field is at `+0x16` in GC and `+0x1A`
in RC1. The class/partial/function traversal order is unchanged.
The function-instance generator and class-member body worker are recovered below.

##### Recovered function-instance generation

`retro_GenerateFunctionInstance`
replaces `CTempl_GenFuncInstance` at `0x5C17D0`. Implicit extern instances are
skipped. The generator follows fallback templates until it finds a stored
definition, a token stream, or the end of the fallback chain. When CIMan
declines an implicit instance, the current object receives `ImplicitWeak`
and the instance is marked instantiated without parsing a body. Otherwise,
nonstatic instances receive `ImplicitWeak`; explicit instantiation also
sets the graph-root flag. Those flags are set before missing-definition
diagnostics or stored-definition dispatch.

A packed definition receives a zeroed `0x26`-byte context containing the
instance. The stored-definition helper's byte result is forwarded unchanged. A token
stream marks the instance instantiated, establishes source and debug state,
then parses the body in a template-argument namespace. The generator saves
debug state after obtaining declaration source information. It restores the
namespace parent and instantiation point, removes the stream, restores
debug state, and calls the browser even when parsing reported diagnostics.
The parser accepts `{`, `:`, and token `0x15E` at entry; other tokens retain
internal source line 1002. RC1's instance-stack helper takes two arguments
and returns the token needed by its pop helper.

Object pointers and namespace parents are read again after callbacks at the
same points as native RC1. Synthetic callbacks replace those values, change
source information, and clear an instantiation point to check the reads.
The controls also cover implicit and explicit externs, static storage,
fallback stops, missing definitions, all entry tokens, and debug suppression.
Across 19 real and diagnostic compilations, native and C traces agree on
1,774 generator calls and 1,938 body-parser/browser calls, including 304
synthetic streamed-body cases. The 3,072-object regional sweep and four
cross-compiler PCH combinations remain byte-identical to the preceding build.
The standalone check and its synthetic controls are committed beside the
[patch sources](../gc2/tools/wii_retro/tests/README.md).

The native GC 3.0a5.2 generator at `0x5A1F80` has the same `0x2CB`-byte
control flow after rebasing global addresses and checking all 21 calls.
Its declaration state is `0x98` bytes rather than RC1's `0xA4`, accounting
for the frame and trailing source-field offsets. The other differences are
the try token (`0x15D` versus `0x15E`) and internal source line (998 versus
1002). The weak-flag, fallback, parsing and cleanup sequence is unchanged.

##### Recovered member-body instantiation

The relink builds `CTempl_InstantiateMember` at `0x5C13E0` from C. It applies the
implicit and explicit instantiation rules before selecting a stored function
definition, a stored initializer, or a token stream. Stored functions and initializers
use a zeroed 0x26-byte context whose first pointer addresses the class-instance record
at offset 0x34. The initializer path balances the error source and class scope
while instantiating the copy and submits even an empty copy to global initialization.

The token-stream path looks up the master template's parameters when the member has
none. It installs the member's source references before inserting the stream. It merges
function parameter names from the declaration, then parses the body or initializes a
data member. It restores the stream, scope, instantiation point, and debug options in
the native order.

RC1 and GC 3.0a5.2 both have a 1,007-byte worker with 63 corresponding call or absolute
reference operands and the same five jump-table entries after rebasing. The remaining
native byte differences come from the 12-byte declaration-state size difference and
internal source line 1186 in RC1 versus 1182 in GC.

The relink uses distinct packed contexts and scope saves, with one shared
instantiation-point save. Each path reads the old point only when installing a
new one. Explicit branches to the two packed paths reproduce the native call
offsets. Inlining the debug suppression check preserves the native
source-pointer test and aligns the five switch destinations.

Windows 9.2 and 9.4 compile this C body with `-O4,p -inline auto,deferred` to
match 1,003 of 1,007 byte positions and all five switch destinations in both
stock versions. The four differences encode two stack slots. Stock uses
`esp+0x24` for the stored initializer's saved scope and `esp+0x20` for the
token stream's saved source-location flag. The recovered body uses `esp+0x20`
and `esp+0x30` respectively.

The stock RC1 differential matched 8,192 synthetic calls, including the callback
sequence, source and scope state, flags, and argument values. The standalone
test also compiles class-template constructors, virtual members, and a static initializer.
[Member-body source](../gc2/tools/wii_retro/template_instantiation.c)

##### Recovered stored-definition instantiation

The relink builds the shared worker at `0x5C1AA0` from C. The worker marks the
instance instantiated, unpacks the body, and creates the hidden constructor
argument when a class instance acquires virtual bases through template
substitution. It fixes argument typedefs, instantiates dependent types, and
inserts an indirect result argument when the unpacked body lacks one. It
processes statements in list order and skips final code generation after a
fatal diagnostic. It then restores the current namespace, instantiation point,
and warning options.

Windows CodeWarrior 9.2 and 9.4 reproduce all 925 bytes of the RC1 worker and
all 884 bytes of the GC 3.0a5.2 worker at `0x5A2250` after resolving address
operands. Both use `-O4,p -inline auto,deferred`. The GC probe omits the
per-argument typedef fixup and uses the earlier function type layout, four
earlier diagnostic line numbers, and one warning option. RC1 models the
warning and its as-error option as adjacent byte fields so CodeWarrior saves
both before clearing either. Both workers read the current class flags only
after checking the constructor and current-class state.

The stock RC1 differential matched 65,536 cases, including hidden-argument
placement, argument replacement during callbacks, diagnostics, and scope
cleanup. A real fixture compiled with `parse_func_templ` and `parse_mfunc_templ`
exercised the stored worker and produced identical objects before and after the
recovery, with and without PCH. The standalone template check requires a real
packed function instantiation.

##### Recovered stored-statement instantiation

The relink builds the worker at `0x5C1E40` from C. It handles ordinary expressions,
dependent declarations, constructor initializers, conditions, switch labels, and
returns. For a void-valued return expression it emits the expression as its own
statement, instantiates that statement's cleanups, and inserts a bare return.
Source wrappers change the diagnostic location during substitution. A switch can
change the location several times and restores the original one when it finishes.
Both stock versions instantiate the low endpoint of a case range twice before
checking the high endpoint.

Windows 9.2 and 9.4 compile the recovered C body with `-O3` or `-O4,p` and
`-inline auto,deferred`. Both match all 1,154 bytes and 16 switch destinations
in RC1 and GC 3.0a5.2 (`0x5A25D0`) after resolving 45 call and data operands.
The expression arm keeps separate locals for the original and unwrapped expression.
The return arm keeps the instantiated value available while checking whether the
original expression needs conversion. In the switch loop, the integer check encloses the
range and single-label paths. Each later source lock has its own save slot.
The local `build/rc1-host-compiler/attached/recover_stored_statement.py` probe
extracts the relink's C body for the x86 comparison.

The relink builds `CError_LockErrorSourceRef` and `CError_UnlockErrorSourceRef`
from C. Lock saves the current diagnostic location before examining the
incoming reference, which can alias the save destination. It installs a new
location if both the reference and its file are present. Unlock restores all
three words. Windows 9.2 and 9.4 reproduce the 73-byte lock and 28-byte
unlock in both stock compilers after resolving `cerror_lockref` operands.
The direct check matched 8,192 stock RC1 cases, including null sources,
nested locks, and aliased save destinations.

The stock RC1 differential matched 65,536 cases covering callback order,
diagnostic locations, switch endpoints, and statement mutations. A real
dependent-switch fixture matched the preceding relink under configured, file
IPA, program IPA and debug flags. The separate case-range fixture reproduced
the stock illegal-constant-expression diagnostic. Repeated program-IPA runs
of the preceding relink produced identical PowerPC objects and different raw
`.irobj` bytes.

##### Recovered packed declarations

The relink builds the packed static and local declaration workers at `0x5C2380`
and `0x5C24C0` from C. The static worker creates an object in the root namespace,
resolves its type, records the substitution, and emits the initializer. The local
worker matches the unpacked object by index. It records the substitution before
resolving the type and initializer. It captures a dependent array bound during
type substitution and uses the VLA setup and cleanup path when needed. In the
ordinary initializer path, a data object is copied into global storage and
removed from the locals list.

Windows 9.2 and 9.4 reproduce the full 305-byte static worker in RC1 and GC
3.0a5.2 after resolving 14 operands. The relink's static worker unwraps the
source expression in its own body and shares the declaration check with the
local worker. Windows 9.2 and 9.4 also reproduce all 620 bytes of GC's local worker.
That probe keeps the original object pointer for the remap and a local-record
view for the index, then removes the emitted object through a pointer to the
matching list link. The stock workers share 207 instructions and the same
branch targets after accounting for relocations, padding, and RC1's six
instructions for typedef fixup. The closest RC1 C90 trial matches 632 of 636
bytes at their native positions, including all 28 branch encodings, on both
Windows compilers at `-O3` and `-O4,p`. Scoped volatile accesses in that trial
preserve the load order in the remap setup. In the pending-array path, stock
loads the type into `eax` and the previous list head into `esi`. The trial
uses `esi` and `edx` respectively. Changing local declaration order, temporary
lifetimes, inlining, and x86 processor targets left those four bytes unmatched.

The relink also builds `CTemplTool_FixTypedefObject` at `0x54FB80` from C.
It discards a typedef when any ancestor scope is a template scope or when
the type is dependent. A null typedef or a template scope skips the
type-dependence check. Windows 9.2 and 9.4 reproduce all 68 bytes and the
single call operand with `-O4,p -inline auto,deferred`. Pro 8 emits 60 bytes.
The stock RC1 differential matched 4,096 combinations of typedef presence,
scope depth, scope flags, and type dependence, including predicate calls.
A packed-template fixture reaches the retained-typedef path in all four
compiler modes and produces the same objects as the preceding relink.

The stock RC1 differential matched 8,192 cases, including object substitution
during allocation, diagnostics, array initializer tracking, and VLA cleanup.
Real packed local and static declarations matched the preceding relink under
configured, file IPA, program IPA, and debug flags.

##### Recovered packed initializers and cleanups

The relink builds five more packed-instantiation helpers from C. The stored
initializer dispatcher substitutes a single expression, walks an expression
list, or visits a nested aggregate. The aggregate visitor substitutes each
node's expression before descending into its children. The cleanup walker
substitutes dependent catch types and resolves or unlinks dependent cleanup
records. A fully initialized class array uses one destruction record. While
an initializer is pending, the compiler emits per-element destruction records
in reverse order. A VLA with a class destructor runs the destructor loop before
releasing the allocation.

The native bodies agree across Wii 1.0RC1 and GC 3.0a5.2 after resolving
their address operands and switch targets. Windows CodeWarrior 9.2 and 9.4
reproduce all 102 bytes and three switch entries of the dispatcher,
all 280 bytes of the VLA cleanup updater, all 147 bytes and 22 switch
entries of the cleanup walker, and all 444 bytes of the dependent cleanup
resolver with `-O4,p -inline auto,deferred`. They reproduce the 412-byte
aggregate visitor with `-O4,p -inline auto,deferred,level=5` and the
following pragmas before the isolated function.

```c
#pragma opt_common_subs off
#pragma inline_max_size(512)
```

The visitor reads the child pointer into a local after substituting the
node's expression. The disabled common-subexpression pass produces the
out-of-line expression calls, and the inlining settings give five child
expansions before the recursive call. Pro 8 emits 373 bytes for this visitor
and 466 bytes for the resolver with the corresponding settings.

The updater initializes the destructor slot before walking the array
element types. In the walker an explicit null
comparison on the catch type allows CodeWarrior to test the field in memory
and pass it directly to the dependent-type check. The resolver shares the
failure exit for a non-class element or missing destructor. When the
pending-initializer search reaches the end of the list, the resolver writes
the whole-array cleanup and returns from inside the search loop.

| Helper | Wii 1.0RC1 | GC 3.0a5.2 | Native bytes | Windows 9.x byte match |
| --- | --- | --- | ---: | --- |
| VLA cleanup update | `0x5C2740` | `0x5A2EC0` | 280 | Exact |
| Stored initializer dispatcher | `0x5C2860` | `0x5A2FE0` | 102 | Exact |
| Aggregate initializer visitor | `0x5C28D0` | `0x5A3050` | 412 | Exact |
| Packed cleanup walker | `0x5C2A70` | `0x5A31F0` | 147 | Exact |
| Dependent cleanup resolution | `0x5C2B10` | `0x5A3290` | 444 | Exact |

The stock RC1 differential matched 65,536 cases, including callback order,
diagnostics, dependent catch types, and both class-array destruction paths.
The [compiler fixture](../gc2/tools/wii_retro/tests/README.md) exercises nested
aggregate initializers and dependent class cleanup with C++ exceptions enabled.
The fixture matches 116 native generator calls, including parser-error
controls. Separate traces reach the VLA destructor, dependent catch, and
catch-all paths in all four compiler modes. All four class-array PCH pairs pass.

The relink also builds the four named `CDecl` VLA registry functions from C.
Registration prepends a record. Lookup finds the first record by type
pointer and reports an internal error if it is missing. Removal unlinks the
first matching record from a supplied list and returns the resulting head.
Discard clears the active head. Windows 9.2 and 9.4 reproduce all 149 bytes
in both native compilers with `-O4,p -inline auto,deferred` after resolving
operands and using diagnostic line 90 for RC1 and 89 for GC.

The stock RC1 differential matched 24,576 registry lifecycles covering empty
lists, duplicate type pointers, missing records, and head and interior
removal. Separate missing-lookup controls verified the diagnostic. The
class-array fixture reached registration, lookup, and discard in configured,
file IPA, program IPA, and debug modes and reproduced the preceding objects.

##### Recovered constructor initializers

`RC1_InstantiateMemberInitializers` copies the packed constructor initializer
nodes in list order and processes them on the way back from recursion. It
substitutes the arguments, resolves a member variable or base type according
to the target flag, and passes each copied node to `CFunc_TransMemInit`. The
called `CTemplTool_InstantiateExprList` helper copies argument nodes and
substitutes their expressions in list order.

Windows CodeWarrior 9.2 and 9.4 reproduced both native functions with
`-O4,p -inline auto,deferred` after resolving the direct calls. Pro 8 emitted
178 and 85 bytes with the same flags.

| Function | Wii 1.0RC1 | GC 3.0a5.2 | Exact bytes | Resolved operands |
| --- | --- | --- | ---: | ---: |
| Constructor initializer worker | `0x5C22D0` | `0x5A2A60` | 171 | 7 |
| Expression-list substitution | `0x54EC70` | `0x53C140` | 82 | 3 |

The direct stock RC1 comparison matched 65,536 cases per function, including
empty lists, argument and target dispatch, callback order, and mutations during
allocation or substitution. The [constructor fixture](../gc2/tools/wii_retro/tests/README.md)
reached both base and member substitution and produced identical complete
PowerPC objects under configured, file IPA, program IPA and debug flags. All
3,072 Echoes objects matched the preceding relink, all four PCH producer/reader
pairs matched, and three regional link controls reproduced the original DOL
hashes.

##### Recovered member-variable substitution

`CTemplTool_InstantiateMemberVar` keeps a path-qualified member as-is. For
other members it searches the enclosing template records for a class template
and checks the original member by pointer in the master template. It then
finds the instantiated class through `CTemplTool_GetTemplClassInst` and
looks up the matching member by name. A context with no enclosing class
template keeps the original member.

| Function | Wii 1.0RC1 | GC 3.0a5.2 | Exact bytes | Resolved operands |
| --- | --- | --- | ---: | ---: |
| Member-variable substitution | `0x54EAA0` | `0x53BF70` | 252 | 11 |
| Class instance lookup | `0x551BA0` | `0x53EEA0` | 43 | 2 |

Windows CodeWarrior 9.2 and 9.4 reproduced both routines using
`-O4,p -inline auto,deferred`. The native member-variable bodies share
the same instructions and differ in four source line numbers for internal
diagnostics. The comparison uses the diagnostic constants from each version.
Pro 8 emitted 250 and 41 bytes with the same flags.

The direct stock RC1 comparison matched 65,536 cases per routine. It checked
path-qualified members, nested context chains, all five diagnostics and an
instance change during master-template lookup. The constructor and packed
member fixture matches the preceding relink under configured, file IPA,
program IPA, and debug flags.

##### Recovered master-template selection

`CTemplClass_GetMasterTemplate` follows the stored template record when the
class has an enclosing instance and the nested-template flag is set. Otherwise
it returns the original template. `CTemplTool_Template2TemplClass` converts
the embedded record at offset `0x34` to the owning class template and checks
the record kind. It returns null for a null record.

| Function | Wii 1.0RC1 | GC 3.0a5.2 | Exact bytes | Resolved operands |
| --- | --- | --- | ---: | ---: |
| Master-template selection | `0x5F1A30` | `0x5DC000` | 33 | 1 |
| Template class-record conversion | `0x551B70` | `0x53EE70` | 40 | 2 |

Windows CodeWarrior 9.2 and 9.4 reproduced both routines with
`-O4,p -inline auto,deferred`. Pro 8 also reproduced master-template selection
and emitted 41 bytes for class-record conversion.

The direct stock RC1 comparison matched 65,536 cases for each routine. A
nested class template followed the enclosing-record path 16 times in each
flag mode and produced the same PowerPC object as the preceding relink.
The nested class also matched across all four PCH producer/reader combinations.

##### Recovered class-record conversions

`CTemplTool_Class2TemplClassInst` checks the class-instance flag.
`CTemplTool_TemplInst2TemplClassInst` checks the parent template record before
recovering the owning instance from the record at offset `0x34`.
`CTemplTool_InstClass2Template` uses an existing class instance to recover the
owning template. The stored-definition path uses that template's virtual-base
flag when deciding whether to insert the hidden constructor argument.

| Function | Wii 1.0RC1 | GC 3.0a5.2 | Exact bytes |
| --- | --- | --- | ---: |
| `CTemplTool_Class2TemplClassInst` | `0x551A70` | `0x53ED70` | 51 |
| `CTemplTool_TemplInst2TemplClassInst` | `0x551B40` | `0x53EE40` | 48 |
| `CTemplTool_InstClass2Template` | `0x5519C0` | `0x53ECC0` | 84 |

Windows CodeWarrior 9.2 and 9.4 reproduced all three routines in both versions
after resolving their calls and data operands. The 84-byte routine inlines the
class and template conversions. The direct stock RC1 comparisons matched
65,536 cases per routine, including diagnostic order and returned pointers.
The constructor and nested-template fixture reached all three routines in
configured, file IPA, program IPA, and debug builds. Its PowerPC objects
matched the preceding relink in each mode and across all four nested-class PCH pairs.

##### Recovered class-instance allocation and lookup

`CTemplClass_NewInstance` links the new record into the master template's
instance list before name mangling or namespace creation. The original template
supplies the enclosing-instance record. Namespace creation uses that instance's
scope when the master has a parent template. Otherwise it walks past template
scopes. The allocator applies the file-unique name for a local class and
registers a public type tag under the master's name.

`CTemplClass_GetInstance` checks the cached arguments and the enclosing-instance
record. It prefers the cached original arguments when present and reloads the
enclosing record after comparing arguments. A cache miss calls the allocator
with the original template.

| Function | Wii 1.0RC1 | GC 3.0a5.2 | Native bytes |
| --- | --- | --- | ---: |
| Class-instance allocation | `0x5F1450` | `0x5DBA20` | 388 |
| Class-instance lookup | `0x5F13B0` | `0x5DB980` | 149 |

Windows CodeWarrior 9.2 and 9.4 reproduce all 149 lookup bytes and five address
operands in both versions with `-O3` or `-O4,p` and `-inline auto,deferred`.
An explicit branch keeps the ordinary-template fallback after the cache-miss
return, at the native offset. The revised lookup reproduces the preceding
Clang/LLD executable byte for byte.

In the allocator, RC1 calls `CClass_IsLocal` where GC tests the same `0x400`
class flag inline. The namespace template flag is at offset `0x23` in RC1
and `0x1F` in GC. The initial C90 translation emitted 355 bytes in each
version. The current trial places the master-template, name, argument,
parent-scope and file-local-name fallbacks after the main return. With
`-O4,p -opt nospeed,nocse -inline auto,deferred`, both Windows 9.x compilers
emit 377 bytes for RC1 and 375 for GC. They match 293 and 283 native byte
positions respectively. All calls on the main path are at their native
offsets. The file-local rename call is eight bytes early in RC1 and ten
early in GC. Register choices and alignment of the fallback blocks still
differ. The same configuration led the subsequent optimizer-pragma sweep.
The local comparison source is
`build/rc1-host-compiler/attached/match-native/class-alloc-nospeed-235.c`.

The production C and the allocator trial passed the 65,536-case stock RC1
comparison, including callback
order, diagnostics, namespace selection, and mutations during argument
comparison. A nested class fixture with the same inner argument in two
enclosing instances recorded four cache hits and one enclosing-instance
mismatch in each flag mode. Its PowerPC object matched the preceding relink,
including all four nested-class PCH pairs.

#### Ordering compatibility and experiments

##### Implicit class-template instantiation

The end-of-file loop at `0x47BF90` drains
declaration actions, finalizes classes, then calls `CTempl_Instantiate`. That routine calls
`CTempl_GenClassInstance` at `0x5C1012` for ordinary instances and `0x5C1052` for instances of
partial specializations. The instance generator visits members in namespace iterator order.
GC 3.0a5.2 uses the same iterator traversal and also pops deferred exports from the head.

In `CFilteredObjectList`, the implicit instantiation of `rstl::list<CEntity*>` adds its destructor
and then `do_erase` to the export list. RC1 drains `do_erase` first. The shipped order runs the
destructor first, followed by `do_erase` and then the derived destructor group queued by
class finalization. Wii_Retro reverses the newly prepended export list segment at each of
the two implicit class-instantiation calls. The rest of the list keeps its existing order.
The 27 common functions now appear in shipped order in all three Trilogy versions.
The R3ME01 body of `AddObject` still differs, and a relink with this object still misses the
original DOL hash.

| Trilogy build | Objects compiled | Readable object pairs | Units with improved function order | Units that gained exact order |
| --- | ---: | ---: | ---: | ---: |
| R32J01 | 319 | 315 | 18 | 4 |
| R3ME01 | 898 | 886 | 25 | 4 |
| R3MP01 | 319 | 315 | 18 | 4 |

The per-instance patch leaves all objdiff function scores unchanged and introduces no
relative function-order regressions in these configured builds. The 898 R3ME01 objects
also remain byte-identical to the previous compiler with file IPA forced for C++.
This is a compatibility rule inferred from the shipped output.

##### Ordering across instances and vtables

A probe that reverses all exports added by
`CTempl_Instantiate` matches the linked DOL hashes with `CSpatialPrimitive` substituted in each
region. Its two reserves belong to separate instances. In R3ME01 the same probe improves
64 units and worsens six. `CScriptPickupGenerator` and `CStreamPreloadedToken` lose exact
relative order. The production patch keeps the ordering between these instances.

With `-ipa file`, substituting either `CSpatialPrimitive` or `CCharacterFactoryBuilder`
built by Wii_Retro reproduces the original DOL hash in all three regions.
Substituting `CAnimTreeAnimReaderContainer` reproduces the R3ME01 DOL hash. Each
substitution uses an otherwise matching link. In R3ME01, the relative order of the
reader's weak functions also follows the IPA setting in the stock GC 3.0a5,
GC 3.0a5.2, Wii 1.0 and Wii 1.3 compilers. GC 3.0a5.2's non-IPA export trace
queues and drains the reserves in the same order as RC1.

##### File IPA

`CCallGraph_NewNode` at `0x5C0D00` appends to the list at `0x752F70`
using its tail at `0x752F74`. `CMid_ObjectAddrRef` and `CMid_DefineFunction` add nodes from
the paths that would otherwise prepend to the deferred export list. After computing the
graph and removing its dead nodes, the loop at `0x47CEC7` calls `CMid_GenerateCGNode` for
defined nodes in list order. The trace preserves object bytes and shows the distinct
sources of ordering. The two reserves in `CSpatialPrimitive` are added in definition order
at `0x47E834`. The reader's virtuals are added in vtable reference order at `0x47DA4C`.
In the factory, `CMid_DefineFunction` adds `ReleaseData` after the two virtuals.
The `CSequenceHelper` constructor wrappers are registered in reference order and defined
in reverse order through `CMid_RegisterDummyCtorFunction` and the pending dummy function list.
Their graph nodes enter in definition order at `0x47E606`.

##### Weak virtuals within one vtable

The vtable builder pushes `OLinkList` entries in
namespace traversal order, with the table offset at entry offset `0x8`. At `0x552F4C`
it calls `CInit_DeclareData`, which passes the list to `CMid_DefineData`. The original
loop at `0x47E1D3` calls `CMid_ObjectAddrRef` in list order. In the shipped vtables
for `CLineInstruction`, `CBlockInstruction`, `CAnimTreeAnimReaderContainer` and
`CDummyFactory`, their weak bodies follow descending slot offsets. Simple reversal
of the vtable's export segment breaks `CLineInstruction`. The patch ranks the new
exports by slot offset after `CInit_DeclareData`, preserving data relocations and
the existing order between vtables.

The patch improves relative order in 24 R3ME01 units and 14 units in each of R32J01 and
R3MP01. Eight R3ME01 units and four units in each other region gain exact relative
order. Function scores remain unchanged. Two R3ME01 units report extra inversions by
symbol name. In `CGMDeathMatch`, putting the destructor and `GetNumPlayers` out of line
at the beginning of a scratch source reproduces the relative order of all 12 common
functions with either compiler. In `CUVElement`, the shipped relocations point to
boolean bodies labelled for the other class. Following the physical slots, the
boolean virtuals in each shipped table appear in descending slot order.

Replacing `CLineInstruction` in otherwise matching links preserves the original
DOL hashes for all three regions. With file IPA forced, all 898 R3ME01 objects
are byte-identical to the preceding compiler.

With file IPA, the current inline `CLineInstruction` destructor joins the
graph through the vtable at `0x47DA4C` before `IsLineInstruction`. Making
the destructor implicit in a common scratch header lets `IsLineInstruction`
join first. The destructor enters from its definition at `0x47E606`.
This reproduces all nine shipped function positions on stock RC1 and
Wii_Retro in all three regions. Substituting the file-IPA Wii_Retro object
in each otherwise matching link reproduces the original DOL SHA-1.

##### References discovered during function code generation

The function at
`0x47E9D0` walks statement references with the callback at `0x47EAB0`, then visits
exception cleanup references through `CExcept_CheckStackRefs` before running the
backend. These paths call `CMid_ObjectAddrRef`. Simply reversing their new exports
corrects `VClone`'s references and reverses four `CSequenceHelper` constructor
wrappers whose native definition order is already correct.

Recording `CMid_DefineFunction` order fixes both cases. Sorting every new export
by definition gives 31 improvements and three regressions in R3ME01. In
`CCharacterSet` it moves the synthesized pair destructor past the templated
`CInputStream::Get` call. Keeping boundaries between ordinary and synthesized
members and between member and free functions avoids those inversions. Ordering
within each consecutive run improves 23 R3ME01 units with the same function
scores. The configured `CAnimTreeAnimReaderContainer` now reproduces the original
R3ME01 DOL hash when substituted in an otherwise matching link.

The four direct calls to `CMid_DefineFunction` in RC1 all record a definition
rank. Instrumenting the batches that enter definition sorting found no
unranked functions across 898 configured R3ME01 compilations. A scratch
sort that leaves unranked entries in place and sorts on either side
produces byte-identical objects in that sweep.

##### Class-template instances first referenced together

The two `CSpatialPrimitive`
reserves first become referenced during code generation for the same constructor
through the ordinary statement walk at `0x47EAD1`. The class visitor then instantiates
the box reserve before the sphere reserve. For consecutive instances first referenced
by the same function, preserving the visitor's ordering reproduces the shipped order
with the configured flags. It preserves the ordering within each class instance.
`CScriptPickupGenerator` is a control whose reserves are first referenced from
different functions. The corresponding probe improves 29 R3ME01 units without
order or function-score regressions. Substituting the resulting `CSpatialPrimitive`
object in an otherwise matching link reproduces all three DOL hashes.

| Configured build | Objects compiled | Readable object pairs | Improved order | Gained exact order |
| --- | ---: | ---: | ---: | ---: |
| R32J01 | 319 | 315 | 40 | 10 |
| R3ME01 | 898 | 886 | 69 | 21 |
| R3MP01 | 319 | 315 | 40 | 10 |

These counts compare all three ordering changes against the patch for individual
template instances. The combined build has the same function scores, and all
898 forced-file-IPA R3ME01 objects are byte-identical to the preceding compiler. The only
additional inversions by symbol name are the `CGMDeathMatch` and `CUVElement`
cases above.

The end-of-file pass now groups adjacent exports of each class instance
before looking up its first referrer. A trace of the native export list
shows both list destructors in `CMorphBallShadow` with no recorded statement
referrer. Both `do_erase` members first become referenced in
`RenderIdBuffer`. In `CHierarchyPoseBuilder`, the constructor first
references both `TSegIdMap` instances, while `BuildIntoHeirarchy` first
references the tree map's `insert`. Using the earliest recorded member
reference for the entire instance preserves the class visitor's order and
keeps each instance's members together.

The configured sweeps remove 18 inversions in R3ME01 and eight each in
R32J01 and R3MP01 without adding inversions or changing function body
scores. All seven functions in `CHierarchyPoseBuilder` follow the shipped
order in all three regions. All 11 `CMorphBallShadow` functions follow
the shipped order in R3ME01. The remaining body differences in those units
are outside this ordering change. All 898 forced-file-IPA R3ME01 objects
are byte-identical to the preceding relink. The three substitute-link
controls still reproduce the shipped DOL hashes in all three regions.

##### Vtables referenced by deferred functions

In `CScriptVisorGoo`, the two
`GetNewDerivedObject` helpers first reference their respective class-template
vtables while generating their bodies. The declaration pass at `0x47C120`
normally runs all deferred actions before returning to the vtable pass. The
shipped objects place each helper's virtual destructor immediately after that
helper. The relink checks for a referenced, undefined template vtable after
each work item at the two calls to `0x47D800`, then finalizes the table and
instantiates its virtuals when ready. The declaration boundary still handles
the native key-function and RTTI paths.

This restores the order of all 13 shared functions in `CScriptVisorGoo` in
R32J01, R3ME01 and R3MP01 with both configured flags and file IPA. Two R3ME01
function bodies still differ and substituting the object changes the DOL hash.
The R32J01 and R3MP01 substitute links fail on unresolved destructor and
`CMath::FastArcCosR` references from the current sources.
The full configured sweeps improve three R3ME01 units and two units in each
other region. The 898-object forced-file-IPA R3ME01 sweep improves three units
as well. Both sweeps preserve all function scores and show zero additional
order inversions. The linked controls for `CLineInstruction`,
`CSpatialPrimitive`, and `CCharacterFactoryBuilder` still reproduce the
original DOL hashes in all three regions with their matching flags.

##### Early class finalization

The wrapper used to cut the class lists
down to template instances before calling `CClass_Finalize`, then restore
them from two fixed-size arrays. With more than 2,048 nodes in either
list, it skipped early finalization. Prepending 4,096 unused polymorphic
classes to the `CScriptVisorGoo` probe exposed the fallback in both modes.
Each owner destructor was delayed until after both factory helpers.

`template_vtables.c` now walks both original lists directly. The code
mirrors the key-function search at `0x552D70`, the referenced weak vtable
path at `0x552C63`, and the two RTTI paths at `0x552CB5` and `0x552D04`.
It applies the same template and delay checks while walking, then uses
RC1's emitters for the vtable and RTTI data. End-of-file finalization
still runs over the original lists. The compiler emits all 24 functions
in the same order with zero or 4,096 added classes, including each
`GetNewDerivedObject` immediately followed by its owner destructor.
The standalone vtable patch uses the same walker. An RTTI-enabled control
with an ordinary key function, two class templates, and downcasts compiles
byte-identically before and after the change in both compiler builds.

In configured R3ME01, the first list reaches 2,772 nodes in
`CStateManager` and 2,336 in `CPlayer`. Those are the only two objects
whose bytes change in each of the six sweeps across three regions and
both compiler modes. The relink moves extra weak bodies earlier in
those objects. Across all 3,072 compilations, function scores and order
among symbols shared with the shipped objects stay the same. The nine
substitute-link controls preserve the shipped DOL hashes.

##### Vtables first referenced by ordinary functions

The R3ME01 relocation
audit found 35 template destructor and vtable-user pairs in the rebuilt
objects and the shipped objects. Of those, two destructors appeared too early
with Wii_Retro. The first references come from the ordinary functions
`CCharacterFactory::CreateCharacter` and the `CFBStreamedAnimReader`
constructor. The first references in the matching owner factories come from
out-of-line template helpers.

In the configured build the statement walker at `0x47EAD1` passes these
references to `CMid_ObjectAddrRef`. The relink records implicitly weak data
first referenced by an ordinary function and leaves its template vtable for
RC1's end-of-file pass. This removes 42 inversions and adds none in R3ME01,
plus four inversions removed in each other region. Function scores stay the
same. The changed `CFactoryStore` object still reproduces the R3ME01 DOL hash.

File IPA marks the same data through the IR-reference pass at `0x47EDB9`,
while `cscope_currentfunc` is still the inline constructor. The relink records
when the same template instance defines an implicitly weak method other than
a constructor or destructor. A definition in the current top-level
declaration identifies the helper path in the traced factories.
`GetData` in `CCharacterFactory` is defined during an earlier declaration,
so it does not bring the later vtable forward. The R3ME01 forced-file-IPA
sweep removes 44 inversions across the two units. The other two regions
each lose six inversions in `CCharacterFactory` with file IPA.
The six configured and file-IPA sweeps compile 3,072 objects with unchanged
function scores and zero added inversions.

##### Function templates instantiated by deferred actions

The statement
walker in `CStateManager` first references the light sort specializations
from `BuildDynamicLightListForWorld`. The action dispatcher calls
`CTempl_GenFuncInstance` at `0x47D8D3` and defines the actor-light sort
before the world-light sort. Each sort also defines `__sort3` before
`__insertion_sort`. The shipped objects preserve those definitions' order.
In RC1, `CMid_DefineFunction` prepends both exports to the list and the
dispatcher runs them in reverse.

When the action list is empty, `CMid_DefineFunction` can instead generate
the last function immediately at `0x47E6FA`. This happens for the area sort
in `CStateManager`, so reordering the existing exports alone misses that
pair. The relink uses RC1's deferred-codegen flag during the last definition
if the export list contains an earlier free function template from the
same first referrer. It orders the pending siblings by reverse
first-reference order. This restores all three sort groups in
`CStateManager` and both groups in the configured
`CTransitionDatabaseGame` build. Their other order differences remain.

`CLightComboProjectile` distinguishes reference order from definition
order. The statement walker references the three `lower_bound` instances
before the sorts. The native action queue defines the mutable unique-ID
bound first, then the sorts, then the other bounds. Reverse first-reference
order puts both sort groups ahead of the bounds and reproduces the order
of all 23 common functions.

The R3ME01 `CGameResultsScreen` symbol at `0x8011111C` is labelled as
the coin insertion sort. Across all three shipped DOLs, the frag sort
branches to the code matching the compiled frag insertion sort. In R3ME01
that destination is `0x8011111C`. The coin sort branches to `0x80110CB4`,
and the frag sort itself starts at `0x80110D88`. Comparing the three
addresses under their physical names gives the shipped order for all
27 common functions.

| Configured build | Objects compiled | Improved order | Gained exact order by name |
| --- | ---: | ---: | ---: |
| R32J01 | 319 | 16 | 6 |
| R3ME01 | 898 | 25 | 10 |
| R3MP01 | 319 | 16 | 6 |

The only added pairwise inversion by symbol name is the
`CGameResultsScreen` label above. Function match scores are unchanged.
All 898 forced-file-IPA R3ME01 objects are byte-identical to the
preceding relink.
The `CLineInstruction`, `CSpatialPrimitive`, and `CCharacterFactoryBuilder`
substitute links still reproduce the shipped DOL hashes in all three regions.

##### Constructor member cleanup

RC1 calls `CExcept_CheckStackRefs` at
`0x47EA98` for each statement with a cleanup stack, after the statement
reference scan and before PPC code generation. Member actions (kind 7) and
base actions (kind 17) share the reference call at `0x4A3150`. A local object
(kind 1) uses the call at `0x4A30FA`. Member actions record the object pointer
and member offset, including for cleanups introduced by inlined constructors.

In `CBodyStateCmdMgr`, the command destructors enter the deferred list from
member actions for the same object. The original emits those destructors in
ascending member-offset order. The region controls locate the contiguous
destructor block in all three Trilogy DOLs with relocation masks. Following the
constructor's vtable references to their destructor slots independently
identifies 14 of the command destructors in the same order in each DOL.

A trial that puts all member and base cleanups before locals also moves
`~vector<SConnection>` ahead of the colliders in `CBallCamera`. The vector
cleanup belongs to an inlined object with a different object pointer.
The same trial moves the member `~CDummyFactory` ahead of the local
`~rc_ptr<IMetaTrans>` in `CCharacterFactory`, reversing their shipped order.
The relink instead orders only adjacent member cleanups for the same object
after the existing definition-order pass. It records only exports added by
the native cleanup visitor.

In the configured R3ME01 sweep, this removes all 88 inversions among the 30
shipped functions in `CBodyStateCmdMgr` and three in `CGameState`.
`CBodyStateCmdMgr` still has two body differences, in the constructor and
`BlendSteeringCmds`. No function body scores or other pairwise orderings
against the shipped objects regress.
The 319 configured R32J01 and 319 R3MP01 controls have unchanged body scores
and relative order. All 898 forced-file-IPA R3ME01 objects are byte-identical
to the previous build. The three substitute-link controls above still match
the original DOL hashes for all three regions.

##### Constructor temporaries

The constructor reference trace distinguishes
`CGameState`'s temporary `ncrc_ptr<CPlayerState>` from
`CCharacterFactory`'s named `defaultTrans` local. Both use action kind 1.
RC1 assigns a generated `@` name to the temporary and gives each direct
member cleanup the interned `this` name on its object pointer. The relink
moves those temporary exports behind the constructor's direct member
cleanups, preserving the order within each group.

This restores the relative order of `~CPersistentOptions`, `~CHintOptions`,
and `~ncrc_ptr<CPlayerState>` in configured R3ME01. Relocation-masked code
and the local destructor's call to `ReleaseData` confirm that order in the
shipped R3ME01 and R3MP01 DOLs. The patch removes three more R3ME01
inversions in `CGameState` and six in `CMemoryCard` in every region.
`CCharacterFactory` retains its matching pair. The configured sweeps
introduce no new pairwise inversions or changes in function body scores.

The tested stock Wii compilers from 1.0a to 1.5 and GC 3.0a5.2 emit the `CGameState` temporary
before both member destructors with file IPA. During `CMid_DefineFunction`,
the inline references from the reserved-vector destructor reach
`~ncrc_ptr<CPlayerState>` at `0x47DC78`. The member references follow at
`0x47E982`. All three nodes join the call graph during the same constructor
definition. The compiler emits the surviving nodes in list order.

The file-IPA relink records constructor cleanup provenance for newly
appended nodes. It orders defined cleanup nodes before emission at
`0x47CE80`, after the exception optimizer finishes. This fixes three
inversions in the 898-object forced-file-IPA R3ME01 sweep with no new
inversions or body-score changes. Scratch compilations with the Trilogy
regional flag sets also produce the three destructors in the same order.
The R32J01 graph lacks a split `CGameState` reference object. The masked
R32J01 DOL comparison identifies the persistent-options destructor, with
the separate temporary destructor still unidentified.

Naming the temporary or using an implicit conversion in a scratch source
preserves the prepatch Wii_Retro order with both flag sets.

##### Synthesized copy constructors in file IPA

The `CPortalTransition`
constructor calls the `CSfxHandle` copy at `0x1F4`, `0x208`, and `0x21C`
in all three originals. The copy body follows the default constructor
of `vector<CLight>`. RC1 appends the copy at `0x47DA4C` while serializing
the parent at `0x47E982`. Earlier references to the other member
constructors are still waiting for definition. Those constructors join
through `0x47E606` when the following declaration work runs. File IPA then
emits the copy ahead of the earlier member constructors.

The relink records the first serialized constructor references and the
declaration work that defines them. It moves a synthesized copy node
past earlier constructor references from the same parent when the last
of those deferred definitions is a non-copy constructor of a class
template instance. It uses RC1's `CClass_IsCopyConstructor` at `0x555740`
to identify both cases. The broader rule also treated `CAABox`'s copy as
an endpoint and added two inversions in `CAnimData`. It moved a `CScriptDoor`
copy past another copy as well. Neither case has the `CPortalTransition`
constructor sequence.

The resulting order matches all 40 common `CPortalTransition` functions
in each region and preserves their bodies. The shipped DOLs confirm the
three call targets and the copy's position after the vector constructor.
Stock RC1, GC 3.0a5.2, and Wii 1.0a, 1.3, and 1.5 reproduce the early
copy and its exact body in the R3ME01 probe. Three split functions remain
outside the compiled-name comparison.

A shared scratch source recovers those three entries. Ordinary inline
helpers return the `CMDL` and `CSKR` tags. The particle constructor calls
`CToken::GetRef`, `CObjectReference::GetObjectTag`, and `SObjectTag::GetType`
in sequence, using the reference-return form of `GetType` also present
in the shared headers for Corruption. `GetType` emits a single `blr`
because the type is the first member of the tag. The DOLs confirm all six
calls to the three local helpers and the preceding calls to the two
accessors outside this unit. With Wii_Retro, the probe reproduces the
relative order of all 43 shipped functions and 42 masked bodies in each
region. The camera spline optional assignment still differs. Stock RC1
retains 19 inversions with the same source probe, all involving the
sound-handle copy.

The regression sweep compiles 898 R3ME01 objects and 319 in each other
region, once with configured flags and once with file IPA forced. The
configured builds reproduce the previous object bytes. Each file-IPA sweep
changes only `CPortalTransition` and removes its 16 inversions. Function
scores and the other tested orders are preserved.

##### Copy definition availability in `CAnimData`

The original constructor
calls `CCECharacterInfo`'s copy at `0x64`, the `CAABox` copy at `0xDC`, and
the `CVector3f` copy at `0xF8`. The first two copies precede the vector copy
in all three DOLs. The box call targets the split label `fn_8001A6D8`.
With the current source, RC1 appends the vector copy at `0x47DA4C` during
serialization and appends the other copies at `0x47E606` during later
definition work. The overly broad copy-order rule moves the vector past
that late box copy and crosses unrelated helpers in the process.

Moving the existing `CCECharacterInfo.inl` include ahead of the parent
constructor makes both the character-info copy and the implicit box copy
available during serialization. Both join through `0x47DA4C`, ahead of the
vector copy. Moving only the character-info copy definition gives the same
sequence. In a separate shared-header probe, an explicit inline memberwise
box copy also appears ahead of the vector copy. An explicit inline
character-info destructor joins through `0x47DA4C` and takes its
earlier position. Making the `rc_ptr<CAnimationManager>` copy available
before the parent, by explicit instantiation in the probe, brings that
copy forward as well.

Together the scratch changes reduce R3ME01 and R3MP01 from 306 to 120
inversions across 131 identified functions. They fix 199 inversions and
create 13. R32J01 goes from 319 to 121 across 134 functions. The mapped
instruction bodies are byte-identical with relocations masked. Each build
has 20 functions with different relocation records. At this stage the
`ncrc_ptr` copy and other helper order differences remain. The shared
headers were used through scratch include overlays.

##### Deferred copies from member accessors in file IPA

The original
`CCharacterFactory` constructor calls the `CAdditiveAnimationInfo` copy
at `0xD0` and the `rc_ptr<IMetaTrans>` copy at `0x138` and `0x198` in all
three DOLs. Each copy immediately precedes the accessor supplying its
source in the shipped text order. `GetDefaultAdditiveAnimInfo` and
`GetDefaultTransition` both return an lvalue reference to the copy's
class. RC1 visits each copy before its accessor during serialized
reference marking at `0x47E982`. The accessor joins the graph at
`0x47DA4C`. The copy definition joins at `0x47E606` during the next
declaration's work. The additive copy is implicitly weak and the
`rc_ptr` copy is a template instance.

An explicit memberwise copy and an early copy instantiation reproduce
this order in a shared source probe. The relink uses the corresponding
serialized copy and accessor references to preserve that order for
first-referenced copies defined by the next declaration's work. It reads
the result type at `FunctionType+0xE`, the same field used by
`funccallexpr` at `0x5C83A3`, and the static member flag at `+0x2E`, as used
by `CABI_GetFirstRealArgument` at `0x566CD8`. The deferred-copy move
is limited to non-static members. In `CAnimData`, imposing adjacency on the static
`CAABox::MakeMaxInvertedBox` would move the copy across a
`CPASDatabase` destructor that precedes the copy in the original.

The change removes all 33 inversions among the 61 shared named functions
in `CCharacterFactory` in each region and preserves their masked bodies.
Both copy bodies match the originals. The DOLs confirm the three call
targets. The other 14 split entries lie outside the named comparison.
The sweep compiled 1,536 objects with configured flags and another
1,536 with file IPA forced. All configured objects were byte-identical
to the previous build. Only `CCharacterFactory` changed under file IPA,
and all audited function scores were preserved.

##### Copies already adjacent to their accessors in file IPA

The original
`CAnimData` constructor calls the `ncrc_ptr<CAnimSysContext>` and
`rc_ptr<CAnimationManager>` copies at `0xB8` and `0xC4` respectively.
It calls `CQuaternion::NoRotation` at `0xFC`, then the quaternion copy at
`0x108`. The shipped text order puts the copy immediately before
`NoRotation`. The DOLs confirm these calls and the masked instruction
bodies of the four targets in all three regions.

The shared source probe makes the `ncrc_ptr` copy available by defining
it explicitly with the existing base initializer and instantiating it
early. Explicit inline bodies for the three derived point-of-interest
destructors reproduce the shipped interleaving with their constructors.
Placing `GetLocatorSegId` before `ResetPOILists` and `GetAnimSysContext`
after `GetAnimationDuration` reproduces the ordinary definition order.
The overlays are scratch controls. Stock RC1, GC 3.0a5.2, and Wii 1.0a,
1.3, and 1.5 already keep the quaternion copy next to `NoRotation`
with those inputs. In the previous relink, making the earlier
`ncrc_ptr` copy available changed which deferred constructor the
synthesized-copy rule saw last. It moved the quaternion copy away from
`NoRotation`, introducing 23 inversions per region.

The relink now keeps an early synthesized copy in place when its
serialized reference appends it directly before a member accessor that
returns its class by reference. This also preserves adjacent static
accessors. The rule removes the 23 inversions in each regional probe.
All mapped instruction bodies and relocation records are preserved.
The combined probe leaves 11 inversions among 131 identified functions
in R3ME01 and R3MP01 and among 134 in R32J01. The original source sweeps
reproduce all 3,072 object files byte for byte with configured and
forced file-IPA flags.

##### Template registration and end-of-file passes

The 11 pairs left in
the `CAnimData` probe involve template members that join through
`CMid_DefineFunction` at `0x47E834`. RC1 defines the `IMetaAnim`
`ReleaseData` before four other `rc_ptr` specializations. The original
places it after those four. RC1 also defines three tree helpers before
the `vector<SObjectTag>` reserve, then emits the `CRandom16`
`ReleaseData`. The original places that `ReleaseData` and reserve,
in that order, before the tree helpers.

RC1 prepends new instances to the owning template at `0x5F14AC` and
`0x5F14AF`. `CTempl_Instantiate` starts at the head of each instance list
and visits its `+0x30` links. In the baseline trace, generating
`ReleaseData<CAnimSysContext>` discovers `ReleaseData<CRandom16>` through
the inline destructor references at `0x47DC78`. The visitor has already
passed that specialization, so its body joins in the second pass.
The three tree helpers and the reserve join in the first pass.

Putting `rstl/set.hpp` and `CAnimationManager.hpp` ahead of the game's
`CAnimData.hpp` reproduces 10 of the 11 pairs. The remaining pair is
`ReleaseData<IMetaAnim>` and `ReleaseData<CRandom16>`. In the shared
`CAnimSysContext.hpp` overlay, spelling `CCEAnimationSet::EventSetList`
as its existing type, `rstl::vector<CAnimPOIData>`, lets the header use
the POI and vector headers directly. This removes the early inclusion of
`CAnimation.hpp` through `CCEAnimationSet.hpp` and registers the random
pointer before the meta-animation pointer. RC1 visits the two in the
shipped order in the first pass. An early `sizeof` of the random pointer
reproduces the same order independently. The overlay keeps the layout
and template specialization declarations common across the three builds.

With the full source probe, Wii_Retro reproduces the order of all 131
identified functions in R3ME01 and R3MP01 and all 134 in R32J01. Stock RC1
also has zero inversions among its shared functions. Across the ten
affected helpers, eight masked bodies match. Decoding 41 calls in each
original DOL verifies every target in this group. The
reserve and tree insert still have body differences. Comparing the new
probe against the previous source probe preserves all mapped masked
bodies and changes relocation records in 20 functions. The header
overlay also preserves common function order and masked bodies in
`CCharacterFactory` in all three regions and in the R3ME01
`CAnimationManager`, `CMetaAnimPlay`, `CMetaAnimRandom`, and `CTreeUtils`
controls. The game and RSTL changes are scratch probes.

`CScriptObjectLoaderHelper` provides an independent test. Including
`rstl/red_black_tree.hpp` first makes the vector reserve precede the two
tree helpers with file IPA on stock RC1 and Wii_Retro. All 18 function
positions follow the original order in each region. Thirteen masked
bodies match, including the three affected helpers. The original DOLs
confirm five calls to those helpers per region. Including `vector.hpp`
first retains both inversions.

##### Ordinary vtables in file IPA

RC1 prepends completed classes at
`0x555E98`. Its end-of-file class finalizer at `0x47BFB2` visits that list
and declares vtables, appending their newly referenced virtual functions
to the call graph at `0x47DA4C`. The non-IPA deferred list also prepends
those references, which reverses their relative order again before
emission. The file-IPA emitter uses the surviving call-graph order.

An experimental reversal of the file-IPA groups that introduce their
class's own destructor appeared to remove all 136 name-based inversions
in `CVectorElement`. Several identical particle destructor bodies in the
split have labels that name a different class from the vtable that
references them. Checking the destructor slots against uniquely located
`GetValue` bodies verifies 15 of the 17 late `CVectorElement` destructors,
all 12 in the tested `CRealElement` range and all three in the
`CIntElement` range in each shipped DOL. Comparing by vtable ownership
raises the `CVectorElement` inversion count from 40 to 96 among pairs
involving moved destructors. The experiment also introduces new
inversions in `CBSLocomotion` and `CBSTurn`. The relink retains the
existing file-IPA vtable order.

The six derived destructors in `CFilteredObjectList` have distinct
vtable ownership. With the existing explicit inline definitions, the
configured Wii_Retro flags reproduce all 27 function positions and
forcing file IPA reverses the six destructors. Removing those six
definitions in a shared scratch header gives implicit destructors and
reproduces all 27 positions and the vtable order with file IPA on stock
RC1 and Wii_Retro in all three regional builds. Wii_Retro still matches
26 of 27 bodies. `AddObject` remains at 94.51%. In the stock trace the
explicit inline destructors join the graph through the vtables at
`0x47DA4C`. The implicit definitions join later at `0x47E606` in shipped
order. Reversing the class declarations puts the six destructors in
order but also reverses the vtable data order relative to the shipped
objects.

In `CABSFlinch` and `CMetaAnimRandom`, the explicit inline virtual destructor
joins through vtable references at `0x47DA4C`. Making it implicit in the
scratch header defers its definition until `0x47E606`, after the file-IPA
template pass. That puts the destructor immediately after
`rc_ptr<CAnimTreeNode>::ReleaseData` in `CABSFlinch` and after the vector
reserve in `CMetaAnimRandom`, in the shipped order. The probe reproduces all
nine `CABSFlinch` positions in each Trilogy region and all eight
`CMetaAnimRandom` positions in R3ME01. Stock RC1 agrees on order. The DOLs
verify the destructor slots and the helper calls. The source probes preserve
the existing masked bodies on Wii_Retro. The current explicit-inline
`CABSFlinch` header also reproduces the order under configured flags.

##### `CAutoMapper::SetResLockState`

The two template bodies are identical
after masking relocations. Ten branches in `OnNewInGameGuiState` and
`CompleteMapperStateTransition` identify the capacity-nine instance
before the capacity-two instance in all three shipped DOLs. With the
current source and file IPA, stock RC1 adds the capacity-two graph
node first at `0x47E834`. Explicitly instantiating the capacity-nine
instance first, directly after the template definition, restores the
pair's order and preserves both exact bodies on stock RC1 and Wii_Retro
with all three regional flag sets. It lowers the total count of
relative-order inversions among common R3ME01 functions from 25 to 14
with file IPA. End-of-file instantiation comes too late to affect this
pair.

Moving the unchanged `LoadMapIcons` before the constructor and
`IsAreaInOtherWorld` before `CanSwitchLightDarkWorld` in the same scratch
source leaves three inversions against the R3ME01 split with each regional
flag set on both compilers. The remaining pairs involve the cached-token
destructors and the weak wide-string concatenation and `CanBuild` helpers.
The constructor trace shows that the `CStringTable` token destructor
enters the graph with its optional-member helper at `0x47DA4C`, before
the direct `CModel` and `CTexture` token cleanups at `0x47E606`. Four
branches in `CScanDisplay::StartScan` identify the three identical
bodies in each shipped DOL. Their addresses confirm the same
`CModel`, `CTexture`, `CStringTable` order in all three regions.

The serialized reference at `0x47E982` enters
`optional_object<TCachedToken<CStringTable>>::~optional_object` and
appends both token destructors through its inline references. The same
constructor queues implicit special-member actions for the model and
texture token destructors. The relink follows those actions into the
call graph and delays an indirect weak destructor behind earlier
deferred member cleanups of the same class template. It compares the
template identity stored by `CTemplClass_NewInstance` at `0x5F1537`
and changes graph traversal only for emission. The `TToken<CStringTable>`
dependency keeps its position. The change removes both cached-token
inversions.

The cached-token validation compiled 3,072 objects across six sweeps.
All 1,536 configured objects were byte-identical to the preceding
relink. The file-IPA sweeps found zero new order inversions or
body-score changes. In addition to `CAutoMapper`, the file-IPA output
changes for `CPlayerGun` in each region. Relocation-masked matches
for its two cached-token destructors occur outside the shipped
`CPlayerGun` text ranges. All nine substitution links for
`CLineInstruction`, `CSpatialPrimitive`, and
`CCharacterFactoryBuilder` reproduce their region's shipped DOL hash.

##### Serialized free and virtual functions
The serialized-reference trace places both first graph entries inside
`CAutoMapper::Update`. The shipped `CResFactory` vtable identifies
`CanBuild` immediately before the concatenation body in all three DOLs.
The call sites appear in the opposite order within `Update`. The direct
concatenation call precedes the virtual call through slot `0x18` in
all three versions, as it does in the current source. In RC1 the
reference-array walk at `0x47E8C0` reaches the free function, then the
virtual method at `0x47E982`. With ordinary deferred compilation,
the prepended exports reproduce the shipped pair. With file IPA,
the appended graph nodes reverse that pair. GC 3.0a5.2 and the tested
Wii compilers through 1.5 reproduce the same mode difference.

The graph trace reproduces all 898 R3ME01 file-IPA objects byte for
byte. Among adjacent emitted functions first discovered through the
same serialized body, 68 comparable pairs already have shipped order
and nine are inverted. Reversing whole serialized-reference groups
would disturb the matching pairs. The mapper is the only exact pair
in this audit with a free function followed by a virtual method.

The relink records each eligible node's first serialized reference and
emits the virtual first when both nodes are adjacent during emission
and come from their own references in the same body. Defined data and
other emitted functions delimit the swap. The mapper then has zero
order inversions across all 84 common functions when compiled with the
earlier source probes under each regional flag set. The unmodified
source still has 22 inversions against the R3ME01 split with file IPA.

The six serialized-order sweeps compiled 3,072 objects. All 1,536
configured objects were byte-identical. The file-IPA runs removed the
mapper inversion and reported zero new inversions or body-score
changes. The only other changed unit is `CScanDisplay`, whose moved
concatenation and `GetHealthInfo` bodies match elsewhere in each
shipped DOL. A substitution link for that unit is blocked by unresolved
symbols in the split inputs. The nine `CLineInstruction`,
`CSpatialPrimitive`, and `CCharacterFactoryBuilder` control links
all reproduce their region's shipped DOL hash.

##### Serialized nonvirtual members

The other eight inversions in the
adjacency audit are between nonvirtual member functions. Five strong
definitions separate the `GetVelocityWR` and `SetMomentumWR` bodies in
the shipped `CGroundMovement` objects, and four separate the other
`CGroundMovement` pair. The other six pairs lie within their weak
function groups.

`CGroundMovement::CheckFalling` matches with file IPA. Relocation-masked
body matches place `SetMomentumWR` and `GetVelocityWR` at offsets
`0x1BC` and `0x1EB8` from `CheckFalling` in all three shipped DOLs.
In the R3ME01 split both helpers' first surviving direct calls are in
`MoveGroundCollider_New`. Their first serialized reference is also
shared, so those two events alone cannot explain their emission points.

In a scratch copy, reading `mVelocity` directly in `CheckFalling` moves
the getter between `MoveGroundColliderXY` and `GetLastNonCollidingState`,
its shipped position. The Prime decompilation already grants
`CGroundMovement` friend access to `CPhysicsActor` for the last
non-colliding state, and its `CheckFalling` reconstruction calls the
getter. The direct-field probe preserves all 75 previously exact
bodies on stock RC1 and Wii_Retro under each Trilogy flag set. Binding
the getter to a local reference keeps the earlier placement. Running
native inline expansion before IR packing removes both shipped
helpers from the file-IPA object.

The shipped `CWorld::GetAliveAreasEnd` body loads `skGlobalEnd`.
A scratch `__declspec(noinline) inline` definition before
`CheckFalling` preserves the call and places the helper before
`SetMomentumWR`. Together with the direct-field probe, it reproduces
the first three common function positions and the getter's neighbors
in every region, with 76 of 87 common bodies exact. The original
source forms and the rest of the unit's order remain open.

In the `CCollisionActorManager` constructor, the shipped code calls
`ByElementMultiply` at offset `0xB88` and `SetTranslation` at
`0xB94`. The current file-IPA body makes both calls at those offsets
but serializes the outer call first. Computing the argument in its
own statement reproduces the pair's shipped order on stock RC1 and
Wii_Retro with all three flag sets. The constructor score falls from
99.89% to 99.82% in the scratch build.

RC1's expression serializer at `0x55B5F0` visits the callee before
the arguments. The call case recurses on the callee at `0x55B796`,
then walks the argument list and recurses on each expression at
`0x55B7B3`. The `EFUNCCALL` and `EFUNCCALLP` cases in the GC 2.7
`CInline_SerializeExpr` have the same traversal. `CMid_DefineFunction`
reads the resulting reference list at `0x47E8C0` and records references
for the call graph at `0x47E982`. For member calls the receiver is an
argument, so the serialized references can precede the instructions
that compute the receiver. Recompiling the following R3ME01 probes with
the reference tracer reproduces their uninstrumented objects byte for
byte and shows the same first referrers with the new reference order.

- In `CCollisionTracker::Think`, chaining the world, area, and
  occlusion lookups places `GetOcclusionState`, `GetAreaAlways`, and
  `GetWorld` in that order. The source with a local area reference
  emits `GetAreaAlways`, `GetWorld`, then `GetOcclusionState`.
  `Think`'s masked body is identical under both source forms on stock
  RC1 and Wii_Retro with each release's flags. The three call sites
  at `0x80`, `0x88`, and `0x8C` in the uniquely matched `Think` body
  resolve the helper order directly in each shipped DOL. In occlusion,
  area, world order the helper addresses are `0x8006B438`, `0x8006B458`,
  `0x8006B470` in R3ME01, `0x8006B208`, `0x8006B228`, `0x8006B240` in
  R32J01, and `0x8006C9B0`, `0x8006C9D0`, `0x8006C9E8` in R3MP01.
- In `CSpindleCamera::Think`, a single `GetParameters` call in the
  receiver of `IsFlagSet` serializes the flag test first. The probe
  saves the parameters pointer in that expression for reuse after
  the early return. In `CalculateTargetSplineDistance`, binding the
  player spline before the target spline fixes the second pair. Both
  changes preserve the masked instructions in the two bodies and all
  per-function match scores. The shipped `CMotionSpline::HasControlPoints`
  tests the control-point count for nonzero. Using that inline method
  for the three spline checks in `Think` reproduces the getter body but
  places it after `Think`. Using the same method for both checks in
  `CalculateTargetSplineDistance` makes that function the first
  serialized referrer. Both calls inline there, and the three direct
  calls in `Think` remain. The combined probe reproduces the order
  of all 32 shipped functions in each region on stock RC1 and Wii_Retro.
  `HasControlPoints` is exact and the other body scores are identical
  to the baseline. The probe emits 77 functions before linking and
  `Think` still differs from the shipped body.
- In the leaf case of `CMetroidAreaCollider::AABoxCollisionCheck_Internal`,
  binding `node.GetOwner()` between `GetTriangleArray()` and `GetSize()`
  reproduces the shipped `GetOwner`, `GetSize` order. Both compilers
  reproduce the result for all three split objects. The root body's
  masked instructions and relocations and all per-function match scores
  are identical under the original and probe source forms. Three
  remaining inversions involve `CCollisionPrimitiveData::GetVert`.
  The shipped DOLs place `GetVert` 16 bytes before
  `MovingSphereCollisionCheck_Cached`. A scan of each DOL's text finds
  three direct branches to that definition, at `0x288`, `0x298`, and
  `0x2A8` in `MovingSphereCollisionCheck_Cached`, all after its
  `GetTriangleMaterial` call at `0x240`. In the scratch object the
  same function first serializes `GetTriangleMaterial`, the surface
  constructor, and `GetVert` in that order. Moving the unchanged getter
  body out of line just before `MovingSphereCollisionCheck_Cached`
  corrects all three inversions with normal or explicit weak linkage
  on both compilers in all regions. The getter still matches, but the
  `MovingAABoxCollisionCheck_Cached` overload that takes a leaf cache
  falls from 96.20% to 89.54%. Keeping the moved definition `inline`
  preserves the body scores and its later emission, even with
  `force_active`. Adding `noinline` to that definition keeps the late
  order and reproduces the same body regression. The source form that
  preserves both the early position and the later inline expansion
  remains open.

##### Discarded first referrers

Across 886 readable R3ME01 file-IPA
pairs, 186 exact helper bodies have a comparable first serialized
referrer. In 23 cases the shipped helper comes before its first
surviving direct caller, while the current file-IPA build puts it
after that function. These are `GetVert` and 22 helpers called by
`CGroundMovement::MoveGroundCollider_New`. In 97 other cases the
first surviving direct caller and first serialized referrer are the
same and both layouts place the helper after the caller.

In a scratch copy, an earlier uncalled `static` function that calls
`GetVert` gives the getter its shipped position. The same probe for
`SetPlayerIsSlidingOnWall` moves it ahead of `MoveGroundCollider_New`
and `GetDynamicGeometryMode`. With file IPA, `CMid_DefineFunction`
adds the static function at `0x47E606` and visits its serialized
references at `0x47E982`. `CCallGraph_Compute` and
`CCallGraph_RemoveDeadNodes` run at `0x47CD41` and `0x47CD46` before
emission. The first referrer disappears while its callee remains
live through later calls. The trace names the discarded function as
the first referrer and preserves the scratch object's bytes. An
unused static pointer to the member gives the same order. Declaring
the uncalled probe function `inline` leaves the callee's later order.
Both effective probes preserve the emitted function set, masked
instructions, and match scores on both compilers across the three
regional flag sets. They remove three inversions for `GetVert` and
five for `SetPlayerIsSlidingOnWall` against the R3ME01 splits.

Each shipped DOL contains the same masked `MoveGroundCollider_New`
body and the same 22 helper bodies before it. Branch targets confirm
those helper identities in each image. The setter's two calls are
at `0x234` and `0xEC8` within the movement function. The dynamic-mode
getter is called at `0x1C4` and is emitted immediately after the
movement function in each DOL.

An uncalled scratch function containing the movement statements
after cache initialization moves all 22 helpers before the movement
function in shipped order. The scope of the probe ends before the
local cache copy-back. It preserves the emitted function set and
masked bodies and reduces the common-function inversions against
the R3ME01 split from 479 to 155 on both compilers with each region's
flags. Extracting the same statements into a called helper changes
the caller body substantially. The discarded-function test isolates
reference timing. The original source of the early references is
unverified.

Moving the existing `BoxPrimType` and `SpherePrimType` definitions
before their first use removes 84 inversions and adds seven on top
of the discarded-function probe, with unchanged masked bodies.
Four shipped calls to `CCast::ToReal32(double)` correspond to casts
in the current movement source. Using the existing `CCast` helper
in those expressions recovers its exact body and raises the caller's
match against R3ME01 from 92.91% to 93.93%. Adding the helper brings
the combined probe to 81 inversions across all common functions.

##### The four unnamed movement helpers

The two `blr` bodies reached
at `0x11CC` and `0x11E4` in the shipped `MoveGroundCollider_New`
are the mutable `optional_object<CCollisionInfo>` and
`optional_object<TUniqueId>` dereferences. Both already match the
scratch source. Two pairs of calls at `0x9BC`/`0x9C4` and
`0xDD0`/`0xDD8` read the collision ID halfword and pack it into the
`TUniqueId` return register. The branch targets and all four bodies
match across the three shipped DOLs.

The scratch source nests an inline raw-ID accessor inside an inline
conversion. RC1 serializes the outer conversion first, so its body
appears ahead of the accessor, in shipped order. Saving the raw ID in
a local serializes the accessor first and adds an inversion with the
same match scores. The nested version accounts for all 92 R3ME01
functions after matching the four unnamed symbols by their bodies and
call sites. The two bodies are exact, and the movement body match rises
from 93.93% to 94.42%. These results hold on stock RC1 and Wii_Retro
with all three flag sets.

An earlier discarded placement construction of `CCollidableAABox`,
just before `CollisionDamping`, restores the constructor's shipped
order. It reduces inversions from 81 to 60 under each flag set while
preserving the full emitted function set and masked instruction bodies.
An uncalled probe with automatic storage gives the same result. Both
probes retain the current construction in `MoveGroundCollider_New`.

Giving `CCollisionInfoList` an explicit empty constructor in the
shared scratch header removes another five inversions, with the same
masked instruction bodies and match scores on both compilers in all
three regions. RC1 appends the implicit constructor after processing
the rest of `MoveGroundCollider`'s references, through the definition
path at `0x47E606`. The explicit inline constructor joins on its first
reference at `0x47DA4C`, before the material copy and normal helpers.
The R3ME01 file-IPA sweep compiled 898 objects against that same
header. It changed 42 objects, removed the five movement inversions,
and added zero inversions or body-score changes.

##### Deferred free-function templates in file IPA

The serialized
reference to `rstl::max_val<double>` precedes the direct graph
insertion of `CCast::ToReal32(double)`. The reference to
`rstl::min_val<double>` follows it. Each template reference prepends
a kind-two action at `0x47DCC2` in `CMid_ObjectAddrRef`. The drain
calls `CTempl_GenFuncInstance` at `0x47D8D3`, and both resulting bodies
enter the graph through the definition path at `0x47E606`. This puts
the adjacent `min_val`, `max_val` pair after the intervening ordinary
members. The shipped layout interleaves the conversion with the two
templates in reference order. The call destinations and masked bodies
confirm that consecutive sequence in all three DOLs.

The timing trace preserves all 898 R3ME01 file-IPA objects byte for
byte. An offline replay that assigns each queued free-function
template the position of its first serialized reference removes 24
inversions and adds eight in the unmodified corpus. It breaks existing
orders for the two `TCastToConstPtr` instances in `CSpindleCamera` and
the `allocate`, `destroy`, and `uninitialized_copy` group in
`CCharacterSet`. A compiler trial orders adjacent deferred templates
queued by the same serialized body in reference order at emission.
Across 3,072 compilations it removes the `min_val`/`max_val` inversion
in each region's forced-file-IPA build and adds 134 inversions. Most
of the new pairs are `__sort3` and `__insertion_sort` or
`CacheParticleId` instances. The trial captures later queue events
that the first-reference replay missed. The body scores are unchanged.
The compiler trial remains in the scratch build.

The trace also
records four reserved-vector destructors with the same first reference
anchor in the movement probe. Only the `float` instance occupies that
early position in the shipped layout. All four have a surviving direct
call in both versions of `MoveGroundCollider_New`. An offline replay
that also moves the traced destructors to first-reference order adds
379 inversions across the corpus and the movement probe.

##### Making the three inline bodies available

In the combined scratch
source, placing these explicit instantiations before the earlier
discarded movement body resolves 54 of its 55 inversions on both
compilers with all three Trilogy flag sets.

```cpp
template double rstl::max_val<double>(double, double);
template double rstl::min_val<double>(double, double);
template rstl::reserved_vector<float, 2>::~reserved_vector();
```

The explicit instantiations make the bodies available when RC1 walks
the serialized references. All three enter the graph at `0x47DA4C`,
alongside the ordinary helpers. The destructor follows `HasMaterials`,
and the two templates surround `ToReal32` in shipped order. Swapping
the explicit declarations leaves the emitted order unchanged. Both
compilers preserve the emitted function set and masked instruction
bodies in this probe. The shared RSTL declarations are the same in
all three regions.

The remaining pair comprises the `optional_object<TUniqueId>`
assignment and `reserved_vector<CCollisionInfo, 2>` subscript. Both
join through `0x47E606`. The movement body indexes the vector before
assigning the optional in the shipped instructions. In the earlier
discarded body, adding an optional assignment just before the first
subscript gives the assignment's template member the earlier entry.
An assignment inside `if (false)` has the same effect in R3ME01.
Explicitly instantiating the assignment instead moves it too early
and introduces 32 inversions.

The combined scratch probe reproduces the order of all 92 shipped
`CGroundMovement` functions on stock RC1 and Wii_Retro. All 92
positions were verified against the text of each shipped DOL by
matching relocation-masked instructions. `MoveGroundCollider_New`
still scores 94.42% against R3ME01. The source forms that supplied the
earlier references in the shipped translation unit remain unverified.

Tracing `CInline_PackIFunctionData` preserves all 898 R3ME01 file-IPA
objects byte for byte. Among 63 adjacent nonvirtual pairs with a
shared serialized referrer, the current order matches 55. Ordering
those pairs by the first packing event would correct seven and
reverse 28 previously matching pairs. The last packing event gives
the same comparison.

##### Stream-reader template order

With unchanged source, file IPA
reproduces the order of all 11 common `CCECharacterInfo` functions on
stock RC1 and Wii_Retro in all three regions. This includes the
`Get<CPASDatabase>` wrapper ahead of the bounds and effects readers.
Wii_Retro with file IPA also reproduces the order of all 24 common
`CPortalAreaData` functions. All three stream-reader bodies in that
unit match. Each shipped DOL's PAS, volume and portal wrappers branch
directly to their respective constructors, which match independently
after masking relocations. With the configured flags the volume and
portal readers are reversed. Marking either constructor inline changes
the matching reader body, and changing the vector initialization to call `Get`
explicitly leaves an order difference under file IPA.

#### Remaining order differences and source controls

##### Remaining exact-body order differences

Five configured R3ME01 units still
have a different relative order with all target function bodies matched. File IPA
reproduces the factory's DOL hash as described above. The following substitutions
also reproduce their DOL hashes in otherwise matching links.

- `CBlockInstruction` uses the scratch inline `PageInvoke` and file IPA. The
  configured source still emits `PageInvoke` before the inherited weak virtuals.
- `CStaticGeometryMap` uses the scratch reference that makes the vector reserve
  implicit. This reproduces the DOL hash in all three Trilogy regions.
- `NMWException` uses an out-of-line definition of the partial-array destructor
  before `__construct_array`, so the former can be emitted before its caller.
- `SSafeZone_FuncPtrs` places the crystal loader before `SafeZone_ApplyRenderEffect`
  and uses the original link's existing definition of the loader pointer.

File IPA also matches all 14 `CTransitionDatabaseGame` functions in R3ME01.
The original places the derived destructor before `GetMetaTrans` and the
two `lower_bound` instantiations. Moving the unchanged destructor out of
line ahead of `GetMetaTrans` in a scratch copy gives the original DOL hash
with either normal or explicit weak linkage. An inline definition in the same
location still emits after those three functions, even with `force_active`.

The analogous scratch change in `CAnimationDatabaseGame` reproduces the
order of all 11 functions with file IPA in all three Trilogy regions.
`GetAllUniquePrimitives` still differs in register allocation. The substitute
links complete and have different DOL hashes.

These source and flag probes establish which order changes can occur without
another compiler patch. They leave the original source forms and flags unverified.

Making the unchanged `CCoinGameMode` destructor inline in a scratch source or
header reproduces the configured order of all 18 common functions in each
region. The derived destructor then follows the red-black tree cleanup
instantiated at end of file. Seventeen bodies match. `Update` still differs
in registers and in the order of its loop increments. With file IPA, an
implicit destructor reproduces the order of all 18 functions and matches
the same 17 bodies in each region.

The same inline-destructor probe with the current relink reproduces all
nine `CScriptAreaDamage` function positions with the configured flags in
each Trilogy region. Six bodies match. File IPA still emits the destructor
before the four template helpers with either explicit source form. An
implicit destructor puts all nine in shipped order with file IPA, with the
same six body matches.

##### Exact-body order audit

Of 886 readable R3ME01 object pairs, 138
configured units and 110 forced-file-IPA units still have inversions
among common names. All shipped bodies match in five of the configured
units and seven of the file-IPA units. The large `CStateManager`
count includes 1,710 inversions between exact, ordinary definitions
with file IPA. The 279 exact inversions in `CCollisionActorManager`
under the same flags are all between weak bodies.

- Moving `Think` before `AcceptScriptMsg` and the loader's
  `AppendCommand` before `UpdateFireCommand` puts all eight
  `CScriptControlHint` functions in shipped order. File IPA matches
  all eight bodies on stock RC1 and Wii_Retro. Substituting the
  Wii_Retro object in the matching links reproduces all three shipped
  DOL hashes.
- With unchanged source, file IPA reproduces the order of all 73
  `CFlanger` positions and all 70 `CParticleDatabase` positions on
  both compilers in all three regions. The native `CacheParticleDesc`
  label uses `CCharacterInfo::CParticleResData`. The compiled
  `CCECharacterInfo::CParticleResData` spelling identifies the same
  position by its masked instructions. The shipped text positions
  for both units match their split objects after masking relocations.
  Several compiled function bodies still differ.

##### `CCollisionActorManager` inline definitions

Bringing the joint
description accessors and `ScaleAllBounds` ahead of the manager
constructor moves the ordinary weak methods from the definition
path at `0x47E606` to the serialized-reference path at `0x47DA4C`.
Moving the entire inline include also brings the copy constructor
and destructor too far forward. Omitting both explicit special-member
definitions lets RC1 generate them between the iterator dereference
and `push_back_unsafe`, in shipped order. Both synthesized bodies
match. Moving both explicit bodies after the manager constructor
leaves 22 inversions, and the implicit pair leaves six in each region.

`CActor::GetAnimationData` in the current source references
`GetModelData` through its inline body. Reading the same pointer
directly from `mModelData` gives `GetScale`, `GetModelData` in
shipped order with the same machine code. The header-only R3ME01
file-IPA sweep changed one of 898 objects and removed that inversion
with zero new inversions or body-score changes. An unused mutable
subscript in `ScaleAllBounds` provides the earlier `CVector3f`
subscript reference and resolves the other five inversions. Both
source forms give the same masked instructions. A subscript inside
`if (false)` leaves the later order.

The combined scratch source reproduces all 69 positions with stock
RC1 and Wii_Retro in each region. The R3ME01 label `fn_80068CF8`
is the float-times-vector overload, identified by its masked body
and the labels at the same relative position in R32J01 and R3MP01.
The R3ME01 Wii_Retro probe preserves its 78 emitted function bodies
after masking relocations. The manager constructor and `Update`
still differ from the target. The original expression that brought
the subscript forward is open.

##### `CGenericFSM2` definition order

Reordering the ordinary
definitions in a scratch source reduces the Wii_Retro file-IPA
R3ME01 count from 1,820 to 87. Moving the existing inline
`CTriggerData` constructor before the stream reader reduces it to
43. Registering the state classes in trigger, state, code, submachine
order, using implicit destructors, and explicitly instantiating the
four state-vector constructors before the reader reduces it to 12.
The final group of probes preserves the masked bodies. The 12 inversions
in each region split between constructor cleanup and reserve order.

The file-IPA trace appends each state-vector constructor, its vector
destructor, and then its leaf destructor during `CGenericFSM2`'s stream
constructor definition. The same leaf destructors also appear on later
`resize` temporaries. Wii_Retro was replacing the earlier member cleanup
record with the temporary cleanup record and moving those four leaf
destructors. The relink preserves the recorded member origin for a
virtual destructor when a temporary in the same constructor reuses
that call-graph node. This fixes the six constructor inversions on
all three regional probes, with identical masked function bodies.
The configured and forced-file-IPA sweeps each preserve object bytes
for 898 R3ME01 entries and 319 entries in each of R32J01 and R3MP01.

The serialized-transition vector's existing range-destruction
specialization names its iterator before the other state vectors are
registered. Moving the same declaration to the scratch implementation
after the includes lets the end-of-file visitor emit its `reserve`
after `ReleaseData` and ahead of the other reserves. The declaration
is common to the three builds. The move fixes all six remaining
inversions with no masked function body changes. Stock RC1 also
corrects the six reserve inversions with that source change.

The `CGenericFSM2State` destructor calls the same-type const-iterator copy
constructor at `0x4C`. A mutable iterator also reaches that constructor
through its base class. Defining the shared base copy constructor out of
line restores the call through the existing mutable range. Explicitly
instantiating the copy after the state constructor puts it in the
shipped order.

The split labels the `0x58` call target `~SSubMachine`. The shipped caller
passes only the pointer in `r3`. The seven instructions at the target
match the out-of-line `rstl::destroy<SSubMachine>(SSubMachine*)` helper,
including its tail branch to `rc_ptr<CGenericFSM2State>::ReleaseData`.
Calling the existing `destroy_impl` range from the vector destructor
and outlining the shared pointer helper also reproduces the entire
state destructor, including both call offsets and the single loop test.
The three bodies and their relocations match in all three regions after
accounting for the helper label.

The combined file-IPA probe reproduces all 89 R3ME01, 90 R32J01, and 89
R3MP01 function positions in relative order on Wii_Retro. The split positions
and both branch targets were verified against the original DOLs. Masked
instruction bodies identify the regional labels for both `Setup` methods.
Stock RC1 reproduces the same destructor and both helpers with 32 order
inversions in each region. The shared outlining experiment loses 15
previously exact R3ME01 instruction bodies, so these header edits serve as
diagnostic controls. The original inline decisions remain open.

##### `CBSWallHang` command manager accessors

In the original `Start`, the
accessor is inlined. `UpdateBody` calls the mutable accessor at `0x604` and
the second accessor at `0x62C`. Both bodies return the same member address.
A scratch source uses the existing const `GetCommandMgr` in `Start` and
at the second call site, with a cast that preserves the nonconst `GetCmd`
overload. That reproduces the relative order and masked instruction bodies
of all 41 functions with file IPA on stock RC1 and Wii_Retro. This also
identifies `fn_8003F460` as the const accessor. The 41 bodies and both
branch targets occur at the same relative addresses in all three DOLs.
In the R3ME01 substitute link, the split names `fn_8001ABDC` and
`fn_8001AC7C` identify the existing copies of `CVector3f::Zero` and the
four-argument `CAnimPlaybackParms` constructor. Giving the replacement's
two weak definitions those names lets the linker reuse the existing
bodies. Exporting the compiled actor cast under the split suffix
`_800402D4` also preserves its external references. With those symbol
aliases, the substitute link reproduces DOL SHA-1
`2375606f4e9429a699cfa02728b0bb1176421226`.

##### `CAnnihilatorProjectile` template registration

The implicit derived
destructor fixes its position under file IPA. Forward declaring
`rstl::vector` before the includes registers it ahead of `rstl::list`.
The end-of-file visitor then defines the exclusion-list destructor first
and the target-candidate reserve second. In the stock RC1 trace both
enter the graph at `0x47E834` and swap positions when the first
declaration changes. This gives the order of all 14 shipped functions
with stock RC1 and Wii_Retro in all three regions. Thirteen bodies match.
`GatherTargetCandidates` still differs in its stack frame and code.

Making the existing predicate `IsValid` definitions inline moves the weak
destructor before `IsValid` in `ScriptCameraSpline`, `CWaypointNavigation`,
`CScanDisplay`, and `CScriptTeamAiMgr` in R3ME01 with both flag sets. The
file-IPA probes reproduce the order of all common functions in those units.
`ScriptCameraSpline` and `CScanDisplay` give the same result in the two
regional graphs.

##### `CIOWinManager` definitions

Relocation-masked comparisons of all 13
bodies establish the same layout in the three Trilogy DOLs. Moving the
nested node constructor after `AddIOWin` and `GetIOWin` after `RemoveIOWin`
in a scratch copy reproduces that order on stock RC1 and Wii_Retro with all
three flag sets, with or without file IPA. Body scores are unchanged.
The `Draw` entry is labelled `fn_800D92DC` in the R3ME01 split. Its two
passes call `GetIOWin` on each node and dispatch through the window vtable.

##### `CTextExecuteBuffer` cleanup

The three DOLs place the generated
`CFontRenderState` destructor immediately after `AddString` and before
`GetAssets` and `BuildRenderBuffer`. With stock RC1 and file IPA, an unused
static function that deletes a `CFontRenderState*` between the first two
definitions gives the destructor an earlier graph node at `0x47E606`.
The call graph discards the static function and emits the destructor still
needed by the render code. A static function that just constructs and
destroys a local state has the same effect.

Both controls reproduce the order of the 30 named functions shared by
the split and the scratch builds on stock RC1 and Wii_Retro with all three
regional flag sets. The delete control preserves the R3ME01 body scores,
including the two render routines. The DOL comparison covers all 44
shipped positions, including 14 functions outside the named comparison.
An out-of-line definition of the destructor between `AddString` and
`GetAssets` also reproduces the named order in either mode. It changes
both render bodies. The original source that first references the
destructor remains unknown.

##### `CFBStreamedAnimReader` template registration

The 12 functions at the
end of the R3ME01 object have the same bodies and order in the three DOLs
after relocation masking. The compressed-reader constructor points to a
`TAnimSourceInfo` vtable whose slots resolve to `GetAnimationDuration`,
`HasScaleData` (the R3ME01 label `fn_803031D8`), and the destructor. The
destructor follows both bit loaders and the varying-length vector's
`AfterEnd`. Calls in `IncrementInto` identify `fn_803031A8` as the
four-component channel's `GetBitCount`.

RC1 prepends new class templates at `0x5F0A93`. The end-of-file visitor
at `0x5C0FE0` walks that list and its instance lists. With file IPA,
a scratch source first declares `TVectorOfVaryingLengthItems`,
`CBitLevelLoader`, `TAnimSourceInfo`, and
`CFBBitCompressedDataChannelHeader` in that order before the includes.
The channel declaration puts `AfterEnd` before `GetBitCount`, and
`TAnimSourceInfo` uses its implicit destructor. That reproduces the 12
helper positions. Moving the converter constructor and destructor before
`IncrementInto` and `VGetAdvancementResults` before `GetSegStatement`
also reproduces the order of all 46 shipped functions on stock RC1 and
Wii_Retro with all three regional flag sets. These source controls leave
the original declaration spelling and flags unverified.

Inlining the scale checks and caching the channel list's end iterator
reproduce the shipped `HasScaleData` body. Ten of the 12 helper bodies
match in the resulting R3ME01 scratch object. The two `GetBitCount`
variants each differ in the operand order of a commutative `add` after
using the same index calculation as `GetInitialValue`.

##### `CAnimSourceReader` ordinary virtuals

All 22 shipped function positions
agree across the three Trilogy DOLs after masking relocations. The reader
constructor loads both vtables. Their slots identify the two destructors
and pin the last four functions to this order.

| Region | `HasScaleData` | `GetAnimationDuration` | `~CAnimSourceInfo` | `~CAnimSourceReader` |
| --- | --- | --- | --- | --- |
| R3ME01 | `802D0698` | `802D06B0` | `802D06C0` | `802D0728` |
| R32J01 | `802CEFAC` | `802CEFC4` | `802CEFD4` | `802CF03C` |
| R3MP01 | `802D2CB4` | `802D2CCC` | `802D2CDC` | `802D2D44` |

With file IPA, removing the explicit reader destructor lets the compiler
synthesize it and reproduces all 22 positions on stock RC1 and Wii_Retro
with each regional flag set. All four tail bodies match. In the stock RC1
trace, the out-of-line destructor enters the graph from its definition at
`0x47E834`. The three info virtuals enter through `CMid_ObjectAddrRef` at
`0x47DA4C`. The implicit reader destructor enters later at `0x47E606`.

An explicit inline reader destructor also reproduces all 22 positions with
file IPA when the header completes `CAnimSourceReader` before
`CAnimSourceInfo`. The probe defines the private reader constructor after
both complete classes so it can still allocate a `CAnimSourceInfo`.
`CClass_AddClass` prepends each class to the list at `0x756FF4`.
`CClass_Finalize` walks that list at `0x552C20`. In this variant all four
virtuals enter the graph at `0x47DA4C` in shipped order. Moving the private
constructor alone leaves the order unchanged.

The configured builds still place the info destructor before its getters.
It occupies the highest vtable slot, as do the four `CGenericFSM2` state
destructors that ship before their other virtuals. The file-IPA source
controls leave the original source and flags unverified. Nineteen of the
22 R3ME01 bodies match with Wii_Retro in either file-IPA variant.

##### `CCharLayoutInfo` owner destructor

The shipped factory calls
`GetIObjObjectFor`, which branches to `GetNewDerivedObject`. The virtual
destructor precedes both helpers in all three Trilogy regions, the earlier
Wii objects, and the GameCube objects. In the current Wii sources the
first RC1 reference to the vtable is in
`GetNewDerivedObject`, so the check between deferred emissions runs after
both helpers. Moving the shared destructor definition out of the class body
in a scratch header leaves the order unchanged in all three Trilogy regions.
The R3ME01 inline-deferred, bottom-up, and RTTI probes also leave these four
functions in the same relative order. This is the only owner type with the
destructor before its construction helper among 33 such pairs in the R3ME01
objects. Explicit destructor specializations around the factory put it
before the entire group or after both helpers.

Stock GC 2.7 reproduces all 28 named G2ME01 bodies and their order.
Substituting that object in the existing link reproduces the shipped
G2ME01 DOL hash. In the native trace, the caller of `CInline_AddFRefList_Object`
at `0x55D1E9` first visits the typed `auto_ptr` cleanup. The visitor at
`0x55D1B8` reaches the direct call to the virtual owner destructor when
defining that cleanup. The `CInline_GenerateDeferredFuncs` export list
then emits the owner destructor ahead of the two helpers.

Using this earlier `auto_ptr<TObjOwnerDerivedFromIObj<T>>` return type in
a common scratch header changes how the same destructor is discovered
by Wii RC1. With the configured flags, the statement walker at
`0x47EAD1` adds `GetIObjObjectFor` and then the explicitly called
destructor while generating the factory. RC1 drains the destructor first
and matches the four bodies and their order with all three Trilogy flag
sets after accounting for relocations. The previous Wii_Retro definition
sort used the earlier definition rank of `GetIObjObjectFor` and moved it
ahead of the destructor.

Keeping every virtual destructor in a separate run breaks the shipped
helper order in `CScriptVisorGoo` and `CCredits`. Their paired factory
helpers first enter the export list from ordinary calls while compiling
constructors. The relink now separates a virtual destructor only when the
statement walker first references it directly from the function being
generated and that function is outside constructor cleanup handling.
This reproduces the four-function order with the typed-owner scratch
header in all three Trilogy builds. The masked instructions match each
region's split object. The two regional labels at `0x802DF238` and
`0x802E2F40` resolve to `CCharAnimMemoryMetrics::SubtractFromTotalSize`
by matching their bodies in the shipped DOLs.

All 1,536 configured objects across the three regions and all 898
forced-file-IPA R3ME01 objects are byte-identical to the previous relink.
The nine substitute-link controls retain the shipped DOL hashes.

With file IPA, the typed pointer brings the destructor's graph node
forward. In the trace at the emission boundary, the factory now has a
direct call edge to the destructor as well as to `GetIObjObjectFor`.
The outgoing list is at graph node offset `0x10`. These call edges have
count 1 and flags 0. The vtable's address edge to the destructor has
count 0 and flag 1 in both builds.
The typed `CScriptVisorGoo` control has the same destructor edge from a
constructor and keeps its existing helper order.

With the typed owner in a scratch header, declaring
`TToken<T>::GetIObjObjectFor` in the header and placing its inline
definition after the factory moves the destructor ahead of both helpers.
The unresolved helper is then instantiated after ordinary function
definitions. An explicit instantiation of the member immediately after
its definition brings both helpers forward, before the first layout
constructor. The graph enters the owner destructor, both helpers, and the
node constructor in that order.

This reproduces the order of all 13 shipped functions on stock RC1 and
Wii_Retro with file IPA in all three Trilogy regions. The four factory
bodies match after resolving the regional relocation aliases. Nine of the
13 bodies match in R3ME01. The original return type, source spelling, and
compiler flags remain unverified.

A scratch factory that constructs an owner in an unreachable branch
reproduces the first four functions' order with the configured flags in all
three regions, with unchanged body scores. In that probe
`CExcept_CheckStackRefs` enqueues the destructor at `0x4A30FA` after the
statement walker enqueues `GetIObjObjectFor`. The export list runs the
destructor first. The probe gives the original R3ME01 bodies for those four
functions. The rest of the translation unit still has differences, and the
probe does not reproduce the four-function order with file IPA.

The stock RC1 file-IPA graph records both factory helpers at `0x47E606`
before the unreachable cleanup reaches the owner destructor at the same
site. A scratch static helper that explicitly deletes an owner and precedes
the factory brings the destructor ahead of the factory itself. File IPA
eliminates that unused helper but keeps the destructor because the vtable
also references it. Defining the helper after the factory leaves both
factory helpers ahead of the destructor. Both controls preserve the four
shipped bodies. They constrain where an earlier reference would have to
enter the graph to reproduce the shipped order with file IPA.

##### `CSwarmBasics` reserve order

All seven reserve bodies can be located
uniquely in all three original Trilogy DOLs by masking the relocations in
the R3ME01 object. They are contiguous and in the same order in each region.
`AcceptScriptMsg` calls the boid reserve first and the sound-pair reserve
second. Their bodies appear in the opposite order. The `CSpatialPrimitive`
constructor calls the sphere reserve first and the box reserve second.
Its emitted reserves start with the box.

Reversing first-reference order within a class-instance group fixes the
boid and sound-pair order, preserves `CSpatialPrimitive`, and worsens eight
of 898 configured R3ME01 builds. `CPlayerRagDoll` and `CFontRenderState` lose
their previously exact common function order. In `CPlayerRagDoll`, the
class visitor visits the particle, joint and length reserves in shipped
order, while the first references arrive in particle, length and joint
order. The relink retains the class visitor rule.

Moving class registration with scratch typedefs reproduces all seven
`CSwarmBasics` reserve positions with the configured flags. Reversing the
early class declarations in a separate `sizeof` probe reproduces their
order and bodies with file IPA. Explicit instantiation at the end of the
source also reproduces that order in both modes. These probes leave the
rest of the translation unit and its original declaration order unresolved.

##### `CRevolutionMotionProcessor::Update`

The originals for R32J01, R3ME01 and R3MP01 all interleave the member
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

### Aggregate-copy scheduling (2026-11-17)

Tracing RC1's PCode and dependency graph around both calls to `scheduleinstructions` leaves the
compiled R3ME01 object byte-identical. The same baseline function scores 70.94% under the three-patch
compiler and the current native relink. The pre-allocation scheduler receives 27 loads and 27 stores
for `mStatus = status`. Scalar members arrive as load/store pairs. The embedded `DPDObject` array
arrives in groups of two loads and two stores. The aggregate assignment already has the target's
copy granularity at this point. The next aggregate copy is `mLatestSample = mHistory[mWriteIndex]`.
It reads from an indexed history entry.

RC1's `scheduleinstructions` (0x5B6920) selects the processor model, gathers aliases, and calls the
block scheduler (0x5B6AD0). The block scheduler traverses PCode backwards to build its graph. A
register dependency uses the def/use lists. A load is checked against later stores. A store is
checked against later loads and stores. The three memory checks call `may_alias` at 0x5B7026,
0x5B70D6 and 0x5B7126. A positive answer adds a dependency with the earlier instruction's latency.
The issue selector (0x5B6CB0) checks readiness and the processor's issue rules, then compares the
deadline, number of successors made ready, critical-path height, and opcode rank during coloring.
The block scheduler and issue selector match their GC 3.0a5.2 counterparts after masking relocations.

Both Windows 9.x hosts reproduced the scheduler entry routine and six alias-query wrappers
byte for byte after relocation. The scheduler entry is 417 bytes in RC1 and 369 bytes at
`0x5995C0` in GC 3.0a5.2. RC1 adds ELF and Zen model choices to the earlier selection rules.
The matching wrappers are `may_alias`, `may_alias_object`, `may_alias_worst_case`,
`instr_may_alias_alias`, `instr_must_alias_alias`, and `uniquely_aliases`.

RC1's alias-pair query at `0x5BE950` checks range overlap for parts of the same object. For parts
of distinct objects it resolves each part's whole-object alias through its parent list and
consults membership. The first whole object's membership takes precedence when present.
The recovered C query still differs in x86 encoding. Its decisions matched stock RC1 across
18,432 alias configurations, including signed range boundaries and asymmetric membership.
Scheduler model selection, callbacks, and block flags matched across 2,048 configurations.
The relink builds these routines from C.

The memory-width query `nbytes_loaded_or_stored_by` also builds from C. Both Windows 9.x
hosts reproduced all 838 bytes at `0x5A7620` in RC1 and all 551 bytes at `0x58B4C0` in
GC 3.0a5.2 after relocation. RC1 adds SPE and VLE memory opcodes, checks that paired-single
transfers use Gekko, and keeps the width calculation for register lists and string operands.
The opcode descriptor's load/store flags gate the dispatch in both versions.

The native differential covered all 912 RC1 opcode descriptors under four processor choices,
with varied operands and descriptor flags. All 700,416 cases matched transfer widths and
diagnostic sequences. [Memory-width source](../gc2/tools/wii_retro/pcode_width.c)

The relink builds the alias setup routines from C. Both Windows 9.x hosts matched the
11-byte `gather_alias_info` entry in both versions and the 219-byte GC 3.0a5.2 membership
worker at `0x5A0370` with `-O4,p`. GC rebuilds membership when the alias count changes.
RC1 refreshes the `set_aliases` list on each call, records ancestor bits, and unions child
membership until no bits change. Its ancestor walk handles three levels before recursing and
skips any parent whose bit is already set.

The RC1 membership, ancestor-walk, and synthetic-parameter routines still differ in x86 encoding.
Direct comparisons passed 16,384 synthetic parameter cases and 512 graphs, including cycles,
reused vectors, and vector growth. The checks compared allocation sizes, callback order,
synthetic records, and membership bits. [Alias setup source](../gc2/tools/wii_retro/alias_setup.c)

The relink now builds RC1's `propagatealiasinfo` at `0x6655A0` and its two workers from C.
The pass copies each block's incoming definition bits, clears earlier definitions when a GPR
is overwritten, and merges aliases from the definitions that reach each memory operand.
RC1 runs that pass when coloring is active and the optimization level is positive. Its fallback
uses both the read and write flags and the operand memory flags. GC 3.0a5.2 checks coloring alone,
uses the read and write flags in its fallback, and adds the worst-case pointer set to the
worst-case function set before walking the blocks.

Both Windows 9.x hosts reproduce the 225-byte reaching-operand worker and the 348-byte
per-instruction worker in RC1 (`0x665940`, `0x6657E0`) and GC 3.0a5.2
(`0x63D800`, `0x63D6A0`) after relocation. The per-instruction matching probe forces
separate loads of the two instruction flag words. The 574-byte main pass still has
x86 code generation differences. The GC map now records the full per-instruction
body and locates the 11-byte routine at `0x63D8F0`.

A stock RC1 differential matched 2,048 cases covering callback order, reaching bits,
alias choices, and instruction flags. [Alias propagation source](../gc2/tools/wii_retro/alias_propagation.c)

The same C unit now provides `is_const_alias_runtime_call`, `canidate_reaches_use`,
and `candidatealias`. Windows 9.2 and 9.4 reproduce the 59-byte call classifier and
the 263-byte reachability test in both RC1 and GC 3.0a5.2. The reachability test scans
intervening register writes from the definition for a forward use in the same block.
For other uses it checks the incoming definition bit and scans from the start of the block.

Both versions' candidate classifier derives aliases for symbolic immediates and
updates the address and access aliases separately for pointer-updating instructions.
RC1 can look up a type-based alias for a four-byte access with a GPR operand when
indirect aliasing is enabled in C99 or C++ mode. The GC map now ends
`candidatealias` at `0x644255` and places
`is_address_load` at `0x644260`. The candidate classifier still differs in x86 code
generation in both versions.

The stock RC1 differential matched 262,144 candidate cases, 32,768 reachability cases,
and 128 const-call cases. The candidate comparison checked the return value, alias
mutation, and callback order.

`make_distinct_virtual_alias` (0x5BFAB0) creates a synthetic array object for a pointer or reference
parameter. A read-only parameter gets a const synthetic object. A writable parameter enters the
typed alias set. A restricted parameter has an independent alias. In `Update`, the const
`WPADFSStatus&` loads and destination stores have distinct alias objects. The scheduler gets
negative answers between those accesses and positive answers from the stores to the following
history loads. In the virtual schedule, each status load can make its own store ready. Each store
still has a dependency on the history copy. The scheduler issues all 27 status loads before the
first status store, and register allocation spills.

| R3ME01 probe | Frame | Function match |
| --- | ---: | ---: |
| Native Wii_Retro | 0x90 | 70.94% |
| Add store-to-load edges within the status copy before coloring | 0x50 | 86.43% |
| Add store-to-load edges within the copy in both scheduler passes | 0x50 | 98.41% |
| Add load-to-store and store-to-load edges within the copy in both passes | 0x50 | 98.78% |
| Use the writable-parameter alias path in a scratch compiler | 0x50 | 98.78% |
| Recognize the complete aggregate copy in PCode and add the same edges | 0x50 | 98.78% |

The final three probes reproduce the original R3ME01 instruction bytes from function offset
`0xE0` through the epilogue, including both aggregate copies. The remaining differences occur
in the history-loop setup and register assignment before the copy. Applying the edges before
coloring reduces the live ranges. Applying them again after coloring preserves the target order
within the two-halfword groups. The writable-parameter probe reaches the same function score in
R32J01 and R3MP01. The copy-local probe tracks instruction identities from the first scheduler
pass into the second and leaves alias answers outside the tagged group intact.

The first copy-local rule covered every complete aggregate copied from a read-only alias. In
the 898-object R3ME01 sweeps it lost 28 exact functions under forced file IPA and 21 with the
configured flags. Uniform-width copies and small ordinary copies account for many of the losses.
The native rule selects copies that cover the whole source aggregate, mix transfer widths, and
contain scalar pairs and consecutive two-load groups with the same source-location marker.
The latter marks the array member in the status copy.
[`aggregate_schedule.c`](../gc2/tools/wii_retro/aggregate_schedule.c)

The native rule lowers peak live copy GPR values from 27 to two. All instructions and
relocations from offset `0xE0` through the epilogue match in R32J01, R3ME01 and R3MP01.
The function scores 98.78% in each build. The remaining history-loop mismatch is independent
of the aggregate copy. Spelling the loop's count initialization as an explicit if/else in
scratch source restores its instruction order and leaves a three-register permutation.

Each native sweep changed only `CRevolutionMotionProcessor.o`. R3ME01 compiled 898 objects in
each of the configured and forced-file-IPA sweeps. R32J01 and R3MP01 each compiled 319 with
forced file IPA. All prior exact matches survive, including the 164 audited register-order
controls in R3ME01. A synthetic aggregate with scalar fields and an embedded array also gets a
shorter schedule. Uniform-width, small scalar-only, and volatile-copy controls retain their
code. Compiling that fixture with scheduling disabled gives byte-identical objects.

### File IPA and inlined-local register order

RC1's initial assignment routine (`0x46D690`) and reassignment routine (`0x46D8B0`) both visit
arguments, named locals, temporary locals, and TOC entries in that order. The two local scans use
`IsTempName`. Initial assignment temporarily enables `optimizing` and requires the memory flag
for the first three scans. Reassignment checks usage counts for TOC entries. Both passes check
target volatility for pointers and arrays. The ordinary allocation checks also appear in GC
2.x's `allocate_local_vregs` in [CodeGen.c](../gc2/src/backend/CodeGen.c).

The GC 3.0a5.2 routines at `0x46B280` and `0x46B4A0` use the same field offsets as RC1. A C90
adaptation of the relink source compiles with Windows 9.2 and 9.4 using
`-O3 -inline auto,deferred`. After relocation, it matches all 536 bytes of `AssignLocals` in
both versions and 466 of 475 bytes of `ReassignLocals`. The remaining reassignment bytes encode
register choices in the named and temporary local scans. Initial assignment matches when the
volatility helper reuses an unsigned temporary for the pointer-or-array test and the selected
qualifier mask, and the TOC scan retains the second test of the register number.

Both relink passes save each list entry's object before calling the name or variable-info
lookup, matching stock. A direct comparison against RC1 and GC covered 16,384 cases across both
passes. It checked callback order and object identity, register fields, and restoration of the
optimization flag, including cases where a test callback replaced the object in the list entry.

The file-IPA ordering wrappers call these C routines. A fixture with volatile locals and
small-data accesses produced identical objects in five compiler configurations during the
initial source replacement.

**Register class and allocation dispatch.** `Registers_GetVarInfo` uses a cached 20-byte record for
data and absolute objects and reports missing local records. RC1 also returns null for function
objects. Both Windows 9.x hosts reproduce its 156 instruction bytes and its four-entry switch
table in RC1 (`0x590780`). The three-entry GC 3.0a5.2 version (`0x579790`) also matches with the
older data-record offset and diagnostic line.

`Registers_ClassForType` at `0x590530` and `assign_register_by_type` at `0x590A60` now run from C
in the relink. RC1 classifies eight-byte vector structures as GPR values and can assign a pair
for such a vector passed through the parameter area in a varargs function on processors `0x17`,
`0x1C`, and `0x26`.

Windows CodeWarrior 9.2 and 9.4 reproduce all 185 classifier bytes in RC1 and all 154 at
`0x579560` in GC 3.0a5.2, including 13 switch entries per version. The standalone C probe
matches with `-O3` or `-O4,p` and `-inline auto,deferred`. The stock float arm falls through
to the vector-structure check after its scalar and Gekko paired-single exits. That check reads
offset `0x10`, beyond the eight-byte `TypeIntegral` record. The byte at that position in the
stock paired-single data is zero in both executables. The relink's float arm exits after the
scalar and paired-single checks. It agrees with stock on the paired-single records on all
three tested processors, with and without soft float. A further 30,720 padded test records
per stock version confirmed that the fallthrough can change classification when the byte at
`0x10` is in the vector subtype range.

Both Windows 9.x compilers reproduce the relink C's full 788-byte RC1 dispatcher and all
eight switch destinations with `-O3 -inline auto,deferred`. It reads the register class
before and after the pair-selection checks and routes the pair cases through one assignment.
Reusing the integer comparison temporary for the parameter-area check reproduces the native
register choices. The GC probe matches 561 of 563 byte positions and all six switch destinations.
The two remaining GC bytes encode the registers in the final single-allocation call setup.
The rebuilt RC1 dispatcher passed all 798,720 end-to-end stock dispatch cases.

A direct comparison with stock RC1 covered 512 variable-info cases, 12,779,520 register-class
cases, and 798,720 assignment-dispatch cases. The dispatcher check intercepted allocation
calls to compare the selected worker and resulting register fields.

**Register assignment and reservation.** Both Windows 9.x hosts reproduced the full x86 bodies
below in RC1 and GC 3.0a5.2, including both switch tables in `assign_GPR_pair`. The relink now
builds all four from C.

| Routine | RC1 | GC 3.0a5.2 | Instruction bytes |
| --- | --- | --- | ---: |
| `obtain_nonvolatile_register` | `0x5B1380` | `0x594590` | 92 |
| `retain_register` | `0x5B15B0` | `0x594740` | 154 |
| `assign_register_to_variable` | `0x5B1650` | `0x5947E0` | 180 |
| `assign_GPR_pair` | `0x590820` | `0x579830` | 568 |

The pair allocator consumes two virtual registers in sequence when optimizing. In the physical
path it selects both nonvolatile registers before retaining either one. Reserving a free
register advances the nonvolatile cursor if the register is the next candidate, and reduces
the assignable count only while that count is positive.

The end-to-end dispatcher check ran these workers in all 798,720 cases. Separate controls covered
38,400 scans, reservations, and single assignments and 2,560 pair assignments. They compared
register fields, diagnostics, counters, and reservation tables. [Register checks](../gc2/tools/wii_retro/tests/README.md#register-classification-and-allocation)

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

1. The initial partition placed the callee's own named scalars ahead of pointers cloned from nested
   calls, preserving each group's relative order. It included materialized formals only if they
   were pointers to `void`, as in the vector placement-construction path. Moving typed formals
   breaks the exact `CGameHintInfo`, `CMapWorldInfo` and `CPlayerEnergyDrain` controls.
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
relocations masked. Across the 898 R3ME01 objects, the register-order relink changes 92 outputs
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

### Echoes integration follow-up

Disabling only the partition restored six register-only matches in the Echoes compiler patch
integration. All 72 disputed instructions agree with stock RC1 in each of the three shipped Wii
DOLs. The full R3ME01 sweep also found a lost exact match in the sort called by `CHintManager`.
[Echoes PR #1992](https://github.com/PrimeDecomp/echoes/pull/1992)

The assignment paths inlined a same-pointee pair of named iterators from one clone into a function
with a separate destruction counter. The earlier matching `CActor` and similar assignments had
one eligible cloned iterator. Range insertion had three source pointer locals of its own and
several scalars, while the older exact counted copies had two source iterators and one counter.
The partition now excludes same-pointee named pointers with the same first and last clone.
It also excludes non-formal nested pointers when the callee has multiple eligible named pointer
and scalar locals. The pointer-to-`void` formal path remains eligible, including placement
construction in `CSpatialPrimitive`.

The full comparison rebuilt every Wii RC1 unit from the PR's Ninja graph with the original
delta, with the partition disabled, and with the refined rule. The refined rule retained
every exact match from both controls under `functionRelocDiffs=none` and `data_value`.

| Wii build | Translation units | Compared functions | Exact before | Exact after |
| --- | ---: | ---: | ---: | ---: |
| R3ME01 | 997 | 22,273 | 12,056 | 12,064 |
| R32J01 | 321 | 8,628 | 6,195 | 6,200 |
| R3MP01 | 321 | 8,642 | 6,194 | 6,200 |

The earlier 898-unit R3ME01 sweep retained all 12,801 exact functions under file IPA. All 30
register-order controls also matched each shipped version in the original fixture. Several
already-inexact functions scored slightly lower in the PR sweep. All three rebuilt DOLs and
symbol files matched their expected hashes, and the remaining Wii outputs had unchanged
linker inputs and matching hashes.

## Phase 3: decompile both edge versions

Start with RC1's symbols and reuse gc2's 2.7 sources where the units still correspond.
Use `VERSION_` guards for GC 3.0a5.2 where the diff map shows a shared implementation. Check
function equivalence with `prove.py` and compare emitted PowerPC objects and relocated x86
bodies. Prioritize the backend and optimizer ahead of the driver, plugin, and build-agent layers.

## Phase 4: `Wii_Retro`

Keep the replacements on RC1's ABI in the relink and move recovered units into the shared
build as their versioned implementations mature. GC_2_0p2 is the precedent for a target
configured with `derived_from`. [Build and integration plan](wii-retro-compiler.md#plan)

[Trilogy-compiler-struct-copies.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-struct-copies.md
[Trilogy-compiler-destroy-pretest.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-destroy-pretest.md
