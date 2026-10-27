//! Command line argument parsing and initial build invocation.
//!
//! mwcc-ninja: [`run`] takes its arguments and an [`Executor`] from the caller, where n2's `main` read the process's;
//! `-k` defaults to 1 and `-k 0` never stops, as in ninja; and build.ninja is brought up to date again after each
//! regeneration until it is ([`MAX_REGENERATIONS`] times at most), as ninja does, where n2 regenerates it once; `-n`
//! and `-t commands` are ninja's.

use crate::{
    canon::to_owned_canon_path,
    densemap::Index,
    graph::{BuildId, FileId, Graph},
    load,
    progress::Progress,
    progress_dumb::DumbConsoleProgress,
    progress_fancy::FancyConsoleProgress,
    task::Executor,
    terminal, trace, work,
};
use anyhow::anyhow;
use std::{ffi::OsString, sync::Arc};

/// How many times build.ninja may be regenerated in one run (ninja's limit).
pub const MAX_REGENERATIONS: usize = 100;

/// Arguments to start a build, after parsing all the command line etc.
#[derive(Default)]
struct BuildArgs {
    fake_ninja_compat: bool,
    options: work::Options,
    build_filename: Option<String>,
    targets: Vec<String>,
    verbose: bool,
}

/// Returns the number of completed tasks on a successful build.
fn build(args: BuildArgs) -> anyhow::Result<Option<usize>> {
    let (dumb_console, fancy_console);
    // (mwcc-ninja: a dry run's lines are ninja's on a pipe, whatever the console)
    let progress: &dyn Progress = if terminal::use_fancy() && !args.options.dry_run {
        fancy_console = FancyConsoleProgress::new(args.verbose);
        &fancy_console
    } else {
        dumb_console = DumbConsoleProgress::new(args.verbose);
        &dumb_console
    };

    let build_filename = args.build_filename.as_deref().unwrap_or("build.ninja");
    let dry_run = args.options.dry_run;
    let mut state = trace::scope("load::read", || load::read(build_filename, dry_run))?;
    let mut work = work::Work::new(
        state.graph,
        state.hashes,
        state.db,
        &args.options,
        progress,
        state.pools,
    );

    let mut tasks_run = 0;

    // Bring build.ninja up to date; while that runs anything, load it again and start over (the generator reruns
    // configure, and the new build.ninja has a configure step of its own).
    let mut build_file_target = work.lookup(build_filename);
    let mut regenerations = 0;
    while let Some(target) = build_file_target {
        work.want_file(target)?;
        if !trace::scope("work.run", || work.run())? {
            return Ok(None);
        }
        if work.tasks_run == 0 {
            // build.ninja already up to date.
            break;
        }
        if dry_run {
            // (mwcc-ninja) As ninja: build.ninja would be regenerated, and what then runs is not known.
            return Ok(Some(work.tasks_run));
        }
        // Regenerated build.ninja; start over.
        tasks_run += work.tasks_run;
        regenerations += 1;
        if regenerations > MAX_REGENERATIONS {
            anyhow::bail!(
                "{} is still out of date after {} regenerations",
                build_filename,
                MAX_REGENERATIONS
            );
        }
        state = trace::scope("load::read", || load::read(build_filename, dry_run))?;
        work = work::Work::new(
            state.graph,
            state.hashes,
            state.db,
            &args.options,
            progress,
            state.pools,
        );
        build_file_target = work.lookup(build_filename);
    }

    if !args.targets.is_empty() {
        for name in &args.targets {
            // (mwcc-ninja: on Windows, as in ninja, a target named with / is the one build.ninja names with \)
            let target = work.lookup(name).or_else(|| {
                cfg!(windows)
                    .then(|| work.lookup(&name.replace('/', "\\")))
                    .flatten()
            });
            let Some(target) = target else {
                if args.options.adopt {
                    // cmake invokes -t restat with paths that don't exist
                    // https://github.com/evmar/n2/issues/142
                    continue;
                }
                return Err(anyhow::anyhow!("unknown target {:?}", name));
            };
            if Some(target) == build_file_target {
                // Already built above.
                continue;
            }
            work.want_file(target)?;
        }
    } else if !state.default.is_empty() {
        for target in state.default {
            work.want_file(target)?;
        }
    } else {
        work.want_every_file(build_file_target)?;
    }

    if !trace::scope("work.run", || work.run())? {
        return Ok(None);
    }
    // Include any tasks from initial build in final count of steps.
    Ok(Some(tasks_run + work.tasks_run))
}

fn default_parallelism() -> anyhow::Result<usize> {
    // Ninja uses available processors + a constant, but I don't think the
    // difference matters too much.
    let par = std::thread::available_parallelism()?;
    Ok(usize::from(par))
}

