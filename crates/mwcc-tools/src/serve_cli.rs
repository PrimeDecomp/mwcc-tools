//! The engine's methods from outside (mwcc-methods): `serve` answers requests on stdin, `call` runs one method,
//! `methods` lists them.
use anyhow::{Context, Result, anyhow, bail};
use mwcc_methods::{Session, Timing, ms};
use serde_json::{Map, Value, json};
use std::{
    collections::HashMap,
    env,
    io::{BufRead, Write},
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::Instant,
};

pub const SERVE_HELP: &str = "usage: mwcc-tools serve [--project DIR] [--oracle-root DIR]

Answers requests on standard input, one JSON object per line, until it ends:
  {\"id\": ID, \"method\": NAME, \"params\": {...}}
with, on standard output, one line per result and then the end:
  {\"id\": ID, \"result\": {...}}  ...  {\"id\": ID, \"done\": true, \"timing\": {...}}
or {\"id\": ID, \"error\": {\"message\": ...}}. Requests run at once, each on a thread of its own;
{\"id\": ID2, \"method\": \"cancel\", \"params\": {\"id\": ID}} stops one. `session.hello` gives the schema
version, `methods.list` the methods (docs/api.md). The session keeps what it loads (projects, units,
targets) until it ends. --project is the project a request names none of; --oracle-root (else MWCC_ORACLE_ROOT) is
the mwcc checkout whose modules run the projects' compilers (else the one this executable was built in).";

pub const CALL_HELP: &str = "usage: mwcc-tools call METHOD [@PARAMS.json | KEY=VALUE ...] [--project DIR] [--oracle-root DIR]
       mwcc-tools methods [METHOD | --markdown [--write docs/api.md]]

`call` runs one method in a fresh session and prints its results, one JSON object per line. Its
parameters are a JSON file (@PARAMS.json), or KEY=VALUE pairs (a VALUE that is JSON is read as JSON, any
other as text; KEY may be dotted: files.src/a.cpp=...). `methods` lists the methods, or gives one's
parameter and result schemas; --markdown, the reference docs/api.md holds (--write updates it).";

/// OPTION's value in ARGS, taken out of them.
fn take(args: &mut Vec<String>, option: &str) -> Result<Option<PathBuf>> {
    let Some(at) = args.iter().position(|a| a == option) else {
        return Ok(None);
    };
    let value = PathBuf::from(
        args.get(at + 1)
            .with_context(|| format!("{option} requires a directory"))?,
    );
    args.drain(at..at + 2);
    Ok(Some(value))
}

/// The session of a command: the mwcc checkout (`--oracle-root`, else MWCC_ORACLE_ROOT, else this executable's, else
/// the download cache, which gets each version's modules the first time a method needs them), and the project of
/// `--project` (both taken out of ARGS).
fn session(args: &mut Vec<String>) -> Result<Session> {
    let project = take(args, "--project")?;
    let root = match take(args, "--oracle-root")? {
        Some(root) => root,
        None => crate::run_cli::run_root()?,
    };
    Ok(Session::new(root, project))
}

fn write_line(out: &Mutex<std::io::Stdout>, value: &Value) {
    let mut out = out.lock().unwrap();
    let _ = writeln!(out, "{value}");
    let _ = out.flush();
}

pub fn serve(args: &[String]) -> Result<()> {
    let mut args = args.to_vec();
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!("{SERVE_HELP}");
        return Ok(());
    }
    let session = Arc::new(session(&mut args)?);
    if let Some(extra) = args.first() {
        bail!("serve: unexpected {extra}\n{SERVE_HELP}");
    }
    let out = Arc::new(Mutex::new(std::io::stdout()));
    let running: Arc<Mutex<HashMap<String, Arc<AtomicBool>>>> = Arc::default();
    let mut threads = vec![];
    for line in std::io::stdin().lock().lines() {
        let line = line?;
        if line.trim().is_empty() {
            continue;
        }
        let received = Instant::now();
        let request: Value = match serde_json::from_str(&line) {
            Ok(request) => request,
            Err(e) => {
                write_line(
                    &out,
                    &json!({"id": null, "error": {"message": format!("not JSON: {e}")}}),
                );
                continue;
            }
        };
        let id = request["id"].clone();
        let Some(method) = request["method"].as_str().map(String::from) else {
            write_line(
                &out,
                &json!({"id": id, "error": {"message": "a request needs `method`"}}),
            );
            continue;
        };
        let params = request.get("params").cloned().unwrap_or(Value::Null);
        if method == "cancel" {
            let found = running
                .lock()
                .unwrap()
                .get(&params["id"].to_string())
                .map(|flag| flag.store(true, Ordering::Relaxed))
                .is_some();
            write_line(&out, &json!({"id": id, "result": {"cancelled": found}}));
            write_line(
                &out,
                &json!({"id": id, "done": true, "timing": Timing::default()}),
            );
            continue;
        }
        let flag = Arc::new(AtomicBool::new(false));
        running.lock().unwrap().insert(id.to_string(), flag.clone());
        let (session, out, running) = (session.clone(), out.clone(), running.clone());
        threads.push(std::thread::spawn(move || {
            let queued = ms(received.elapsed());
            let cancel = || flag.load(Ordering::Relaxed);
            let outcome = mwcc_methods::call(&session, &method, params, &cancel, &|result| {
                write_line(&out, &json!({"id": id, "result": result}));
            });
            running.lock().unwrap().remove(&id.to_string());
            match outcome {
                Ok(mut timing) => {
                    timing.queue += queued;
                    write_line(&out, &json!({"id": id, "done": true, "timing": timing}));
                }
                Err(error) => {
                    write_line(
                        &out,
                        &json!({"id": id, "error": {"message": format!("{error:#}")}}),
                    );
                }
            }
        }));
        threads.retain(|t| !t.is_finished());
    }
    for thread in threads {
        let _ = thread.join();
    }
    Ok(())
}

/// VALUE as `call` reads it: JSON, else text.
fn value(text: &str) -> Value {
    serde_json::from_str(text).unwrap_or_else(|_| Value::String(text.to_owned()))
}

/// PARAMS with KEY (dotted: one object in another) set to VALUE.
fn set(params: &mut Map<String, Value>, key: &str, value: Value) -> Result<()> {
    match key.split_once('.') {
        // (a file's path has its own dots: `files.` takes the rest as one key)
        Some((head, rest)) if head != "files" || !rest.is_empty() => {
            let inner = params
                .entry(head)
                .or_insert_with(|| Value::Object(Map::new()));
            let Value::Object(inner) = inner else {
                bail!("{head} is not an object");
            };
            if head == "files" {
                inner.insert(rest.to_owned(), value);
                Ok(())
            } else {
                set(inner, rest, value)
            }
        }
        _ => {
            params.insert(key.to_owned(), value);
            Ok(())
        }
    }
}

pub fn call(args: &[String]) -> Result<()> {
    let mut args = args.to_vec();
    if args.is_empty() || args.iter().any(|a| a == "--help" || a == "-h") {
        println!("{CALL_HELP}");
        return Ok(());
    }
    let session = session(&mut args)?;
    let method = args.remove(0);
    let mut params = Map::new();
    for arg in &args {
        if let Some(file) = arg.strip_prefix('@') {
            let text = std::fs::read_to_string(file).with_context(|| format!("read {file}"))?;
            match serde_json::from_str(&text).with_context(|| format!("{file} is not JSON"))? {
                Value::Object(map) => params.extend(map),
                _ => bail!("{file} is not a JSON object"),
            }
        } else {
            let (key, text) = arg
                .split_once('=')
                .ok_or_else(|| anyhow!("{arg}: expected KEY=VALUE or @FILE"))?;
            set(&mut params, key, value(text))?;
        }
    }
    let stdout = Mutex::new(std::io::stdout());
    let timing = mwcc_methods::call(
        &session,
        &method,
        Value::Object(params),
        &|| false,
        &|result| write_line(&stdout, &result),
    )?;
    if env::var_os("MWCC_CALL_TIMING").is_some() {
        eprintln!("timing: {}", serde_json::to_string(&timing)?);
    }
    Ok(())
}

pub fn methods(args: &[String]) -> Result<()> {
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!("{CALL_HELP}");
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("--markdown") {
        let reference = mwcc_methods::reference::markdown();
        match args.get(1).map(String::as_str) {
            None => print!("{reference}"),
            Some("--write") => {
                let path = args.get(2).context("--write requires docs/api.md's path")?;
                let document =
                    std::fs::read_to_string(path).with_context(|| format!("read {path}"))?;
                let updated = mwcc_methods::reference::updated(&document)
                    .with_context(|| format!("{path} has no generated reference's markers"))?;
                std::fs::write(path, updated).with_context(|| format!("write {path}"))?;
            }
            Some(other) => bail!("methods --markdown: unexpected {other}"),
        }
        return Ok(());
    }
    let session = Session::new(PathBuf::from("."), None);
    let params = match args.first() {
        Some(name) => json!({"name": name}),
        None => json!({}),
    };
    let results = Mutex::new(vec![]);
    mwcc_methods::call(&session, "methods.list", params, &|| false, &|r| {
        results.lock().unwrap().push(r)
    })?;
    let listed = results.into_inner().unwrap()[0]["value"]
        .as_array()
        .cloned()
        .unwrap_or_default();
    if args.is_empty() {
        let width = listed
            .iter()
            .filter_map(|m| m["name"].as_str())
            .map(str::len)
            .max()
            .unwrap_or(0);
        for m in listed {
            println!(
                "{:width$}  {}",
                m["name"].as_str().unwrap_or(""),
                m["summary"].as_str().unwrap_or("")
            );
        }
    } else {
        println!("{}", serde_json::to_string_pretty(&listed[0])?);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn keys_set_nested_values_and_file_paths() {
        let mut params = Map::new();
        set(&mut params, "unit", value("main/A")).unwrap();
        set(&mut params, "inline_max_size", value("77")).unwrap();
        set(&mut params, "files.src/a.cpp", value("int x;")).unwrap();
        set(&mut params, "files.include/b.h", value("null")).unwrap();
        set(&mut params, "trace.locals", value("true")).unwrap();
        assert_eq!(
            Value::Object(params),
            json!({"unit": "main/A", "inline_max_size": 77, "files": {"src/a.cpp": "int x;", "include/b.h": null},
                   "trace": {"locals": true}})
        );
    }
}
