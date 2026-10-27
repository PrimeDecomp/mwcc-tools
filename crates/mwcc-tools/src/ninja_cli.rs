//! `mwcc-tools run ninja [OPTIONS] [TARGETS]`: a project's ninja build, run by this process (mwcc-ninja, a fork of
//! n2) rather than by ninja. A command that runs a compiler or linker whose module is built (known as `mwcc-tools
//! run` knows it: the size and SHA-1 of its version's original) runs in process, on the build's thread; any other
//! runs as a process, as ninja would run it. So a project configured without `--wrapper` builds as one configured
//! with `--wrapper mwcc-run`, without a process per compile.
//!
//! What runs in process: a command line whose program is such a compiler or linker, behind any of `mwcc-run` (or
//! `mwcc-tools run`), `wibo` and `sjiswrap` (in that order; sjiswrap's conversion is done by the host). On Windows a
//! command may also be `cmd /c COMMAND && REST` (dtk-template's extab rules): COMMAND runs in process and, once it
//! succeeds, `cmd /c REST` as a process; elsewhere `COMMAND && REST`, through the shell. A command line with other
//! shell syntax in COMMAND (redirections, pipes, variables, globs) runs as a process.
//!
//! A project still configured with `--wrapper mwcc-run` builds the same: the wrapper is dropped and its command runs
//! in process. Regeneration runs `configure.py` with the arguments it was configured with, so the wrapper stays.
use crate::run_cli;
use anyhow::Result;
use mwcc_ninja::{Executor, Termination};
use std::{path::Path, sync::Arc};

/// The messages' name.
const NAME: &str = "mwcc-tools run ninja";

/// The exit code of the build with ARGS (ninja's options and targets). `MWCC_RUN_TIMING=1` prints how many
/// commands ran in process, and their time.
#[cfg(feature = "wasm")]
pub fn run(args: &[String]) -> Result<i32> {
    // (absolute: `-C` changes the directory before the build. The download cache, where there is no checkout, is
    // prepared once a command runs: `-n` and `-t` need neither.)
    let checkout = run_cli::root().ok().map(std::path::absolute).transpose()?;
    let executor = Arc::new(in_process::InProcess::new(checkout));
    let code = mwcc_ninja::run(NAME, args, Some(executor.clone() as Arc<dyn Executor>))?;
    if std::env::var_os("MWCC_RUN_TIMING").is_some_and(|v| v == "1") {
        eprintln!("{NAME}: {}", executor.summary());
    }
    Ok(code)
}

#[cfg(not(feature = "wasm"))]
pub fn run(args: &[String]) -> Result<i32> {
    mwcc_ninja::run(NAME, args, None)
}

/// A command a build runs: the program and its arguments, after the wrappers (whether sjiswrap was one), and what
/// runs after it once it succeeds.
#[derive(Debug, PartialEq)]
struct Plan {
    program: String,
    args: Vec<String>,
    sjis: bool,
    /// The command line from the program on, as its process would get it (Windows; not behind sjiswrap, which makes
    /// its own).
    command_line: Option<String>,
    /// The rest of a `cmd /c COMMAND && REST` (`sh -c`), as a command line of its own.
    rest: Option<String>,
}

/// Whether WORD names a program STEM (in any case, with or without an extension). Its directory ends at the last `/`
/// or `\` on any host: a Windows command line's paths have backslashes.
fn stem_is(word: &str, stem: &str) -> bool {
    let name = word.rsplit(['/', '\\']).next().unwrap_or(word);
    let name = match name.rsplit_once('.') {
        Some((base, _)) if !base.is_empty() => base,
        _ => name,
    };
    name.eq_ignore_ascii_case(stem)
}

/// WORDS (each with where it starts in LINE) as a plan: the wrappers dropped.
fn plan_words(line: &str, words: &[(usize, String)], windows: bool) -> Option<Plan> {
    let mut at = 0;
    let word = |i: usize| words.get(i).map(|(_, w)| w.as_str());
    if word(at).is_some_and(|w| stem_is(w, "mwcc-run")) {
        at += 1;
    } else if word(at).is_some_and(|w| stem_is(w, "mwcc-tools")) && word(at + 1) == Some("run") {
        at += 2;
    }
    if word(at).is_some_and(|w| stem_is(w, "wibo")) {
        at += 1;
    }
    let sjis = word(at).is_some_and(|w| stem_is(w, "sjiswrap"));
    if sjis {
        at += 1;
    }
    let (start, program) = words.get(at)?;
    Some(Plan {
        program: program.clone(),
        args: words[at + 1..].iter().map(|(_, w)| w.clone()).collect(),
        sjis,
        command_line: (windows && !sjis).then(|| line[*start..].trim_end().to_owned()),
        rest: None,
    })
}