/// The file NAME in GRAPH: ninja's target names (on Windows, as in ninja, one named with / is the one build.ninja
/// names with \; `NAME^` is the first output of the first build NAME is an input of).
fn lookup(graph: &Graph, name: &str) -> anyhow::Result<FileId> {
    let (path, first_dependent) = match name.strip_suffix('^') {
        Some(path) => (path, true),
        None => (name, false),
    };
    let find = |path: &str| graph.files.lookup(&to_owned_canon_path(path));
    let id = find(path)
        .or_else(|| {
            cfg!(windows)
                .then(|| find(&path.replace('/', "\\")))
                .flatten()
        })
        .ok_or_else(|| anyhow!("unknown target {:?}", name))?;
    if !first_dependent {
        return Ok(id);
    }
    let build = graph
        .file(id)
        .dependents
        .first()
        .ok_or_else(|| anyhow!("{:?} has no out edge", path))?;
    graph.builds[*build]
        .outs()
        .first()
        .copied()
        .ok_or_else(|| anyhow!("{:?} has no out edge", path))
}

/// (mwcc-ninja) `-t commands [-s] [TARGETS]`, as ninja's: the commands that build TARGETS (else the default ones,
/// else the files nothing uses), each once, a build's after those of its inputs; with `-s` only TARGETS' own.
/// build.ninja is not brought up to date first, nor .n2_db read.
fn commands(build_filename: &str, rest: &[String]) -> anyhow::Result<i32> {
    let mut single = false;
    let mut names = vec![];
    for arg in rest {
        match arg.as_str() {
            "-s" => single = true,
            option if option.starts_with('-') && option.len() > 1 => {
                println!(
                    "usage: ninja -t commands [options] [targets]

options:
  -s     only print the final command to build [target], not the whole chain"
                );
                return Ok(1);
            }
            name => names.push(name),
        }
    }
    let (graph, default) = load::read_graph(build_filename)?;
    let targets = if !names.is_empty() {
        names
            .iter()
            .map(|name| lookup(&graph, name))
            .collect::<anyhow::Result<Vec<_>>>()?
    } else if !default.is_empty() {
        default
    } else {
        let mut roots = vec![];
        for build in 0..graph.builds.next_id().index() {
            let build = &graph.builds[BuildId::from(build)];
            roots.extend(
                build
                    .outs()
                    .iter()
                    .filter(|&&id| graph.file(id).dependents.is_empty()),
            );
        }
        roots
    };

    fn print(
        graph: &Graph,
        id: Option<BuildId>,
        single: bool,
        seen: &mut std::collections::HashSet<BuildId>,
        out: &mut impl std::io::Write,
    ) -> std::io::Result<()> {
        let Some(id) = id else { return Ok(()) };
        if !seen.insert(id) {
            return Ok(());
        }
        let build = &graph.builds[id];
        if !single {
            for &input in build.ordering_ins() {
                print(graph, graph.file(input).input, single, seen, out)?;
            }
        }
        if let Some(cmdline) = &build.cmdline {
            // (ninja's lines end as its C runtime ends them: CRLF on Windows)
            let end = if cfg!(windows) { "\r\n" } else { "\n" };
            write!(out, "{cmdline}{end}")?;
        }
        Ok(())
    }
    let mut seen = std::collections::HashSet::new();
    let mut out = std::io::BufWriter::new(std::io::stdout().lock());
    for target in targets {
        print(
            &graph,
            graph.file(target).input,
            single,
            &mut seen,
            &mut out,
        )?;
    }
    std::io::Write::flush(&mut out)?;
    Ok(0)
}

/// Run a tool as specified by the `-t` flag`.
fn subtool(args: &mut BuildArgs, tool: &str) -> anyhow::Result<Option<i32>> {
    match tool {
        "list" => {
            println!("subcommands:");
            println!("  commands  list all commands required to rebuild given targets");
            return Ok(Some(1));
        }
        "recompact" if args.fake_ninja_compat => {
            // CMake unconditionally invokes this tool, yuck.
            return Ok(Some(0)); // do nothing
        }
        "restat" if args.fake_ninja_compat => {
            // CMake invokes this after generating build files; mark build
            // targets as up to date by running the build with "adopt" flag
            // on.
            args.options.adopt = true;
        }
        _ => {
            anyhow::bail!("unknown -t {:?}, use -t list to list", tool);
        }
    }
    Ok(None)
}

