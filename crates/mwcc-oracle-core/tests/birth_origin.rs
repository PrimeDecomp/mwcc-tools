use mwcc_oracle_core::{birth_origin::ResultPart, evidence::EvaluationState, value_history};

fn trace(version: usize) -> String {
    let arrays = [
        [0x571072, 0x571085],
        [0x571d32, 0x571d45],
        [0x573ff2, 0x574005],
    ][version];
    let call = [0x5267c0, 0x527320, 0x5281b0][version];
    let mut text = "pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes array_promotion=guarded_scalar_v1 call_result=guarded_call_result_v1 link=example__Fv\n".to_owned();
    let rows = [
        (arrays[0], 3, 0, 0),
        (arrays[1], 4, 0, 0),
        (call + 0x4c7, 3, 1, 1),
        (call + 0x514, 2, 1, 2),
        (call + 0x55d, 3, 1, 3),
        (call + 0x602, 4, 2, 4),
        (call + 0x612, 4, 3, 4),
        (call + 0x66d, 4, 1, 5),
    ];
    let mut counters = [4, 8, 32, 32, 32];
    for (i, (site, class, part, item)) in rows.into_iter().enumerate() {
        let register = counters[class];
        counters[class] += 1;
        let value = i + 1;
        text.push_str(&format!("pv.value.birth schema=1 function=1 value={value} cls={class} register={register} count={} physical=32 site={site:x} expression=0 phase={} statement=0 line=-1 category=unknown\n",counters[class],if part==0 {1}else{0}));
        if part == 0 {
            text.push_str(&format!("pv.value.array schema=1 function=1 value={value} promotion=100 object=200 register={register} element_index=1 element_size=4 array_size=8 element_count=2 flags={} datatype=0 storage=2 type_kind=12 type_size=8 name_known=1 name_complete=1 name_hex=6172726179\n",if class==3 {2}else{0}));
        } else {
            let tag = match (class, part) {
                (3, 1) => 5,
                (2, 1) => 6,
                (4, 2 | 3) => 3,
                _ => 0,
            };
            let output = format!("{tag:02x}{}", "00".repeat(23));
            text.push_str(&format!("pv.value.call_result schema=1 function=1 value={value} part={part} register={register} item={:x} kind=54 callee=400 return_type=500 type_kind=8 type_size=4 output=600 output_kind={tag} output_complete=0 output_hex={output} tree_known=1 tree_complete=0 tree_hex=63616c6c2829\n",0x300+item));
        }
    }
    text.push_str(&format!("pv.function.end function=1 births=8 graphs=0 ranges=0 array_promotions=2 call_results=6 c0=4 c1=8 c2={} c3={} c4={}\n",counters[2],counters[3],counters[4]));
    text
}
#[test]
fn guarded_origins_keep_array_elements_and_partial_call_results_on_all_versions() {
    for version in 0..3 {
        let history = value_history::parse(&trace(version)).unwrap();
        let function = &history.functions[0];
        assert_eq!(
            function.counter_observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            function.array_promotion_observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            function.call_result_observations.state,
            EvaluationState::Satisfied
        );
        let element = function.births[1].array_promotion.as_ref().unwrap();
        assert_eq!(
            (
                element.element_index,
                element.element_size,
                element.element_count
            ),
            (1, 4, 2)
        );
        assert_eq!(element.name_text.as_deref(), Some("array"));
        let first = function.births[5].call_result.as_ref().unwrap();
        let second = function.births[6].call_result.as_ref().unwrap();
        assert_eq!(first.part, ResultPart::FirstWord);
        assert_eq!(second.part, ResultPart::SecondWord);
        assert_eq!(first.item, second.item);
        assert!(!first.output_complete && !first.tree_complete);
        assert_eq!(first.output_bytes.len(), 24);
        assert_eq!(first.tree_text.as_deref(), Some("call()"));
    }
}
#[test]
fn missing_origin_records_cannot_be_hidden_by_adjusting_summary_counts() {
    let complete = trace(2);
    for (record, summary, adjusted) in [
        ("pv.value.array", "array_promotions=2", "array_promotions=1"),
        ("pv.value.call_result", "call_results=6", "call_results=5"),
    ] {
        let mut removed = false;
        let dropped = complete
            .lines()
            .filter(|line| {
                if !removed && line.starts_with(record) {
                    removed = true;
                    false
                } else {
                    true
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
            .replace(summary, adjusted);
        let history = value_history::parse(&dropped).unwrap();
        let function = &history.functions[0];
        assert_eq!(
            function.counter_observations.state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            if record == "pv.value.array" {
                &function.array_promotion_observations
            } else {
                &function.call_result_observations
            }
            .state,
            EvaluationState::Indeterminate
        );
    }
    for change in [
        complete.replace("guarded_scalar_v1", "unknown"),
        complete.replace("array_promotions=2", "array_promotions=3"),
        complete.replace("pv.value.array schema=1", "pv.value.array schema=9"),
    ] {
        assert_eq!(
            value_history::parse(&change).unwrap().functions[0]
                .array_promotion_observations
                .state,
            EvaluationState::Indeterminate
        );
    }
    for change in [
        complete.replace("guarded_call_result_v1", "unknown"),
        complete.replace("call_results=6", "call_results=7"),
        complete.replace(
            "pv.value.call_result schema=1",
            "pv.value.call_result schema=9",
        ),
    ] {
        assert_eq!(
            value_history::parse(&change).unwrap().functions[0]
                .call_result_observations
                .state,
            EvaluationState::Indeterminate
        );
    }
    let mismatched_pair = complete.replacen("item=304", "item=399", 1);
    assert_eq!(
        value_history::parse(&mismatched_pair).unwrap().functions[0]
            .call_result_observations
            .state,
        EvaluationState::Indeterminate
    );
}
#[test]
fn origin_bounds_identity_sites_and_partial_output_claims_are_validated() {
    let complete = trace(2);
    for change in [
        complete.replace("element_index=1", "element_index=2"),
        complete.replace("element_count=2", "element_count=9"),
        complete.replace("element_size=4", "element_size=2147483647"),
        complete.replace("array_size=8", "array_size=9"),
        complete.replace("flags=2 datatype", "flags=0 datatype"),
        complete.replace("type_kind=12", "type_kind=8"),
        complete.replace("promotion=100", "promotion=0"),
        complete.replace("name_known=1", "name_known=0"),
        complete.replace("site=574005", "site=574004"),
        complete.replace("register=32 element_index", "register=33 element_index"),
        complete.replace("output_complete=0", "output_complete=1"),
        complete.replace("tree_complete=0", "tree_complete=1"),
        complete.replace("kind=54 callee", "kind=56 callee"),
        complete.replace("output_kind=0", "output_kind=1"),
        complete
            .replace("output_kind=5", "output_kind=0")
            .replace("output_hex=05", "output_hex=00"),
        complete.replace("part=3 register", "part=2 register"),
        complete.replace("item=304", "item=0"),
    ] {
        assert!(value_history::parse(&change).is_err());
    }
}
