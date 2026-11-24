//! `mwcc-tools run configure [-C DIR] [--configure COMMAND] [--versions A,B,...] [--default V] [-- ARGS...]`: a
//! dtk-template project's `uv run configure.py` for every version at once. It runs `COMMAND --version V ARGS...` for
//! each version (the project's `--version` choices, else the `config/*/config.yml` directories) and merges the
//! `build.ninja` each writes into one that builds every version (`mwcc-tools run ninja`, in parallel). The files
//! that are not per version (`objdiff.json`, `compile_commands.json`) are those of the default version: the last
//! configure runs without `--version` (the script's own default), or with `--default V`.
//!
//! The build.ninja files are read as `run ninja` reads them ([`mwcc_ninja::parse`]). They are alike (dtk-template
//! names everything by version: `build/V/...`), so what is equal in all of them is kept once (the tools' downloads,
//! the rules) and what differs is told apart:
//! * a global variable with different values is `NAME__V` for the version V (its references and its edges' bindings
//!   of it with it);
//! * a rule or pool with different bodies is `NAME__V` (the version's edges follow);
//! * edges that make some of the same files the same way are one (the downloads of a tool by two versions);
//! * the edges for one output that differ: a phony target (`all_source`) gets the union of its inputs; a target that
//!   is a name, not a path (`progress`, `report`), is `NAME_V` for each version, with `NAME` a phony target of all of
//!   them; any other is an error, as the versions would write one file differently;
//! * the configure edges are one, whose rule is this command: build.ninja is regenerated as it was made (`run ninja`
//!   regenerates it when out of date, as ninja does).
use anyhow::{Context, Result, bail};
use mwcc_ninja::{
    eval::{EvalPart, EvalString},
    parse::{Parser, Statement},
};
use std::{
    collections::{BTreeMap, BTreeSet, HashMap, HashSet},
    env, fs,
    process::Command,
};

const NAME: &str = "mwcc-tools run configure";

const USAGE: &str = "mwcc-tools run configure [-C DIR] [--configure COMMAND] [--versions A,B,...] [--default V] [-- ARGS...]\n\n\
Runs `COMMAND --version V ARGS...` (COMMAND is `uv run configure.py`) for every version of the project and writes \
one build.ninja that builds all of them: `mwcc-tools run ninja` builds the versions in parallel. The versions are \
the configure script's `--version` choices, else the directories config/*/config.yml, else those of --versions. \
ARGS are the configure script's other options (--wrapper, --map...). The files that are not per version \
(objdiff.json, compile_commands.json) are the default version's: the last run is `COMMAND ARGS...` (the script's \
default version), or `COMMAND --version V ARGS...` with --default V.";

/// The names a ninja file gives its own meaning: not global variables to tell apart.
const RESERVED: &[&str] = &[
    "command",
    "description",
    "depfile",
    "deps",
    "generator",
    "restat",
    "rspfile",
    "rspfile_content",
    "pool",
    "msvc_deps_prefix",
    "in",
    "in_newline",
    "out",
    "builddir",
];

/// A string with variable references, as `run ninja` parses it.
type Text = Vec<EvalPart<String>>;

/// A rule or pool: its name and bindings.
#[derive(Clone, Debug, PartialEq)]
struct Block {
    name: String,
    bindings: Vec<(String, Text)>,
}

/// A `build` edge: its outputs (explicit, implicit), rule, inputs (explicit, implicit, order-only) and bindings.
#[derive(Clone, Debug, PartialEq)]
struct Edge {
    outs: [Vec<Text>; 2],
    rule: String,
    ins: [Vec<Text>; 3],
    bindings: Vec<(String, Text)>,
}

/// The parts of a build.ninja the merge handles.
#[derive(Debug, Default)]
struct Ninja {
    /// The global variables, as the parser leaves them: their values expanded.
    vars: BTreeMap<String, String>,
    pools: Vec<Block>,
    rules: Vec<Block>,
    edges: Vec<Edge>,
    defaults: Vec<Text>,
}

fn text(eval: &EvalString<&str>) -> Text {
    eval.parts()
        .iter()
        .map(|part| match part {
            EvalPart::Literal(s) => EvalPart::Literal(s.to_string()),
            EvalPart::VarRef(s) => EvalPart::VarRef(s.to_string()),
        })
        .collect()
}

