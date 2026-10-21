//! Relevant-state grouping retains inputs read by source definition predicates.
mod common;
use common::{register, trace};
use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::{
        objective::{self, Contract, Input, ObjectAnchor, Objective, Purpose, Selection},
        state,
    },
    value_history::ObjectPart,
    version::Version,
};
use std::{fs, path::PathBuf};

fn compilation(text: String) -> Compilation {
    let source = b"int flag=0; use(flag); flag=1;";
    let object = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
    Compilation {
        module: None,
        version: Version::Gc27,
        source: "model.cpp".into(),
        oracle: "model".into(),
        producer_sha256: "producer".into(),
        original_sha256: Some("stock".into()),
        source_sha256: state::digest(source),
        flags: vec![],
        trace_options: TraceOptions {
            backend: true,
            ..Default::default()
        },
        object_sha256: state::digest(&object),
        check_status: CheckStatus::Verified,
        identical: Some(true),
        differences: vec![],
        diagnostics: String::new(),
        trace: text,
        trace_present: true,
        original_fallback: false,
        object,
    }
}
fn evaluate_trace(text: String, objective: Objective) -> objective::EvaluationReport {
    let source = b"int flag=0; use(flag); flag=1;";
    let compilation = compilation(text);
    let report = provenance::parse(&compilation.trace).unwrap();
    let input = Input {
        compilation: &compilation,
        provenance: &report,
        source,
        semantic_check: None,
    };
    let contract = Contract {
        format_version: 1,
        selection: Selection {
            link: "pipeline__Fii".into(),
            class: 4,
            round: 0,
        },
        objective,
        relocations: vec![],
        purpose: Purpose::MechanismReproducer,
    };
    objective::evaluate(&contract, &input, &input).unwrap()
}
fn scalarized(text: String) -> String {
    let frontend = "pv.scalar.begin schema=1 pass=1 function=1 function_object=abc coverage=guarded_temp_returns_v1 link=pipeline__Fii\npv.scalar.binding schema=1 pass=1 binding=1 function=1 function_object=abc site=461748 part=100 source_linear=101 source_index=7 source_kind=2 variable=102 source_object=110 source_info=111 source_type=112 source_type_kind=12 source_type_size=8 source_datatype=1 source_storage=0 source_name_known=1 source_name_complete=1 source_name_hex=666c6167 source_element_known=1 source_element_type_kind=1 source_element_type_size=4 byte_offset=4 byte_size=4 bit_first=32 bit_last=63 flags=0 temporary=1 temporary_info=123 temporary_type=202 temporary_type_kind=1 temporary_type_size=4\npv.scalar.end schema=1 pass=1 function=1 function_object=abc bindings=1 coverage=guarded_temp_returns_v1\n";
    text.replace("function=1 parent=0","function=1 parent=0 function_object=abc")
        .replacen("pv.value.birth",&(frontend.to_owned()+"pv.value.birth"),1)
        .replace("pv.function.end function=1", "pv.function.end function=1 scalarization=guarded_temp_returns_v1 scalar_passes=1 scalar_bindings=1 scalar_first_pass=1 scalar_last_pass=1")
}
fn definition(ordinal: usize) -> Objective {
    Objective::ScalarizedArrayDefinition {
        value: objective::PromotedElementAnchor {
            name: "flag".into(),
            element_index: 1,
            element_size: 4,
        },
        definition: ordinal,
    }
}
fn non_li_definition() -> String {
    let mut text = trace();
    for (prefix, number) in [("ra.i", 32), ("pv.graph.instruction", 32), ("ra.i", 5)] {
        let old = format!(
            "{prefix} p=ccc op=137 flags=80004000 n=2 line=20 ops={} 020001000000000000000000",
            register(number, 2)
        );
        let new = format!(
            "{prefix} p=ccc op=103 flags=88004000 n=5 line=20 ops={} {} 02001b000000000000000000 020005000000000000000000 02001f000000000000000000",
            register(number, 2),
            register(3, 1)
        );
        text = text.replace(&old, &new);
    }
    scalarized(text)
}

fn fingerprint(text: &str) -> String {
    let report = provenance::parse(text).unwrap();
    state::fingerprint(
        &report.graphs.graphs[0],
        &report,
        &compilation(text.to_owned()),
    )
    .unwrap()
}

#[test]
fn arena_captures_cannot_use_fingerprints_without_arena_normalization() {
    let text = trace();
    let report = provenance::parse(&text).unwrap();
    let mut capture = compilation(text.clone());
    capture.trace_options.source_arenas = true;
    assert!(state::fingerprint(&report.graphs.graphs[0], &report, &capture).is_err());
    let with_record = format!("{text}\npv.arena.unknown schema=1\n");
    let report = provenance::parse(&with_record).unwrap();
    assert!(
        state::fingerprint(&report.graphs.graphs[0], &report, &compilation(with_record)).is_err()
    );
}

#[test]
fn scalarized_definition_fingerprint_keeps_preallocation_definition_shape() {
    let original = non_li_definition();
    let changed = original.replacen("ra.i p=ccc op=103", "ra.i p=ccc op=102", 1);
    assert_ne!(original, changed);
    assert_eq!(
        evaluate_trace(original.clone(), definition(1))
            .overall
            .state,
        EvaluationState::Satisfied
    );
    assert_eq!(
        evaluate_trace(changed.clone(), definition(1)).overall.state,
        EvaluationState::Unsatisfied
    );
    assert_ne!(
        fingerprint(&original),
        fingerprint(&changed),
        "different preallocation definition outcomes must not group together"
    );
}

