use mwcc_oracle_core::{
    runner::TraceOptions,
    source_guided::{
        objective::{Contract, Objective, Purpose, Selection},
        reduction::{self, Cursor, Plan},
        search::{Build, Edit, Transformation},
    },
    version::Version,
};
fn plan() -> Plan {
    Plan {
        format_version: 1,
        build: Build {
            oracle_root: ".".into(),
            working_directory: ".".into(),
            source: "test.cpp".into(),
            version: Some(Version::Gc27),
            flags: vec![],
            from_ninja: None,
            traces: TraceOptions {
                backend: true,
                ..TraceOptions::default()
            },
            timeout_seconds: 30,
        },
        contract: Contract {
            format_version: 1,
            selection: Selection {
                link: "test".into(),
                class: 4,
                round: 0,
            },
            objective: Objective::SymbolAddressPairs {
                symbol: "symbol".into(),
                minimum: 1,
                maximum: 1,
            },
            relocations: vec![],
            purpose: Purpose::MechanismReproducer,
        },
        reductions: vec![],
        delete_lines: true,
        semantic_check: None,
        target: None,
    }
}
#[test]
fn reduction_order_is_deterministic_strict_and_byte_preserving() {
    let plan = plan();
    let source = b"first\r\n\xffsecond\nthird";
    let enumerate = || {
        let mut cursor = Cursor {
            recipe: 0,
            chunk_lines: 4,
            start_line: 0,
        };
        let mut output = vec![];
        while let Some((id, bytes)) = reduction::propose(&plan, source, &mut cursor) {
            assert!(bytes.len() < source.len());
            output.push((id, bytes));
        }
        output
    };
    let proposals = enumerate();
    assert_eq!(proposals, enumerate());
    assert_eq!(
        proposals.iter().map(|p| p.0.as_str()).collect::<Vec<_>>(),
        [
            "delete-lines:1..3",
            "delete-lines:1..2",
            "delete-lines:3..3",
            "delete-lines:1..1",
            "delete-lines:2..2",
            "delete-lines:3..3"
        ]
    );
    assert_eq!(proposals[3].1, b"\xffsecond\nthird");
    assert_eq!(proposals[4].1, b"first\r\nthird");
}
#[test]
fn guarded_reductions_skip_missing_ambiguous_and_nondecreasing_recipes() {
    let mut plan = plan();
    plan.delete_lines = false;
    plan.reductions = [
        ("missing", "missing", ""),
        ("ambiguous", "x", ""),
        ("growing", "xx", "xxx"),
        ("smaller", "xx", "x"),
    ]
    .map(|(id, find, replacement)| Transformation {
        id: id.into(),
        family: id.into(),
        precondition: "guarded fixture".into(),
        edits: vec![Edit {
            find: find.into(),
            replacement: replacement.into(),
        }],
    })
    .into();
    let mut cursor = Cursor::default();
    assert_eq!(
        reduction::propose(&plan, b"xx", &mut cursor),
        Some(("smaller".into(), b"x".to_vec()))
    );
    assert!(reduction::propose(&plan, b"xx", &mut cursor).is_none());
    assert!(reduction::validate(&plan).is_ok());
    let mut invalid = serde_json::to_value(&plan).unwrap();
    invalid["unknown"] = true.into();
    assert!(serde_json::from_value::<Plan>(invalid).is_err());
    plan.format_version = 2;
    assert!(reduction::validate(&plan).is_err());
}
#[test]
fn cursor_round_trips_pending_pass_position_without_repeating_candidates() {
    let plan = plan();
    let source = b"a\nb\nc\nd\n";
    let mut cursor = Cursor {
        recipe: 0,
        chunk_lines: 4,
        start_line: 0,
    };
    reduction::propose(&plan, source, &mut cursor).unwrap();
    let mut resumed: Cursor =
        serde_json::from_slice(&serde_json::to_vec(&cursor).unwrap()).unwrap();
    assert_eq!(
        reduction::propose(&plan, source, &mut cursor),
        reduction::propose(&plan, source, &mut resumed)
    );
    assert_eq!(cursor.start_line, resumed.start_line);
    let mut empty = Cursor {
        recipe: 0,
        chunk_lines: 1,
        start_line: 0,
    };
    assert!(reduction::propose(&plan, b"", &mut empty).is_none());
}
