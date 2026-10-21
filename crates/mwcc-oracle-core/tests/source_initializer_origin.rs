//! Initialization must not be relabeled by selecting a later constant write.
//! Synthetic complete observation fixture; no compiler/source-equivalence claim.
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

fn register(number: u16, effect: u16) -> String {
    let effect = effect.to_le_bytes();
    let number = number.to_le_bytes();
    format!(
        "0004{:02x}{:02x}{:02x}{:02x}000000000000",
        effect[0], effect[1], number[0], number[1]
    )
}
fn instructions(prefix: &str, physical: bool) -> String {
    let (flag, temporary) = if physical { (5, 6) } else { (32, 33) };
    format!(
        "{prefix} p=aaa op=137 flags=80004000 n=2 line=10 ops={} 020000000000000000000000\n\
         {prefix} p=bbb op=139 flags=80000010 n=3 line=10 ops={} {} 060000000000000000000000\n\
         {prefix} p=ccc op=137 flags=80004000 n=2 line=20 ops={} 020001000000000000000000\n",
        register(temporary, 2),
        register(flag, 2),
        register(temporary, 1),
        register(flag, 2)
    )
}
fn trace() -> String {
    let mut text = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\n".to_owned();
    for (id, register, name) in [(1, 32, "666c6167"), (2, 33, "74656d70")] {
        text += &format!(
            "pv.value.birth schema=1 function=1 value={id} cls=4 register={register} count={} physical=32 site=400028 expression=0 phase=0 line=10 category=unknown\npv.value.object function=1 value={id} object={id:08x} info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex={name}\n",
            register + 1
        );
    }
    text += "pv.category.window function=1 births=2 f2=32 f3=32 f4=34 l2=32 l3=32 l4=34\nra.before function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n";
    text += &instructions("ra.i", false);
    text += "ra.end\npv.coalesce.begin schema=1 pass=1 cls=4 count=34 physical=32 first=34 last=34 special=1 function_id=1 creation=direct_counter_writes link=pipeline__Fii\npv.coalesce.candidate pass=1 candidate=1 instruction=bbb flags=80000010 opcode=139 operands=3 destination_kind=0 destination_class=4 destination=32 source_kind=0 source_class=4 source=33\n";
    for (condition, site, taken, eflags) in [
        ("copy_flag", "10f", 0, "40"),
        ("destination_kind", "17b", 0, "40"),
        ("register_class", "18b", 0, "40"),
        ("roots_differ", "1d6", 1, "0"),
        ("interference", "244", 0, "40"),
        ("destination_below_window", "271", 1, "80"),
    ] {
        text += &format!(
            "pv.coalesce.check pass=1 candidate=1 site={site} condition={condition} taken={taken} eflags={eflags} eax=0 ebx=21 ecx=0 edx=20 esi=0 edi=0 ebp=0\n"
        );
    }
    text += "pv.coalesce.end pass=1 candidates=1\npv.graph.begin schema=1 function=1 graph=1 births=2 cls=4 physical=32 start=32 count=34 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first=34 last=34\n";
    text += &instructions("pv.graph.instruction", false);
    text += "pv.graph.node v=32 visible=1 deg=1 flags=0 color=-1 n=33,\npv.graph.node v=33 visible=1 deg=1 flags=0 color=-1 n=32,\npv.graph.end function=1 graph=1 nodes=2 instructions=3 complete=1\nra.graph cls=4 k=29 start=32 count=34\nra.node v=32 p=1000 deg=1 flags=0 color=-1 n=33,\nra.node v=33 p=1020 deg=1 flags=0 color=-1 n=32,\nra.order cls=4 v=1000:0,1020:0,\nra.select cls=4 mask=0 after=60 ok=1 v=1000:5:0,1020:6:0,\nra.after function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n";
    text += &instructions("ra.i", true);
    text += "ra.end\npv.function.end function=1 births=2 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=34\n";
    text
}
fn evaluate_trace(text: String, objective: Objective) -> objective::EvaluationReport {
    let source = b"int flag=0; use(flag); flag=1;";
    let object = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
    let compilation = Compilation {
        version: Version::Gc27,
        source: "model.cpp".into(),
        oracle: "model".into(),
        module: None,
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
    };
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
fn initialization(constant: i32, source_line: Option<i32>) -> Objective {
    Objective::DirectInitialization {
        value: ObjectAnchor {
            name: "flag".into(),
            part: ObjectPart::Single,
            type_kind: 1,
            type_size: 4,
        },
        constant,
        source_line,
    }
}
fn evaluate(constant: i32, source_line: Option<i32>) -> objective::EvaluationReport {
    evaluate_trace(trace(), initialization(constant, source_line))
}
#[test]
fn original_zero_initialization_still_requires_its_retained_copy() {
    assert_eq!(
        evaluate(0, None).objective.state,
        EvaluationState::Unsatisfied
    );
}
#[test]
fn later_matching_constant_is_not_the_original_initializer() {
    let report = evaluate(1, None);
    assert!(
        report
            .constraints
            .values()
            .all(|e| e.state == EvaluationState::Satisfied)
    );
    assert_ne!(
        report.objective.state,
        EvaluationState::Satisfied,
        "later LI1 incorrectly relabeled as source initializer: {:?}; {}",
        report.overall,
        report.observations
    );
}
#[test]
fn source_line_cannot_relabel_a_later_assignment_as_initialization() {
    let report = evaluate(1, Some(20));
    assert!(
        report
            .constraints
            .values()
            .all(|e| e.state == EvaluationState::Satisfied)
    );
    assert_ne!(
        report.objective.state,
        EvaluationState::Satisfied,
        "later assignment selected by line is not the source initializer: {:?}; {}",
        report.overall,
        report.observations
    );
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
#[test]
fn explicit_definition_ordinal_accepts_non_li_without_claiming_initialization() {
    let text = non_li_definition();
    let result = evaluate_trace(text.clone(), definition(1));
    assert_eq!(
        result.overall.state,
        EvaluationState::Satisfied,
        "{:?}",
        result.objective
    );
    assert_eq!(result.observations["producer"]["opcode"], 103);
    assert_eq!(result.observations["assigned_register"], 5);
    assert!(
        result.observations["scope"]
            .as_str()
            .unwrap()
            .contains("no C++ initialization")
    );
    assert!(result.observations.get("initialization").is_none());
    assert_eq!(
        evaluate_trace(text.clone(), definition(0)).objective.state,
        EvaluationState::Unsatisfied
    );
    assert_eq!(
        evaluate_trace(text.clone(), definition(2)).objective.state,
        EvaluationState::Unsatisfied
    );
    let changed = text.replace(
        "ra.i p=ccc op=103 flags=88004000 n=5 line=20 ops=000402000500",
        "ra.i p=ccc op=102 flags=88004000 n=5 line=20 ops=000402000500",
    );
    assert_eq!(
        evaluate_trace(changed, definition(1)).objective.state,
        EvaluationState::Unsatisfied
    );
    let fixed_source = format!(
        "ra.i p=ccc op=103 flags=88004000 n=5 line=20 ops={} {}",
        register(5, 2),
        register(3, 1)
    );
    let wrong_source = format!(
        "ra.i p=ccc op=103 flags=88004000 n=5 line=20 ops={} {}",
        register(5, 2),
        register(4, 1)
    );
    assert_eq!(
        evaluate_trace(text.replace(&fixed_source, &wrong_source), definition(1))
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    let partial = text.replace("temporary_info=123", "temporary_info=456");
    assert_eq!(
        evaluate_trace(partial, definition(1)).objective.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn multiple_instruction_blocks_do_not_imply_initializer_dominance() {
    let split = non_li_definition()
        .replace(
            "ra.block p=100 index=0 weight=1 flags=0 succ=",
            "ra.block p=100 index=0 weight=1 flags=0 succ=200,",
        )
        .replace(
            "ra.i p=ccc",
            "ra.block p=200 index=1 weight=1 flags=0 succ=\nra.i p=ccc",
        );
    assert_eq!(
        evaluate_trace(split.clone(), initialization(0, None))
            .objective
            .state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        evaluate_trace(split, definition(1)).objective.state,
        EvaluationState::Satisfied
    );
}
#[test]
fn initializer_allows_only_verified_linear_empty_cfg_scaffolding() {
    let text=trace().replace("ra.block p=100 index=0 weight=1 flags=0 succ=", "ra.block p=90 index=9 weight=1 flags=0 succ=100,\nra.block p=100 index=0 weight=1 flags=0 succ=200,")
        .replace("ra.end", "ra.block p=200 index=2 weight=1 flags=0 succ=\nra.end");
    assert_eq!(
        evaluate_trace(text.clone(), initialization(0, None))
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    for bad in [
        text.replace(
            "p=200 index=2 weight=1 flags=0 succ=",
            "p=200 index=2 weight=1 flags=0 succ=90,",
        ),
        text.replace(
            "p=90 index=9 weight=1 flags=0 succ=100,",
            "p=90 index=9 weight=1 flags=0 succ=100,200,",
        ),
        text.replace(
            "p=90 index=9 weight=1 flags=0 succ=100,",
            "p=90 index=9 weight=1 flags=0 succ=999,",
        ),
    ] {
        assert_eq!(
            evaluate_trace(bad, initialization(0, None)).objective.state,
            EvaluationState::Indeterminate
        );
    }
}

#[test]
fn copy_opcode_without_eligibility_flag_never_becomes_an_arbitrary_producer() {
    let text = scalarized(trace()).replace("flags=80000010", "flags=80000000");
    let rejected = text
        .lines()
        .filter(|line| {
            !line.starts_with("pv.coalesce.check") || line.contains("condition=copy_flag ")
        })
        .map(|line| {
            if line.starts_with("pv.coalesce.check") {
                line.replace("taken=0 eflags=40", "taken=1 eflags=0")
            } else {
                line.to_owned()
            }
        })
        .collect::<Vec<_>>()
        .join("\n");
    let report = evaluate_trace(rejected.clone(), definition(0));
    assert_eq!(
        report.objective.state,
        EvaluationState::Unsatisfied,
        "{:?}",
        report.objective
    );
    assert_eq!(
        report.observations["copy_decision"]["candidate"]["decision"]["condition"],
        "copy_flag"
    );
    let unavailable_producer = rejected
        .lines()
        .filter(|line| {
            !line.starts_with("ra.i p=aaa") && !line.starts_with("pv.graph.instruction p=aaa")
        })
        .map(|line| line.replace("instructions=3", "instructions=2"))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        evaluate_trace(unavailable_producer, definition(0))
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    let missing = rejected
        .lines()
        .filter(|line| {
            !line.starts_with("pv.coalesce.candidate") && !line.starts_with("pv.coalesce.check")
        })
        .map(|line| line.replace("candidates=1", "candidates=0"))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(
        evaluate_trace(missing, definition(0)).objective.state,
        EvaluationState::Indeterminate
    );
}
