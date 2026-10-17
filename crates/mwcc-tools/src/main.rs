use anyhow::{Context, Result, bail};
use mwcc_oracle_core::{
    allocation, corpus, document, elf, ninja, report,
    runner::{self, BlockLimit, CompileRequest, TraceOptions},
    trace,
    version::Version,
};
use serde_json::json;
use std::{env, fs, path::PathBuf, process::ExitCode, time::Duration};
mod binary_cli;
mod build_cli;
mod calibrate_cli;
mod colouring_cli;
mod header_impact_cli;
mod kit_cli;
mod provenance_cli;
mod release_cli;
mod source_arenas_cli;
mod source_index_cli;
mod source_nodes_cli;
mod source_reduce_cli;
mod source_repro_cli;
mod source_search_cli;
mod tu_cli;

struct ParsedCompile {
    request: CompileRequest,
    function: Option<String>,
    link: Option<String>,
    near: Option<i64>,
    json_output: bool,
    pcode: bool,
    target_asm: Option<PathBuf>,
    inline_cost: Option<String>,
    instantiation: Option<String>,
    cse_view: bool,
    ir: Option<String>,
    exchange: Option<String>,
    raw: bool,
}

fn parse_compile(args: &[String], schema_six: bool) -> Result<ParsedCompile> {
    let mut root = env::var_os("MWCC_ORACLE_ROOT")
        .map(PathBuf::from)
        .unwrap_or(env::current_dir()?);
    let mut directory = env::current_dir()?;
    let mut from_ninja = None;
    let mut source = None;
    let mut flags = vec![];
    let mut version = None;
    let mut check = true;
    let mut options = TraceOptions::default();
    let mut overrides = vec![];
    let mut keep_object = None;
    let mut cut_limit = None;
    let mut merge_limit = None;
    let mut timeout = Duration::from_secs(600);
    let mut original_fallback = false;
    let mut function = None;
    let mut link = None;
    let mut near = None;
    let mut json_output = false;
    let mut pcode = false;
    let mut target = None;
    let mut inline_cost = None;
    let mut instantiation = None;
    let mut cse_view = false;
    let mut ir = None;
    let mut exchange = None;
    let mut raw = false;
    let mut i = 0;
    while i < args.len() {
        let option = args[i].as_str();
        let needs_value = matches!(
            option,
            "--oracle-root"
                | "--working-directory"
                | "--from-ninja"
                | "--version"
                | "--override"
                | "--keep-object"
                | "--cut-limit"
                | "--merge-limit"
                | "--timeout"
                | "--function"
                | "--link"
                | "--near"
                | "--ir"
                | "--exchange"
                | "--target"
        ) || schema_six && matches!(option, "--inline-cost" | "--instantiation");
        if needs_value {
            let v = args
                .get(i + 1)
                .with_context(|| format!("{option} needs a value"))?;
            match option {
                "--oracle-root" => root = v.into(),
                "--working-directory" => directory = v.into(),
                "--from-ninja" => from_ninja = Some(PathBuf::from(v)),
                "--version" => version = Some(Version::parse(v)?),
                "--override" => overrides.push(v.clone()),
                "--keep-object" => keep_object = Some(v.into()),
                "--cut-limit" => cut_limit = Some(BlockLimit::parse(v)?),
                "--merge-limit" => merge_limit = Some(BlockLimit::parse(v)?),
                "--timeout" => {
                    timeout = Duration::try_from_secs_f64(v.parse()?)
                        .context("timeout must be finite and positive")?
                }
                "--function" => function = Some(v.clone()),
                "--link" => link = Some(v.clone()),
                "--near" => {
                    near = Some(v.parse()?);
                    options.inline_cost = true;
                }
                "--ir" => ir = (!v.is_empty()).then(|| v.clone()),
                "--exchange" => exchange = (!v.is_empty()).then(|| v.clone()),
                "--inline-cost" => {
                    options.inline_cost = !v.is_empty();
                    inline_cost = (!v.is_empty()).then(|| v.clone());
                }
                "--instantiation" => {
                    options.instantiation = !v.is_empty();
                    instantiation = (!v.is_empty()).then(|| v.clone());
                }
                "--target" => target = Some(PathBuf::from(v)),
                _ => unreachable!(),
            }
            i += 2;
            continue;
        }
        match option {
            "--" => {
                flags.extend_from_slice(&args[i + 1..]);
                break;
            }
            "--no-check" => check = false,
            "--original-fallback" => original_fallback = true,
            "--schedule" => options.schedule = true,
            "--inline-cost" => options.inline_cost = true,
            "--full-cost" => options.full_cost = true,
            "--instantiation" => options.instantiation = true,
            "--locals" => options.locals = true,
            "--blocks" => options.blocks = true,
            "--cse" => {
                options.cse = true;
                cse_view = true;
            }
            "--stages" => options.stages = true,
            "--backend" => options.backend = true,
            "--source-arenas" => options.source_arenas = true,
            "--source-nodes" => {
                options.source_nodes = true;
                options.source_arenas = true;
            }
            "--json" => json_output = true,
            "--pcode" => pcode = true,
            "--raw" => raw = true,
            unknown if unknown.starts_with("--") && source.is_none() => {
                bail!("unknown compile-trace option {unknown}")
            }
            value if source.is_none() && !value.starts_with('-') => source = Some(value.to_owned()),
            value if source.is_some() => flags.push(value.into()),
            _ => bail!("compiler flags must follow SOURCE or --"),
        }
        i += 1;
    }
    let mut source = source.context("compile-trace requires SOURCE")?;
    options.cse |= ir.is_some() || exchange.is_some();
    options.inline_cost |= near.is_some();
    let target_directory = target.or_else(|| {
        from_ninja
            .as_deref()
            .and_then(|p| mwcc_oracle_core::project::target_directory(p).ok())
    });
    if let Some(path) = from_ninja {
        anyhow::ensure!(
            flags.is_empty(),
            "--from-ninja cannot be combined with compiler flags"
        );
        let (rule, cwd) = ninja::find_rule(&path, &source)?;
        let inferred = rule.mw_version.as_deref().map(Version::parse).transpose()?;
        if version.is_some() && inferred.is_some() && version != inferred {
            eprintln!(
                "version override differs from the Ninja rule; using explicitly requested version"
            );
        }
        version = version.or(inferred);
        flags = rule.flags;
        source = rule.source;
        directory = cwd;
    }
    let target_asm = if options.locals {
        target_directory.as_deref().and_then(|p| {
            mwcc_oracle_core::project::target_asm(p, &source)
                .ok()
                .flatten()
        })
    } else {
        None
    };
    let request = CompileRequest {
        root,
        working_directory: directory,
        source,
        flags,
        version: version.unwrap_or(Version::Gc132),
        options,
        overrides,
        check,
        original_fallback,
        cut_limit,
        merge_limit,
        keep_object,
        timeout,
    };
    Ok(ParsedCompile {
        request,
        function,
        link,
        near,
        json_output,
        pcode,
        target_asm,
        inline_cost,
        instantiation,
        cse_view,
        ir,
        exchange,
        raw,
    })
}

