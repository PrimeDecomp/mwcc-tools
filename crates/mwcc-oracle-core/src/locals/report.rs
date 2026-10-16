use super::{TargetFrame, items, number as n, shown, text as s, truth as t, unique};
use indexmap::IndexMap;
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};

fn kind(n: i64) -> String {
    match n {
        0 => "void",
        1 => "int",
        2 => "float",
        3 => "enum",
        4 => "struct",
        5 => "class",
        7 => "bitfield",
        10 => "member pointer",
        11 => "pointer",
        12 => "array",
        _ => return n.to_string(),
    }
    .into()
}
fn flip(reason: &str) -> Option<&'static str> {
    if reason.starts_with("used as a whole or at an offset not known") {
        Some(
            "every use were of a member at a constant offset (no whole copy of it, no index that is not a constant)",
        )
    } else if reason.starts_with("used as a ") {
        Some(
            "that copy were made member by member, or went away. An operator= the compiler makes copies a member whose class has no user operator= whole, even when that class has a user copy constructor; an implicit copy constructor does the same for a member whose class has no user copy constructor. So for an assignment, give the class copied whole a user operator= that copies each member (inlined); for a construction, a user copy constructor. The copy functions it is bound to are listed below",
        )
    } else if reason.starts_with("its uses overlap") {
        Some(
            "its bytes were always used at the same size and type (no stores of parts read back as a word)",
        )
    } else if reason.starts_with("its address is taken") {
        Some(
            "its address were not taken: no &x, no binding to a reference or pointer formal of a call that is not inlined, no member function call on it that is not inlined",
        )
    } else {
        None
    }
}
fn special(link: &str) -> Option<String> {
    let (rest, assignment) = if let Some(r) = link.strip_prefix("__as__") {
        (r, true)
    } else {
        (link.strip_prefix("__ct__")?, false)
    };
    let count = rest.chars().take_while(|c| c.is_ascii_digit()).count();
    if count == 0 {
        return None;
    }
    let length = rest[..count].parse::<usize>().ok()?;
    let owner: String = rest[count..].chars().take(length).collect();
    Some(if assignment {
        format!("{owner}::operator=")
    } else {
        format!("{owner}::{owner} (constructor)")
    })
}
fn copies(name: &str, bindings: &[Value]) -> Vec<(String, String, bool)> {
    let mut out = IndexMap::new();
    for b in bindings {
        if n(&b["index"]) >= 1
            && s(&b["object"]) == name
            && let Some(readable) = special(s(&b["callee"]))
        {
            out.entry(s(&b["callee"]).to_owned())
                .or_insert((readable, t(&b["generated"])));
        }
    }
    out.into_iter().map(|(l, (r, g))| (l, r, g)).collect()
}
fn short(reason: &str) -> String {
    if reason.starts_with("used as a whole or at an offset not known") {
        format!(
            "used whole or at an unknown offset{}",
            if reason.contains("a call's argument") {
                " (a call's argument)"
            } else {
                ""
            }
        )
    } else if reason.starts_with("used as a ") {
        format!(
            "{}: {}",
            reason
                .split(" at offset")
                .next()
                .unwrap()
                .replace("used as a ", "a "),
            if reason.contains("copied as a whole") {
                "copied whole"
            } else {
                "a member aggregate copied whole"
            }
        )
    } else {
        reason.split(" (").next().unwrap().into()
    }
}
pub fn blocked_reasons(var: &Value, pass: &Value) -> Vec<String> {
    let mut reasons = vec![];
    for u in items(&var["uses"]) {
        let at = format!(
            "node {}{}",
            n(&u["node"]),
            if t(&u["call"]) {
                ", a call's argument"
            } else {
                ""
            }
        );
        if n(&u["offset"]) < 0 {
            reasons.push(format!("used as a whole or at an offset not known ({at})"));
        } else if !matches!(n(&u["kind"]), 1 | 2 | 11) {
            let whole =
                n(&u["offset"]) == 0 && !var["size"].is_null() && n(&u["size"]) >= n(&var["size"]);
            reasons.push(format!("used as a {} of {} bytes at offset {} ({at}): {}",kind(n(&u["kind"])),n(&u["size"]),n(&u["offset"]),if whole{"copied as a whole (an assignment or copy construction that copies it bitwise, a by-value argument, a return)"}else{"a member aggregate copied whole (by an assignment or copy construction that copies that member bitwise)"}));
        }
    }
    let scalars: BTreeSet<_> = items(&var["uses"])
        .iter()
        .filter(|u| n(&u["offset"]) >= 0 && matches!(n(&u["kind"]), 1 | 2 | 11))
        .map(|u| {
            (
                n(&u["offset"]),
                n(&u["offset"]) + n(&u["size"]).max(1),
                n(&u["kind"]),
            )
        })
        .collect();
    let scalars: Vec<_> = scalars.into_iter().collect();
    let overlaps: Vec<_> = scalars.windows(2).filter(|p| p[1].0 < p[0].1).collect();
    if let Some(p) = overlaps.first() {
        reasons.push(format!(
            "its uses overlap with different sizes or types (bytes {}..{} and {}..{}{})",
            p[0].0,
            p[0].1 - 1,
            p[1].0,
            p[1].1 - 1,
            if overlaps.len() > 1 {
                format!(", and {} more", overlaps.len() - 1)
            } else {
                String::new()
            }
        ));
    }
    if t(&var["noregister"]) && t(&pass["calls"]) {
        reasons.push("its address is taken and a call may reach it (pointer analysis)".into());
    }
    let reasons = unique(reasons);
    if reasons.is_empty() {
        vec!["(not identified from its uses)".into()]
    } else {
        reasons
    }
}

