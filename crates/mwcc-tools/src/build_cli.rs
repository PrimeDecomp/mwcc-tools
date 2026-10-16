use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::build_config::{self, Platform, Request};
use std::path::PathBuf;
pub fn run(command: &str, args: &[String]) -> Result<()> {
    let mut root = std::env::current_dir()?;
    let mut version = build_config::PRIMARY.to_owned();
    let mut mode = None;
    let mut split_only = false;
    let mut oracle = false;
    let mut rust_hooks = false;
    let mut trace_shim = false;
    let mut frontend_version = None;
    let mut rustc = std::env::var_os("RUSTC")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("rustc"));
    let mut timeout = std::time::Duration::from_secs(if command == "download" { 300 } else { 600 });
    let mut ninja = PathBuf::from("ninja");
    let mut jobs = None;
    let mut positional = vec![];
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--split-only" if command == "configure" => split_only = true,
            "--oracle" if command == "configure" => oracle = true,
            "--rust-hooks" if command == "configure" => rust_hooks = true,
            "--trace-shim" if command == "build-hooks" => trace_shim = true,
            "--root" | "--version" | "--mode" | "--timeout" | "--ninja" | "--jobs" | "--rustc"
            | "--frontend-version" => {
                let option = &args[i];
                ensure!(
                    option == "--root"
                        || (command == "configure"
                            && ["--version", "--mode"].contains(&option.as_str()))
                        || (command == "download" && option == "--timeout")
                        || (command == "build-hooks"
                            && ["--rustc", "--timeout", "--frontend-version"]
                                .contains(&option.as_str()))
                        || (["verify", "gates"].contains(&command)
                            && ["--timeout", "--ninja", "--jobs"].contains(&option.as_str())),
                    "unknown {command} argument {option}"
                );
                i += 1;
                let value = args.get(i).context("build option requires a value")?;
                match option.as_str() {
                    "--root" => root = value.into(),
                    "--version" => version = value.clone(),
                    "--ninja" => ninja = value.into(),
                    "--rustc" => rustc = value.into(),
                    "--frontend-version" => frontend_version = Some(value.clone()),
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
            value => bail!("unknown {command} argument {value}"),
        }
        i += 1;
    }
    match command {
        "build-hooks" => {
            ensure!(
                positional.len() == 1,
                "build-hooks OUTPUT [--trace-shim | --frontend-version V] [--root DIR] [--rustc FILE] [--timeout SECS]"
            );
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
                "configure [--version V] [--mode link|compare] [--split-only] [--oracle] [--rust-hooks] [--root DIR]"
            );
            let tools = std::env::current_exe()?.to_string_lossy().into_owned();
            build_config::run(
                &root,
                &Request {
                    version,
                    mode,
                    split_only,
                    oracle,
                    rust_hooks,
                    tools,
                    platform: Platform::current(),
                },
            )?;
        }
        "transform-dep" => {
            ensure!(positional.len() == 2, "transform-dep DEP OUT [--root DIR]");
            build_config::depfile::run(&root, &PathBuf::from(&positional[0]), &positional[1])?;
        }
        _ => bail!("unknown build command {command}"),
    }
    Ok(())
}