fn bindings<'a>(
    vars: impl Iterator<Item = &'a (&'a str, EvalString<&'a str>)>,
) -> Vec<(String, Text)> {
    vars.map(|(k, v)| (k.to_string(), text(v))).collect()
}

fn parse(source: &str) -> Result<Ninja> {
    let mut bytes = source.as_bytes().to_vec();
    bytes.push(0);
    let mut parser = Parser::new(&bytes);
    let mut ninja = Ninja::default();
    loop {
        let statement = match parser.read() {
            Ok(Some(statement)) => statement,
            Ok(None) => break,
            Err(e) => bail!("{}", parser.format_parse_error("build.ninja".as_ref(), e)),
        };
        match statement {
            Statement::Rule(rule) => ninja.rules.push(Block {
                name: rule.name.to_string(),
                bindings: bindings(rule.vars.iter()),
            }),
            Statement::Pool(pool) => ninja.pools.push(Block {
                name: pool.name.to_string(),
                bindings: vec![(
                    "depth".into(),
                    vec![EvalPart::Literal(pool.depth.to_string())],
                )],
            }),
            Statement::Build(build) => {
                if build.validation_ins > 0 {
                    bail!("line {}: validations are not supported", build.line);
                }
                let outs: Vec<Text> = build.outs.iter().map(text).collect();
                let ins: Vec<Text> = build.ins.iter().map(text).collect();
                let (explicit_outs, implicit_outs) = outs.split_at(build.explicit_outs);
                let (explicit, rest) = ins.split_at(build.explicit_ins);
                let (implicit, order_only) = rest.split_at(build.implicit_ins);
                ninja.edges.push(Edge {
                    outs: [explicit_outs.to_vec(), implicit_outs.to_vec()],
                    rule: build.rule.to_string(),
                    ins: [explicit.to_vec(), implicit.to_vec(), order_only.to_vec()],
                    bindings: bindings(build.vars.iter()),
                });
            }
            Statement::Default(targets) => ninja.defaults.extend(targets.iter().map(text)),
            Statement::Include(_) | Statement::Subninja(_) => {
                bail!("include and subninja are not supported")
            }
        }
    }
    ninja.vars = parser
        .vars
        .get_all()
        .iter()
        .map(|(k, v)| (k.to_string(), v.clone()))
        .collect();
    Ok(ninja)
}

/// TEXT renamed: the references to the variables NAMES, with SUFFIX.
fn rename(text: &mut Text, names: &HashSet<String>, suffix: &str) {
    for part in text {
        if let EvalPart::VarRef(name) = part
            && names.contains(name.as_str())
        {
            name.push_str(suffix);
        }
    }
}

fn rename_bindings(bindings: &mut [(String, Text)], names: &HashSet<String>, suffix: &str) {
    for (key, value) in bindings {
        if names.contains(key.as_str()) {
            key.push_str(suffix);
        }
        rename(value, names, suffix);
    }
}

/// TEXT as ninja reads it back: as a path (blanks and colons escaped) or a value.
fn render(text: &Text, path: bool) -> String {
    let mut out = String::new();
    for part in text {
        match part {
            EvalPart::VarRef(name) => out.push_str(&format!("${{{name}}}")),
            EvalPart::Literal(s) => {
                for c in s.chars() {
                    match c {
                        '$' => out.push_str("$$"),
                        ' ' | ':' if path => {
                            out.push('$');
                            out.push(c);
                        }
                        _ => out.push(c),
                    }
                }
            }
        }
    }
    out
}

/// TEXT when it has no variable references.
fn literal(text: &Text) -> Option<String> {
    text.iter()
        .map(|part| match part {
            EvalPart::Literal(s) => Some(s.as_str()),
            EvalPart::VarRef(_) => None,
        })
        .collect()
}

fn paths(texts: &[Text]) -> Vec<String> {
    texts.iter().map(|t| render(t, true)).collect()
}

fn bindings_text(bindings: &[(String, Text)]) -> String {
    bindings
        .iter()
        .map(|(k, v)| format!("  {k} = {}\n", render(v, false)))
        .collect()
}

