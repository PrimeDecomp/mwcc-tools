use mwcc_oracle_core::{
    call_arguments::{self, OperandStage, Stage},
    evidence::EvaluationState,
    provenance,
};

fn function(body: &str, counts: &str, births: u32) -> String {
    format!(
        "pv.function.begin schema=1 function=1 parent=0 function_object=aa c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes call_arguments={} link=caller__Fv\n{body}pv.function.end function=1 births={births} graphs=0 ranges=0 c0=4 c1=8 c2=32 c3=32 c4={} call_arguments={} {counts}\n",
        call_arguments::SCOPE,
        32 + births,
        call_arguments::SCOPE
    )
}
fn call_begin(id: u32, parent: u32) -> String {
    format!(
        "pv.call.begin schema=1 function=1 function_object=aa call={id} parent_call={parent} parent_expression=0 item=1000 kind=54 callee_node=300 callee_kind=56 callee_object=400 callee_known=1 callee_complete=1 callee_hex=7461726765745f5f465549 return_type=500 type_kind=1 type_size=4 output=600 scope={} ordinal_kind={}\n",
        call_arguments::SCOPE,
        call_arguments::ORDINAL_KIND
    )
}
fn binding(call: u32) -> String {
    format!(
        "pv.argument.bind schema=1 function=1 call={call} ordinal=0 list=700 context=100 node=200 kind=56 type=500 type_kind=1 type_size=4 hascall=0 flags=1 gpr=3 second_gpr=-1 fpr=-1 vector=-1 stack_offset=0 evaluated=0\n"
    )
}
fn begin(call: u32, episode: u32, parent: u32, stage: &str, births: u32) -> String {
    format!(
        "pv.argument.begin schema=1 function=1 call={call} ordinal=0 episode={episode} parent_episode={parent} stage={stage} context=100 node=200 operand=108 births_before={births} block=800 tail=900\n"
    )
}
fn operand(call: u32, episode: u32, stage: &str) -> String {
    format!(
        "pv.argument.operand schema=1 function=1 call={call} ordinal=0 episode={episode} stage={stage} operand=108 kind=4 immediate=17\n"
    )
}
fn end(call: u32, episode: u32, births: u32) -> String {
    format!(
        "pv.argument.end schema=1 function=1 call={call} ordinal=0 episode={episode} births_after={births} block=800 tail=900 instructions=0 pcode_complete=1\n"
    )
}
fn call_end(id: u32, episodes: u32, births: u32, operands: u32) -> String {
    format!(
        "pv.call.end schema=1 function=1 call={id} arguments=1 episodes={episodes} births={births} operands={operands} instructions=0 complete=1\n"
    )
}
fn dispatch_begin(node: &str) -> String {
    format!(
        "pv.expression.begin function=1 expression=1 node={node} kind=56 line=3 scope=codegen_dispatch complete=0 parent=0 target=400000 a=0 b=0 tree_hex=\n"
    )
}
fn simple() -> String {
    function(
        &(call_begin(1, 0)
            + &binding(1)
            + &begin(1, 1, 0, "register", 0)
            + &operand(1, 1, "helper_return")
            + &end(1, 1, 0)
            + &call_end(1, 1, 0, 1)),
        "call_count=1 argument_count=1 argument_episodes=1 argument_births=0 argument_operands=1 argument_instructions=0",
        0,
    )
}
fn state(text: &str) -> EvaluationState {
    provenance::parse(text)
        .unwrap()
        .call_arguments
        .observations
        .state
}

