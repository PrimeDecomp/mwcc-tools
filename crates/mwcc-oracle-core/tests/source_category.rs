use mwcc_oracle_core::{
    allocation_category::Category,
    evidence::{EvaluationState, EvidenceGap},
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

fn trace(first: u32) -> String {
    let mut text = format!(
        "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\npv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=1 object=00000001 info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=666c6167\npv.category.window function=1 births=1 f2=32 f3=32 f4={first} l2=32 l3=32 l4={first}\npv.graph.begin schema=1 function=1 graph=1 births=1 cls=4 physical=32 start=32 count=33 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first={first} last={first}\npv.graph.node v=32 visible=0 deg=0 flags=0 color=-1 n=\npv.graph.end function=1 graph=1 nodes=1 instructions=0 complete=1\npv.function.end function=1 births=1 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=33\n"
    );
    if first == 32 {
        let marker = "pv.category.window function=1 births=1 f2=32 f3=32 f4=32 l2=32 l3=32 l4=32\n";
        text = text.replace(marker, "").replace(
            "pv.value.birth",
            &format!("{}pv.value.birth", marker.replace("births=1", "births=0")),
        );
    }
    text
}
fn compilation(trace: &str) -> Compilation {
    let object = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
    Compilation {
        module: None,
        version: Version::Gc27,
        source: "model.c".into(),
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
fn anchor() -> ObjectAnchor {
    ObjectAnchor {
        name: "flag".into(),
        part: ObjectPart::Single,
        type_kind: 1,
        type_size: 4,
    }
}
fn contract(target: Category) -> Contract {
    Contract {
        format_version: 1,
        selection: Selection {
            link: "pipeline__Fii".into(),
            class: 4,
            round: 0,
        },
        objective: Objective::RangeCategory {
            value: anchor(),
            target,
        },
        relocations: vec![],
        purpose: Purpose::MechanismReproducer,
    }
}
fn evaluate(
    c: &Contract,
    a: &Compilation,
    ar: &provenance::Report,
    b: &Compilation,
    br: &provenance::Report,
) -> objective::EvaluationReport {
    objective::evaluate(
        c,
        &Input {
            compilation: a,
            provenance: ar,
            source: b"model",
            semantic_check: None,
        },
        &Input {
            compilation: b,
            provenance: br,
            source: b"model",
            semantic_check: None,
        },
    )
    .unwrap()
}
#[test]
fn static_category_is_not_a_transition_and_rebinds_each_candidate() {
    let a = compilation(&trace(32));
    let ar = provenance::parse(&a.trace).unwrap();
    let c = contract(Category::InsideCoalescingWindow);
    let result = evaluate(&c, &a, &ar, &a, &ar);
    assert_eq!(
        result.overall.state,
        EvaluationState::Satisfied,
        "{result:#?}"
    );
    assert!(!result.constraints.contains_key("instructions_preserved"));
    assert_eq!(result.observations["candidate_root"], 32);
    let instructions_changed=compilation(&a.trace.replace("pv.graph.node", "pv.graph.instruction p=aaa op=137 flags=80004000 n=2 line=3 ops=000402002000000000000000 020000000000000000000000\npv.graph.node").replace("visible=0","visible=1").replace("instructions=0","instructions=1"));
    let instructions_changed_report = provenance::parse(&instructions_changed.trace).unwrap();
    assert_eq!(
        evaluate(
            &c,
            &a,
            &ar,
            &instructions_changed,
            &instructions_changed_report
        )
        .overall
        .state,
        EvaluationState::Satisfied
    );
    let b = compilation(&trace(33));
    let br = provenance::parse(&b.trace).unwrap();
    assert_eq!(
        evaluate(&c, &a, &ar, &b, &br).overall.state,
        EvaluationState::Unsatisfied
    );
    assert_eq!(
        evaluate(
            &contract(Category::BeforeCoalescingWindow),
            &a,
            &ar,
            &b,
            &br
        )
        .overall
        .state,
        EvaluationState::Satisfied
    );
    let mut transition = c.clone();
    transition.objective = Objective::ChangeRangeCategory {
        value: anchor(),
        target: Category::InsideCoalescingWindow,
    };
    assert_eq!(
        evaluate(&transition, &a, &ar, &a, &ar).objective.state,
        EvaluationState::Unsatisfied
    );
    let json = serde_json::to_value(&c).unwrap();
    assert_eq!(json["objective"]["kind"], "range_category");
    let decoded: Contract = serde_json::from_value(json).unwrap();
    assert!(matches!(
        decoded.objective,
        Objective::RangeCategory {
            target: Category::InsideCoalescingWindow,
            ..
        }
    ));
}
#[test]
fn current_category_requires_complete_anchor_binding_range_and_hard_gates() {
    let a = compilation(&trace(32));
    let ar = provenance::parse(&a.trace).unwrap();
    let c = contract(Category::InsideCoalescingWindow);
    for changed in [
        a.trace.replace("name_complete=1", "name_complete=0"),
        a.trace.replace("category_ready=1", "category_ready=0"),
        a.trace.replace("ranges=2", "ranges=1"),
        a.trace.replace("c4=33", "c4=34"),
    ] {
        let b = compilation(&changed);
        let br = provenance::parse(&b.trace).unwrap();
        assert_eq!(
            evaluate(&c, &a, &ar, &b, &br).overall.state,
            EvaluationState::Indeterminate
        );
    }
    let mut ambiguous = ar.clone();
    let mut duplicate = ambiguous.history.functions[0].births[0].clone();
    duplicate.id.sequence = 2;
    ambiguous.history.functions[0].births.push(duplicate);
    assert_eq!(
        evaluate(&c, &a, &ar, &a, &ambiguous).overall.state,
        EvaluationState::Indeterminate
    );
    let mut br = ar.clone();
    br.graphs.graphs[0]
        .nodes
        .get_mut(&32)
        .unwrap()
        .binding
        .direct
        .clear();
    assert_eq!(
        evaluate(&c, &a, &ar, &a, &br).overall.state,
        EvaluationState::Indeterminate
    );
    let mut wrong_source = compilation(&a.trace);
    wrong_source.source_sha256 = state::digest(b"wrong");
    assert_eq!(
        evaluate(&c, &a, &ar, &wrong_source, &ar).overall.state,
        EvaluationState::Indeterminate
    );
    let mut skipped = compilation(&a.trace);
    skipped.check_status = CheckStatus::Skipped;
    let result = evaluate(&c, &a, &ar, &skipped, &ar);
    assert_eq!(result.overall.state, EvaluationState::Indeterminate);
    assert!(
        result
            .overall
            .reasons
            .iter()
            .any(|r| r.code == EvidenceGap::CompilationCoverage)
    );
}

#[test]
fn physical_category_requires_an_observed_merge_not_a_final_color() {
    let a = compilation(&trace(32));
    let ar = provenance::parse(&a.trace).unwrap();
    let mut pass="pv.coalesce.begin schema=1 pass=1 cls=4 count=33 physical=32 first=32 last=33 special=1 function_id=1 creation=direct_counter_writes link=pipeline__Fii\npv.coalesce.candidate pass=1 candidate=1 instruction=aaa flags=10 opcode=123 operands=2 destination_kind=0 destination_class=4 destination=32 source_kind=0 source_class=4 source=4\n".to_owned();
    for (condition, site, taken, flags) in [
        ("copy_flag", "10f", 0, "40"),
        ("destination_kind", "17b", 0, "40"),
        ("register_class", "18b", 0, "40"),
        ("roots_differ", "1d6", 1, "0"),
        ("interference", "244", 0, "40"),
        ("gpr_special_register", "2b9", 0, "0"),
    ] {
        pass += &format!(
            "pv.coalesce.check pass=1 candidate=1 site={site} condition={condition} taken={taken} eflags={flags} eax=0 ebx=4 ecx=0 edx=20 esi=0 edi=0 ebp=0\n"
        );
    }
    pass += "pv.coalesce.merge pass=1 candidate=1 parent=4 child=32 observed_parent=4\npv.coalesce.end pass=1 candidates=1\n";
    let b = compilation(
        &a.trace
            .replace("pv.graph.begin", &format!("{pass}pv.graph.begin")),
    );
    let br = provenance::parse(&b.trace).unwrap();
    assert_eq!(br.graphs.graphs[0].merges.len(), 1);
    assert_eq!(
        br.graphs.graphs[0].merges[0].binding.state,
        EvaluationState::Satisfied
    );
    let result = evaluate(&contract(Category::Physical), &a, &ar, &b, &br);
    assert_eq!(result.overall.state, EvaluationState::Satisfied);
    assert_eq!(result.observations["candidate_root"], 4);
    let colored = compilation(&a.trace.replace("color=-1", "color=4"));
    let colored_report = provenance::parse(&colored.trace).unwrap();
    assert_eq!(
        evaluate(
            &contract(Category::Physical),
            &a,
            &ar,
            &colored,
            &colored_report
        )
        .overall
        .state,
        EvaluationState::Unsatisfied
    );
    let mut incomplete = br.clone();
    incomplete.graphs.graphs[0].merges[0].binding =
        mwcc_oracle_core::evidence::Evaluation::indeterminate(
            EvidenceGap::AmbiguousCorrespondence,
            "test missing merge binding",
        );
    assert_eq!(
        evaluate(&contract(Category::Physical), &a, &ar, &b, &incomplete)
            .overall
            .state,
        EvaluationState::Indeterminate
    );
}

#[test]
fn reducer_and_static_index_accept_current_category_but_reject_transitions() {
    use mwcc_oracle_core::source_guided::{index, reduction, search::Build};
    let directory = tempfile::tempdir().unwrap();
    let c = contract(Category::InsideCoalescingWindow);
    let mut plan = reduction::Plan {
        format_version: 1,
        build: Build {
            oracle_root: directory.path().into(),
            working_directory: directory.path().into(),
            source: "model.c".into(),
            version: Some(Version::Gc27),
            flags: vec![],
            from_ninja: None,
            traces: TraceOptions {
                backend: true,
                ..Default::default()
            },
            timeout_seconds: 1,
        },
        contract: c.clone(),
        reductions: vec![],
        delete_lines: true,
        semantic_check: None,
        target: None,
    };
    reduction::validate(&plan).unwrap();
    fs::create_dir(directory.path().join("entries")).unwrap();
    let mut query = index::Query {
        format_version: 1,
        link: None,
        version: None,
        flags: None,
        class: 4,
        round: 0,
        objective: c.objective,
        relocations: vec![],
    };
    assert!(
        index::query(directory.path(), &query)
            .unwrap()
            .hits
            .is_empty()
    );
    let transition = Objective::ChangeRangeCategory {
        value: anchor(),
        target: Category::InsideCoalescingWindow,
    };
    plan.contract.objective = transition.clone();
    query.objective = transition;
    assert!(reduction::validate(&plan).is_err());
    assert!(index::query(directory.path(), &query).is_err());
}