/// LINE split at its first `&&` outside quotes (QUOTES: the quote characters).
fn split_and<'a>(line: &'a str, quotes: &[char], escapes: bool) -> (&'a str, Option<&'a str>) {
    let mut quote = None;
    let mut escaped = false;
    let mut chars = line.char_indices().peekable();
    while let Some((i, c)) = chars.next() {
        if escaped {
            escaped = false;
        } else if escapes && c == '\\' && quote != Some('\'') {
            escaped = true;
        } else if let Some(q) = quote {
            if c == q {
                quote = None;
            }
        } else if quotes.contains(&c) {
            quote = Some(c);
        } else if c == '&' && chars.peek().is_some_and(|&(_, c)| c == '&') {
            return (&line[..i], Some(&line[i + 2..]));
        }
    }
    (line, None)
}

/// A Windows command line (as `CreateProcess` gets it) as a plan.
fn plan_windows(line: &str) -> Option<Plan> {
    let words = run_cli::split_command_line(line);
    let is_cmd = words.first().is_some_and(|(_, w)| stem_is(w, "cmd"))
        && words
            .get(1)
            .is_some_and(|(_, w)| w.eq_ignore_ascii_case("/c"));
    if !is_cmd {
        return plan_words(line, &words, true);
    }
    // (cmd's own rules for a command that starts with a quote are not followed: run as a process)
    let body = &line[words.get(2)?.0..];
    let (first, rest) = split_and(body, &['"'], false);
    let first = first.trim_end();
    if !cmd_plain(first) {
        return None;
    }
    let mut plan = plan_words(first, &run_cli::split_command_line(first), true)?;
    plan.rest = rest.map(|rest| format!("cmd /c {}", rest.trim_start()));
    Some(plan)
}

/// Whether cmd runs COMMAND as its words: no variables, escapes or other operators.
fn cmd_plain(command: &str) -> bool {
    let mut quoted = false;
    for c in command.chars() {
        match c {
            '"' => quoted = !quoted,
            '%' | '!' => return false,
            '&' | '|' | '<' | '>' | '^' | '(' | ')' if !quoted => return false,
            _ => {}
        }
    }
    true
}

/// REST, what a plan runs after its program (`cmd /c ...` on Windows, a shell's line elsewhere), as commands joined by
/// `&&`: each as a command line of its own and its words. None when it has more than words and `&&`.
fn rest_commands(rest: &str) -> Option<Vec<(String, Vec<String>)>> {
    let windows = cfg!(windows);
    let mut line = if windows {
        let words = run_cli::split_command_line(rest);
        let is_cmd = words.first().is_some_and(|(_, w)| stem_is(w, "cmd"))
            && words
                .get(1)
                .is_some_and(|(_, w)| w.eq_ignore_ascii_case("/c"));
        if !is_cmd {
            return None;
        }
        &rest[words.get(2)?.0..]
    } else {
        rest
    };
    let mut commands = vec![];
    loop {
        let (first, next) = if windows {
            split_and(line, &['"'], false)
        } else {
            split_and(line, &['"', '\''], true)
        };
        let first = first.trim();
        let words: Vec<String> = if windows {
            if !cmd_plain(first) {
                return None;
            }
            run_cli::split_command_line(first)
                .into_iter()
                .map(|(_, w)| w)
                .collect()
        } else {
            split_sh(first)?.into_iter().map(|(_, w)| w).collect()
        };
        let command = if windows {
            format!("cmd /c {first}")
        } else {
            first.to_owned()
        };
        commands.push((command, words));
        match next {
            Some(next) => line = next,
            None => break,
        }
    }
    Some(commands)
}

