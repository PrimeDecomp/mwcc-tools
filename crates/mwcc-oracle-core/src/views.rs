//! The oracle's text views of a traced compile (what `oracle` printed): inlining, the allocation and frame reports,
//! and on request the near-miss inlining, instantiation, common subexpression, block, locals, exchange, inline-cost,
//! pcode and schedule views. Each is a list of lines, built from a schema-6 document ([`crate::document::build`])
//! and the trace it was built from.
use crate::{blocks, cse, document, locals, ppc, report, trace::Trace, version::Version};
use anyhow::{Context, Result};
use serde_json::Value;

/// The traced compile the document was built from.
pub struct Traced<'a> {
    pub trace: &'a Trace,
    pub version: Version,
    pub object: &'a [u8],
    /// Compiled with the original compiler (no oracle hooks).
    pub original_fallback: bool,
}

/// What to show (the first of `near`, `instantiation`, `cse`/`ir`, `blocks` and `locals` that is set is the only
/// view; otherwise each function's inlining (or `inline_cost`'s, or `exchange`'s), pcode, allocation, frame and
/// schedule).
#[derive(Default)]
pub struct Views<'a> {
    pub function: Option<&'a str>,
    pub link: Option<&'a str>,
    pub near: Option<i64>,
    pub instantiation: Option<&'a str>,
    pub cse: bool,
    pub ir: Option<&'a str>,
    pub blocks: bool,
    /// The cut limit the blocks view marks (none: unlimited).
    pub block_limit: Option<i32>,
    pub locals: bool,
    /// The target's asm text, for the locals view's frame and slot comparisons.
    pub target_asm: Option<&'a str>,
    pub exchange: Option<&'a str>,
    pub inline_cost: Option<&'a str>,
    pub pcode: bool,
    pub schedule: bool,
}

fn selected(views: &Views<'_>, link: &str) -> bool {
    views.function.is_none_or(|q| link.contains(q)) && views.link.is_none_or(|q| link == q)
}

/// The register of TEXT (`r5`, `%5`, `f1`, `cr2`, `v3`): its class and number.
fn register(text: &str) -> Option<(&'static str, u32)> {
    let text = text.trim();
    let text = text.strip_prefix('%').unwrap_or(text);
    let (class, digits) = if let Some(d) = text.strip_prefix("cr") {
        ("CR", d)
    } else if let Some(d) = text.strip_prefix('r') {
        ("GPR", d)
    } else if let Some(d) = text.strip_prefix('f') {
        ("FPR", d)
    } else if let Some(d) = text.strip_prefix('v') {
        ("VR", d)
    } else {
        ("GPR", text)
    };
    (!digits.is_empty() && digits.chars().all(|c| c.is_ascii_digit()))
        .then(|| digits.parse().ok().map(|n| (class, n)))
        .flatten()
}

