use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{coff, pe};
use serde_json::json;
use std::fs;
fn integer(s: &str) -> Result<u32> {
    Ok(if let Some(s) = s.strip_prefix("0x") {
        u32::from_str_radix(s, 16)?
    } else {
        s.parse()?
    })
}
pub fn run(command: &str, args: &[String]) -> Result<()> {
    match command {
        "disasm" | "fdiff" | "nearest" => {
            let mut root = std::env::current_dir()?;
            let mut positional = vec![];
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--root" => {
                        i += 1;
                        root = args.get(i).context("--root requires DIR")?.into();
                    }
                    value if !value.starts_with('-') => positional.push(value.to_owned()),
                    value => bail!("unknown {command} argument {value}"),
                }
                i += 1;
            }
            let hex = |text: &str| mwcc_oracle_core::executable::hex(text);
            let report = match command {
                "disasm" => {
                    ensure!(
                        (2..=3).contains(&positional.len()),
                        "disasm VERSION ADDRESS [SIZE] [--root DIR]"
                    );
                    mwcc_oracle_core::disassembly::disasm(
                        &root,
                        &positional[0],
                        hex(&positional[1])?,
                        positional
                            .get(2)
                            .map(|s| hex(s).map(|s| s as usize))
                            .transpose()?,
                    )?
                }
                "fdiff" => {
                    ensure!(
                        positional.len() == 4,
                        "fdiff VERSION SOURCE FUNCTION ADDRESS [--root DIR]"
                    );
                    mwcc_oracle_core::disassembly::fdiff(
                        &root,
                        &positional[0],
                        &positional[1],
                        &positional[2],
                        hex(&positional[3])?,
                    )?
                }
                _ => {
                    ensure!(
                        !positional.is_empty(),
                        "nearest VERSION SOURCE... [--root DIR]"
                    );
                    mwcc_oracle_core::disassembly::nearest(&root, &positional[0], &positional[1..])?
                }
            };
            print!("{report}");
        }
        "remap" | "derive-splits" => {
            let mut root = std::env::current_dir()?;
            let mut reference = "GC_1_2_5".to_owned();
            let mut exe = None;
            let mut out = None;
            let mut symbols = false;
            let mut positional = vec![];
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--symbols" if command == "remap" => symbols = true,
                    "--root" | "--reference" | "--exe" | "--out" => {
                        let option = &args[i];
                        ensure!(
                            command == "derive-splits" || option == "--root",
                            "{option} is only supported by derive-splits"
                        );
                        i += 1;
                        let value = args
                            .get(i)
                            .context("binary metadata option needs a value")?;
                        match option.as_str() {
                            "--root" => root = value.into(),
                            "--reference" => reference = value.clone(),
                            "--exe" => exe = Some(std::path::PathBuf::from(value)),
                            _ => out = Some(std::path::PathBuf::from(value)),
                        }
                    }
                    value if !value.starts_with('-') => positional.push(value.to_owned()),
                    value => bail!("unknown {command} argument {value}"),
                }
                i += 1;
            }
            if command == "remap" {
                ensure!(
                    positional.len() == 2,
                    "remap FROM TO [--symbols] [--root DIR]"
                );
                print!(
                    "{}",
                    mwcc_oracle_core::remap_binary::run(
                        &root,
                        &positional[0],
                        &positional[1],
                        symbols
                    )?
                );
            } else {
                ensure!(
                    positional.len() == 1,
                    "derive-splits VERSION [--reference V] [--exe FILE] [--out DIR] [--root DIR]"
                );
                mwcc_oracle_core::derive_splits::run(
                    &root,
                    &positional[0],
                    &reference,
                    exe.as_deref(),
                    out.as_deref(),
                )?;
            }
        }
        "locate" | "bindfrom" | "funcmatch" => {
            let mut root = std::env::current_dir()?;
            let mut positional = vec![];
            let mut json_output = false;
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--root" => {
                        i += 1;
                        root = args.get(i).context("--root requires DIR")?.into();
                    }
                    "--json" if command == "funcmatch" => json_output = true,
                    value if !value.starts_with('-') => positional.push(value.to_owned()),
                    value => bail!("unknown {command} argument {value}"),
                }
                i += 1;
            }
            match command {
                "locate" => {
                    ensure!(
                        !positional.is_empty(),
                        "locate VERSION SOURCE... [--root DIR]"
                    );
                    print!(
                        "{}",
                        mwcc_oracle_core::investigate::locate(
                            &root,
                            &positional[0],
                            &positional[1..]
                        )?
                    );
                }
                "bindfrom" => {
                    ensure!(positional.len() == 1, "bindfrom VERSION [--root DIR]");
                    print!(
                        "{}",
                        mwcc_oracle_core::investigate::bindfrom(&root, &positional[0])?
                    );
                }
                _ => {
                    ensure!(
                        positional.len() == 2,
                        "funcmatch FROM TO [--root DIR] [--json]"
                    );
                    let matches = mwcc_oracle_core::investigate::funcmatch(
                        &root,
                        &positional[0],
                        &positional[1],
                    )?;
                    if json_output {
                        println!(
                            "{}",
                            serde_json::to_string_pretty(
                                &json!({"format_version":1,"result":matches.result,"ambiguous":matches.ambiguous,"total":matches.total})
                            )?
                        );
                    } else {
                        println!(
                            "{} unique, {} ambiguous of {}",
                            matches.result.len(),
                            matches.ambiguous.len(),
                            matches.total
                        );
                    }
                }
            }
        }
        "compare" => {
            let mut root = std::env::current_dir()?;
            let mut positional = vec![];
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--root" => {
                        i += 1;
                        root = args.get(i).context("--root requires DIR")?.into();
                    }
                    value if !value.starts_with('-') => positional.push(value),
                    value => bail!("unknown compare argument {value}"),
                }
                i += 1;
            }
            ensure!(
                positional.len() == 2,
                "compare extract|compare|report VERSION [--root DIR]"
            );
            let version = positional[1];
            match positional[0] {
                "extract" => mwcc_oracle_core::compare::extract(&root, version)?,
                "compare" => {
                    let result = mwcc_oracle_core::compare::compare(&root, version)?;
                    println!("{}", result.summary);
                    if result.failed {
                        std::process::exit(1);
                    }
                }
                "report" => {
                    let root = root.canonicalize()?;
                    let executable = std::env::var_os("OBJDIFF_CLI")
                        .map(std::path::PathBuf::from)
                        .unwrap_or_else(|| {
                            root.join(if cfg!(windows) {
                                "build/tools/objdiff-cli.exe"
                            } else {
                                "build/tools/objdiff-cli"
                            })
                        });
                    let path = format!("build/{version}/report.json");
                    let status = std::process::Command::new(executable)
                        .args(["report", "generate", "-o", &path])
                        .current_dir(&root)
                        .status()?;
                    ensure!(
                        status.success(),
                        "objdiff report generation failed: {status}"
                    );
                    let mut report = serde_json::from_slice(&fs::read(root.join(&path))?)?;
                    mwcc_oracle_core::compare::adjust_report(&mut report)?;
                    mwcc_oracle_core::compare::write_json(&root, &path, &report)?;
                }
                value => bail!("unknown compare action {value}"),
            }
        }
        "link" => {
            let mut root = std::env::current_dir()?;
            let mut oracle = false;
            let mut source = false;
            let mut timeout = std::time::Duration::from_secs(600);
            let mut positional = vec![];
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--oracle" => oracle = true,
                    "--source" => source = true,
                    "--root" | "--timeout" => {
                        let option = &args[i];
                        i += 1;
                        let value = args.get(i).context("link option needs a value")?;
                        if option == "--root" {
                            root = value.into();
                        } else {
                            timeout = std::time::Duration::try_from_secs_f64(value.parse()?)?;
                        }
                    }
                    v => positional.push(v.to_owned()),
                }
                i += 1;
            }
            ensure!(
                positional.len() >= 2,
                "link [--oracle] VERSION OUTPUT INPUT..."
            );
            let version = positional.remove(0);
            let output = positional.remove(0);
            let report = mwcc_oracle_core::link::run(&mwcc_oracle_core::link::Request {
                root,
                version,
                output,
                inputs: positional,
                oracle,
                source,
                record: true,
                timeout,
            })?;
            for message in report.messages {
                eprintln!("{message}");
            }
            ensure!(
                report.identical != Some(false),
                "linked executable differs from its configured SHA-1"
            );
        }
        "relink-check" => {
            let mut root = std::env::current_dir()?;
            let mut version = None;
            let mut write = false;
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--write" => write = true,
                    "--root" => {
                        i += 1;
                        root = args.get(i).context("--root needs a value")?.into();
                    }
                    v if !v.starts_with('-') && version.is_none() => version = Some(v),
                    other => bail!("unknown relink-check argument {other}"),
                }
                i += 1;
            }
            let version = version.context("relink-check requires VERSION")?;
            let tools = std::env::current_exe()?.to_string_lossy().into_owned();
            let outcome = mwcc_oracle_core::relink_check::run(&root, version, &tools, write)?;
            for unit in &outcome.rejected {
                println!("does not link: {unit}");
            }
            println!(
                "{version}: {} units link into the original, {} do not ({} links)",
                outcome.accepted.len(),
                outcome.rejected.len(),
                outcome.links
            );
        }
        "source-only" => {
            let mut root = std::env::current_dir()?;
            let mut version = None;
            let mut oracle = false;
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--root" => {
                        i += 1;
                        root = args.get(i).context("--root needs a value")?.into();
                    }
                    "--oracle" => oracle = true,
                    v if !v.starts_with('-') && version.is_none() => version = Some(v),
                    other => bail!("unknown source-only argument {other}"),
                }
                i += 1;
            }
            let version = version.context("source-only requires VERSION")?;
            if oracle {
                let outcome = mwcc_oracle_core::source_oracle::run(&root, version)?;
                for hook in &outcome.hooks {
                    println!(
                        "hook {} = {} ({}{})",
                        hook.hook,
                        hook.function,
                        hook.unit,
                        if hook.matching { "" } else { ", not matching" }
                    );
                }
                println!(
                    "{version}: {}: {} hooks, {} names read",
                    outcome.output,
                    outcome.hooks.len(),
                    outcome.imports.len()
                );
                return Ok(());
            }
            let outcome = mwcc_oracle_core::source_only::run(&root, version)?;
            for function in &outcome.trapped {
                println!("traps (the original does not have it): {function}");
            }
            println!(
                "{version}: {}: {} objects; {} COMDATs made any, {} duplicate definitions dropped, {} functions trap",
                outcome.output,
                outcome.objects,
                outcome.comdats,
                outcome.duplicates,
                outcome.trapped.len()
            );
        }
        "split" => {
            let mut root = std::env::current_dir()?;
            let mut output = None;
            let mut version = None;
            let mut source = false;
            let mut i = 0;
            while i < args.len() {
                match args[i].as_str() {
                    "--source" => source = true,
                    "--root" | "--output" => {
                        let option = &args[i];
                        i += 1;
                        let value = args.get(i).context("split option needs a value")?;
                        if option == "--root" {
                            root = value.into();
                        } else {
                            output = Some(std::path::PathBuf::from(value));
                        }
                    }
                    v if !v.starts_with('-') && version.is_none() => version = Some(v),
                    other => bail!("unknown split argument {other}"),
                }
                i += 1;
            }
            let version = version.context("split requires VERSION")?;
            if source {
                mwcc_oracle_core::split::run_source(&root, version, output.as_deref())?;
            } else {
                mwcc_oracle_core::split::run(&root, version, output.as_deref())?;
            }
        }
        "implib" => {
            ensure!(args.len() == 2, "implib requires DEF OUTPUT");
            let (dll, exports) = coff::definition(&fs::read_to_string(&args[0])?)?;
            fs::write(&args[1], coff::import_library(&dll, &exports)?)?;
        }
        "rsrc2res" => {
            ensure!(args.len() == 2, "rsrc2res requires EXE OUTPUT");
            let data = fs::read(&args[0])?;
            let pe = pe::Pe::parse(&data)?;
            let section = pe
                .sections
                .iter()
                .find(|s| s.name == ".rsrc")
                .context("PE image has no .rsrc section")?;
            let entries = pe::resources(&pe, section)?;
            fs::write(&args[1], pe::res_file(&entries)?)?;
        }
        "pe" => {
            let subcommand = args.first().context("pe requires inspect, read or find")?;
            let file = args.get(1).context("pe requires FILE")?;
            let data = fs::read(file)?;
            let pe = pe::Pe::parse(&data)?;
            let output = match subcommand.as_str() {
                "inspect" => {
                    ensure!(args.len() == 2, "pe inspect requires FILE");
                    json!({"image_base":pe.image_base,"sections":pe.sections})
                }
                "read" => {
                    ensure!(args.len() == 4, "pe read requires FILE ADDRESS SIZE");
                    let data = pe.read(integer(&args[2])?, integer(&args[3])? as usize)?;
                    json!({"bytes":data.iter().map(|b|format!("{b:02x}")).collect::<String>()})
                }
                "find" => {
                    let needle = args
                        .get(2)
                        .context("pe find requires FILE HEX [--limit N]")?;
                    ensure!(
                        needle.is_ascii() && needle.len().is_multiple_of(2),
                        "HEX must contain byte pairs"
                    );
                    let needle = (0..needle.len())
                        .step_by(2)
                        .map(|i| u8::from_str_radix(&needle[i..i + 2], 16))
                        .collect::<std::result::Result<Vec<_>, _>>()?;
                    let limit = match args.len() {
                        3 => None,
                        5 if args[3] == "--limit" => Some(args[4].parse()?),
                        _ => bail!("pe find requires FILE HEX [--limit N]"),
                    };
                    json!({"addresses":pe.find(&needle,limit)})
                }
                _ => bail!("unknown pe command {subcommand}"),
            };
            println!("{}", serde_json::to_string_pretty(&output)?);
        }
        _ => unreachable!(),
    }
    Ok(())
}
