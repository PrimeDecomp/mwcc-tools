//! mwdec's profile of what differs between a target function and ours (mwdec-search's `score.rs`, CC0): an
//! opcode-level alignment of the masked words, each aligned pair classified (register, stack, branch, other), the
//! unaligned ones as reorderings, substitutions, insertions and deletions, and the residue categories of those; and
//! its penalty, which ranks non-exact candidates.
use crate::words::Code;
use serde::Serialize;
use std::cmp::Ordering;

pub const PEN_REG: u64 = 5;
pub const PEN_STACK: u64 = 1;
pub const PEN_BRANCH: u64 = 1;
pub const PEN_OTHER: u64 = 5;
pub const PEN_REORDER: u64 = 60;
pub const PEN_INSDEL: u64 = 100;
/// A deleted and an inserted instruction in the same alignment gap (an instruction changed into another opcode, e.g.
/// `cmpwi` -> `cmplwi`).
pub const PEN_SUBST: u64 = 100;
pub const PEN_RELOC: u64 = 30;

#[derive(Clone, Copy, Debug, Default, Serialize, PartialEq, Eq)]
pub struct Profile {
    /// Aligned instructions with the same opcode differing only in registers.
    pub reg: u32,
    /// Same opcode, different r1-relative displacement.
    pub stack: u32,
    /// Branch displacement / condition differences.
    pub branch: u32,
    /// Same opcode, other immediate differences.
    pub other: u32,
    /// Instructions present in both but at different places.
    pub reorder: u32,
    /// Instructions replaced by a different opcode at the same place.
    pub substituted: u32,
    pub inserted: u32,
    pub deleted: u32,
    /// The masked code is identical but the function does not match (relocations or literals differ).
    pub reloc: u32,
    /// Categories of inserted, deleted and substituted instructions ([`res`] bits).
    pub residue: u32,
    /// mwdec's permuter-style penalty (0 when it matches).
    pub penalty: u64,
}

impl Profile {
    pub fn structural(&self) -> u32 {
        self.inserted + self.deleted + self.reorder + self.substituted
    }
}

/// The profile of OURS against TARGET; MATCHED: the function matches (its penalty is 0).
pub fn profile(target: &Code, ours: &Code, matched: bool) -> Profile {
    let (penalty, mut profile) = penalty(&target.masked(), &ours.masked());
    profile.penalty = penalty;
    if !matched && penalty == 0 {
        // (the score knows only that there is a difference: [`strict_profile`] counts the strict level's classes)
        profile.reloc = 1;
        profile.penalty = PEN_RELOC;
    }
    if matched {
        profile.penalty = 0;
    }
    profile
}

/// The profile of OURS against TARGET with its STRICT comparison: as mwdec's, `reloc` counts the strict level's
/// difference classes where the masked code is the same.
pub fn strict_profile(target: &Code, ours: &Code, strict: &crate::strict::Strict) -> Profile {
    let (penalty, mut profile) = penalty(&target.masked(), &ours.masked());
    profile.penalty = penalty;
    if !strict.exact && penalty == 0 {
        profile.reloc = strict.classes.len().max(1) as u32;
        profile.penalty = PEN_RELOC * u64::from(profile.reloc);
    }
    if strict.exact {
        profile.penalty = 0;
    }
    profile
}

/// Opcode identity: primary opcode plus extended opcode where it exists, plus Rc.
fn opkey(w: u32) -> u32 {
    let p = w >> 26;
    match p {
        19 | 31 | 63 => (p << 16) | ((w >> 1) & 0x3ff) << 1 | (w & 1),
        59 => (p << 16) | ((w >> 1) & 0x1f) << 1 | (w & 1),
        4 => (p << 16) | ((w >> 1) & 0x3ff),
        _ => p << 16,
    }
}

fn is_dform_mem(p: u32) -> bool {
    (32..=55).contains(&p) || p == 14 || p == 15 || p == 56 || p == 57 || p == 60 || p == 61
}

