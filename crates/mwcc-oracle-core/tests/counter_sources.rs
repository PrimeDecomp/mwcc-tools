use mwcc_oracle_core::counter_sources;
use std::{fs, path::Path};

/// The WebAssembly oracle's counter table is the one gc2's sources and the objects the original's compiler builds
/// from them give (`ninja all_source` for GC 2.7; without them, nothing is checked).
#[test]
fn the_counter_table_is_the_sources_one() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    if !root.join("build/GC_2_7/gc2/compiled").is_dir() {
        eprintln!("no build/GC_2_7/gc2/compiled: not checked");
        return;
    }
    let table = counter_sources::generate(&root).unwrap();
    let written = fs::read_to_string(root.join(counter_sources::OUTPUT)).unwrap();
    assert!(
        counter_sources::render(&table) == written.replace('\r', ""),
        "stale: mwcc-tools provenance counter-sites --write (after ninja all_source)"
    );
}
