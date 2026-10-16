use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::{
        objective::{
            self, Contract, CopyOutcome, Input, Objective, PromotedElementAnchor, Purpose,
            Selection,
        },
        state,
    },
    version::Version,
};
use std::{fs, path::PathBuf};
fn checks(
    candidate: u32,
    destination: u32,
    source: u32,
    path: &[(&str, &str, u8, &str)],
) -> String {
    path.iter().map(|(condition,site,taken,flags)|format!("pv.coalesce.check pass=1 candidate={candidate} site={site} condition={condition} taken={taken} eflags={flags} eax=0 ebx={source:x} ecx=0 edx={destination:x} esi=0 edi=0 ebp=0\n")).collect()
}
fn candidate(class: u8, id: u32, destination: u32, source: u32) -> String {
    format!(
        "pv.coalesce.candidate pass=1 candidate={id} instruction={id:x} flags=80000010 opcode={} operands=2 destination_kind=0 destination_class={class} destination={destination} source_kind=0 source_class={class} source={source}\n",
        if class == 4 { 139 } else { 158 }
    )
}
fn trace(class: u8) -> String {
    let mut text="pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes array_promotion=guarded_scalar_v1 link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\n".to_owned();
    text += &format!(
        "pv.value.birth schema=1 function=1 value=1 cls={class} register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\n"
    );
    text += &format!(
        "pv.category.window function=1 births=1 f2=32 f3={} f4={} l2=32 l3={} l4={}\n",
        if class == 3 { 33 } else { 32 },
        if class == 4 { 33 } else { 32 },
        if class == 3 { 33 } else { 32 },
        if class == 4 { 33 } else { 32 }
    );
    for (value, index, register) in [(2, 0, 33), (3, 1, 34)] {
        text += &format!(
            "pv.value.birth schema=1 function=1 value={value} cls={class} register={register} count={} physical=32 site={} expression=0 phase=1 line=3 category=unknown\npv.value.array schema=1 function=1 value={value} promotion=100 object=200 register={register} element_index={index} element_size=4 array_size=8 element_count=2 flags={} datatype=0 storage=2 type_kind=12 type_size=8 name_known=1 name_complete=1 name_hex=666c616773\n",
            register + 1,
            if class == 4 { "574005" } else { "573ff2" },
            if class == 3 { 2 } else { 0 }
        );
    }
    text += &format!(
        "pv.coalesce.begin schema=1 pass=1 cls={class} count=35 physical=32 first=33 last=33 special=1 function_id=1 creation=direct_counter_writes link=pipeline__Fii\n"
    );
    let prefix = [
        ("copy_flag", "10f", 0, "40"),
        ("destination_kind", "17b", 0, "40"),
        ("register_class", "18b", 0, "40"),
        ("roots_differ", "1d6", 1, "0"),
        ("interference", "244", 0, "40"),
    ];
    text += &candidate(class, 1, 33, 3);
    text += &checks(1, 33, 3, &prefix);
    if class == 4 {
        text += &checks(1, 33, 3, &[("gpr_special_register", "2b9", 0, "0")]);
    }
    text += "pv.coalesce.merge pass=1 candidate=1 parent=3 child=33 observed_parent=3\n";
    text += &candidate(class, 2, 34, 32);
    text += &checks(2, 34, 32, &prefix);
    text += &checks(
        2,
        34,
        32,
        &[
            ("destination_below_window", "271", 0, "40"),
            ("destination_above_window", "280", 1, "0"),
        ],
    );
    text += &candidate(class, 3, 34, 34);
    text += &checks(
        3,
        34,
        34,
        &[
            ("copy_flag", "10f", 0, "40"),
            ("destination_kind", "17b", 0, "40"),
            ("register_class", "18b", 0, "40"),
            ("roots_differ", "1d6", 0, "40"),
        ],
    );
    text += "pv.coalesce.end pass=1 candidates=3\n";
    text += &format!(
        "pv.graph.begin schema=1 function=1 graph=1 births=3 cls={class} physical=32 start=32 count=35 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first=33 last=33\n"
    );
    for register in 32..35 {
        text += &format!("pv.graph.node v={register} visible=0 deg=0 flags=0 color=-1 n=\n");
    }
    text += &format!(
        "pv.graph.end function=1 graph=1 nodes=3 instructions=0 complete=1\npv.function.end function=1 births=3 graphs=1 ranges=2 array_promotions=2 c0=4 c1=8 c2=32 c3={} c4={}\n",
        if class == 3 { 35 } else { 32 },
        if class == 4 { 35 } else { 32 }
    );
    text
}
fn compilation(trace: &str) -> Compilation {
    let object = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
    Compilation {
        version: Version::Gc27,
        source: "model.cpp".into(),
        oracle: "model".into(),
        producer_sha256: "producer".into(),
        original_sha256: Some("stock".into()),
        source_sha256: state::digest(b"model"),
        flags: vec!["-O4,p".into()],
        trace_options: TraceOptions {
            backend: true,
            ..Default::default()
        },
        object_sha256: state::digest(&object),
        check_status: CheckStatus::Verified,
        identical: Some(true),
        differences: vec![],
        diagnostics: String::new(),
        trace: trace.into(),
        trace_present: true,
        original_fallback: false,
        object,
    }
}
fn contract(class: u8, index: u32, copy: usize, outcome: CopyOutcome) -> Contract {
    Contract {
        format_version: 1,
        selection: Selection {
            link: "pipeline__Fii".into(),
            class,
            round: 0,
        },
        objective: Objective::PromotedCopyCoalescing {
            value: PromotedElementAnchor {
                name: "flags".into(),
                element_index: index,
                element_size: 4,
            },
            copy,
            outcome,
        },
        relocations: vec![],
        purpose: Purpose::MechanismReproducer,
    }
}
fn evaluate(c: &Contract, trace: &str) -> objective::EvaluationReport {
    let a = compilation(trace);
    let ar = provenance::parse(trace).unwrap();
    let input = Input {
        compilation: &a,
        provenance: &ar,
        source: b"model",
        semantic_check: None,
    };
    objective::evaluate(c, &input, &input).unwrap()
}
fn rejected() -> CopyOutcome {
    CopyOutcome::Rejected {
        condition: "destination_above_window".into(),
    }
}
#[test]
fn promoted_first_copy_merges_second_rejects_and_later_same_root_is_not_an_initializer_claim() {
    for class in [3, 4] {
        let text = trace(class);
        let first = evaluate(&contract(class, 0, 0, CopyOutcome::Coalesced), &text);
        assert_eq!(
            first.overall.state,
            EvaluationState::Satisfied,
            "{first:#?}"
        );
        assert_eq!(first.observations["selected"]["decision"]["kind"], "merged");
        let second = evaluate(&contract(class, 1, 0, rejected()), &text);
        assert_eq!(second.overall.state, EvaluationState::Satisfied);
        assert_eq!(
            second.observations["incoming_copies"]
                .as_array()
                .unwrap()
                .len(),
            2
        );
        let same = evaluate(&contract(class, 1, 1, CopyOutcome::Coalesced), &text);
        assert_eq!(same.overall.state, EvaluationState::Satisfied);
        assert_eq!(
            same.observations["selected"]["decision"]["kind"],
            "already_same_root"
        );
        assert_eq!(
            evaluate(&contract(class, 1, 0, CopyOutcome::Coalesced), &text)
                .overall
                .state,
            EvaluationState::Unsatisfied
        );
        assert_eq!(
            evaluate(&contract(class, 1, 2, rejected()), &text)
                .overall
                .state,
            EvaluationState::Unsatisfied
        );
        let json = serde_json::to_value(contract(class, 1, 0, rejected())).unwrap();
        assert_eq!(json["objective"]["kind"], "promoted_copy_coalescing");
        let _: Contract = serde_json::from_value(json).unwrap();
    }
}
#[test]
fn partial_or_ambiguous_array_pass_generation_opcode_and_window_evidence_is_indeterminate() {
    let text = trace(4);
    let c = contract(4, 1, 0, rejected());
    for changed in [
        text.replace("array_promotions=2", "array_promotions=1"),
        text.replace("name_complete=1", "name_complete=0"),
        text.replace(
            "array_promotion=guarded_scalar_v1",
            "array_promotion=unknown",
        ),
        text.replace("pv.coalesce.end pass=1 candidates=3\n", ""),
        text.replace("first=33 last=33 special=1", "first=32 last=33 special=1"),
        text.replace("opcode=139", "opcode=137"),
        text.replace("category_ready=1", "category_ready=0"),
        text.replace("element_index=0", "element_index=1"),
    ] {
        assert_eq!(
            evaluate(&c, &changed).overall.state,
            EvaluationState::Indeterminate
        );
    }
    let a = compilation(&text);
    let base = provenance::parse(&text).unwrap();
    for kind in 0..4 {
        let mut report = base.clone();
        match kind {
            0 => report.history.functions[0].births[2].generation = 1,
            1 => report.graphs.graphs[0]
                .nodes
                .get_mut(&34)
                .unwrap()
                .binding
                .direct
                .clear(),
            2 => {
                report.passes[0].candidates[1].decision_position =
                    Some(report.graphs.graphs[0].begin_position + 1)
            }
            _ => report.passes[0]
                .header
                .insert("count".into(), "36".into())
                .map(|_| ())
                .unwrap_or(()),
        };
        let input = Input {
            compilation: &a,
            provenance: &report,
            source: b"model",
            semantic_check: None,
        };
        assert_eq!(
            objective::evaluate(&c, &input, &input)
                .unwrap()
                .overall
                .state,
            EvaluationState::Indeterminate
        );
    }
}
#[test]
fn invalid_promoted_anchor_and_rejection_request_are_rejected_at_validation() {
    let mut c = contract(4, 1, 0, rejected());
    c.selection.class = 2;
    assert!(objective::validate(&c).is_err());
    c.selection.class = 4;
    for (name, index, size) in [("", 1, 4), ("flags", 8, 4), ("flags", 1, 0)] {
        c.objective = Objective::PromotedCopyCoalescing {
            value: PromotedElementAnchor {
                name: name.into(),
                element_index: index,
                element_size: size,
            },
            copy: 0,
            outcome: rejected(),
        };
        assert!(objective::validate(&c).is_err());
    }
    c = contract(
        4,
        1,
        0,
        CopyOutcome::Rejected {
            condition: "invented_reason".into(),
        },
    );
    assert!(objective::validate(&c).is_err());
}

