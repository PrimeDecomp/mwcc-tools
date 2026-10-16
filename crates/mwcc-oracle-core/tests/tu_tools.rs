use mwcc_oracle_core::{inline_diff, inline_window};
use serde_json::{Value, json};
use std::{fs, path::PathBuf};

fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/oracle")
}

#[test]
fn inline_comparisons_match_python() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let doc: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.json"))).unwrap()).unwrap();
        let e: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.tu.json"))).unwrap()).unwrap();
        let decisions = inline_diff::decisions(&doc);
        assert_eq!(
            serde_json::to_value(&decisions).unwrap(),
            e["decisions"],
            "{id}"
        );
        let emitted = inline_diff::emission(&doc);
        assert_eq!(json!(emitted), e["emission"], "{id}");
        for (link, ds) in decisions {
            let summary = inline_diff::summary(&ds);
            assert_eq!(summary, e["summaries"][&link]["summary"], "{id}/{link}");
            assert_eq!(
                json!(inline_diff::summary_report("A", &summary)),
                e["summaries"][&link]["report"],
                "{id}/{link}"
            );
            for present in [false, true] {
                assert_eq!(
                    json!(inline_diff::verdict(&ds, present)),
                    e["summaries"][&link][if present {
                        "verdict_present"
                    } else {
                        "verdict_absent"
                    }],
                    "{id}/{link}"
                );
            }
        }
        let mut target = inline_diff::TargetFunctions {
            functions: serde_json::from_value(e["target"].clone()).unwrap(),
            module: serde_json::from_value(e["module"].clone()).unwrap(),
            stripped: false,
        };
        assert_eq!(
            json!(inline_diff::emission_report(
                "A", &doc, &target, &emitted, None
            )),
            e["emission_report"],
            "{id}"
        );
        for stripped in [false, true] {
            target.stripped = stripped;
            for (link, expected) in e["predictions"][stripped.to_string()].as_object().unwrap() {
                assert_eq!(
                    json!(inline_diff::prediction(link, Some(&target), &emitted)),
                    *expected,
                    "{id}/{link}/{stripped}"
                );
            }
        }
        let ours =
            inline_window::object_calls(&fs::read(root.join(format!("{id}.o"))).unwrap()).unwrap();
        assert_eq!(json!(ours), e["object_calls"], "{id}");
        for w in e["windows"].as_array().unwrap() {
            let calls = serde_json::from_value(w["calls"].clone()).unwrap();
            let rows = inline_window::bounds(&doc, &ours, &calls);
            assert_eq!(json!(rows), w["bounds"], "{id}");
            assert_eq!(json!(inline_window::window(&rows)), w["window"], "{id}");
            assert_eq!(
                json!(inline_window::report(
                    doc["source"].as_str().unwrap(),
                    &rows,
                    false
                )),
                w["report"],
                "{id}"
            );
            assert_eq!(
                json!(inline_window::report(
                    doc["source"].as_str().unwrap(),
                    &rows,
                    true
                )),
                w["verbose"],
                "{id}"
            );
        }
    }
}

#[test]
fn target_module_ownership_and_quoted_assembly() {
    let scratch = tempfile::TempDir::new().unwrap();
    let root = scratch.path();
    let asm = root.join("mod/asm");
    fs::create_dir_all(asm.join("folder")).unwrap();
    fs::write(asm.join("a.s"), ".fn shared, weak\n.endfn shared\n").unwrap();
    fs::write(
        asm.join("folder/b.s"),
        ".fn \"shared\", weak\n.endfn shared\n.fn own, global\n.endfn own\n",
    )
    .unwrap();
    let target = inline_diff::target_functions(root, "src/folder/b.cpp")
        .unwrap()
        .unwrap();
    assert!(!target.stripped);
    assert_eq!(target.functions["own"], "global");
    assert_eq!(target.module["shared"], asm.join("a.s"));
    let target = inline_diff::target_functions(root, "src/a.cpp")
        .unwrap()
        .unwrap();
    assert!(!target.module.contains_key("shared"));
    let calls = inline_window::asm_calls(
        ".fn \"has spaces\", global\n/* 00 00 00 00 */ bl \"callee spaces\"\nbl plain\nb plain\n.endfn \"has spaces\"\n",
    );
    assert_eq!(
        json!(calls),
        json!({"has spaces":{"callee spaces":1,"plain":1}})
    );
}

#[test]
fn data_layouts_and_reports_match_python() {
    use mwcc_oracle_core::data_diff;
    let root = fixtures();
    let data_root = root.parent().unwrap().join("data");
    let text = fs::read_to_string(data_root.join("target/asm/sample.s")).unwrap();
    let target = data_diff::asm_layout(&text).unwrap();
    let expected: Value =
        serde_json::from_slice(&fs::read(data_root.join("assembly.json")).unwrap()).unwrap();
    assert_eq!(json!(target), expected);
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.data.json"))).unwrap())
                .unwrap();
        let mut mine =
            data_diff::object_layout(&fs::read(root.join(format!("{id}.o"))).unwrap()).unwrap();
        assert_eq!(json!(mine), expected["object"], "{id}");
        let mut theirs = target.clone();
        for (name, section) in &mine {
            if let Some(target) = theirs.get_mut(name) {
                data_diff::absorb(
                    section["items"].as_array().unwrap(),
                    target["items"].as_array_mut().unwrap(),
                );
            }
        }
        data_diff::annotate(&mut mine, &mut theirs);
        data_diff::made_early(
            &mut theirs,
            &["use literals".into()],
            &["first".into(), "absent".into(), "use literals".into()],
        );
        assert_eq!(json!(mine), expected["mine"], "{id}");
        assert_eq!(json!(theirs), expected["theirs"], "{id}");
        for (name, reference) in expected["comparisons"].as_object().unwrap() {
            let a = mine.get(name);
            let b = theirs.get(name);
            assert_eq!(
                json!(data_diff::report(name, a, b, true)),
                reference["report"],
                "{id}/{name}"
            );
            assert_eq!(
                json!(data_diff::report(name, a, b, false)),
                reference["default"],
                "{id}/{name}"
            );
            if let (Some(a), Some(b)) = (a, b) {
                let actual = data_diff::compare(
                    &data_diff::linked(a["items"].as_array().unwrap()),
                    &data_diff::linked(b["items"].as_array().unwrap()),
                );
                assert_eq!(json!(actual), reference["differences"], "{id}/{name}");
            }
        }
    }
}

