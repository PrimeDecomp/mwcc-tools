use anyhow::{Context, Result, bail};
use mwcc_oracle_core::{
    corpus, elf,
    runner::{self},
    version::Version,
};
use serde_json::json;
use std::{env, fs, path::PathBuf, process::ExitCode, time::Duration};
mod binary_cli;
mod build_cli;
mod ninja_cli;
mod provenance_cli;
mod release_cli;
mod run_cli;
mod serve_cli;
#[cfg(feature = "wasm")]
mod transform_dep;
#[cfg(feature = "wasm")]
mod wasm_download;

/// The value of the option ARGS[I], the argument after it (I moves to it).
fn option_value<'a>(args: &'a [String], i: &mut usize) -> Result<&'a String> {
    let option = &args[*i];
    *i += 1;
    args.get(*i)
        .with_context(|| format!("{option} needs a value"))
}

/// A `--timeout SECS` value: finite and positive.
fn timeout(value: &str) -> Result<Duration> {
    let timeout = Duration::try_from_secs_f64(value.parse()?)
        .context("timeout must be finite and positive")?;
    anyhow::ensure!(!timeout.is_zero(), "timeout must be positive");
    Ok(timeout)
}

fn corpus_command(args: &[String]) -> Result<()> {
    let mut root = None;
    let mut version = Version::Gc132;
    let mut canaries = None;
    let mut project = None;
    let mut keep = None;
    let mut exe = None;
    let mut limit = None;
    let mut list = None;
    let mut jobs = std::thread::available_parallelism().map_or(4, usize::from);
    let mut all_traces = false;
    let mut source_arenas = false;
    let mut source_nodes = false;
    let mut trace_reference = None;
    let mut ignore = vec![];
    let mut wasm: Option<PathBuf> = None;
    let mut json_output = false;
    let mut timeout = Duration::from_secs(600);
    let mut i = 0;
    while i < args.len() {
        if args[i] == "--all-traces" {
            all_traces = true;
            i += 1;
            continue;
        }
        if args[i] == "--json" {
            json_output = true;
            i += 1;
            continue;
        }
        if args[i] == "--source-arenas" || args[i] == "--source-nodes" {
            source_arenas = true;
            source_nodes |= args[i] == "--source-nodes";
            i += 1;
            continue;
        }
        let option = args[i].as_str();
        let value = option_value(args, &mut i)?;
        match option {
            "--oracle-root" => root = Some(PathBuf::from(value)),
            "--version" => version = Version::parse(value)?,
            "--canaries" => canaries = Some(PathBuf::from(value)),
            "--prime" | "--project" => project = Some(PathBuf::from(value)),
            "--keep" => keep = Some(PathBuf::from(value)),
            "--exe" => exe = Some(PathBuf::from(value)),
            "--trace-reference" => trace_reference = Some(PathBuf::from(value)),
            "--ignore" => ignore.push(value.clone()),
            "--wasm" => wasm = Some(PathBuf::from(value)),
            "--limit" => limit = Some(value.parse::<usize>()?),
            "--list" => list = Some(PathBuf::from(value)),
            "--jobs" => jobs = value.parse()?,
            "--timeout" => timeout = self::timeout(value)?,
            _ => bail!("unknown corpus option {option}"),
        }
        i += 1;
    }
    let mut inputs = vec![];
    if let Some(path) = canaries {
        inputs.extend(corpus::canary_jobs(&path)?);
    }
    if let Some(path) = project {
        inputs.extend(corpus::project_jobs(&path, version)?);
    }
    if let Some(path) = list {
        let names = fs::read_to_string(path)?;
        let names: std::collections::BTreeSet<_> = names.lines().map(str::trim).collect();
        inputs.retain(|j| names.contains(j.name.as_str()));
    }
    if let Some(limit) = limit {
        inputs.truncate(limit);
    }
    let in_process = wasm
        .map(|module| wasm_compiler(version, &module))
        .transpose()?;
    let report = corpus::run(&corpus::Request {
        root: root.map_or_else(run_cli::root, Ok)?,
        version,
        executable: exe,
        jobs: inputs,
        parallelism: jobs,
        keep,
        timeout,
        all_traces,
        source_arenas,
        source_nodes,
        trace_reference,
        ignore,
        in_process,
    })?;
    if json_output {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        for outcome in &report.outcomes {
            if !outcome.problems.is_empty() {
                println!("DIFF {}: {}", outcome.name, outcome.problems.join("; "));
            }
        }
        println!("{}/{} identical", report.identical, report.total);
    }
    anyhow::ensure!(
        report.identical == report.total,
        "{} corpus input(s) differ",
        report.total - report.identical
    );
    Ok(())
}