/// The lines of DOCUMENT's views.
pub fn text(document: &Value, traced: &Traced<'_>, views: &Views<'_>) -> Result<Vec<String>> {
    let mut out = vec![];
    let parsed = traced.trace;
    let version = traced.version;
    if let Some(margin) = views.near {
        out.extend(report::near(
            document["near"].as_array().map_or(&[][..], Vec::as_slice),
            margin,
        ));
        return Ok(out);
    }
    if let Some(query) = views.instantiation {
        let events = document::instantiation(parsed, version)?;
        out.extend(report::instantiation(&events, query));
        return Ok(out);
    }
    if views.cse || views.ir.is_some() {
        let mut shown = false;
        for (link, function) in cse::parse(&parsed.records)? {
            if !function.passes.is_empty() && selected(views, &link) {
                shown = true;
                out.extend(cse::report(
                    &link,
                    &function,
                    crate::version::enode_names(),
                    version,
                    views.ir,
                ));
            }
        }
        if !shown {
            out.push(
                "no common subexpression trace for that function (an oracle with the ce.* hooks is needed)"
                    .into(),
            );
        }
        return Ok(out);
    }
    if views.blocks {
        let mut shown = false;
        for (link, function) in blocks::parse(&parsed.records)? {
            if selected(views, &link) {
                shown = true;
                out.extend(blocks::report(
                    &link,
                    &function,
                    crate::version::opcode_names(version),
                    views.pcode,
                    views.block_limit,
                ));
            }
        }
        if !shown {
            out.push(
                "no block trace for that function (an oracle with the bk.* hooks is needed)".into(),
            );
        }
        return Ok(out);
    }
    let functions = document["functions"]
        .as_array()
        .map_or(&[][..], Vec::as_slice);
    if views.locals {
        let passes = locals::scalarize(&parsed.records)?;
        let bindings = locals::bindings(&parsed.records)?;
        for a in functions {
            let link = a["link"].as_str().unwrap_or("");
            let p = passes.get(link).map_or(&[][..], Vec::as_slice);
            let b = bindings.get(link).map_or(&[][..], Vec::as_slice);
            let target = views.target_asm.and_then(|s| locals::frame(s, link));
            out.extend(locals::report(a, p, target.as_ref(), b));
            let ours = ppc::function_words(traced.object, link)?;
            let theirs = views.target_asm.and_then(|s| ppc::target_words(s, link));
            let mut runs = vec![];
            if let (Some(ours), Some(theirs), Some(target)) =
                (ours.filter(|w| !w.is_empty()), theirs, target)
                && !a["layout"].is_null()
            {
                let layout = &a["layout"];
                let limit = ["linkage", "outgoing", "locals"]
                    .iter()
                    .map(|k| layout[k].as_i64().unwrap_or(0))
                    .sum();
                let frame = a["frame"].as_array().map_or(&[][..], Vec::as_slice);
                out.extend(locals::shift_report(
                    &locals::slot_shifts(&ours, &theirs, frame, limit, target.limit),
                    frame,
                ));
                let (r, mine) =
                    locals::unpaired_objects(&ours, &theirs, frame, limit, target.limit);
                out.extend(locals::unpaired_report(&r, &mine));
                runs = r;
            }
            out.extend(report::frame(a));
            out.extend(locals::summary(a, p, b, &runs));
        }
        return Ok(out);
    }
    let origins = if views.exchange.is_some() {
        Some(cse::parse(&parsed.records)?)
    } else {
        None
    };
    let about = |d: &&Value| {
        views.inline_cost.is_none_or(|query| {
            d["event"] == "decision"
                && (d["callee"].as_str().is_some_and(|s| s.contains(query))
                    || d["link"].as_str().is_some_and(|s| s.contains(query)))
        })
    };
    for analysis in functions {
        let name = analysis["function"].as_str().unwrap_or("?");
        let link = analysis["link"].as_str().unwrap_or("?");
        if let Some(query) = views.inline_cost {
            let decisions: Vec<Value> = analysis["inlining"]
                .as_array()
                .into_iter()
                .flatten()
                .filter(about)
                .cloned()
                .collect();
            if !decisions.is_empty() {
                out.push(format!("== {name} ({link}): inlining {query} =="));
                out.extend(report::inlining(&decisions, true));
                for d in &decisions {
                    out.extend(report::cost(d, crate::version::enode_names()));
                }
            }
            continue;
        }
        if let Some(query) = views.exchange {
            let (a, b) = query.split_once(',').context("exchange needs A,B")?;
            out.extend(report::exchange(analysis, a, b));
            let mut held = vec![];
            if let Some(record) = origins.as_ref().and_then(|f| f.get(link)) {
                for text in [a, b] {
                    if let Some((class, number)) = register(text)
                        && let Some(origin) = cse::origin(
                            record,
                            class,
                            number,
                            version,
                            crate::version::enode_names(),
                        )
                    {
                        held.push(format!("   %{number} holds {origin}"));
                    }
                }
            }
            if !held.is_empty() {
                out.push("   what they hold:".into());
                out.extend(held);
            }
            continue;
        }
        if let Some(decisions) = analysis["inlining"].as_array().filter(|v| !v.is_empty()) {
            out.push(format!("== {name} ({link}): inlining =="));
            out.extend(report::inlining(decisions, false));
        }
        if views.pcode
            && let Some(a) = parsed.allocations.iter().find(|a| {
                analysis["function"] == a.before.function && analysis["link"] == a.before.link
            })
        {
            out.extend(report::pcode(
                &a.before,
                crate::version::opcode_names(version),
            ));
        }
        out.extend(report::allocation(analysis));
        out.extend(report::frame(analysis));
        if views.schedule {
            out.extend(report::schedule(
                name,
                analysis["schedule"]
                    .as_array()
                    .map_or(&[][..], Vec::as_slice),
            ));
        }
    }
    if let Some(after) = document["inlining_after_last_function"]
        .as_array()
        .filter(|v| !v.is_empty() && views.exchange.is_none())
    {
        let after: Vec<Value> = after.iter().filter(about).cloned().collect();
        if !after.is_empty() {
            out.push("== inlining after the last allocated function ==".into());
            if views.inline_cost.is_some() {
                for d in &after {
                    out.extend(report::inlining(std::slice::from_ref(d), true));
                    out.extend(report::cost(d, crate::version::enode_names()));
                }
            } else {
                out.extend(report::inlining(&after, false));
            }
        }
    }
    if let Some(query) = views.function.filter(|_| functions.is_empty()) {
        out.push(format!(
            "no function matches {query}; the traced functions most like it (name, link name):"
        ));
        for similar in document["similar"].as_array().into_iter().flatten() {
            out.push(format!(
                "   {}  {}",
                similar["function"].as_str().unwrap_or("?"),
                similar["link"].as_str().unwrap_or("?")
            ));
        }
    } else if parsed.allocations.is_empty() && parsed.inliner.is_empty() {
        out.push(
            if traced.original_fallback {
                "no trace: compiled with the original compiler (no oracle hooks)"
            } else {
                "no trace: the source produced no function code"
            }
            .into(),
        );
    }
    Ok(out)
}
