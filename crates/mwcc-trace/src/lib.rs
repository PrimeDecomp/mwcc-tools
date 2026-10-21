//! The oracle's traces: what a compile traces (`options`), their records (`trace`, and mwdec's model of them,
//! `records`), the compiler versions they come from (`version`), and the models replayed on them: colouring,
//! allocation, scheduling (`schedule`, and the list scheduler's pick rules, `sched`), inlining and locals.
//! mwcc-oracle-core re-exports most modules under their own names (not `options`, `records` or `sched`), and
//! `ppc`'s functions from its own `ppc` module.
pub mod allocation;
pub mod colouring;
pub mod evidence;
pub mod inlining;
pub mod locals;
mod m750;
mod machine;
pub mod options;
pub mod ppc;
pub mod python_random;
pub mod records;
pub mod sched;
pub mod schedule;
pub mod sequence;
pub mod trace;
pub mod version;
