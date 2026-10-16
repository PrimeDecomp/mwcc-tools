use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    colouring::{
        self, Edit,
        correspondence::{self, Selection, SnapshotPhase},
        search::{self, Options, Problem},
        target,
    },
    cse, ppc, project, runner,
    trace::{self, Allocation, Trace},
    version::{Version, class_name},
};
use serde_json::{Value, json};
use std::{collections::BTreeMap, fs, path::PathBuf};

#[derive(Default)]
struct Args {
    command: String,
    load: Vec<String>,
    save: Option<String>,
    class: Option<String>,
    function: Option<String>,
    json: bool,
    compile: Vec<String>,
    source: Option<String>,
    from_ninja: Option<PathBuf>,
    version: Option<Version>,
    asm: Option<PathBuf>,
    wants: BTreeMap<u32, i32>,
    edits: BTreeMap<String, Vec<String>>,
    search: Options,
    first_graph: Selection,
    second_graph: Selection,
}

fn number(text: &str) -> Result<u32> {
    text.trim()
        .trim_start_matches('%')
        .trim_start_matches(['r', 'f', 'v'])
        .parse()
        .with_context(|| format!("invalid register {text}"))
}
fn list(text: &str) -> Result<Vec<u32>> {
    text.split(',')
        .filter(|s| !s.trim().is_empty())
        .map(number)
        .collect()
}
fn parse(args: &[String]) -> Result<Args> {
    let mut out = Args::default();
    let mut i = 0;
    while i < args.len() {
        let option = args[i].as_str();
        if option == "--" {
            out.compile.extend_from_slice(&args[i..]);
            break;
        }
        if option == "--json" {
            out.json = true;
            i += 1;
            continue;
        }
        if matches!(
            option,
            "--load"
                | "--save"
                | "--class"
                | "--function"
                | "--version"
                | "--target-asm"
                | "--want"
                | "--order"
                | "--move"
                | "--drop"
                | "--degree"
                | "--unlink"
                | "--link"
                | "--iterations"
                | "--seed"
                | "--restarts"
                | "--top"
                | "--fixed"
                | "--nodes"
                | "--kinds"
                | "--from-ninja"
                | "--oracle-root"
                | "--working-directory"
                | "--override"
                | "--timeout"
                | "--first-round"
                | "--second-round"
                | "--first-snapshot"
                | "--second-snapshot"
        ) {
            let value = args
                .get(i + 1)
                .with_context(|| format!("{option} needs a value"))?;
            match option {
                "--load" => out.load.push(value.clone()),
                "--save" => out.save = Some(value.clone()),
                "--class" => out.class = Some(value.to_uppercase()),
                "--function" => out.function = Some(value.clone()),
                "--version" => {
                    out.version = Some(Version::parse(value)?);
                    out.compile.extend([option.into(), value.clone()]);
                }
                "--target-asm" => out.asm = Some(value.into()),
                "--want" => {
                    let (v, c) = value.split_once('=').context("--want needs V=REGISTER")?;
                    out.wants.insert(number(v)?, i32::try_from(number(c)?)?);
                }
                "--first-round" | "--second-round" => {
                    let round = Some(
                        value
                            .parse()
                            .context("class round must be a nonnegative index")?,
                    );
                    if option == "--first-round" {
                        out.first_graph.round = round;
                    } else {
                        out.second_graph.round = round;
                    }
                }
                "--first-snapshot" | "--second-snapshot" => {
                    let phase = match value.as_str() {
                        "before" => SnapshotPhase::Before,
                        "after" => SnapshotPhase::After,
                        _ => bail!("snapshot must be before or after"),
                    };
                    if option == "--first-snapshot" {
                        out.first_graph.phase = phase;
                    } else {
                        out.second_graph.phase = phase;
                    }
                }
                "--iterations" => out.search.iterations = value.parse()?,
                "--seed" => out.search.seed = value.clone(),
                "--restarts" => out.search.restarts = value.parse()?,
                "--top" => out.search.keep = value.parse()?,
                "--fixed" => out.search.fixed = list(value)?.into_iter().collect(),
                "--nodes" => {
                    let nodes = list(value)?;
                    out.search.nodes = (!nodes.is_empty()).then_some(nodes);
                }
                "--kinds" => {
                    out.search.kinds = if value == "none" {
                        vec![]
                    } else {
                        value.split(',').map(str::to_owned).collect()
                    }
                }
                "--order" | "--move" | "--drop" | "--degree" | "--unlink" | "--link" => out
                    .edits
                    .entry(option.into())
                    .or_default()
                    .push(value.clone()),
                _ => {
                    if option == "--from-ninja" {
                        out.from_ninja = Some(value.into());
                    }
                    out.compile.extend([option.into(), value.clone()]);
                }
            }
            i += 2;
            continue;
        }
        if out.command.is_empty() && !option.starts_with('-') {
            out.command = option.into();
        } else {
            if out.source.is_none() && !option.starts_with('-') {
                out.source = Some(option.into());
            }
            out.compile.push(option.into());
        }
        i += 1;
    }
    ensure!(
        ["verify", "target", "force", "search", "replay", "remap"].contains(&out.command.as_str()),
        "a colouring command is needed: verify, target, force, search, replay, remap"
    );
    ensure!(
        matches!(out.command.as_str(), "verify" | "remap") || out.function.is_some(),
        "{} needs --function",
        out.command
    );
    if let Some(class) = &out.class {
        ensure!(
            ["GPR", "FPR", "VR", "CR", "SPR"].contains(&class.as_str()),
            "unknown register class {class}"
        );
    }
    ensure!(
        out.command == "remap"
            || (out.first_graph == Selection::default()
                && out.second_graph == Selection::default()),
        "graph snapshot/round selection is available only for remap"
    );
    Ok(out)
}