fn classify_pair(a: u32, b: u32, prof: &mut Profile) -> u64 {
    if a == b {
        return 0;
    }
    let p = a >> 26;
    let x = a ^ b;
    if p == 16 || p == 18 {
        prof.branch += 1;
        return PEN_BRANCH;
    }
    if is_dform_mem(p) && x & 0xffff != 0 {
        let ra = (a >> 16) & 31;
        if ra == 1 && (b >> 16) & 31 == 1 {
            prof.stack += 1;
            return PEN_STACK;
        }
        prof.other += 1;
        return PEN_OTHER;
    }
    let imm_differs = match p {
        7 | 8 | 10 | 11 | 12 | 13 | 24..=29 => x & 0xffff != 0,
        // rlwimi/rlwinm/rlwnm: SH/MB/ME fields
        20 | 21 | 23 => x & 0xfffe != 0,
        _ => false,
    };
    if imm_differs {
        prof.other += 1;
        return PEN_OTHER;
    }
    prof.reg += 1;
    PEN_REG
}

/// Opcode-level LCS alignment; returns matched index pairs.
fn align(a: &[u32], b: &[u32]) -> Vec<(usize, usize)> {
    let (n, m) = (a.len(), b.len());
    if n == 0 || m == 0 {
        return vec![];
    }
    if n.saturating_mul(m) > 12_000_000 {
        // Positional fallback for huge functions.
        return (0..n.min(m))
            .filter(|&i| a[i] == b[i])
            .map(|i| (i, i))
            .collect();
    }
    let w = m + 1;
    let mut dp = vec![0u16; (n + 1) * w];
    for i in (0..n).rev() {
        for j in (0..m).rev() {
            dp[i * w + j] = if a[i] == b[j] {
                dp[(i + 1) * w + j + 1] + 1
            } else {
                dp[(i + 1) * w + j].max(dp[i * w + j + 1])
            };
        }
    }
    let mut out = Vec::new();
    let (mut i, mut j) = (0, 0);
    while i < n && j < m {
        if a[i] == b[j] {
            out.push((i, j));
            i += 1;
            j += 1;
        } else if dp[(i + 1) * w + j] >= dp[i * w + j + 1] {
            i += 1;
        } else {
            j += 1;
        }
    }
    out
}

/// Opcode-level alignment of target words `t` and ours `o` (matched index pairs).
pub fn align_pairs(t: &[u32], o: &[u32]) -> Vec<(usize, usize)> {
    let tk: Vec<u32> = t.iter().map(|&w| opkey(w)).collect();
    let ok: Vec<u32> = o.iter().map(|&w| opkey(w)).collect();
    align(&tk, &ok)
}

/// Penalty and profile for target words `t` vs ours `o` (masked).
pub fn penalty(t: &[u32], o: &[u32]) -> (u64, Profile) {
    let mut prof = Profile::default();
    let tk: Vec<u32> = t.iter().map(|&w| opkey(w)).collect();
    let ok: Vec<u32> = o.iter().map(|&w| opkey(w)).collect();
    let pairs = align(&tk, &ok);
    let mut pen = 0;
    let mut tm = vec![false; t.len()];
    let mut om = vec![false; o.len()];
    for &(i, j) in &pairs {
        tm[i] = true;
        om[j] = true;
        pen += classify_pair(t[i], o[j], &mut prof);
    }
    // Unmatched instructions per alignment gap: pairs of different opcodes in the same gap are
    // substitutions; the rest are paired by opcode across gaps as reorderings, else ins/del.
    let mut del: Vec<u32> = Vec::new();
    let mut ins: Vec<u32> = Vec::new();
    let mut bounds: Vec<(usize, usize)> = pairs.clone();
    bounds.push((t.len(), o.len()));
    let (mut pi, mut pj) = (0usize, 0usize);
    for &(bi, bj) in &bounds {
        let gd: Vec<u32> = (pi..bi).map(|i| tk[i]).collect();
        let gi: Vec<u32> = (pj..bj).map(|j| ok[j]).collect();
        let k = gd.len().min(gi.len());
        // Same opcode in one gap would have been aligned unless order differs: keep those for
        // reorder pairing; substitute the rest pairwise.
        let mut gd_rest = Vec::new();
        let mut gi_rest = gi.clone();
        let mut subst = 0;
        for &d in &gd {
            if let Some(pos) = gi_rest.iter().position(|&x| x == d) {
                // equal opcode within the gap: a local reordering
                gi_rest.remove(pos);
                prof.reorder += 1;
                pen += PEN_REORDER;
            } else {
                gd_rest.push(d);
            }
        }
        let k2 = gd_rest.len().min(gi_rest.len());
        let _ = k;
        subst += k2;
        prof.substituted += k2 as u32;
        pen += k2 as u64 * PEN_SUBST;
        del.extend(gd_rest.into_iter().skip(k2));
        ins.extend(gi_rest.into_iter().skip(k2));
        let _ = subst;
        pi = bi + 1;
        pj = bj + 1;
    }
    del.sort_unstable();
    ins.sort_unstable();
    let (mut i, mut j, mut r) = (0, 0, 0u32);
    while i < del.len() && j < ins.len() {
        match del[i].cmp(&ins[j]) {
            Ordering::Equal => {
                r += 1;
                i += 1;
                j += 1;
            }
            Ordering::Less => i += 1,
            Ordering::Greater => j += 1,
        }
    }
    prof.reorder += r;
    prof.deleted = del.len() as u32 - r;
    prof.inserted = ins.len() as u32 - r;
    prof.residue = residue(t, o, &pairs);
    pen += r as u64 * PEN_REORDER + (prof.deleted + prof.inserted) as u64 * PEN_INSDEL;
    (pen, prof)
}