/// A POSIX shell's words of LINE (each with where it starts), when it is only words: None when it has shell syntax
/// beyond quotes and backslashes.
fn split_sh(line: &str) -> Option<Vec<(usize, String)>> {
    let mut words = vec![];
    let mut chars = line.char_indices().peekable();
    loop {
        while chars.peek().is_some_and(|&(_, c)| c == ' ' || c == '\t') {
            chars.next();
        }
        let Some(&(start, _)) = chars.peek() else {
            break;
        };
        let mut word = String::new();
        while let Some(&(_, c)) = chars.peek() {
            if c == ' ' || c == '\t' {
                break;
            }
            chars.next();
            match c {
                '\'' => loop {
                    match chars.next()? {
                        (_, '\'') => break,
                        (_, c) => word.push(c),
                    }
                },
                '"' => loop {
                    match chars.next()? {
                        (_, '"') => break,
                        (_, '\\') => match chars.next()? {
                            (_, c @ ('"' | '\\')) => word.push(c),
                            (_, c @ ('$' | '`' | '\n')) => {
                                let _ = c;
                                return None;
                            }
                            (_, c) => {
                                word.push('\\');
                                word.push(c);
                            }
                        },
                        (_, '$' | '`') => return None,
                        (_, c) => word.push(c),
                    }
                },
                '\\' => match chars.next()? {
                    (_, '\n') => return None,
                    (_, c) => word.push(c),
                },
                '|' | '&' | ';' | '<' | '>' | '(' | ')' | '$' | '`' | '*' | '?' | '[' | '#'
                | '~' | '{' | '}' | '\n' => return None,
                c => word.push(c),
            }
        }
        words.push((start, word));
    }
    Some(words)
}

/// A POSIX command line (as `sh -c` gets it) as a plan.
fn plan_posix(line: &str) -> Option<Plan> {
    let (first, rest) = split_and(line, &['"', '\''], true);
    let mut plan = plan_words(first, &split_sh(first)?, false)?;
    plan.rest = rest.map(|rest| rest.trim_start().to_owned());
    Some(plan)
}

fn plan(line: &str) -> Option<Plan> {
    if cfg!(windows) {
        plan_windows(line)
    } else {
        plan_posix(line)
    }
}

#[cfg(feature = "wasm")]
mod in_process {
    use super::*;
    use crate::transform_dep;
    use mwcc_oracle_core::in_process as module;
    use std::{
        collections::HashSet,
        path::PathBuf,
        sync::{
            Mutex,
            atomic::{AtomicU64, AtomicUsize, Ordering},
        },
        time::Instant,
    };

    /// Runs the commands of compilers and linkers with modules in process.
    pub struct InProcess {
        /// The mwcc checkout (`MWCC_ORACLE_ROOT`, or the one this executable is in), when there is one.
        checkout: Option<PathBuf>,
        environment: Vec<(String, String)>,
        /// The modules loaded (each once, before its first run, while the others wait).
        loaded: Mutex<HashSet<PathBuf>>,
        /// The commands run in process, and their time (µs).
        runs: AtomicUsize,
        micros: AtomicU64,
        /// Of that, instantiating the module, and running it (µs).
        instantiate: AtomicU64,
        running: AtomicU64,
        /// The files the runs share: each directory listed and each file read once in the build (a process's
        /// outputs again after it).
        files: Arc<module::SharedFiles>,
        /// The host as `transform_dep.py` tells it, and the `.d` files transformed here.
        host: transform_dep::Host,
        transformed: AtomicUsize,
        /// For `--relative`: the build's directory, and each path's real path.
        build_root: std::sync::OnceLock<Option<String>>,
        realpaths: Mutex<std::collections::HashMap<String, Option<String>>>,
    }

    impl InProcess {
        pub fn new(checkout: Option<PathBuf>) -> Self {
            Self {
                checkout,
                environment: std::env::vars().collect(),
                loaded: Mutex::new(HashSet::new()),
                runs: AtomicUsize::new(0),
                micros: AtomicU64::new(0),
                instantiate: AtomicU64::new(0),
                running: AtomicU64::new(0),
                files: Arc::default(),
                host: transform_dep::Host::this(),
                transformed: AtomicUsize::new(0),
                build_root: std::sync::OnceLock::new(),
                realpaths: Mutex::default(),
            }
        }

