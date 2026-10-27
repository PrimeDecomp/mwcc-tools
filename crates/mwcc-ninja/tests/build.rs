//! Builds of small build.ninja files, whose commands a fake executor runs (no processes but one test's).
use mwcc_ninja::{Executor, Termination};
use std::{
    fs,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::{Duration, SystemTime},
};

/// (a build changes the directory: one at a time)
static DIRECTORY: Mutex<()> = Mutex::new(());

/// Runs the commands of these tests, and records them:
/// * `write OUT TEXT...`: OUT holds TEXT;
/// * `cc SRC OUT DEPFILE HEADER...`: OUT holds SRC's text, and DEPFILE names SRC and the HEADERs, as mwcc's `-MD`
///   writes it (CRLF, a tab before each continued line);
/// * `regen NEXT`: build.ninja becomes a copy of NEXT;
/// * `rsp RSP OUT`: OUT holds RSP's text;
/// * `fail`: fails;
/// * anything else runs as a process.
#[derive(Default)]
struct Fake {
    ran: Mutex<Vec<String>>,
}

impl Executor for Fake {
    fn execute(
        &self,
        cmdline: &str,
        _outputs: &[std::path::PathBuf],
        output: &mut dyn FnMut(&[u8]),
    ) -> Option<anyhow::Result<Termination>> {
        let words: Vec<&str> = cmdline.split(' ').collect();
        let mut run = || -> anyhow::Result<Termination> {
            match words[0] {
                "write" => write(words[1], &words[2..].join(" ")),
                "cc" => {
                    write(words[2], &fs::read_to_string(words[1])?);
                    let mut d = format!("{}: {}", words[2], words[1]);
                    for header in &words[4..] {
                        d.push_str(&format!(" \\\r\n\t{header}"));
                    }
                    d.push_str(" \r\n");
                    write(words[3], &d);
                }
                // (a new file: a copy would keep NEXT's time)
                "regen" => write("build.ninja", &fs::read_to_string(words[1])?),
                "rsp" => write(words[2], &fs::read_to_string(words[1])?),
                "fail" => {
                    output(b"failed!\n");
                    return Ok(Termination::Failure);
                }
                _ => unreachable!(),
            }
            Ok(Termination::Success)
        };
        if !matches!(words[0], "write" | "cc" | "regen" | "rsp" | "fail") {
            return None;
        }
        self.ran.lock().unwrap().push(cmdline.to_owned());
        Some(run())
    }
}

fn write(path: impl AsRef<Path>, text: &str) {
    let path = path.as_ref();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    fs::write(path, text).unwrap();
}

/// A project directory, its files the TEXTs (`\n` as CRLF where CRLF).
struct Project {
    dir: tempfile::TempDir,
    fake: Arc<Fake>,
}

impl Project {
    fn new(files: &[(&str, &str)], crlf: bool) -> Self {
        let dir = tempfile::tempdir().unwrap();
        for (name, text) in files {
            let text = if crlf {
                text.replace('\n', "\r\n")
            } else {
                text.to_string()
            };
            write(dir.path().join(name), &text);
        }
        let project = Project {
            dir,
            fake: Arc::default(),
        };
        // (everything there a while ago, so a touch is newer by far)
        project.age();
        project
    }

    fn path(&self, name: &str) -> PathBuf {
        self.dir.path().join(name)
    }

    /// The commands a build with ARGS ran, and its exit code.
    fn build(&self, args: &[&str]) -> (Vec<String>, i32) {
        let (ran, code) = self.try_build(args);
        (ran, code.unwrap())
    }

    fn try_build(&self, args: &[&str]) -> (Vec<String>, anyhow::Result<i32>) {
        let _one = DIRECTORY.lock().unwrap_or_else(|e| e.into_inner());
        let back = std::env::current_dir().unwrap();
        self.fake.ran.lock().unwrap().clear();
        let mut argv = vec!["-C".to_owned(), self.dir.path().display().to_string()];
        argv.extend(args.iter().map(|a| a.to_string()));
        let code = mwcc_ninja::run("test", argv, Some(self.fake.clone()));
        std::env::set_current_dir(back).unwrap();
        (self.fake.ran.lock().unwrap().clone(), code)
    }