// ------------------------------------------------------------------ residue signature

/// Residue categories (bits of [`Profile::residue`]): what kind of instructions are inserted,
/// deleted or substituted. Derived from a train-split analysis of mismatches left by the search;
/// they steer operator weights ([`residue_mult`]).
pub mod res {
    /// zero/sign extension: extsb, extsh, clrlwi, lha/lhz
    pub const EXT: u32 = 1;
    /// signedness: cmpw/cmplw, cmpwi/cmplwi, srawi/srwi, divw/divwu, addze
    pub const SIGN: u32 = 2;
    /// bool normalisation: neg, cntlzw, `srwi rX,rY,31`
    pub const BOOL: u32 = 4;
    /// double precision: frsp, fadd/fsub/fmul/fdiv (double), lfd
    pub const DOUBLE: u32 = 8;
    /// fused multiply-add vs separate multiply/add
    pub const FMA: u32 = 16;
    /// int<->float conversions: xoris, fctiwz, stfd
    pub const CONV: u32 = 32;
    /// branches: b, bc, cror
    pub const BRANCH: u32 = 64;
    /// calls: bl, bctrl
    pub const CALL: u32 = 128;
    /// stack traffic: loads/stores through r1
    pub const STACK: u32 = 256;
    /// register moves: mr, fmr
    pub const MOVE: u32 = 512;
    /// constants / addresses: li, lis, addi
    pub const CONST: u32 = 1024;
}

/// Residue categories of one instruction word.
pub fn word_class(w: u32) -> u32 {
    use res::*;
    let p = w >> 26;
    let xo10 = (w >> 1) & 0x3ff;
    let xo5 = (w >> 1) & 0x1f;
    let ra = (w >> 16) & 31;
    let rb = (w >> 11) & 31;
    let rs = (w >> 21) & 31;
    match p {
        10 | 11 => SIGN,
        14 => {
            if ra == 1 {
                STACK
            } else {
                CONST
            }
        }
        15 => CONST,
        16 => BRANCH,
        18 => {
            if w & 1 == 1 {
                CALL
            } else {
                BRANCH
            }
        }
        19 => match xo10 {
            449 | 417 | 193 | 225 | 257 | 289 | 129 | 33 => BRANCH,
            528 if w & 1 == 1 => CALL,
            _ => BRANCH,
        },
        21 => {
            let sh = (w >> 11) & 31;
            let mb = (w >> 6) & 31;
            let me = (w >> 1) & 31;
            if sh == 0 && me == 31 && mb >= 16 {
                EXT
            } else if me == 31 && sh != 0 && mb == 32 - sh {
                if mb == 31 { BOOL | SIGN } else { SIGN }
            } else {
                0
            }
        }
        27 => CONV,
        31 => match xo10 {
            954 | 922 => EXT,
            0 | 32 | 824 | 792 | 536 | 491 | 459 | 202 | 75 | 11 => SIGN,
            104 | 26 => BOOL,
            444 if rs == rb => MOVE,
            _ => 0,
        },
        42 | 43 => EXT,
        40 | 41 => EXT,
        32..=39 | 44..=47 | 48..=49 | 52..=53 => {
            if ra == 1 {
                STACK
            } else {
                0
            }
        }
        50 | 51 => DOUBLE | if ra == 1 { STACK } else { 0 },
        54 | 55 => CONV | if ra == 1 { STACK } else { 0 },
        59 => match xo5 {
            28..=31 => FMA,
            25 | 21 | 20 => FMA,
            _ => 0,
        },
        63 => match xo5 {
            18 | 20 | 21 | 25 | 28 | 29 | 30 | 31 => DOUBLE,
            _ => match xo10 {
                12 => DOUBLE,
                15 | 14 => CONV,
                72 => MOVE,
                _ => 0,
            },
        },
        _ => 0,
    }
}