        /// How many commands ran in process, and their time.
        pub fn summary(&self) -> String {
            let runs = self.runs.load(Ordering::Relaxed);
            let seconds = self.micros.load(Ordering::Relaxed) as f64 / 1e6;
            let instantiate = self.instantiate.load(Ordering::Relaxed) as f64 / 1e6;
            let running = self.running.load(Ordering::Relaxed) as f64 / 1e6;
            let (listed, read, bytes) = self.files.stats();
            format!(
                "{runs} commands in process, {seconds:.1} s in all: {instantiate:.1} s instantiating the modules, \
                 {running:.1} s running them; {listed} directories listed and {read} files read ({:.1} MB); {} .d files transformed",
                bytes as f64 / 1e6,
                self.transformed.load(Ordering::Relaxed)
            )
        }

        fn preload(&self, module: &Path) -> Result<()> {
            let mut loaded = self.loaded.lock().unwrap_or_else(|e| e.into_inner());
            if !loaded.contains(module) {
                module::preload(module)?;
                loaded.insert(module.to_path_buf());
            }
            Ok(())
        }

        fn run(
            &self,
            root: &Path,
            plan: &Plan,
            version: mwcc_oracle_core::version::Version,
            module: &Path,
            outputs: &[PathBuf],
            output: &mut dyn FnMut(&[u8]),
        ) -> Result<Termination> {
            self.preload(module)?;
            let program = Path::new(&plan.program);
            let outcome = module::run(
                &module::Run {
                    root,
                    version,
                    module,
                    program,
                    args: &plan.args,
                    directory: &std::env::current_dir()?,
                    environment: &self.environment,
                    timeout: None,
                    trace: module::traces(root, program),
                    overlay: None,
                    transcode_sjis: plan.sjis,
                    command_line: plan.command_line.as_deref(),
                    files: Some(&self.files),
                },
                &mwcc_ninja::was_interrupted,
            )
            .inspect_err(|e| {
                // (what the run wrote before a trap)
                if let Some(trapped) = e.downcast_ref::<module::Trapped>() {
                    output(&trapped.console);
                }
            })?;
            output(&outcome.console);
            self.instantiate
                .fetch_add(outcome.instantiate.as_micros() as u64, Ordering::Relaxed);
            self.running
                .fetch_add(outcome.run.as_micros() as u64, Ordering::Relaxed);
            if outcome.cancelled {
                return Ok(Termination::Interrupted);
            }
            if outcome.exit_code != 0 {
                return Ok(Termination::Failure);
            }
            match &plan.rest {
                Some(rest) => self.rest(rest, outputs, output),
                None => Ok(Termination::Success),
            }
        }

        /// REST, what runs after a command run in process: its commands in turn when it is only commands joined by
        /// `&&`, dtk-template's `transform_dep.py` done here ([`crate::transform_dep`]); else one process.
        fn rest(
            &self,
            rest: &str,
            outputs: &[PathBuf],
            output: &mut dyn FnMut(&[u8]),
        ) -> Result<Termination> {
            let commands = rest_commands(rest).filter(|commands| {
                commands
                    .iter()
                    .any(|(_, words)| transform_dep::command(words).is_some())
            });
            let Some(commands) = commands else {
                return self.process(rest, outputs, output);
            };
            for (command, words) in commands {
                if let Some(script) = transform_dep::command(&words)
                    && self.transform_dep(&script)
                {
                    continue;
                }
                let termination = self.process(&command, outputs, output)?;
                if termination != Termination::Success {
                    return Ok(termination);
                }
            }
            Ok(Termination::Success)
        }

        /// SCRIPT's work: whether it was done (else the script is to do it).
        fn transform_dep(&self, script: &transform_dep::Script) -> bool {
            let realpath = |path: &str| self.realpath(path);
            let root = script.relative.then(|| {
                self.build_root
                    .get_or_init(|| {
                        let directory = std::env::current_dir().ok()?;
                        transform_dep::realpath(directory.to_str()?)
                    })
                    .as_deref()
            });
            let relative = match root {
                None => None,
                Some(None) => return false,
                Some(Some(root)) => Some(transform_dep::Relative {
                    root,
                    realpath: &realpath,
                }),
            };
            let Some(text) = std::fs::read(script.input)
                .ok()
                .and_then(|text| transform_dep::transform(&text, self.host, relative.as_ref()))
            else {
                return false;
            };
            if std::fs::write(script.output, text).is_err() {
                return false;
            }
            self.files.changed(Path::new(script.output));
            self.transformed.fetch_add(1, Ordering::Relaxed);
            true
        }

