mwcc-tools
==========

[![Build Status]][actions]

[Build Status]: https://github.com/PrimeDecomp/mwcc-tools/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/PrimeDecomp/mwcc-tools/actions/workflows/build.yml

Tools for matching code compiled by `mwcceppc.exe`, the CodeWarrior compiler for GameCube. They build decomp
projects faster by running the compilers in process, explain register assignment, inlining, scheduling and stack
layout from a trace of the compiler (the oracle), and search, reduce and compare candidate sources against it.

Installation
============

On Windows (PowerShell):

```powershell
powershell -ExecutionPolicy ByPass -c "irm https://github.com/PrimeDecomp/mwcc-tools/releases/latest/download/install.ps1 | iex"
```

On Linux x64 and macOS Apple Silicon:

```sh
curl -LsSf https://github.com/PrimeDecomp/mwcc-tools/releases/latest/download/install.sh | sh
```

This installs the latest release into `%LOCALAPPDATA%\mwcc-tools` (Windows) or `~/.local/share/mwcc-tools`
(Linux, macOS) and gives you:

- `mwcc-tools` and `mw-ninja` on your `PATH` (`mw-ninja ARGS` is `mwcc-tools run ninja ARGS`): on Windows, the
  installation's `bin` is added to your user `PATH`; elsewhere, they are linked into `~/.local/bin`, which is added
  to your shell profiles if your `PATH` lacks it. Open a new terminal to use them;
- the Python package `mwcc_tools`, importable in your Python 3.11 or later (through a `mwcc-tools.pth` in its user
  site-packages). It runs the `mwcc-tools` installed beside it, so the two always match.

To update:

```sh
mwcc-tools self update
```

`mwcc-tools self update --check` only says whether there is a newer release, and `--version VERSION` installs a
given one. Running the installer again also updates.

The installers read these environment variables: `MWCC_TOOLS_HOME` (the installation directory),
`XDG_DATA_HOME` and `XDG_BIN_HOME` (`install.sh`: the parent of the installation directory and the directory of the
links), `MWCC_TOOLS_VERSION` (a release instead of the latest), `MWCC_TOOLS_NO_MODIFY_PATH=1` (leave the `PATH` and
shell profiles alone) and `MWCC_TOOLS_NO_PYTHON=1` (skip the `.pth`). To uninstall, delete the installation
directory and the `mwcc-tools.pth` files, then on Windows remove `%LOCALAPPDATA%\mwcc-tools\bin` from your user
`PATH`; elsewhere delete the `mwcc-tools` and `mw-ninja` links in `~/.local/bin` (and the
`. "~/.local/share/mwcc-tools/env"` line from your shell profiles, where the installer added one).

<details>
<summary>Manual installation</summary>

