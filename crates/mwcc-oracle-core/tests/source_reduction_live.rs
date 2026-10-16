//! Compiler-backed reduction proves the mechanism, not source equivalence.
use mwcc_oracle_core::{
    evidence::EvaluationState,
    runner::TraceOptions,
    source_guided::{
        objective::{Contract, Objective, Purpose, RequiredRelocation, Selection},
        reduction::{self, Plan},
        search::{Budget, Build, Edit, Transformation},
    },
    version::Version,
};
use std::{
    env, fs,
    path::{Path, PathBuf},
};

fn budget(output: &Path, candidates: usize, compilations: usize) -> Budget {
    Budget {
        candidates,
        compilations,
        seconds: 600,
        cancel_file: output.join("CANCEL"),
    }
}
#[test]
#[ignore = "requires MWCC_SOURCE_REDUCTION_TEST_ROOT with all three current backend producers; MWCC_SOURCE_REDUCTION_KEEP optionally retains proofs"]
fn retail_reduction_preserves_symbolic_creation_with_cache_resume_and_replay() {
    let oracle = PathBuf::from(
        env::var_os("MWCC_SOURCE_REDUCTION_TEST_ROOT")
            .expect("set MWCC_SOURCE_REDUCTION_TEST_ROOT"),
    );
    let scratch = if let Some(root) = env::var_os("MWCC_SOURCE_REDUCTION_KEEP") {
        fs::create_dir_all(&root).unwrap();
        tempfile::Builder::new()
            .prefix("source-reduction-")
            .tempdir_in(root)
            .unwrap()
            .keep()
    } else {
        tempfile::tempdir().unwrap().keep()
    };
    println!("retained source-reduction proofs: {}", scratch.display());
    for version in [Version::Gc13, Version::Gc132, Version::Gc27] {
        let directory = scratch.join(version.name());
        fs::create_dir_all(&directory).unwrap();
        let fixture = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/backend/symbolic_address.cpp");
        let original = fs::read_to_string(fixture).unwrap();
        let extra = "\n// This helper is unrelated to the symbolic argument mechanism.\nstatic unsigned unrelated(unsigned x) { return x + 42; }\n";
        let source = format!("{original}{extra}");
        fs::write(directory.join("case.cpp"), &source).unwrap();
        fs::write(
            directory.join("candidate-only.h"),
            "// candidate dependency\n",
        )
        .unwrap();
        let mut plan = Plan {
            format_version: 1,
            build: Build {
                oracle_root: oracle.clone(),
                working_directory: directory.clone(),
                source: "case.cpp".into(),
                version: Some(version),
                flags: ["-nodefaults", "-proc", "gekko", "-fp", "hardware", "-O4,p"]
                    .map(str::to_owned)
                    .into(),
                from_ninja: None,
                traces: TraceOptions {
                    backend: true,
                    stages: true,
                    locals: true,
                    cse: true,
                    ..TraceOptions::default()
                },
                timeout_seconds: 60,
            },
            contract: Contract {
                format_version: 1,
                selection: Selection {
                    link: "symbolic_address__FUi".into(),
                    class: 4,
                    round: 0,
                },
                objective: Objective::SymbolAddressPairs {
                    symbol: "symbol__8Graphics".into(),
                    minimum: 1,
                    maximum: 10,
                },
                relocations: vec![RequiredRelocation {
                    symbol: "symbol__8Graphics".into(),
                    kind: None,
                    addend: Some(0),
                    minimum: 1,
                }],
                purpose: Purpose::MechanismReproducer,
            },
            reductions: vec![Transformation {
                id: "remove-unrelated-helper".into(),
                family: "helper".into(),
                precondition: "Fixture helper is unrelated; the shared predicate must still pass."
                    .into(),
                edits: vec![Edit {
                    find: extra.into(),
                    replacement: "\n#include \"candidate-only.h\"\n".into(),
                }],
            }],
            delete_lines: false,
            semantic_check: None,
            target: None,
        };
        let output = directory.join("run");
        // Setup + first candidate fit; independent compile must remain pending.
        let pending = reduction::run(&plan, &output, &budget(&output, 10, 3), false).unwrap();
        assert_eq!(pending.status, "compilation_budget");
        assert_eq!(pending.best.id, 0);
        assert_eq!(pending.pending.as_ref().unwrap().id, 1);
        assert!(pending.outcomes.is_empty());
        let completed = reduction::run(&plan, &output, &budget(&output, 10, 1), true).unwrap();
        assert_eq!(completed.status, "verified_reduction");
        assert_eq!(completed.compilations, 4);
        assert_eq!(completed.best.id, 1);
        assert!(completed.best.bytes < source.len());
        assert!(completed.outcomes[0].compilation_cache_hit);
        assert_eq!(
            completed.outcomes[0]
                .evaluation
                .as_ref()
                .unwrap()
                .overall
                .state,
            EvaluationState::Satisfied
        );
        assert_eq!(
            completed.outcomes[0].verification.as_ref().unwrap()["independent"]["satisfied"],
            true
        );
        let replay = reduction::replay(&output, 1, &budget(&output, 1, 3)).unwrap();
        assert_eq!(replay.status, "replay_satisfied");
        assert_eq!(
            replay.verification.as_ref().unwrap()["fresh_compilations"],
            3
        );
        fs::write(directory.join("candidate-only.h"), "// dependency drift\n").unwrap();
        let drift = reduction::replay(&output, 1, &budget(&output, 1, 3)).unwrap_err();
        assert!(format!("{drift:#}").contains("dependency changed"));
        fs::write(
            directory.join("candidate-only.h"),
            "// candidate dependency\n",
        )
        .unwrap();
        // Automatic line deletion shrinks further without supplying another
        // source recipe. Incomplete or missing selected graphs cannot pass.
        let mut automatic = plan.clone();
        automatic.reductions.clear();
        automatic.delete_lines = true;
        let automatic_output = directory.join("automatic-lines");
        let automatic_result = reduction::run(
            &automatic,
            &automatic_output,
            &budget(&automatic_output, 300, 300),
            false,
        )
        .unwrap();
        assert_eq!(automatic_result.status, "verified_reduction");
        assert!(automatic_result.best.bytes < completed.best.bytes);
        assert!(
            automatic_result
                .outcomes
                .iter()
                .any(|o| o.status == "accepted")
        );
        assert!(automatic_result.outcomes.iter().all(|o| {
            o.status != "accepted"
                || o.evaluation
                    .as_ref()
                    .is_some_and(|e| e.overall.state == EvaluationState::Satisfied)
        }));
        // Reproducer output cannot become a checked source transformation.
        plan.contract.purpose = Purpose::SourceTransformation;
        let missing = directory.join("missing-semantic-check");
        let rejected = reduction::run(&plan, &missing, &budget(&missing, 10, 10), false).unwrap();
        assert_eq!(rejected.status, "baseline_not_satisfied");
        assert!(rejected.outcomes.is_empty());
        assert_eq!(
            rejected.baseline_evaluation.unwrap().constraints["semantic_check"].state,
            EvaluationState::Indeterminate
        );
        // Cancellation leaves its baseline/candidate cursor replayable.
        let cancelled = directory.join("cancelled");
        fs::create_dir_all(&cancelled).unwrap();
        fs::write(cancelled.join("CANCEL"), b"stop").unwrap();
        let stopped =
            reduction::run(&plan, &cancelled, &budget(&cancelled, 10, 10), false).unwrap();
        assert_eq!(stopped.status, "cancelled");
        fs::remove_file(cancelled.join("CANCEL")).unwrap();
        plan.contract.purpose = Purpose::MechanismReproducer;
        // Exact plan identity is enforced, including its purpose.
        assert!(reduction::run(&plan, &cancelled, &budget(&cancelled, 10, 10), true).is_err());
        println!(
            "{}: smaller standalone symbolic reproducer, independent compile, resume, replay and hard gates pass",
            version.name()
        );
    }
}
