//! A unit's object compared with its target, at three levels:
//! - [`identical`]: the bytes are equal;
//! - [`score`]: objdiff-core's scores and rows, the kit's statistics and residual-scan's verdicts, with mwdec's
//!   [`profile`] of what differs;
//! - [`strict`]: mwdec's exact rules.
pub mod profile;
pub mod score;
pub mod stores;
pub mod strict;
pub mod words;

pub use profile::Profile;
pub use score::{FunctionScore, Residual, ScoreOptions, Stats, UnitScore, Verdict, score};

/// Whether the two objects' bytes are equal.
pub fn identical(a: &[u8], b: &[u8]) -> bool {
    a == b
}
