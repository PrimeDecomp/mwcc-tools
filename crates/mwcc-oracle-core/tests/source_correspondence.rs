use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    source_guided::correspondence::{self, Boundary, Selection},
    version::Version,
};

fn selection(class: u8) -> Selection {
    Selection {
        link: "case__Fv".into(),
        class,
        round: 0,
        boundary: Boundary::BeforeSimplify,
    }
}
fn operand(class: u8, register: u32) -> String {
    let mut raw = [0u8; 12];
    raw[1] = class;
    raw[2] = 2;
    raw[4..6].copy_from_slice(&(register as u16).to_le_bytes());
    raw.iter().map(|b| format!("{b:02x}")).collect()
}
fn birth(class: u8, physical: u32, id: usize, name: &str) -> String {
    let register = physical + id as u32 - 1;
    let mut text = format!(
        "pv.value.birth schema=1 function=1 value={id} cls={class} register={register} count={} physical={physical} site=400028 expression=0 phase=0 line=3 category=unknown\n",
        register + 1
    );
    if !name.is_empty() {
        let hex: String = name.bytes().map(|b| format!("{b:02x}")).collect();
        text += &format!(
            "pv.value.object function=1 value={id} object={id:08x} info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex={hex}\n"
        );
    }
    text
}
fn model(
    class: u8,
    physical: u32,
    names: &[&str],
    instructions: &[usize],
    neighbors: &[Vec<u32>],
) -> String {
    let mut initial = [4, 8, 32, 32, 32];
    initial[class as usize] = physical;
    let mut text = format!(
        "pv.function.begin schema=1 function=1 parent=0 c0={} c1={} c2={} c3={} c4={} coverage=direct_counter_writes link=case__Fv\n",
        initial[0], initial[1], initial[2], initial[3], initial[4]
    );
    for (i, name) in names.iter().enumerate() {
        text += &birth(class, physical, i + 1, name);
    }
    let count = physical + names.len() as u32;
    text += &format!(
        "pv.graph.begin schema=1 function=1 graph=1 births={} cls={class} physical={physical} start={physical} count={count} phase=3 boundary=before_simplify\n",
        names.len()
    );
    for (i, node) in instructions.iter().enumerate() {
        text += &format!(
            "pv.graph.instruction p={i:x} op=137 flags=80004000 n=2 line=3 ops={} 020000000000000000000000\n",
            operand(class, physical + *node as u32)
        );
    }
    for (i, ns) in neighbors.iter().enumerate() {
        let raw = ns.iter().map(|n| format!("{n},")).collect::<String>();
        text += &format!(
            "pv.graph.node v={} visible={} deg={} flags=0 color=-1 n={raw}\n",
            physical + i as u32,
            u8::from(instructions.contains(&i)),
            ns.len()
        );
    }
    text += &format!(
        "pv.graph.end function=1 graph=1 nodes={} instructions={} complete=1\n",
        names.len(),
        instructions.len()
    );
    initial[class as usize] = count;
    text += &format!(
        "pv.function.end function=1 births={} graphs=1 ranges=0 c0={} c1={} c2={} c3={} c4={}\n",
        names.len(),
        initial[0],
        initial[1],
        initial[2],
        initial[3],
        initial[4]
    );
    text
}
fn compare(a: &str, b: &str, class: u8) -> correspondence::Comparison {
    correspondence::compare(
        &provenance::parse(a).unwrap(),
        &selection(class),
        &provenance::parse(b).unwrap(),
        &selection(class),
        Version::Gc27,
    )
    .unwrap()
}
#[test]
fn invisible_anchors_map_all_nodes_and_keep_boundary_and_neighbor_multiplicity() {
    let a = model(
        4,
        32,
        &["flag", "other", ""],
        &[],
        &[vec![1, 1, 33, 34], vec![32], vec![32]],
    );
    let b = model(
        4,
        32,
        &["other", "flag", ""],
        &[],
        &[vec![33], vec![1, 32, 34], vec![33]],
    );
    let report = compare(&a, &b, 4);
    assert_eq!(report.coverage.state, EvaluationState::Satisfied);
    assert_eq!(
        report
            .mapping
            .iter()
            .map(|p| (p.first, p.second))
            .collect::<Vec<_>>(),
        vec![(32, 33), (33, 32)]
    );
    assert_eq!(report.unmatched_first, vec![34]);
    assert_eq!(
        report.first.nodes[&32].node.recorded_neighbors,
        vec![1, 1, 33, 34]
    );
    assert!(!report.first.nodes[&32].node.visible_in_instructions);
    assert!(report.neighbor_differences.iter().any(|d| d.first == 32
        && d.neighbor_in_second_space == 1
        && d.first_occurrences == 2
        && d.second_occurrences == 1));
    assert!(
        report
            .boundary_first
            .iter()
            .any(|e| e.node == 32 && e.neighbor == 34)
    );
    assert!(
        report
            .boundary_first
            .iter()
            .any(|e| e.node == 34 && e.neighbor == 32)
    );
    assert_eq!(report.elimination.state, EvaluationState::Indeterminate);
    assert_eq!(
        report.lifetime_correspondence.state,
        EvaluationState::Indeterminate
    );
    assert_eq!(report.first.nodes[&34].unknown_origins.len(), 1);
}
#[test]
fn repeated_names_multiple_nodes_and_reused_numbers_do_not_become_unique_origins() {
    let duplicate = model(4, 32, &["shadow", "shadow"], &[], &[vec![], vec![]]);
    let report = compare(&duplicate, &duplicate, 4);
    assert!(report.mapping.is_empty());
    assert_eq!(report.anchors[0].first_values.len(), 2);
    assert!(!report.anchors[0].usable);
    let one = model(4, 32, &["flag"], &[0], &[vec![]]);
    let rewind = "pv.counter.change function=1 cls=4 old=33 after=32 physical=32 role=3 phase=0 site=400000\n";
    let second = birth(4, 32, 1, "other")
        .replace("value=1", "value=2")
        .replace("object=00000001", "object=00000002");
    let reused = one
        .replace("pv.graph.begin", &format!("{rewind}{second}pv.graph.begin"))
        .replace("births=1", "births=2");
    let report = compare(&reused, &reused, 4);
    assert!(report.mapping.is_empty());
    assert!(
        report
            .conflicts
            .iter()
            .any(|c| c.kind == "incomplete_node_binding")
    );
    let a = provenance::parse(&model(4, 32, &["flag", ""], &[], &[vec![], vec![]])).unwrap();
    let mut b = a.clone();
    let id = b.history.functions[0].births[0].id;
    b.graphs.graphs[0]
        .nodes
        .get_mut(&33)
        .unwrap()
        .binding
        .contributors
        .push(id);
    let report =
        correspondence::compare(&a, &selection(4), &b, &selection(4), Version::Gc27).unwrap();
    assert!(!report.anchors[0].usable);
    assert_eq!(report.anchors[0].second_nodes, vec![32, 33]);
    assert!(report.mapping.is_empty());
}
#[test]
fn conflicts_and_instruction_collisions_are_retained_without_claiming_merges() {
    let a = model(4, 32, &["flag", "other"], &[0, 1], &[vec![], vec![]]);
    let b = model(4, 32, &["other", "flag"], &[0, 1], &[vec![], vec![]]);
    let report = compare(&a, &b, 4);
    assert!(report.mapping.is_empty());
    assert!(
        report
            .conflicts
            .iter()
            .any(|c| c.kind == "competing_targets")
    );
    assert!(report.first.observed_merges.is_empty());
    let a = model(4, 32, &["", ""], &[0, 1], &[vec![], vec![]]);
    let b = model(4, 32, &[""], &[0, 0], &[vec![]]);
    let report = compare(&a, &b, 4);
    assert!(report.mapping.is_empty());
    assert!(
        report
            .conflicts
            .iter()
            .any(|c| c.kind == "many_to_one_hypotheses")
    );
    assert!(report.second.observed_merges.is_empty());
}
#[test]
fn physical_limits_rounds_boundaries_and_capture_coverage_are_explicit() {
    let a = model(1, 8, &[""], &[0], &[vec![1, 1]]);
    let report = compare(&a, &a, 1);
    assert_eq!(report.mapping.len(), 1);
    assert_eq!(report.mapping[0].first, 8);
    assert_eq!(report.first.physical_limit, 8);
    assert!(report.neighbor_differences.is_empty());
    assert!(report.boundary_first.is_empty());
    let parsed = provenance::parse(&a).unwrap();
    let mut wrong = selection(1);
    wrong.round = 1;
    assert!(
        correspondence::compare(&parsed, &wrong, &parsed, &selection(1), Version::Gc27).is_err()
    );
    let partial = a.replace("complete=1", "complete=0");
    let report = compare(&a, &partial, 1);
    assert_eq!(report.coverage.state, EvaluationState::Indeterminate);
    assert!(report.mapping.is_empty());
    let counts = a.replace("graphs=1 ranges=0", "graphs=2 ranges=0");
    assert_eq!(
        compare(&a, &counts, 1).coverage.state,
        EvaluationState::Indeterminate
    );
    let boundary = a.replace(
        "phase=3 boundary=before_simplify",
        "phase=2 boundary=before_simplify",
    );
    assert!(
        correspondence::compare(
            &parsed,
            &selection(1),
            &provenance::parse(&boundary).unwrap(),
            &selection(1),
            Version::Gc27
        )
        .is_err()
    );
    assert!(
        correspondence::compare(
            &parsed,
            &selection(1),
            &parsed,
            &selection(4),
            Version::Gc27
        )
        .is_err()
    );
}
#[test]
fn observed_physical_union_and_its_value_chain_are_separate_from_mapping() {
    let a = model(4, 32, &["flag"], &[], &[vec![]]);
    let mut pass="pv.coalesce.begin schema=1 pass=1 cls=4 count=33 physical=32 first=32 last=33 special=1 function_id=1 creation=direct_counter_writes link=case__Fv\npv.coalesce.candidate pass=1 candidate=1 instruction=aaa flags=10 opcode=123 operands=2 destination_kind=0 destination_class=4 destination=32 source_kind=0 source_class=4 source=4\n".to_owned();
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
    let b = a.replace("pv.graph.begin", &format!("{pass}pv.graph.begin"));
    let report = compare(&a, &b, 4);
    assert_eq!(report.mapping.len(), 1);
    assert!(report.first.observed_merges.is_empty());
    assert_eq!(report.second.observed_merges.len(), 1);
    assert_eq!(report.second.observed_roots[0].root, 4);
    assert_eq!(report.second.observed_roots[0].chain.len(), 1);
    assert_eq!(
        report.second.observed_roots[0].evidence.state,
        EvaluationState::Satisfied
    );
    assert_eq!(report.elimination.state, EvaluationState::Indeterminate);
}

