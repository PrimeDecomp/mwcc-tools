# Echoes with the patched Wii compiler: notes for the game-side work

These notes are for the session that moves Echoes' Wii builds (R3ME01, R3MP01, R32J01) to the patched RC1 compiler.
The findings behind the patch are in the [compiler research notes](wii-retro-compiler-research.md#results-2026-11-14).

The compiler now builds as a native relink under `build/Wii_Retro/`. Copy `mwcceppc.exe` and `lmgr8c.dll`
together into the project's compiler directory, or build directly there with `wii_retro_link.py --out-dir`.
See the [compiler build instructions](wii-retro-compiler.md#build-wii_retro) and
[validation results](wii-retro-compiler-research.md#native-relink-and-validation).

## The patch

`gc2/tools/wii_patch_struct_copy.py <rc1_dir> <out_dir>` copies Wii 1.0RC1's directory and patches `mwcceppc.exe`
in 8 places (it checks each place's bytes and refuses anything else). Output SHA-1:
`fbb0c00006afad9e60a82a020caba0078d4d4a5b`.

The patched compiler copies structs the way GC 3.x does (an operand's alignment is its declared alignment, so a
4-byte struct of alignment 1 is copied byte by byte). It lays out the stack the way RC1 does. Nothing else changes.

It is a stand-in. The compiler the Wii builds really used (`Wii_Retro`) will be built from source in mwcc-tools later,
and more behaviours may be found: the destroy pretest is the next candidate. Echoes should name the compiler so it
can be swapped later without touching every object.

## Measured on R3ME01 (Echoes `origin/main` 8deb345f5)

| Build | Exact functions (of 18,016) |
|---|---|
| Stock RC1 | 12,866 |
| Patched RC1 | 12,892: 32 gained, 6 lost |
| Patched RC1 + the `CModelFlags` change below | 12,898: 32 gained, 0 lost |

The stock compiler with the `CModelFlags` change alone is unchanged (12,866, the same functions).

The suite functions that become exact: `CTevCombiners::SetTevRegisterColor`, `CGraphics::SetCopyClear`,
`CGX::SetChanMatColor`, `CGX::SetChanAmbColor`, `CCubeRenderer::SetupCGraphicsStates`. Units that improve most:
CHintManager 70.3 → 96.7%, CStateMachine 82.4 → 99.7%, CRasterFont 82.3 → 97.3%, CPakFile 76.9 → 88.2%.

## The work

1. **Ship the compiler.** Echoes downloads its compilers as one archive (`config.compilers_tag`), which has only
   the stock RC1. The patched one has to be produced at build time from it. The likely shape: a small step in
   Echoes' build that runs the patch on `build/compilers/Wii/1.0RC1` into its own directory (for example
   `Wii/1.0RC1_retro`), checks the SHA-1, and is used as `retro_mw_version` for the Wii versions. Whether Echoes
   vendors the patch script or fetches it from mwcc-tools is open: agree on it with the mwcc-tools session.
2. **`CModelFlags`** (`include/Kyoto/Graphics/CModelFlags.hpp` in PrimeDecomp/rstl, the non-PRIME1 branch): its
   first 4 bytes, `uchar x0_[4]`, are a field whose type is unknown. The target copies them as one word. Declaring
   them `union { uchar x0_[4]; uint x0w_; };` fixes the 6 lost functions (`CScriptColorModulate::SetTargetFlags`,
   `CScriptDoor::SetShieldAlpha`, `CPortalTransition::Update`, `CCinematicCamera::Think`,
   `CGrenchlerTail::PreRender`, `CPlayer::PreRender`). A 4-aligned 4-byte struct works too. A plain `uint`/`int`
   breaks `CPortalTransition::Update`'s stack layout (under stock RC1 too), and `float` or `aligned(4)` break
   others. rstl is shared with the other games, so check that Prime and Corruption are unaffected.
3. **Take the gains.** Rebuild R3ME01, R3MP01 and R32J01 and compare reports against stock. Mark the units that
   now match. Look into the functions that still have byte-copy differences: a remaining one whose source is
   trusted is evidence for mwcc-tools (see below).
4. **The destroy pretest** (Echoes research page `Trilogy-compiler-destroy-pretest.md`): GC 3.0a5.2, 3.0a5, 3.0a3
   and RC1 all keep `if (begin != end)` in `rstl::destroy` when the loop body calls, and the target drops it.
   Before treating it as a compiler behaviour, rule out the source: rstl has an unguarded range-destruction policy
   (`RSTL_DECLARE_UNGUARDED_RANGE_DESTRUCTION`). Report what you find.

## Coordination

The mwcc-tools session that made the patch is
`local_6a52faa3-dff6-4007-8644-a78215e40e4b` ("Wii Prime compiler hybrid version"). Send it a message when:

- a function still differs in a way that looks like the compiler, not the source (give the function, the version,
  and the diff);
- the patch needs to change (another site, a different rule for some operands), or the way Echoes consumes it
  needs something from mwcc-tools (a release, a different script interface);
- the destroy pretest turns out to be a source issue, or definitely not one.

The probe used for the numbers above is `F:\programming\claude-worktrees\echoes\wii-retro-probe` (detached, configured
for R3ME01 with `--compilers ..\wii-retro-probe-compilers\patched`), with its reports in
`..\wii-retro-probe-reports`. It is throwaway: use a fresh worktree from `origin/main`.
