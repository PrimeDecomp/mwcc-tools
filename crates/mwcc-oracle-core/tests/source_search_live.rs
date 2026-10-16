//! Compiler-backed mechanism experiments. The zero-initializer experiment
//! deliberately changes behavior; its result must remain mechanism-only.
use mwcc_oracle_core::{
    allocation_category::Category,
    evidence::EvaluationState,
    runner::TraceOptions,
    source_guided::{
        objective::{Contract, ObjectAnchor, Objective, Purpose, RequiredRelocation, Selection},
        search::{self, Budget, Build, Edit, Plan, Transformation},
    },
    value_history::ObjectPart,
    version::Version,
};
use std::{
    env, fs,
    path::{Path, PathBuf},
};
fn anchor() -> ObjectAnchor {
    ObjectAnchor {
        name: "flag".into(),
        part: ObjectPart::Single,
        type_kind: 1,
        type_size: 4,
    }
}
fn transform(id: &str, family: &str, find: &str, replacement: &str) -> Transformation {
    Transformation {
        id: id.into(),
        family: family.into(),
        precondition:
            "Mechanism fixture; no semantic equivalence is claimed for changing the initializer."
                .into(),
        edits: vec![Edit {
            find: find.into(),
            replacement: replacement.into(),
        }],
    }
}
fn plan(oracle: &Path, directory: &Path, version: Version) -> Plan {
    Plan {
        format_version: 1,
        build: Build {
            oracle_root: oracle.into(),
            working_directory: directory.into(),
            source: "initializer.c".into(),
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
                link: "source_initializer".into(),
                class: 4,
                round: 0,
            },
            objective: Objective::ChangeRangeCategory {
                value: anchor(),
                target: Category::InsideCoalescingWindow,
            },
            relocations: vec![RequiredRelocation {
                symbol: "observe".into(),
                kind: None,
                addend: None,
                minimum: 1,
            }],
            purpose: Purpose::MechanismReproducer,
        },
        transformations: vec![
            transform(
                "parenthesize",
                "initialization",
                "observe(input)",
                "(observe(input))",
            ),
            transform(
                "spacing",
                "formatting",
                "unsigned flag =",
                "unsigned flag  =",
            ),
            transform(
                "parenthesize-alias",
                "initialization",
                "observe(input)",
                "(observe(input))",
            ),
        ],
        maximum_depth: 2,
        generation_limit: 20,
        semantic_check: None,
        target: None,
    }
}
fn budget(output: &Path, candidates: usize, compilations: usize) -> Budget {
    Budget {
        candidates,
        compilations,
        seconds: 300,
        cancel_file: output.join("CANCEL"),
    }
}
#[test]
#[ignore = "requires MWCC_SOURCE_SEARCH_TEST_ROOT with all three current backend producers; optionally MWCC_SOURCE_SEARCH_KEEP retains proofs"]
fn retail_source_search_cache_resume_independent_replay_and_evidence_gates() {
    let oracle = PathBuf::from(
        env::var_os("MWCC_SOURCE_SEARCH_TEST_ROOT").expect("set MWCC_SOURCE_SEARCH_TEST_ROOT"),
    );
    let scratch = if let Some(root) = env::var_os("MWCC_SOURCE_SEARCH_KEEP") {
        let root = PathBuf::from(root);
        fs::create_dir_all(&root).unwrap();
        tempfile::Builder::new()
            .prefix("source-search-")
            .tempdir_in(root)
            .unwrap()
            .keep()
    } else {
        tempfile::Builder::new()
            .prefix("mwcc-source-search-")
            .tempdir()
            .unwrap()
            .keep()
    };
    println!("retained source-search proofs: {}", scratch.display());
    for version in [Version::Gc13, Version::Gc132, Version::Gc27] {
        let directory = scratch.join(version.name());
        fs::create_dir_all(&directory).unwrap();
        let fixtures =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/source-guided");
        for name in ["initializer.c", "initializer-linear.c", "input.h"] {
            fs::copy(fixtures.join(name), directory.join(name)).unwrap();
        }
        let mut plan = plan(&oracle, &directory, version);
        let output = directory.join("grouped");
        let first = search::run(&plan, &output, &budget(&output, 1, 2), false).unwrap();
        assert_eq!(first.status, "candidate_budget");
        assert_eq!(first.next, 1);
        let finished = search::run(&plan, &output, &budget(&output, 20, 20), true).unwrap();
        assert_eq!(finished.status, "bounded_search_exhausted");
        assert_eq!(finished.outcomes.len(), 8);
        assert_eq!(finished.compilations, 5);
        assert!(finished.cache_hits >= 4);
        assert_eq!(finished.groups.len(), 1);
        assert!(finished.outcomes.iter().all(|o| {
            o.evaluation
                .as_ref()
                .is_some_and(|e| e.overall.state == EvaluationState::Unsatisfied)
        }));
        // A changed header must invalidate the input cache even when only its
        // comments changed, leaving preprocessed instructions unchanged.
        fs::write(
            directory.join("input.h"),
            "#define SOURCE_GUIDED_OFFSET 0\n// changed raw dependency\n",
        )
        .unwrap();
        let changed = search::run(&plan, &output, &budget(&output, 20, 20), true).unwrap_err();
        assert!(format!("{changed:#}").contains("inputs changed"));
        fs::copy(fixtures.join("input.h"), directory.join("input.h")).unwrap();
        println!(
            "{}: compiler-state grouping, exact cache reuse, resume and header invalidation pass",
            version.name()
        );

        // Keep the branching fixture above for cache/grouping coverage. The
        // initializer recognizer supports one instruction-bearing block with
        // linear empty scaffolding, so positive/replay gates use this fixture.
        plan.build.source = "initializer-linear.c".into();
        plan.contract.objective = Objective::DirectInitialization {
            value: anchor(),
            constant: 0,
            source_line: None,
        };
        plan.transformations = vec![transform(
            "zero-initializer",
            "mechanism-probe",
            "unsigned flag = input;",
            "\n#include \"candidate-only.h\"\n    unsigned flag = 0;",
        )];
        fs::write(
            directory.join("candidate-only.h"),
            "// candidate-only dependency\n",
        )
        .unwrap();
        plan.maximum_depth = 1;
        let output = directory.join("mechanism");
        let pending = search::run(&plan, &output, &budget(&output, 10, 3), false).unwrap();
        assert_eq!(pending.status, "compilation_budget");
        assert_eq!(pending.next, 1);
        let completed = search::run(&plan, &output, &budget(&output, 10, 1), true).unwrap();
        assert_eq!(completed.status, "verified_success");
        assert_eq!(completed.compilations, 4);
        let winner = completed.outcomes.last().unwrap();
        assert_eq!(winner.id, 1);
        assert!(winner.compilation_cache_hit);
        assert_eq!(
            winner.independent_verification.as_ref().unwrap()["satisfied"],
            true
        );
        assert_eq!(
            winner.evaluation.as_ref().unwrap().semantic_validation,
            "not_checked_mechanism_only"
        );
        let replay = search::replay(&output, 1, &budget(&output, 1, 3)).unwrap();
        assert_eq!(replay.status, "replay_satisfied");
        assert_eq!(
            replay.independent_verification.as_ref().unwrap()["fresh_compilations"],
            3
        );
        fs::write(
            directory.join("candidate-only.h"),
            "// changed candidate-only dependency\n",
        )
        .unwrap();
        let changed = search::replay(&output, 1, &budget(&output, 1, 3)).unwrap_err();
        assert!(format!("{changed:#}").contains("dependency changed"));
        fs::write(
            directory.join("candidate-only.h"),
            "// candidate-only dependency\n",
        )
        .unwrap();
        // The same experiment cannot become a claimed source transformation
        // without the additional semantic check required by that purpose.
        plan.contract.purpose = Purpose::SourceTransformation;
        let output = directory.join("missing-semantic-check");
        let blocked = search::run(&plan, &output, &budget(&output, 10, 10), false).unwrap();
        assert_eq!(blocked.status, "bounded_search_exhausted");
        assert!(
            blocked
                .outcomes
                .iter()
                .all(|o| o.status != "verified_success")
        );
        assert_eq!(
            blocked.outcomes[1].evaluation.as_ref().unwrap().constraints["semantic_check"].state,
            EvaluationState::Indeterminate
        );
        println!(
            "{}: independent recompile/replay and semantic-evidence gates pass",
            version.name()
        );

        plan.contract.purpose = Purpose::MechanismReproducer;
        let output = directory.join("cancelled");
        fs::create_dir_all(&output).unwrap();
        fs::write(output.join("CANCEL"), "").unwrap();
        let cancelled = search::run(&plan, &output, &budget(&output, 10, 10), false).unwrap();
        assert_eq!(cancelled.status, "cancelled");
        assert_eq!(cancelled.compilations, 0);
        fs::remove_file(output.join("CANCEL")).unwrap();
        let resumed = search::run(&plan, &output, &budget(&output, 10, 10), true).unwrap();
        assert_eq!(resumed.status, "verified_success");
        // Retained input blobs are part of the artifact, not unverified hints.
        let key = resumed.baseline_artifact.unwrap();
        let entry: serde_json::Value = serde_json::from_slice(
            &fs::read(output.join("cache").join(key).join("capture.json")).unwrap(),
        )
        .unwrap();
        let sha = entry["inputs"]["dependencies"][0]["sha256"]
            .as_str()
            .unwrap();
        fs::write(output.join("input-files").join(sha), b"corrupt").unwrap();
        assert!(search::replay(&output, 1, &budget(&output, 1, 3)).is_err());
        println!(
            "{}: cancellation/resume and retained-input integrity pass",
            version.name()
        );
    }
}
