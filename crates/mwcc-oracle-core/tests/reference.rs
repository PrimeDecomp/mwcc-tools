use mwcc_oracle_core::{allocation, elf::Elf, trace, version::Version};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::{fs, path::PathBuf};

fn fixtures() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/oracle")
}

#[test]
fn allocation_reports_match_python_reference_for_every_captured_case() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let source = fs::read(root.join(case["source"].as_str().unwrap())).unwrap();
        assert_eq!(
            format!("{:x}", Sha256::digest(&source)),
            case["source_sha256"].as_str().unwrap(),
            "source drift: {id}"
        );
        let raw = fs::read(root.join(format!("{id}.trace"))).unwrap();
        assert_eq!(
            format!("{:x}", Sha256::digest(&raw)),
            case["trace_sha256"].as_str().unwrap(),
            "trace drift: {id}"
        );
        let text = String::from_utf8(raw).unwrap();
        let parsed = trace::parse(&text).unwrap_or_else(|e| panic!("{id}: {e:#}"));
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let actual = serde_json::to_value(
            parsed
                .allocations
                .iter()
                .map(|a| allocation::analyze(a, version))
                .collect::<Vec<_>>(),
        )
        .unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.allocation.json"))).unwrap())
                .unwrap();
        // Save a concise first structural difference instead of dumping a large trace.
        if let Some(difference) = first_difference(&actual, &expected, "$") {
            panic!("{id}: {difference}");
        }
    }
}

fn first_difference(a: &Value, b: &Value, path: &str) -> Option<String> {
    match (a, b) {
        (Value::Object(x), Value::Object(y)) => {
            for key in x
                .keys()
                .chain(y.keys())
                .collect::<std::collections::BTreeSet<_>>()
            {
                let next = format!("{path}.{key}");
                match (x.get(key), y.get(key)) {
                    (Some(a), Some(b)) => {
                        if let Some(d) = first_difference(a, b, &next) {
                            return Some(d);
                        }
                    }
                    _ => {
                        return Some(format!(
                            "{next}: missing key (Rust={}, Python={})",
                            x.contains_key(key),
                            y.contains_key(key)
                        ));
                    }
                }
            }
            None
        }
        (Value::Array(x), Value::Array(y)) if x.len() == y.len() => x
            .iter()
            .zip(y)
            .enumerate()
            .find_map(|(i, (a, b))| first_difference(a, b, &format!("{path}[{i}]"))),
        _ if a == b => None,
        _ => Some(format!("{path}: Rust {a}, Python {b}")),
    }
}

#[test]
fn all_fixture_objects_have_valid_symbols_and_function_ranges() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let data = fs::read(root.join(format!("{id}.o"))).unwrap();
        assert_eq!(
            format!("{:x}", Sha256::digest(&data)),
            case["object_sha256"].as_str().unwrap()
        );
        let elf = Elf::parse(&data).unwrap();
        for symbol in elf.symbols().unwrap().iter().filter(|s| s.kind == 2) {
            elf.function_bytes(symbol).unwrap();
        }
    }
}

#[test]
fn inlining_and_scheduling_match_python_reference() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let parsed =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.json"))).unwrap()).unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let bodies = mwcc_oracle_core::inlining::body_costs(&parsed.inliner).unwrap();
        let mut start = 0;
        for (allocation, expected) in parsed
            .allocations
            .iter()
            .zip(expected["functions"].as_array().unwrap())
        {
            let actual = mwcc_oracle_core::inlining::decisions(
                &parsed.inliner[start..allocation.inliner_position],
                &bodies,
            )
            .unwrap();
            start = allocation.inliner_position;
            if let Some(d) = first_difference(
                &serde_json::to_value(actual).unwrap(),
                &expected["inlining"],
                "inlining",
            ) {
                panic!("{id}/{}: {d}", allocation.before.link);
            }
            let actual = mwcc_oracle_core::schedule::analyze(
                &allocation.before.schedule,
                mwcc_oracle_core::version::opcode_names(version),
            )
            .unwrap();
            if let Some(d) = first_difference(
                &serde_json::to_value(actual).unwrap(),
                &expected["schedule"],
                "schedule",
            ) {
                panic!("{id}/{}: {d}", allocation.before.link);
            }
        }
    }
}

#[test]
fn full_schema_six_documents_match_python_reference() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let parsed =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.json"))).unwrap()).unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let actual = mwcc_oracle_core::document::build(
            &parsed,
            version,
            case["source"].as_str().unwrap(),
            expected["oracle"].as_str().unwrap(),
            Some(true),
            &[],
            &Default::default(),
        )
        .unwrap();
        if let Some(d) = first_difference(&actual, &expected, "$") {
            panic!("{id}: {d}");
        }
    }
}