struct Input {
    trace: Trace,
    object: Option<Vec<u8>>,
    version: Version,
    source: Option<String>,
}
fn load(prefix: &str, version: Option<Version>) -> Result<Input> {
    let text = fs::read_to_string(format!("{prefix}.trace"))?;
    let path = PathBuf::from(format!("{prefix}.o"));
    let object = if path.is_file() {
        Some(fs::read(path)?)
    } else {
        None
    };
    let manifest = PathBuf::from(format!("{prefix}.manifest.json"));
    let mut inferred = None;
    let mut source = None;
    if manifest.is_file() {
        let meta: Value = serde_json::from_slice(&fs::read(manifest)?)?;
        ensure!(
            meta["format_version"] == 1,
            "unsupported saved trace manifest format"
        );
        let compilation = &meta["compilation"];
        inferred = compilation["version"]
            .as_str()
            .map(Version::parse)
            .transpose()?;
        source = compilation["source"].as_str().map(str::to_owned);
        if let Some(expected) = compilation["object_sha256"].as_str() {
            let object = object
                .as_ref()
                .context("saved object named in manifest is missing")?;
            ensure!(
                hash(object) == expected,
                "saved object differs from its manifest"
            );
        }
        if let Some(expected) = meta["trace_sha256"].as_str() {
            ensure!(
                hash(text.as_bytes()) == expected,
                "saved trace differs from its manifest"
            );
        }
    }
    ensure!(
        version.is_none() || inferred.is_none() || version == inferred,
        "--version disagrees with the saved trace manifest"
    );
    Ok(Input {
        trace: trace::parse(&text)?,
        object,
        version: version.or(inferred).unwrap_or(Version::Gc27),
        source,
    })
}
fn hash(bytes: &[u8]) -> String {
    use sha2::{Digest, Sha256};
    format!("{:x}", Sha256::digest(bytes))
}
fn compile(args: &Args, base: bool) -> Result<Input> {
    let mut parsed = super::parse_compile(&args.compile, true)?;
    parsed.request.options.cse = true;
    parsed.request.check = false;
    if base {
        parsed.request.overrides.clear();
    }
    let compiled = runner::compile(&parsed.request)?;
    if !base && let Some(prefix) = &args.save {
        fs::write(format!("{prefix}.o"), &compiled.object)?;
        fs::write(format!("{prefix}.trace"), &compiled.trace)?;
        let mut metadata = serde_json::to_value(&compiled)?;
        metadata.as_object_mut().unwrap().remove("trace");
        fs::write(
            format!("{prefix}.manifest.json"),
            serde_json::to_vec_pretty(
                &json!({"format_version":1,"trace_sha256":hash(compiled.trace.as_bytes()),"compilation":metadata}),
            )?,
        )?;
    }
    Ok(Input {
        trace: trace::parse(&compiled.trace)?,
        object: Some(compiled.object),
        version: compiled.version,
        source: Some(compiled.source),
    })
}
fn one<'a>(input: &'a Input, function: Option<&str>) -> Result<&'a Allocation> {
    let mut found: Vec<_> = input
        .trace
        .allocations
        .iter()
        .filter(|a| {
            function.is_none_or(|f| a.before.function.contains(f) || a.before.link.contains(f))
        })
        .collect();
    if found.len() > 1
        && let Some(f) = function
    {
        let exact: Vec<_> = found
            .iter()
            .copied()
            .filter(|a| a.before.function == f || a.before.link == f)
            .collect();
        if !exact.is_empty() {
            found = exact;
        }
    }
    ensure!(
        found.len() == 1,
        "{function:?} matches {} functions{}",
        found.len(),
        if found.is_empty() {
            String::new()
        } else {
            format!(
                ": {}",
                found
                    .iter()
                    .take(8)
                    .map(|a| a.before.link.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            )
        }
    );
    Ok(found[0])
}
fn names(input: &Input, allocation: &Allocation) -> Result<BTreeMap<(String, u32), String>> {
    let parsed = cse::parse(&input.trace.records)?;
    parsed
        .get(&allocation.before.link)
        .map(cse::names)
        .transpose()
        .map(Option::unwrap_or_default)
}
fn reg(class: &str, number: i32) -> String {
    format!(
        "{}{number}",
        match class {
            "GPR" => "r",
            "FPR" => "f",
            "VR" => "v",
            "CR" => "cr",
            _ => "r",
        }
    )
}
fn label(
    class: &str,
    v: u32,
    names: &BTreeMap<(String, u32), String>,
    round: Option<&mwcc_oracle_core::trace::Round>,
) -> String {
    if v < 32 {
        return reg(class, v as i32);
    }
    let extra = names
        .get(&(class.into(), v))
        .map(String::as_str)
        .or_else(|| {
            round
                .and_then(|r| r.nodes.get(&v))
                .filter(|n| n.flags & 4 != 0)
                .map(|_| "coalesced")
        });
    format!(
        "%{v}{}",
        extra.map_or(String::new(), |name| format!(" ({name})"))
    )
}
fn edits(args: &Args) -> Result<Vec<Edit>> {
    let mut edits = vec![];
    for kind in [
        "--order", "--move", "--drop", "--degree", "--unlink", "--link",
    ] {
        for text in args.edits.get(kind).into_iter().flatten() {
            edits.push(match kind {
                "--order" => Edit::Order(list(text)?),
                "--move" => {
                    let parts: Vec<_> = text.split(':').collect();
                    ensure!(
                        parts.len() == 3 && ["before", "after"].contains(&parts[1]),
                        "--move needs V:before|after:W"
                    );
                    Edit::Move {
                        node: number(parts[0])?,
                        after: parts[1] == "after",
                        anchor: number(parts[2])?,
                    }
                }
                "--drop" => Edit::Drop(number(text)?),
                "--degree" => {
                    let (v, d) = text.split_once(':').context("--degree needs V:+1")?;
                    Edit::Degree(number(v)?, d.parse()?)
                }
                _ => {
                    let (a, b) = text.split_once(',').context("edge edit needs A,B")?;
                    if kind == "--unlink" {
                        Edit::Unlink(number(a)?, number(b)?)
                    } else {
                        Edit::Link(number(a)?, number(b)?)
                    }
                }
            });
        }
    }
    Ok(edits)
}
fn describe(
    edit: &Edit,
    class: &str,
    names: &BTreeMap<(String, u32), String>,
    round: &mwcc_oracle_core::trace::Round,
) -> String {
    let label = |v| label(class, v, names, Some(round));
    match edit {
        Edit::Order(order) => {
            let moved = colouring::renumbered(order);
            format!(
                "numbered, lowest first: {} (* moved: the others keep their order)",
                order
                    .iter()
                    .map(|v| label(*v) + if moved.contains(v) { "*" } else { "" })
                    .collect::<Vec<_>>()
                    .join(" < ")
            )
        }
        Edit::Move {
            node,
            after,
            anchor,
        } => format!(
            "number {} just {} {}",
            label(*node),
            if *after { "after" } else { "before" },
            label(*anchor)
        ),
        Edit::Drop(v) => format!("remove {}", label(*v)),
        Edit::Degree(v, d) => format!("degree of {} {d:+}", label(*v)),
        Edit::Unlink(a, b) | Edit::Link(a, b) => format!(
            "{} the edge {} - {}",
            if matches!(edit, Edit::Unlink(..)) {
                "remove"
            } else {
                "add"
            },
            label(*a),
            label(*b)
        ),
    }
}
fn repr(value: &Value, numeric_keys: bool) -> String {
    match value {
        Value::Null => "None".into(),
        Value::Bool(v) => if *v { "True" } else { "False" }.into(),
        Value::String(s) => {
            let quote = if s.contains('\'') && !s.contains('"') {
                '"'
            } else {
                '\''
            };
            format!(
                "{quote}{}{quote}",
                s.replace('\\', "\\\\")
                    .replace(quote, &format!("\\{quote}"))
                    .replace('\n', "\\n")
                    .replace('\r', "\\r")
                    .replace('\t', "\\t")
            )
        }
        Value::Array(a) => format!(
            "[{}]",
            a.iter()
                .map(|v| repr(v, numeric_keys))
                .collect::<Vec<_>>()
                .join(", ")
        ),
        Value::Object(o) => format!(
            "{{{}}}",
            o.iter()
                .map(|(k, v)| format!(
                    "{}: {}",
                    if numeric_keys && k.parse::<i64>().is_ok() {
                        k.clone()
                    } else {
                        repr(&json!(k), false)
                    },
                    repr(v, numeric_keys)
                ))
                .collect::<Vec<_>>()
                .join(", ")
        ),
        _ => value.to_string(),
    }
}

