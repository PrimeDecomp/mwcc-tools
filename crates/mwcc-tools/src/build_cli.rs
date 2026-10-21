use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::build_config::{self, Platform, Request};
use std::path::PathBuf;
/// Each build command's usage (`COMMAND --help`).
fn usage(command: &str) -> &'static str {
    match command {
        "configure" => {
            "configure [--version V] [--mode link|compare] [--split-only] [--oracle [--producer rust|c]] [--root DIR]
    writes build.ninja for the version (default GC_1_2_5); for GC 2.6 and 2.7 its default targets include the
    WebAssembly modules: build/V/wasm/mwcc.wasm (the module every in-process compile and trace runs: gc2's sources
    with ORACLE_PROBES and the Rust hooks), mwcceppc.wasm (the stock compiler), oracle.wasm and persist.wasm (mwdec's)
    --oracle   also the native oracle, build/V/oracle/mwcceppc.exe (the original's code with the hooks linked in)"
        }
        "probe-build" => {
            "probe-build [--version V] [--from CHECKOUT] [--root DIR] [--ninja FILE] [--timeout SECS]
    builds the in-process module of a checkout with changed gc2 sources (a probe of your own: docs/wasm.md):
    copies build/compilers and build/tools from CHECKOUT (another checkout that has downloaded them) when the root
    has none, configures the version (default GC_2_7) and builds build/V/wasm/mwcc.wasm, which mwcc-tools in that
    root runs in process for every compile and trace of the version"
        }
        "build-hooks" => {
            "build-hooks OUTPUT [--trace-shim | --frontend-version V] [--wasm] [--root DIR] [--rustc FILE] [--timeout SECS]"
        }
        "verify" | "gates" => "verify|gates [--root DIR] [--ninja FILE] [--jobs N] [--timeout SECS]",
        "download" => "download TOOL OUTPUT [OTHER_OUTPUT...] [--root DIR] [--timeout SECS]",
        "transform-dep" => "transform-dep DEP OUT [--root DIR]",
        _ => "",
    }
}

pub fn run(command: &str, args: &[String]) -> Result<()> {
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!("{}", usage(command));
        return Ok(());
    }
    let mut root = std::env::current_dir()?;
    let mut version = build_config::PRIMARY.to_owned();
    let mut mode = None;
    let mut split_only = false;
    let mut oracle = false;
    let mut producer = None;
    let mut trace_shim = false;
    let mut wasm = false;
    let mut frontend_version = None;
    let mut rustc = std::env::var_os("RUSTC")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("rustc"));
    let mut timeout = std::time::Duration::from_secs(if command == "download" { 300 } else { 600 });
    let mut ninja = PathBuf::from("ninja");
    let mut jobs = None;
    let mut from = None;
    let mut positional = vec![];
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--split-only" if command == "configure" => split_only = true,
            "--oracle" if command == "configure" => oracle = true,
            "--trace-shim" if command == "build-hooks" => trace_shim = true,
            "--wasm" if command == "build-hooks" => wasm = true,
            "--root" | "--version" | "--mode" | "--timeout" | "--ninja" | "--jobs" | "--rustc"
            | "--frontend-version" | "--producer" | "--from" => {
                let option = &args[i];
                ensure!(
                    option == "--root"
                        || (command == "configure"
                            && ["--version", "--mode", "--producer"].contains(&option.as_str()))
                        || (command == "download" && option == "--timeout")
                        || (command == "build-hooks"
                            && ["--rustc", "--timeout", "--frontend-version"]
                                .contains(&option.as_str()))
                        || (["verify", "gates"].contains(&command)
                            && ["--timeout", "--ninja", "--jobs"].contains(&option.as_str()))
                        || (command == "probe-build"
                            && ["--version", "--from", "--ninja", "--timeout"]
                                .contains(&option.as_str())),
                    "unknown {command} argument {option}"
                );
                i += 1;
                let value = args.get(i).context("build option requires a value")?;
                match option.as_str() {
                    "--root" => root = value.into(),
                    "--version" => version = value.clone(),
                    "--ninja" => ninja = value.into(),
                    "--rustc" => rustc = value.into(),
                    "--from" => from = Some(PathBuf::from(value)),
                    "--frontend-version" => frontend_version = Some(value.clone()),
                    "--producer" => {
                        let rust = match value.as_str() {
                            "rust" => true,
                            "c" => false,
                            _ => bail!("--producer must be rust or c"),
                        };
                        ensure!(
                            producer.is_none_or(|old| old == rust),
                            "conflicting producer choices"
                        );
                        producer = Some(rust);
                    }
                    "--jobs" => {
                        let n = value.parse::<usize>()?;
                        ensure!(n > 0, "jobs must be positive");
                        jobs = Some(n);
                    }
                    "--timeout" => {
                        timeout = std::time::Duration::try_from_secs_f64(value.parse()?)
                            .context("timeout must be finite and positive")?;
                        ensure!(!timeout.is_zero(), "timeout must be positive");
                    }
                    _ => mode = Some(value.clone()),
                }
            }
            value if !value.starts_with('-') => positional.push(value.to_owned()),
            value => bail!("unknown {command} argument {value}; see {command} --help"),
        }
        i += 1;
    }
    match command {
        "build-hooks" => {
            ensure!(
                positional.len() == 1,
                "build-hooks OUTPUT [--trace-shim | --frontend-version V] [--wasm] [--root DIR] [--rustc FILE] [--timeout SECS]"
            );
            if wasm {
                ensure!(
                    !trace_shim,
                    "the WebAssembly oracle writes traces in C (wasm/src/oracle.c)"
                );
                mwcc_oracle_core::producer::build_wasm(
                    &root,
                    &PathBuf::from(&positional[0]),
                    &rustc,
                    timeout,
                    frontend_version.as_deref(),
                )?;
                return Ok(());
            }
            ensure!(
                !trace_shim || frontend_version.is_none(),
                "trace output and frontend must be separate objects"
            );
            if let Some(version) = frontend_version {
                mwcc_oracle_core::producer::build_frontend(
                    &root,
                    &PathBuf::from(&positional[0]),
                    &rustc,
                    timeout,
                    &version,
                )?;
                return Ok(());
            }
            let build = if trace_shim {
                mwcc_oracle_core::producer::build_trace
            } else {
                mwcc_oracle_core::producer::build
            };
            build(&root, &PathBuf::from(&positional[0]), &rustc, timeout)?;
        }
        "verify" | "gates" => {
            ensure!(
                positional.is_empty(),
                "{command} [--root DIR] [--ninja FILE] [--jobs N] [--timeout SECS]"
            );
            let report = mwcc_oracle_core::build_verify::run(
                &mwcc_oracle_core::build_verify::Request {
                    root,
                    mode: if command == "verify" {
                        mwcc_oracle_core::build_verify::Mode::Verify
                    } else {
                        mwcc_oracle_core::build_verify::Mode::Gates
                    },
                    tools: std::env::current_exe()?.to_string_lossy().into_owned(),
                    ninja,
                    jobs,
                    timeout,
                },
                |step| println!("{}", step.text),
            )?;
            if let Some(error) = report.restoration_error {
                eprintln!("{error}");
            }
            ensure!(report.passed, "{command} failed; see version results above");
        }
        "download" => {
            ensure!(
                positional.len() >= 2,
                "download TOOL OUTPUT [OTHER_OUTPUT...] [--root DIR] [--timeout SECS]"
            );
            mwcc_oracle_core::download::run(
                &root,
                &positional[0],
                &PathBuf::from(&positional[1]),
                timeout,
            )?;
        }
        "configure" => {
            ensure!(
                positional.is_empty(),
                "configure [--version V] [--mode link|compare] [--split-only] [--oracle [--producer rust|c]] [--root DIR]"
            );
            ensure!(producer.is_none() || oracle, "--producer requires --oracle");
            let tools = std::env::current_exe()?.to_string_lossy().into_owned();
            build_config::run(
                &root,
                &Request {
                    version,
                    mode,
                    split_only,
                    oracle,
                    rust_hooks: producer.unwrap_or(oracle),
                    tools,
                    platform: Platform::current(),
                },
            )?;
        }
        "probe-build" => {
            ensure!(positional.is_empty(), "{}", usage(command));
            if version == build_config::PRIMARY {
                version = "GC_2_7".into();
            }
            probe_build(&root, &version, from.as_deref(), &ninja, timeout)?;
        }
        "transform-dep" => {
            ensure!(positional.len() == 2, "transform-dep DEP OUT [--root DIR]");
            build_config::depfile::run(&root, &PathBuf::from(&positional[0]), &positional[1])?;
        }
        _ => bail!("unknown build command {command}"),
    }
    Ok(())
}

