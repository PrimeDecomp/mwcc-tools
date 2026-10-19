//! sinit-profile's block profile of a function (a static-table initializer, such as `__sinit_ScriptLoader_cpp`,
//! that is a long run of stores): its instruction count, the `stmw` register, the frame size, the `lwz` and `mr`
//! emitted, and the lengths of its runs of at least 8 stores. A variant can match instruction for instruction and
//! still differ in these, because the compiler's block structure changed.
use crate::words::{self, Code};
use anyhow::Result;
use serde::Serialize;

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct StoreProfile {
    /// The function profiled (the first, by name, that starts with the prefix).
    pub function: String,
    pub instructions: usize,
    /// The first `stmw`'s register, and the first `stwu r1`'s offset (the frame, negated).
    pub stmw: Option<u8>,
    pub frame: Option<i16>,
    /// The `lwz` and `mr` (`or rA, rS, rS`).
    pub reloads: usize,
    /// The runs of at least 8 `stw`/`stwu`, in order.
    pub runs: Vec<usize>,
}

impl StoreProfile {
    /// sinit-profile's line: `N ins stmw rS frame -0xF lwz/mr=R runs=K [a, b]`.
    pub fn line(&self) -> String {
        let stmw = self.stmw.map_or("?".into(), |r| format!("r{r},"));
        let frame = self.frame.map_or("?".into(), |d| {
            if d < 0 {
                format!("-0x{:x}(r1)", -i32::from(d))
            } else {
                format!("0x{d:x}(r1)")
            }
        });
        let runs: Vec<String> = self.runs.iter().map(usize::to_string).collect();
        format!(
            "{} ins stmw {stmw} frame {frame} lwz/mr={} runs={} [{}]",
            self.instructions,
            self.reloads,
            self.runs.len(),
            runs.join(", ")
        )
    }
}

/// The profile of CODE, the function NAME.
pub fn profile(name: &str, code: &Code) -> StoreProfile {
    let (mut stmw, mut frame, mut reloads) = (None, None, 0);
    let (mut runs, mut count) = (vec![], 0);
    for &word in code.words.iter().chain([&0]) {
        let op = word >> 26;
        let (s, a, b) = ((word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31);
        let store = matches!(op, 36 | 37);
        if store {
            count += 1;
        } else {
            if count >= 8 {
                runs.push(count);
            }
            count = 0;
        }
        if op == 37 && s == 1 && a == 1 && frame.is_none() {
            frame = Some(word as u16 as i16);
        }
        if op == 47 && stmw.is_none() {
            stmw = Some(s as u8);
        }
        // (lwz; `or rA, rS, rS` without a record bit, which dtk writes `mr`)
        if op == 32 || (op == 31 && (word >> 1) & 0x3ff == 444 && s == b && word & 1 == 0) {
            reloads += 1;
        }
    }
    StoreProfile {
        function: name.to_owned(),
        instructions: code.words.len(),
        stmw,
        frame,
        reloads,
        runs,
    }
}

/// The profile of OBJECT's first function (by name) whose name starts with PREFIX, if it has one.
pub fn profile_object(object: &[u8], prefix: &str) -> Result<Option<StoreProfile>> {
    let functions = words::functions(object)?;
    let mut names: Vec<&String> = functions.keys().filter(|n| n.starts_with(prefix)).collect();
    names.sort();
    Ok(names.first().map(|name| profile(name, &functions[*name])))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stores_frames_and_reloads_are_profiled() {
        let stwu = 0x9421_ffc0; // stwu r1, -0x40(r1)
        let stmw = 0xbf61_002c; // stmw r27, 0x2c(r1)
        let stw = 0x9003_0000; // stw r0, 0(r3)
        let lwz = 0x8003_0000; // lwz r0, 0(r3)
        let mr = 0x7c7f_1b78; // mr r31, r3
        let mut words = vec![stwu, stmw, lwz, mr];
        words.extend([stw; 9]);
        words.push(lwz);
        words.extend([stw; 3]);
        let code = Code {
            words,
            relocations: vec![],
        };
        let found = profile("__sinit_x", &code);
        assert_eq!(found.instructions, 17);
        assert_eq!(found.stmw, Some(27));
        assert_eq!(found.frame, Some(-0x40));
        assert_eq!(found.reloads, 3);
        // (the stwu begins a run that the stmw ends)
        assert_eq!(found.runs, [9]);
        assert_eq!(
            found.line(),
            "17 ins stmw r27, frame -0x40(r1) lwz/mr=3 runs=1 [9]"
        );
    }
}