/// Residue signature of target words `t` vs ours `o` given the opcode alignment `pairs`:
/// categories of unmatched instructions whose opcode does not also appear unmatched on the other
/// side (those are reorderings).
pub fn residue(t: &[u32], o: &[u32], pairs: &[(usize, usize)]) -> u32 {
    let mut tm = vec![false; t.len()];
    let mut om = vec![false; o.len()];
    for &(i, j) in pairs {
        tm[i] = true;
        om[j] = true;
    }
    let tu: Vec<u32> = (0..t.len()).filter(|&i| !tm[i]).map(|i| t[i]).collect();
    let ou: Vec<u32> = (0..o.len()).filter(|&j| !om[j]).map(|j| o[j]).collect();
    let mut ok: Vec<u32> = ou.iter().map(|&w| opkey(w)).collect();
    let mut sig = 0;
    for &w in &tu {
        let k = opkey(w);
        if let Some(p) = ok.iter().position(|&x| x == k) {
            ok.swap_remove(p);
            continue;
        }
        sig |= word_class(w);
    }
    let mut tk: Vec<u32> = tu.iter().map(|&w| opkey(w)).collect();
    for &w in &ou {
        let k = opkey(w);
        if let Some(p) = tk.iter().position(|&x| x == k) {
            tk.swap_remove(p);
            continue;
        }
        sig |= word_class(w);
    }
    sig
}

#[cfg(test)]
mod tests {
    use super::*;

    fn code(words: &[u32]) -> Code {
        Code {
            words: words.to_vec(),
            relocations: vec![],
        }
    }

    #[test]
    fn differences_are_classified() {
        // addi r3,r3,1; lwz r0,8(r1); blr
        let target = [0x3863_0001, 0x8001_0008, 0x4e80_0020];
        assert_eq!(
            profile(&code(&target), &code(&target), true),
            Profile::default()
        );
        // (addi r4,r4,1: registers; lwz r0,12(r1): the stack)
        let ours = [0x3884_0001, 0x8001_000c, 0x4e80_0020];
        let p = profile(&code(&target), &code(&ours), false);
        assert_eq!((p.reg, p.stack, p.structural()), (1, 1, 0));
        assert_eq!(p.penalty, PEN_REG + PEN_STACK);
        // (an extra mr r31,r3: an insertion)
        let ours = [0x7c7f_1b78, 0x3863_0001, 0x8001_0008, 0x4e80_0020];
        let p = profile(&code(&target), &code(&ours), false);
        assert_eq!((p.inserted, p.penalty), (1, PEN_INSDEL));
        assert_eq!(p.residue, res::MOVE);
        // (identical once relocated fields are masked, yet not matching: a relocation)
        let mut a = code(&[0x3c60_1234]);
        let mut b = code(&[0x3c60_5678]);
        a.relocations.push((0, 6));
        b.relocations.push((0, 6));
        let p = profile(&a, &b, false);
        assert_eq!((p.reloc, p.penalty), (1, PEN_RELOC));
    }
}
