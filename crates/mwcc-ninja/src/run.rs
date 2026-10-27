//! Command line argument parsing and initial build invocation.
//!
//! mwcc-ninja: [`run`] takes its arguments and an [`Executor`] from the caller, where n2's `main` read the process's;
//! `-k` defaults to 1 and `-k 0` never stops, as in ninja; and build.ninja is brought up to date again after each
//! regeneration until it is ([`MAX_REGENERATIONS`] times at most), as ninja does, where n2 regenerates it once.

use crate::{
    load, progress::Progress, progress_dumb::DumbConsoleProgress,
    progress_fancy::FancyConsoleProgress, task::Executor, terminal, trace, work,
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
    let progress: &dyn Progress = if terminal::use_fancy() {
        fancy_console = FancyConsoleProgress::new(args.verbose);
        &fancy_console
    } else {
        dumb_console = DumbConsoleProgress::new(args.verbose);
        &dumb_console
    };

    let build_filename = args.build_filename.as_deref().unwrap_or("build.ninja");
    let mut state = trace::scope("load::read", || load::read(build_filename))?;
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
        state = trace::scope("load::read", || load::read(build_filename))?;
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

/// Run a tool as specified by the `-t` flag`.
fn subtool(args: &mut BuildArgs, tool: &str) -> anyhow::Result<Option<i32>> {
    match tool {
        "list" => {
            println!("subcommands:");
            println!("  (none)");
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
                if let Some(exit) = subtool(&mut args, &parser.value()?.to_string_lossy())? {
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