#[test]
fn normalize_symbol_anchors_require_complete_function_capture_and_part_identity() {
    let mut a = model(4, 32, &["", ""], &[], &[vec![], vec![]])
        .replace(
            "coverage=direct_counter_writes",
            "coverage=direct_counter_writes materialization=guarded_normalize_pair_v1",
        )
        .replace("graphs=1 ranges=0", "graphs=1 ranges=0 materializations=2");
    for (id, site) in [(1, "4e2fea"), (2, "4e2ff6")] {
        let old = birth(4, 32, id, "");
        let new = old.replace("site=400028", &format!("site={site}"))
            + &format!(
                "pv.value.materialization schema=1 function=1 value={id} part={id} operand=1234 kind=8 object=abc displacement=4 datatype=0 storage=2 symbol_known=1 symbol_complete=1 symbol_hex=73796d626f6c\n"
            );
        a = a.replace(&old, &new);
    }
    let report = compare(&a, &a, 4);
    assert_eq!(report.mapping.len(), 2);
    assert_eq!(report.anchors.len(), 2);
    let incomplete = a.replace("materializations=2", "materializations=1");
    let report = compare(&a, &incomplete, 4);
    assert!(report.mapping.is_empty());
    assert!(report.anchors.iter().all(|a| !a.usable));
    let displaced = a.replace("displacement=4", "displacement=8");
    let report = compare(&a, &displaced, 4);
    assert!(report.mapping.is_empty());
    assert_eq!(report.anchors.len(), 4);
}

