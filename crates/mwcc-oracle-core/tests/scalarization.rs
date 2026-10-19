use mwcc_oracle_core::{evidence::EvaluationState, provenance, scalarization};

fn frontend() -> String {
    "pv.scalar.begin schema=1 pass=1 function_object=abc coverage=guarded_temp_returns_v1 link=case__Fv\n\
pv.scalar.binding schema=1 pass=1 binding=1 function_object=abc site=461748 part=100 source_linear=101 source_index=7 source_kind=2 variable=102 source_object=110 source_info=111 source_type=112 source_type_kind=12 source_type_size=8 source_datatype=1 source_storage=0 source_name_known=1 source_name_complete=1 source_name_hex=666c6167 source_element_known=1 source_element_type_kind=1 source_element_type_size=4 byte_offset=4 byte_size=4 bit_first=32 bit_last=63 flags=0 temporary=200 temporary_info=201 temporary_type=202 temporary_type_kind=1 temporary_type_size=4\n\
pv.scalar.end schema=1 pass=1 function_object=abc bindings=1 coverage=guarded_temp_returns_v1\n".replace("function_object=abc", "function=1 function_object=abc")
}
fn backend() -> String {
    "pv.function.begin schema=1 function=1 parent=0 function_object=abc scalarization=guarded_temp_returns_v1 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=case__Fv\n\
pv.value.birth schema=1 function=1 value=1 cls=4 register=32 count=33 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\n\
pv.value.object function=1 value=1 object=00000200 info=00000201 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=403137\n\
pv.function.end function=1 births=1 graphs=0 ranges=0 c0=4 c1=8 c2=32 c3=32 c4=33 scalarization=guarded_temp_returns_v1 scalar_passes=1 scalar_bindings=1 scalar_first_pass=1 scalar_last_pass=1\n".into()
}
fn insert_frontend(backend: &str, frontend: &str) -> String {
    backend.replacen(
        "pv.value.birth",
        &(frontend.to_owned() + "pv.value.birth"),
        1,
    )
}
fn trace() -> String {
    insert_frontend(&backend(), &frontend())
}
fn state(text: &str) -> EvaluationState {
    let report = provenance::parse(text).unwrap();
    scalarization::object_origin(
        &report.scalarization,
        &report.history.functions,
        &report.history.functions[0].births[0],
    )
    .evidence
    .state
}

#[test]
fn exact_frontend_objects_bind_backend_births_and_whole_array_elements() {
    for site in ["4608a8", "460b28", "461748"] {
        let report =
            provenance::parse(&trace().replace("site=461748", &format!("site={site}"))).unwrap();
        assert_eq!(
            report.scalarization.observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            report.history.counter_observations.state,
            EvaluationState::Satisfied
        );
        let result = scalarization::object_origin(
            &report.scalarization,
            &report.history.functions,
            &report.history.functions[0].births[0],
        );
        assert_eq!(result.evidence.state, EvaluationState::Satisfied);
        assert_eq!(result.pass, Some(1));
        let binding = result.binding.unwrap();
        let element = binding.array_element().unwrap();
        assert_eq!(element.name, b"flag");
        assert_eq!(element.element_index, 1);
        assert_eq!(element.element_size, 4);
        assert_eq!(element.element_count, 2);
        assert_eq!(binding.source.object, "00000110");
        assert_eq!(binding.temporary.object, "00000200");
        assert!(
            report.scalarization.passes[0].begin_position
                > report.history.functions[0].begin_position
        );
        assert!(
            report.scalarization.passes[0].end_position.unwrap()
                < report.history.functions[0].births[0].trace_position
        );
        assert_eq!(report.lifecycle.state, EvaluationState::Indeterminate);
    }
}

#[test]
fn missing_partial_or_conflicting_identity_cannot_join_by_name_or_order() {
    let text = trace();
    for altered in [
        backend(),
        text.replace("pv.scalar.end schema=1 pass=1 function=1 function_object=abc bindings=1 coverage=guarded_temp_returns_v1\n",""),
        text.replace("bindings=1 coverage=", "bindings=2 coverage="),
        text.replace("scalar_passes=1", "scalar_passes=2"),
        text.replace("scalar_bindings=1", "scalar_bindings=2"),
        text.replace("scalar_first_pass=1", "scalar_first_pass=2"),
        text.replace("scalarization=guarded_temp_returns_v1", "scalarization=unknown"),
        text.replace("pv.scalar.begin schema=1", "pv.scalar.begin schema=2"),
        text.replace("source_object=110", "source_object=210").replace("temporary=200", "temporary=300"),
        text.replace("temporary_info=201", "temporary_info=301"),
        text.replace("function=1 parent=0 function_object=abc", "function=1 parent=0 function_object=def"),
        text.replace("function=1 parent=0 function_object=abc", "function=1 parent=0"),
        text.replace("c4=32 coverage=direct_counter_writes link=case__Fv", "c4=32 coverage=direct_counter_writes link=other__Fv"),
        backend()+&frontend(),
        frontend()+&backend(),
    ] { assert_eq!(state(&altered),EvaluationState::Indeterminate,"{altered}"); }
    let second = frontend().replace("pass=1", "pass=2");
    let two_passes = backend().replace(
        "scalar_passes=1 scalar_bindings=1 scalar_first_pass=1 scalar_last_pass=1",
        "scalar_passes=2 scalar_bindings=2 scalar_first_pass=1 scalar_last_pass=2",
    );
    assert_eq!(
        state(&insert_frontend(&two_passes, &(frontend() + &second))),
        EvaluationState::Indeterminate
    );
    let extra = "pv.value.birth schema=1 function=1 value=2 cls=4 register=33 count=34 physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value=2 object=200 info=201 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex=403137\n";
    let reused = text
        .replace("pv.function.end", &(extra.to_owned() + "pv.function.end"))
        .replace("births=1 graphs=0", "births=2 graphs=0")
        .replace("c4=33 scalarization=", "c4=34 scalarization=");
    assert_eq!(state(&reused), EvaluationState::Indeterminate);
}