fn verify(input: &Input, args: &Args) -> Result<(Value, Vec<String>, bool)> {
    let classes = args.class.clone().map_or_else(
        || vec!["GPR".into(), "FPR".into(), "VR".into()],
        |c| vec![c],
    );
    let mut results = vec![];
    for a in &input.trace.allocations {
        if args
            .function
            .as_deref()
            .is_some_and(|f| !a.before.function.contains(f) && !a.before.link.contains(f))
        {
            continue;
        }
        for c in &classes {
            if let Some(result) = colouring::verify(a, c)? {
                results.push(result);
            }
        }
    }
    let mut lines = vec![];
    for r in &results {
        let mut head = format!(
            "{} {}: {} nodes",
            r["function"].as_str().unwrap_or("?"),
            r["class"].as_str().unwrap_or("?"),
            r["nodes"]
        );
        if r["rounds"].as_u64().unwrap_or(0) > 1 {
            head += &format!(", round {} (earlier ones spilled)", r["rounds"]);
        }
        let mut details = vec![];
        if r["order_agrees"] != true {
            details.push("the simplification's replay gives another order".into());
        }
        let disagreements = r["colour_disagreements"].as_array().unwrap();
        if !disagreements.is_empty() {
            details.push(format!(
                "colours differ: {}",
                disagreements
                    .iter()
                    .take(8)
                    .map(|d| format!("%{} {} (traced {})", d[0], repr(&d[1], false), d[2]))
                    .collect::<Vec<_>>()
                    .join(", ")
            ));
        }
        if r["spilled"] != r["traced_spilled"] {
            details.push(format!(
                "spilled {} (traced {})",
                repr(&r["spilled"], false),
                repr(&r["traced_spilled"], false)
            ));
        }
        let claims = r["claims"].as_array().unwrap();
        let claimed = if claims.is_empty() {
            String::new()
        } else {
            format!(
                " claims {}",
                claims
                    .iter()
                    .map(Value::to_string)
                    .collect::<Vec<_>>()
                    .join(",")
            )
        };
        let guess = if r["inferred"] == true && !claims.is_empty() {
            " (the registers used before the colouring are a guess: no claim traced)"
        } else {
            ""
        };
        lines.push(format!(
            "{:9} {head}{claimed}{guess}{}",
            if r["ok"] == true { "OK" } else { "DISAGREES" },
            if details.is_empty() {
                String::new()
            } else {
                format!("; {}", details.join("; "))
            }
        ));
    }
    let ok = results.iter().all(|r| r["ok"] == true);
    lines.push(format!(
        "{} of {} colourings replayed exactly",
        results.iter().filter(|r| r["ok"] == true).count(),
        results.len()
    ));
    Ok((json!({"functions":results,"ok":ok}), lines, ok))
}