/// `probe-build`: the root's in-process module, built from its gc2 sources (see [`usage`]).
fn probe_build(
    root: &std::path::Path,
    version: &str,
    from: Option<&std::path::Path>,
    ninja: &std::path::Path,
    timeout: std::time::Duration,
) -> Result<()> {
    for downloaded in ["build/compilers", "build/tools"] {
        let target = root.join(downloaded);
        if target.exists() {
            continue;
        }
        let Some(from) = from else {
            continue;
        };
        let source = from.join(downloaded);
        ensure!(
            source.is_dir(),
            "{} has no {downloaded} to copy",
            from.display()
        );
        println!("copying {} to {}", source.display(), target.display());
        copy_tree(&source, &target)?;
    }
    let tools = std::env::current_exe()?.to_string_lossy().into_owned();
    build_config::run(
        root,
        &Request {
            version: version.to_owned(),
            mode: None,
            split_only: false,
            oracle: false,
            rust_hooks: false,
            tools,
            platform: Platform::current(),
        },
    )?;
    let module = mwcc_project::compilers::module_output(version);
    let mut child = std::process::Command::new(ninja)
        .arg(&module)
        .current_dir(root)
        .spawn()
        .with_context(|| format!("running {}", ninja.display()))?;
    let started = std::time::Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break status;
        }
        if started.elapsed() > timeout {
            let _ = child.kill();
            bail!("ninja {module}: timed out after {:?}", timeout);
        }
        std::thread::sleep(std::time::Duration::from_millis(200));
    };
    ensure!(status.success(), "ninja {module} failed");
    println!("{}", root.join(&module).display());
    Ok(())
}

fn copy_tree(from: &std::path::Path, to: &std::path::Path) -> Result<()> {
    std::fs::create_dir_all(to)?;
    for entry in std::fs::read_dir(from)? {
        let entry = entry?;
        let target = to.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &target)?;
        } else {
            std::fs::copy(entry.path(), &target)
                .with_context(|| format!("copying {}", entry.path().display()))?;
        }
    }
    Ok(())
}
