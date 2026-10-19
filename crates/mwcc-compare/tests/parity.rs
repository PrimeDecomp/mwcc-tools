//! Scores equal objdiff-cli's and verdicts residual-scan's, on 20 units of an Echoes checkout (MWCC_TEST_ECHOES, with
//! its built objects, report.json and objdiff-cli; skipped without it): five matched and five nonmatching DOL units,
//! and as many REL units. Every target function's match, row count and both sides' diff kinds equal
//! `objdiff-cli diff -c functionRelocDiffs=data_value -u UNIT --format json`'s, and the residual rows and verdicts
//! equal `residual-scan.py --json`'s. Each unit's fuzzy match, scored as objdiff's report scores it, equals report.json's.
use mwcc_compare::{
    ScoreOptions, score,
    score::{diff_config, report_config, score_with},
};
use mwcc_project::Project;
use serde_json::Value;
use std::{fs, path::Path, process::Command};

fn units(project: &Project) -> Vec<String> {
    let report = project
        .report()
        .unwrap()
        .expect("report.json (ninja build/<version>/report.json)");
    let mut picked = vec![];
    for rel in [false, true] {
        for matched in [true, false] {
            picked.extend(
                report["units"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .filter(|u| u["name"].as_str().unwrap().starts_with("main/") != rel)
                    .filter(|u| {
                        let code = u["measures"]["total_code"]
                            .as_str()
                            .and_then(|c| c.parse::<u64>().ok());
                        let fuzzy = u["measures"]["fuzzy_match_percent"].as_f64().unwrap_or(0.0);
                        code.unwrap_or(0) > 0
                            && (fuzzy >= 100.0) == matched
                            && (matched || fuzzy > 0.0)
                    })
                    .filter_map(|u| u["name"].as_str().map(String::from))
                    .filter(|name| {
                        let unit = project.unit(name);
                        unit.is_ok_and(|u| {
                            u.base_path.is_file() && u.target_path.is_some_and(|t| t.is_file())
                        })
                    })
                    .take(5),
            );
        }
    }
    picked
}

fn kinds(rows: &Value) -> Vec<String> {
    rows.as_array()
        .map(|rows| {
            rows.iter()
                .map(|r| r["diff_kind"].as_str().unwrap_or("DIFF_NONE").to_owned())
                .collect()
        })
        .unwrap_or_default()
}

#[test]
fn scores_and_verdicts_equal_objdiff_cli_and_residual_scan() {
    let Some(root) = std::env::var_os("MWCC_TEST_ECHOES") else {
        eprintln!("(skipped: MWCC_TEST_ECHOES is not set)");
        return;
    };
    let root = Path::new(&root);
    let project = Project::open(root).unwrap();
    let cli = root.join(if cfg!(windows) {
        "build/tools/objdiff-cli.exe"
    } else {
        "build/tools/objdiff-cli"
    });
    let names = units(&project);
    assert_eq!(names.len(), 20, "{names:?}");
    let mut differences = vec![];
    let (mut compared, mut nonmatching, mut scanned) = (0, 0, 0);
    for name in &names {
        let unit = project.unit(name).unwrap();
        let target = fs::read(unit.target_path.as_ref().unwrap()).unwrap();
        let ours = fs::read(&unit.base_path).unwrap();
        let scored = score(
            &target,
            &ours,
            &ScoreOptions {
                rows: true,
                ..ScoreOptions::default()
            },
        )
        .unwrap();
        let output = Command::new(&cli)
            .args([
                "diff",
                "-c",
                "functionRelocDiffs=data_value",
                "-u",
                name,
                "-o",
                "-",
                "--format",
                "json",
            ])
            .current_dir(root)
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        );
        let theirs: Value = serde_json::from_slice(&output.stdout).unwrap();
        // (objdiff-cli's document itself, made in process)
        let options = ScoreOptions::default();
        let document = mwcc_compare::score::Target::new(&target, diff_config(&options).unwrap())
            .unwrap()
            .diff_document(&ours, &options)
            .unwrap();
        if document != theirs {
            differences.push(format!(
                "{name}: the diff document differs from objdiff-cli's"
            ));
        }
        // (the unit's fuzzy match, as report.json has it: objdiff's report compares relocations by nothing)
        let reported =
            project.report_unit(name).unwrap().unwrap()["measures"]["fuzzy_match_percent"].as_f64();
        let fuzzy = score_with(&target, &ours, &report_config(), &ScoreOptions::default())
            .unwrap()
            .fuzzy;
        if fuzzy.map(|f| (f * 1e3).round()) != reported.map(|f| (f * 1e3).round()) {
            differences.push(format!(
                "{name}: fuzzy ours {fuzzy:?}, report.json {reported:?}"
            ));
        }
        let (left, right) = (&theirs["left"]["symbols"], &theirs["right"]["symbols"]);
        let functions: Vec<&Value> = left
            .as_array()
            .unwrap()
            .iter()
            .filter(|s| {
                s["kind"] == "SYMBOL_FUNCTION" && s["size"].as_str().is_some_and(|z| z != "0")
            })
            .collect();
        assert_eq!(functions.len(), scored.functions.len(), "{name}");
        for (ours, theirs) in scored.functions.iter().zip(functions) {
            assert_eq!(ours.name, theirs["name"].as_str().unwrap(), "{name}");
            let other = theirs["target_symbol"].as_u64().map(|i| &right[i as usize]);
            let percent = other.and(theirs["match_percent"].as_f64());
            let same = ours.match_percent.map(f64::from).map(|p| (p * 1e3).round())
                == percent.map(|p| (p * 1e3).round())
                && ours.rows.len() == theirs["instructions"].as_array().map_or(0, Vec::len)
                && ours
                    .rows
                    .iter()
                    .map(|r| r.target.diff_kind.cli_name())
                    .collect::<Vec<_>>()
                    == kinds(&theirs["instructions"])
                && other.is_none_or(|o| {
                    ours.rows
                        .iter()
                        .map(|r| r.ours.diff_kind.cli_name())
                        .collect::<Vec<_>>()
                        == kinds(&o["instructions"])
                });
            compared += 1;
            nonmatching += usize::from(ours.match_percent.is_none_or(|p| p < 100.0));
            if !same {
                differences.push(format!(
                    "{name} {}: ours {:?}, objdiff-cli {percent:?}",
                    ours.name, ours.match_percent
                ));
            }
        }
        // (residual-scan, with the same objdiff-cli, on the objects as built)
        let scan = Command::new("python")
            .args(["scripts/residual-scan.py", "-u", name, "--json"])
            .env("DECOMP_NO_REBUILD", "1")
            .env("OBJDIFF_CLI", &cli)
            .current_dir(root)
            .output()
            .unwrap();
        assert!(
            scan.status.success(),
            "{}",
            String::from_utf8_lossy(&scan.stderr)
        );
        let rows: Vec<Value> = serde_json::from_slice(&scan.stdout).unwrap();
        for row in rows {
            scanned += 1;
            let function = row["function"].as_str().unwrap();
            let ours = scored
                .functions
                .iter()
                .find(|f| f.name == function)
                .unwrap();
            let verdict = ours.residual.verdict.map(|v| v.label());
            let counts = [
                ours.residual.register,
                ours.residual.stack,
                ours.residual.other,
            ];
            let theirs = ["register", "stack", "other"].map(|k| row[k].as_u64().unwrap() as u32);
            if verdict != row["verdict"].as_str()
                || (verdict != Some("missing") && counts != theirs)
            {
                differences.push(format!(
                    "{name} {function}: residual ours {verdict:?} {counts:?}, residual-scan {row}"
                ));
            }
        }
    }
    eprintln!(
        "{compared} functions ({nonmatching} not matching), {scanned} residual-scan rows, of {names:?}"
    );
    assert!(nonmatching > 0 && scanned > 0);
    assert!(
        differences.is_empty(),
        "{} differences:\n{}",
        differences.len(),
        differences.join("\n")
    );
}