pub fn summary(
    analysis: &Value,
    passes: &[Value],
    bindings: &[Value],
    runs: &[Value],
) -> Vec<String> {
    let mut lines: BTreeMap<&str, BTreeSet<i64>> = BTreeMap::new();
    for d in items(&analysis["inlining"]) {
        if d["kind"] == "decision" && t(&d["lines"]) {
            lines
                .entry(s(&d["link"]))
                .or_default()
                .extend(items(&d["lines"]).iter().map(n));
        }
    }
    let sizes: BTreeMap<_, _> = items(&analysis["frame"])
        .iter()
        .map(|e| (s(&e["name"]), e["size"].clone()))
        .collect();
    let mut kept = IndexMap::new();
    for pass in passes {
        for (name, var) in pass["vars"].as_object().into_iter().flatten() {
            if t(&var["parts"]) && !items(&var["parts"]).iter().all(|p| t(&p["blocked"])) {
                kept.shift_remove(name);
                continue;
            }
            let mut reasons: Vec<_> = blocked_reasons(var, pass)
                .iter()
                .filter(|r| !r.starts_with('('))
                .map(|r| short(r))
                .collect();
            if reasons.is_empty() && (t(&var["noregister"]) || !t(&pass["pointers"])) {
                reasons.push(format!(
                    "its address is taken{}",
                    if t(&pass["pointers"]) {
                        ""
                    } else {
                        " (pointer analysis off)"
                    }
                ));
            }
            let how: Vec<_> = copies(name, bindings)
                .iter()
                .map(|(link, readable, generated)| {
                    let at: Vec<_> = lines.get(link.as_str()).into_iter().flatten().collect();
                    format!(
                        "{readable}{}{}",
                        if *generated {
                            " (made by the compiler)"
                        } else {
                            ""
                        },
                        if at.is_empty() {
                            String::new()
                        } else {
                            format!(
                                ", line{} {}",
                                if at.len() > 1 { "s" } else { "" },
                                at.iter()
                                    .take(4)
                                    .map(|n| n.to_string())
                                    .collect::<Vec<_>>()
                                    .join(", ")
                            )
                        }
                    )
                })
                .collect();
            let size = if var["size"].is_null() {
                sizes.get(name.as_str()).cloned().unwrap_or(Value::Null)
            } else {
                var["size"].clone()
            };
            if reasons.is_empty() {
                reasons.push("not identified".into())
            }
            kept.insert(name.clone(), (size, unique(reasons), how));
        }
    }
    let copies: Vec<_> = runs.iter().flat_map(|r| items(&r["copies"])).collect();
    if kept.is_empty() && copies.is_empty() {
        return vec![];
    }
    let mut out = vec![format!(
        "== {}: kept in memory (summary) ==",
        s(&analysis["function"])
    )];
    for (name, (size, reasons, how)) in kept {
        out.push(format!(
            "   ours: {name} ({} bytes): {}{}",
            shown(&size),
            reasons.join("; "),
            if how.is_empty() {
                String::new()
            } else {
                format!("; through {}", how.join("; "))
            }
        ));
    }
    for c in copies {
        let offsets = items(&c["offsets"]);
        let read = offsets
            .iter()
            .filter(|o| c["reloaded"].get(n(o).to_string()).is_some())
            .count();
        out.push(format!("   target: {} copies of a {}-byte value at r1+{}, {read} read back{}",offsets.len(),n(&c["size"]),offsets.iter().map(|o|format!("{:#x}",n(o))).collect::<Vec<_>>().join(", "),if read<offsets.len(){" (a value made again for each use: a macro or inline that evaluates its argument more than once, or a by-value chain)"}else{""}));
    }
    out
}