#[test]
fn typed_regular_call_observations_do_not_initialize_unevaluated_storage() {
    let report = provenance::parse(&simple()).unwrap();
    let h = &report.call_arguments;
    assert_eq!(h.observations.state, EvaluationState::Satisfied);
    assert_eq!(h.functions[0].expected_calls, Some(1));
    let c = &h.calls[0];
    assert_eq!(c.callee.name_text.as_deref(), Some("target__FUI"));
    assert_eq!(c.arguments[0].second_gpr, -1);
    assert!(!c.arguments[0].evaluated);
    assert_eq!(c.episodes[0].stage, Stage::Register);
    assert_eq!(c.episodes[0].operands[0].stage, OperandStage::HelperReturn);
    let o = &c.episodes[0].operands[0].observation;
    assert_eq!(o.immediate, Some(17));
    assert_eq!(o.register, None);
    assert!(
        report
            .decisions
            .reasons
            .iter()
            .all(|r| !r.detail.contains("unknown"))
    );
    let mut json = serde_json::to_value(&report).unwrap();
    json.as_object_mut().unwrap().remove("call_arguments");
    let old: provenance::Report = serde_json::from_value(json).unwrap();
    assert_eq!(
        old.call_arguments.observations.state,
        EvaluationState::Indeterminate
    );
}

#[test]
fn independent_totals_scope_and_missing_streams_fail_closed() {
    let text = simple();
    for altered in [
        text.replace("call_count=1","call_count=2"),
        text.replace("argument_operands=1","argument_operands=2"),
        text.replace("argument_episodes=1","argument_episodes=2"),
        text.replace("call_arguments=guarded_regular_call_arguments_v1", "call_arguments=unknown"),
        text.replace("scope=guarded_regular_call_arguments_v1", "scope=unknown"),
        text.replace("ordinal_kind=lowered_argument_context_order", "ordinal_kind=source_argument_order"),
        text.replace("pv.call.begin schema=1", "pv.call.begin schema=2"),
        text.replace("stage=register", "stage=unknown"),
        text.lines().filter(|l|!l.starts_with("pv.argument.operand ")).collect::<Vec<_>>().join("\n"),
        text.lines().filter(|l|!l.starts_with("pv.argument.begin ")).collect::<Vec<_>>().join("\n"),
        text.lines().filter(|l|!l.starts_with("pv.argument.")).collect::<Vec<_>>().join("\n"),
        text.lines().filter(|l|!l.starts_with("pv.call.")&&!l.starts_with("pv.argument.")).collect::<Vec<_>>().join("\n"),
        text.replace(" argument_operands=1", ""),
        text.replace(" operands=1", ""),
        text.replace(" complete=1", " complete=0"),
        text.replace(" pcode_complete=1", " pcode_complete=0"),
        text.replace("kind=4 immediate=17", "kind=12"),
        text.replace("pv.argument.bind", "pv.argument.gap schema=1 function=1 call=1 ordinal=0 episode=0 reason=test\npv.argument.bind"),
        text.replace("flags=1 gpr", "flags=5 gpr"),
        text.replace("item=1000", "item=0"),
        text.lines().filter(|l|!l.starts_with("pv.argument.end ")).collect::<Vec<_>>().join("\n"),
    ] {assert_eq!(state(&altered),EvaluationState::Indeterminate,"{altered}");}
    assert_eq!(state(""), EvaluationState::Indeterminate);
}