        /// `os.path.realpath` of PATH, each once in the build (the headers are the same for every compile).
        fn realpath(&self, path: &str) -> Option<String> {
            if let Some(real) = self
                .realpaths
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .get(path)
            {
                return real.clone();
            }
            let real = transform_dep::realpath(path);
            self.realpaths
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .insert(path.to_owned(), real.clone());
            real
        }

        /// CMDLINE run as a process; then the runs read its OUTPUTS (the build's) again from the disk.
        fn process(
            &self,
            cmdline: &str,
            outputs: &[PathBuf],
            output: &mut dyn FnMut(&[u8]),
        ) -> Result<Termination> {
            let termination = mwcc_ninja::run_command(cmdline, output);
            for path in outputs {
                self.files.changed(path);
            }
            termination
        }
    }

    impl Executor for InProcess {
        fn execute(
            &self,
            cmdline: &str,
            outputs: &[PathBuf],
            output: &mut dyn FnMut(&[u8]),
        ) -> Option<Result<Termination>> {
            // (the checkout's module, else the released one: downloaded before its first run)
            let found = match plan(cmdline) {
                Some(plan) => match crate::wasm_download::module_for(
                    self.checkout.as_deref(),
                    Path::new(&plan.program),
                ) {
                    Ok(found) => found.map(|found| (plan, found)),
                    Err(e) => return Some(Err(e)),
                },
                None => None,
            };
            let Some((plan, (root, version, module))) = found else {
                return Some(self.process(cmdline, outputs, output));
            };
            let began = Instant::now();
            let termination = self.run(&root, &plan, version, &module, outputs, output);
            self.runs.fetch_add(1, Ordering::Relaxed);
            self.micros
                .fetch_add(began.elapsed().as_micros() as u64, Ordering::Relaxed);
            Some(termination)
        }

