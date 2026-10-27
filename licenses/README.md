# Bundled third-party notices

The native disassembly commands use the Rust Capstone bindings and their bundled x86 C decoder. These components retain their upstream licenses; the repository's CC0 dedication does not replace them.

Source: [capstone-rust/capstone-rs, commit 04f196369ee64f4ad1821a9453670e9a9ce9b25a](https://github.com/capstone-rust/capstone-rs/tree/04f196369ee64f4ad1821a9453670e9a9ce9b25a). The bundled decoder's `CS_VERSION_EXTRA` is 9 (Capstone 5.0.9). Cargo enables `std`, `full` and `arch_x86`, with other architecture features disabled. It compiles and statically links the C decoder; users of a released executable do not need a separate Capstone installation.

| Component | Upstream file | Retained notice |
|---|---|---|
| Rust bindings | `capstone-rs/LICENSE` | [capstone-rs-MIT.txt](capstone-rs-MIT.txt) |
| Low-level bindings | `capstone-sys/LICENSE` | [capstone-sys-MIT.txt](capstone-sys-MIT.txt) |
| C decoder | `capstone-sys/capstone/LICENSE.TXT` | [capstone-5.0.9-LICENSE.txt](capstone-5.0.9-LICENSE.txt) |
| Decoder's LLVM-derived components | `capstone-sys/capstone/LICENSE_LLVM.TXT` | [capstone-5.0.9-LICENSE_LLVM.txt](capstone-5.0.9-LICENSE_LLVM.txt) |

`crates/mwcc-ninja` (the build engine of `mwcc-tools run ninja`) is a fork of n2, a ninja reimplementation:
[evmar/n2, commit b1fead52ccda0c497d816696f23f4099c3e8ec1f](https://github.com/evmar/n2/tree/b1fead52ccda0c497d816696f23f4099c3e8ec1f),
under the Apache License 2.0 ([n2-Apache-2.0.txt](n2-Apache-2.0.txt)); the crate keeps that licence. Its changes
from n2 are listed at the top of `crates/mwcc-ninja/src/lib.rs` and marked `mwcc-ninja` where they are; n2's
benchmarks, end-to-end tests, `main` and jemalloc allocator are left out.

Each release package also carries its target's dependency notices, collected on the runner that builds it
(`build/notices/TARGET`, stored in the archive under `licenses/dependencies/TARGET`):

```text
cargo fetch --locked --target TARGET
python tools/migration/capture_notices.py --target TARGET --output build/notices/TARGET
```

The collector reads offline, target-filtered Cargo metadata of the locked graph, including build dependencies (a
conservative inventory, not a claim that every listed crate is linked), and the pinned toolchain's Rust library
notices. It copies every crate's notice files verbatim. A crate that ships no licence file (objdiff-core,
cwdemangle and powerpc, for example) gets the standard text of each licence its `Cargo.toml` declares (MIT,
Apache-2.0, from the toolchain's SPDX copies, with the crate's authors as MIT's copyright holders), labelled as such
in `THIRD-PARTY-NOTICES.txt` and `inventory.json`; a licence with no standard text is an error. `inventory.json`
records the `Cargo.lock` hash, the package versions and every notice's hash, and `package-release` checks them. See
the [distribution guide](../docs/oracle-rust-release.md).

`LICENSE` and `Cargo.lock` are pinned to LF in `.gitattributes`. The collector
preserves their actual bytes: CRLF conversion changes both the workspace notice
block and its hash. After updating attributes in an existing Windows checkout,
refresh those files before regenerating inventories. The checkout regression
test compares notice output from real Git checkouts with `core.autocrlf` enabled
and disabled.