#[test]
fn array_element_roles_require_complete_target_type_and_exact_non_bitfield_extent() {
    for altered in [
        frontend().replace(
            "source_element_known=1 source_element_type_kind=1 source_element_type_size=4",
            "source_element_known=0 source_element_type_kind=-1 source_element_type_size=-1",
        ),
        frontend().replace(
            "source_name_known=1 source_name_complete=1 source_name_hex=666c6167",
            "source_name_known=0 source_name_complete=0 source_name_hex=",
        ),
        frontend()
            .replace("flags=0", "flags=2")
            .replace("bit_first=32 bit_last=63", "bit_first=33 bit_last=34"),
        frontend()
            .replace("byte_size=4", "byte_size=2")
            .replace("temporary_type_size=4", "temporary_type_size=2")
            .replace("bit_last=63", "bit_last=47"),
    ] {
        let history = scalarization::parse(&altered).unwrap();
        assert_eq!(history.observations.state, EvaluationState::Satisfied);
        assert!(history.passes[0].bindings[0].array_element().is_err());
    }
    for altered in [
        frontend().replace("site=461748", "site=461749"),
        frontend().replace("flags=0", "flags=1"),
        frontend().replace("byte_offset=4", "byte_offset=8"),
        frontend().replace("bit_last=63", "bit_last=62"),
        frontend().replace("temporary_type_size=4", "temporary_type_size=8"),
        frontend().replace("binding=1", "binding=2"),
        frontend().replace("temporary=200", "temporary=110"),
        frontend().replace("source_datatype=1", "source_datatype=2"),
        frontend().replace("temporary_type_kind=1", "temporary_type_kind=4"),
    ] {
        assert!(scalarization::parse(&altered).is_err(), "{altered}");
    }
}

#[test]
fn reused_function_objects_do_not_inherit_an_earlier_frontend_creation() {
    let second_backend = backend().replace("function=1", "function=2").replace(
        "scalar_first_pass=1 scalar_last_pass=1",
        "scalar_first_pass=2 scalar_last_pass=2",
    );
    let old_only = trace() + &second_backend;
    let report = provenance::parse(&old_only).unwrap();
    let origin = scalarization::object_origin(
        &report.scalarization,
        &report.history.functions,
        &report.history.functions[1].births[0],
    );
    assert_eq!(origin.evidence.state, EvaluationState::Indeterminate);
    let fresh = trace()
        + &insert_frontend(
            &second_backend,
            &frontend()
                .replace("pass=1", "pass=2")
                .replace("function=1", "function=2"),
        );
    let report = provenance::parse(&fresh).unwrap();
    let origin = scalarization::object_origin(
        &report.scalarization,
        &report.history.functions,
        &report.history.functions[1].births[0],
    );
    assert_eq!(origin.evidence.state, EvaluationState::Satisfied);
    assert_eq!(origin.pass, Some(2));
}

#[test]
fn late_frontend_pass_and_invalid_public_array_sizes_are_not_complete_origins() {
    let late = "pv.scalar.begin schema=1 pass=2 function=1 function_object=abc coverage=guarded_temp_returns_v1 link=case__Fv\npv.scalar.end schema=1 pass=2 function=1 function_object=abc bindings=0 coverage=guarded_temp_returns_v1\n";
    let text = trace()
        .replace("pv.function.end", &(late.to_owned() + "pv.function.end"))
        .replace("scalar_passes=1", "scalar_passes=2")
        .replace("scalar_last_pass=1", "scalar_last_pass=2");
    assert_eq!(state(&text), EvaluationState::Indeterminate);
    let mut binding = scalarization::parse(&frontend())
        .unwrap()
        .passes
        .remove(0)
        .bindings
        .remove(0);
    binding.source.element_type_size = 0;
    assert!(binding.array_element().is_err());
    binding.source.element_type_size = 4;
    binding.bit_first = 31;
    assert!(binding.array_element().is_err());
}