#[test]
fn header_impact_matches_python() {
    use mwcc_oracle_core::header_impact as h;
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let e: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.impact.json"))).unwrap())
                .unwrap();
        let before = fs::read(root.join(format!("{id}.o"))).unwrap();
        let after = fs::read(root.join(format!("{id}.impact.o"))).unwrap();
        let doc: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.json"))).unwrap()).unwrap();
        let after_doc: Value = serde_json::from_slice(
            &fs::read(root.join(format!("{id}.impact-after.json"))).unwrap(),
        )
        .unwrap();
        assert_eq!(
            json!(h::object_functions(&before).unwrap()),
            e["functions"],
            "{id}"
        );
        for f in doc["functions"].as_array().unwrap() {
            let link = f["link"].as_str().unwrap();
            assert_eq!(json!(h::decisions(f)), e["decisions"][link], "{id}/{link}");
            assert_eq!(json!(h::frame(f)), e["frames"][link], "{id}/{link}");
        }
        for target in [false, true] {
            let rows = h::compare(
                [&before, &after],
                [&doc, &after_doc],
                target.then(|| e["asm"].as_str().unwrap()),
            )
            .unwrap();
            assert_eq!(
                json!(rows),
                e[if target { "with_target" } else { "comparison" }],
                "{id}/{target}"
            );
            assert_eq!(
                json!(h::report(
                    "src/unit.cpp",
                    &["include/header.hpp".into()],
                    &rows
                )),
                e[if target { "target_report" } else { "report" }],
                "{id}/{target}"
            );
        }
    }
    let e: Value = serde_json::from_slice(
        &fs::read(root.parent().unwrap().join("header-impact/pairing.json")).unwrap(),
    )
    .unwrap();
    let names: Vec<String> = serde_json::from_value(e["names"].clone()).unwrap();
    assert_eq!(json!(h::paired(&names)), e["paired"]);
    let scratch = tempfile::TempDir::new().unwrap();
    let deps = "build/a.o: #deps 2\n    src/a.cpp\n    include/a.hpp\n\nbuild/no.o: #deps 1\n    include/a.hpp\n\nbuild/b.o: #deps 3\n    src/b.cpp\n    include/../include/a.hpp\n    include/second.hpp\n";
    assert_eq!(
        json!(h::dependents(
            scratch.path(),
            &["include/a.hpp".into(), "include/second.hpp".into()],
            deps
        )),
        json!({"src/a.cpp":["include/a.hpp"],"src/b.cpp":["include/a.hpp","include/second.hpp"]})
    );
}

#[test]
fn inline_calibration_matches_kit_reports() {
    use mwcc_oracle_core::inline_calibrate as c;
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let document: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.json"))).unwrap()).unwrap();
        let expected: Vec<Value> =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.calibrate.json"))).unwrap())
                .unwrap();
        for e in expected {
            let callee = e["callee"].as_str().unwrap();
            if e.get("missing_field").is_some() {
                assert!(
                    c::rows("synthetic", &e["target"], &document, callee, 3).is_err(),
                    "{id}/{callee}: missing cost must remain explicit"
                );
                continue;
            }
            let rows = c::rows("synthetic", &e["target"], &document, callee, 3).unwrap();
            assert_eq!(json!(rows), e["rows"], "{id}/{callee}");
            let lines = c::report(&rows, 3, true).join("\n");
            let actual: Vec<_> = if lines.is_empty() {
                vec![]
            } else {
                lines.lines().collect()
            };
            assert_eq!(json!(actual), e["report"], "{id}/{callee}");
            for b in e["binaries"].as_array().unwrap() {
                let symbols = serde_json::from_value(b["symbols"].clone()).unwrap();
                let table =
                    c::binary_table(&[("synthetic".into(), document.clone())], &symbols, callee)
                        .unwrap();
                assert_eq!(json!(table), b["table"], "{id}/{callee}");
                assert_eq!(
                    json!(c::binary_report(&table, 3)),
                    b["report"],
                    "{id}/{callee}"
                );
            }
        }
    }
    assert_eq!(c::demangle_type("Q23Bar3Baz").unwrap(), "Bar::Baz");
    assert_eq!(
        c::mangle_qualified("CImpactVisorEffect::SParticleEffect"),
        "Q218CImpactVisorEffect15SParticleEffect"
    );
    assert_eq!(
        c::first_template_argument("iterator<pair<A,B>,C>").unwrap(),
        "pair<A,B>"
    );
}