#[test]
fn near_views_suggestions_and_colourings_match_reference() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let parsed =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.views.json"))).unwrap())
                .unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let document = mwcc_oracle_core::document::build(
            &parsed,
            version,
            "",
            "",
            None,
            &[],
            &mwcc_oracle_core::document::Selection {
                near: Some(8),
                ..Default::default()
            },
        )
        .unwrap();
        if let Some(d) = first_difference(&document["near"], &expected["near"], "near") {
            panic!("{id}: {d}");
        }
        let near_text = serde_json::to_value(mwcc_oracle_core::report::near(
            document["near"].as_array().unwrap(),
            8,
        ))
        .unwrap();
        if let Some(d) = first_difference(&near_text, &expected["near_report"], "near_report") {
            panic!("{id}: {d}");
        }
        let text = mwcc_oracle_core::report::instantiation(
            &mwcc_oracle_core::document::instantiation(&parsed, version).unwrap(),
            "",
        );
        if let Some(d) = first_difference(
            &serde_json::to_value(text).unwrap(),
            &expected["instantiation_report"],
            "instantiation_report",
        ) {
            panic!("{id}: {d}");
        }
        for query in ["accumualte", "does_not_exist"] {
            let actual = serde_json::to_value(mwcc_oracle_core::sequence::similar_functions(
                &parsed, query, 12,
            ))
            .unwrap();
            if let Some(d) =
                first_difference(&actual, &expected["suggestions"][query], "suggestions")
            {
                panic!("{id}/{query}: {d}");
            }
        }
        let text = fs::read_to_string(root.join(format!("{id}.trace"))).unwrap();
        for query in ["common", "pipeline", "does_not_exist"] {
            let actual = serde_json::to_value(trace::raw_for(&text, Some(query))).unwrap();
            if let Some(d) =
                first_difference(&actual, &expected["raw_selected"][query], "raw_selected")
            {
                panic!("{id}/{query}: {d}");
            }
        }
        let mut results = vec![];
        for a in &parsed.allocations {
            for class in ["GPR", "FPR", "VR"] {
                if let Some(v) = mwcc_oracle_core::colouring::verify(a, class).unwrap() {
                    results.push(v);
                }
            }
        }
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.colouring.json"))).unwrap())
                .unwrap();
        if let Some(d) = first_difference(
            &serde_json::to_value(results).unwrap(),
            &expected,
            "colouring",
        ) {
            panic!("{id}: {d}");
        }
    }
}

#[test]
fn human_reports_match_reference_including_whitespace() {
    use mwcc_oracle_core::{document, report, version};
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let parsed =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.reports.json"))).unwrap())
                .unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let analyses = document::analyze(&parsed, version, &Default::default()).unwrap();
        let actual:Vec<_>=analyses.iter().zip(&parsed.allocations).map(|(analysis,a)|serde_json::json!({
            "allocation":report::allocation(analysis),"frame":report::frame(analysis),"inlining":report::inlining(analysis["inlining"].as_array().unwrap(),false),
            "inlining_links":report::inlining(analysis["inlining"].as_array().unwrap(),true),"schedule":report::schedule(analysis["function"].as_str().unwrap(),analysis["schedule"].as_array().unwrap()),
                    "costs":analysis["inlining"].as_array().unwrap().iter().map(|d|report::cost(d,version::enode_names())).collect::<Vec<_>>(),
            "pcode":report::pcode(&a.before,version::opcode_names(version))})).collect();
        if let Some(d) =
            first_difference(&serde_json::to_value(actual).unwrap(), &expected, "reports")
        {
            panic!("{id}: {d}");
        }
    }
}

#[test]
fn cse_events_and_source_names_match_python() {
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let parsed =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.cse.json"))).unwrap())
                .unwrap();
        let actual =
            serde_json::to_value(mwcc_oracle_core::cse::parse(&parsed.records).unwrap()).unwrap();
        if let Some(d) = first_difference(&actual, &expected, "cse") {
            panic!("{id}: {d}");
        }
    }
}