/// EDGE's outputs as written.
fn edge_outs(edge: &Edge) -> String {
    let mut outs = paths(&edge.outs[0]).join(" ");
    if !edge.outs[1].is_empty() {
        outs.push_str(&format!(" | {}", paths(&edge.outs[1]).join(" ")));
    }
    outs
}

fn edge_text(edge: &Edge) -> String {
    let mut line = format!("build {}", edge_outs(edge));
    line.push_str(&format!(": {}", edge.rule));
    for (marker, section) in ["", " |", " ||"].iter().zip(&edge.ins) {
        if !section.is_empty() {
            line.push_str(&format!("{marker} {}", paths(section).join(" ")));
        }
    }
    format!("{line}\n{}", bindings_text(&edge.bindings))
}

/// A version's name as part of another: only characters ninja's names have.
fn tag(version: &str) -> String {
    version
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.' {
                c
            } else {
                '_'
            }
        })
        .collect()
}

fn union<T: PartialEq + Clone>(into: &mut Vec<T>, from: &[T]) {
    for item in from {
        if !into.contains(item) {
            into.push(item.clone());
        }
    }
}

/// The name EDGE builds, when its output is a name rather than a path.
fn alias(edge: &Edge) -> Option<String> {
    match (&edge.outs[0][..], &edge.outs[1][..]) {
        ([out], []) => literal(out).filter(|name| !name.contains(['/', '\\', '.'])),
        _ => None,
    }
}

