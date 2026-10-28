use crate::{trace::Record, version::class_name};
use anyhow::{Context, Result};
use serde::Serialize;
use serde_json::{Map, Value, json};
use std::collections::BTreeMap;

#[derive(Clone, Debug, Default, Serialize)]
pub struct Function {
    pub passes: Vec<Value>,
    pub objects: Vec<(String, u32)>,
    pub vregs: Map<String, Value>,
    pub propagations: Vec<Value>,
}
pub fn parse(records: &[Record]) -> Result<indexmap::IndexMap<String, Function>> {
    let mut functions: indexmap::IndexMap<String, Function> = indexmap::IndexMap::new();
    let mut objects = vec![];
    let mut vregs = Map::new();
    let mut propagations = vec![];
    let mut current: Option<(String, usize)> = None;
    for r in records {
        match r.kind.as_str(){
            "ra.before"=>{let link=r.fields.get("link").filter(|s|!s.is_empty()).map_or(r.get("function")?,String::as_str);let function=functions.entry(link.into()).or_default();function.objects.append(&mut objects);function.vregs.extend(std::mem::take(&mut vregs));function.propagations.append(&mut propagations);}
            kind if kind.starts_with("pp.")=>propagations.push(json!({"kind":kind.trim_start_matches("pp."),"at":r.number::<i64>("at")?,"line":r.number::<i64>("line")?,"tree":r.get("tree")?})),
            "ce.object"=>objects.push((r.get("name")?.into(),u32::from_str_radix(r.get("caller")?,16)?)),
            "ce.vreg"=>{vregs.insert(r.get("name")?.into(),json!([r.number::<i64>("reg")?,r.number::<u8>("class")?,r.get("noregister")?=="1"]));}
            "ce.pass"=>{let link=r.get("link")?.to_owned();let f=functions.entry(link.clone()).or_default();current=Some((link,f.passes.len()));f.passes.push(json!({"n":r.number::<u32>("n")?,"ir":[],"temps":[],"replaces":[]}));}
            "ce.l"|"ce.move"|"ce.temp"|"ce.replace"=>{
                let Some((link,index))=&current else{continue;};let pass=&mut functions.get_mut(link).unwrap().passes[*index];
                match r.kind.as_str(){
                    "ce.l"=>{let mut ir=Map::new();for key in ["i","kind","op","a","b","line","expr"]{ir.insert(key.into(),json!(r.number::<i64>(key)?));}ir.insert("text".into(),json!(r.get("text")?));pass["ir"].as_array_mut().unwrap().push(Value::Object(ir));}
                    "ce.move"=>pass["temps"].as_array_mut().unwrap().push(json!({"expr":r.number::<i64>("expr")?,"line":r.number::<i64>("line")?,"tree":r.get("tree")?,"name":null,"path":null,"repeat":null,"replaces":[]})),
                    "ce.temp"=>{if let Some(temp)=pass["temps"].as_array_mut().unwrap().last_mut(){temp["name"]=json!(r.get("name")?);temp["path"]=json!(r.get("path")?);}}
                    _=>{let at=r.number::<i64>("at")?;let with=r.number::<i64>("with")?;pass["replaces"].as_array_mut().unwrap().push(json!([at,with]));
                        for temp in pass["temps"].as_array_mut().unwrap(){if temp["expr"]==with{temp["replaces"].as_array_mut().unwrap().push(json!(at));if temp["repeat"].is_null(){temp["repeat"]=json!(at);}}}}
                }
            }
            _=>(),
        }
    }
    Ok(functions)
}
pub fn names(function: &Function) -> Result<BTreeMap<(String, u32), String>> {
    let mut names = BTreeMap::new();
    for (name, value) in &function.vregs {
        let register = value[0].as_i64().context("CSE vreg missing register")?;
        let class = value[1].as_u64().context("CSE vreg missing class")? as u8;
        if value[2] != true && register >= 0 {
            names
                .entry((class_name(class), register as u32))
                .or_insert_with(|| name.clone());
        }
    }
    Ok(names)
}

