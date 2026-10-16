# Native oracle kit boundary

The documented kit API is the native `mwcc-tools` executable and its structured results. See the installable [native oracle guide](overlay/.agents/docs/oracle-native.md). Kit callers and tests should use this API directly. Matching legacy Python APIs or report prose is not required. The optional launchers preserve old script names without importing the Python oracle:

```text
mwcc-tools oracle run-unit --project PROJECT --unit UNIT -- --locals
mwcc-tools oracle regdiff --project PROJECT --unit UNIT --function FUNCTION --pcode
mwcc-tools colouring unit --project PROJECT --unit UNIT search --function FUNCTION --seed 2
mwcc-tools inline-calibrate --project PROJECT --callee destroy_impl --unit UNIT --breakdown
```

Set `MWCC_TOOLS` to the native executable and `MWCC_ORACLE_ROOT` to the built compiler checkout. Without `MWCC_TOOLS`, the launchers look under the oracle root's `bin`, `target/release`, `build/bin`, then PATH. An explicit missing executable or a native command failure is an error; there is no silent Python fallback.

`oracle-run.py` now checks object equality by default. Use `--fast` before the separator, or `--no-check` after it, for an explicitly unchecked run. Candidate compilation retains the unit's flags and source-directory include path without swapping project files. `oracle-regdiff.py` retains `functionRelocDiffs=data_value` and supports the `OBJDIFF_CLI` override. `--json` returns a format-1 register-difference document with structured allocation evidence and per-register explanations. Offline regdiff accepts `--trace`, `--diff-json` and an explicit compiler version.

Saved colouring traces still support legacy `.trace`/`.o` pairs. Unit dispatch supplies the unit's compiler version when loading them; an explicit version takes precedence and conflicting new manifest metadata is rejected.

`inline-calibrate.py` dispatches to native calibration, including `--binary`, `--breakdown`, repeatable units/overrides and JSON-file output. Compiler versions are inferred per unit unless `--compiler` overrides them; the game build defaults to the Ninja target directory rather than a hardcoded game ID. Thresholds remain per decision. Missing cost data produces an explicit error instead of inventing a threshold (the legacy helper can raise `KeyError` for these inputs).

The launchers are prepared locally. Archive regeneration and pin updates remain required before adoption. Existing archives have not been modified.

`tools/migration/verify_archived_kits.py` validates each archive's sidecar, CRC and complete file manifest, then builds its original oracle pin in a private directory. It copies the real unit's sources, headers, include dependencies, target/source objects and build metadata. It runs the archived scripts and native launchers against the same compiler, comparing source/candidate objects, schema-6 reports, full regdiff/PCode/inlining reports and both calibration modes. Candidates add an emitted function so the gate detects an ignored candidate. Separate checked runs compare the complete Rust producer with the frozen C producer on both inputs. Echoes additionally saves a legacy colouring trace with its original wrapper and loads it through the native launcher.

The selected units are Prime's `main/GuiSys/CGuiFrame` (`Draw`, original oracle pin `f00b8a8201d0dca13c2206fce61472b02d83b01e`) and Echoes' `main/Kyoto/Particles/CElementGen` (`RenderParticles`, pin `b75c4e5e0b4bc97ae06c26ea9d6aad8379ac1722`). A pre-existing stale game object remains stale; both implementations must report that state consistently, while checked compilation independently verifies equality with the stock compiler. The gate hashes original inputs and Git status before and after execution, without configuring or building either game checkout.

On Linux, the staged Ninja file translates Windows separators only in compiler-edge paths and `mw_version` fields, because the archived Python parser cannot read Windows source paths there. Compiler flags and every other copied input remain unchanged. Original/adjusted Ninja hashes are recorded. A Windows-managed Git worktree can supply exported source with repeatable `--pin-archive COMMIT=TAR`; the verifier checks the Git tar's embedded commit and retains the tar/hash. Supply an explicit pinned `--wibo` on Linux. `audit_archived_kits.py ROOT --binary FILE --objdiff FILE` independently rechecks retained inputs, command hashes and comparisons without recompiling.

These archived probes establish regression evidence; matching legacy APIs/text is not an adoption requirement. The old Linux Python calibration still fails on Echoes' continued Windows source edge. `verify_native_kit_api.py` validates the direct native API against audited behavioral results, including both calibration modes, legacy saved input and explicit compilation/check/provenance output; `audit_native_kit_api.py` rechecks the retained evidence and unchanged inputs. This avoids extending the old parser to make it a production dependency.

Kit adoption must also update tests that import removed Python helper internals: Prime's `InlineCalibrateTests` calls `binary_mode`, and Echoes' equivalent calibration test plus `OracleColouringTests` call `binary_mode`/`build_command`. Their behavioral contracts are covered by native fixture/CLI tests and the live gates; leaving the original import-based tests in a migrated kit would be incorrect. Update the kit source, test callers, bundle file list, external/native release manifests, documentation and new archive sidecars together once a native release pin is available.