/// One build.ninja for all the VERSIONS' files (name, text), whose configure step runs CONFIGURE.
fn merge(versions: &[(String, String)], configure: &str) -> Result<String> {
    let mut parsed: Vec<(String, Ninja)> = vec![];
    for (version, source) in versions {
        let ninja = parse(source).with_context(|| format!("{version}'s build.ninja"))?;
        parsed.push((tag(version), ninja));
    }

    // The global variables: those with different values are told apart.
    let mut values: BTreeMap<&str, Vec<Option<&str>>> = BTreeMap::new();
    for (i, (_, ninja)) in parsed.iter().enumerate() {
        for (key, value) in &ninja.vars {
            values
                .entry(key)
                .or_insert_with(|| vec![None; parsed.len()])[i] = Some(value);
        }
    }
    let mut varied: HashSet<String> = HashSet::new();
    let mut vars: Vec<(String, String)> = vec![];
    let mut required = None;
    for (name, per_version) in &values {
        if *name == "ninja_required_version" {
            required = per_version.iter().flatten().next().map(|v| v.to_string());
        } else if RESERVED.contains(name) {
            bail!("the global {name} is not supported");
        } else if per_version.iter().all(|v| v == &per_version[0]) {
            vars.push((name.to_string(), per_version[0].unwrap_or("").to_string()));
        } else {
            varied.insert(name.to_string());
        }
    }
    for (version, ninja) in &parsed {
        for (key, value) in &ninja.vars {
            if varied.contains(key) {
                vars.push((format!("{key}__{version}"), value.clone()));
            }
        }
    }

    // Pools, then rules: the same body is one; another is told apart by version.
    let mut pools: Vec<Block> = vec![];
    let mut pool_names: Vec<HashMap<String, String>> = vec![];
    let mut rules: Vec<Block> = vec![];
    let mut rule_names: Vec<HashMap<String, String>> = vec![];
    let place = |blocks: &mut Vec<Block>,
                 block: &Block,
                 body: Vec<(String, Text)>,
                 version: &str| {
        let named: Vec<&Block> = blocks
            .iter()
            .filter(|b| b.name == block.name || b.name.starts_with(&format!("{}__", block.name)))
            .collect();
        let name = match named.iter().find(|b| b.bindings == body) {
            Some(b) => b.name.clone(),
            None if named.is_empty() => block.name.clone(),
            None => format!("{}__{version}", block.name),
        };
        if !blocks.iter().any(|b| b.name == name) {
            blocks.push(Block {
                name: name.clone(),
                bindings: body,
            });
        }
        name
    };
    for (version, ninja) in &parsed {
        let mut map = HashMap::new();
        for pool in &ninja.pools {
            map.insert(
                pool.name.clone(),
                place(&mut pools, pool, pool.bindings.clone(), version),
            );
        }
        pool_names.push(map);
    }
    let pooled = |mut bindings: Vec<(String, Text)>, map: &HashMap<String, String>| {
        for (key, value) in &mut bindings {
            if key == "pool"
                && let Some(new) = literal(value).and_then(|p| map.get(&p))
            {
                *value = vec![EvalPart::Literal(new.clone())];
            }
        }
        bindings
    };
    for (i, (version, ninja)) in parsed.iter().enumerate() {
        let suffix = format!("__{version}");
        let mut map = HashMap::new();
        for rule in &ninja.rules {
            // (replaced: the configure rule)
            if rule.name == "configure" {
                continue;
            }
            let mut body = rule.bindings.clone();
            rename_bindings(&mut body, &varied, &suffix);
            let body = pooled(body, &pool_names[i]);
            map.insert(rule.name.clone(), place(&mut rules, rule, body, version));
        }
        rule_names.push(map);
    }

    // The edges, version by version, with their variables, pools and rules.
    struct Placed {
        version: String,
        edge: Edge,
    }
    let mut configure_outs: Vec<Text> = vec![];
    let mut configure_ins: [Vec<Text>; 3] = Default::default();
    let mut placed: Vec<Placed> = vec![];
    for (i, (version, ninja)) in parsed.iter().enumerate() {
        let suffix = format!("__{version}");
        for edge in &ninja.edges {
            let mut edge = edge.clone();
            for text in edge.outs.iter_mut().chain(edge.ins.iter_mut()).flatten() {
                rename(text, &varied, &suffix);
            }
            if edge.rule == "configure" {
                union(&mut configure_outs, &edge.outs[0]);
                for (into, section) in configure_ins.iter_mut().zip(&edge.ins) {
                    union(into, section);
                }
                continue;
            }
            rename_bindings(&mut edge.bindings, &varied, &suffix);
            edge.bindings = pooled(edge.bindings, &pool_names[i]);
            if edge.rule != "phony" {
                edge.rule = rule_names[i]
                    .get(&edge.rule)
                    .with_context(|| format!("{version}: no rule {}", edge.rule))?
                    .clone();
            }
            placed.push(Placed {
                version: version.clone(),
                edge,
            });
        }
    }

    // Targets that are names, which the versions make differently, are told apart.
    let mut by_outs: BTreeMap<String, Vec<usize>> = BTreeMap::new();
    for (n, p) in placed.iter().enumerate() {
        by_outs.entry(edge_outs(&p.edge)).or_default().push(n);
    }
    let mut renamed: HashMap<(String, String), String> = HashMap::new();
    let mut aggregates: Vec<(String, Vec<String>)> = vec![];
    for (outs, group) in &by_outs {
        let first = &placed[group[0]].edge;
        let differs = group.iter().any(|&n| placed[n].edge != *first);
        let phony = group.iter().all(|&n| placed[n].edge.rule == "phony");
        if !differs || phony {
            continue;
        }
        let Some(name) = alias(first) else {
            let versions: Vec<_> = group.iter().map(|&n| placed[n].version.as_str()).collect();
            bail!("{} write {outs} differently", versions.join(", "));
        };
        let mut members = vec![];
        for &n in group {
            let new = format!("{name}_{}", placed[n].version);
            renamed.insert((placed[n].version.clone(), name.clone()), new.clone());
            members.push(new);
        }
        aggregates.push((name, members));
    }
    for p in &mut placed {
        let version = p.version.clone();
        for text in p
            .edge
            .outs
            .iter_mut()
            .chain(p.edge.ins.iter_mut())
            .flatten()
        {
            if let Some(new) = literal(text).and_then(|n| renamed.get(&(version.clone(), n))) {
                *text = vec![EvalPart::Literal(new.clone())];
            }
        }
    }

    let mut out = String::from("# Generated by mwcc-tools run configure\n");
    out.push_str(&format!(
        "ninja_required_version = {}\n",
        required.as_deref().unwrap_or("1.3")
    ));
    for (key, value) in &vars {
        out.push_str(&format!(
            "{key} = {}\n",
            render(&vec![EvalPart::Literal(value.clone())], false)
        ));
    }
    for pool in &pools {
        out.push_str(&format!(
            "\npool {}\n{}",
            pool.name,
            bindings_text(&pool.bindings)
        ));
    }
    for rule in &rules {
        out.push_str(&format!(
            "\nrule {}\n{}",
            rule.name,
            bindings_text(&rule.bindings)
        ));
    }
    out.push_str(&format!(
        "\nrule configure\n  command = {configure}\n  description = CONFIGURE\n  generator = 1\n"
    ));
    // (edges that make some of the same files, in the same way, are one: the versions' downloads of a tool)
    let mut owner: HashMap<String, usize> = HashMap::new();
    let mut edges: Vec<Edge> = vec![];
    for p in placed {
        let tokens: Vec<String> = p.edge.outs.iter().flat_map(|o| paths(o)).collect();
        let owners: BTreeSet<usize> = tokens
            .iter()
            .filter_map(|t| owner.get(t).copied())
            .collect();
        let at = match owners.iter().next() {
            None => {
                edges.push(p.edge);
                edges.len() - 1
            }
            Some(&at) if owners.len() == 1 => {
                let edge = &mut edges[at];
                if edge.rule != p.edge.rule || edge.bindings != p.edge.bindings {
                    bail!("{} is built differently by {}", tokens.join(" "), p.version);
                }
                for (into, section) in edge.outs.iter_mut().zip(&p.edge.outs) {
                    union(into, section);
                }
                // (explicit in either is explicit)
                let explicit = edge.outs[0].clone();
                edge.outs[1].retain(|o| !explicit.contains(o));
                for (into, section) in edge.ins.iter_mut().zip(&p.edge.ins) {
                    union(into, section);
                }
                at
            }
            Some(_) => bail!(
                "{} is built by several edges ({})",
                tokens.join(" "),
                p.version
            ),
        };
        for token in tokens {
            owner.insert(token, at);
        }
    }
    for edge in &edges {
        out.push('\n');
        out.push_str(&edge_text(edge));
    }
    for (name, members) in &aggregates {
        out.push_str(&format!("\nbuild {name}: phony {}\n", members.join(" ")));
    }
    if !configure_outs.is_empty() {
        let edge = Edge {
            outs: [configure_outs, vec![]],
            rule: "configure".into(),
            ins: configure_ins,
            bindings: vec![],
        };
        out.push('\n');
        out.push_str(&edge_text(&edge));
    }
    let mut defaults: Vec<Text> = vec![];
    for (version, ninja) in &parsed {
        let mut targets = ninja.defaults.clone();
        for target in &mut targets {
            rename(target, &varied, &format!("__{version}"));
        }
        union(&mut defaults, &targets);
    }
    if !defaults.is_empty() {
        out.push_str(&format!("\ndefault {}\n", paths(&defaults).join(" ")));
    }
    Ok(out)
}