fn num(v: &Value, key: &str) -> i64 {
    v[key].as_i64().unwrap_or(0)
}
fn arr(v: &Value) -> &[Value] {
    v.as_array().map(Vec::as_slice).unwrap_or(&[])
}
pub fn tree(text: &str, types: &BTreeMap<i64, String>, indices: bool) -> String {
    use regex::Regex;
    use std::sync::LazyLock;
    static TOKENS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\(|\)|[^\s()]+").unwrap());
    let tokens: Vec<_> = TOKENS.find_iter(text).map(|m| m.as_str()).collect();
    let mut at = 0;
    fn parse(
        tokens: &[&str],
        at: &mut usize,
        types: &BTreeMap<i64, String>,
        indices: bool,
        depth: usize,
    ) -> Option<String> {
        if depth > 512 {
            return None;
        }
        let token = *tokens.get(*at)?;
        *at += 1;
        if token != "(" {
            return Some(token.into());
        }
        let head = *tokens.get(*at)?;
        *at += 1;
        let mut children = vec![];
        while tokens.get(*at).copied()? != ")" {
            children.push(parse(tokens, at, types, indices, depth + 1)?);
        }
        *at += 1;
        let (number, index) = head.split_once('#').unwrap_or((head, ""));
        let name = if !number.is_empty() && number.chars().all(|c| c.is_ascii_digit()) {
            number
                .parse::<i64>()
                .ok()
                .and_then(|n| types.get(&n))
                .cloned()
                .unwrap_or_else(|| format!("op{number}"))
        } else {
            number.to_owned()
        };
        let mark = if indices && !index.is_empty() {
            format!("#{index}")
        } else {
            String::new()
        };
        if name == "EINDIRECT" && children.len() == 1 {
            return Some(format!("*{mark}{}", children[0]));
        }
        if matches!(name.as_str(), "ETYPCON" | "EFORCELOAD") && children.len() == 1 {
            return Some(format!(
                "{}{mark}({})",
                name[1..].to_lowercase(),
                children[0]
            ));
        }
        let infix = match name.as_str() {
            "EADD" => Some("+"),
            "ESUB" => Some("-"),
            "EMUL" => Some("*"),
            "EDIV" => Some("/"),
            "EASS" => Some("="),
            "ESHL" => Some("<<"),
            "ESHR" => Some(">>"),
            "EAND" => Some("&"),
            "EOR" => Some("|"),
            "EXOR" => Some("^"),
            "ELESS" => Some("<"),
            "EGREATER" => Some(">"),
            "ELESSEQU" => Some("<="),
            "EGREATEREQU" => Some(">="),
            "EEQU" => Some("=="),
            "ENOTEQU" => Some("!="),
            "ECOMMA" => Some(","),
            _ => None,
        };
        if let Some(op) = infix
            && children.len() == 2
        {
            return Some(format!("({} {op}{mark} {})", children[0], children[1]));
        }
        Some(format!("{name}{mark}({})", children.join(", ")))
    }
    parse(&tokens, &mut at, types, indices, 0).unwrap_or_else(|| text.into())
}
pub fn temp_creator(version: crate::version::Version, caller: u32) -> String {
    // (the WebAssembly oracle has no return address: only the scalarizer's temporaries, which gc2 probes, name one)
    if caller == 0 {
        return "an unknown function (the WebAssembly oracle records only the scalarizer's)".into();
    }
    let Some(name) = crate::version::function_at(version, caller) else {
        return format!("0x{caller:x}");
    };
    let why = match name {
        "fn_0046e030" | "fn_0046ec50" => Some(
            "linearization: an EFORCELOAD's temporary (an inlined function's return, or a value loaded once)",
        ),
        "extract_right_operand_to_statement" => Some("linearization: a comma expression's value"),
        "insert_intconst_assignment" => Some("linearization: a && or || value"),
        "IroCSE_CreateTemp" => {
            Some("loop-invariant code motion (IroLoop reduce_strength_and_move_loop_invariants)")
        }
        "fn_00461860" => {
            Some("loop strength reduction: an induction variable's reduced expression")
        }
        "fn_004a7500" => {
            Some("loop optimizer (probably strength reduction, as fn_00461860 in 1.3.2)")
        }
        "move_common_sub" => Some("common subexpressions (where the uses meet)"),
        "create_replacement_temp_assignment" => Some("common subexpressions (in place)"),
        "fn_0055d510" => Some("inliner: flattening an inlined expression into statements"),
        "fn_0055d7e0" => Some("inliner: flattening `cond ? a : b` into statements"),
        "fn_0055da80" => Some("inliner: flattening a null check into statements"),
        "fn_00460780" => Some("scalarizer: a member of a local aggregate"),
        _ => None,
    };
    let name = crate::version::function_name(version, caller).unwrap_or_else(|| name.into());
    why.map_or(name.clone(), |why| format!("{name}: {why}"))
}
pub fn origin(
    function: &Function,
    class: &str,
    number: u32,
    version: crate::version::Version,
    types: &BTreeMap<i64, String>,
) -> Option<String> {
    let name = function
        .vregs
        .iter()
        .find(|(_, v)| {
            v[0].as_u64() == Some(u64::from(number))
                && v[1].as_u64().is_some_and(|c| class_name(c as u8) == class)
                && v[2] != true
        })?
        .0;
    for pass in &function.passes {
        for temp in arr(&pass["temps"]) {
            if temp["name"] == name.as_str() {
                return Some(format!(
                    "{name} = {}, a common subexpression's temporary (pass {}, expression #{}), made at its first repeat #{}: the temporaries are made in the order of their first repeats, so this one's number moves with that order (see --cse)",
                    tree(temp["tree"].as_str().unwrap_or(""), types, false),
                    num(pass, "n"),
                    num(temp, "expr"),
                    temp["repeat"]
                        .as_i64()
                        .map_or("None".into(), |n| n.to_string())
                ));
            }
        }
    }
    if let Some((_, caller)) = function.objects.iter().find(|(n, _)| n == name) {
        return Some(format!(
            "{name}, a temporary made by {}",
            temp_creator(version, *caller)
        ));
    }
    Some(format!("{name}, a local of the source"))
}
pub fn propagation_report(
    function: &Function,
    types: &BTreeMap<i64, String>,
    line: &str,
) -> Vec<String> {
    let mut out = vec![];
    let mut assignments = BTreeMap::new();
    for q in &function.propagations {
        if line != "all" && num(q, "line").to_string() != line {
            continue;
        }
        let kind = q["kind"].as_str().unwrap_or("");
        let text = if kind == "dead" {
            assignments
                .get(&num(q, "at"))
                .cloned()
                .unwrap_or_else(|| format!("#{}", num(q, "at")))
        } else {
            let text = tree(q["tree"].as_str().unwrap_or(""), types, false);
            assignments.insert(num(q, "at"), text.clone());
            text
        };
        let why = match kind {
            "copy" => {
                "copy propagation (IRO_CopyAndConstantPropagation): the left side's uses read the right side instead"
            }
            "expression" => {
                "expression propagation candidate (IroPropagate_PropagateExpressions): a single use in the block gets the expression itself"
            }
            "dead" => "dead assignment removed (IROUseDef): nothing reads it any more",
            other => other,
        };
        out.push(format!("      #{:<5} {text}: {why}", num(q, "at")));
    }
    if !out.is_empty() {
        out.insert(0,format!("   propagations{} (before and between the passes; indices are those of the IR at the time)",if line=="all"{String::new()}else{format!(" on line {line}")}));
    }
    out
}
pub fn report(
    link: &str,
    function: &Function,
    types: &BTreeMap<i64, String>,
    version: crate::version::Version,
    line: Option<&str>,
) -> Vec<String> {
    let mut out = vec![format!("== {link}: common subexpressions ==")];
    if function.passes.is_empty() {
        out.push("   no IRO_CommonSubs pass traced (MWCC_ORACLE_CSE=2 and an oracle with the ce.* hooks needed)".into());
        return out;
    }
    let mut made = BTreeMap::new();
    let vreg = |name: &str| -> String {
        let Some(v) = function.vregs.get(name) else {
            return "not allocated".into();
        };
        let reg = v[0].as_i64().unwrap_or(0);
        if v[2] == true || reg == 0 {
            return "memory".into();
        }
        let class = class_name(v[1].as_u64().unwrap_or(255) as u8);
        format!(
            "%{}{reg}",
            match class.as_str() {
                "GPR" => "r",
                "FPR" => "f",
                "VR" => "v",
                "CR" => "cr",
                _ => "?",
            }
        )
    };
    for pass in &function.passes {
        let lines: BTreeMap<_, _> = arr(&pass["ir"])
            .iter()
            .map(|n| (num(n, "i"), num(n, "line")))
            .collect();
        let temps = arr(&pass["temps"]);
        let count = temps
            .iter()
            .filter(|t| !t["name"].is_null() && t["name"] != "-")
            .count();
        out.push(format!(
            "   pass {}: {}{}",
            num(pass, "n"),
            if count > 0 || !arr(&pass["replaces"]).is_empty() {
                format!(
                    "{count} temporaries, {} repeats replaced",
                    arr(&pass["replaces"]).len()
                )
            } else {
                "no temporaries made, no repeats replaced".into()
            },
            if num(pass, "n") > 1 {
                " (its IR is numbered anew: the indices are not pass 1's)"
            } else {
                ""
            }
        ));
        let mut previous: Option<&Value> = None;
        for temp in temps {
            let name = temp["name"]
                .as_str()
                .filter(|s| !s.is_empty())
                .unwrap_or("?");
            made.insert(name.to_owned(), num(pass, "n"));
            let repeat = temp["repeat"].as_i64();
            let where_ = format!(
                "#{}{}",
                num(temp, "expr"),
                if num(temp, "line") > 0 {
                    format!(" line {}", num(temp, "line"))
                } else {
                    String::new()
                }
            );
            let first = repeat.map_or("no repeat replaced".into(), |r| {
                format!(
                    "first repeat #{r}{}",
                    lines
                        .get(&r)
                        .filter(|line| **line > 0 && **line != num(temp, "line"))
                        .map_or(String::new(), |line| format!(" line {line}"))
                )
            });
            let note = previous
                .filter(|p| {
                    num(temp, "expr") < num(p, "expr") && repeat.is_some() && !p["repeat"].is_null()
                })
                .map_or(String::new(), |p| {
                    format!(
                        "  <- occurs before {}'s expression (#{} < #{}) but repeats after it",
                        p["name"].as_str().unwrap_or("None"),
                        num(temp, "expr"),
                        num(p, "expr")
                    )
                });
            out.push(format!(
                "      {name} = {}  [{where_}; {first}; {} repeat{}; {}{}] -> {}{note}",
                tree(temp["tree"].as_str().unwrap_or(""), types, false),
                arr(&temp["replaces"]).len(),
                if arr(&temp["replaces"]).len() != 1 {
                    "s"
                } else {
                    ""
                },
                if temp["path"] == "comma" {
                    "assigned where all uses meet"
                } else {
                    "assigned in place"
                },
                if temp["path"] == "none" {
                    "; NOT MADE (over 64 uses)"
                } else {
                    ""
                },
                vreg(name)
            ));
            previous = Some(temp);
        }
        if let Some(line) = line {
            let temp_of: BTreeMap<_, _> =
                temps.iter().map(|t| (num(t, "expr"), &t["name"])).collect();
            let repeats: BTreeMap<_, _> = arr(&pass["replaces"])
                .iter()
                .map(|pair| (pair[0].as_i64().unwrap_or(0), pair[1].as_i64().unwrap_or(0)))
                .collect();
            out.push(format!("   pass {}: linear IR{} (index, node, operands; expressions with their temporary or the one they repeat)",num(pass,"n"),if line=="all"{String::new()}else{format!(" of line {line}")}));
            for node in arr(&pass["ir"]).iter().filter(|n| {
                num(n, "kind") != 0 && (line == "all" || num(n, "line").to_string() == line)
            }) {
                let kind = match num(node, "kind") {
                    0 => "nop".into(),
                    1 => "operand".into(),
                    2 => "op1".into(),
                    3 => "op2".into(),
                    4 => "goto".into(),
                    5 => "if".into(),
                    6 => "ifnot".into(),
                    7 => "return".into(),
                    8 => "label".into(),
                    9 => "switch".into(),
                    10 => "cond".into(),
                    11 => "call".into(),
                    k => format!("kind{k}"),
                };
                let op = if matches!(num(node, "kind"), 2 | 3) {
                    types
                        .get(&num(node, "op"))
                        .cloned()
                        .unwrap_or_else(|| format!("op{}", num(node, "op")))
                } else {
                    kind
                };
                let operands = [num(node, "a"), num(node, "b")]
                    .into_iter()
                    .filter(|v| *v >= 0)
                    .map(|v| format!("#{v}"))
                    .collect::<Vec<_>>()
                    .join(" ");
                let extra = if num(node, "kind") == 1 {
                    node["text"].as_str().unwrap_or("").to_owned()
                } else {
                    operands
                };
                let tag = if let Some(name) = temp_of.get(&num(node, "i")) {
                    format!("  -> {}", name.as_str().unwrap_or("None"))
                } else if let Some(repeat) = repeats.get(&num(node, "i")) {
                    format!(
                        "  repeats #{repeat}{}",
                        temp_of.get(repeat).map_or(String::new(), |name| format!(
                            " ({})",
                            name.as_str().unwrap_or("None")
                        ))
                    )
                } else {
                    String::new()
                };
                out.push(format!(
                    "      #{:<5} {op:<12} {extra}{tag}",
                    num(node, "i")
                ));
            }
        }
    }
    if let Some(line) = line {
        out.extend(propagation_report(function, types, line));
    }
    let others: Vec<_> = function
        .objects
        .iter()
        .filter(|(name, _)| !made.contains_key(name))
        .collect();
    let shown: Vec<_> = others
        .iter()
        .filter(|(name, _)| !matches!(vreg(name).as_str(), "memory" | "not allocated"))
        .collect();
    if !shown.is_empty() {
        out.push("   other temporaries in registers (the function that made each):".into());
        for (name, caller) in &shown {
            out.push(format!(
                "      {name} -> {}  made by {}",
                vreg(name),
                temp_creator(version, *caller)
            ));
        }
    }
    let hidden = others.len() - shown.len();
    if hidden > 0 {
        out.push(format!(
            "   ({hidden} other temporaries are in memory or gone by the allocation)"
        ));
    }
    out
}