fn compile_trace(args: &[String], schema_six: bool) -> Result<()> {
    let ParsedCompile {
        request,
        function,
        link,
        near,
        json_output,
        pcode,
        target_asm,
        inline_cost,
        instantiation,
        cse_view,
        ir,
        exchange,
        raw,
    } = parse_compile(args, schema_six)?;
    let compiled = runner::compile(&request)?;
    let parsed = trace::parse(&compiled.trace)?;
    let selection = document::Selection {
        function: function.as_deref(),
        link: link.as_deref(),
        near,
    };
    let mut report = document::build(
        &parsed,
        compiled.version,
        &compiled.source,
        &compiled.oracle,
        compiled.identical,
        &compiled.differences,
        &selection,
    )?;
    let raw_lines = if raw {
        Some(
            if function.as_ref().is_some_and(|f| !f.is_empty()) || link.is_some() {
                trace::raw_selected(&compiled.trace, function.as_deref(), link.as_deref())
            } else {
                compiled.trace.lines().map(str::to_owned).collect()
            },
        )
    } else {
        None
    };
    if json_output && let Some(raw) = &raw_lines {
        report["raw"] = json!(raw);
    }
    if schema_six && !json_output {
        let object_check = match compiled.identical {
            Some(true) => "; object identical to the original compiler's: yes".to_owned(),
            Some(false) => {
                "; object identical to the original compiler's: NO - the oracle differs".to_owned()
            }
            None => match compiled.check_status {
                runner::CheckStatus::Skipped => "; object check skipped".into(),
                _ => "; object check unavailable".into(),
            },
        };
        let mut limits = String::new();
        if request.cut_limit.is_some() || request.merge_limit.is_some() {
            let limit = |value: Option<BlockLimit>| match value {
                Some(BlockLimit::Count(n)) => n.to_string(),
                Some(BlockLimit::Unlimited) => "none".into(),
                None => "100".into(),
            };
            limits = format!(
                "; block limits patched (both compilers): cut {}, merge {}",
                limit(request.cut_limit),
                limit(request.merge_limit)
            );
        }
        println!(
            "oracle: {} ({}); {}{limits}{object_check}",
            compiled.oracle.replace('/', std::path::MAIN_SEPARATOR_STR),
            compiled.version.name(),
            if compiled.original_fallback {
                "the original compiler (no oracle hooks)"
            } else {
                "the original's code with trace wrappers"
            }
        );
        if compiled.identical == Some(false) {
            println!(
                "WARNING: the oracle's object differs from the original compiler's; the explanation may not hold"
            );
            for d in &compiled.differences {
                println!("   {d}");
            }
        }
        if let Some(raw) = &raw_lines {
            println!("{}", raw.join("\n"));
        }
        if let Some(margin) = near {
            println!(
                "{}",
                report::near(report["near"].as_array().unwrap(), margin).join("\n")
            );
            return Ok(());
        }
        if let Some(query) = instantiation {
            let events = mwcc_oracle_core::document::instantiation(&parsed, compiled.version)?;
            println!("{}", report::instantiation(&events, &query).join("\n"));
            return Ok(());
        }
        if cse_view || ir.is_some() {
            let functions = mwcc_oracle_core::cse::parse(&parsed.records)?;
            let mut shown = false;
            for (l, f) in functions {
                if !f.passes.is_empty()
                    && function.as_ref().is_none_or(|q| l.contains(q))
                    && link.as_ref().is_none_or(|q| l == *q)
                {
                    shown = true;
                    println!(
                        "{}",
                        mwcc_oracle_core::cse::report(
                            &l,
                            &f,
                            mwcc_oracle_core::version::enode_names(),
                            compiled.version,
                            ir.as_deref()
                        )
                        .join("\n")
                    );
                }
            }
            if !shown {
                println!(
                    "no common subexpression trace for that function (an oracle with the ce.* hooks is needed)"
                );
            }
            return Ok(());
        }
        if request.options.blocks {
            let functions = mwcc_oracle_core::blocks::parse(&parsed.records)?;
            let mut shown = false;
            let limit = match request.cut_limit {
                Some(BlockLimit::Unlimited) => None,
                Some(BlockLimit::Count(n)) => Some(i32::from(n)),
                None => Some(100),
            };
            for (l, f) in functions {
                if function.as_ref().is_none_or(|q| l.contains(q))
                    && link.as_ref().is_none_or(|q| l == *q)
                {
                    shown = true;
                    println!(
                        "{}",
                        mwcc_oracle_core::blocks::report(
                            &l,
                            &f,
                            mwcc_oracle_core::version::opcode_names(compiled.version),
                            pcode,
                            limit
                        )
                        .join("\n")
                    );
                }
            }
            if !shown {
                println!(
                    "no block trace for that function (an oracle with the bk.* hooks is needed)"
                );
            }
            return Ok(());
        }
        if request.options.locals {
            use mwcc_oracle_core::{locals, ppc};
            let passes = locals::scalarize(&parsed.records)?;
            let bindings = locals::bindings(&parsed.records)?;
            let target_text = target_asm
                .as_deref()
                .map(fs::read)
                .transpose()?
                .map(|bytes| String::from_utf8_lossy(&bytes).into_owned());
            for a in report["functions"].as_array().unwrap() {
                let link = a["link"].as_str().unwrap_or("");
                let p = passes.get(link).map_or(&[][..], Vec::as_slice);
                let b = bindings.get(link).map_or(&[][..], Vec::as_slice);
                let target = target_text.as_deref().and_then(|s| locals::frame(s, link));
                println!("{}", locals::report(a, p, target.as_ref(), b).join("\n"));
                let ours = ppc::function_words(&compiled.object, link)?;
                let theirs = target_text
                    .as_deref()
                    .and_then(|s| ppc::target_words(s, link));
                let mut runs = vec![];
                if let (Some(ours), Some(theirs), Some(target)) =
                    (ours.filter(|w| !w.is_empty()), theirs, target)
                    && !a["layout"].is_null()
                {
                    let layout = &a["layout"];
                    let limit = ["linkage", "outgoing", "locals"]
                        .iter()
                        .map(|k| layout[k].as_i64().unwrap_or(0))
                        .sum();
                    let frame = a["frame"].as_array().map_or(&[][..], Vec::as_slice);
                    println!(
                        "{}",
                        locals::shift_report(
                            &locals::slot_shifts(&ours, &theirs, frame, limit, target.limit),
                            frame
                        )
                        .join("\n")
                    );
                    let (r, mine) =
                        locals::unpaired_objects(&ours, &theirs, frame, limit, target.limit);
                    println!("{}", locals::unpaired_report(&r, &mine).join("\n"));
                    runs = r;
                }
                println!("{}", report::frame(a).join("\n"));
                let summary = locals::summary(a, p, b, &runs);
                if !summary.is_empty() {
                    println!("{}", summary.join("\n"));
                }
            }
            return Ok(());
        }
        let cse_origins = if exchange.is_some() {
            Some(mwcc_oracle_core::cse::parse(&parsed.records)?)
        } else {
            None
        };
        let functions = report["functions"].as_array().unwrap();
        let about = |d: &&serde_json::Value| {
            inline_cost.as_ref().is_none_or(|query| {
                d["event"] == "decision"
                    && (d["callee"].as_str().is_some_and(|s| s.contains(query))
                        || d["link"].as_str().is_some_and(|s| s.contains(query)))
            })
        };
        for analysis in functions {
            if let Some(query) = &inline_cost {
                let decisions: Vec<_> = analysis["inlining"]
                    .as_array()
                    .unwrap()
                    .iter()
                    .filter(about)
                    .cloned()
                    .collect();
                if !decisions.is_empty() {
                    println!(
                        "== {} ({}): inlining {query} ==",
                        analysis["function"].as_str().unwrap_or("?"),
                        analysis["link"].as_str().unwrap_or("?")
                    );
                    println!("{}", report::inlining(&decisions, true).join("\n"));
                    for d in decisions {
                        let cost = report::cost(&d, mwcc_oracle_core::version::enode_names());
                        if !cost.is_empty() {
                            println!("{}", cost.join("\n"));
                        }
                    }
                }
                continue;
            }
            if let Some(query) = &exchange {
                let (a, b) = query.split_once(',').context("--exchange needs A,B")?;
                println!("{}", report::exchange(analysis, a, b).join("\n"));
                let mut origins = vec![];
                if let Some(record) = cse_origins
                    .as_ref()
                    .and_then(|f| f.get(analysis["link"].as_str().unwrap_or("")))
                {
                    for text in [a, b] {
                        let text = text.trim();
                        let text = text.strip_prefix('%').unwrap_or(text);
                        let (class, digits) = if let Some(d) = text.strip_prefix("cr") {
                            ("CR", d)
                        } else if let Some(d) = text.strip_prefix('r') {
                            ("GPR", d)
                        } else if let Some(d) = text.strip_prefix('f') {
                            ("FPR", d)
                        } else if let Some(d) = text.strip_prefix('v') {
                            ("VR", d)
                        } else {
                            ("GPR", text)
                        };
                        if !digits.is_empty()
                            && digits.chars().all(|c| c.is_ascii_digit())
                            && let Ok(number) = digits.parse::<u32>()
                            && let Some(origin) = mwcc_oracle_core::cse::origin(
                                record,
                                class,
                                number,
                                compiled.version,
                                mwcc_oracle_core::version::enode_names(),
                            )
                        {
                            origins.push(format!("   %{digits} holds {origin}"));
                        }
                    }
                }
                if !origins.is_empty() {
                    println!("   what they hold:");
                    println!("{}", origins.join("\n"));
                }
                continue;
            }
            if let Some(decisions) = analysis["inlining"].as_array().filter(|v| !v.is_empty()) {
                println!(
                    "== {} ({}): inlining ==",
                    analysis["function"].as_str().unwrap_or("?"),
                    analysis["link"].as_str().unwrap_or("?")
                );
                println!("{}", report::inlining(decisions, false).join("\n"));
            }
            if pcode
                && let Some(a) = parsed.allocations.iter().find(|a| {
                    analysis["function"] == a.before.function && analysis["link"] == a.before.link
                })
            {
                println!(
                    "{}",
                    report::pcode(
                        &a.before,
                        mwcc_oracle_core::version::opcode_names(compiled.version)
                    )
                    .join("\n")
                );
            }
            println!("{}", report::allocation(analysis).join("\n"));
            let frame = report::frame(analysis);
            if !frame.is_empty() {
                println!("{}", frame.join("\n"));
            }
            if request.options.schedule {
                println!(
                    "{}",
                    report::schedule(
                        analysis["function"].as_str().unwrap_or("?"),
                        analysis["schedule"].as_array().unwrap()
                    )
                    .join("\n")
                );
            }
        }
        if let Some(after) = report["inlining_after_last_function"]
            .as_array()
            .filter(|v| !v.is_empty() && exchange.is_none())
        {
            let after: Vec<_> = after.iter().filter(about).cloned().collect();
            if !after.is_empty() {
                println!("== inlining after the last allocated function ==");
                if inline_cost.is_some() {
                    for d in after {
                        println!(
                            "{}",
                            report::inlining(std::slice::from_ref(&d), true).join("\n")
                        );
                        let cost = report::cost(&d, mwcc_oracle_core::version::enode_names());
                        if !cost.is_empty() {
                            println!("{}", cost.join("\n"));
                        }
                    }
                } else {
                    println!("{}", report::inlining(&after, false).join("\n"));
                }
            }
        }
        if let Some(query) = function.filter(|_| functions.is_empty()) {
            println!(
                "no function matches {query}; the traced functions most like it (name, link name):"
            );
            for similar in report["similar"].as_array().into_iter().flatten() {
                println!(
                    "   {}  {}",
                    similar["function"].as_str().unwrap_or("?"),
                    similar["link"].as_str().unwrap_or("?")
                );
            }
        } else if parsed.allocations.is_empty() && parsed.inliner.is_empty() {
            println!(
                "{}",
                if compiled.original_fallback {
                    "no trace: compiled with the original compiler (no oracle hooks)"
                } else {
                    "no trace: the source produced no function code"
                }
            );
        }
        return Ok(());
    }
    println!(
        "{}",
        serde_json::to_string_pretty(&if schema_six {
            report
        } else {
            json!({"format_version": 1, "compilation": compiled, "report": report})
        })?
    );
    Ok(())
}