/// Run a debug tool as specified by the `-d` flag.
fn debugtool(args: &mut BuildArgs, tool: &str) -> anyhow::Result<Option<i32>> {
    match tool {
        "list" => {
            println!("debug tools:");
            println!("  explain       print why each target is considered out of date");
            println!("  trace         generate json performance trace");
            return Ok(Some(1));
        }

        "explain" => args.options.explain = true,
        "trace" => trace::open("trace.json")?,

        _ => anyhow::bail!("unknown -d {:?}, use -d list to list", tool),
    }
    Ok(None)
}

fn parse_args(name: &str, argv: Vec<OsString>) -> anyhow::Result<Result<BuildArgs, i32>> {
    let mut args = BuildArgs::default();
    // (ninja's default: the first failure stops the build)
    args.options.failures_left = Some(1);

    use lexopt::prelude::*;
    let mut parser = lexopt::Parser::from_args(argv);
    while let Some(arg) = parser.next()? {
        match arg {
            Short('h') | Long("help") => {
                println!(
                    "{name}: a ninja-compatible build tool (a fork of n2)
usage: {name} [options] [targets...]

options:
-C dir   chdir before running
-f file  input build file [default: build.ninja]
-j N     parallelism [default: use system thread count]
-k N     keep going until N failures (0 means infinity) [default: 1]
-n       dry run (don't run commands but act like they succeeded)
-v       print executed command lines

-t tool  tools (`-t list` to list)
-d tool  debugging tools (use `-d list` to list)
"
                );
                return Ok(Err(0));
            }

            Short('C') => {
                let dir = parser.value()?;
                std::env::set_current_dir(&dir)
                    .map_err(|err| anyhow!("chdir {:?}: {}", dir, err))?;
            }

            Short('f') => args.build_filename = Some(parser.value()?.to_string_lossy().into()),
            Short('t') => {
                let tool = parser.value()?.to_string_lossy().into_owned();
                if tool == "commands" {
                    // (as in ninja, what follows the tool is its own)
                    let rest: Vec<String> = parser
                        .raw_args()?
                        .map(|arg| arg.to_string_lossy().into_owned())
                        .collect();
                    let build_filename = args.build_filename.as_deref().unwrap_or("build.ninja");
                    return Ok(Err(commands(build_filename, &rest)?));
                }
                if let Some(exit) = subtool(&mut args, &tool)? {
                    return Ok(Err(exit));
                }
            }
            Short('d') => {
                if let Some(exit) = debugtool(&mut args, &parser.value()?.to_string_lossy())? {
                    return Ok(Err(exit));
                }
            }
            Short('j') => args.options.parallelism = parser.value()?.parse()?,
            Short('k') => {
                let failures: usize = parser.value()?.parse()?;
                args.options.failures_left = (failures > 0).then_some(failures);
            }
            Short('n') => args.options.dry_run = true,
            Short('v') | Long("verbose") => args.verbose = true,

            Long("version") => {
                println!("{}", env!("CARGO_PKG_VERSION"));
                return Ok(Err(0));
            }

            Value(arg) => args.targets.push(arg.to_string_lossy().into()),

            _ => anyhow::bail!("{}", arg.unexpected()),
        }
    }

    if args.options.parallelism == 0 {
        args.options.parallelism = default_parallelism()?;
    }

    Ok(Ok(args))
}

fn run_impl(
    name: &str,
    argv: Vec<OsString>,
    executor: Option<Arc<dyn Executor>>,
) -> anyhow::Result<i32> {
    let mut args = match parse_args(name, argv)? {
        Ok(args) => args,
        Err(exit) => return Ok(exit),
    };
    args.options.executor = executor;
    let dry_run = args.options.dry_run;

    match build(args)? {
        None => {
            // (after the failing task's output, ninja's line)
            println!("{name}: build stopped: subcommand failed.");
            return Ok(1);
        }
        Some(0) => {
            // Special case: don't print numbers when no work done.
            println!("{name}: no work to do.");
        }
        // (mwcc-ninja: as ninja, a dry run's lines are all)
        Some(_) if dry_run => {}
        Some(n) => {
            println!(
                "{name}: ran {} task{}, now up to date",
                n,
                if n == 1 { "" } else { "s" }
            );
        }
    }

    Ok(0)
}

/// A build with ARGV (ninja's options and targets), whose commands EXECUTOR runs where it takes them (else as
/// processes); its messages are NAME's. The exit code: 0 when it succeeds, 1 when it fails.
pub fn run(
    name: &str,
    argv: impl IntoIterator<Item = impl Into<OsString>>,
    executor: Option<Arc<dyn Executor>>,
) -> anyhow::Result<i32> {
    let argv = argv.into_iter().map(Into::into).collect();
    let res = run_impl(name, argv, executor);
    trace::close();
    res
}