#[test]
fn nested_calls_and_topmost_birth_ownership_are_explicit() {
    let birth = "pv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\n\
pv.argument.birth schema=1 function=1 call=2 ordinal=0 episode=3 value=1 cls=4 register=32\n";
    let body = call_begin(1, 0)
        + &binding(1)
        + &begin(1, 1, 0, "register", 0)
        + &begin(1, 2, 1, "dispatch", 0)
        + &call_begin(2, 1)
        + &binding(2)
        + &begin(2, 3, 2, "register", 0)
        + birth
        + &operand(2, 3, "helper_return")
        + &end(2, 3, 1)
        + &call_end(2, 1, 1, 1)
        + &operand(1, 2, "dispatch_return")
        + &end(1, 2, 1)
        + &operand(1, 1, "helper_return")
        + &end(1, 1, 1)
        + &call_end(1, 2, 0, 2);
    let body = body
        .replace(
            &begin(1, 2, 1, "dispatch", 0),
            &(dispatch_begin("200") + &begin(1, 2, 1, "dispatch", 0)),
        )
        .replace(
            &operand(1, 2, "dispatch_return"),
            &("pv.expression.result function=1 expression=1 operand=108 kind=4 immediate=17\n"
                .to_owned()
                + &operand(1, 2, "dispatch_return")),
        )
        .replace(
            &end(1, 2, 1),
            &(end(1, 2, 1) + "pv.expression.end function=1 expression=1\n"),
        );
    let text = function(
        &body,
        "call_count=2 argument_count=2 argument_episodes=3 argument_births=1 argument_operands=3 argument_instructions=0",
        1,
    );
    let report = provenance::parse(&text).unwrap();
    let h = &report.call_arguments;
    assert_eq!(h.observations.state, EvaluationState::Satisfied);
    assert_eq!(h.calls[1].parent_call, Some(1));
    assert_eq!(h.calls[1].episodes[0].parent_episode, Some(2));
    assert_eq!(h.calls[0].episodes[1].dispatch_expression, Some(1));
    assert!(h.calls[0].episodes.iter().all(|e| e.births.is_empty()));
    assert_eq!(h.calls[1].episodes[0].births[0].value.sequence, 1);
    let omitted = text
        .lines()
        .filter(|l| !l.starts_with("pv.argument.birth "))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(state(&omitted), EvaluationState::Indeterminate);
    let missing_dispatch = text
        .lines()
        .filter(|l| !l.starts_with("pv.expression."))
        .collect::<Vec<_>>()
        .join("\n");
    assert_eq!(state(&missing_dispatch), EvaluationState::Indeterminate);
    assert_eq!(
        state(&text.replace("expression=1 operand=108", "expression=1 operand=109")),
        EvaluationState::Indeterminate
    );
    let registers = text.replace(
        "operand=108 kind=4 immediate=17",
        "operand=108 kind=0 cls=4 register=32",
    );
    assert_eq!(state(&registers), EvaluationState::Satisfied);
    let changed_result = registers.replace(
        "pv.expression.result function=1 expression=1 operand=108 kind=0 cls=4 register=32",
        "pv.expression.result function=1 expression=1 operand=108 kind=0 cls=4 register=33",
    );
    assert_eq!(state(&changed_result), EvaluationState::Indeterminate);
    for altered in [
        text.replace("parent_call=1", "parent_call=0"),
        text.replace("parent_episode=2", "parent_episode=1"),
        text.replace("value=1 cls=4 register=32\n", "value=1 cls=4 register=33\n"),
    ] {
        assert!(provenance::parse(&altered).is_err(), "{altered}");
    }
}

#[test]
fn nested_call_result_birth_can_belong_to_enclosing_argument() {
    let birth = "pv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\n\
pv.argument.birth schema=1 function=1 call=1 ordinal=0 episode=1 value=1 cls=4 register=32\n";
    let body = call_begin(1, 0)
        + &binding(1)
        + &begin(1, 1, 0, "register", 0)
        + &call_begin(2, 1)
        + &binding(2)
        + &begin(2, 2, 1, "register", 0)
        + &operand(2, 2, "helper_return")
        + &end(2, 2, 0)
        + birth
        + &call_end(2, 1, 0, 1)
        + &operand(1, 1, "helper_return")
        + &end(1, 1, 1)
        + &call_end(1, 1, 1, 1);
    let text = function(
        &body,
        "call_count=2 argument_count=2 argument_episodes=2 argument_births=1 argument_operands=2 argument_instructions=0",
        1,
    );
    let report = provenance::parse(&text).unwrap();
    assert_eq!(
        report.call_arguments.observations.state,
        EvaluationState::Satisfied
    );
    assert_eq!(report.call_arguments.calls[0].episodes[0].births.len(), 1);
    assert!(report.call_arguments.calls[1].episodes[0].births.is_empty());
    // Delaying a child-owned birth until the outer episode resumes must not
    // turn it into an outer-owned event merely because its value id matches.
    let stolen=text.replace(&end(2,2,0),"").replace(birth,&("pv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\n".to_owned()+&end(2,2,1)+"pv.argument.birth schema=1 function=1 call=1 ordinal=0 episode=1 value=1 cls=4 register=32\n"));
    assert!(provenance::parse(&stolen).is_err());
}