        fn changed(&self, path: &Path) {
            self.files.changed(path);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn words(line: &str) -> Vec<String> {
        split_sh(line)
            .unwrap()
            .into_iter()
            .map(|(_, w)| w)
            .collect()
    }

    #[test]
    fn a_windows_command_is_its_program_after_the_wrappers() {
        let line = r#"C:\x\mwcc-run.exe build\tools\sjiswrap.exe build\compilers\GC\1.3.2\mwcceppc.exe -nodefaults -i "a b" -c x.cpp"#;
        let plan = plan_windows(line).unwrap();
        assert_eq!(plan.program, r"build\compilers\GC\1.3.2\mwcceppc.exe");
        assert_eq!(plan.args, ["-nodefaults", "-i", "a b", "-c", "x.cpp"]);
        assert!(plan.sjis);
        assert_eq!(plan.command_line, None);
        assert_eq!(plan.rest, None);

        let line = r#"build\compilers\GC\1.3.2\mwcceppc.exe -pragma "x(1)" -c x.cpp "#;
        let plan = plan_windows(line).unwrap();
        assert!(!plan.sjis);
        assert_eq!(
            plan.command_line.as_deref(),
            Some(r#"build\compilers\GC\1.3.2\mwcceppc.exe -pragma "x(1)" -c x.cpp"#)
        );
        let line = r"mwcc-tools.exe run build\compilers\GC\2.7\mwldeppc.exe -o a.elf @a.elf.rsp";
        let plan = plan_windows(line).unwrap();
        assert_eq!(plan.program, r"build\compilers\GC\2.7\mwldeppc.exe");
        assert_eq!(
            plan.command_line.as_deref(),
            Some(r"build\compilers\GC\2.7\mwldeppc.exe -o a.elf @a.elf.rsp")
        );
    }

    #[test]
    fn cmd_runs_its_first_command_then_the_rest() {
        let line = r#"cmd /c build\tools\sjiswrap.exe build\compilers\GC\2.7\mwcceppc.exe -c "a.cpp" -o build\x && build\tools\dtk.exe extab clean --padding "" build\x\a.o build\x\a.o"#;
        let plan = plan_windows(line).unwrap();
        assert_eq!(plan.program, r"build\compilers\GC\2.7\mwcceppc.exe");
        assert_eq!(plan.args, ["-c", "a.cpp", "-o", r"build\x"]);
        assert!(plan.sjis);
        assert_eq!(
            plan.rest.as_deref(),
            Some(r#"cmd /c build\tools\dtk.exe extab clean --padding "" build\x\a.o build\x\a.o"#)
        );
        let plan = plan_windows(r"cmd /c mwcceppc.exe -c a.c && b && c").unwrap();
        assert_eq!(plan.rest.as_deref(), Some("cmd /c b && c"));
        assert_eq!(plan.command_line.as_deref(), Some("mwcceppc.exe -c a.c"));
        // (what cmd reads its own way runs as a process)
        for line in [
            r"cmd /c mwcceppc.exe -c a.c > log && b",
            r"cmd /c mwcceppc.exe -c %SRC%",
            r"cmd /c mwcceppc.exe -c a.c | more",
            r"cmd /c mwcceppc.exe -c a.c & b",
        ] {
            assert_eq!(plan_windows(line), None, "{line}");
        }
        assert!(plan_windows(r#"cmd /c mwcceppc.exe -c "a&b.c""#).is_some());
    }

    #[test]
    fn what_follows_a_command_is_its_commands() {
        let commands = |rest: &str| {
            rest_commands(rest).map(|c| {
                c.into_iter()
                    .map(|(command, words)| (command, words.join("|")))
                    .collect::<Vec<_>>()
            })
        };
        if cfg!(windows) {
            assert_eq!(
                commands(
                    r#"cmd /c build\tools\dtk.exe extab clean a.o a.o && "C:\Py 3\python.exe" tools\transform_dep.py a.d a.d --relative"#
                ),
                Some(vec![
                    (
                        r"cmd /c build\tools\dtk.exe extab clean a.o a.o".to_owned(),
                        r"build\tools\dtk.exe|extab|clean|a.o|a.o".to_owned()
                    ),
                    (
                        r#"cmd /c "C:\Py 3\python.exe" tools\transform_dep.py a.d a.d --relative"#
                            .to_owned(),
                        r"C:\Py 3\python.exe|tools\transform_dep.py|a.d|a.d|--relative".to_owned()
                    ),
                ])
            );
            assert_eq!(commands("cmd /c a %X% && b"), None);
            assert_eq!(commands("a && b"), None);
        } else {
            assert_eq!(
                commands(
                    "build/tools/dtk extab clean a.o a.o && \"/usr/bin/python3\" tools/transform_dep.py a.d a.d"
                ),
                Some(vec![
                    (
                        "build/tools/dtk extab clean a.o a.o".to_owned(),
                        "build/tools/dtk|extab|clean|a.o|a.o".to_owned()
                    ),
                    (
                        "\"/usr/bin/python3\" tools/transform_dep.py a.d a.d".to_owned(),
                        "/usr/bin/python3|tools/transform_dep.py|a.d|a.d".to_owned()
                    ),
                ])
            );
            assert_eq!(commands("a || b && c"), None);
        }
    }

    #[test]
    fn a_posix_command_is_only_words() {
        assert_eq!(
            words(r#"wibo 'a b' "c \"d\" \x" e\ f"#),
            ["wibo", "a b", r#"c "d" \x"#, "e f"]
        );
        for line in [
            "a > b", "a | b", "a; b", "a $X", "a *.c", "a `b`", "a \"$X\"", "a (b)",
        ] {
            assert_eq!(split_sh(line), None, "{line}");
        }
        let plan = plan_posix(
            "build/tools/wibo build/tools/sjiswrap.exe build/compilers/GC/2.7/mwcceppc.exe -c 'a b.c' && build/tools/dtk extab clean a.o a.o",
        )
        .unwrap();
        assert_eq!(plan.program, "build/compilers/GC/2.7/mwcceppc.exe");
        assert_eq!(plan.args, ["-c", "a b.c"]);
        assert!(plan.sjis);
        assert_eq!(plan.command_line, None);
        assert_eq!(
            plan.rest.as_deref(),
            Some("build/tools/dtk extab clean a.o a.o")
        );
        assert_eq!(split_and("a '&&' && b", &['"', '\''], true).1, Some(" b"));
        assert_eq!(split_and(r"a \&& b", &['"', '\''], true).1, None);
    }
}