fn help() {
    println!(
        r#"mwcc-tools

  serve [--project DIR]              the engine's methods over JSON lines on stdin/stdout (serve --help)
  call METHOD [@PARAMS.json | KEY=VALUE ...]   one method, in a fresh session (call --help)
  methods [METHOD]                   the methods, or one's parameter and result schemas
  provenance audit-counters|audit-dispatch|audit-source-arenas --root ORACLE_ROOT --version GC_2_7
  provenance counter-sites [--write] [--root DIR]
      the WebAssembly oracle's table of the original's counter increments, from gc2's sources (after ninja all_source)
  provenance derive-guards --version V [--root DIR] NAME[=SIZE]...
  run [sjiswrap.exe] EXE ARGS...
      a build's compiler wrapper (as wibo): a compiler with a built module runs in process, anything else as itself;
      a copy or link of this executable named mwcc-run runs as `mwcc-tools run`
  run ninja [-C DIR] [-j N] [-k N] [-n] [--json FILE] [TARGETS...] | -t commands|inputs|targets all ...
      a project's build.ninja (configured without --wrapper) built in place of ninja: the compilers and linkers
      with built modules run in process, any other command as a process (docs/wasm.md)
  corpus --canaries DIR | --project DIR [--oracle-root ROOT] [--version V] [--jobs N] [--all-traces]
         [--source-arenas | --source-nodes] [--exe ORACLE] [--trace-reference OTHER_ORACLE [--ignore PREFIX]...] [--wasm MODULE]
      --wasm runs MODULE (build/VERSION/wasm/*.wasm) in process in the oracle's place
      --source-arenas, --source-nodes trace those streams too, and compare them with the reference's
  object compare ORACLE.o ORIGINAL.o
  pe inspect|read|find FILE [options]
  implib DEF OUTPUT
  rsrc2res EXE OUTPUT
  split VERSION [--source] [--root DIR] [--output DIR]
  relink-check VERSION [--write] [--root DIR]
      which compiled units of a source tree's build link into the original (after ninja all_source);
      --write records the others as its state.json's relink_exclude
  source-only VERSION [--oracle [--wasm [--clang FILE] [--wasm-ld FILE]]] [--root DIR]
      link the compiler of a source tree's sources alone, build/VERSION/<tree>/source-only/mwcceppc.exe;
      --wasm: link the oracle's WebAssembly module, build/VERSION/wasm/linked/oracle.wasm, which ninja's
      wasm-opt makes build/VERSION/wasm/oracle.wasm of (ninja builds both)
      (after ninja all_source; compare it with the original: corpus --exe); --oracle links the Rust hooks in,
      build/VERSION/<tree>/source-oracle/mwcceppc.exe
  compare extract|compare|report VERSION [--root DIR]
  locate VERSION SOURCE... [--root DIR]
  bindfrom VERSION [--root DIR]
  funcmatch FROM TO [--root DIR] [--json]
  remap FROM TO [--symbols] [--root DIR]
  derive-splits VERSION [--reference V] [--exe FILE] [--out DIR] [--root DIR]
  disasm VERSION ADDRESS [SIZE] [--root DIR]
  fdiff VERSION SOURCE FUNCTION ADDRESS [--root DIR]
  nearest VERSION SOURCE... [--root DIR]
  link [--oracle | --source] VERSION OUTPUT INPUT... [--root DIR] [--timeout SECS]
  configure [--version V] [--mode link|compare] [--split-only] [--oracle [--producer rust|c]] [--root DIR]
  probe-build [--version V] [--from CHECKOUT] [--root DIR]
      the in-process module of a checkout with changed gc2 sources (configure, then build/V/wasm/mwcc.wasm);
      COMMAND --help gives a build command's options
  build-hooks OUTPUT [--trace-shim | --frontend-version V] [--wasm] [--root DIR] [--rustc FILE] [--timeout SECS]
  package-release OUTPUT.zip --binary FILE --target TARGET [--root DIR] [--source-revision COMMIT] [--notices DIR]
  transform-dep DEP OUT [--root DIR]
  download TOOL OUTPUT [OTHER_OUTPUT...] [--root DIR] [--timeout SECS]
  download wasm [VERSION...] [--root DIR]   the released WebAssembly compilers and linkers, into the cache `run` uses
  verify|gates [--root DIR] [--ninja FILE] [--jobs N] [--timeout SECS]

The oracle, colouring, inlining, TU, provenance and source-guided tools are methods: `methods` lists them,
`call` runs one, `serve` keeps a session (docs/api.md; the Python package mwcc_tools).
Guides: docs/oracle.md; what is open: docs/open-work.md."#
    );
}

fn run() -> Result<()> {
    // (`--option=value` is `--option value`, up to a `--`, after which the arguments are a compiler's)
    let mut args = vec![];
    let mut compiler_flags = false;
    for arg in env::args().skip(1) {
        if arg == "--" {
            compiler_flags = true;
        }
        if !compiler_flags
            && arg.starts_with("--")
            && let Some((option, value)) = arg.split_once('=')
        {
            args.push(option.to_owned());
            args.push(value.to_owned());
        } else {
            args.push(arg);
        }
    }
    match args.first().map(String::as_str) {
        Some("serve") => serve_cli::serve(&args[1..])?,
        Some("call") => serve_cli::call(&args[1..])?,
        Some("methods") => serve_cli::methods(&args[1..])?,
        Some("provenance") => provenance_cli::run(&args[1..])?,
        Some("--version") => println!("mwcc-tools {}", env!("CARGO_PKG_VERSION")),
        Some("package-release") => release_cli::run(&args[1..])?,
        #[cfg(feature = "wasm")]
        Some("download") if args.get(1).is_some_and(|a| a == "wasm") => {
            wasm_download::command(&args[2..])?
        }
        Some(
            "configure"
            | "probe-build"
            | "transform-dep"
            | "download"
            | "verify"
            | "gates"
            | "build-hooks"
            | "wasm-mirror"
            | "wasm-linker-version"
            | "wasm-common"
            | "wasm-stubs",
        ) => build_cli::run(&args[0], &args[1..])?,
        Some("--help" | "-h") | None => help(),
        Some("corpus") => corpus_command(&args[1..])?,
        Some(
            "pe" | "implib" | "rsrc2res" | "split" | "link" | "relink-check" | "source-only"
            | "compare" | "locate" | "bindfrom" | "funcmatch" | "remap" | "derive-splits"
            | "disasm" | "fdiff" | "nearest",
        ) => binary_cli::run(&args[0], &args[1..])?,
        Some("object") if args.get(1).map(String::as_str) == Some("compare") => {
            let a = fs::read(
                args.get(2)
                    .context("object compare requires ORACLE.o ORIGINAL.o")?,
            )?;
            let b = fs::read(args.get(3).context("object compare requires ORIGINAL.o")?)?;
            println!(
                "{}",
                serde_json::to_string_pretty(
                    &json!({"identical": a == b, "differences": elf::differences(&a, &b)?})
                )?
            );
        }
        Some(command) => bail!("unknown command {command}; use --help"),
    }
    Ok(())
}

fn main() -> ExitCode {
    // (`run`'s arguments are the wrapped command's, as they are)
    if let Some(args) = run_cli::wrapped_command() {
        match run_cli::run(&args) {
            Ok(code) => std::process::exit(code),
            Err(error) => {
                eprintln!("{error:#}");
                return ExitCode::FAILURE;
            }
        }
    }
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error:#}");
            ExitCode::FAILURE
        }
    }
}

/// `corpus --wasm MODULE`: the module run in process in the oracle's place (as `runner` runs one by default).
#[cfg(feature = "wasm")]
fn wasm_compiler(version: Version, module: &std::path::Path) -> Result<corpus::InProcess> {
    let module = std::path::absolute(module)?;
    Ok(corpus::InProcess(Box::new(
        move |invocation: runner::Invocation<'_>| {
            mwcc_oracle_core::in_process::execute(&invocation, version, &module, &|| false)
        },
    )))
}

#[cfg(not(feature = "wasm"))]
fn wasm_compiler(_: Version, _: &std::path::Path) -> Result<corpus::InProcess> {
    bail!("--wasm needs mwcc-tools built with the `wasm` feature")
}