fn colouring_command(args: &[String]) -> Result<()> {
    colouring_cli::run(args)
}

fn corpus_command(args: &[String]) -> Result<()> {
    let mut root = env::var_os("MWCC_ORACLE_ROOT")
        .map(PathBuf::from)
        .unwrap_or(env::current_dir()?);
    let mut version = Version::Gc132;
    let mut canaries = None;
    let mut project = None;
    let mut keep = None;
    let mut exe = None;
    let mut limit = None;
    let mut list = None;
    let mut jobs = std::thread::available_parallelism().map_or(4, usize::from);
    let mut all_traces = false;
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
        let option = args[i].as_str();
        let value = args
            .get(i + 1)
            .with_context(|| format!("{option} needs a value"))?;
        match option {
            "--oracle-root" => root = value.into(),
            "--version" => version = Version::parse(value)?,
            "--canaries" => canaries = Some(PathBuf::from(value)),
            "--prime" | "--project" => project = Some(PathBuf::from(value)),
            "--keep" => keep = Some(PathBuf::from(value)),
            "--exe" => exe = Some(PathBuf::from(value)),
            "--limit" => limit = Some(value.parse::<usize>()?),
            "--list" => list = Some(PathBuf::from(value)),
            "--jobs" => jobs = value.parse()?,
            "--timeout" => timeout = Duration::try_from_secs_f64(value.parse()?)?,
            _ => bail!("unknown corpus option {option}"),
        }
        i += 2;
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
    let report = corpus::run(&corpus::Request {
        root,
        version,
        executable: exe,
        jobs: inputs,
        parallelism: jobs,
        keep,
        timeout,
        all_traces,
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

  oracle SOURCE [--json] [--oracle-root ROOT] [--from-ninja FILE] [--version V] [view options] -- COMPILER FLAGS
  oracle analyze-trace TRACE [--version V] [--function NAME] [--link NAME] [--raw] [--allocation-only | --events]
  oracle compile-trace SOURCE [options] -- COMPILER FLAGS
  provenance inspect TRACE_OR_CAPTURE.json [--link NAME] [--json]
  source-arenas inspect TRACE_OR_CAPTURE.json [--json]
  source-arenas resolve TRACE_OR_CAPTURE.json --position LINE --pool POOL --address HEX --extent BYTES
  source-arenas resolve-many TRACE_OR_CAPTURE.json --queries QUERIES.json
  source-nodes inspect TRACE_OR_CAPTURE.json [--json]
  source-nodes snapshot|write|clone TRACE_OR_CAPTURE.json ID
  provenance compare FIRST SECOND --link NAME [--second-link NAME] [--class 4] [--first-round 0] [--second-round 0] [--version GC_2_7] [--json]
  provenance audit-counters|audit-dispatch|audit-source-arenas --root ORACLE_ROOT --version GC_2_7
  oracle run-unit --project DIR --unit NAME [--candidate FILE] [--fast] -- VIEW OPTIONS
  oracle regdiff --project DIR --unit NAME --function QUERY [--full] [--json]
  colouring verify|target|force|search|replay|remap [SOURCE | --load PREFIX] [options]
  colouring unit --project DIR --unit NAME COMMAND [options]
  source-search run PLAN --output DIR [--resume] [--candidates N] [--compilations N] [--seconds N] [--cancel-file FILE]
  source-search replay DIR CANDIDATE_ID
  source-search state CAPTURE --object FILE --link NAME [--class 4] [--round 0]
  source-search evaluate CONTRACT CAPTURE --source FILE --object FILE [--baseline-capture FILE --baseline-source FILE --baseline-object FILE]
  source-reduce run PLAN --output DIR [--resume] [--candidates N] [--compilations N] [--seconds N] [--cancel-file FILE]
  source-reduce replay DIR CANDIDATE_ID
  source-repro export search|reduction RUN CANDIDATE_ID --output NEW [--run-analyzer FILE]
  source-repro verify BUNDLE [--expected-sha256 HASH]
  source-repro replay BUNDLE --output NEW [--seconds N] [--compilations N] [--cancel-file FILE]
  source-index ingest SPEC --index DIR
  source-index origins --index DIR [--link NAME] [--version VERSION]
  source-index query QUERY --index DIR
  source-index verify QUERY ENTRY_ID --index DIR --output DIR [--seconds N] [--cancel-file FILE]
  inline-diff A [B] [--from-ninja FILE] [--target [DIR]] [--all] [--json]
  inline-window --from-ninja FILE UNIT... [--target DIR] [--verbose] [--json]
  data-diff SOURCE [--from-ninja FILE] [--target [DIR]] [--section NAME] [--all] [--json]
  header-impact --from-ninja FILE (--git REV | --override PATH=FILE) [--unit TEXT] [--jobs N] [--json]
  inline-calibrate --project DIR --callee NAME [--unit UNIT] [--binary] [--json FILE]
  corpus --canaries DIR | --project DIR [--oracle-root ROOT] [--version V] [--jobs N] [--all-traces]
  object compare ORACLE.o ORIGINAL.o
  pe inspect|read|find FILE [options]
  implib DEF OUTPUT
  rsrc2res EXE OUTPUT
  split VERSION [--source] [--root DIR] [--output DIR]
  relink-check VERSION [--write] [--root DIR]
      which compiled units of a source tree's build link into the original (after ninja all_source);
      --write records the others as its state.json's relink_exclude
  source-only VERSION [--root DIR]
      link the compiler of a source tree's sources alone, build/VERSION/<tree>/source-only/mwcceppc.exe
      (after ninja all_source; compare it with the original: corpus --exe)
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
  build-hooks OUTPUT [--trace-shim | --frontend-version V] [--root DIR] [--rustc FILE] [--timeout SECS]
  package-release OUTPUT.zip --binary FILE --target TARGET [--root DIR] [--source-revision COMMIT] [--notices DIR]
  transform-dep DEP OUT [--root DIR]
  download TOOL OUTPUT [OTHER_OUTPUT...] [--root DIR] [--timeout SECS]
  verify|gates [--root DIR] [--ninja FILE] [--jobs N] [--timeout SECS]

Oracle views: --pcode, --schedule, --near N, --inline-cost CALLEE, --full-cost, --instantiation TEXT, --cse, --ir LINE|all, --exchange A,B, --blocks, --locals, --raw.
Use compile-trace for raw trace, hashes and stock check status; oracle --json returns report schema 6.
Development capture: --backend explains actual coalescing; --source-arenas tracks allocation lifetimes.
Experimental --source-nodes records frontend snapshots and clone calls and enables --source-arenas.
source-search tests C++ edits; source-reduce shrinks a mechanism; source-index finds matched examples.
Guides: docs/oracle.md and docs/oracle-source-guided-status.md."#
    );
}

fn run() -> Result<()> {
    let mut args = vec![];
    let mut compiler_flags = false;
    for arg in env::args().skip(1) {
        if arg == "--" {
            compiler_flags = true;
        }
        if !compiler_flags
            && let Some((option, value)) = arg.split_once('=')
            && matches!(
                option,
                "--oracle-root"
                    | "--working-directory"
                    | "--from-ninja"
                    | "--version"
                    | "--override"
                    | "--keep-object"
                    | "--cut-limit"
                    | "--merge-limit"
                    | "--timeout"
                    | "--function"
                    | "--link"
                    | "--near"
                    | "--ir"
                    | "--exchange"
                    | "--target"
                    | "--inline-cost"
                    | "--instantiation"
                    | "--source"
                    | "--load"
                    | "--save"
                    | "--class"
                    | "--canaries"
                    | "--prime"
                    | "--project"
                    | "--keep"
                    | "--exe"
                    | "--limit"
                    | "--list"
                    | "--jobs"
                    | "--target-asm"
                    | "--want"
                    | "--order"
                    | "--move"
                    | "--drop"
                    | "--degree"
                    | "--unlink"
                    | "--iterations"
                    | "--seed"
                    | "--restarts"
                    | "--top"
                    | "--fixed"
                    | "--nodes"
                    | "--kinds"
                    | "--callee"
                    | "--section"
                    | "--git"
                    | "--unit"
                    | "--candidate"
                    | "--objdiff"
                    | "--trace"
                    | "--diff-json"
                    | "--object"
                    | "--compiler"
                    | "--root"
                    | "--output"
                    | "--reference"
                    | "--out"
                    | "--mode"
                    | "--producer"
                    | "--binary"
                    | "--source-revision"
                    | "--notices"
                    | "--frontend-version"
                    | "--first-round"
                    | "--second-round"
                    | "--first-snapshot"
                    | "--second-snapshot"
                    | "--ninja"
                    | "--rustc"
            )
        {
            args.push(option.to_owned());
            args.push(value.to_owned());
        } else {
            args.push(arg);
        }
    }
    match args.first().map(String::as_str) {
        Some("provenance") => provenance_cli::run(&args[1..])?,
        Some("--version") => println!("mwcc-tools {}", env!("CARGO_PKG_VERSION")),
        Some("package-release") => release_cli::run(&args[1..])?,
        Some("configure" | "transform-dep" | "download" | "verify" | "gates" | "build-hooks") => {
            build_cli::run(&args[0], &args[1..])?
        }
        Some("--help" | "-h") | None => help(),
        Some("oracle") if args.get(1).map(String::as_str) == Some("regdiff") => {
            kit_cli::regdiff(&args[2..])?
        }
        Some("oracle") if args.get(1).map(String::as_str) == Some("run-unit") => {
            kit_cli::run_unit(&args[2..])?
        }
        Some("oracle") if args.get(1).map(String::as_str) == Some("compile-trace") => {
            compile_trace(&args[2..], false)?
        }
        Some("oracle") if args.get(1).map(String::as_str) == Some("analyze-trace") => {
            let path = args.get(2).context("analyze-trace requires TRACE")?;
            let mut version = Version::Gc132;
            let mut function = None;
            let mut link = None;
            let mut events = false;
            let mut allocation_only = false;
            let mut raw = false;
            let mut source = String::new();
            let mut near = None;
            let mut i = 3;
            while i < args.len() {
                match args[i].as_str() {
                    "--version" | "--function" | "--link" | "--source" | "--near" => {
                        let v = args
                            .get(i + 1)
                            .with_context(|| format!("{} requires a value", args[i]))?;
                        match args[i].as_str() {
                            "--version" => version = Version::parse(v)?,
                            "--function" => function = Some(v),
                            "--link" => link = Some(v),
                            "--source" => source = v.clone(),
                            "--near" => near = Some(v.parse()?),
                            _ => unreachable!(),
                        }
                        i += 2;
                    }
                    "--events" => {
                        events = true;
                        i += 1;
                    }
                    "--allocation-only" => {
                        allocation_only = true;
                        i += 1;
                    }
                    "--json" => i += 1,
                    "--raw" => {
                        raw = true;
                        i += 1;
                    }
                    other => bail!("unknown analyze-trace option {other}"),
                }
            }
            let text = fs::read_to_string(path).with_context(|| format!("read {path}"))?;
            let parsed = trace::parse(&text)?;
            let mut output = if events {
                serde_json::to_value(&parsed)?
            } else if allocation_only {
                json!(
                    parsed
                        .allocations
                        .iter()
                        .filter(|a| function.is_none_or(
                            |q| a.before.function.contains(q) || a.before.link.contains(q)
                        ) && link.is_none_or(|q| a.before.link == *q))
                        .map(|a| allocation::analyze(a, version))
                        .collect::<Vec<_>>()
                )
            } else {
                document::build(
                    &parsed,
                    version,
                    &source,
                    &format!("build/{}/oracle/mwcceppc.exe", version.name()),
                    None,
                    &[],
                    &document::Selection {
                        function: function.map(String::as_str),
                        link: link.map(String::as_str),
                        near,
                    },
                )?
            };
            if raw {
                anyhow::ensure!(
                    !events && !allocation_only,
                    "--raw requires the schema-6 document output"
                );
                output["raw"] = json!(
                    if function.is_some_and(|f| !f.is_empty()) || link.is_some() {
                        trace::raw_selected(
                            &text,
                            function.map(String::as_str),
                            link.map(String::as_str),
                        )
                    } else {
                        text.lines().map(str::to_owned).collect()
                    }
                );
            }
            println!("{}", serde_json::to_string_pretty(&output)?);
        }
        Some("oracle") => compile_trace(&args[1..], true)?,
        Some("corpus") => corpus_command(&args[1..])?,
        Some("colouring") if args.get(1).map(String::as_str) == Some("unit") => {
            kit_cli::colouring_unit(&args[2..])?
        }
        Some("colouring") => colouring_command(&args[1..])?,
        Some("inline-diff") => tu_cli::inline_diff(&args[1..])?,
        Some("inline-window") => tu_cli::inline_window(&args[1..])?,
        Some("data-diff") => tu_cli::data_diff(&args[1..])?,
        Some("header-impact") => header_impact_cli::run(&args[1..])?,
        Some("source-search") => source_search_cli::run(&args[1..])?,
        Some("source-arenas") => source_arenas_cli::run(&args[1..])?,
        Some("source-nodes") => source_nodes_cli::run(&args[1..])?,
        Some("source-reduce") => source_reduce_cli::run(&args[1..])?,
        Some("source-repro") => source_repro_cli::run(&args[1..])?,
        Some("source-index") => source_index_cli::run(&args[1..])?,
        Some("inline-calibrate") => calibrate_cli::run(&args[1..])?,
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
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error:#}");
            ExitCode::FAILURE
        }
    }
}
