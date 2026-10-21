//! Human-readable renderers kept independent of compilation and analysis.
use crate::trace::Snapshot;
use serde_json::Value;
use std::collections::BTreeMap;

fn n(v: &Value, key: &str) -> i64 {
    v[key].as_i64().unwrap_or(0)
}
fn s<'a>(v: &'a Value, key: &str) -> &'a str {
    v[key].as_str().unwrap_or("")
}
fn array(v: &Value) -> &[Value] {
    v.as_array().map(Vec::as_slice).unwrap_or(&[])
}
fn show(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        Value::Null => "None".into(),
        Value::Bool(v) => if *v { "True" } else { "False" }.into(),
        _ => v.to_string(),
    }
}
fn reg(class: &str, number: i64) -> String {
    format!(
        "{}{number}",
        match class {
            "GPR" => "r",
            "FPR" => "f",
            "VR" => "v",
            "CR" => "cr",
            _ => "r",
        }
    )
}

pub fn mask_text(class: &str, mask: u32) -> String {
    let mut parts = vec![];
    let mut run = vec![];
    for c in 0..=32 {
        if c < 32 && mask & (1u32 << c) != 0 {
            run.push(c);
            continue;
        }
        if !run.is_empty() {
            parts.push(if run.len() == 1 {
                reg(class, run[0])
            } else {
                format!(
                    "{}..{}",
                    reg(class, run[0]),
                    reg(class, *run.last().unwrap())
                )
            });
            run.clear();
        }
    }
    if parts.is_empty() {
        "none".into()
    } else {
        parts.join(", ")
    }
}

fn taken_text(class: &str, taken: &Value) -> String {
    let mut parts = vec![];
    let mut fixed = vec![];
    fn flush(class: &str, fixed: &mut Vec<i64>, parts: &mut Vec<String>) {
        if !fixed.is_empty() {
            parts.push(if fixed.len() == 1 {
                reg(class, fixed[0])
            } else {
                format!(
                    "{}..{}",
                    reg(class, fixed[0]),
                    reg(class, *fixed.last().unwrap())
                )
            });
            fixed.clear();
        }
    }
    let values: BTreeMap<i64, &Value> = taken
        .as_object()
        .into_iter()
        .flatten()
        .filter_map(|(k, v)| k.parse().ok().map(|k| (k, v)))
        .collect();
    for (c, who) in values {
        let virtual_: Vec<_> = array(who)
            .iter()
            .filter_map(Value::as_i64)
            .filter(|v| *v >= 32)
            .collect();
        if virtual_.is_empty() {
            if fixed.last().is_some_and(|last| *last != c - 1) {
                flush(class, &mut fixed, &mut parts);
            }
            fixed.push(c);
            continue;
        }
        flush(class, &mut fixed, &mut parts);
        let label = virtual_
            .iter()
            .map(|v| format!("%{v}"))
            .collect::<Vec<_>>()
            .join(",");
        parts.push(format!(
            "{}({label}{})",
            reg(class, c),
            if virtual_.len() != array(who).len() {
                ",fixed"
            } else {
                ""
            }
        ));
    }
    flush(class, &mut fixed, &mut parts);
    parts.join(" ")
}

