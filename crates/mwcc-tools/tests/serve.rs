//! `mwcc-tools serve`'s protocol: results and the end of each request by its id, errors, and cancels.
use serde_json::{Value, json};
use std::{
    io::{BufRead, BufReader, Write},
    process::{Command, Stdio},
};

#[test]
fn requests_are_answered_by_id() {
    let mut server = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"))
        .arg("serve")
        .env("MWCC_ORACLE_ROOT", env!("CARGO_MANIFEST_DIR"))
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    let mut input = server.stdin.take().unwrap();
    for request in [
        json!({"id": 1, "method": "session.hello"}),
        json!({"id": "two", "method": "methods.list", "params": {"inputs": [{"name": "unit.load"}, {"name": "x"}]}}),
        json!({"id": 3, "method": "no.such"}),
        json!({"id": 4, "method": "cancel", "params": {"id": 99}}),
        json!({"id": 5, "method": "unit.load", "params": {"unit": "a"}}),
    ] {
        writeln!(input, "{request}").unwrap();
    }
    writeln!(input, "not json").unwrap();
    drop(input);
    let lines: Vec<Value> = BufReader::new(server.stdout.take().unwrap())
        .lines()
        .map(|l| serde_json::from_str(&l.unwrap()).unwrap())
        .collect();
    assert!(server.wait().unwrap().success());
    let of = |id: Value| {
        lines
            .iter()
            .filter(|l| l["id"] == id)
            .cloned()
            .collect::<Vec<_>>()
    };
    let hello = of(json!(1));
    assert_eq!(hello[0]["result"]["schema_version"], 1);
    assert_eq!(hello[1]["done"], true);
    assert!(hello[1]["timing"]["queue"].is_number());
    let listed = of(json!("two"));
    assert_eq!(listed.len(), 3);
    assert_eq!(listed[0]["result"]["index"], 0);
    assert!(
        listed[1]["result"]["error"]
            .as_str()
            .unwrap()
            .contains("no method x")
    );
    assert_eq!(listed[2]["done"], true);
    assert!(
        of(json!(3))[0]["error"]["message"]
            .as_str()
            .unwrap()
            .contains("no method no.such")
    );
    assert_eq!(of(json!(4))[0]["result"]["cancelled"], false);
    assert!(
        of(json!(5))[0]["error"]["message"]
            .as_str()
            .unwrap()
            .contains("no project")
    );
    assert!(
        of(Value::Null)[0]["error"]["message"]
            .as_str()
            .unwrap()
            .contains("not JSON")
    );
}
