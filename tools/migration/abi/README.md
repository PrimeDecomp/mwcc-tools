# Hook ABI probes

C probes and harnesses that check the Rust hooks' view of the compiler's structures
([`mwcc-oracle-hooks`](../../../crates/mwcc-oracle-hooks/README.md)) against the compiler itself, for each version.

* `*_layout.c`: sizes, offsets, widths, alignments and enum values, compiled with each version's source compiler
  (frame, PCode/scheduler, blocks, scalarization/CSE, origins, instantiation, bindings, frontend, trace writer).
* `*_runtime.c`, `runtime.c`, `trace_caller.rs`: differential cases that run the C hook bodies and the Rust hooks on
  the same records and compare the trace output byte for byte.
* `local_call.rs`: the first single-hook spike (`lc.call`).

`verify_frontend.py`, `verify_trace_writer.py` and `verify_link.py` (frame, PCode, blocks, scalarization/CSE; it
needs a pinned checkout of the old Python tools) compile and run them; `--help` gives their inputs. The Rust side
needs the i686 target (`rustup target add i686-pc-windows-msvc`).