    /// Every file's time set an hour back.
    fn age(&self) {
        let then = SystemTime::now() - Duration::from_secs(3600);
        let mut dirs = vec![self.dir.path().to_path_buf()];
        while let Some(dir) = dirs.pop() {
            for entry in fs::read_dir(dir).unwrap() {
                let path = entry.unwrap().path();
                if path.is_dir() {
                    dirs.push(path);
                } else {
                    let file = fs::File::options().append(true).open(&path).unwrap();
                    file.set_modified(then).unwrap();
                }
            }
        }
    }

    /// NAME changed (a line added), after the last build's files. (The file system gives it its time: on Windows
    /// its clock is coarser than `SystemTime::now`, which could stamp it after the files the next build writes.)
    fn touch(&self, name: &str) {
        use std::io::Write;
        std::thread::sleep(Duration::from_millis(50));
        let mut file = fs::File::options()
            .append(true)
            .open(self.path(name))
            .unwrap();
        file.write_all(b"\n").unwrap();
    }
}

#[test]
fn an_mwcc_depfile_gives_the_headers_and_is_removed() {
    let project = Project::new(
        &[
            (
                "build.ninja",
                "rule cc\n  command = cc $in $out $out.d inc/a.h\n  depfile = $out.d\n  deps = gcc\n\
                 build obj/x.o: cc src/x.c\n",
            ),
            ("src/x.c", "int x;\n"),
            ("inc/a.h", "\n"),
        ],
        true,
    );
    let (ran, code) = project.build(&[]);
    assert_eq!((ran.len(), code), (1, 0));
    assert!(project.path("obj/x.o").is_file());
    // (ninja's deps = gcc: read into the database, then removed)
    assert!(!project.path("obj/x.o.d").exists());
    assert_eq!(project.build(&[]), (vec![], 0));
    project.touch("inc/a.h");
    assert_eq!(project.build(&[]).0.len(), 1);
    assert_eq!(project.build(&[]), (vec![], 0));
}

/// As dtk-template's configure: the first build.ninja splits, then configure writes the full one, whose configure
/// step has more inputs.
const FIRST: &str = "rule split
  command = write config.json split
rule configure
  command = regen full.ninja
  generator = 1
build config.json: split
build build.ninja: configure | config.json configure.py
default config.json
";

const FULL: &str = "rule split
  command = write config.json split
rule configure
  command = regen full.ninja
  generator = 1
rule cc
  command = cc $in $out $out.d
  depfile = $out.d
  deps = gcc
build config.json: split
build obj/x.o: cc src/x.c
build build.ninja: configure | config.json configure.py tools/project.py
default obj/x.o
";

fn configure_project() -> Project {
    Project::new(
        &[
            ("build.ninja", FIRST),
            ("full.ninja", FULL),
            ("configure.py", ""),
            ("tools/project.py", ""),
            ("src/x.c", "int x;\n"),
        ],
        true,
    )
}

#[test]
fn build_ninja_is_regenerated_then_the_new_one_built() {
    let project = configure_project();
    let (ran, code) = project.build(&[]);
    assert_eq!(code, 0);
    assert_eq!(
        ran,
        [
            "write config.json split",
            "regen full.ninja",
            "cc src/x.c obj/x.o obj/x.o.d"
        ]
    );
    assert_eq!(project.build(&[]), (vec![], 0));
    // (configure's input changed: regenerated, and nothing else is out of date)
    project.touch("configure.py");
    assert_eq!(project.build(&[]), (vec!["regen full.ninja".to_owned()], 0));
    assert_eq!(project.build(&[]), (vec![], 0));
    project.touch("tools/project.py");
    assert_eq!(project.build(&[]).0, ["regen full.ninja"]);
}

