//! `mwcc-tools run ninja`'s `-n` and `-t` tools, as ninja's, on a small project whose commands are processes.
use std::{
    fs,
    path::Path,
    process::Command,
    time::{Duration, SystemTime},
};

/// A project: `a.c` compiled to `a.o`, linked to `main.elf`, and `all` (the default) over it.
fn project() -> tempfile::TempDir {
    let copy = if cfg!(windows) {
        "cmd /c type $in > $out"
    } else {
        "cat $in > $out"
    };
    let dir = tempfile::tempdir().unwrap();
    fs::write(
        dir.path().join("build.ninja"),
        format!(
            "rule cc\n  command = {copy}\n  description = CC $out\n\
             rule link\n  command = {copy}\n  description = LINK $out\n\
             build a.o: cc a.c\n\
             build main.elf: link a.o\n\
             build all: phony main.elf\n\
             default all\n"
        ),
    )
    .unwrap();
    fs::write(dir.path().join("a.c"), "int a;\n").unwrap();
    dir
}

/// `mwcc-tools run ninja ARGS` in DIR: its exit code and its standard output. (`-n` and `-t` need no mwcc checkout;
/// a build does, and DIR is one without modules: every command runs as a process.)
fn ninja(dir: &Path, args: &[&str]) -> (i32, String) {
    let mut command = Command::new(env!("CARGO_BIN_EXE_mwcc-tools"));
    if args.iter().any(|arg| matches!(*arg, "-n" | "-t")) {
        command.env_remove("MWCC_ORACLE_ROOT");
    } else {
        command.env("MWCC_ORACLE_ROOT", dir);
    }
    let output = command
        .args(["run", "ninja"])
        .args(args)
        .current_dir(dir)
        .output()
        .unwrap();
    (
        output.status.code().unwrap(),
        String::from_utf8(output.stdout).unwrap(),
    )
}

#[test]
fn a_dry_run_shows_what_would_run_and_runs_nothing() {
    let dir = project();
    let (code, out) = ninja(dir.path(), &["-n"]);
    assert_eq!(code, 0);
    let lines: Vec<&str> = out.lines().collect();
    assert_eq!(lines.len(), 2, "{out}");
    assert!(lines[0].starts_with("[1/") && lines[0].ends_with("] CC a.o"));
    assert!(lines[1].starts_with("[2/") && lines[1].ends_with("] LINK main.elf"));
    assert!(!dir.path().join("a.o").exists());
    assert!(!dir.path().join(".n2_db").exists());

    assert_eq!(ninja(dir.path(), &[]).0, 0);
    let built = fs::metadata(dir.path().join("a.o"))
        .unwrap()
        .modified()
        .unwrap();
    let (code, out) = ninja(dir.path(), &["-n"]);
    assert_eq!((code, out.trim_end()), (0, "ninja: no work to do."));

    // (a.c changed: a.o would be compiled, so main.elf linked, though a.o on the disk is as it was)
    let later = SystemTime::now() + Duration::from_secs(3600);
    let source = fs::File::options()
        .append(true)
        .open(dir.path().join("a.c"))
        .unwrap();
    source.set_modified(later).unwrap();
    let (code, out) = ninja(dir.path(), &["-n", "-v"]);
    assert_eq!(code, 0);
    let lines: Vec<&str> = out.lines().collect();
    assert_eq!(lines.len(), 2, "{out}");
    assert!(lines[0].contains("a.c > a.o"), "{out}");
    assert!(lines[1].contains("a.o > main.elf"), "{out}");
    let after = fs::metadata(dir.path().join("a.o"))
        .unwrap()
        .modified()
        .unwrap();
    assert_eq!(after, built);
}

