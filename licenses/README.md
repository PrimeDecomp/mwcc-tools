# Bundled third-party notices

The native disassembly commands use the Rust Capstone bindings and their bundled x86 C decoder. These components retain their upstream licenses; the repository's CC0 dedication does not replace them.

Source: [capstone-rust/capstone-rs, commit 04f196369ee64f4ad1821a9453670e9a9ce9b25a](https://github.com/capstone-rust/capstone-rs/tree/04f196369ee64f4ad1821a9453670e9a9ce9b25a). The bundled decoder's `CS_VERSION_EXTRA` is 9 (Capstone 5.0.9). Cargo enables `std`, `full` and `arch_x86`, with other architecture features disabled. It compiles and statically links the C decoder; users of a released executable do not need a separate Capstone installation.

| Component | Upstream file | Retained notice |
|---|---|---|
| Rust bindings | `capstone-rs/LICENSE` | [capstone-rs-MIT.txt](capstone-rs-MIT.txt) |
| Low-level bindings | `capstone-sys/LICENSE` | [capstone-sys-MIT.txt](capstone-sys-MIT.txt) |
| C decoder | `capstone-sys/capstone/LICENSE.TXT` | [capstone-5.0.9-LICENSE.txt](capstone-5.0.9-LICENSE.txt) |
| Decoder's LLVM-derived components | `capstone-sys/capstone/LICENSE_LLVM.TXT` | [capstone-5.0.9-LICENSE_LLVM.txt](capstone-5.0.9-LICENSE_LLVM.txt) |

The locked dependency inventories also retain the actual notice files from the resolved Cargo graph and the pinned Rust standard library:

| Native target | Inventory | Verbatim notices |
|---|---|---|
| Windows x86-64 | [69 packages](dependencies/x86_64-pc-windows-msvc/inventory.json) | [Cargo notices](dependencies/x86_64-pc-windows-msvc/THIRD-PARTY-NOTICES.txt), [Rust library attribution](dependencies/x86_64-pc-windows-msvc/rust/COPYRIGHT-library.html) |
| Linux x86-64 | [72 packages](dependencies/x86_64-unknown-linux-gnu/inventory.json) | [Cargo notices](dependencies/x86_64-unknown-linux-gnu/THIRD-PARTY-NOTICES.txt), [Rust library attribution](dependencies/x86_64-unknown-linux-gnu/rust/COPYRIGHT-library.html) |

This is a conservative inventory including build dependencies, rather than a claim that every listed crate is linked into the CLI. It includes nested decoder notices, ring's BoringSSL/other-bits notices, Unicode terms, webpki root-certificate terms and the Rust library's accompanying license texts. The manifest records the Cargo.lock hash, exact source/package versions, individual notice hashes and hashes of the generated files. Notice contents are retained verbatim; Git text conversion is disabled for these generated artifacts. The 67 packages common to the two targets have identical notice inventories, and the Rust library notices agree across hosts.

Regenerate on each release host from the pinned toolchain and already available locked sources; add `--check` to validate without writing:

```text
python tools/migration/capture_notices.py --output licenses/dependencies/x86_64-pc-windows-msvc --check
python tools/migration/capture_notices.py --output licenses/dependencies/x86_64-unknown-linux-gnu --check
```

Run only the command matching that host. The collector uses offline, target-filtered Cargo metadata and requires the toolchain's installed library attribution/license files. It rejects missing notices and detects stale lockfiles, changed notice bytes and unexpected inventory files. Regenerate after dependency/toolchain changes. Native `package-release` includes the target's inventory and every inventoried file, checks their hashes against the lockfile and bundles the decoder notices separately. See the [distribution guide](../docs/oracle-rust-release.md).