fn run_remap(args: &Args, class: &str) -> Result<(Value, Vec<String>)> {
    let (a, b) = if args.load.len() == 2 {
        (
            load(&args.load[0], args.version)?,
            load(&args.load[1], args.version)?,
        )
    } else {
        ensure!(
            args.source.is_some() && args.compile.iter().any(|s| s == "--override"),
            "remap needs two --load prefixes, or SOURCE with --override (the variant)"
        );
        (compile(args, true)?, compile(args, false)?)
    };
    let first = one(&a, args.function.as_deref())?;
    let second = one(&b, args.function.as_deref())?;
    ensure!(
        a.version == b.version,
        "remap requires the same compiler version on both inputs"
    );
    let first_snapshot = match args.first_graph.phase {
        SnapshotPhase::Before => &first.before,
        SnapshotPhase::After => &first.after,
    };
    let second_snapshot = match args.second_graph.phase {
        SnapshotPhase::Before => &second.before,
        SnapshotPhase::After => &second.after,
    };
    let inferred = target::remap_snapshots(first_snapshot, second_snapshot, a.version);
    let comparison = correspondence::compare(
        first,
        second,
        &inferred,
        class,
        args.first_graph,
        args.second_graph,
    )?;
    let output = serde_json::to_value(&comparison)?;
    let mut lines = vec![format!(
        "== {}: {} of {} / {} instructions paired ==",
        first.before.function, inferred.paired, inferred.instructions.0, inferred.instructions.1
    )];
    lines.push(format!(
        "   {} unambiguous instruction hypotheses; elimination indeterminate",
        comparison.inferred_mapping.len()
    ));
    for (side, graph) in [("first", &comparison.first), ("second", &comparison.second)] {
        if let Some(graph) = graph {
            lines.push(format!(
                "   {side}: {:?} class round {}, {} graph nodes, {} graph-only nodes",
                graph.phase,
                graph.class_round,
                graph.nodes.len(),
                graph.graph_only.len()
            ));
        } else {
            lines.push(format!("   {side}: requested graph snapshot unavailable"));
        }
    }
    if let Some(delta) = &comparison.delta {
        lines.push(format!(
            "   unmatched graph nodes: {} first / {} second (not proven eliminated)",
            delta.unmatched_first.len(),
            delta.unmatched_second.len()
        ));
        lines.push(format!(
            "   unclassified boundary edges: {} first / {} second",
            delta.boundary_first.len(),
            delta.boundary_second.len()
        ));
        lines.push(format!(
            "   inferred edge changes: +{} / -{}",
            delta.inferred_edges_added.len(),
            delta.inferred_edges_removed.len()
        ));
    }
    for limitation in &comparison.limitations {
        lines.push(format!("   {limitation}"));
    }
    Ok((output, lines))
}