#[test]
fn the_commands_of_a_target_are_those_of_its_inputs_then_its_own() {
    let dir = project();
    let copy = |input: &str, output: &str| {
        if cfg!(windows) {
            format!("cmd /c type {input} > {output}")
        } else {
            format!("cat {input} > {output}")
        }
    };
    // (ninja's lines end as its C runtime ends them)
    let end = if cfg!(windows) { "\r\n" } else { "\n" };
    let chain = format!(
        "{}{end}{}{end}",
        copy("a.c", "a.o"),
        copy("a.o", "main.elf")
    );
    assert_eq!(ninja(dir.path(), &["-t", "commands"]), (0, chain.clone()));
    assert_eq!(
        ninja(dir.path(), &["-t", "commands", "main.elf"]),
        (0, chain)
    );
    assert_eq!(
        ninja(dir.path(), &["-t", "commands", "-s", "main.elf"]),
        (0, format!("{}{end}", copy("a.o", "main.elf")))
    );
    assert_eq!(
        ninja(dir.path(), &["-t", "commands", "a.c^"]),
        (0, format!("{}{end}", copy("a.c", "a.o")))
    );
    assert_eq!(ninja(dir.path(), &["-t", "commands", "nothing"]).0, 1);
    // (nothing built, nor .n2_db made)
    assert!(!dir.path().join("a.o").exists());
    assert!(!dir.path().join(".n2_db").exists());
}

#[test]
fn a_dry_run_stops_where_build_ninja_would_be_regenerated() {
    let copy = |input: &str, output: &str| {
        if cfg!(windows) {
            format!("cmd /c type {input} > {output}")
        } else {
            format!("cat {input} > {output}")
        }
    };
    // (dtk-template's: a split step's output is an input of the configure step, which writes build.ninja)
    let manifest = format!(
        "rule split\n  command = {}\n  description = SPLIT $out\n\
         rule configure\n  command = {}\n  generator = 1\n  description = RUN configure.py\n\
         rule cc\n  command = {}\n  description = CC $out\n\
         build config.json: split config.yml\n\
         build build.ninja: configure configure.py | config.json\n\
         build a.o: cc a.c\n",
        copy("$in", "$out"),
        copy("build.ninja.in", "build.ninja"),
        copy("$in", "$out"),
    );
    let dir = tempfile::tempdir().unwrap();
    for (name, text) in [
        ("build.ninja", manifest.as_str()),
        ("build.ninja.in", manifest.as_str()),
        ("config.yml", "x\n"),
        ("configure.py", ""),
        ("a.c", "int a;\n"),
    ] {
        fs::write(dir.path().join(name), text).unwrap();
    }
    assert_eq!(ninja(dir.path(), &[]).0, 0);
    assert_eq!(ninja(dir.path(), &["-n"]).1.trim(), "ninja: no work to do.");

    // (config.json would be split again, so build.ninja regenerated: the dry run ends there, as ninja's)
    let later = SystemTime::now() + Duration::from_secs(3600);
    fs::File::options()
        .append(true)
        .open(dir.path().join("config.yml"))
        .unwrap()
        .set_modified(later)
        .unwrap();
    let (code, out) = ninja(dir.path(), &["-n"]);
    assert_eq!(code, 0);
    let steps: Vec<&str> = out
        .lines()
        .map(|line| line.split_once("] ").map_or(line, |(_, step)| step))
        .collect();
    assert_eq!(steps, ["SPLIT config.json", "RUN configure.py"], "{out}");
}

#[test]
fn the_inputs_and_targets_are_ninjas() {
    let dir = project();
    let end = if cfg!(windows) { "\r\n" } else { "\n" };
    // (a.o is an input of main.elf; all is phony, and main.elf its input)
    assert_eq!(
        ninja(dir.path(), &["-t", "inputs", "main.elf"]),
        (0, format!("a.c{end}a.o{end}"))
    );
    assert_eq!(
        ninja(dir.path(), &["-t", "inputs"]),
        (0, format!("a.c{end}a.o{end}main.elf{end}"))
    );
    assert_eq!(
        ninja(dir.path(), &["-t", "inputs", "-0", "main.elf"]),
        (0, "a.c\0a.o\0".to_owned())
    );
    assert_eq!(
        ninja(dir.path(), &["-t", "targets", "all"]),
        (0, format!("a.o: cc{end}main.elf: link{end}all: phony{end}"))
    );
    assert_eq!(ninja(dir.path(), &["-t", "inputs", "nothing"]).0, 1);
}