#[test]
fn cse_ir_exchange_and_blocks_match_python_views() {
    use mwcc_oracle_core::{blocks, cse, document, report, version};
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let trace =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let expected: Value =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.advanced.json"))).unwrap())
                .unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let types = version::enode_names();
        let opcodes = version::opcode_names(version);
        let functions = cse::parse(&trace.records).unwrap();
        let mut actual_cse = serde_json::Map::new();
        for (link, f) in &functions {
            let origins:Vec<_>=f.vregs.values().filter(|v|v[0].as_i64().unwrap_or(-1)>=0).map(|v|{let class=version::class_name(v[1].as_u64().unwrap() as u8);let number=v[0].as_u64().unwrap() as u32;
                serde_json::json!({"class":class,"number":number,"text":cse::origin(f,&class,number,version,types)})}).collect();
            actual_cse.insert(link.clone(),serde_json::json!({"plain":cse::report(link,f,types,version,None),"ir_all":cse::report(link,f,types,version,Some("all")),"ir_line":cse::report(link,f,types,version,Some("3")),"origins":origins}));
        }
        if let Some(d) = first_difference(&Value::Object(actual_cse), &expected["cse"], "cse") {
            panic!("{id}: {d}");
        }
        let functions = blocks::parse(&trace.records).unwrap();
        let actual:serde_json::Map<_,_>=functions.iter().map(|(link,f)|(link.clone(),serde_json::json!({"parsed":f,"cuts":blocks::cuts(f,opcodes,Some(100)),"cuts_none":blocks::cuts(f,opcodes,None),"plain":blocks::report(link,f,opcodes,false,Some(100)),"pcode":blocks::report(link,f,opcodes,true,Some(100)),"limit_zero":blocks::report(link,f,opcodes,false,Some(0))}))).collect();
        if let Some(d) = first_difference(&Value::Object(actual), &expected["blocks"], "blocks") {
            panic!("{id}: {d}");
        }
        let analyses = document::analyze(&trace, version, &Default::default()).unwrap();
        for (analysis, reference) in analyses
            .iter()
            .zip(expected["exchange"].as_array().unwrap())
        {
            for pair in reference["pairs"].as_array().unwrap() {
                let actual = report::exchange(
                    analysis,
                    pair["first"].as_str().unwrap(),
                    pair["second"].as_str().unwrap(),
                );
                if let Some(d) = first_difference(
                    &serde_json::to_value(actual).unwrap(),
                    &pair["text"],
                    "exchange",
                ) {
                    panic!("{id}/{}: {d}", analysis["link"]);
                }
            }
        }
    }
}

#[test]
fn locals_and_target_frames_match_python() {
    use mwcc_oracle_core::{document, locals};
    let root = fixtures();
    let manifest: Value =
        serde_json::from_slice(&fs::read(root.join("manifest.json")).unwrap()).unwrap();
    for case in manifest["cases"].as_array().unwrap() {
        let id = case["id"].as_str().unwrap();
        let trace =
            trace::parse(&fs::read_to_string(root.join(format!("{id}.trace"))).unwrap()).unwrap();
        let version = Version::parse(case["version"].as_str().unwrap()).unwrap();
        let analyses = document::analyze(&trace, version, &Default::default()).unwrap();
        let passes = locals::scalarize(&trace.records).unwrap();
        let bindings = locals::bindings(&trace.records).unwrap();
        let expected: Vec<Value> =
            serde_json::from_slice(&fs::read(root.join(format!("{id}.locals.json"))).unwrap())
                .unwrap();
        for (analysis, e) in analyses.iter().zip(expected) {
            let link = analysis["link"].as_str().unwrap();
            let p = passes.get(link).map_or(&[][..], Vec::as_slice);
            let b = bindings.get(link).map_or(&[][..], Vec::as_slice);
            let target = locals::frame(e["asm"].as_str().unwrap(), link).unwrap();
            let ours: Vec<u32> = serde_json::from_value(e["words"].clone()).unwrap();
            let theirs: Vec<u32> = serde_json::from_value(e["target_words"].clone()).unwrap();
            let limit = e["limit"].as_i64().unwrap();
            let frame = analysis["frame"].as_array().map_or(&[][..], Vec::as_slice);
            let shifts = locals::slot_shifts(&ours, &theirs, frame, limit, target.limit);
            let (runs, mine) = locals::unpaired_objects(&ours, &theirs, frame, limit, target.limit);
            let actual = serde_json::json!({"plain":locals::report(analysis,p,None,b),"summary":locals::summary(analysis,p,b,&[]),
                "target":target,"target_report":locals::report(analysis,p,Some(&target),b),"shifts":shifts,"shift_report":locals::shift_report(&shifts,frame),
                "runs":runs,"mine":mine,"unpaired_report":locals::unpaired_report(&runs,&mine),"target_summary":locals::summary(analysis,p,b,&runs)});
            for (key, value) in actual.as_object().unwrap() {
                if let Some(d) = first_difference(value, &e[key], key) {
                    panic!("{id}/{link}: {d}");
                }
            }
        }
    }
}