#[test]
fn pointer_order_tag_and_identity_contradictions_are_errors() {
    let text = simple();
    for altered in [
        text.replace(
            "context=100 node=200 operand",
            "context=101 node=200 operand",
        ),
        text.replace("node=200 operand", "node=201 operand"),
        text.replace(
            "stage=helper_return operand=108",
            "stage=helper_return operand=109",
        ),
        text.replace("context=100", "context=zz"),
        text.replace(
            "kind=4 immediate=17",
            "kind=4 immediate=17 cls=4 register=0",
        ),
        text.replace("evaluated=0", "evaluated=2"),
        text.replace("callee_object=400", "callee_object=0"),
        text.replace("stage=helper_return", "stage=force_after"),
        text.replace("ordinal=0 list", "ordinal=1 list"),
        text.replace("episode=1 parent_episode=0", "episode=0 parent_episode=0"),
    ] {
        assert!(provenance::parse(&altered).is_err(), "{altered}");
    }
}

#[test]
fn force_stages_and_complete_pcode_intervals_are_typed() {
    let body = call_begin(1, 0)
        + &binding(1).replace("flags=1 gpr", "flags=4 gpr")
        + &begin(1, 1, 0, "stack", 0)
        + &begin(1, 2, 1, "force_gpr", 0)
        + &operand(1, 2, "force_before")
        + &operand(1, 2, "force_after")
        + &end(1, 2, 0)
        + &operand(1, 1, "helper_return")
        + "pv.argument.instruction schema=1 function=1 call=1 ordinal=0 episode=1 index=0 p=901 op=139 flags=88000010 n=1 line=5 ops=000402002000000000000000\n"
        + &end(1, 1, 0).replace("tail=900 instructions=0", "tail=901 instructions=1")
        + &call_end(1, 2, 0, 3).replace("instructions=0", "instructions=1");
    let text = function(
        &body,
        "call_count=1 argument_count=1 argument_episodes=2 argument_births=0 argument_operands=3 argument_instructions=1",
        0,
    );
    let report = provenance::parse(&text).unwrap();
    assert_eq!(
        report.call_arguments.observations.state,
        EvaluationState::Satisfied
    );
    assert_eq!(
        report.call_arguments.calls[0].episodes[0].instructions[0]
            .instruction
            .pointer,
        "00000901"
    );
    assert_eq!(
        state(&text.replace("tail=901 instructions=1", "tail=902 instructions=1")),
        EvaluationState::Indeterminate
    );
    assert_eq!(
        state(&text.replace(
            "births_after=0 block=800 tail=901",
            "births_after=0 block=801 tail=901"
        )),
        EvaluationState::Indeterminate
    );
    assert!(provenance::parse(&text.replace("index=0 p=901", "index=1 p=901")).is_err());
}

#[test]
fn complete_zero_call_scope_is_supported_without_claiming_other_call_families() {
    let text = function(
        "",
        "call_count=0 argument_count=0 argument_episodes=0 argument_births=0 argument_operands=0 argument_instructions=0",
        0,
    );
    let report = provenance::parse(&text).unwrap();
    assert_eq!(
        report.call_arguments.observations.state,
        EvaluationState::Satisfied
    );
    assert!(report.call_arguments.calls.is_empty());
    assert_eq!(
        state(&text.replace("call_count=0", "call_count=1")),
        EvaluationState::Indeterminate
    );
}