/// The `--json` file of `run ninja ARGS` in DIR (written at DIR/out.json), and its exit code.
fn json(dir: &Path, args: &[&str]) -> (serde_json::Value, i32) {
    let mut all = vec!["--json", "out.json"];
    all.extend(args);
    let (code, _) = ninja(dir, &all);
    let text = fs::read_to_string(dir.join("out.json")).unwrap();
    (serde_json::from_str(&text).unwrap(), code)
}

#[test]
fn json_tells_what_the_build_did_and_each_failure_with_its_own_output() {
    let dir = project();
    let (report, code) = json(dir.path(), &["-n"]);
    assert_eq!(code, 0);
    assert_eq!(
        (&report["result"], &report["dry_run"], &report["tasks_run"]),
        (&"succeeded".into(), &true.into(), &2.into())
    );
    let (report, code) = json(dir.path(), &[]);
    assert_eq!(code, 0);
    assert_eq!(
        (&report["result"], &report["dry_run"], &report["tasks_run"]),
        (&"succeeded".into(), &false.into(), &2.into())
    );
    assert_eq!(report["failures"], serde_json::json!([]));
    assert!(report["error"].is_null());

    let (report, code) = json(dir.path(), &["nothing"]);
    assert_eq!((&report["result"], code), (&"error".into(), 1));
    assert!(report["error"].as_str().unwrap().contains("unknown target"));

    // (two that fail, one that does not: each failure keeps only its own output)
    let (fail, copy) = if cfg!(windows) {
        ("cmd /c echo oops $out& exit 1", "cmd /c type $in > $out")
    } else {
        ("echo oops $out; exit 1", "cat $in > $out")
    };
    fs::write(
        dir.path().join("build.ninja"),
        format!(
            "rule fail\n  command = {fail}\n  description = FAIL $out\n\
             rule cc\n  command = {copy}\n\
             build x.o: fail a.c\nbuild y.o: fail a.c\nbuild z.o: cc a.c\n"
        ),
    )
    .unwrap();
    let (report, code) = json(dir.path(), &["-k", "0", "-j", "1"]);
    assert_eq!(code, 1);
    assert_eq!(
        (&report["result"], &report["tasks_run"]),
        (&"failed".into(), &1.into())
    );
    let failures = report["failures"].as_array().unwrap();
    assert_eq!(failures.len(), 2, "{report}");
    let mut outputs: Vec<&str> = failures
        .iter()
        .map(|f| f["outputs"][0].as_str().unwrap())
        .collect();
    outputs.sort();
    assert_eq!(outputs, ["x.o", "y.o"]);
    for failure in failures {
        let out = failure["outputs"][0].as_str().unwrap();
        assert_eq!(failure["description"], format!("FAIL {out}"));
        assert!(failure["command"].as_str().unwrap().contains("oops"));
        assert_eq!(
            failure["output"].as_str().unwrap().trim(),
            format!("oops {out}")
        );
        assert_eq!(failure["interrupted"], false);
    }

    // (FILE is where the build began, whatever -C says)
    let parent = tempfile::tempdir().unwrap();
    let project = parent.path().join("p");
    fs::create_dir(&project).unwrap();
    fs::copy(dir.path().join("a.c"), project.join("a.c")).unwrap();
    fs::write(
        project.join("build.ninja"),
        "rule cc\n  command = x\nbuild z.o: phony a.c\n",
    )
    .unwrap();
    let (report, code) = json(parent.path(), &["-C", "p"]);
    assert_eq!((&report["result"], code), (&"succeeded".into(), 0));
}