pub fn allocation(a: &Value) -> Vec<String> {
    let mut out = vec![
        format!(
            "== {} ({}): register allocation ==",
            s(a, "function"),
            s(a, "link")
        ),
        format!(
            "   {} blocks, {} instructions; the allocator removed {} and inserted {}",
            n(a, "blocks"),
            n(a, "instructions"),
            array(&a["removed"]).len(),
            array(&a["inserted"]).len()
        ),
    ];
    for (class, info) in a["classes"].as_object().into_iter().flatten() {
        let vregs = array(&info["vregs"]);
        out.push(format!("-- {class}: {} virtual registers", vregs.len()));
        if info.get("k").is_some() {
            let failed = &array(&info["rounds"])[..array(&info["rounds"]).len().saturating_sub(1)];
            let earlier = if failed.is_empty() {
                String::new()
            } else {
                format!(
                    "{} earlier colouring(s) failed and spilled {}; ",
                    failed.len(),
                    failed
                        .iter()
                        .flat_map(|r| array(&r["spilled"]))
                        .map(|v| format!("%{}", show(v)))
                        .collect::<Vec<_>>()
                        .join(", ")
                )
            };
            out.push(format!("   K={} colours, the class's mask {}; {earlier}select replayed: {} of {} agree{}",n(info,"k"),mask_text(class,n(info,"mask") as u32),n(info,"select_replay_agrees"),n(info,"select_replayed"),if info["simplify_replay_agrees"]==true{""}else{" (the simplification's replay gives another order: the pushes below are approximate)"}));
            let order = array(&info["order"]);
            out.push(if order.is_empty() {
                "   nothing was left to colour (every node was coalesced)".into()
            } else {
                format!(
                    "   colouring order (traced; first coloured first): {}{}",
                    order
                        .iter()
                        .take(40)
                        .map(|v| format!("%{}", show(v)))
                        .collect::<Vec<_>>()
                        .join(" "),
                    if order.len() > 40 {
                        format!(" ... {} more", order.len() - 40)
                    } else {
                        String::new()
                    }
                )
            });
        } else {
            out.push("   (no colouring trace for this class)".into());
        }
        let unused = |v: &&Value| v["in_pcode"] == false && v["state"] == "colored";
        let unused_nodes: Vec<_> = vregs.iter().filter(unused).collect();
        if !unused_nodes.is_empty() {
            out.push(format!("   {} graph nodes no instruction uses any more (each took the lowest free colour): {}",unused_nodes.len(),unused_nodes.iter().map(|v|format!("%{}",n(v,"vreg"))).collect::<Vec<_>>().join(" ")));
        }
        let mut used: Vec<_> = vregs.iter().filter(|v| !unused(v)).collect();
        used.sort_by_key(|v| (v["order"].as_i64().unwrap_or(1 << 30), n(v, "vreg")));
        for v in used {
            out.extend(register(class, v));
        }
    }
    for (key, heading, limit) in [
        ("removed", "-- removed (coalesced or dead):", 12),
        (
            "inserted",
            "-- inserted by the allocator (spill code and the like):",
            16,
        ),
    ] {
        let entries = array(&a[key]);
        if entries.is_empty() {
            continue;
        }
        out.push(heading.into());
        out.extend(
            entries
                .iter()
                .take(limit)
                .map(|t| format!("   {}", show(t))),
        );
        if entries.len() > limit {
            out.push(format!("   ... {} more", entries.len() - limit));
        }
    }
    out
}

