//! A ninja build engine: a fork of n2 (<https://github.com/evmar/n2>, commit b1fead5, Apache-2.0), whose commands
//! a caller's [`Executor`] may run another way than as processes (mwcc-tools runs the Metrowerks compilers and
//! linkers in process, `mwcc-tools run ninja`).
//!
//! What the fork changes, besides the executor:
//! * CRLF reads as LF everywhere (n2's `crlf` feature, made tolerant of a lone CR): Python's configure scripts write
//!   build.ninja with CRLF on Windows, and mwcc writes its `.d` files so;
//! * a `.d` file's blanks may be tabs (mwcc starts each continued line with one);
//! * build.ninja is regenerated until it is up to date, as ninja does, not once;
//! * `-k` is ninja's: 1 by default, 0 for no limit; once failures stop the build the running tasks finish;
//! * [`run::run`] takes its arguments, rather than the process's;
//! * ninja's `-n` (a dry run, its lines as ninja's), `-t commands`, `-t inputs` and `-t targets all`; ninja's
//!   `no work to do.` and `build stopped` lines.
//!
//! The rest is n2's: `.n2_db` in place of ninja's `.ninja_log` and `.ninja_deps` (a build tree is not shared with
//! ninja: the first build after ninja's runs everything again, as does ninja's after this one), every `deps` discovered from the `depfile`, no
//! `restat` or `generator` special cases (a step's record is the hash of its command and its files' times).
#![allow(clippy::all)]

pub mod canon;
mod db;
mod densemap;
mod depfile;
mod eval;
mod graph;
mod hash;
pub mod load;
pub mod parse;
mod process;
#[cfg(unix)]
mod process_posix;
#[cfg(windows)]
mod process_win;
mod progress;
mod progress_dumb;
mod progress_fancy;
pub mod run;
pub mod scanner;
mod signal;
mod smallmap;
mod task;
mod terminal;
mod trace;
mod work;

pub use process::Termination;
pub use run::run;
pub use signal::was_interrupted;
pub use task::Executor;

/// CMDLINE run as a process, as a build's command runs without an executor (the platform's shell elsewhere than on
/// Windows; `CreateProcess` on Windows), its console output given to OUTPUT.
pub fn run_command(cmdline: &str, output: impl FnMut(&[u8])) -> anyhow::Result<Termination> {
    process::run_command(cmdline, output)
}
