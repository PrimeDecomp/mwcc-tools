use super::{items, number as n, text as s, truth as t};
use crate::{
    ppc::{fn_body, instruction_token, r1_offset},
    sequence::matching_blocks_with_autojunk,
};
use indexmap::IndexMap;
use regex::Regex;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::LazyLock,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct TargetFrame {
    pub size: i64,
    pub uses: BTreeMap<i64, BTreeSet<(i64, String)>>,
    pub limit: i64,
}
/// A decimal or `0x` hexadecimal integer, optionally negative; None when it does not fit.
fn integer(s: &str) -> Option<i64> {
    let (negative, s) = match s.strip_prefix('-') {
        Some(s) => (true, s),
        None => (false, s),
    };
    let value = match s.strip_prefix("0x") {
        Some(s) => i64::from_str_radix(s, 16).ok()?,
        None => s.parse::<i64>().ok()?,
    };
    if negative {
        value.checked_neg()
    } else {
        Some(value)
    }
}
pub fn frame(text: &str, link: &str) -> Option<TargetFrame> {
    static SIZE: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^stwu r1, -(0x[0-9a-fA-F]+|[0-9]+)\(r1\)").unwrap());
    static SAVES: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"^(stmw r[0-9]+|stfd f(?:1[4-9]|2[0-9]|3[01])|psq_st f(?:1[4-9]|2[0-9]|3[01])), (0x[0-9a-fA-F]+|[0-9]+)\(r1\)").unwrap()
    });
    static ACCESS: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^(\w+) \w+, (-?0x[0-9a-fA-F]+|-?[0-9]+)\(r1\)").unwrap());
    static ADDRESS: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^addi \w+, r1, (0x[0-9a-fA-F]+|[0-9]+)$").unwrap());
    let body: Vec<&str> = fn_body(text, link)
        .map(|line| line.rsplit("*/").next().unwrap().trim())
        .collect();
    let mut size = None;
    let mut saves = None;
    let mut uses: BTreeMap<i64, BTreeSet<(i64, String)>> = BTreeMap::new();
    for (number, ins) in body.iter().enumerate() {
        if size.is_none()
            && let Some(bytes) = SIZE.captures(ins).and_then(|c| integer(&c[1]))
        {
            size = Some(bytes);
            continue;
        }
        if number < 16
            && let Some(offset) = SAVES.captures(ins).and_then(|c| integer(&c[2]))
        {
            saves = Some(saves.unwrap_or(1 << 30).min(offset));
        }
        if let Some(c) = ACCESS.captures(ins)
            && let Some(offset) = integer(&c[2])
        {
            let mnemonic = &c[1];
            let width = if mnemonic.starts_with("psq") {
                8
            } else {
                match mnemonic.trim_end_matches(['u', 'x', 'a']).chars().last() {
                    Some('b') => 1,
                    Some('h') => 2,
                    Some('d') => 8,
                    _ => 4,
                }
            };
            uses.entry(offset)
                .or_default()
                .insert((width, mnemonic.into()));
        }
        if let Some(offset) = ADDRESS.captures(ins).and_then(|c| integer(&c[1])) {
            uses.entry(offset).or_default().insert((0, "addi".into()));
        }
    }
    let size = size?;
    let limit = saves.unwrap_or(size);
    uses.retain(|o, _| 8 <= *o && *o < limit);
    Some(TargetFrame { size, uses, limit })
}
pub type Shifts = IndexMap<String, IndexMap<i64, usize>>;
fn pairing(ours: &[u32], theirs: &[u32]) -> Vec<(usize, usize, usize)> {
    matching_blocks_with_autojunk(
        &ours
            .iter()
            .copied()
            .map(instruction_token)
            .collect::<Vec<_>>(),
        &theirs
            .iter()
            .copied()
            .map(instruction_token)
            .collect::<Vec<_>>(),
        false,
    )
}
fn slot(e: &Value) -> bool {
    t(&e["used"]) && !t(&e["reg"]) && e.get("r1").is_some()
}
pub fn slot_shifts(
    ours: &[u32],
    theirs: &[u32],
    frame: &[Value],
    our_limit: i64,
    their_limit: i64,
) -> Shifts {
    let mut slots: Vec<_> = frame
        .iter()
        .filter(|e| slot(e) && n(&e["offset"]) != 0)
        .collect();
    slots.sort_by_key(|e| n(&e["r1"]));
    slots.extend(
        frame
            .iter()
            .filter(|e| slot(e) && n(&e["offset"]) == 0 && !s(&e["name"]).starts_with('@')),
    );
    let mut out = Shifts::new();
    for (a, b, size) in pairing(ours, theirs) {
        for i in 0..size {
            if let (Some(mine), Some(target)) = (r1_offset(ours[a + i]), r1_offset(theirs[b + i])) {
                let (mine, target) = (i64::from(mine), i64::from(target));
                if !(8..our_limit).contains(&mine) || !(8..their_limit).contains(&target) {
                    continue;
                }
                let name = slots
                    .iter()
                    .find(|e| n(&e["r1"]) <= mine && mine < n(&e["r1"]) + n(&e["size"]).max(1))
                    .map_or("?", |e| s(&e["name"]));
                *out.entry(name.into())
                    .or_default()
                    .entry(target - mine)
                    .or_default() += 1;
            }
        }
    }
    out
}
pub fn shift_report(shifts: &Shifts, frame: &[Value]) -> Vec<String> {
    if shifts.is_empty() {
        return vec![];
    }
    let by_name: IndexMap<_, _> = frame.iter().map(|e| (s(&e["name"]), e)).collect();
    let mut groups: IndexMap<i64, Vec<&str>> = IndexMap::new();
    let mut mixed = vec![];
    let where_ = |name: &str| {
        by_name
            .get(name)
            .filter(|e| e.get("r1").is_some())
            .map_or("?".into(), |e| {
                format!("{}@r1+{:#x}", n(&e["size"]), n(&e["r1"]))
            })
    };
    for (name, deltas) in shifts {
        let usual = deltas
            .iter()
            .enumerate()
            .max_by_key(|(i, (_, n))| (**n, std::cmp::Reverse(*i)))
            .unwrap()
            .1
            .0;
        groups.entry(*usual).or_default().push(name);
        if deltas.len() > 1 {
            mixed.push(name);
        }
    }
    let mut out=vec!["   slot shifts (target offset - ours, by the object our paired r1 accesses are in; the smallest groups are the objects the target places in another order):".into()];
    let count = groups.len();
    let mut groups: Vec<_> = groups.into_iter().collect();
    groups.sort_by_key(|(d, n)| (n.len(), *d));
    for (delta, mut names) in groups {
        names.sort_by_key(|name| {
            by_name
                .get(name)
                .and_then(|e| e["r1"].as_i64())
                .unwrap_or(1 << 30)
        });
        let shown = names
            .iter()
            .take(10)
            .map(|n| format!("{n} ({})", where_(n)))
            .collect::<Vec<_>>()
            .join(", ");
        out.push(format!(
            "      {delta:+#x}: {} object(s): {shown}{}",
            names.len(),
            if names.len() > 10 {
                format!(", ... {} more", names.len() - 10)
            } else {
                String::new()
            }
        ));
        if names.len() <= 2 && count > 1 {
            for name in names {
                if let Some(e) = by_name.get(name).filter(|e| e.get("r1").is_some()) {
                    let offset = n(&e["r1"]) + delta;
                    let there: Vec<_> = frame
                        .iter()
                        .filter(|e| {
                            slot(e)
                                && n(&e["offset"]) != 0
                                && n(&e["r1"]) <= offset
                                && offset < n(&e["r1"]) + n(&e["size"]).max(1)
                        })
                        .map(|e| s(&e["name"]))
                        .collect();
                    if !there.is_empty() && there != [name] {
                        out.push(format!("         the target has {name} at r1+{offset:#x}, where ours has {} (temporaries get slots by when they are made)",there.join(", ")));
                    }
                }
            }
        }
    }
    for name in mixed {
        let mut deltas: Vec<_> = shifts[name].iter().collect();
        deltas.sort_by_key(|(_, count)| std::cmp::Reverse(**count));
        let shown = deltas
            .iter()
            .map(|(d, n)| format!("{d:+#x} x{n}"))
            .collect::<Vec<_>>()
            .join(", ");
        out.push(format!(
            "      {name} ({}) has accesses at several deltas: {shown}",
            where_(name)
        ));
    }
    out
}
fn memory(op: u32) -> Option<(i64, &'static str)> {
    Some(match op {
        14 => (0, "addi"),
        32 => (4, "lwz"),
        33 => (4, "lwzu"),
        34 => (1, "lbz"),
        35 => (1, "lbzu"),
        36 => (4, "stw"),
        37 => (4, "stwu"),
        38 => (1, "stb"),
        39 => (1, "stbu"),
        40 => (2, "lhz"),
        41 => (2, "lhzu"),
        42 => (2, "lha"),
        43 => (2, "lhau"),
        44 => (2, "sth"),
        45 => (2, "sthu"),
        46 => (4, "lmw"),
        47 => (4, "stmw"),
        48 => (4, "lfs"),
        49 => (4, "lfsu"),
        50 => (8, "lfd"),
        51 => (8, "lfdu"),
        52 => (4, "stfs"),
        53 => (4, "stfsu"),
        54 => (8, "stfd"),
        55 => (8, "stfdu"),
        56 => (8, "psq_l"),
        57 => (8, "psq_lu"),
        60 => (8, "psq_st"),
        61 => (8, "psq_stu"),
        _ => return None,
    })
}
fn store(op: u32) -> bool {
    matches!(op,36..=39|44|45|52..=55|60|61)
}
type Register = (char, u32);
fn writes(word: u32) -> Vec<Register> {
    let op = word >> 26;
    let d = (word >> 21) & 31;
    let a = (word >> 16) & 31;
    let xo = (word >> 1) & 0x3ff;
    if matches!(op, 18 | 19) && word & 1 != 0 {
        return (0..14)
            .map(|n| ('f', n))
            .chain((0..13).map(|n| ('r', n)))
            .collect();
    }
    if store(op) || matches!(op, 16 | 18 | 19 | 10 | 11 | 47) {
        return vec![];
    }
    if matches!(op, 48..=51 | 56 | 57)
        || matches!(op, 59 | 63) && !(op == 63 && matches!(xo, 0 | 32))
        || op == 4
    {
        let mut out = vec![('f', d)];
        if matches!(op, 49 | 51 | 57) {
            out.push(('r', a));
        }
        return out;
    }
    if op == 31 && matches!(xo, 0 | 32) {
        return vec![];
    }
    vec![('r', d), ('r', a)]
}
type Stored = (Register, i64, i64);
type Members = Vec<Vec<Stored>>;
type Group = (i64, Vec<i64>, Members);
pub fn run_copies(words: &[u32], start: i64, end: i64) -> Vec<Value> {
    let mut versions: BTreeMap<Register, i64> = BTreeMap::new();
    let mut stores: BTreeMap<i64, Vec<Stored>> = BTreeMap::new();
    let mut loads: BTreeMap<i64, Vec<&str>> = BTreeMap::new();
    let mut at: BTreeMap<i64, Vec<usize>> = BTreeMap::new();
    for (index, &word) in words.iter().enumerate() {
        let op = word >> 26;
        if let Some(offset) = r1_offset(word)
            .map(i64::from)
            .filter(|o| (start..end).contains(o))
        {
            if store(op) {
                let register = (
                    if matches!(op, 52..=55 | 60 | 61) {
                        'f'
                    } else {
                        'r'
                    },
                    (word >> 21) & 31,
                );
                stores.entry(offset).or_default().push((
                    register,
                    *versions.get(&register).unwrap_or(&0),
                    memory(op).unwrap().0,
                ));
                at.entry(offset).or_default().push(index);
            } else if op != 14 {
                loads.entry(offset).or_default().push(memory(op).unwrap().1);
            }
        }
        for register in writes(word) {
            *versions.entry(register).or_default() += 1;
        }
    }
    if stores.is_empty() {
        return vec![];
    }
    let mut best: Vec<Group> = vec![];
    for size in (8..=(end - start).min(64)).step_by(4) {
        let mut blocks: IndexMap<Members, Vec<i64>> = IndexMap::new();
        for &base in stores.keys() {
            let members: Members = (base..base + size)
                .step_by(4)
                .map(|o| {
                    let mut m = stores.get(&o).cloned().unwrap_or_default();
                    m.sort();
                    m
                })
                .collect();
            if members.len() > 1 && members.iter().all(|m| !m.is_empty()) {
                blocks.entry(members).or_default().push(base);
            }
        }
        let mut groups = vec![];
        for (members, bases) in blocks {
            let mut chosen = vec![];
            for base in bases {
                if chosen.last().is_none_or(|b| base >= b + size) {
                    chosen.push(base);
                }
            }
            if chosen.len() > 1 {
                groups.push((size, chosen, members));
            }
        }
        groups.sort_by_key(|g| std::cmp::Reverse(g.1.len()));
        let mut taken = BTreeSet::new();
        let mut picked = vec![];
        for group in groups {
            let bytes: BTreeSet<_> = group.1.iter().flat_map(|b| *b..b + size).collect();
            if bytes.is_disjoint(&taken) {
                taken.extend(bytes);
                picked.push(group);
            }
        }
        let total = |groups: &[Group]| groups.iter().map(|g| g.1.len() as i64 * g.0).sum::<i64>();
        if total(&picked) > total(&best) {
            best = picked;
        }
    }
    best.sort_by_key(|g| (-(g.1.len() as i64 * g.0), g.1[0]));
    let mut out = vec![];
    for (size, bases, members) in best {
        let mut reloaded = serde_json::Map::new();
        for &base in &bases {
            for o in base..base + size {
                for mnemonic in loads.get(&o).into_iter().flatten() {
                    reloaded
                        .entry(base.to_string())
                        .or_insert(json!([]))
                        .as_array_mut()
                        .unwrap()
                        .push(json!([o - base, mnemonic]));
                }
            }
        }
        let mut order: Vec<_> = bases
            .iter()
            .flat_map(|base| {
                (*base..base + size)
                    .step_by(4)
                    .flat_map(|o| at.get(&o).into_iter().flatten().map(|i| (*i, *base)))
            })
            .collect();
        order.sort();
        let changes = order.windows(2).filter(|p| p[0].1 != p[1].1).count();
        out.push(json!({"size":size,"offsets":bases,"reloaded":reloaded,"registers":members.iter().map(|m|format!("{}{}",m[0].0.0,m[0].0.1)).collect::<Vec<_>>(),"order":if changes==bases.len()-1{"copy by copy"}else{"interleaved"},"first":order[0].1}));
    }
    out
}
fn accesses(words: &[u32], limit: i64) -> IndexMap<i64, Vec<(i64, &'static str)>> {
    let mut out = IndexMap::new();
    for &word in words {
        if let Some(offset) = r1_offset(word)
            .map(i64::from)
            .filter(|o| (8..limit).contains(o))
        {
            out.entry(offset)
                .or_insert_with(Vec::new)
                .push(memory(word >> 26).unwrap());
        }
    }
    out
}
pub fn unpaired_objects(
    ours: &[u32],
    theirs: &[u32],
    frame: &[Value],
    our_limit: i64,
    their_limit: i64,
) -> (Vec<Value>, Vec<Value>) {
    let mut paired_ours = BTreeSet::new();
    let mut paired_theirs = BTreeSet::new();
    for (a, b, size) in pairing(ours, theirs) {
        for i in 0..size {
            if let (Some(mine), Some(target)) = (r1_offset(ours[a + i]), r1_offset(theirs[b + i])) {
                paired_ours.insert(i64::from(mine));
                paired_theirs.insert(i64::from(target));
            }
        }
    }
    let theirs_at = accesses(theirs, their_limit);
    let mut offsets: Vec<_> = theirs_at
        .keys()
        .filter(|o| !paired_theirs.contains(o))
        .copied()
        .collect();
    offsets.sort();
    let mut runs: Vec<Value> = vec![];
    for offset in offsets {
        let width = theirs_at[&offset]
            .iter()
            .map(|(w, _)| *w)
            .max()
            .unwrap()
            .max(1);
        if let Some(run) = runs.last_mut().filter(|r| offset <= n(&r["end"])) {
            run["end"] = json!(n(&run["end"]).max(offset + width));
        } else {
            runs.push(json!({"start":offset,"end":offset+width,"accesses":{}}));
        }
        let run = runs.last_mut().unwrap();
        for (_, mnemonic) in &theirs_at[&offset] {
            run["accesses"][*mnemonic] = json!(n(&run["accesses"][*mnemonic]) + 1);
        }
    }
    let ours_at = accesses(ours, our_limit);
    let mut mine = vec![];
    for e in frame {
        if !slot(e) || n(&e["offset"]) == 0 && s(&e["name"]).starts_with('@') {
            continue;
        }
        let offsets: Vec<_> = ours_at
            .keys()
            .filter(|o| n(&e["r1"]) <= **o && **o < n(&e["r1"]) + n(&e["size"]).max(1))
            .copied()
            .collect();
        if !offsets.is_empty() && !offsets.iter().any(|o| paired_ours.contains(o)) {
            let mut counts = json!({});
            for o in offsets {
                for (_, m) in &ours_at[&o] {
                    counts[*m] = json!(n(&counts[*m]) + 1);
                }
            }
            let mut e = e.clone();
            e["accesses"] = counts;
            mine.push(e);
        }
    }
    for run in &mut runs {
        run["copies"] = json!(run_copies(theirs, n(&run["start"]), n(&run["end"])));
    }
    (runs, mine)
}
fn counts(counts: &Value) -> String {
    let mut counts: Vec<_> = counts.as_object().into_iter().flatten().collect();
    counts.sort_by_key(|(_, n_)| std::cmp::Reverse(n(n_)));
    counts
        .iter()
        .map(|(m, n)| format!("{m} x{n}"))
        .collect::<Vec<_>>()
        .join(", ")
}
pub fn unpaired_report(runs: &[Value], mine: &[Value]) -> Vec<String> {
    let mut out = vec![];
    if !runs.is_empty() {
        out.push("   in the target's memory, with no access of ours paired (kept in memory there; ours has it in registers, elsewhere, or not at all):".into());
        for run in runs {
            out.push(format!(
                "      r1+{:#x}..{:#x} ({} bytes): {}{}",
                n(&run["start"]),
                n(&run["end"]) - 1,
                n(&run["end"]) - n(&run["start"]),
                counts(&run["accesses"]),
                if run["accesses"].get("addi").is_some() {
                    " (its address passed on)"
                } else {
                    ""
                }
            ));
            for c in items(&run["copies"]) {
                out.push(format!("         {} identical copies of a {}-byte value ({}) at r1+{}, stored {} (first: {:#x})",items(&c["offsets"]).len(),n(&c["size"]),items(&c["registers"]).iter().map(s).collect::<Vec<_>>().join(", "),items(&c["offsets"]).iter().map(|o|format!("{:#x}",n(o))).collect::<Vec<_>>().join(", "),s(&c["order"]),n(&c["first"])));
                let reloads = c["reloaded"].as_object().unwrap();
                if !reloads.is_empty() {
                    let read = reloads
                        .iter()
                        .map(|(o, members)| {
                            format!(
                                "the copy at {:#x}: {}",
                                o.parse::<i64>().unwrap(),
                                items(members)
                                    .iter()
                                    .map(|m| format!("+{} ({})", n(&m[0]), s(&m[1])))
                                    .collect::<Vec<_>>()
                                    .join(", ")
                            )
                        })
                        .collect::<Vec<_>>()
                        .join("; ");
                    let dead = items(&c["offsets"]).len() - reloads.len();
                    out.push(format!(
                        "            read back: {read}{}",
                        if dead > 0 {
                            format!("; the {dead} other(s) never read (dead stores)")
                        } else {
                            String::new()
                        }
                    ));
                } else {
                    out.push(
                        "            never read back (dead stores, or read through an address)"
                            .into(),
                    );
                }
            }
            if t(&run["copies"]) {
                out.push("         (one value copied whole into several temporaries the target keeps in memory: a chain of whole copies, by-value returns or arguments, each a temporary of its own; compare with our locals of that size split into scalars, above)".into());
            }
        }
    }
    if !mine.is_empty() {
        out.push("   in our memory, with no access of the target's paired (the target keeps it elsewhere or scalarized):".into());
        for e in mine {
            out.push(format!(
                "      {} ({} bytes at r1+{:#x}): {}",
                s(&e["name"]),
                n(&e["size"]),
                n(&e["r1"]),
                counts(&e["accesses"])
            ));
        }
    }
    out
}