Download the package for your system from the [latest release](https://github.com/PrimeDecomp/mwcc-tools/releases/latest),
unzip it, and put its `bin/` directory on your `PATH`:

| System | Package |
|---|---|
| Windows x64 | `mwcc-tools-VERSION-x86_64-pc-windows-msvc.zip` |
| Linux x64 | `mwcc-tools-VERSION-x86_64-unknown-linux-gnu.zip` |
| macOS Apple Silicon | `mwcc-tools-VERSION-aarch64-apple-darwin.zip` |

Its `python/` directory is the Python package. `mwcc-tools self update` works in an unzipped package too.

</details>

To build from source instead, see [BUILDING.md](BUILDING.md).

Compilers
---------

`mwcc-tools` downloads the WebAssembly build of each compiler (GC 1.2.5n, 1.3.2, 2.6 and 2.7) the first time a
build or trace needs it. Your project still needs its own original compilers: they are recognised by their hash.

To download them all ahead of time (for example, before going offline):

```sh
mwcc-tools download wasm
```

To never download, set `MWCC_WASM_DOWNLOAD=0`: builds then run the original compilers as ninja would, and tracing
needs a checkout with the modules ([BUILDING.md](BUILDING.md)).

Only the download cache is downloaded into. With `MWCC_ORACLE_ROOT` set to a source checkout, the methods use that
checkout's modules alone: a version it has not built or unpacked fails ("GC_1_3_2's oracle is not built"). Unset
`MWCC_ORACLE_ROOT` to use the downloads, or unpack that version's archive into the checkout
([BUILDING.md](BUILDING.md#the-tools)).

Building a project
==================

`mwcc-tools run ninja` (or `mw-ninja`) builds a [dtk-template](https://github.com/encounter/dtk-template) project in place of
ninja, running the compilers and linkers in process. Configure the project as usual, then:

```sh
python configure.py
mwcc-tools run ninja
```

It takes ninja's usual options (`-j N`, `-k N`, `-n`, `-v`, targets...). `mwcc-tools run COMPILER ARGS...` runs a
single compiler or linker command the same way. [wasm.md](docs/wasm.md#a-projects-build-mwcc-tools-run) has the details.

Using the oracle
================

The oracle's tools are methods: JSON parameters in, JSON results out. `mwcc-tools call METHOD KEY=VALUE ...` runs
one (a VALUE that is JSON is read as JSON), `mwcc-tools serve` keeps a session over JSON lines, and the Python
package `mwcc_tools` drives that session.

```sh
mwcc-tools call oracle.trace project=../project unit=main/Foo function=Draw views.text=true
mwcc-tools call oracle.regdiff project=../project unit=main/Foo function=Draw text=true full=true
```

`mwcc-tools methods` lists every method, and the [oracle guide](docs/oracle.md) says which to use when.

The Python package (standard library only) comes with the installation (above). Inside a virtual environment,
which does not see the user site-packages, install the installation's copy editable, so it follows
`mwcc-tools self update`:

```sh
pip install -e ~/.local/share/mwcc-tools/python
```

(`$env:LOCALAPPDATA\mwcc-tools\python` on Windows.)

It can also be installed from a release's tag. It then runs `mwcc-tools` from the `PATH` (or `MWCC_TOOLS`), which
should be the same version:

```sh
pip install "git+https://github.com/PrimeDecomp/mwcc-tools@v0.4.5#subdirectory=python"
```

```python
from mwcc_tools import Engine
with Engine(project='../project') as engine:
    trace = engine.call_one('oracle.trace', unit='main/Foo', function='Draw', views={'text': True})
    print('
'.join(trace['text']))
```

[python/README.md](python/README.md) describes it.

Documentation
=============

| Documentation | Covers |
|---|---|
| [Building](BUILDING.md) | Building the tools, the WebAssembly modules and the native oracle. |
| [Oracle guide](docs/oracle.md) | Tool selection and everyday commands. |
| [Methods](docs/api.md) | Every method's parameters and results. |
| [WebAssembly builds](docs/wasm.md) | The modules in detail: how they are built and run, the probes in the compiler's sources. |
| [Native kit workflow](integrations/decomp-kits/overlay/.agents/docs/oracle-native.md) | Project units, candidates, register differences and structured checks. |
| [Project scan](docs/scan.md), [other versions](docs/versions.md) | Instruction patterns over a whole project; compiling a unit with another of its compilers; GC 1.3.2's oracle. |
| [Trace reference](docs/oracle/trace.md) | Raw records and versioned JSON. |
| [Provenance](docs/oracle/provenance.md), [source search](docs/oracle/source-search.md) | Value origins, source search, reduction and the matched-source index. |
| [Decompilation](docs/decompilation.md) | The compilers' sources and their other builds. |

Third-party notices are under [licenses](licenses/README.md).

License
=======

The project's own files are dedicated to the public domain under CC0 (`LICENSE`); third-party components retain their
upstream licenses. The project started from
[inspiredrobot/mwcc](https://github.com/inspiredrobot/mwcc), also CC0. `gc2/` is mweppc, NWPlayer123's
decompilation of the GC 1.3.2 to 2.7 compilers, CC0 (`gc2/LICENSE`). `wasm/libc` adapts parts of the MSL of
[PrimeDecomp/echoes](https://github.com/PrimeDecomp/echoes), the Metroid Prime 2: Echoes decompilation, also CC0.