/// The versions a project's configure script takes: the choices of its `--version` option, in the script's help.
fn choices(help: &str) -> Option<Vec<String>> {
    let help = help.split_whitespace().collect::<Vec<_>>().join(" ");
    let at = help.find("--version {").or_else(|| help.find("-v {"))?;
    let rest = &help[help[at..].find('{')? + at + 1..];
    let list = &rest[..rest.find('}')?];
    Some(list.split(',').map(|v| v.trim().to_string()).collect())
}

/// The directories of config with a config.yml.
fn config_directories() -> Vec<String> {
    let mut versions: Vec<String> = fs::read_dir("config")
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.path().join("config.yml").is_file())
        .filter_map(|e| e.file_name().into_string().ok())
        .collect();
    versions.sort();
    versions
}

fn quote(arg: &str) -> String {
    let arg = arg.replace('$', "$$");
    if arg.is_empty() || arg.contains([' ', '\t', '"']) {
        format!("\"{}\"", arg.replace('"', "\\\""))
    } else {
        arg
    }
}

/// The exit code of `run configure` with ARGS.
pub fn run(args: &[String]) -> Result<i32> {
    let mut directory = None;
    let mut command = "uv run configure.py".to_string();
    let mut versions: Vec<String> = vec![];
    let mut default = None;
    let mut extra: &[String] = &[];
    let mut i = 0;
    while i < args.len() {
        let option = args[i].as_str();
        let mut value = || {
            i += 1;
            args.get(i)
                .cloned()
                .with_context(|| format!("{option} needs a value"))
        };
        match option {
            "-h" | "--help" => {
                println!("{USAGE}");
                return Ok(0);
            }
            "-C" => directory = Some(value()?),
            "--configure" => command = value()?,
            "--default" => default = Some(value()?),
            "--versions" => versions = value()?.split(',').map(|v| v.trim().to_string()).collect(),
            "--" => {
                extra = &args[i + 1..];
                break;
            }
            _ => bail!("unknown option {option} (the configure script's go after `--`)\n{USAGE}"),
        }
        i += 1;
    }
    if let Some(directory) = &directory {
        env::set_current_dir(directory).with_context(|| format!("-C {directory}"))?;
    }
    let configure: Vec<&str> = command.split_whitespace().collect();
    let Some((program, program_args)) = configure.split_first() else {
        bail!("--configure needs a command");
    };
    if versions.is_empty() {
        let help = Command::new(program)
            .args(program_args)
            .arg("--help")
            .output()
            .with_context(|| format!("running {command} --help"))?;
        versions =
            choices(&String::from_utf8_lossy(&help.stdout)).unwrap_or_else(config_directories);
    }
    if versions.is_empty() {
        bail!("no versions: name them with --versions");
    }
    if let Some(default) = &default
        && !versions.contains(default)
    {
        bail!(
            "--default {default} is not one of the versions ({})",
            versions.join(", ")
        );
    }

    // (the default version last, as its objdiff.json and compile_commands.json stay)
    let mut ninjas = vec![];
    let mut order: Vec<Option<&String>> = versions
        .iter()
        .filter(|v| Some(*v) != default.as_ref())
        .map(Some)
        .collect();
    order.push(default.as_ref());
    for version in order {
        let mut c = Command::new(program);
        c.args(program_args);
        match version {
            Some(version) => {
                eprintln!("{NAME}: {version}");
                c.args(["--version", version]);
            }
            None => eprintln!("{NAME}: the default version"),
        }
        let status = c
            .args(extra)
            .status()
            .with_context(|| format!("running {command}"))?;
        if !status.success() {
            eprintln!("{NAME}: {command} failed ({status})");
            return Ok(status.code().unwrap_or(1));
        }
        if let Some(version) = version {
            let ninja = fs::read_to_string("build.ninja")
                .with_context(|| format!("{version}: no build.ninja written"))?;
            ninjas.push((version.clone(), ninja));
        }
    }

    // (regenerated as it was made: `-C` is not one of its arguments, the build runs in the directory)
    let exe = env::current_exe()?;
    let mut regenerate = vec![
        quote(&exe.to_string_lossy()),
        "run".into(),
        "configure".into(),
    ];
    regenerate.extend(["--configure".into(), quote(&command)]);
    regenerate.extend(["--versions".into(), quote(&versions.join(","))]);
    if let Some(default) = &default {
        regenerate.extend(["--default".into(), quote(default)]);
    }
    if !extra.is_empty() {
        regenerate.push("--".into());
        regenerate.extend(extra.iter().map(|a| quote(a)));
    }
    fs::write("build.ninja", merge(&ninjas, &regenerate.join(" "))?)?;
    eprintln!("{NAME}: build.ninja for {} versions", versions.len());
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn version(v: &str, mw: &str, flags: &str) -> (String, String) {
        (
            v.to_string(),
            format!(
                "ninja_required_version = 1.3\nmw_version = {mw}\nldflags = -fp hardware\n\
rule mwcc\n  command = cc $cflags $in -o $out --mw $mw_version\n\
rule link\n  command = ld $ldflags\n  pool = one\n\
pool one\n  depth = 1\n\
rule download\n  command = dl $tool $out\n\
rule configure\n  command = uv run configure.py --version {v}\n  generator = 1\n\
build build/tools/dtk: download\n  tool = dtk\n\
build build/{v}/a.o: mwcc src/a.c\n  cflags = {flags}\n\
build build/{v}/main.dol: link build/{v}/a.o $\n    | build/tools/dtk\n\
build all_source: phony build/{v}/a.o\n\
build progress: link build/{v}/main.dol\n\
build build.ninja: configure | configure.py config/{v}/config.json\n\
default progress\n"
            ),
        )
    }

    #[test]
    fn versions_merge_into_one_build() {
        let merged = merge(
            &[version("A", "1", "-O4"), version("B", "2", "-O2")],
            "tools run configure",
        )
        .unwrap();
        // shared once
        assert_eq!(merged.matches("build build/tools/dtk: download").count(), 1);
        assert_eq!(merged.matches("rule mwcc\n").count(), 1);
        assert_eq!(merged.matches("rule link\n").count(), 1);
        assert_eq!(merged.matches("pool one\n").count(), 1);
        assert!(merged.contains("ldflags = -fp hardware\n"));
        // told apart: the global that differs, and the rules that use it
        assert!(merged.contains("mw_version__A = 1\n"));
        assert!(merged.contains("mw_version__B = 2\n"));
        assert!(
            merged.contains(
                "rule mwcc\n  command = cc ${cflags} ${in} -o ${out} --mw ${mw_version__A}"
            ),
            "{merged}"
        );
        assert!(merged.contains(
            "rule mwcc__B\n  command = cc ${cflags} ${in} -o ${out} --mw ${mw_version__B}"
        ));
        assert!(merged.contains("build build/B/a.o: mwcc__B src/a.c\n  cflags = -O2\n"));
        // the phony target is the union; the named targets are per version, under theirs
        assert!(merged.contains("build all_source: phony build/A/a.o build/B/a.o\n"));
        assert!(merged.contains("build progress_A: link build/A/main.dol\n"));
        assert!(merged.contains("build progress: phony progress_A progress_B\n"));
        assert!(merged.contains("build build/B/main.dol: link build/B/a.o | build/tools/dtk\n"));
        // one configure step
        assert!(merged.contains("rule configure\n  command = tools run configure\n"));
        assert!(merged.contains(
            "build build.ninja: configure | configure.py config/A/config.json config/B/config.json\n"
        ));
        assert_eq!(merged.matches("default progress\n").count(), 1);
    }

    #[test]
    fn one_file_written_differently_is_an_error() {
        let a = (
            "A".to_string(),
            "rule r\n  command = x\nbuild out.o: r a\n".to_string(),
        );
        let b = (
            "B".to_string(),
            "rule r\n  command = x\nbuild out.o: r b\n".to_string(),
        );
        let error = merge(&[a, b], "c").unwrap_err().to_string();
        assert!(error.contains("A, B write out.o differently"), "{error}");
    }

    #[test]
    fn edges_that_make_some_of_the_same_files_are_one() {
        let a = "rule d\n  command = x\nbuild c: d | t\n".to_string();
        let b = "rule d\n  command = x\nbuild d/c | d/x d/y: d | t\n".to_string();
        let merged = merge(&[("A".into(), a), ("B".into(), b)], "c").unwrap();
        assert!(merged.contains("build d/c | d/x d/y: d | t\n"), "{merged}");
    }

    #[test]
    fn the_versions_are_the_scripts_choices() {
        let help = "usage: configure.py [-h] [-v {P2ME01,D75E01,\n   G2ME01}] [--map]\n";
        assert_eq!(choices(help).unwrap(), vec!["P2ME01", "D75E01", "G2ME01"]);
        assert_eq!(choices("usage: configure.py [-h]"), None);
    }

    #[test]
    fn escapes_survive_a_round_trip() {
        let ninja =
            parse("rule r\n  command = x\nbuild a$ b C$:/x: r in1 $\n   in2\n  k = $$v\n").unwrap();
        let edge = &ninja.edges[0];
        assert_eq!(paths(&edge.outs[0]), vec!["a$ b", "C$:/x"]);
        assert_eq!(paths(&edge.ins[0]), vec!["in1", "in2"]);
        assert_eq!(edge_text(edge), "build a$ b C$:/x: r in1 in2\n  k = $$v\n");
    }
}