#[test]
fn nested_backend_function_suspends_call_and_episode_contexts() {
    let inner=function(&(call_begin(2,0)+&binding(2)+&begin(2,2,0,"register",0)+&operand(2,2,"helper_return")+&end(2,2,0)+&call_end(2,1,0,1)),"call_count=1 argument_count=1 argument_episodes=1 argument_births=0 argument_operands=1 argument_instructions=0",0)
        .replace("function=1", "function=2")
        .replace("function=2 parent=0", "function=2 parent=1")
        .replace("function_object=aa", "function_object=bb");
    let text = simple().replace(
        &operand(1, 1, "helper_return"),
        &(inner + &operand(1, 1, "helper_return")),
    );
    let report = provenance::parse(&text).unwrap();
    assert_eq!(
        report.call_arguments.observations.state,
        EvaluationState::Satisfied
    );
    assert_eq!(report.call_arguments.calls[1].function, 2);
    assert_eq!(report.call_arguments.calls[1].parent_call, None);
    assert_eq!(
        report.call_arguments.calls[1].episodes[0].parent_episode,
        None
    );
    assert!(
        report.call_arguments.calls[0].episodes[0]
            .end_position
            .is_some()
    );
}

#[test]
fn independent_event_totals_do_not_hide_local_call_coverage() {
    let first = call_begin(1, 0)
        + &binding(1)
        + &begin(1, 1, 0, "register", 0)
        + &operand(1, 1, "helper_return")
        + &end(1, 1, 0)
        + &call_end(1, 1, 0, 1);
    let gap = "pv.argument.gap schema=1 function=1 call=2 ordinal=0 episode=2 reason=pcode_interval_incomplete\n";
    let second = call_begin(2, 0)
        + &binding(2)
        + &begin(2, 2, 0, "register", 0)
        + &operand(2, 2, "helper_return")
        + gap
        + &end(2, 2, 0).replace("pcode_complete=1", "pcode_complete=0")
        + &call_end(2, 1, 0, 1).replace("complete=1", "complete=0");
    let mut text = function(
        &(first + &second),
        "call_count=2 argument_count=2 argument_episodes=2 argument_births=0 argument_operands=2 argument_instructions=0",
        0,
    );
    let position = text.rfind(call_arguments::SCOPE).unwrap();
    text.replace_range(position..position + call_arguments::SCOPE.len(), "unknown");
    let report = provenance::parse(&text).unwrap();
    let h = &report.call_arguments;
    assert_eq!(h.observations.state, EvaluationState::Indeterminate);
    assert_eq!(
        h.functions[0].observations.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        h.functions[0].event_totals.state,
        EvaluationState::Satisfied
    );
    assert_eq!(h.calls[0].observations.state, EvaluationState::Satisfied);
    assert_eq!(
        h.calls[1].observations.state,
        EvaluationState::Indeterminate
    );
    let future = text.replace(
        gap,
        &(gap.to_owned()
            + "pv.argument.future schema=1 function=1 call=2 ordinal=0 episode=2 reason=unknown\n"),
    );
    let changed = provenance::parse(&future).unwrap();
    assert_eq!(
        changed.call_arguments.functions[0].event_totals.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        changed.call_arguments.calls[0].observations.state,
        EvaluationState::Indeterminate
    );
    let unsupported = text.replace("pv.function.begin schema=1", "pv.function.begin schema=2");
    assert_eq!(
        provenance::parse(&unsupported)
            .unwrap()
            .call_arguments
            .functions[0]
            .event_totals
            .state,
        EvaluationState::Indeterminate
    );
    let missing = text.replace(" argument_births=0", "");
    assert_eq!(
        provenance::parse(&missing)
            .unwrap()
            .call_arguments
            .functions[0]
            .event_totals
            .state,
        EvaluationState::Indeterminate
    );
}