fn location(name: &str, by_name: &IndexMap<&str, &Value>, crowded: bool) -> String {
    let Some(e) = by_name.get(name).filter(|e| t(&e["used"])) else {
        return "not in the frame: split, folded or dead".into();
    };
    if t(&e["reg"]) {
        return format!("reg {}", shown(&e["reg"]));
    }
    if crowded && n(&e["offset"]) == 0 && name.starts_with('@') {
        return "no slot of its own (listed at offset 0)".into();
    }
    if let Some(r1) = e.get("r1") {
        format!("memory r1+{}", shown(r1))
    } else {
        format!("memory +{}", n(&e["offset"]))
    }
}
fn pointed(b: &Value, holds: &IndexMap<&str, &str>) -> Option<String> {
    if !t(&b["ref"]) && n(&b["kind"]) != 11 {
        return None;
    }
    let mut target = s(&b["object"]);
    for _ in 0..16 {
        if let Some(next) = holds.get(target) {
            target = next;
        } else {
            break;
        }
    }
    (!target.is_empty()).then(|| target.into())
}
pub fn report(
    analysis: &Value,
    passes: &[Value],
    target: Option<&TargetFrame>,
    bindings: &[Value],
) -> Vec<String> {
    let mut out = vec![format!(
        "== {}: locals (the scalarizer: IRO_ScalarizeClassDataMembers) ==",
        s(&analysis["function"])
    )];
    let frame = items(&analysis["frame"]);
    let mut by_name = IndexMap::new();
    for e in frame {
        by_name.entry(s(&e["name"])).or_insert(e);
    }
    let crowded = frame
        .iter()
        .filter(|e| t(&e["used"]) && !t(&e["reg"]) && n(&e["offset"]) == 0)
        .count()
        > 2;
    let where_ = |name: &str| location(name, &by_name, crowded);
    let mut listed = BTreeSet::new();
    let mut seen = BTreeSet::new();
    let mut previous: Option<(usize, Vec<String>)> = None;
    if passes.is_empty() {
        out.push("   no scalarizer pass traced (MWCC_ORACLE_LOCALS=1 and an oracle with the sc.* hooks needed; the pass runs only with the global optimizer)".into());
    }
    for (index, pass) in passes.iter().enumerate() {
        if !t(&pass["vars"]) {
            continue;
        }
        let before = out.len();
        let mark = format!(
            "   pass {} (pointer analysis {})",
            index + 1,
            if t(&pass["pointers"]) { "on" } else { "off" }
        );
        for (name, original) in pass["vars"].as_object().into_iter().flatten() {
            seen.insert(name.as_str());
            let mut var = original.clone();
            if var["size"].is_null()
                && let Some(e) = by_name.get(name.as_str())
            {
                var["size"] = e["size"].clone();
            }
            let parts = items(&var["parts"]);
            let blocked = parts.iter().all(|p| t(&p["blocked"]));
            if parts.is_empty() || blocked {
                let mut reasons = blocked_reasons(&var, pass);
                if parts.is_empty() {
                    reasons.retain(|r| !r.starts_with('('));
                    if reasons.is_empty() && !t(&pass["pointers"]) {
                        reasons.push(format!("its address is taken (noregister), and pointer analysis is off: its {} member use(s) were not recorded",items(&var["uses"]).len()));
                    }
                }
                if reasons.is_empty() {
                    reasons.push("(not identified from its uses)".into());
                }
                out.push(format!(
                    "      {name} ({} bytes): KEPT IN MEMORY ({}){}",
                    shown(&var["size"]),
                    where_(name),
                    if parts.is_empty() {
                        ", no part made"
                    } else {
                        ""
                    }
                ));
                for reason in reasons {
                    out.push(format!("         because: {reason}"));
                    if let Some(hint) = flip(&reason) {
                        out.push(format!("            it would be split if {hint}"));
                    }
                    if reason.starts_with("used as a ") && listed.insert(name.clone()) {
                        for (link, readable, generated) in copies(name, bindings) {
                            out.push(format!("            {name} is the argument of inlined {readable} ({link}{})",if generated{", made by the compiler"}else{""}));
                        }
                    }
                }
            } else {
                out.push(format!("      {name} ({} bytes): split into scalars (it would be kept in memory by a whole copy of it or of a member aggregate, its address taken, or bytes used at different sizes)",shown(&var["size"])));
            }
            let mut spans: BTreeMap<_, Vec<&Value>> = BTreeMap::new();
            for p in parts {
                spans
                    .entry((n(&p["start"]), n(&p["end"]), n(&p["kind"]), n(&p["size"])))
                    .or_default()
                    .push(p);
            }
            for ((start, end, kind_, size), same) in spans {
                let span = if start < 0 {
                    "whole/unknown".into()
                } else {
                    format!(
                        "bytes {}..{}",
                        start.div_euclid(8),
                        (end + 1).div_euclid(8) - 1
                    )
                };
                let temps: Vec<_> = same.iter().filter_map(|p| p["temp"].as_str()).collect();
                out.push(format!(
                    "         {span:<14} {} {size}, {} use(s){}{}",
                    kind(kind_),
                    same.iter().map(|p| n(&p["uses"])).sum::<i64>(),
                    if same.len() > 1 {
                        format!(" (recorded {} times)", same.len())
                    } else {
                        String::new()
                    },
                    if temps.is_empty() {
                        String::new()
                    } else {
                        format!(
                            " -> {}",
                            temps
                                .iter()
                                .map(|t| format!("{t} ({})", where_(t)))
                                .collect::<Vec<_>>()
                                .join(", ")
                        )
                    }
                ));
            }
        }
        if let Some((number, lines)) = &previous
            && out[before..] == *lines
        {
            out.truncate(before);
            out.push(format!("{mark}: as pass {number}"));
            continue;
        }
        previous = Some((index + 1, out[before..].to_vec()));
        out.insert(before, format!("{mark}:"));
    }
    let holds: IndexMap<_, _> = bindings
        .iter()
        .filter(|b| {
            b["mode"] == "local"
                && t(&b["var"])
                && t(&b["object"])
                && (t(&b["ref"]) || n(&b["kind"]) == 11)
        })
        .map(|b| (s(&b["var"]), s(&b["object"])))
        .collect();
    // JSON keys are grouped by their full tuple, with stable first-seen callee order.
    let mut groups: Vec<(Value, Vec<String>)> = vec![];
    let mut callees = IndexMap::new();
    for b in bindings {
        if b["mode"] == "unused" {
            continue;
        }
        let next = callees.len();
        callees.entry(s(&b["callee"])).or_insert(next);
        let byvalue = if matches!(n(&b["declared"]), 4 | 5) {
            b["declared_size"].clone()
        } else {
            Value::Null
        };
        let key = json!([
            b["callee"],
            b["index"],
            b["name"],
            b["kind"],
            b["size"],
            b["ref"],
            b["mode"],
            byvalue
        ]);
        let value = if b["mode"] == "local" && t(&b["var"]) {
            if let Some(pointed) = pointed(b, &holds) {
                format!("{}=&{pointed}", s(&b["var"]))
            } else if (t(&b["ref"]) || n(&b["kind"]) == 11) && t(&b["expression"]) {
                format!("{}={}", s(&b["var"]), s(&b["expression"]))
            } else {
                s(&b["var"]).into()
            }
        } else {
            s(&b["object"]).into()
        };
        if let Some((_, values)) = groups.iter_mut().find(|(k, _)| *k == key) {
            values.push(value);
        } else {
            groups.push((key, vec![value]));
        }
    }
    if !groups.is_empty() {
        out.push("   formals of the calls inlined into it (substituted: the caller's expression stands for it, aliased; reference: bound to the temporary the argument constructs (a BY VALUE formal's is the caller's copy; a reference formal's, a temporary such as V(a, b, c)); local: a fresh local holding the argument, or its address for a reference formal):".into());
        groups.sort_by_key(|(k, _)| (callees[s(&k[0])], n(&k[1]), s(&k[6]).to_owned()));
        let mut callee = "";
        for (key, variables) in &groups {
            let link = s(&key[0]);
            let mode = s(&key[6]);
            if link != callee {
                out.push(format!("      {link}"));
                callee = link;
            }
            let named: Vec<_> = variables.iter().filter(|v| !v.is_empty()).collect();
            let mut fates = String::new();
            if !named.is_empty()
                && (matches!(mode, "local" | "reference")
                    || named.iter().any(|v| by_name.contains_key(v.as_str())))
            {
                fates = format!(
                    " -> {}{}",
                    unique(named.iter().take(6).copied())
                        .iter()
                        .map(|v| if !v.contains('=') || v.contains("=&") {
                            format!("{v} ({})", where_(v.split("=&").last().unwrap()))
                        } else {
                            v.to_string()
                        })
                        .collect::<Vec<_>>()
                        .join(", "),
                    if named.len() > 6 {
                        format!(", ... {} more", named.len() - 6)
                    } else {
                        String::new()
                    }
                );
            }
            let byvalue = !key[7].is_null();
            let what = if byvalue {
                format!("BY VALUE, a class of {}", shown(&key[7]))
            } else if t(&key[5]) {
                "reference".into()
            } else {
                format!("{} {}", kind(n(&key[3])), n(&key[4]))
            };
            let copy = if byvalue && mode == "reference" {
                " (the caller's COPY)"
            } else if mode == "local" && matches!(n(&key[3]), 4 | 5 | 12) {
                " (a COPY of the aggregate)"
            } else {
                ""
            };
            out.push(format!(
                "         #{} {} ({what}): {mode}{copy}{}{fates}",
                n(&key[1]),
                s(&key[2]),
                if variables.len() > 1 {
                    format!(" x{}", variables.len())
                } else {
                    String::new()
                }
            ));
        }
    }
    let mut holders: IndexMap<String, Vec<String>> = IndexMap::new();
    for (order, b) in bindings.iter().enumerate() {
        if b["mode"] == "local"
            && t(&b["var"])
            && let Some(pointed) = pointed(b, &holds)
        {
            holders.entry(pointed).or_default().push(format!(
                "#{} {} {} ({})",
                order + 1,
                s(&b["callee"]),
                s(&b["name"]),
                s(&b["var"])
            ));
        }
    }
    if !holders.is_empty() {
        out.push("   objects whose address an inlined call holds in a local (#: the order the formals were bound; such an object is address-taken, and the scalarizer splits it only if every use of it is a member at a known offset):".into());
        for (name, held) in holders {
            out.push(format!(
                "      {name} ({}): {}{}",
                where_(&name),
                held.iter().take(8).cloned().collect::<Vec<_>>().join("; "),
                if held.len() > 8 {
                    format!("; ... {} more", held.len() - 8)
                } else {
                    String::new()
                }
            ));
        }
    }
    let temps: BTreeSet<_> = passes
        .iter()
        .flat_map(|p| p["vars"].as_object().into_iter().flat_map(|o| o.values()))
        .flat_map(|v| items(&v["parts"]))
        .filter_map(|p| p["temp"].as_str())
        .collect();
    let mut unseen: Vec<_> = frame
        .iter()
        .filter(|e| {
            t(&e["used"])
                && !t(&e["reg"])
                && !seen.contains(s(&e["name"]))
                && !temps.contains(s(&e["name"]))
                && n(&e["size"]) > 4
        })
        .collect();
    if !unseen.is_empty() {
        out.push("   in memory, and never seen by the scalarizer (no member access of it: used only whole, as a copy's source or target, or its address is taken):".into());
        unseen.sort_by_key(|e| e.get("r1").map_or(n(&e["offset"]), n));
        for e in unseen {
            out.push(format!(
                "      {} ({} bytes, {})",
                s(&e["name"]),
                n(&e["size"]),
                where_(s(&e["name"]))
            ));
        }
    }
    if let Some(target) = target {
        let ours = analysis["layout"]["size"].as_i64();
        out.push(format!(
            "   frame: target {:#x} ({}), ours {}{}",
            target.size,
            target.size,
            ours.map_or("?".into(), |o| format!("{o:#x} ({o})")),
            ours.filter(|o| *o != target.size)
                .map_or(String::new(), |o| format!(
                    ": {:+} bytes in the target's",
                    target.size - o
                ))
        ));
        let mut runs: Vec<(i64, i64, Vec<i64>)> = vec![];
        for (&offset, uses) in &target.uses {
            let width = uses.iter().map(|(w, _)| *w).max().unwrap_or(0);
            if let Some(run) = runs.last_mut().filter(|r| offset <= r.1) {
                run.1 = run.1.max(offset + width);
                run.2.push(offset);
            } else {
                runs.push((offset, offset + width.max(1), vec![offset]));
            }
        }
        if !runs.is_empty() {
            out.push("   the target's locals area, by the r1 offsets its code uses (addi: the address is taken):".into());
            let mine: Vec<_> = frame
                .iter()
                .filter(|e| t(&e["used"]) && !t(&e["reg"]) && e.get("r1").is_some())
                .collect();
            let crowded = mine.iter().filter(|e| n(&e["offset"]) == 0).count() > 2;
            for (start, end, offsets) in runs {
                let accesses = offsets
                    .iter()
                    .map(|o| {
                        format!(
                            "{o:#x}:{}",
                            target.uses[o]
                                .iter()
                                .map(|(_, m)| m.as_str())
                                .collect::<BTreeSet<_>>()
                                .into_iter()
                                .collect::<Vec<_>>()
                                .join("/")
                        )
                    })
                    .collect::<Vec<_>>()
                    .join(", ");
                let here: Vec<_> = mine
                    .iter()
                    .filter(|e| n(&e["r1"]) < end && n(&e["r1"]) + n(&e["size"]) > start)
                    .collect();
                let mut names: Vec<_> = here
                    .iter()
                    .filter(|e| {
                        !(crowded && n(&e["offset"]) == 0 && s(&e["name"]).starts_with('@'))
                    })
                    .map(|e| s(&e["name"]).to_owned())
                    .collect();
                let others = here.len() - names.len();
                if others > 0 {
                    names.push(format!(
                        "{others} compiler temporaries listed at offset 0 (no slot of their own)"
                    ));
                }
                out.push(format!(
                    "      r1+{start:#x}..{:#x} ({} bytes) {accesses}{}",
                    end - 1,
                    end - start,
                    if names.is_empty() {
                        String::new()
                    } else {
                        format!("   ours there: {}", names.join(", "))
                    }
                ));
            }
        }
    }
    out
}
