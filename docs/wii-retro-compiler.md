# Plan: the compiler of the Prime games' Wii builds

**Goal.** Retro's code in the Wii builds of the Prime games was built by a compiler we do not have. It sits between GC
3.0a5.2 (2006-08-31) and Wii 1.0RC1 (2008-05-28). The plan is to decompile both edge versions, then build a new compiler
from parts of each that behaves the way those builds show. Success is that Echoes Wii's sample functions below match
with it, with no losses among the functions RC1 already matches.

The new version is called `Wii_Retro` below. The name is a placeholder.

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

1. Add `config/Wii_Retro/config.json` (`derived_from: Wii_1_0RC1`, a description, its behaviour switches), with the
   changes as `VERSION_` code in RC1's sources, as GC_2_0p2 does.
2. **Before the decompilation is complete,** build `Wii_Retro` as RC1's split executable relinked. Only the units
   Phase 2 changes are replaced, compiled from the decompiled sources, which need only be equivalent. Its data layouts
   must agree with RC1's (options, `copts`, IR node structs). Building the whole compiler from the sources replaces
   the relink once Phase 3 is done.
3. **Make it usable in mwcc-tools.** It has no original, so it cannot be recognised by hash the way other compilers
   are. Decide how a project names it, then add the WebAssembly module and the oracle hooks (wasm.md), so it builds
   in process and traces.
4. **Echoes:** switch `retro_mw_version` for R32J01, R3ME01 and R3MP01, then check the suite, the no-loss set, and
   all 447 image hashes.

## Decision points

- **After Phase 2's patch:** whether RC1's legacy copy path is separable from its stack layout. If it is, the copy
  rule is a small, local change. If not, the decompilation of the struct move and frame layout is the critical path.
- **The destroy pretest:** a GC 3.x behaviour carried forward, or one belonging to neither edge version.
- **Host compiler found or not:** whether Phase 3 aims for matching or for equivalence.

[Trilogy-compiler-struct-copies.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-struct-copies.md
[Trilogy-compiler-destroy-pretest.md]: https://collab.decomp.dev/projects/echoes/docs/research/Trilogy-compiler-destroy-pretest.md
