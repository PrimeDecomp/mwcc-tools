//! `mwcc-tools run ninja`'s `-n` and `-t commands`, as ninja's, on a small project whose commands are processes.
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
    assert_eq!(
        (code, out.trim_end()),
        (0, "mwcc-tools run ninja: no work to do.")
    );

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
    assert!(ninja(dir.path(), &["-n"]).1.contains("no work to do"));

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