pub fn run(raw: &[String]) -> Result<()> {
    if raw
        .iter()
        .take_while(|s| s.as_str() != "--")
        .any(|s| s == "--help" || s == "-h")
    {
        println!(
            "mwcc-tools colouring COMMAND [OPTIONS] [SOURCE] -- COMPILER FLAGS\n\nCommands: verify, target, force, search, replay, remap\n\n--load PREFIX (repeat twice for remap), --save PREFIX, --from-ninja FILE, --version V,\n--function NAME, --class GPR|FPR|VR, --target-asm FILE, --want V=REGISTER, --json\n\nReplay edits: --order V,V,... --move V:before|after:W --drop V --degree V:+1 --unlink A,B --link A,B\nSearch: --iterations N --seed INTEGER --restarts N --top N --fixed V,V,... --nodes V,V,... --kinds drop,degree,unlink\nRemap: --first-round N --second-round N (zero-based within class), --first-snapshot before|after --second-snapshot before|after; JSON format 2 retains unmatched nodes and boundary edges"
        );
        return Ok(());
    }
    let args = parse(raw)?;
    let class = args.class.as_deref().unwrap_or("GPR");
    if args.command == "remap" {
        let (json, lines) = run_remap(&args, class)?;
        if args.json {
            println!("{}", serde_json::to_string_pretty(&json)?);
        } else {
            println!("{}", lines.join("\n"));
        }
        return Ok(());
    }
    ensure!(
        args.load.len() <= 1,
        "{} takes at most one --load",
        args.command
    );
    ensure!(
        args.load.is_empty() || args.save.is_none(),
        "--load and --save cannot be combined"
    );
    let input = if let Some(prefix) = args.load.first() {
        load(prefix, args.version)?
    } else {
        compile(&args, false)?
    };
    if args.command == "verify" {
        let (json, lines, ok) = verify(&input, &args)?;
        if args.json {
            println!("{}", serde_json::to_string_pretty(&json)?);
        } else {
            println!("{}", lines.join("\n"));
        }
        ensure!(ok, "colouring replay disagrees with the trace");
        return Ok(());
    }
    let a = one(&input, args.function.as_deref())?;
    let source = args.source.as_deref().or(input.source.as_deref());
    let asm = if let Some(path) = &args.asm {
        Some(path.clone())
    } else if let (Some(ninja), Some(source)) = (&args.from_ninja, source) {
        project::target_asm(&project::target_directory(ninja)?, source)?
    } else {
        None
    };
    let derived = if let Some(path) = asm {
        let text = String::from_utf8_lossy(&fs::read(&path)?).into_owned();
        let words = ppc::target_words(&text, &a.before.link)
            .with_context(|| format!("{} has no function {}", path.display(), a.before.link))?;
        let ours = input
            .object
            .as_ref()
            .map(|obj| ppc::function_words(obj, &a.before.link))
            .transpose()?
            .flatten()
            .unwrap_or_default();
        target::derive(a, &ours, &words, class, &args.wants)?
    } else {
        ensure!(
            !args.wants.is_empty(),
            "no target asm (--target-asm FILE, or --from-ninja) and no --want"
        );
        target::Derived {
            targets: args.wants.clone(),
            stats: json!({}),
            ..Default::default()
        }
    };
    let rounds: Vec<_> = a
        .before
        .rounds
        .iter()
        .filter(|r| class_name(r.class) == class)
        .collect();
    let round = *rounds
        .last()
        .with_context(|| format!("{}: no {class} colouring traced", a.before.function))?;
    let problem = Problem::new(round, class, Some(rounds[0].count), derived.targets.clone())?;
    let names = names(&input, a)?;
    let label = |v| label(class, v, &names, Some(round));
    let describe = |e: &Edit| describe(e, class, &names, round);
    let head = format!(
        "== {}: {class}, {} registers wrong",
        a.before.function,
        problem.base.wrong.len()
    );
    let mut lines = vec![];
    let output = match args.command.as_str() {
        "target" => {
            lines.extend([
                format!("== {}: {class} targets ==", a.before.function),
                format!("   pairing: {}", repr(&derived.stats, false)),
            ]);
            if !derived.renumbered.is_empty() {
                lines.push(format!("   WARNING: {} registers hold other colours in the PCode than in the last colouring round (a spill round renumbered them): these targets cannot be trusted",derived.renumbered.len()));
            }
            for (i, j) in &derived.conflicts {
                lines.push(format!("   CONFLICT: {} and {} interfere but both get {}: a wrong pairing, or a --want to check",label(*i),label(*j),reg(class,derived.targets[i])));
            }
            lines.push(format!(
                "   {} registers with a target, {} wrong:",
                problem.targets.len(),
                problem.base.wrong.len()
            ));
            for v in &problem.base.wrong {
                let votes = derived.votes.get(v);
                lines.push(format!(
                    "      {}: ours {}, target {}{}",
                    label(*v),
                    reg(
                        class,
                        problem.base.replay.colour.get(v).copied().unwrap_or(-1)
                    ),
                    reg(class, problem.targets[v]),
                    if votes.is_some_and(|v| v.as_object().is_some_and(|o| o.len() > 1)) {
                        format!(" (the fields disagree: {})", repr(votes.unwrap(), true))
                    } else {
                        String::new()
                    }
                ));
            }
            let untargeted: Vec<_> = problem
                .base
                .replay
                .order
                .iter()
                .filter(|v| {
                    !problem.targets.contains_key(v)
                        && problem.base.replay.colour.get(v).copied().unwrap_or(-1)
                            >= if class == "VR" { 20 } else { 14 }
                })
                .collect();
            if !untargeted.is_empty() {
                lines.push(format!(
                    "   non-volatile registers with no target (no paired field): {}",
                    untargeted
                        .into_iter()
                        .map(|v| label(*v))
                        .collect::<Vec<_>>()
                        .join(", ")
                ));
            }
            json!({"function":a.before.function,"class":class,"targets":problem.targets,"wrong":problem.base.wrong,"votes":derived.votes,"conflicts":derived.conflicts,"renumbered":derived.renumbered,"stats":derived.stats})
        }
        "force" => {
            let forced = search::force(&problem)?;
            lines.extend([
                head + " ==",
                format!(
                    "   the claiming nodes ordered by their target: {}",
                    forced
                        .order
                        .iter()
                        .filter(|v| problem.targets.get(v).copied().unwrap_or(-1)
                            >= if class == "VR" { 20 } else { 14 })
                        .map(|v| label(*v))
                        .collect::<Vec<_>>()
                        .join(" ")
                ),
            ]);
            lines.push(if forced.exact{"   EXACT: the graph can give the target's colours; only the order is wrong (the numbering or the degrees: search them)".into()}else{format!("   still wrong: {}{}: no order of the claims gives the target; the graph must change (its edges or nodes)",forced.wrong.iter().map(|v|label(*v)).collect::<Vec<_>>().join(", "),if forced.spilled.is_empty(){String::new()}else{format!("; uncoloured: {}",repr(&json!(forced.spilled),false))})});
            let mut value = serde_json::to_value(forced)?;
            value["function"] = json!(a.before.function);
            value["class"] = json!(class);
            value
        }
        "replay" => {
            let edits = edits(&args)?;
            let result = problem.run(&edits)?;
            lines.extend([
                format!(
                    "== {}: {class} with {} ==",
                    a.before.function,
                    if edits.is_empty() {
                        "no edit".into()
                    } else {
                        edits.iter().map(describe).collect::<Vec<_>>().join("; ")
                    }
                ),
                format!(
                    "   wrong: {} -> {}{}",
                    problem.base.wrong.len(),
                    result.wrong.len(),
                    if result.replay.spilled.is_empty() {
                        String::new()
                    } else {
                        format!(
                            "; UNCOLOURED (a spill, not modelled): {}",
                            repr(&json!(result.replay.spilled), false)
                        )
                    }
                ),
            ]);
            let all: std::collections::BTreeSet<_> = problem
                .base
                .replay
                .colour
                .keys()
                .chain(result.replay.colour.keys())
                .copied()
                .collect();
            for v in all {
                let before = problem.base.replay.colour.get(&v);
                let after = result.replay.colour.get(&v);
                if before != after {
                    lines.push(format!(
                        "   {}: {} -> {}{}",
                        label(v),
                        before.map_or("-".into(), |c| reg(class, *c)),
                        after.map_or("-".into(), |c| reg(class, *c)),
                        problem
                            .targets
                            .get(&v)
                            .map_or(String::new(), |c| format!(" (target {})", reg(class, *c)))
                    ));
                }
            }
            json!({"function":a.before.function,"class":class,"edits":edits,"wrong":result.wrong,"spilled":result.replay.spilled,"colour":result.replay.colour,"order":result.replay.order})
        }
        "search" => {
            let found = search::search_with_progress(
                &problem,
                &args.search,
                |restart, step, wrong, cost| {
                    eprintln!(
                        "   run {}, step {step}: {wrong} wrong{}",
                        restart + 1,
                        cost.map_or(String::new(), |c| format!(", smallest exact change {c}"))
                    )
                },
            )?;
            lines.push(format!(
                "{head}: {} ==",
                problem
                    .base
                    .wrong
                    .iter()
                    .map(|v| label(*v))
                    .collect::<Vec<_>>()
                    .join(", ")
            ));
            if !found.singles.is_empty() {
                lines.push("   single changes that leave fewer wrong:".into());
                lines.extend(
                    found
                        .singles
                        .iter()
                        .map(|(wrong, e)| format!("      {wrong} wrong: {}", describe(e))),
                );
            }
            lines.push(format!(
                "   renumbering {}",
                found
                    .nodes
                    .iter()
                    .map(|v| label(*v))
                    .collect::<Vec<_>>()
                    .join(" ")
            ));
            for (change, edits) in &found.exact {
                lines.push(format!("   EXACT (change {change}):"));
                lines.extend(edits.iter().map(|e| format!("      {}", describe(e))));
            }
            if let Some((wrong, edits)) = &found.best {
                lines.push(format!(
                    "   no exact solution found; the best leaves {wrong} wrong:"
                ));
                lines.extend(edits.iter().map(|e| format!("      {}", describe(e))));
            }
            json!({"function":a.before.function,"class":class,"wrong":problem.base.wrong,"nodes":found.nodes,"exact":found.exact.iter().map(|(change,edits)|json!({"change":change,"edits":edits})).collect::<Vec<_>>(),"best":found.best,"singles":found.singles})
        }
        _ => bail!("unknown colouring command"),
    };
    if args.json {
        println!("{}", serde_json::to_string_pretty(&output)?);
    } else {
        println!("{}", lines.join("\n"));
    }
    Ok(())
}