#[test]
fn ghost_promotion_nodes_without_bound_incoming_copies_are_indeterminate() {
    let text = trace(4);
    let start = text
        .find("pv.coalesce.candidate pass=1 candidate=2")
        .unwrap();
    let end = text.find("pv.coalesce.end pass=1").unwrap();
    let ghost =
        format!("{}{}", &text[..start], &text[end..]).replace("candidates=3", "candidates=1");
    let report = evaluate(&contract(4, 1, 0, CopyOutcome::Coalesced), &ghost);
    assert_eq!(report.overall.state, EvaluationState::Indeterminate);
    assert!(
        report.observations["incoming_copies"]
            .as_array()
            .unwrap()
            .is_empty()
    );
    assert!(
        report
            .objective
            .reasons
            .iter()
            .any(|r| r.code == mwcc_oracle_core::evidence::EvidenceGap::BackendLifecycleCoverage)
    );
}

#[test]
fn report_binding_and_pass_end_must_precede_the_selected_graph() {
    let text = trace(4);
    let c = contract(4, 1, 0, rejected());
    let closing = "pv.coalesce.end pass=1 candidates=3\n";
    let late = text
        .replace(closing, "")
        .replace("pv.function.end", &format!("{closing}pv.function.end"));
    assert_eq!(
        evaluate(&c, &late).overall.state,
        EvaluationState::Indeterminate
    );
    let a = compilation(&text);
    let other = provenance::parse(&format!("\n{text}")).unwrap();
    let input = Input {
        compilation: &a,
        provenance: &other,
        source: b"model",
        semantic_check: None,
    };
    let result = objective::evaluate(&c, &input, &input).unwrap();
    assert_eq!(result.objective.state, EvaluationState::Satisfied);
    assert_eq!(
        result.constraints["captured_provenance"].state,
        EvaluationState::Indeterminate
    );
    assert_eq!(result.overall.state, EvaluationState::Indeterminate);
}