#[test]
fn a_target_is_built_after_the_regeneration() {
    let project = configure_project();
    let (ran, code) = project.build(&["config.json"]);
    assert_eq!(code, 0);
    assert_eq!(ran, ["write config.json split", "regen full.ninja"]);
    assert_eq!(
        project.build(&["obj/x.o"]).0,
        ["cc src/x.c obj/x.o obj/x.o.d"]
    );
    let (ran, code) = project.try_build(&["nothing"]);
    assert!(ran.is_empty());
    assert!(code
        .unwrap_err()
        .to_string()
        .contains("unknown target \"nothing\""));
}

#[test]
fn a_phony_target_is_as_up_to_date_as_its_inputs() {
    let project = Project::new(
        &[
            (
                "build.ninja",
                "rule w\n  command = write $out x\n\
                 build all_source: phony a.c b.c\n\
                 build report.json: w | all_source\n\
                 build always: phony\n\
                 build progress: w | always\n",
            ),
            ("a.c", ""),
            ("b.c", ""),
        ],
        false,
    );
    let (mut ran, _) = project.build(&[]);
    ran.sort();
    assert_eq!(ran, ["write progress x", "write report.json x"]);
    // (a phony target without inputs always runs what depends on it)
    assert_eq!(project.build(&[]).0, ["write progress x"]);
    project.touch("b.c");
    let (mut ran, _) = project.build(&[]);
    ran.sort();
    assert_eq!(ran, ["write progress x", "write report.json x"]);
}

#[test]
fn failures_stop_the_build_as_k_says() {
    let manifest = "rule fail\n  command = fail\nrule w\n  command = write $out x\n\
                    build f1: fail\nbuild f2: fail\nbuild ok: w\n";
    let project = Project::new(&[("build.ninja", manifest)], false);
    let (ran, code) = project.build(&["-j", "1"]);
    assert_eq!(code, 1);
    assert_eq!(ran.iter().filter(|c| *c == "fail").count(), 1);
    let (mut ran, code) = project.build(&["-j", "1", "-k", "0"]);
    ran.sort();
    assert_eq!(
        (ran, code),
        (vec!["fail".into(), "fail".into(), "write ok x".into()], 1)
    );
    let (ran, code) = project.build(&["-j", "1", "-k", "2"]);
    assert_eq!((ran.iter().filter(|c| *c == "fail").count(), code), (2, 1));
}

#[test]
fn an_rspfile_is_written_before_its_command_and_crlf_reads_as_lf() {
    let project = Project::new(
        &[(
            "build.ninja",
            "ldflags = -fp hardware\n\
             rule link\n  command = rsp $out.rsp $\n      $out\n  rspfile = $out.rsp\n  rspfile_content = $in_newline\n\
             build main.elf: link a.o b.o\n",
        ),
        ("a.o", ""),
        ("b.o", "")],
        true,
    );
    let (ran, code) = project.build(&[]);
    assert_eq!(
        (ran, code),
        (vec!["rsp main.elf.rsp main.elf".to_owned()], 0)
    );
    assert_eq!(
        fs::read_to_string(project.path("main.elf")).unwrap(),
        "a.o\nb.o"
    );
}

#[test]
fn a_command_the_executor_declines_runs_as_a_process() {
    let command = if cfg!(windows) {
        "cmd /c echo made> made.txt"
    } else {
        "echo made > made.txt"
    };
    let project = Project::new(
        &[(
            "build.ninja",
            &format!("rule make\n  command = {command}\nbuild made.txt: make\n"),
        )],
        false,
    );
    let (ran, code) = project.build(&[]);
    assert_eq!((ran.len(), code), (0, 0));
    assert!(fs::read_to_string(project.path("made.txt"))
        .unwrap()
        .starts_with("made"));
}

#[cfg(windows)]
#[test]
fn a_target_named_with_slashes_is_found_on_windows() {
    let project = Project::new(
        &[(
            "build.ninja",
            "rule w\n  command = write $out x\nbuild obj\\a.o: w\nbuild obj\\b.o: w\n",
        )],
        true,
    );
    assert_eq!(
        project.build(&["obj/a.o"]),
        (vec![r"write obj\a.o x".to_owned()], 0)
    );
}
