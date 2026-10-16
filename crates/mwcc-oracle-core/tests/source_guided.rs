use mwcc_oracle_core::{
    allocation_category::Category,
    evidence::{Evaluation, EvaluationState},
    provenance,
    runner::{CheckStatus, Compilation, TraceOptions},
    source_guided::{
        inputs,
        objective::{self, Contract, Input, ObjectAnchor, Objective, Purpose, Selection},
        search::{self, Edit},
        state,
    },
    value_history::ObjectPart,
    version::Version,
};
use std::{fs, path::PathBuf};
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn reg(register: u32, effect: u16) -> String {
    format!(
        "0004{}{}000000000000",
        effect
            .to_le_bytes()
            .iter()
            .map(|v| format!("{v:02x}"))
            .collect::<String>(),
        (register as u16)
            .to_le_bytes()
            .iter()
            .map(|v| format!("{v:02x}"))
            .collect::<String>()
    )
}
fn instruction(
    prefix: &str,
    pointer: &str,
    opcode: u32,
    destination: u32,
    source: Option<u32>,
) -> String {
    if let Some(source) = source {
        format!(
            "{prefix} p={pointer} op={opcode} flags=80000010 n=3 line=3 ops={} {} 060000000000000000000000\n",
            reg(destination, 2),
            reg(source, 1)
        )
    } else {
        format!(
            "{prefix} p={pointer} op={opcode} flags=80004000 n=2 line=3 ops={} 020000000000000000000000\n",
            reg(destination, 2)
        )
    }
}
fn birth(id: u32, name: &str) -> String {
    let register = 31 + id;
    let hex: String = name.bytes().map(|b| format!("{b:02x}")).collect();
    format!(
        "pv.value.birth schema=1 function=1 value={id} cls=4 register={register} count={} physical=32 site=400028 expression=0 phase=0 line=3 category=unknown\npv.value.object function=1 value={id} object={id:08x} info=123 part=1 datatype=1 storage=0 flags_before=0 type_kind=1 type_size=4 name_known=1 name_complete=1 name_hex={hex}\n",
        register + 1
    )
}
fn synthetic(copy: bool, inside: bool, swap: bool) -> String {
    let mut t="pv.function.begin schema=1 function=1 parent=0 c0=4 c1=8 c2=32 c3=32 c4=32 coverage=direct_counter_writes link=pipeline__Fii\npv.category.initial function=1 births=0 i2=31 i3=31 i4=31\n".to_owned();
    let window = |births, first| {
        format!(
            "pv.category.window function=1 births={births} f2=32 f3=32 f4={first} l2=32 l3=32 l4={first}\n"
        )
    };
    if inside {
        t += &window(0, 32);
    }
    t += &birth(1, if swap { "other" } else { "flag" });
    t += &birth(2, if swap { "flag" } else { "other" });
    if !inside {
        t += &window(2, 34);
    }
    let first = if inside { 32 } else { 34 };
    let flag = if swap { 33 } else { 32 };
    let other = if swap { 32 } else { 33 };
    t += "ra.before function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n";
    t += &instruction("ra.i", "aaa", 137, if copy { other } else { flag }, None);
    if copy {
        t += &instruction("ra.i", "bbb", 139, flag, Some(other));
    }
    t += "ra.end\n";
    t += &format!(
        "pv.coalesce.begin schema=1 pass=1 cls=4 count=34 physical=32 first={first} last=34 special=1 function_id=1 creation=direct_counter_writes link=pipeline__Fii\n"
    );
    if copy {
        t += &format!(
            "pv.coalesce.candidate pass=1 candidate=1 instruction=bbb flags=80000010 opcode=139 operands=3 destination_kind=0 destination_class=4 destination={flag} source_kind=0 source_class=4 source={other}\n"
        );
        for (condition, site, taken, eflags) in [
            ("copy_flag", "10f", 0, "40"),
            ("destination_kind", "17b", 0, "40"),
            ("register_class", "18b", 0, "40"),
            ("roots_differ", "1d6", 1, "0"),
            ("interference", "244", 0, "40"),
            ("destination_below_window", "271", 1, "80"),
        ] {
            t += &format!(
                "pv.coalesce.check pass=1 candidate=1 site={site} condition={condition} taken={taken} eflags={eflags} eax=0 ebx={other:x} ecx=0 edx={flag:x} esi=0 edi=0 ebp=0\n"
            );
        }
    }
    t += &format!(
        "pv.coalesce.end pass=1 candidates={}\npv.graph.begin schema=1 function=1 graph=1 births=2 cls=4 physical=32 start=32 count=34 phase=3 boundary=before_simplify category_ready=1 initial_last=31 first={first} last=34\n",
        u32::from(copy)
    );
    t += &instruction(
        "pv.graph.instruction",
        "aaa",
        137,
        if copy { other } else { flag },
        None,
    );
    if copy {
        t += &instruction("pv.graph.instruction", "bbb", 139, flag, Some(other));
    }
    for register in 32..34 {
        t += &format!(
            "pv.graph.node v={register} visible={} deg=1 flags=0 color=-1 n={},\n",
            u32::from(copy || register == flag),
            65 - register
        );
    }
    t += &format!(
        "pv.graph.end function=1 graph=1 nodes=2 instructions={} complete=1\nra.graph cls=4 k=29 start=32 count=34\nra.node v=32 p=1000 deg=1 flags=0 color=-1 n=33,\nra.node v=33 p=1020 deg=1 flags=0 color=-1 n=32,\nra.order cls=4 v=1000:0,1020:0,\nra.select cls=4 mask=0 after=60 ok=1 v=1000:5:0,1020:6:0,\nra.after function=pipeline link=pipeline__Fii\nra.block p=100 index=0 weight=1 flags=0 succ=\n",
        1 + u32::from(copy)
    );
    t += &instruction(
        "ra.i",
        "aaa",
        137,
        if copy { other - 27 } else { flag - 27 },
        None,
    );
    if copy {
        t += &instruction("ra.i", "bbb", 139, flag - 27, Some(other - 27));
    }
    t += "ra.end\npv.function.end function=1 births=2 graphs=1 ranges=2 c0=4 c1=8 c2=32 c3=32 c4=34\n";
    t
}
fn compilation(trace: &str, source: &[u8]) -> Compilation {
    let object = fs::read(
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/oracle/GC_2_7/pipeline.o"),
    )
    .unwrap();
    Compilation {
        version: Version::Gc27,
        source: "model.c".into(),
        oracle: "model".into(),
        producer_sha256: "producer".into(),
        original_sha256: Some("stock".into()),
        source_sha256: state::digest(source),
        flags: vec!["-O4,p".into()],
        trace_options: TraceOptions {
            backend: true,
            ..TraceOptions::default()
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
fn contract(objective: Objective) -> Contract {
    Contract {
        format_version: 1,
        selection: Selection {
            link: "pipeline__Fii".into(),
            class: 4,
            round: 0,
        },
        objective,
        relocations: vec![],
        purpose: Purpose::MechanismReproducer,
    }
}
fn symbolic_pair_trace() -> String {
    let mut trace = synthetic(false, true, false)
        .replace(
            "coverage=direct_counter_writes",
            "coverage=direct_counter_writes materialization=guarded_normalize_pair_v1",
        )
        .replace(
            "births=2 graphs=1 ranges=2 c0=",
            "births=2 graphs=1 ranges=2 materializations=2 c0=",
        );
    for (id, name, site) in [(1, "flag", "4e2fea"), (2, "other", "4e2ff6")] {
        trace = trace.replace(&birth(id, name), &format!("{}pv.value.materialization schema=1 function=1 value={id} part={id} operand=1234 kind=8 object=abc displacement=0 datatype=0 storage=2 symbol_known=1 symbol_complete=1 symbol_hex=73796d5f5f3143\n", birth(id, name).replace("site=400028", &format!("site={site}"))));
    }
    trace
}
#[test]
fn symbol_pair_objective_counts_a_guarded_mechanism_and_requires_complete_identity_and_graph_bindings()
 {
    let source = b"model source";
    let trace = symbolic_pair_trace();
    let a = compilation(&trace, source);
    let ar = provenance::parse(&a.trace).unwrap();
    let input = Input {
        compilation: &a,
        provenance: &ar,
        source,
        semantic_check: None,
    };
    let c = contract(Objective::SymbolAddressPairs {
        symbol: "sym__1C".into(),
        minimum: 1,
        maximum: 1,
    });
    let result = objective::evaluate(&c, &input, &input).unwrap();
    assert_eq!(result.overall.state, EvaluationState::Satisfied);
    assert_eq!(result.observations["candidate"]["count"], 1);
    assert_eq!(
        result.observations["candidate"]["pairs"][0]["low"]["sequence"],
        2
    );
    let zero = contract(Objective::SymbolAddressPairs {
        symbol: "sym__1C".into(),
        minimum: 0,
        maximum: 0,
    });
    assert_eq!(
        objective::evaluate(&zero, &input, &input)
            .unwrap()
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
    for changed in [
        trace.replace("materializations=2", "materializations=1"),
        trace.replace(
            "symbol_known=1 symbol_complete=1 symbol_hex=73796d5f5f3143",
            "symbol_known=0 symbol_complete=0 symbol_hex=",
        ),
        trace.replace("symbol_complete=1", "symbol_complete=0"),
        trace.replace("value=2 part=2 operand=1234", "value=2 part=2 operand=5678"),
    ] {
        let b = compilation(&changed, source);
        let br = provenance::parse(&b.trace).unwrap();
        let candidate = Input {
            compilation: &b,
            provenance: &br,
            source,
            semantic_check: None,
        };
        assert_eq!(
            objective::evaluate(&zero, &input, &candidate)
                .unwrap()
                .objective
                .state,
            EvaluationState::Indeterminate
        );
    }
    let mut br = ar.clone();
    br.graphs.graphs[0]
        .nodes
        .get_mut(&33)
        .unwrap()
        .binding
        .direct
        .clear();
    let candidate = Input {
        compilation: &a,
        provenance: &br,
        source,
        semantic_check: None,
    };
    assert_eq!(
        objective::evaluate(&zero, &input, &candidate)
            .unwrap()
            .objective
            .state,
        EvaluationState::Indeterminate
    );
    let graph = state::graph(&ar, "pipeline__Fii", 4, 0).unwrap();
    let before = state::fingerprint(graph, &ar, &a).unwrap();
    let mut changed = ar.clone();
    changed.history.functions[0].births[1]
        .materialization
        .as_mut()
        .unwrap()
        .symbol_bytes
        .push(b'X');
    assert_ne!(before, state::fingerprint(graph, &changed, &a).unwrap());
    changed = ar.clone();
    // Moving both halves preserves their identity relation. Moving only one
    // makes the observed pair inconsistent and must change its state.
    for birth in &mut changed.history.functions[0].births {
        let binding = birth.materialization.as_mut().unwrap();
        binding.operand = "9999".into();
        binding.object = "ffff".into();
    }
    assert_eq!(before, state::fingerprint(graph, &changed, &a).unwrap());
    changed.history.functions[0].births[1]
        .materialization
        .as_mut()
        .unwrap()
        .operand = "8888".into();
    assert_ne!(before, state::fingerprint(graph, &changed, &a).unwrap());
    assert!(
        objective::validate(&contract(Objective::SymbolAddressPairs {
            symbol: "sym".into(),
            minimum: 2,
            maximum: 1
        }))
        .is_err()
    );
}
fn anchor() -> ObjectAnchor {
    ObjectAnchor {
        name: "flag".into(),
        part: ObjectPart::Single,
        type_kind: 1,
        type_size: 4,
    }
}
#[test]
fn range_objective_requires_real_change_and_independent_hard_constraints() {
    let source = b"model source";
    let a = compilation(&synthetic(false, false, false), source);
    let b = compilation(&synthetic(false, true, false), source);
    let ar = provenance::parse(&a.trace).unwrap();
    let br = provenance::parse(&b.trace).unwrap();
    let from = Input {
        compilation: &a,
        provenance: &ar,
        source,
        semantic_check: None,
    };
    let to = Input {
        compilation: &b,
        provenance: &br,
        source,
        semantic_check: None,
    };
    let mut c = contract(Objective::ChangeRangeCategory {
        value: anchor(),
        target: Category::InsideCoalescingWindow,
    });
    let report = objective::evaluate(&c, &from, &to).unwrap();
    assert_eq!(report.overall.state, EvaluationState::Satisfied);
    let mut incomplete = br.clone();
    incomplete.graphs.functions.clear();
    let incomplete_input = Input {
        compilation: &b,
        provenance: &incomplete,
        source,
        semantic_check: None,
    };
    let incomplete_report = objective::evaluate(&c, &from, &incomplete_input).unwrap();
    assert_eq!(
        incomplete_report.objective.state,
        EvaluationState::Satisfied
    );
    assert_eq!(
        incomplete_report.overall.state,
        EvaluationState::Indeterminate
    );
    incomplete.graphs.graphs.clear();
    let missing_graph = Input {
        compilation: &b,
        provenance: &incomplete,
        source,
        semantic_check: None,
    };
    let missing_report = objective::evaluate(&c, &from, &missing_graph).unwrap();
    assert_eq!(missing_report.overall.state, EvaluationState::Indeterminate);
    assert!(!missing_report.score.eligible);
    assert_eq!(
        report.graph_changes["elimination"]["state"],
        "indeterminate"
    );
    assert_eq!(
        objective::evaluate(&c, &from, &from).unwrap().overall.state,
        EvaluationState::Unsatisfied
    );
    c.purpose = Purpose::SourceTransformation;
    assert_eq!(
        objective::evaluate(&c, &from, &to).unwrap().overall.state,
        EvaluationState::Indeterminate
    );
    let checked_from = Input {
        semantic_check: Some(known()),
        ..from
    };
    let checked_to = Input {
        semantic_check: Some(known()),
        ..to
    };
    assert_eq!(
        objective::evaluate(&c, &checked_from, &checked_to)
            .unwrap()
            .overall
            .state,
        EvaluationState::Satisfied
    );
    let failed_check = Input {
        compilation: &b,
        provenance: &br,
        source,
        semantic_check: Some(Evaluation {
            state: EvaluationState::Unsatisfied,
            reasons: vec![],
        }),
    };
    assert_eq!(
        objective::evaluate(&c, &checked_from, &failed_check)
            .unwrap()
            .overall
            .state,
        EvaluationState::Unsatisfied
    );
    let wrong_source = Input {
        source: b"different",
        ..checked_to
    };
    assert_eq!(
        objective::evaluate(&c, &checked_from, &wrong_source)
            .unwrap()
            .overall
            .state,
        EvaluationState::Indeterminate
    );
    let mut changed = b;
    changed.flags.push("-opt off".into());
    let different_flags = Input {
        compilation: &changed,
        provenance: &br,
        source,
        semantic_check: Some(known()),
    };
    assert_eq!(
        objective::evaluate(&c, &checked_from, &different_flags)
            .unwrap()
            .overall
            .state,
        EvaluationState::Unsatisfied
    );
}
#[test]
fn direct_initialization_tracks_the_original_value_copy_and_final_color() {
    let source = b"model source";
    let a = compilation(&synthetic(true, false, false), source);
    let b = compilation(&synthetic(false, false, false), source);
    let ar = provenance::parse(&a.trace).unwrap();
    let br = provenance::parse(&b.trace).unwrap();
    let from = Input {
        compilation: &a,
        provenance: &ar,
        source,
        semantic_check: None,
    };
    let to = Input {
        compilation: &b,
        provenance: &br,
        source,
        semantic_check: None,
    };
    let c = contract(Objective::DirectInitialization {
        value: anchor(),
        constant: 0,
        source_line: Some(3),
    });
    let failed = objective::evaluate(&c, &from, &from).unwrap();
    assert_eq!(failed.objective.state, EvaluationState::Unsatisfied);
    assert_eq!(failed.observations["copy_survives"], true);
    let passed = objective::evaluate(&c, &from, &to).unwrap();
    assert_eq!(passed.overall.state, EvaluationState::Satisfied);
    assert_eq!(passed.observations["assigned_register"], 5);
    let mut wrong = b;
    wrong.trace = wrong.trace.replace(
        &instruction("ra.i", "aaa", 137, 5, None),
        &instruction("ra.i", "aaa", 137, 7, None),
    );
    let wrong_report = provenance::parse(&wrong.trace).unwrap();
    let different_color = Input {
        compilation: &wrong,
        provenance: &wrong_report,
        source,
        semantic_check: None,
    };
    assert_eq!(
        objective::evaluate(&c, &from, &different_color)
            .unwrap()
            .objective
            .state,
        EvaluationState::Unsatisfied
    );
}
#[test]
fn missing_ambiguous_or_partial_evidence_never_proves_elimination_or_category_change() {
    let source = b"model source";
    let a = compilation(&synthetic(false, false, false), source);
    let ar = provenance::parse(&a.trace).unwrap();
    let from = Input {
        compilation: &a,
        provenance: &ar,
        source,
        semantic_check: None,
    };
    let eliminate = contract(Objective::EliminateTemporary { value: anchor() });
    assert_eq!(
        objective::evaluate(&eliminate, &from, &from)
            .unwrap()
            .objective
            .state,
        EvaluationState::Indeterminate
    );
    for text in [
        synthetic(false, true, false).replace("name_hex=666c6167", "name_hex=6d697373696e67"),
        synthetic(false, true, false).replace("name_hex=6f74686572", "name_hex=666c6167"),
        synthetic(false, true, false).replace("category_ready=1", "category_ready=0"),
    ] {
        let b = compilation(&text, source);
        let br = provenance::parse(&text).unwrap();
        let to = Input {
            compilation: &b,
            provenance: &br,
            source,
            semantic_check: None,
        };
        let c = contract(Objective::ChangeRangeCategory {
            value: anchor(),
            target: Category::InsideCoalescingWindow,
        });
        assert_eq!(
            objective::evaluate(&c, &from, &to).unwrap().objective.state,
            EvaluationState::Indeterminate
        );
        assert_eq!(
            objective::evaluate(&eliminate, &from, &to)
                .unwrap()
                .overall
                .state,
            EvaluationState::Indeterminate
        );
    }
}
#[test]
fn equivalent_states_group_pointers_but_keep_category_and_origin_evidence() {
    let source = b"model source";
    let a = compilation(&synthetic(true, false, false), source);
    let mut ar = provenance::parse(&a.trace).unwrap();
    let key = state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap();
    for birth in &mut ar.history.functions[0].births {
        if let Some(o) = &mut birth.object {
            let moved = |pointer: &str| {
                format!("{:x}", u32::from_str_radix(pointer, 16).unwrap() + 0x10000)
            };
            o.object = moved(&o.object);
            o.variable_info = moved(&o.variable_info);
        }
    }
    assert_eq!(
        key,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap()
    );
    // Identical selected instructions do not justify grouping objects whose
    // allocated data differs (for example a changed literal or unwind entry).
    let elf = mwcc_oracle_core::elf::Elf::parse(&a.object).unwrap();
    let section = elf
        .sections
        .iter()
        .position(|s| s.kind == 1 && s.flags & 2 != 0 && s.flags & 4 == 0 && !s.data.is_empty())
        .unwrap();
    let headers = u32::from_be_bytes(a.object[32..36].try_into().unwrap()) as usize;
    let width = u16::from_be_bytes(a.object[46..48].try_into().unwrap()) as usize;
    let offset = headers + width * section + 16;
    let data = u32::from_be_bytes(a.object[offset..offset + 4].try_into().unwrap()) as usize;
    let mut changed_data = a.object.clone();
    changed_data[data] ^= 1;
    assert_eq!(
        state::object_instructions(&a.object, "pipeline__Fii").unwrap(),
        state::object_instructions(&changed_data, "pipeline__Fii").unwrap()
    );
    let mut changed_compilation = a.clone();
    changed_compilation.object = changed_data;
    assert_ne!(
        key,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &changed_compilation).unwrap()
    );
    ar.graphs.graphs[0]
        .nodes
        .get_mut(&32)
        .unwrap()
        .stored_degree += 1;
    assert_ne!(
        key,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap()
    );
    ar.graphs.graphs[0]
        .nodes
        .get_mut(&32)
        .unwrap()
        .recorded_neighbors = vec![5, 33];
    let ordered = state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap();
    ar.graphs.graphs[0]
        .nodes
        .get_mut(&32)
        .unwrap()
        .recorded_neighbors
        .reverse();
    assert_ne!(
        ordered,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap()
    );
    // Retail callers can leave pointer bits in the unused upper half of a
    // short argument. Those bits are preserved in observations but must not
    // split otherwise identical completed graph states.
    ar.history.functions[0].births[0].expression = Some(1);
    ar.history.functions[0]
        .expressions
        .push(mwcc_oracle_core::value_history::Expression {
            id: 1,
            trace_position: 1,
            node: "opaque".into(),
            kind: 56,
            line: Some(3),
            scope: "codegen_dispatch".into(),
            complete: false,
            tree_bytes: b"symbol".to_vec(),
            tree_text: Some("symbol".into()),
            parent: None,
            dispatch: Some(mwcc_oracle_core::value_history::Dispatch {
                retail_target: 0x440000,
                requested: [4, 0],
            }),
            result: None,
            end_position: Some(2),
            abandoned: false,
        });
    let scoped = state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap();
    ar.history.functions[0].expressions[0]
        .dispatch
        .as_mut()
        .unwrap()
        .requested[0] = 0xabcd0004;
    assert_eq!(
        scoped,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap()
    );
    ar.history.functions[0].expressions[0]
        .dispatch
        .as_mut()
        .unwrap()
        .retail_target = 0x441000;
    assert_ne!(
        scoped,
        state::fingerprint(&ar.graphs.graphs[0], &ar, &a).unwrap()
    );
}
#[test]
fn source_edits_are_atomic_and_keep_unrelated_encoded_bytes() {
    let source = b"\xff unsigned flag = 0; return flag;";
    let edits = [
        Edit {
            find: "unsigned flag = 0;".into(),
            replacement: "unsigned flag = (0);".into(),
        },
        Edit {
            find: "return flag;".into(),
            replacement: "return (flag);".into(),
        },
    ];
    assert_eq!(
        search::apply(source, &edits).unwrap(),
        b"\xff unsigned flag = (0); return (flag);"
    );
    assert!(
        search::apply(
            b"x x",
            &[Edit {
                find: "x".into(),
                replacement: "y".into()
            }]
        )
        .is_err()
    );
    assert!(
        search::apply(
            b"abc",
            &[
                Edit {
                    find: "ab".into(),
                    replacement: "x".into()
                },
                Edit {
                    find: "bc".into(),
                    replacement: "y".into()
                }
            ]
        )
        .is_err()
    );
}
#[test]
fn dependency_tokens_preserve_spaces_and_resolve_actual_file_identities() {
    let scratch = tempfile::tempdir().unwrap();
    fs::create_dir(scratch.path().join("space dir")).unwrap();
    fs::write(scratch.path().join("source.c"), b"source").unwrap();
    fs::write(scratch.path().join("space dir/header name.h"), b"header").unwrap();
    let paths = inputs::dependency_paths(
        "unit.o: source.c \\\n space\\ dir/header\\ name.h\n",
        scratch.path(),
    )
    .unwrap();
    assert_eq!(paths.len(), 2);
    assert!(
        paths.contains(
            &scratch
                .path()
                .join("space dir/header name.h")
                .canonicalize()
                .unwrap()
        )
    );
    assert!(inputs::dependency_paths("unit.o: missing.h", scratch.path()).is_err());
    assert!(inputs::dependency_paths("missing separator", scratch.path()).is_err());
}

#[test]
fn fixed_quantizer_operands_keep_hardware_identity_and_reject_other_shapes() {
    use mwcc_oracle_core::trace::{Instruction, Operand};
    let report = provenance::parse(&synthetic(false, true, false)).unwrap();
    let mut graph = report.graphs.graphs[0].clone();
    graph.instructions.clear();
    let mut reference = None;
    for opcode in [403, 407] {
        for register in 912u16..=919 {
            let mut raw = [0u8; 12];
            raw[..4].copy_from_slice(&[1, 0, 1, 0]);
            raw[4..6].copy_from_slice(&register.to_le_bytes());
            graph.instructions = vec![Instruction {
                pointer: "123".into(),
                opcode,
                flags: 0x122,
                operands: vec![Operand { raw }],
                line: None,
            }];
            let normalized = state::instructions(&graph).unwrap();
            assert_eq!(normalized[0]["operands"][0], serde_json::json!(raw));
            if let Some(reference) = &reference {
                assert_ne!(reference, &normalized);
            }
            reference = Some(normalized);
            for (offset, value) in [(1, 4), (2, 2), (4, 0), (6, 1), (10, 1)] {
                let saved = graph.instructions[0].operands[0].raw[offset];
                graph.instructions[0].operands[0].raw[offset] = value;
                assert!(state::instructions(&graph).is_err());
                graph.instructions[0].operands[0].raw[offset] = saved;
            }
        }
    }
    graph.instructions[0].opcode = 137;
    assert!(state::instructions(&graph).is_err());
}

#[test]
fn retained_state_analysis_checks_objects_coverage_and_unsupported_normalization() {
    let source = b"model source";
    let trace = synthetic(false, true, false);
    let mut c = compilation(&trace, source);
    let analysis = state::inspect(&c, "pipeline__Fii", 4, 0).unwrap();
    assert_eq!(analysis.fingerprint_format_version, 3);
    assert_eq!(analysis.normalization.state, EvaluationState::Satisfied);
    assert_eq!(analysis.fingerprint.as_ref().unwrap().len(), 64);
    let original_object = c.object.clone();
    c.object[0] ^= 1;
    c.object_sha256 = state::digest(&c.object);
    assert!(state::inspect(&c, "pipeline__Fii", 4, 0).is_err());
    c.object = original_object;
    c.object_sha256 = state::digest(&c.object);
    c.object[0] ^= 1;
    assert!(
        format!(
            "{:#}",
            state::inspect(&c, "pipeline__Fii", 4, 0).unwrap_err()
        )
        .contains("object bytes")
    );
    c.object[0] ^= 1;
    c.check_status = CheckStatus::Skipped;
    assert!(state::inspect(&c, "pipeline__Fii", 4, 0).is_err());
    c.check_status = CheckStatus::Verified;
    for identical in [None, Some(false)] {
        c.identical = identical;
        assert!(state::inspect(&c, "pipeline__Fii", 4, 0).is_err());
    }
    c.identical = Some(true);
    assert!(state::inspect(&c, "pipeline__Fii", 4, 99).is_err());
    c.trace = trace.replace("graphs=1 ranges=2", "graphs=2 ranges=2");
    assert!(state::inspect(&c, "pipeline__Fii", 4, 0).is_err());
    c.trace = trace.replace("020000000000000000000000", "630000000000000000000000");
    let analysis = state::inspect(&c, "pipeline__Fii", 4, 0).unwrap();
    assert!(analysis.fingerprint.is_none());
    assert_eq!(analysis.normalization.state, EvaluationState::Indeterminate);
    assert_eq!(
        analysis.normalization.reasons[0].code,
        mwcc_oracle_core::evidence::EvidenceGap::CompilerStateCoverage
    );
    assert!(
        analysis.normalization.reasons[0]
            .detail
            .contains("operand kind 99")
    );
}

#[test]
fn backend_origin_fingerprints_keep_elements_and_call_shapes_but_ignore_partial_output_bytes() {
    let t = synthetic(false, true, false);
    let c = compilation(&t, b"model source");
    let mut report = provenance::parse(&t).unwrap();
    report.history.functions[0].births[0].array_promotion=Some(serde_json::from_value(serde_json::json!({
        "trace_position":3,"promotion":"100","object":"200","element_index":1,"element_size":4,"array_size":8,"element_count":2,"flags":0,"datatype":1,"storage":0,"type_kind":12,"type_size":8,"name_known":true,"name_complete":true,"name_bytes":[102,108,97,103],"name_text":"flag"
    })).unwrap());
    report.history.functions[0].births[1].call_result=Some(serde_json::from_value(serde_json::json!({
        "trace_position":4,"part":"single","item":"300","kind":54,"callee":"400","return_type":"500","type_kind":8,"type_size":4,"output":"600","output_kind":0,"output_bytes":vec![0;24],"output_complete":false,"tree_known":true,"tree_complete":false,"tree_bytes":[99,97,108,108],"tree_text":"call"
    })).unwrap());
    let fingerprint = |report: &mwcc_oracle_core::provenance::Report| {
        state::fingerprint(&report.graphs.graphs[0], report, &c).unwrap()
    };
    let original = fingerprint(&report);
    let call = report.history.functions[0].births[1]
        .call_result
        .as_mut()
        .unwrap();
    call.item = "301".into();
    call.callee = "401".into();
    call.return_type = "501".into();
    call.output = "601".into();
    call.output_bytes = vec![0xff; 24];
    call.output_bytes[0] = 0;
    let array = report.history.functions[0].births[0]
        .array_promotion
        .as_mut()
        .unwrap();
    array.promotion = "101".into();
    array.object = "201".into();
    array.flags = 0xf8;
    assert_eq!(original, fingerprint(&report));
    report.history.functions[0].births[0]
        .array_promotion
        .as_mut()
        .unwrap()
        .element_index = 0;
    assert_ne!(original, fingerprint(&report));
    report.history.functions[0].births[0]
        .array_promotion
        .as_mut()
        .unwrap()
        .element_index = 1;
    report.history.functions[0].births[1]
        .call_result
        .as_mut()
        .unwrap()
        .kind = 55;
    assert_ne!(original, fingerprint(&report));
}

fn scalarized_array_trace(copy: bool) -> String {
    let frontend = "pv.scalar.begin schema=1 pass=1 function_object=abc coverage=guarded_temp_returns_v1 link=pipeline__Fii\npv.scalar.binding schema=1 pass=1 binding=1 function_object=abc site=461748 part=100 source_linear=101 source_index=7 source_kind=2 variable=102 source_object=110 source_info=111 source_type=112 source_type_kind=12 source_type_size=8 source_datatype=1 source_storage=0 source_name_known=1 source_name_complete=1 source_name_hex=666c6167 source_element_known=1 source_element_type_kind=1 source_element_type_size=4 byte_offset=4 byte_size=4 bit_first=32 bit_last=63 flags=0 temporary=1 temporary_info=123 temporary_type=202 temporary_type_kind=1 temporary_type_size=4\npv.scalar.end schema=1 pass=1 function_object=abc bindings=1 coverage=guarded_temp_returns_v1\n";
    let backend=synthetic(copy,true,false).replace("function=1 parent=0", "function=1 parent=0 function_object=abc scalarization=guarded_temp_returns_v1").replace("pv.function.end function=1", "pv.function.end function=1 scalarization=guarded_temp_returns_v1 scalar_passes=1 scalar_bindings=1 scalar_first_pass=1 scalar_last_pass=1");
    let frontend = frontend.replace("function_object=abc", "function=1 function_object=abc");
    backend.replacen(
        "pv.category.initial",
        &(frontend.to_owned() + "pv.category.initial"),
        1,
    )
}
#[test]
fn scalarized_source_fingerprints_keep_intervals_but_ignore_capture_identities() {
    let text = scalarized_array_trace(false);
    let c = compilation(&text, b"model source");
    let fingerprint = |text: &str| {
        let report = provenance::parse(text).unwrap();
        assert_eq!(report.scalarization.backend_values.len(), 1);
        assert_eq!(
            report.scalarization.backend_values[0].observations.state,
            EvaluationState::Satisfied
        );
        let mut capture = c.clone();
        capture.trace = text.to_owned();
        state::fingerprint(&report.graphs.graphs[0], &report, &capture).unwrap()
    };
    let original = fingerprint(&text);
    let moved = text
        .replace("source_object=110", "source_object=210")
        .replace("part=100 source_linear=101", "part=300 source_linear=301")
        .replace("variable=102", "variable=302")
        .replace("source_info=111", "source_info=311")
        .replace("source_type=112", "source_type=312")
        .replace("temporary_type=202", "temporary_type=402")
        .replace("source_index=7", "source_index=42")
        .replace("function_object=abc", "function_object=def")
        .replace("pass=1", "pass=2")
        .replace(
            "scalar_first_pass=1 scalar_last_pass=1",
            "scalar_first_pass=2 scalar_last_pass=2",
        );
    assert_eq!(original, fingerprint(&moved));
    let other_element = text.replace(
        "byte_offset=4 byte_size=4 bit_first=32 bit_last=63",
        "byte_offset=0 byte_size=4 bit_first=0 bit_last=31",
    );
    assert_ne!(original, fingerprint(&other_element));
    assert_ne!(
        original,
        fingerprint(&text.replace("source_name_hex=666c6167", "source_name_hex=6f74686572"))
    );
}

#[test]
fn scalarization_state_rejects_unknown_normalization_and_keeps_pointer_aliases() {
    let text = scalarized_array_trace(false);
    let c = compilation(&text, b"model source");
    let fingerprint = |report: &mwcc_oracle_core::provenance::Report| {
        state::fingerprint(&report.graphs.graphs[0], report, &c)
    };
    let report = provenance::parse(&text).unwrap();
    let original = fingerprint(&report).unwrap();
    let mut alias = report.clone();
    alias.scalarization.passes[0].bindings[0]
        .temporary
        .type_pointer = alias.scalarization.passes[0].bindings[0]
        .source
        .type_pointer
        .clone();
    assert_ne!(original, fingerprint(&alias).unwrap());

    let mut unsupported = report.clone();
    unsupported.scalarization.passes[0].schema = 2;
    assert!(fingerprint(&unsupported).is_err());
    let mut unsupported_site = report.clone();
    unsupported_site.scalarization.passes[0].bindings[0].site = 0x123456;
    assert!(fingerprint(&unsupported_site).is_err());

    let unknown_gap = text.replace(
        "pv.scalar.end",
        "pv.scalar.gap schema=1 pass=1 function=1 function_object=abc site=461748 reason=unknown future_pointer=deadbeef\npv.scalar.end",
    );
    let gap_report = provenance::parse(&unknown_gap).unwrap();
    let mut gap_capture = c.clone();
    gap_capture.trace = unknown_gap;
    let error =
        state::fingerprint(&gap_report.graphs.graphs[0], &gap_report, &gap_capture).unwrap_err();
    assert!(
        error
            .to_string()
            .contains("unsupported scalarization gap field")
    );
}

#[test]
fn scalarized_element_initialization_uses_exact_source_binding_and_final_assignment() {
    use objective::PromotedElementAnchor;
    let c = contract(Objective::ScalarizedArrayInitialization {
        value: PromotedElementAnchor {
            name: "flag".into(),
            element_index: 1,
            element_size: 4,
        },
        constant: 0,
        source_line: Some(3),
    });
    let evaluate = |text: &str| {
        let compilation = compilation(text, b"model source");
        let report = provenance::parse(text).unwrap();
        let input = Input {
            compilation: &compilation,
            provenance: &report,
            source: b"model source",
            semantic_check: None,
        };
        objective::evaluate(&c, &input, &input).unwrap()
    };
    let text = scalarized_array_trace(false);
    let passed = evaluate(&text);
    assert_eq!(passed.overall.state, EvaluationState::Satisfied);
    assert_eq!(
        passed.observations["source_element"]["array_element"]["element_index"],
        1
    );
    assert_eq!(
        evaluate(&scalarized_array_trace(true)).overall.state,
        EvaluationState::Unsatisfied
    );
    assert_eq!(
        evaluate(&text.replace("temporary_info=123", "temporary_info=456"))
            .overall
            .state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        evaluate(&text.replace("scalar_bindings=1", "scalar_bindings=2"))
            .overall
            .state,
        EvaluationState::Indeterminate
    );
    assert_eq!(
        evaluate(&text.replace(
            "byte_offset=4 byte_size=4 bit_first=32 bit_last=63",
            "byte_offset=0 byte_size=4 bit_first=0 bit_last=31"
        ))
        .overall
        .state,
        EvaluationState::Indeterminate
    );
    // A second creation with no backend birth still makes the source-name
    // selector ambiguous; graph-node-only state grouping must not hide it.
    let line = text
        .lines()
        .find(|l| l.starts_with("pv.scalar.binding"))
        .unwrap();
    let extra = line
        .replace("binding=1", "binding=2")
        .replace("temporary=1", "temporary=3")
        .replace("temporary_info=123", "temporary_info=456");
    let ambiguous = text
        .replace("pv.scalar.end", &(extra.clone() + "\npv.scalar.end"))
        .replace("bindings=1 coverage=", "bindings=2 coverage=")
        .replace("scalar_bindings=1", "scalar_bindings=2");
    assert_eq!(
        evaluate(&ambiguous).overall.state,
        EvaluationState::Indeterminate
    );
    let partial_extra = extra.replace(
        "source_element_known=1 source_element_type_kind=1 source_element_type_size=4",
        "source_element_known=0 source_element_type_kind=-1 source_element_type_size=-1",
    );
    let partial = text
        .replace("pv.scalar.end", &(partial_extra + "\npv.scalar.end"))
        .replace("bindings=1 coverage=", "bindings=2 coverage=")
        .replace("scalar_bindings=1", "scalar_bindings=2");
    assert_eq!(
        evaluate(&partial).overall.state,
        EvaluationState::Indeterminate
    );
    let compilation = compilation(&text, b"model source");
    let fingerprint = |text: &str| {
        let report = provenance::parse(text).unwrap();
        let mut capture = compilation.clone();
        capture.trace = text.to_owned();
        state::fingerprint(&report.graphs.graphs[0], &report, &capture).unwrap()
    };
    assert_ne!(fingerprint(&text), fingerprint(&ambiguous));
    assert_ne!(fingerprint(&text), fingerprint(&partial));
}
