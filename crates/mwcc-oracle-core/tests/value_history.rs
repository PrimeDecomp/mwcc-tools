use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    value_history::{self, ChangeKind},
};
const BEGIN: &str = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=sample__Fv\n";
fn dispatch_trace(result: &str) -> String {
    format!(
        "{}pv.expression.begin function=1 expression=1 node=100 kind=54 line=9 scope=statement_root complete=0 parent=0 target=0 a=0 b=0 tree_hex=63616c6c\npv.expression.begin function=1 expression=2 node=200 kind=56 line=9 scope=codegen_dispatch complete=0 parent=1 target=440000 a=aabb0001 b=ffff0002 tree_hex=73796d626f6c\n{}pv.expression.result function=1 expression=2 {result}\npv.expression.end function=1 expression=2\npv.expression.end function=1 expression=1\n{}",
        BEGIN.replace(
            "coverage=direct_counter_writes",
            "coverage=direct_counter_writes dispatch=guarded_table_v1"
        ),
        birth(1, 32).replace("expression=0", "expression=2"),
        end(1, 33).replace("births=1", "births=1 dispatches=1 dispatch_returns=1")
    )
}
#[test]
fn dispatch_contexts_preserve_nesting_argument_bits_and_tagged_results() {
    let history =
        value_history::parse(&dispatch_trace("kind=8 object=abc displacement=0")).unwrap();
    let function = &history.functions[0];
    assert_eq!(
        function.dispatch_observations.state,
        EvaluationState::Satisfied
    );
    let expression = &function.expressions[1];
    assert_eq!(expression.parent, Some(1));
    assert_eq!(
        expression.dispatch.as_ref().unwrap().requested,
        [0xaabb0001, 0xffff0002]
    );
    assert_eq!(
        expression.dispatch.as_ref().unwrap().retail_target,
        0x440000
    );
    assert_eq!(
        expression.result.as_ref().unwrap().object.as_deref(),
        Some("abc")
    );
    assert_eq!(function.births[0].expression, Some(2));
    assert!(!expression.complete);
    assert!(expression.end_position.unwrap() > expression.result.as_ref().unwrap().trace_position);
    assert_eq!(
        provenance::parse(&dispatch_trace("kind=8 object=abc displacement=0"))
            .unwrap()
            .lifecycle
            .state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn dispatch_results_read_only_the_fields_defined_by_each_operand_tag() {
    for result in [
        "kind=0 cls=4 register=32",
        "kind=1 cls=4 register=32 displacement=-4",
        "kind=2 cls=4 register=32 index=33",
        "kind=3 cls=4 register=32 second=33",
        "kind=4 immediate=-7",
        "kind=5 cls=3 register=1",
        "kind=6 cls=2 register=2",
        "kind=7 cls=1 register=0",
        "kind=8 object=abc displacement=0",
        "kind=9 cls=4 register=32 displacement=4",
        "kind=10 cls=4 register=32 index=33",
        "kind=11 object=abc displacement=8",
    ] {
        let history = value_history::parse(&dispatch_trace(result)).unwrap();
        assert_eq!(
            history.functions[0].dispatch_observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            history.functions[0].expressions[1]
                .result
                .as_ref()
                .unwrap()
                .evidence
                .state,
            EvaluationState::Satisfied
        );
    }
    for result in [
        "kind=4 immediate=0 register=1",
        "kind=3 cls=4 register=32",
        "kind=8 object=not-hex displacement=0",
        "kind=5 cls=4 register=1",
    ] {
        assert!(value_history::parse(&dispatch_trace(result)).is_err());
    }
}
#[test]
fn dispatch_capture_completeness_is_separate_from_expression_and_output_semantics() {
    let trace = dispatch_trace("kind=99");
    let history = value_history::parse(&trace).unwrap();
    assert_eq!(
        history.functions[0].dispatch_observations.state,
        EvaluationState::Satisfied
    );
    assert_eq!(
        history.functions[0].expressions[1]
            .result
            .as_ref()
            .unwrap()
            .evidence
            .state,
        EvaluationState::Indeterminate
    );
    for changed in [
        trace.replace("dispatch_returns=1", "dispatch_returns=0"),
        trace.replace("pv.expression.result function=1 expression=2 kind=99\n", ""),
        trace.replace("dispatch=guarded_table_v1", "dispatch=unknown"),
        trace.replace(
            "pv.expression.end function=1 expression=2",
            "pv.expression.end function=1 expression=2 abandoned=1",
        ),
    ] {
        assert_eq!(
            value_history::parse(&changed).unwrap().functions[0]
                .dispatch_observations
                .state,
            EvaluationState::Indeterminate
        );
    }
    assert!(
        value_history::parse(&trace.replace("parent=1 target=440000", "parent=0 target=440000"))
            .is_err()
    );
    assert_eq!(
        value_history::parse(BEGIN).unwrap().functions[0]
            .dispatch_observations
            .state,
        EvaluationState::Indeterminate
    );
}
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
