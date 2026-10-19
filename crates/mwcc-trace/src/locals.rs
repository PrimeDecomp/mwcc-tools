use crate::trace::Record;
use anyhow::{Context, Result};
use serde_json::{Value, json};
use std::collections::{BTreeMap, VecDeque};
mod report;
mod target;
pub use report::{blocked_reasons, report, summary};
pub use target::{
    TargetFrame, frame, run_copies, shift_report, slot_shifts, unpaired_objects, unpaired_report,
};

fn items(value: &Value) -> &[Value] {
    value.as_array().map_or(&[], Vec::as_slice)
}
fn text(value: &Value) -> &str {
    value.as_str().unwrap_or("")
}
fn number(value: &Value) -> i64 {
    value.as_i64().unwrap_or(0)
}
fn truth(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(b) => *b,
        Value::Number(_) => number(value) != 0,
        Value::String(s) => !s.is_empty(),
        Value::Array(a) => !a.is_empty(),
        Value::Object(o) => !o.is_empty(),
    }
}
fn shown(value: &Value) -> String {
    if value.is_null() {
        "None".into()
    } else if let Some(s) = value.as_str() {
        s.into()
    } else {
        value.to_string()
    }
}
fn unique<T: PartialEq>(values: impl IntoIterator<Item = T>) -> Vec<T> {
    let mut out = vec![];
    for v in values {
        if !out.contains(&v) {
            out.push(v)
        }
    }
    out
}

fn var() -> Value {
    json!({"size":null,"noregister":null,"uses":[],"parts":[]})
}

pub fn scalarize(records: &[Record]) -> Result<BTreeMap<String, Vec<Value>>> {
    let mut out: BTreeMap<String, Vec<Value>> = BTreeMap::new();
    let mut temps = VecDeque::new();
    let mut pending = VecDeque::new();
    let mut calls = 0;
    let mut uses = vec![];
    let mut current: Option<(String, usize)> = None;
    for r in records {
        match r.kind.as_str() {
            "lc.temp"=>temps.push_back(r.get("name")?.to_owned()),
            "lc.call"=>calls+=1,
            "lc.use"=>uses.push((r.get("var")?.to_owned(),json!({"node":r.number::<i64>("node")?,"call":r.number::<i64>("kind")?==17,
                "offset":r.number::<i64>("offset")?,"kind":r.number::<i64>("type")?,"size":r.number::<i64>("size")?}))),
            "lc.pass"=>{
                let link=r.get("link")?.to_owned();let mut pass=json!({"pointers":r.number::<i64>("pointers")?,"calls":calls,"vars":{}});
                for (name,use_) in std::mem::take(&mut uses) {
                    if pass["vars"].get(&name).is_none() {pass["vars"][&name]=var();}
                    pass["vars"][&name]["uses"].as_array_mut().unwrap().push(use_);
                }
                let passes=out.entry(link.clone()).or_default();current=Some((link,passes.len()));passes.push(pass);
                pending=std::mem::take(&mut temps);calls=0;
            }
            "lc.part"=>{
                if let Some((link,index))=&current {
                    let pass=&mut out.get_mut(link).unwrap()[*index];let name=r.get("var")?;
                    if pass["vars"].get(name).is_none(){pass["vars"][name]=var();}
                    let v=&mut pass["vars"][name];v["size"]=json!(r.number::<i64>("var_size")?);v["noregister"]=json!(r.number::<i64>("noregister")?);
                    let (start,end)=r.get("bits")?.split_once("..").context("invalid scalarized bit range")?;
                    let blocked=r.number::<i64>("blocked")?;
                    v["parts"].as_array_mut().unwrap().push(json!({"start":start.parse::<i64>()?,"end":end.parse::<i64>()?,"kind":r.number::<i64>("type")?,
                        "size":r.number::<i64>("size")?,"uses":r.number::<i64>("uses")?,"blocked":blocked,"temp":if blocked==0{pending.pop_front()}else{None}}));
                }
            }
            _=>(),
        }
    }
    Ok(out)
}

pub fn bindings(records: &[Record]) -> Result<BTreeMap<String, Vec<Value>>> {
    let mut out: BTreeMap<String, Vec<Value>> = BTreeMap::new();
    let mut pending = vec![];
    for r in records {
        if r.kind == "lc.arg" {
            let number = |key: &str, default: i64| -> Result<i64> {
                r.fields.get(key).map_or(Ok(default), |v| Ok(v.parse()?))
            };
            let nullable = |key: &str| r.fields.get(key).filter(|v| v.as_str() != "-");
            pending.push(json!({"callee":r.get("link")?,"index":r.number::<i64>("index")?,"name":r.get("name")?,"kind":r.number::<i64>("type")?,
                "size":r.number::<i64>("size")?,"ref":number("ref",0)?,"dirty":r.number::<i64>("dirty")?,"mode":r.get("mode")?,
                "var":nullable("var"),"object":nullable("object"),"declared":number("declared",-1)?,"declared_size":number("declared_size",-1)?,
                "generated":number("generated",0)?,"expression":nullable("expression")}));
        } else if r.kind == "ra.before"
            && !pending.is_empty()
            && let Some(link) = r.fields.get("link")
        {
            out.entry(link.clone()).or_default().append(&mut pending);
        }
    }
    Ok(out)
}