fn promoted_elements(indices: &[u32]) -> String {
    let names = vec![""; indices.len()];
    let neighbors = vec![vec![]; indices.len()];
    let mut text = model(4, 32, &names, &[], &neighbors)
        .replace(
            "coverage=direct_counter_writes",
            "coverage=direct_counter_writes array_promotion=guarded_scalar_v1",
        )
        .replace(
            "graphs=1 ranges=0",
            &format!("graphs=1 ranges=0 array_promotions={}", indices.len()),
        );
    for (i, index) in indices.iter().enumerate() {
        let id = i + 1;
        let original = birth(4, 32, id, "");
        let replacement = original.replace("site=400028", "site=574005")
            + &format!(
                "pv.value.array schema=1 function=1 value={id} promotion=100 object=200 register={} element_index={index} element_size=4 array_size=8 element_count=2 flags=0 datatype=1 storage=0 type_kind=12 type_size=8 name_known=1 name_complete=1 name_hex=666c616773\n",
                32 + i
            );
        text = text.replace(&original, &replacement);
    }
    text
}

#[test]
fn promoted_array_elements_anchor_invisible_nodes_without_claiming_live_values() {
    let a = promoted_elements(&[0, 1]);
    let b = promoted_elements(&[1, 0]).replace("flags=0 datatype=1", "flags=248 datatype=1");
    let report = compare(&a, &b, 4);
    assert_eq!(report.coverage.state, EvaluationState::Satisfied);
    assert_eq!(
        report
            .mapping
            .iter()
            .map(|h| (h.first, h.second))
            .collect::<Vec<_>>(),
        vec![(32, 33), (33, 32)]
    );
    assert_eq!(report.anchors.len(), 2);
    assert!(report.anchors.iter().all(|a| a.usable));
    for side in [&report.first, &report.second] {
        assert!(side.nodes.values().all(|n| !n.node.visible_in_instructions));
        assert!(side.nodes.values().all(|n| n.unknown_origins.is_empty()));
    }
    assert_eq!(report.elimination.state, EvaluationState::Indeterminate);
    assert_eq!(
        report.lifetime_correspondence.state,
        EvaluationState::Indeterminate
    );

    let incomplete = a.replace("array_promotions=2", "array_promotions=1");
    let report = compare(&a, &incomplete, 4);
    assert!(report.mapping.is_empty());
    assert!(report.anchors.iter().all(|a| !a.usable));
    assert!(
        report
            .anchors
            .iter()
            .all(|a| a.limitations.iter().any(|s| s.contains("scalar-array")))
    );

    let duplicate = promoted_elements(&[0, 0]);
    let report = compare(&duplicate, &a, 4);
    assert!(report.mapping.is_empty());
    assert!(report.anchors.iter().all(|a| !a.usable));
    let differently_typed = a.replace("flags=0 datatype=1", "flags=4 datatype=1");
    assert!(compare(&a, &differently_typed, 4).mapping.is_empty());
}