/// A single virtual register and the reasons derived from its structured allocation data.
pub fn register(class: &str, v: &Value) -> Vec<String> {
    let mut out = vec![];
    let phys = v["phys"].as_i64();
    let state = match s(v, "state") {
        "spilled" => "SPILLED (kept in memory; every use and def was rewritten)".into(),
        "coalesced" => format!(
            "-> {} (coalesced{})",
            phys.map_or("?".into(), |p| reg(class, p)),
            v["coalesced_into"]
                .as_i64()
                .map_or(String::new(), |p| format!(
                    " into {}",
                    if p < 32 {
                        reg(class, p)
                    } else {
                        format!("%{p}")
                    }
                ))
        ),
        "split" => format!(
            "-> {} (split by spilling)",
            array(&v["phys"])
                .iter()
                .map(|p| reg(class, p.as_i64().unwrap_or(0)))
                .collect::<Vec<_>>()
                .join(" / ")
        ),
        _ => format!(
            "-> {}{}",
            reg(class, phys.unwrap_or(0)),
            if v["in_pcode"] == false {
                " (no instruction uses it)"
            } else {
                ""
            }
        ),
    };
    let mut line = format!(
        "   %{:<4} {state}   {} defs, {} uses, live in {} blocks",
        n(v, "vreg"),
        n(v, "defs"),
        n(v, "uses"),
        n(v, "live_blocks")
    );
    if let Some(d) = v.get("def") {
        line += &format!(
            "; defined in block {}{}: {}",
            n(d, "block"),
            if n(d, "line") > 0 {
                format!(" line {}", n(d, "line"))
            } else {
                String::new()
            },
            s(d, "instruction")
        );
    }
    out.push(line);
    let mut reasons = vec![];
    if n(v, "across_call") != 0 {
        reasons.push(format!("live across {} call(s) (first in block {}): the registers a call clobbers are its neighbours",n(v,"across_call"),n(v,"first_call_block")));
    }
    if let Some(p) = v.get("pushed") {
        reasons.push(if p["why"]=="degree"{format!("coloured #{}: the simplification took it when its degree was {} < K={}",n(v,"order")+1,n(p,"degree"),n(p,"k"))}
        else{format!("coloured #{}: no node had a degree below K={}; it was the spill candidate (spill cost {} / degree {} the lowest)",n(v,"order")+1,n(p,"k"),n(p,"cost"),n(p,"degree"))});
    }
    if let Some(selected) = v.get("select") {
        let taken = &selected["taken"];
        if taken.as_object().is_some_and(|o| !o.is_empty()) {
            reasons
                .push("colours its neighbours held then: ".to_owned() + &taken_text(class, taken));
        }
        if class == "GPR"
            && array(&taken["0"]).contains(&Value::from(0))
            && n(v, "across_call") == 0
        {
            reasons.push("r0 is a neighbour: r0 cannot be a base register (an address operand of r0 reads as 0), so registers used as addresses interfere with it".into());
        }
        reasons.push(match s(selected,"how"){
            "lowest free"=>format!("free in the mask: {}; the lowest is {}",mask_text(class,n(selected,"free") as u32),reg(class,n(selected,"predicted"))),
            "claimed"=>format!("no colour of the mask was free: {} was claimed (a non-volatile register, saved in the prologue; it joins the mask from here on)",reg(class,n(selected,"color"))),
            _=>"no colour was free and none could be claimed: spilled, and the class coloured again".into(),
        });
        if selected["agrees"] != true {
            reasons.push(format!(
                "REPLAY DISAGREES: the replay gives {}, the trace {}",
                reg(class, n(selected, "predicted")),
                reg(class, n(selected, "color"))
            ));
        }
    }
    if v["spilled_in_earlier_round"] == true {
        reasons.push("spilled by an earlier colouring of the class".into());
    }
    out.extend(
        reasons
            .into_iter()
            .map(|r| format!("          because: {r}")),
    );
    out
}

pub fn frame(a: &Value) -> Vec<String> {
    let Some(entries) = a["frame"].as_array() else {
        return vec![];
    };
    let layout = &a["layout"];
    let mut out = vec![format!("== {}: stack frame ==", s(a, "function"))];
    if !layout.is_null() {
        out.push(format!("   frame {} bytes before the register saves: linkage {}, outgoing arguments {}, locals {}{}",n(layout,"size"),n(layout,"linkage"),n(layout,"outgoing"),n(layout,"locals"),if n(layout,"above")!=0{format!(", above them {}",n(layout,"above"))}else{String::new()}));
    }
    let mut memory: Vec<_> = entries
        .iter()
        .filter(|e| e["used"] == true && n(e, "reg") == 0)
        .collect();
    memory.sort_by_key(|e| (n(e, "offset"), s(e, "kind")));
    if !memory.is_empty() {
        out.push(
            if layout.is_null() {
                "   in memory (offset in the locals area, size, name):"
            } else {
                "   in memory (offset from r1, offset in the locals area, size, name):"
            }
            .into(),
        );
        for e in &memory {
            out.push(format!(
                "      {}{:>5} {:>5}  {}{}",
                e.get("r1")
                    .map_or(String::new(), |v| format!("{:>5} ", show(v))),
                n(e, "offset"),
                n(e, "size"),
                s(e, "name"),
                if e["kind"] == "temp" {
                    " (temporary)"
                } else {
                    ""
                }
            ));
        }
    }
    let registers: Vec<_> = entries
        .iter()
        .filter(|e| e["used"] == true && n(e, "reg") != 0)
        .collect();
    if !registers.is_empty() {
        out.push(format!(
            "   in registers: {}",
            registers
                .iter()
                .map(|e| format!("{}=reg {}", s(e, "name"), n(e, "reg")))
                .collect::<Vec<_>>()
                .join(", ")
        ));
    }
    if memory.is_empty() && registers.is_empty() {
        out.push("   nothing in the frame".into());
    }
    out
}