#[test]
fn initialization_fingerprint_keeps_preallocation_cfg_topology() {
    let original = trace();
    let changed = original.replacen(
        "ra.block p=100 index=0 weight=1 flags=0 succ=",
        "ra.block p=100 index=0 weight=1 flags=0 succ=100,",
        1,
    );
    let objective = || Objective::DirectInitialization {
        value: ObjectAnchor {
            name: "flag".into(),
            part: ObjectPart::Single,
            type_kind: 1,
            type_size: 4,
        },
        constant: 0,
        source_line: None,
    };
    assert_eq!(
        evaluate_trace(original.clone(), objective())
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    assert_eq!(
        evaluate_trace(changed.clone(), objective()).objective.state,
        EvaluationState::Indeterminate
    );
    assert_ne!(
        fingerprint(&original),
        fingerprint(&changed),
        "an unsupported cyclic preallocation CFG must not group with supported initialization evidence"
    );
}

#[test]
fn scalarized_definition_fingerprint_keeps_candidate_instruction_binding() {
    let original = scalarized(trace());
    let changed = original.replace(
        "pv.coalesce.candidate pass=1 candidate=1 instruction=bbb",
        "pv.coalesce.candidate pass=1 candidate=1 instruction=bba",
    );
    assert_ne!(original, changed);
    assert_eq!(
        evaluate_trace(original.clone(), definition(0))
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    assert_eq!(
        evaluate_trace(changed.clone(), definition(0))
            .objective
            .state,
        EvaluationState::Indeterminate
    );
    assert_ne!(
        fingerprint(&original),
        fingerprint(&changed),
        "a missing candidate binding must not group with an observed rejection"
    );
}

#[test]
fn coherent_instruction_and_cfg_pointer_renaming_keeps_fingerprint() {
    let original = non_li_definition();
    let renamed = original
        .replace("p=aaa", "p=daaa")
        .replace("p=bbb", "p=dbbb")
        .replace("p=ccc", "p=dccc")
        .replace("instruction=bbb", "instruction=dbbb")
        .replace("p=100 ", "p=d100 ");
    assert_eq!(
        evaluate_trace(renamed.clone(), definition(1)).overall.state,
        EvaluationState::Satisfied
    );
    assert_eq!(fingerprint(&original), fingerprint(&renamed));
}

#[test]
fn initializer_source_line_and_unknown_allocation_fields_do_not_group() {
    let original = non_li_definition();
    let changed = original.replacen(
        "ra.i p=ccc op=103 flags=88004000 n=5 line=20",
        "ra.i p=ccc op=103 flags=88004000 n=5 line=21",
        1,
    );
    assert_ne!(fingerprint(&original), fingerprint(&changed));
    let unknown = original.replacen("ra.block p=100", "ra.block future=opaque p=100", 1);
    let report = provenance::parse(&unknown).unwrap();
    assert!(state::fingerprint(&report.graphs.graphs[0], &report, &compilation(unknown)).is_err());
}

#[test]
fn different_instruction_pointer_spellings_keep_literal_join_semantics() {
    let original = non_li_definition();
    let changed = original.replacen("ra.i p=ccc op=103", "ra.i p=0ccc op=103", 1);
    assert_eq!(
        evaluate_trace(original.clone(), definition(1))
            .overall
            .state,
        EvaluationState::Satisfied
    );
    assert_eq!(
        evaluate_trace(changed.clone(), definition(1)).overall.state,
        EvaluationState::Unsatisfied
    );
    assert_ne!(fingerprint(&original), fingerprint(&changed));
    let unknown = original.replace(
        "pv.coalesce.end pass=1 candidates=1",
        "pv.coalesce.end pass=1 candidates=1 future=opaque",
    );
    let report = provenance::parse(&unknown).unwrap();
    assert!(state::fingerprint(&report.graphs.graphs[0], &report, &compilation(unknown)).is_err());
}

#[test]
fn object_anchor_ambiguity_on_a_birth_without_a_graph_node_changes_state() {
    // A partial graph must not erase a competing source identity. Both captures
    // retain the same partial-graph gap; only the birth's source name changes.
    let original = trace()
        .replace(
            "pv.graph.node v=33 visible=1 deg=1 flags=0 color=-1 n=32,\n",
            "",
        )
        .replace(
            "nodes=2 instructions=3 complete=1",
            "nodes=1 instructions=3 complete=1",
        );
    let changed = original.replace("name_hex=74656d70", "name_hex=666c6167");
    let objective = || Objective::DirectInitialization {
        value: ObjectAnchor {
            name: "flag".into(),
            part: ObjectPart::Single,
            type_kind: 1,
            type_size: 4,
        },
        constant: 0,
        source_line: None,
    };
    let original_evaluation = evaluate_trace(original.clone(), objective());
    let changed_evaluation = evaluate_trace(changed.clone(), objective());
    assert_eq!(
        original_evaluation.overall.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        changed_evaluation.overall.state,
        EvaluationState::Indeterminate
    );
    assert_ne!(
        serde_json::to_value(original_evaluation.objective).unwrap(),
        serde_json::to_value(changed_evaluation.objective).unwrap()
    );
    assert_ne!(fingerprint(&original), fingerprint(&changed));
}
