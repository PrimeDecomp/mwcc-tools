//! `oracle.trace`'s `views.memory_order` on a compile in process (skipped when this checkout's GC 2.7 module is
//! not built): a const method's `this` is read-only memory, so the scheduler moves a store through another pointer
//! above a load through it; a `mutable` member takes that freedom away.
use mwcc_methods::{Session, call};
use mwcc_oracle_core::wasm_build;
use serde_json::{Value, json};
use std::{fs, path::PathBuf};

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

/// The memory_order view of `Get` in the reproducer, its class with MEMBER as its last member.
fn memory_order(member: &str) -> Value {
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("order.cpp");
    fs::write(
        &source,
        format!(
            "struct Value {{\n\
               explicit Value(const bool value) : mType(0), mBool(value) {{}}\n\
               int mType;\n\
               bool mBool;\n\
             }};\n\
             class Holder {{\n\
             public:\n\
               Value Get(unsigned int which) const;\n\
               int mPad[22];\n\
               int mState;\n\
               {member}\n\
             }};\n\
             Value Holder::Get(unsigned int which) const {{\n\
               switch (which) {{\n\
               case 1: return Value(mState == 0);\n\
               case 2: return Value(mState == 1);\n\
               default: return Value(false);\n\
               }}\n\
             }}\n"
        ),
    )
    .unwrap();
    let session = Session::new(root(), None);
    let results = std::sync::Mutex::new(vec![]);
    call(
        &session,
        "oracle.trace",
        json!({
            "source": source,
            "directory": work.path(),
            "version": "GC_2_7",
            "flags": ["-nodefaults", "-proc", "gekko", "-fp", "hardware", "-O4,p", "-inline", "auto"],
            "check": false,
            "function": "Get",
            "views": {"memory_order": true, "text": true},
        }),
        &|| false,
        &|r| results.lock().unwrap().push(r),
    )
    .unwrap();
    let result = results.into_inner().unwrap().remove(0);
    result["backend"]["memory_order"].clone()
}

#[test]
fn a_read_only_this_frees_the_store_to_the_result() {
    if !root().join(wasm_build::module_output("GC_2_7")).is_file() {
        eprintln!("(skipped: the GC 2.7 module is not built)");
        return;
    }
    let read_only = memory_order("int mCount;");
    assert_eq!(read_only["scheduled"], read_only["replayed"], "{read_only}");
    let pairs: Vec<&Value> = read_only["blocks"]
        .as_array()
        .unwrap()
        .iter()
        .flat_map(|b| b["pairs"].as_array().unwrap())
        .collect();
    assert!(
        pairs
            .iter()
            .any(|p| p["read_only_pointee"] == true && p["reorders"] == true),
        "no store moved above a load of read-only memory: {read_only}"
    );
    let mutable = memory_order("mutable int mCount;");
    assert!(
        mutable["blocks"]
            .as_array()
            .unwrap()
            .iter()
            .flat_map(|b| b["pairs"].as_array().unwrap())
            .all(|p| p["read_only_pointee"] == false),
        "a class with a mutable member still has read-only memory: {mutable}"
    );
}
