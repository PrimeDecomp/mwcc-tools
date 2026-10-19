use mwcc_oracle_core::{
    allocation_category::Category,
    evidence::EvaluationState,
    provenance,
    value_history::{ObjectPart, RangeKind},
};
fn birth(sequence: u32, register: u32) -> String {
    format!(
        "pv.value.birth schema=1 function=1 value={sequence} cls=4 register={register} count={} physical=32 site=400000 expression=0 phase=0 line=-1 category=unknown\n",
        register + 1
    )
}
fn capture() -> String {
    let mut text="pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=sample\n".to_owned();
    for index in 0..8 {
        text += &birth(index + 1, index + 32);
        if index == 1 {
            text += "pv.category.initial function=1 births=2 i2=31 i3=31 i4=33\n";
        }
        if index == 3 {
            text += "pv.category.window function=1 births=4 f2=32 f3=32 f4=36 l2=32 l3=32 l4=36\n";
        }
    }
    text += "pv.graph.begin schema=1 function=1 graph=1 births=8 cls=4 physical=32 start=32 count=40 phase=3 boundary=before_simplify category_ready=1 initial_last=33 first=36 last=37\n";
    for register in 32..40 {
        text += &format!("pv.graph.node v={register} visible=0 deg=0 flags=0 color=-1 n=\n");
    }
    text += "pv.graph.end function=1 graph=1 nodes=8 instructions=0 complete=1\npv.function.end function=1 births=8 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=40\n";
    text
}
#[test]
fn observed_range_markers_classify_every_inclusive_boundary() {
    let report = provenance::parse(&capture()).unwrap();
    let f = &report.history.functions[0];
    assert_eq!(f.range_observations.state, EvaluationState::Satisfied);
    assert!(matches!(
        f.ranges[0].range,
        RangeKind::Initial { last: [31, 31, 33] }
    ));
    let graph = &report.graphs.graphs[0];
    let categories: Vec<_> = graph
        .nodes
        .values()
        .map(|n| n.range_category.category.unwrap())
        .collect();
    assert_eq!(
        categories,
        vec![
            Category::InitialObjectRange,
            Category::InitialObjectRange,
            Category::BeforeCoalescingWindow,
            Category::BeforeCoalescingWindow,
            Category::InsideCoalescingWindow,
            Category::InsideCoalescingWindow,
            Category::AfterCoalescingWindow,
            Category::AfterCoalescingWindow
        ]
    );
    let bounds = graph.range_bounds.as_ref().unwrap();
    assert_eq!(bounds.classify(31).category, Some(Category::Physical));
    assert!(
        graph
            .nodes
            .values()
            .all(|n| n.range_category.evidence.state == EvaluationState::Satisfied)
    );
    // Range category describes the allocator's numbers; it is not a source role.
    assert!(f.births.iter().all(|b| b.category.is_none()));
    assert_eq!(report.lifecycle.state, EvaluationState::Indeterminate);
}
#[test]
fn missing_stale_or_inconsistent_range_evidence_does_not_become_a_category() {
    let text = capture();
    for broken in [
        text.replace("ranges=2", "ranges=3"),
        text.replace("first=36", "first=35"),
        text.replace("initial_last=33", "initial_last=34"),
        text.replace("last=37", "last=35"),
        text.replace("category_ready=1", "category_ready=0"),
        text.replace("i4=33", "i4=34"),
        text.replace("f4=36", "f4=35"),
        text.replace(
            "pv.category.initial function=1 births=2 i2=31 i3=31 i4=33\n",
            "",
        ),
        text.replace(
            "pv.graph.end function=1 graph=1 nodes=8 instructions=0 complete=1\n",
            "",
        ),
    ] {
        let report = provenance::parse(&broken).unwrap();
        assert!(
            report.graphs.graphs[0]
                .nodes
                .values()
                .all(|n| n.range_category.category.is_none()
                    && n.range_category.evidence.state == EvaluationState::Indeterminate)
        );
    }
    assert!(provenance::parse(&text.replace("category_ready=1", "category_ready=2")).is_err());
    assert!(provenance::parse(&text.replace("births=2 i2", "births=1 i2")).is_err());
}
#[test]
fn object_bindings_keep_pair_parts_and_byte_names_without_claiming_unique_names() {
    let object = |value, part, name: &str| {
        format!(
            "pv.value.object function=1 value={value} object=123 info=456 part={part} datatype=0 storage=1 flags_before=0 type_kind=4 type_size=8 name_known=1 name_complete=1 name_hex={name}\n"
        )
    };
    let text = capture()
        .replace(&birth(1, 32), &(birth(1, 32) + &object(1, 2, "70616972")))
        .replace(&birth(2, 33), &(birth(2, 33) + &object(2, 3, "70616972")));
    let report = provenance::parse(&text).unwrap();
    let births = &report.history.functions[0].births;
    let first = births[0].object.as_ref().unwrap();
    let second = births[1].object.as_ref().unwrap();
    assert_eq!(first.part, ObjectPart::FirstWord);
    assert_eq!(second.part, ObjectPart::SecondWord);
    assert_eq!(first.name_text.as_deref(), Some("pair"));
    assert_eq!(first.object, second.object);
    assert_ne!(births[0].id, births[1].id);
    let non_utf8 = provenance::parse(&text.replace("70616972", "ff")).unwrap();
    assert!(
        non_utf8.history.functions[0].births[0]
            .object
            .as_ref()
            .unwrap()
            .name_text
            .is_none()
    );
    let absent = provenance::parse(&text.replace(
        "name_known=1 name_complete=1 name_hex=70616972",
        "name_known=0 name_complete=0 name_hex=",
    ))
    .unwrap();
    let unnamed = absent.history.functions[0].births[0]
        .object
        .as_ref()
        .unwrap();
    assert!(!unnamed.name_known && unnamed.name_text.is_none());
    for broken in [
        text.replace("part=2", "part=0"),
        text.replace("name_known=1", "name_known=0"),
        text.replace("70616972", "a€b"),
        text.replace(
            "pv.value.object function=1 value=1",
            "pv.value.object function=1 value=2",
        ),
    ] {
        assert!(provenance::parse(&broken).is_err());
    }
}
#[test]
fn graph_physical_boundaries_must_agree_with_the_preceding_retail_pass() {
    let marker = "pv.coalesce.begin schema=1 pass=1 cls=4 count=40 physical=32 first=36 last=37 special=1 function_id=1 creation=direct_counter_writes link=sample\npv.coalesce.end pass=1 candidates=0\n";
    let text = capture().replace("pv.graph.begin", &(marker.to_owned() + "pv.graph.begin"));
    let correct = provenance::parse(&text).unwrap();
    assert_eq!(
        correct.graphs.observations.state,
        EvaluationState::Satisfied
    );
    // Retained development captures cached zero before target initialization.
    let stale = text.replace("births=8 cls=4 physical=32", "births=8 cls=4 physical=0");
    let wrong = provenance::parse(&stale).unwrap();
    assert_eq!(
        wrong.graphs.observations.state,
        EvaluationState::Indeterminate
    );
    assert!(
        wrong.graphs.graphs[0]
            .nodes
            .values()
            .all(|n| n.range_category.category.is_none()
                && n.binding.evidence.state == EvaluationState::Indeterminate)
    );
}