pub fn inlining(decisions: &[Value], links: bool) -> Vec<String> {
    let mut out = vec![];
    for d in decisions {
        let mut times = if n(d, "count") > 1 {
            format!("  (x{})", n(d, "count"))
        } else {
            String::new()
        };
        for field in ["depths", "lines"] {
            let mut numbers: Vec<_> = array(&d[field]).iter().filter_map(Value::as_i64).collect();
            if numbers.is_empty() || (field == "depths" && numbers.iter().all(|v| *v == 0)) {
                continue;
            }
            numbers.sort();
            times += &format!(
                "  ({} {})",
                if field == "depths" { "depth" } else { "line" },
                numbers
                    .iter()
                    .map(i64::to_string)
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }
        out.push(if d["event"]=="decision"{
            format!("   {}: {} - {}{times}{}{}",s(d,"callee"),if d["inline"]==true{"INLINE"}else{"call kept"},s(d,"reason"),if links&&!s(d,"link").is_empty(){format!("\n      [{}]",s(d,"link"))}else{String::new()},
                if links&&!array(&d["chain"]).is_empty(){format!("\n      inside: {}",array(&d["chain"]).iter().map(show).collect::<Vec<_>>().join(" > "))}else{String::new()})
        }else if d["expanded"]==true{format!("      {}: expanded in place{times}",s(d,"callee"))}
        else{format!("      {}: could not be expanded here (its body is not an expression-only body at this stage){times}",s(d,"callee"))});
    }
    out
}

/// The PCode before the allocation; FLAGS: each instruction's flags too (`; fIsRead fIsConst ...`).
pub fn pcode(before: &Snapshot, names: &BTreeMap<i32, String>, flags: bool) -> Vec<String> {
    let mut out = vec![format!(
        "== {}: PCode before the allocation ==",
        before.function
    )];
    for b in &before.blocks {
        let successors = b
            .successors
            .iter()
            .map(|s| {
                before
                    .blocks
                    .iter()
                    .find(|b| b.pointer == *s)
                    .map_or_else(|| s.clone(), |b| b.index.to_string())
            })
            .collect::<Vec<_>>()
            .join(", ");
        out.push(format!(
            "   block {} (weight {}) -> {}",
            b.index,
            b.weight,
            if successors.is_empty() {
                "exit"
            } else {
                &successors
            }
        ));
        for i in &b.instructions {
            let mut operands = i.operands.iter().map(|o| o.text()).collect::<Vec<_>>();
            if operands.len() > 12 {
                let n = operands.len() - 3;
                operands.truncate(3);
                operands.push(format!("... {n} more (the registers the call clobbers)"));
            }
            let mut line = format!(
                "      {} {}",
                names
                    .get(&i.opcode)
                    .cloned()
                    .unwrap_or_else(|| format!("op{}", i.opcode)),
                operands.join(", ")
            );
            if flags {
                line.push_str(&format!(
                    "  ; {}",
                    mwcc_trace::records::flag_names(i.flags).join(" ")
                ));
            }
            out.push(line);
        }
    }
    out
}

pub fn schedule(function: &str, schedule: &[Value]) -> Vec<String> {
    let mut out = vec![format!("== {function}: scheduling ==")];
    for b in schedule {
        let moved = array(&b["picks"])
            .iter()
            .enumerate()
            .filter(|(n, p)| p["position"].as_u64() != Some(*n as u64))
            .count();
        out.push(format!("-- block {} ({} the allocation): {} instructions in {} cycles, {moved} moved; select replayed: {} of {} picks agree",n(b,"block"),if b["virtual"]==true{"before"}else{"after"},n(b,"instructions"),n(b,"cycles"),n(b,"replay_agrees"),n(b,"replayed")));
        for p in array(&b["picks"]) {
            out.push(format!(
                "   cycle {:>3}: #{:<3} {}{}{}",
                n(p, "cycle"),
                show(&p["position"]),
                s(p, "instruction"),
                if n(p, "line") > 0 {
                    format!("   (line {})", n(p, "line"))
                } else {
                    String::new()
                },
                if p["agrees"] == true {
                    ""
                } else {
                    "   REPLAY DISAGREES"
                }
            ));
            for o in array(&p["over"]) {
                out.push(format!(
                    "                 over #{}: {}",
                    show(&o["position"]),
                    s(o, "why")
                ));
            }
        }
    }
    out
}

pub fn tree(paths: &[Value]) -> Vec<String> {
    let mut paths: Vec<Vec<String>> = paths
        .iter()
        .map(|p| array(p).iter().map(show).collect())
        .collect();
    paths.sort();
    let mut out = vec![];
    let mut previous: Vec<String> = vec![];
    for path in paths {
        let mut common = 0;
        while common < path.len().min(previous.len()).saturating_sub(1)
            && path[common] == previous[common]
        {
            common += 1;
        }
        out.push(format!(
            "{}{}{}",
            "  ".repeat(common),
            if common > 0 { "> " } else { "" },
            path[common..].join(" > ")
        ));
        previous = path;
    }
    out
}

pub fn sensitivity(term: &Value) -> String {
    format!(
        "{}  [{}: {:+}]{}{}",
        array(&term["path"])
            .iter()
            .map(show)
            .collect::<Vec<_>>()
            .join(" > "),
        if term["kind"] == "body" {
            "its body is costed; as a plain call"
        } else {
            "costed as a call (no body yet here); with its body"
        },
        n(term, "delta"),
        if term["flips"] == true {
            "  FLIPS THE DECISION"
        } else {
            ""
        },
        if n(term, "count") > 1 {
            format!("  (x{})", n(term, "count"))
        } else {
            String::new()
        }
    )
}

pub fn near(decisions: &[Value], margin: i64) -> Vec<String> {
    let mut out = vec![format!(
        "== inline decisions within {margin} of the threshold (cost, threshold, decision, callee link name, in which function) =="
    )];
    for decision in decisions {
        let mut lines = String::new();
        for field in ["lines", "depths"] {
            let mut values: Vec<_> = array(&decision[field]).iter().map(show).collect();
            values.sort_by_key(|v| v.parse::<i64>().unwrap_or(0));
            if !values.is_empty() {
                lines += &format!(
                    "  {} {}",
                    if field == "lines" { "line" } else { "depth" },
                    values.join(", ")
                );
            }
        }
        let mut whole = String::new();
        if decision.get("full_cost").is_some() && decision["full_cost"] != decision["cost"] {
            whole += &format!(
                "  (whole cost {}{})",
                n(decision, "full_cost"),
                if decision["depth_limited"] == true {
                    ", DEPTH LIMIT"
                } else {
                    ""
                }
            );
        }
        if !array(&decision["chain"]).is_empty() {
            whole += &format!(
                "\n          inside: {}",
                array(&decision["chain"])
                    .iter()
                    .map(show)
                    .collect::<Vec<_>>()
                    .join(" > ")
            );
        }
        if !array(&decision["limit_paths"]).is_empty() {
            whole += "\n          at the depth limit [limit +1]:";
            for line in tree(array(&decision["limit_paths"])) {
                whole += &format!("\n             {line}");
            }
        }
        for term in array(&decision["sensitivity"]).iter().take(4) {
            whole += &format!("\n          sensitive: {}", sensitivity(term));
        }
        if array(&decision["sensitivity"]).len() > 4 {
            whole += &format!(
                "\n          ... {} more sensitive terms (--json: near[].sensitivity)",
                array(&decision["sensitivity"]).len() - 4
            );
        }
        out.push(format!(
            "   {:>4} {:>4}  {:<7}  {}  in {}{lines}{}{whole}",
            n(decision, "cost"),
            n(decision, "threshold"),
            s(decision, "outcome").to_uppercase(),
            s(decision, "link"),
            if decision["function"].is_null() {
                "(after the last function)"
            } else {
                s(decision, "function")
            },
            if n(decision, "count") > 1 {
                format!("  (x{})", n(decision, "count"))
            } else {
                String::new()
            }
        ));
    }
    if decisions.is_empty() {
        out.push("   none".into());
    }
    out
}

pub fn cost(decision: &Value, types: &BTreeMap<i64, String>) -> Vec<String> {
    let Some(b) = decision.get("breakdown").filter(|v| !v.is_null()) else {
        return vec![];
    };
    let mut out = vec![];
    if n(b, "running") != 0 {
        out.push(format!(
            "         {:>4}  the running cost when this body was reached",
            n(b, "running")
        ));
    }
    out.push(format!(
        "         {:>4}  one per statement ({})",
        n(b, "nstmts"),
        n(b, "nstmts")
    ));
    fn walk(nodes: &[Value], indent: usize, types: &BTreeMap<i64, String>, out: &mut Vec<String>) {
        for node in nodes {
            if node["what"] == "body" {
                let of = node["of"].as_str();
                let plain = if of.is_some_and(|v| !v.is_empty()) {
                    format!("  [as a plain call: {:+}]", 1 - n(node, "cost"))
                } else {
                    String::new()
                };
                out.push(format!(
                    "         {:>4}  {}the body of {}: {} statement(s){plain}, and:",
                    n(node, "cost"),
                    "  ".repeat(indent),
                    of.unwrap_or("a callee"),
                    n(node, "nstmts")
                ));
                walk(array(&node["parts"]), indent + 1, types, out);
                continue;
            }
            let mut call = if !s(node, "link").is_empty() {
                format!(
                    "  [{}{}]",
                    s(node, "link"),
                    if node["implicit"] == true {
                        ", compiler-made"
                    } else {
                        ""
                    }
                )
            } else {
                String::new()
            };
            for c in array(&node["calls"]) {
                match c[0].as_str() {
                    Some("!") => {
                        call += &format!("  [limit +1: {} AT THE INLINE DEPTH LIMIT]", show(&c[1]))
                    }
                    Some("?") => {
                        call += &format!("  [{}: no body yet, costed as a call]", show(&c[1]))
                    }
                    _ => (),
                }
            }
            out.push(format!(
                "         {:>4}  {}{}: {}{call}",
                n(node, "cost"),
                "  ".repeat(indent),
                types
                    .get(&n(node, "type"))
                    .cloned()
                    .unwrap_or_else(|| n(node, "type").to_string()),
                s(node, "what")
            ));
            walk(array(&node["parts"]), indent + 1, types, out);
        }
    }
    walk(array(&b["statements"]), 0, types, &mut out);
    let total = n(b, "running")
        + n(b, "nstmts")
        + array(&b["statements"])
            .iter()
            .map(|v| n(v, "cost"))
            .sum::<i64>();
    if decision.get("full_cost").is_some() {
        out.push(format!("         {total:>4}  in all, the whole cost (the compiler's estimate: {}), against the threshold {}{}",n(decision,"cost"),show(&decision["threshold"]),if decision["depth_limited"]==true{"; a call at the inline depth limit (adding nothing here) keeps it anyway"}else{""}));
    } else {
        out.push(format!(
            "         {total:>4}  in all, against the threshold {}{}",
            show(&decision["threshold"]),
            if total
                > decision["threshold"]
                    .as_i64()
                    .filter(|v| *v != 0)
                    .unwrap_or(1 << 30)
            {
                " (the estimate stops once over it; --full-cost goes on)"
            } else {
                ""
            }
        ));
    }
    out
}

pub fn instantiation(events: &[Value], query: &str) -> Vec<String> {
    let mut out = vec![format!(
        "== when the bodies of {query} came to be, and the decisions about calling them (trace line, event) =="
    )];
    let queue_kind = |e: &Value| match n(e, "queue_kind") {
        0 => "an inline definition".into(),
        1 => "a template class member".into(),
        2 => "a template function instance".into(),
        3 => "a compiler-made method".into(),
        v => v.to_string(),
    };
    for e in events {
        if !s(e, "link").contains(query) {
            continue;
        }
        let where_ = ["from", "expanding"]
            .iter()
            .filter_map(|k| e.get(k).map(|v| format!(" {k}={}", show(v))))
            .collect::<Vec<_>>()
            .join("");
        let what = match s(e, "event") {
            "queue" => format!(
                "queued for parsing: {}{}",
                queue_kind(e),
                if e["new"] == false {
                    " (already queued)"
                } else {
                    ""
                }
            ),
            "need" => format!(
                "the inliner asked for its body{}",
                if e["result"] == true {
                    " (queued)"
                } else {
                    " (nothing to do)"
                }
            ),
            "parse" => format!(
                "parsed ({}): {}",
                queue_kind(e),
                if e["body"] == true {
                    "its body is there for the inliner"
                } else {
                    "no inline body"
                }
            ),
            "func" => "template function instance compiled".into(),
            "gen" => "code generated from its saved body".into(),
            "emit" => format!(
                "to be emitted: {}",
                e["why"].as_str().unwrap_or("referenced")
            ),
            "compiled" => "compiled (allocated)".into(),
            "decision" => format!(
                "call {} in {}, round {}{}{}",
                if e["inline"] == true {
                    "INLINED"
                } else {
                    "kept"
                },
                e["in"].as_str().unwrap_or("(after the last function)"),
                n(e, "depth"),
                if e["body"] == true {
                    ""
                } else {
                    ", NO BODY yet"
                },
                if array(&e["chain"]).is_empty() {
                    String::new()
                } else {
                    format!(
                        ", inside {}",
                        array(&e["chain"])
                            .iter()
                            .map(show)
                            .collect::<Vec<_>>()
                            .join(" > ")
                    )
                }
            ),
            other => format!("unrecognized event {other}"),
        };
        out.push(format!(
            "   {:>7}  {what}{where_}  [{}]",
            n(e, "position"),
            s(e, "link")
        ));
    }
    if out.len() == 1 {
        out.push("   nothing".into());
    }
    out
}

pub fn exchange(a: &Value, first: &str, second: &str) -> Vec<String> {
    use regex::Regex;
    use std::collections::BTreeSet;
    use std::sync::LazyLock;
    static REGISTER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"^%?(r|f|v|cr)?(\d+)$").unwrap());
    let mut class = "GPR";
    let mut numbers = vec![];
    for text in [first, second] {
        let Some(c) = REGISTER.captures(text.trim()) else {
            return vec![format!(
                "--exchange: {text} is not a virtual register (45, r45, %r45, f33)"
            )];
        };
        if let Some(prefix) = c.get(1) {
            class = match prefix.as_str() {
                "r" => "GPR",
                "f" => "FPR",
                "v" => "VR",
                _ => "CR",
            };
        }
        let Ok(n) = c[2].parse::<i64>() else {
            return vec![format!(
                "--exchange: {class} register is outside the trace's range"
            )];
        };
        numbers.push(n);
    }
    let info = &a["classes"][class];
    if info.is_null() {
        return vec![format!(
            "--exchange: no {class} allocation in {}",
            s(a, "function")
        )];
    }
    let one = array(&info["vregs"])
        .iter()
        .find(|v| n(v, "vreg") == numbers[0]);
    let two = array(&info["vregs"])
        .iter()
        .find(|v| n(v, "vreg") == numbers[1]);
    let (Some(one), Some(two)) = (one, two) else {
        return vec![format!(
            "--exchange: {class} %{} or %{} is not in {}",
            numbers[0],
            numbers[1],
            s(a, "function")
        )];
    };
    let mut out = vec![format!(
        "== {}: {class} %{} and %{} ==",
        s(a, "function"),
        n(one, "vreg"),
        n(two, "vreg")
    )];
    for v in [one, two] {
        let pushed = &v["pushed"];
        let physical = if let Some(p) = v["phys"].as_i64() {
            reg(class, p)
        } else if v["phys"].is_array() {
            format!(
                "[{}]",
                array(&v["phys"])
                    .iter()
                    .map(show)
                    .collect::<Vec<_>>()
                    .join(", ")
            )
        } else {
            show(&v["phys"])
        };
        out.push(format!("   %{} -> {physical}: coloured #{}, degree {} in the graph, {} neighbours, {} defs, {} uses{}",n(v,"vreg"),v["order"].as_i64().unwrap_or(-1)+1,show(&v["degree"]),array(&v["neighbors"]).len(),n(v,"defs"),n(v,"uses"),pushed["cost"].as_i64().map_or(String::new(),|cost|format!(", spill cost {cost}"))));
        if !pushed.is_null() {
            let mut how = if pushed["why"] == "degree" {
                format!(
                    "pushed in simplification pass {} when its degree was {} < K={}",
                    pushed.get("pass").map_or("?".into(), show),
                    n(pushed, "degree"),
                    n(pushed, "k")
                )
            } else {
                format!(
                    "pushed as the spill candidate (cost {} / degree {} the lowest)",
                    n(pushed, "cost"),
                    n(pushed, "degree")
                )
            };
            if let Some(by) = pushed["unlocked_by"].as_i64() {
                how += &format!(", once %{by} was removed");
            }
            out.push(format!("      {how}"));
        }
        if let Some(selected) = v.get("select") {
            let taken = taken_text(class, &selected["taken"]);
            out.push(format!(
                "      select: neighbours held {}; free {}; {} -> {}",
                if taken.is_empty() { "nothing" } else { &taken },
                mask_text(class, n(selected, "free") as u32),
                s(selected, "how"),
                reg(class, n(selected, "color"))
            ));
        }
    }
    let set = |v: &Value| {
        array(&v["neighbors"])
            .iter()
            .map(|n| self::n(n, "vreg"))
            .collect::<BTreeSet<_>>()
    };
    let first_neighbors = set(one);
    let second_neighbors = set(two);
    let shared: Vec<_> = first_neighbors
        .intersection(&second_neighbors)
        .map(|n| {
            if *n >= 32 {
                format!("%{n}")
            } else {
                reg(class, *n)
            }
        })
        .collect();
    let interfere = first_neighbors.contains(&n(two, "vreg"));
    out.push(format!(
        "   they {}; common neighbours: {}",
        if interfere {
            "interfere"
        } else {
            "do not interfere"
        },
        if shared.is_empty() {
            "none".into()
        } else {
            shared.join(" ")
        }
    ));
    if let (Some(a), Some(b)) = (one["order"].as_i64(), two["order"].as_i64()) {
        let (early, late) = if a < b { (one, two) } else { (two, one) };
        let (pe, pl) = (&early["pushed"], &late["pushed"]);
        let ev = n(early, "vreg");
        let lv = n(late, "vreg");
        out.push(format!("   %{ev} is coloured before %{lv} because the simplification pushed it later (the stack is coloured from the top):"));
        if pe["pass"] == pl["pass"] && pe["why"] == "degree" && pl["why"] == "degree" {
            out.push(format!("      both were pushed in pass {}, which takes the nodes in increasing number: %{lv} first, %{ev} after it",show(&pe["pass"])));
            out.push(format!("      to swap them, %{lv} would need the higher number (be created after %{ev}: define it later), or one of them would have to be pushed in another pass (a different degree: interfere with one value more or fewer)"));
        } else {
            out.push(format!("      %{lv} was pushed in pass {} and %{ev} in pass {}: a node is pushed in the first pass in which its remaining degree is below K, so %{ev} needed more of its neighbours removed first",show(&pl["pass"]),show(&pe["pass"])));
            out.push(format!("      to swap them, change the degrees: make %{ev} interfere with fewer values, or %{lv} with more"));
        }
        if interfere && early["select"]["how"] == "lowest free" {
            out.push(format!("      as they interfere, %{ev} took the lowest free register and %{lv} had to take another"));
        }
    }
    out
}
