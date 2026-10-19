//! Objdiff register/stack differences joined to structured allocation evidence.
use crate::{allocation, report, trace::Trace, version::Version};
use anyhow::{Context, Result, bail};
use indexmap::IndexMap;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
fn array(v: &Value) -> &[Value] {
    v.as_array().map_or(&[], Vec::as_slice)
}
fn text(v: &Value) -> &str {
    v.as_str().unwrap_or("")
}
fn number(v: &Value) -> i64 {
    v.as_i64()
        .or_else(|| v.as_str().and_then(|s| s.parse().ok()))
        .unwrap_or(0)
}
fn truth(v: &Value) -> bool {
    match v {
        Value::Null => false,
        Value::Bool(b) => *b,
        Value::Number(_) => number(v) != 0,
        Value::String(s) => !s.is_empty(),
        Value::Array(v) => !v.is_empty(),
        Value::Object(v) => !v.is_empty(),
    }
}
pub fn short_name(name: &str) -> &str {
    if let Some(tail) = name.strip_prefix("__") {
        if let Some(at) = tail.find("__") {
            &name[..at + 2]
        } else {
            name
        }
    } else {
        name.split("__").next().unwrap()
    }
}
pub fn function_symbols(side: &Value) -> Vec<&Value> {
    let mut out: Vec<_> = array(&side["symbols"])
        .iter()
        .filter(|s| s["kind"] == "SYMBOL_FUNCTION" && number(&s["size"]) != 0)
        .collect();
    out.sort_by_key(|s| number(&s["address"]));
    out
}
pub fn select_symbol<'a>(symbols: &[&'a Value], query: &str) -> Result<&'a Value> {
    if let Some(s) = symbols
        .iter()
        .find(|s| s["name"] == query || s["demangled_name"] == query)
    {
        return Ok(*s);
    }
    let name = |v: &Value| {
        v["demangled_name"]
            .as_str()
            .filter(|s| !s.is_empty())
            .unwrap_or(text(&v["name"]))
            .to_owned()
    };
    let short: Vec<_> = symbols
        .iter()
        .filter(|s| {
            let name = name(s);
            let short = name.split('(').next().unwrap();
            short == query || short.rsplit("::").next() == Some(query)
        })
        .copied()
        .collect();
    let found = if short.is_empty() {
        symbols
            .iter()
            .filter(|s| name(s).to_lowercase().contains(&query.to_lowercase()))
            .copied()
            .collect::<Vec<_>>()
    } else {
        short
    };
    if found.len() != 1 {
        let names = if found.is_empty() {
            "  (none)".into()
        } else {
            found
                .iter()
                .map(|v| format!("  {}", text(&v["name"])))
                .collect::<Vec<_>>()
                .join("\n")
        };
        bail!(
            "{query:?} matches {} functions; use the mangled name:\n{names}",
            found.len()
        );
    }
    Ok(found[0])
}
fn instructions(row: &Value) -> (Option<&str>, Vec<Value>) {
    let ins = &row["instruction"];
    if ins.is_null() {
        return (None, vec![]);
    }
    let mut mnemonic = None;
    let mut args = vec![];
    for part in array(&ins["parts"]) {
        if part.get("opcode").is_some() {
            mnemonic = part["opcode"]["mnemonic"].as_str();
        }
        if let Some(arg) = part.get("arg").filter(|v| !v.is_null()) {
            args.push(arg.get("opaque").unwrap_or(arg).clone());
        }
    }
    (mnemonic, args)
}
fn register(arg: &Value) -> Option<(&str, i64)> {
    let text = arg.as_str()?;
    let (prefix, digits) = text.split_at_checked(1)?;
    if !matches!(prefix, "r" | "f")
        || digits.is_empty()
        || !digits.chars().all(|c| c.is_ascii_digit())
    {
        return None;
    }
    Some((
        if prefix == "r" { "GPR" } else { "FPR" },
        digits.parse().ok()?,
    ))
}
fn immediate(arg: &Value) -> Option<i64> {
    arg.get("signed")
        .or_else(|| arg.get("unsigned"))
        .and_then(|v| {
            v.as_i64()
                .or_else(|| v.as_u64().and_then(|v| v.try_into().ok()))
                .or_else(|| v.as_str().and_then(|s| s.parse().ok()))
        })
}
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Differences {
    pub registers: Vec<Value>,
    pub slots: Vec<Value>,
    pub other: Vec<(String, String)>,
}
pub fn pair_registers(target: &[Value], base: &[Value]) -> Differences {
    let mut pairs: IndexMap<(String, i64, i64), usize> = IndexMap::new();
    let mut slots: BTreeMap<(i64, i64), usize> = BTreeMap::new();
    let mut other = vec![];
    for (left, right) in target.iter().zip(base) {
        if !truth(&left["diff_kind"]) && !truth(&right["diff_kind"]) {
            continue;
        }
        let (lm, la) = instructions(left);
        let (rm, ra) = instructions(right);
        let ours = right["instruction"]["formatted"].as_str().unwrap_or("-");
        let theirs = left["instruction"]["formatted"].as_str().unwrap_or("-");
        if lm.is_none() || rm.is_none() || lm != rm || la.len() != ra.len() {
            other.push((ours.into(), theirs.into()));
            continue;
        }
        let diffs: Vec<_> = la.iter().zip(&ra).filter(|(a, b)| a != b).collect();
        if !diffs.is_empty()
            && diffs.iter().all(|(a, b)| {
                register(a)
                    .zip(register(b))
                    .is_some_and(|(a, b)| a.0 == b.0)
            })
        {
            for (theirs, ours) in diffs {
                let (class, mine) = register(ours).unwrap();
                *pairs
                    .entry((class.into(), mine, register(theirs).unwrap().1))
                    .or_default() += 1;
            }
        } else if !diffs.is_empty()
            && ra.contains(&json!("r1"))
            && diffs
                .iter()
                .all(|(a, b)| immediate(a).is_some() && immediate(b).is_some())
        {
            for (theirs, ours) in diffs {
                *slots
                    .entry((immediate(ours).unwrap(), immediate(theirs).unwrap()))
                    .or_default() += 1;
            }
        } else {
            other.push((ours.into(), theirs.into()));
        }
    }
    Differences{registers:pairs.into_iter().map(|((class,ours,target),count)|json!({"class":class,"ours":ours,"target":target,"count":count})).collect(),slots:slots.into_iter().map(|((ours,target),count)|json!({"ours":ours,"target":target,"count":count})).collect(),other}
}
pub fn select_allocation(trace: &Trace, symbol: &str, order: &[String]) -> (Option<usize>, String) {
    if let Some(index) = trace
        .allocations
        .iter()
        .position(|a| a.before.link == symbol)
    {
        return (Some(index), "link name".into());
    }
    let short = short_name(symbol);
    let linked: BTreeSet<_> = trace
        .allocations
        .iter()
        .map(|a| a.before.link.as_str())
        .collect();
    let same: Vec<_> = trace
        .allocations
        .iter()
        .enumerate()
        .filter(|(_, a)| {
            a.before.function == short && (a.before.link.is_empty() || a.before.link == short)
        })
        .map(|(i, _)| i)
        .collect();
    let peers: Vec<_> = order
        .iter()
        .filter(|name| short_name(name) == short && !linked.contains(name.as_str()))
        .collect();
    let Some(peer) = peers.iter().position(|name| name.as_str() == symbol) else {
        return (None, "absent".into());
    };
    if same.len() != peers.len() {
        return (
            None,
            format!(
                "{} unlinked allocations named {short} for {} functions",
                same.len(),
                peers.len()
            ),
        );
    }
    (
        Some(same[peer]),
        format!("occurrence {} of {short}", peer + 1),
    )
}
pub fn similar_allocations(trace: &Trace, symbol: &str) -> Vec<String> {
    let short = short_name(symbol).to_lowercase();
    let found: BTreeSet<_> = trace
        .allocations
        .iter()
        .filter(|a| {
            a.before.function.to_lowercase().contains(&short)
                || a.before.link.to_lowercase().contains(&short)
        })
        .map(|a| {
            if !a.before.link.is_empty() && a.before.link != a.before.function {
                format!("{} ({})", a.before.function, a.before.link)
            } else {
                a.before.function.clone()
            }
        })
        .collect();
    if found.is_empty() {
        trace
            .allocations
            .iter()
            .map(|a| a.before.function.clone())
            .collect::<BTreeSet<_>>()
            .into_iter()
            .take(30)
            .collect()
    } else {
        found.into_iter().collect()
    }
}
pub fn analyze(diff: &Value, trace: &Trace, version: Version, query: &str) -> Result<Value> {
    let symbols = function_symbols(&diff["right"]);
    let symbol=select_symbol(&symbols,query).or_else(|error|{let missing=function_symbols(&diff["left"]).into_iter().find(|s|s["target_symbol"].is_null()&&(s["name"]==query||s["demangled_name"]==query||short_name(text(&s["name"]))==query));if let Some(s)=missing{bail!("{query:?} exists only in the target (ours has no counterpart): {}. Identify the function or weak instance that should emit it; scripts/decomp-diff.py -d shows its body.",s["demangled_name"].as_str().unwrap_or(text(&s["name"])));}Err(error)})?;
    let target = symbol["target_symbol"]
        .as_u64()
        .and_then(|i| array(&diff["left"]["symbols"]).get(i as usize));
    let name = text(&symbol["name"]);
    let order: Vec<_> = symbols
        .iter()
        .map(|s| s["name"].as_str().unwrap_or("").to_owned())
        .collect();
    let (index, how) = select_allocation(trace, name, &order);
    let index = index.with_context(|| {
        format!(
            "no trace allocation for {name} ({how}); traced functions with similar names:\n{}",
            similar_allocations(trace, name)
                .iter()
                .map(|n| format!("  {n}"))
                .collect::<Vec<_>>()
                .join("\n")
        )
    })?;
    let analysis = allocation::analyze(&trace.allocations[index], version);
    let pairs = pair_registers(
        target.map_or(&[], |t| array(&t["instructions"])),
        array(&symbol["instructions"]),
    );
    let mut explanations = vec![];
    for (class, info) in analysis["classes"].as_object().into_iter().flatten() {
        let mut entries: Vec<_> = array(&info["vregs"]).iter().collect();
        entries.sort_by_key(|v| (v["order"].as_i64().unwrap_or(1 << 30), number(&v["vreg"])));
        for entry in entries {
            if entry["in_pcode"] == false && entry["state"] == "colored" {
                continue;
            }
            explanations.push(json!({"class":class,"vreg":entry["vreg"],"entry":entry,"lines":report::register(class,entry).iter().map(|l|l.trim()).collect::<Vec<_>>()}));
        }
    }
    Ok(
        json!({"format_version":1,"symbol":symbol,"target":target,"allocation_index":index,"selection":how,"analysis":analysis,"differences":pairs,"explanations":explanations}),
    )
}
fn holders(analysis: &Value, class: &str, physical: i64) -> Vec<i64> {
    let mut out: Vec<_> = array(&analysis["classes"][class]["vregs"])
        .iter()
        .filter(|e| {
            number(&e["vreg"]) >= 32
                && (e["phys"] == physical
                    || array(&e["phys"]).iter().any(|p| number(p) == physical))
        })
        .map(|e| number(&e["vreg"]))
        .collect();
    out.sort();
    out
}
fn vregs(values: &[i64]) -> String {
    values
        .iter()
        .map(|v| format!("%{v}"))
        .collect::<Vec<_>>()
        .join(" ")
}
fn pushed(entry: &Value) -> String {
    let p = &entry["pushed"];
    if p.is_null() {
        "not traced".into()
    } else if p["why"] == "degree" {
        format!("degree {} < K", number(&p["degree"]))
    } else {
        format!("spill candidate (cost {})", p["cost"])
    }
}
pub fn report(document: &Value, full: bool, inlining_requested: bool) -> Vec<String> {
    if document["target"].is_null() {
        return vec![];
    }
    let analysis = &document["analysis"];
    let differences = &document["differences"];
    let pairs = array(&differences["registers"]);
    let slots = array(&differences["slots"]);
    let other = array(&differences["other"]);
    let mut out = vec![];
    let entries: BTreeMap<_, _> = analysis["classes"]
        .as_object()
        .into_iter()
        .flatten()
        .flat_map(|(class, info)| {
            array(&info["vregs"])
                .iter()
                .map(move |e| ((class.as_str(), number(&e["vreg"])), e))
        })
        .collect();
    if !slots.is_empty() {
        out.push("\n== stack slot differences (r1 offsets, ours -> target) ==".into());
        for slot in slots {
            let hex = |v: &Value| {
                let n = number(v);
                if n < 0 {
                    format!("-{:x}", n.unsigned_abs())
                } else {
                    format!("{n:x}")
                }
            };
            out.push(format!(
                "   0x{} -> 0x{}  x{}",
                hex(&slot["ours"]),
                hex(&slot["target"]),
                number(&slot["count"])
            ));
        }
        let frame = report::frame(analysis);
        if !frame.is_empty() {
            out.push("   our frame, as the oracle traced it (r1 offset = locals offset + 8 + outgoing arguments):".into());
            out.extend(frame.iter().skip(1).map(|l| format!("   {l}")));
        }
        out.push("   (the layout follows the order locals and temporaries are declared and created; reorder them or change a type/size and remeasure)".into());
    }
    if !other.is_empty() {
        out.push(format!(
            "\n== other differing rows: {} (ours | target) ==",
            other.len()
        ));
        for pair in other.iter().take(12) {
            out.push(format!("   {:<36} | {}", text(&pair[0]), text(&pair[1])));
        }
        if other.len() > 12 {
            out.push(format!(
                "   ... {} more (scripts/decomp-diff.py -d shows them all)",
                other.len() - 12
            ));
        }
        out.push("   (rows the allocator cannot explain: instruction choice or scheduling; compare the source shape)".into());
        if !inlining_requested
            && other
                .iter()
                .flat_map(array)
                .any(|v| text(v).starts_with("bl "))
        {
            out.push("   a call differs: rerun with --inlining for the cost of each call against the threshold".into());
        }
    }
    out.push("\n== register differences (ours -> target, aligned rows) ==".into());
    if pairs.is_empty() {
        out.push("   none".into());
    }
    let mut involved = BTreeSet::new();
    let mut sorted_pairs: Vec<_> = pairs.iter().collect();
    sorted_pairs.sort_by_key(|p| (text(&p["class"]), number(&p["ours"]), number(&p["target"])));
    for pair in sorted_pairs {
        let class = text(&pair["class"]);
        let (mine, theirs) = (number(&pair["ours"]), number(&pair["target"]));
        let prefix = if class == "GPR" { 'r' } else { 'f' };
        let one = holders(analysis, class, mine);
        let two = holders(analysis, class, theirs);
        out.push(format!("   {prefix}{mine} -> {prefix}{theirs}  x{}   ours {prefix}{mine}: {};  ours {prefix}{theirs}: {}",number(&pair["count"]),if one.is_empty(){"no virtual register (fixed/parameter)".into()}else{vregs(&one)},if two.is_empty(){"none".into()}else{vregs(&two)}));
        involved.extend(one.into_iter().chain(two).map(|v| (class, v)));
    }
    let swaps: Vec<_> = pairs
        .iter()
        .filter(|p| {
            number(&p["ours"]) < number(&p["target"])
                && pairs.iter().any(|q| {
                    q["class"] == p["class"] && q["ours"] == p["target"] && q["target"] == p["ours"]
                })
        })
        .collect();
    if !swaps.is_empty() {
        out.push("\n== swaps ==".into());
    }
    for pair in swaps {
        let class = text(&pair["class"]);
        let (a, b) = (number(&pair["ours"]), number(&pair["target"]));
        let prefix = if class == "GPR" { 'r' } else { 'f' };
        let va = holders(analysis, class, a);
        let vb = holders(analysis, class, b);
        if va.is_empty() || vb.is_empty() {
            out.push(format!("   {prefix}{a} <-> {prefix}{b}: {prefix}{} is a fixed/parameter register in ours; the target computes into the other one, so look at which value is moved into/out of the argument register.",if va.is_empty(){a}else{b}));
            continue;
        }
        out.push(format!("   {prefix}{a} <-> {prefix}{b}: ours gives {prefix}{a} to {} and {prefix}{b} to {}; the target has them the other way round.",vregs(&va),vregs(&vb)));
        if va.len() == 1 && vb.len() == 1 {
            let null = Value::Null;
            let ea = entries.get(&(class, va[0])).copied().unwrap_or(&null);
            let eb = entries.get(&(class, vb[0])).copied().unwrap_or(&null);
            if ea.get("order").is_none() || eb.get("order").is_none() {
                continue;
            }
            let (first, second) = if number(&ea["order"]) < number(&eb["order"]) {
                (ea, eb)
            } else {
                (eb, ea)
            };
            out.push(format!("      Traced: %{} was coloured first (#{}, {}) and took the lower free register; %{} came later (#{}, {}).",number(&first["vreg"]),number(&first["order"])+1,pushed(first),number(&second["vreg"]),number(&second["order"])+1,pushed(second)));
            let order = array(&analysis["classes"][class]["order"]);
            if order.windows(2).all(|p| number(&p[0]) >= number(&p[1])) {
                out.push(format!("      This class was coloured in descending virtual register order (creation order, newest first), so the exchange needs the value of %{} created after that of %{}: reorder the statements, temporaries or declarations that create them.",number(&second["vreg"]),number(&first["vreg"])));
            } else {
                out.push(format!("      The simplification removed %{} earlier than %{} (it colours the last removed first); the exchange needs that reversed: a later creation, or more interference (a longer life) for %{}, or less for %{}.",number(&second["vreg"]),number(&first["vreg"]),number(&second["vreg"]),number(&first["vreg"])));
            }
        }
    }
    let explanations: BTreeMap<_, _> = array(&document["explanations"])
        .iter()
        .map(|e| ((text(&e["class"]), number(&e["vreg"])), e))
        .collect();
    if full || !involved.is_empty() {
        out.push("\n== virtual registers ==".into());
        let keys: BTreeSet<_> = if full {
            explanations.keys().copied().collect()
        } else {
            involved
        };
        for key in keys {
            let defined = entries
                .get(&key)
                .and_then(|e| e.get("def"))
                .map_or("?".into(), |d| {
                    format!(
                        "block {}{}: {}",
                        number(&d["block"]),
                        if number(&d["line"]) != 0 {
                            format!(" line {}", number(&d["line"]))
                        } else {
                            String::new()
                        },
                        text(&d["instruction"])
                    )
                });
            out.push(format!("   %{} ({}) defined at {defined}", key.1, key.0));
            if let Some(e) = explanations.get(&key) {
                out.extend(
                    array(&e["lines"])
                        .iter()
                        .skip(1)
                        .map(|l| format!("      {}", text(l))),
                );
            }
        }
    }
    let mut replay = vec![];
    let mut order = vec![];
    for (class, info) in analysis["classes"].as_object().into_iter().flatten() {
        if info.get("select_replayed").is_some() {
            replay.push(format!(
                "{class}: select replay {}/{}, simplify replay {}",
                number(&info["select_replay_agrees"]),
                number(&info["select_replayed"]),
                if info["simplify_replay_agrees"] == true {
                    "agrees"
                } else {
                    "DIFFERS"
                }
            ));
        }
        if info.get("k").is_some() && !array(&info["order"]).is_empty() {
            let regs = array(&info["order"]);
            order.push(format!(
                "colouring order (traced; first coloured first): {}{}",
                regs.iter()
                    .take(40)
                    .map(|v| format!("%{}", number(v)))
                    .collect::<Vec<_>>()
                    .join(" "),
                if regs.len() > 40 {
                    format!(" ... {} more", regs.len() - 40)
                } else {
                    String::new()
                }
            ));
        }
    }
    if !order.is_empty() || !replay.is_empty() {
        out.push("\n== colouring order (traced) ==".into());
        out.extend(replay.into_iter().chain(order).map(|l| format!("   {l}")));
    }
    out
}
