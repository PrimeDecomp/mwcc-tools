use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    value_history::{self, ChangeKind},
};
const BEGIN: &str = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=sample__Fv\n";
fn birth(id: u32, register: u32) -> String {
    format!(
        "pv.value.birth schema=1 function=1 value={id} cls=4 register={register} count={} physical=32 site=400123 expression=0 phase=0 statement=0 line=-1 category=unknown\n",
        register + 1
    )
}
fn end(births: u32, count: u32) -> String {
    format!("pv.function.end function=1 births={births} c0=4 c1=8 c2=32 c3=32 c4={count}\n")
}
#[test]
fn rewinds_preserve_both_generations_and_highwater_restores_are_not_births() {
    let trace = format!(
        "{BEGIN}{}pv.counter.change function=1 role=3 cls=4 old=33 after=32 physical=32 site=400200 phase=0\n{}pv.counter.change function=1 role=2 cls=4 old=33 after=40 physical=32 site=400300 phase=0\n{}",
        birth(1, 32),
        birth(2, 32),
        end(2, 40)
    );
    let report = value_history::parse(&trace).unwrap();
    assert_eq!(
        report.counter_observations.state,
        EvaluationState::Satisfied
    );
    let f = &report.functions[0];
    assert_eq!(f.births.len(), 2);
    assert_eq!(f.births[0].generation, 0);
    assert_eq!(f.births[0].phase, value_history::Phase::CodeGeneration);
    assert_eq!(f.births[1].generation, 1);
    assert_eq!(f.registers[0].values.len(), 2);
    assert_eq!(f.changes[0].kind, ChangeKind::Rewind);
    assert_eq!(f.changes[1].kind, ChangeKind::CloseTemps);
    assert!(f.births[0].trace_position < f.changes[0].trace_position);
    assert!(f.changes[0].trace_position < f.births[1].trace_position);
    assert!(f.births[1].trace_position < f.changes[1].trace_position);
    assert!(f.births.iter().all(|b| b.category.is_none()));
    assert_eq!(
        provenance::parse(&trace).unwrap().lifecycle.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn missing_ends_counter_discontinuities_and_orphans_are_not_complete() {
    let capture = format!("{BEGIN}{}{}", birth(1, 32), end(1, 33));
    assert_eq!(
        value_history::parse(&capture)
            .unwrap()
            .counter_observations
            .state,
        EvaluationState::Satisfied
    );
    for damaged in [
        format!("{BEGIN}{}", birth(1, 32)),
        format!("{BEGIN}{}{}", birth(1, 33), end(1, 34)),
        format!("{capture}pv.counter.orphan site=4 role=1 cls=4 after=34\n"),
        capture.replace("schema=1", "schema=2"),
        capture.replace("births=1", "births=2"),
    ] {
        assert_eq!(
            value_history::parse(&damaged)
                .unwrap()
                .counter_observations
                .state,
            EvaluationState::Indeterminate
        );
    }
    assert_eq!(
        value_history::parse("").unwrap().counter_observations.state,
        EvaluationState::Indeterminate
    );
    let gap = format!(
        "{BEGIN}pv.gap reason=function_context_depth site=400000\n{}{}",
        birth(1, 32),
        end(1, 33)
    );
    let incomplete = value_history::parse(&gap).unwrap();
    assert_eq!(
        incomplete.functions[0].counter_observations.state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn partial_expression_origins_are_retained_as_bytes_with_explicit_scope() {
    let expression = "pv.expression.begin function=1 expression=1 node=123 kind=56 line=9 scope=statement_root complete=0 tree_hex=6e616d653d78\npv.expression.end function=1 expression=1\n";
    let trace = format!(
        "{BEGIN}{expression}{}{}",
        birth(1, 32).replace("expression=0", "expression=1"),
        end(1, 33)
    );
    let report = value_history::parse(&trace).unwrap();
    let f = &report.functions[0];
    assert_eq!(f.expressions[0].tree_text.as_deref(), Some("name=x"));
    assert!(!f.expressions[0].complete);
    assert_eq!(f.births[0].expression, Some(1));
    assert!(value_history::parse(&trace.replace("6e616d653d78", "a€b")).is_err());
    assert!(
        value_history::parse(&trace.replace(
            "pv.expression.end function=1 expression=1",
            "pv.expression.end function=1 expression=2"
        ))
        .is_err()
    );
    assert!(value_history::parse(&trace.replace("site=400123", "site=broken")).is_err());
}
#[test]
fn nested_function_ids_keep_register_namespaces_separate() {
    let child = "pv.function.begin schema=1 function=2 parent=1 c0=4 c1=8 c2=32 c3=32 c4=33 coverage=direct_counter_writes link=child__Fv\n";
    let trace = format!(
        "{BEGIN}{}{child}{}{}{}",
        birth(1, 32),
        birth(1, 33).replace("function=1", "function=2"),
        end(1, 34).replace("function=1", "function=2"),
        end(1, 34)
    );
    let report = value_history::parse(&trace).unwrap();
    assert_eq!(
        report.counter_observations.state,
        EvaluationState::Satisfied
    );
    assert_ne!(
        report.functions[0].births[0].id,
        report.functions[1].births[0].id
    );
    assert_eq!(report.functions[1].parent, 1);
}
