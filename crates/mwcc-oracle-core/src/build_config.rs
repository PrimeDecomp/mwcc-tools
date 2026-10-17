//! Native Ninja/objdiff generation and compiler depfile conversion.
use crate::{compare, executable, source_tree::SourceTree};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, path::Path};
pub mod depfile;
pub const VERSIONS: [&str; 9] = [
    "GC_1_0",
    "GC_1_1",
    "GC_1_1p1",
    "GC_1_2_5",
    "GC_1_2_5n",
    "GC_1_3",
    "GC_1_3_2",
    "GC_2_6",
    "GC_2_7",
];
pub const PRIMARY: &str = "GC_1_2_5";
pub const LIBRARY_OBJECTS: [(&str, &str); 2] = [
    ("cexc.obj", "lib/runtime/cexc.obj"),
    ("ccinit.obj", "lib/runtime/ccinit.obj"),
];
#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub enum Platform {
    Windows,
    Posix,
}
impl Platform {
    pub fn current() -> Self {
        if cfg!(windows) {
            Self::Windows
        } else {
            Self::Posix
        }
    }
    pub fn windows(self) -> bool {
        matches!(self, Self::Windows)
    }
}
/// Match subprocess.list2cmdline/shlex.quote for the legacy compiler arguments.
pub fn quote(argument: &str, platform: Platform) -> String {
    if !platform.windows() {
        if !argument.is_empty()
            && argument
                .chars()
                .all(|c| c.is_ascii_alphanumeric() || "_@%+=:,./-".contains(c))
        {
            return argument.into();
        }
        return format!("'{}'", argument.replace('\'', "'\"'\"'"));
    }
    let quoted = argument.is_empty() || argument.contains([' ', '\t']);
    let mut out = String::new();
    if quoted {
        out.push('"');
    }
    let mut backslashes = 0;
    for c in argument.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.push_str(&"\\".repeat(backslashes * 2 + 1));
                out.push('"');
                backslashes = 0;
            }
            _ => {
                out.push_str(&"\\".repeat(backslashes));
                backslashes = 0;
                out.push(c);
            }
        }
    }
    out.push_str(&"\\".repeat(backslashes));
    if quoted {
        out.push_str(&"\\".repeat(backslashes));
        out.push('"');
    }
    out
}
fn join(arguments: &[String], platform: Platform) -> String {
    arguments
        .iter()
        .map(|a| quote(a, platform))
        .collect::<Vec<_>>()
        .join(" ")
        .replace('$', "$$")
}
fn ninja_path(path: &str) -> String {
    path.replace('$', "$$")
        .replace(' ', "$ ")
        .replace(':', "$:")
}
pub fn pro4_arguments(arguments: &[String]) -> Result<Vec<String>> {
    let mut result = vec![];
    let mut args = arguments.iter();
    while let Some(arg) = args.next() {
        result.push(if arg == "-o" {
            format!("/Fo{}", args.next().context("-o lacks output")?)
        } else if let Some(arg) = arg.strip_prefix('-') {
            format!("/{arg}")
        } else {
            arg.clone()
        });
    }
    Ok(result)
}
#[derive(Clone, Debug)]
pub struct Request {
    pub version: String,
    pub mode: Option<String>,
    pub split_only: bool,
    pub oracle: bool,
    pub rust_hooks: bool,
    pub tools: String,
    pub platform: Platform,
}
#[derive(Debug)]
pub struct Generation {
    pub ninja: String,
    pub objdiff: Option<Value>,
    pub mode: String,
    /// The normal link's inputs, in link mode.
    pub link_inputs: Option<Vec<String>>,
}
fn strings(value: &Value) -> Result<Vec<String>> {
    value
        .as_array()
        .context("expected string array")?
        .iter()
        .map(|v| {
            v.as_str()
                .map(str::to_owned)
                .context("array item is not text")
        })
        .collect()
}
fn optional_strings(value: &Value) -> Result<Vec<String>> {
    if value.is_null() {
        Ok(vec![])
    } else {
        strings(value)
    }
}
fn text<'a>(value: &'a Value, key: &str) -> Result<&'a str> {
    value[key]
        .as_str()
        .with_context(|| format!("settings lack {key}"))
}
fn includes(settings: &Value, key: &str, prefix: &str) -> Result<Vec<String>> {
    Ok(optional_strings(&settings[key])?
        .into_iter()
        .map(|s| format!("-I{prefix}{s}"))
        .collect())
}
fn host(config: &Value, settings: &Value, source: &str) -> Result<String> {
    if let Some(value) = config["compiler_overrides"][source]
        .as_str()
        .filter(|s| !s.is_empty())
    {
        return Ok(value.into());
    }
    let compiler = text(settings, "compiler")?;
    Ok(config["compilers"][compiler]
        .as_str()
        .unwrap_or(compiler)
        .into())
}
fn flags(config: &Value, settings: &Value, source: Option<&str>) -> Result<Vec<String>> {
    // (a vendored tree's config.json gives a source's whole flags: crate::source_tree)
    if let Some(flags) = source.map(|s| &config["flags"][s]).filter(|f| f.is_array()) {
        return strings(flags);
    }
    Ok(strings(&settings["flags"])?
        .into_iter()
        .map(|f| config["flags"][&f].as_str().unwrap_or(&f).to_owned())
        .collect())
}
fn listed(config: &Value, key: &str, source: &str) -> bool {
    config[key]
        .as_array()
        .is_some_and(|a| a.iter().any(|v| v == source))
}
fn compiler_arguments(
    config: &Value,
    settings: &Value,
    version: usize,
    source: Option<&str>,
    oracle: bool,
    prefix: &str,
) -> Result<Vec<String>> {
    let mut flags = flags(config, settings, source)?;
    if let Some(source) = source {
        for (key, value) in [("sym_on", "on"), ("sym_off", "off")] {
            if listed(config, key, source)
                && let Some(i) = flags.iter().position(|s| s == "-sym")
            {
                ensure!(i + 1 < flags.len(), "-sym lacks a value");
                flags[i + 1] = value.into();
            }
        }
    }
    let mut out = vec!["-c".into()];
    out.extend(flags);
    out.push(format!("-DVERSION={version}"));
    if oracle {
        out.push("-DMWCC_ORACLE_TRACE".into());
    }
    out.extend(includes(settings, "include_dirs", prefix)?);
    out.extend([format!("-I{prefix}include"), "-i-".into()]);
    out.extend(includes(settings, "system_include_dirs", prefix)?);
    out.push(format!("-I{prefix}include/libc"));
    Ok(out)
}
fn library(unit: &str) -> Option<&'static str> {
    LIBRARY_OBJECTS
        .iter()
        .find(|(n, _)| *n == unit)
        .map(|(_, p)| *p)
}
fn objdiff(tree: &SourceTree, config: &Value, sources: &Value, units: &[String]) -> Value {
    let build = &tree.build_dir;
    let prefix = &tree.prefix;
    let units:Vec<_>=units.iter().map(|unit| {
        let runtime=unit.starts_with("src/msl/") || unit.starts_with("src/runtime/") || library(unit).is_some();
        let name=unit.strip_prefix("src/").map_or(unit.as_str(),|s|s.rsplit_once('.').map_or(s,|(s,_)|s));
        let mut entry=json!({"name":name,"target_path":format!("{build}/obj/{unit}.obj"),"metadata":{"complete":library(unit).is_some(),"progress_categories":[if runtime {"runtime"} else {"compiler"}]}});
        if sources.get(unit).is_some() {entry["base_path"]=json!(format!("{build}/compiled/{unit}.obj"));entry["metadata"]["complete"]=json!(compare::matching(config,sources,unit));entry["metadata"]["source_path"]=json!(format!("{prefix}{unit}"));}
        else if let Some(path)=library(unit) {entry["base_path"]=json!(path);}
        else {entry["metadata"]["auto_generated"]=json!(true);}
        entry
    }).collect();
    json!({"$schema":"https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json","min_version":"3.8.0","custom_make":"ninja","build_target":false,"build_base":true,"watch_patterns":["src/**/*.c","src/**/*.cpp","include/**/*.h","config/**/*.json","config/**/*.txt"],"units":units,"progress_categories":[{"id":"compiler","name":"Compiler"},{"id":"runtime","name":"MSL and runtime"}]})
}
/// Sources and their compiler arguments.
pub type SourceArguments = Vec<(String, Vec<String>)>;

/// Each source of VERSION's tree with the arguments the build compiles it with (`-c`, its flags, the version's
/// define and include paths, as `generate` gives them), in `sources.json`'s order.
pub fn source_arguments(root: &Path, version: &str) -> Result<(SourceTree, SourceArguments)> {
    let version_index = VERSIONS
        .iter()
        .position(|v| *v == version)
        .context("unsupported build version")?;
    let ours = root.join(format!("config/{version}/config.json"));
    let ours: Value = serde_json::from_slice(
        &fs::read(&ours).with_context(|| format!("read {}", ours.display()))?,
    )?;
    let tree = SourceTree::of(root, version, false)?;
    let config = tree.config(root, &ours)?;
    let define = tree.version_define(root, version)?.unwrap_or(version_index);
    let sources = tree.sources(root)?;
    let mut out = vec![];
    for (source, settings) in sources
        .as_object()
        .context("source settings are not an object")?
    {
        let arguments =
            compiler_arguments(&config, settings, define, Some(source), false, &tree.prefix)?;
        out.push((source.clone(), arguments));
    }
    Ok((tree, out))
}

pub fn generate(root: &Path, request: &Request) -> Result<Generation> {
    ensure!(
        !request.rust_hooks || request.oracle,
        "Rust producer requires --oracle"
    );
    let version = &request.version;
    let version_index = VERSIONS
        .iter()
        .position(|v| v == version)
        .context("unsupported build version")?;
    let read_json = |relative: &str| -> Result<Value> {
        let path = root.join(relative);
        serde_json::from_slice(
            &fs::read(&path).with_context(|| format!("read {}", path.display()))?,
        )
        .with_context(|| format!("parse {}", path.display()))
    };
    let tree = SourceTree::of(root, version, request.oracle)?;
    let config = tree.config(root, &read_json(&format!("config/{version}/config.json"))?)?;
    let sources = tree.sources(root)?;
    let define = tree.version_define(root, version)?.unwrap_or(version_index);
    let (prefix, build) = (tree.prefix.as_str(), tree.build_dir.as_str());
    let source_settings = sources
        .as_object()
        .context("source settings are not an object")?;
    let split_file = format!("{}/splits.txt", tree.config_dir);
    let has_splits = root.join(&split_file).is_file();
    let mode = request
        .mode
        .as_deref()
        .or(if request.oracle { Some("link") } else { None })
        .or_else(|| config["mode"].as_str())
        .unwrap_or(if has_splits { "link" } else { "compare" });
    ensure!(
        ["link", "compare"].contains(&mode),
        "unsupported build mode {mode}"
    );
    let linking = mode == "link";
    ensure!(
        !request.oracle || linking,
        "Oracle instrumentation requires --mode link"
    );
    ensure!(
        !linking || has_splits,
        "{split_file} does not exist: --mode link needs the version's split"
    );
    let platform = request.platform;
    let windows = platform.windows();
    let objdiff_tool = if windows {
        "build/tools/objdiff-cli.exe"
    } else {
        "build/tools/objdiff-cli"
    };
    let tool_dep = if windows { "" } else { "$wibo " };
    let environment = if windows {
        "cmd /c set MWCIncludes=include/libc&& "
    } else {
        "MWCIncludes=include/libc "
    };
    let tools = if windows {
        format!("\"{}\"", request.tools.trim_matches('"'))
    } else {
        quote(&request.tools, platform)
    }
    .replace('$', "$$");
    let mut out = vec![
        "# Generated by mwcc-tools configure".into(),
        format!("tools = {tools}"),
        format!("tools_dep = {}", ninja_path(&request.tools)),
        format!("wibo = {}", if windows { "" } else { "build/tools/wibo" }),
        String::new(),
        "pool downloads".into(),
        "  depth = 1".into(),
        "rule download".into(),
        "  command = $tools download $tool $out".into(),
        "  description = DOWNLOAD $out".into(),
        "  pool = downloads".into(),
        "rule mwcc".into(),
        format!(
            "  command = {environment}$wibo $compiler_command $cflags -MD $in -o $out && $tools transform-dep $stem.dep $out"
        ),
        "  description = MWCC $in".into(),
        "  depfile = $out.d".into(),
        "  deps = gcc".into(),
        "rule mwcc_pro4".into(),
        format!("  command = {environment}$wibo $compiler_command $cflags"),
        "  description = MWCC $in".into(),
        "rule configure".into(),
        format!(
            "  command = $tools configure --version {version} --mode {mode}{}{}{}",
            if request.split_only {
                " --split-only"
            } else {
                ""
            },
            if request.oracle { " --oracle" } else { "" },
            if request.oracle && request.rust_hooks {
                " --producer rust"
            } else if request.oracle {
                " --producer c"
            } else {
                ""
            }
        ),
        "  generator = 1".into(),
        String::new(),
        format!("version = {version}"),
        format!("build {objdiff_tool}: download"),
        "  tool = objdiff-cli".into(),
    ];
    if tree.vendored() {
        // (a vendored tree's own C library headers and lib/: crate::source_tree)
        let environment = environment.replace("include/libc", &format!("{prefix}include/libc"));
        out.extend([
            "rule mwcc_tree".into(),
            format!(
                "  command = {environment}$wibo $compiler_command $cflags -MD $in -o $out && $tools transform-dep $stem.dep $out"
            ),
            "  description = MWCC $in".into(),
            "  depfile = $out.d".into(),
            "  deps = gcc".into(),
            "rule mwcc_pro4_tree".into(),
            format!("  command = {environment}$wibo $compiler_command $cflags"),
            "  description = MWCC $in".into(),
            format!("build {prefix}lib/ok.pro53: download"),
            "  tool = lib-2x".into(),
        ]);
    }
    let (rule, rule_pro4, lib_ok) = if tree.vendored() {
        (
            "mwcc_tree",
            "mwcc_pro4_tree",
            format!("{prefix}lib/ok.pro53"),
        )
    } else {
        ("mwcc", "mwcc_pro4", "lib/ok".to_owned())
    };
    if !windows {
        out.extend([
            "build build/tools/wibo: download".into(),
            "  tool = wibo".into(),
        ]);
    }
    let mut originals = vec![];
    for version in VERSIONS {
        let c = read_json(&format!("config/{version}/config.json"))?;
        originals.push(text(&c, "original")?.to_owned());
    }
    out.extend([
        format!(
            "build build/compilers/ok {}: download",
            originals
                .iter()
                .map(|p| ninja_path(p))
                .collect::<Vec<_>>()
                .join(" ")
        ),
        "  tool = compilers".into(),
        format!(
            "build lib/ok {} lib/win32sdk/KERNEL32.LIB: download",
            LIBRARY_OBJECTS
                .iter()
                .map(|(_, p)| *p)
                .collect::<Vec<_>>()
                .join(" ")
        ),
        "  tool = lib".into(),
    ]);
    let compilers: BTreeSet<_> = source_settings
        .iter()
        .map(|(n, s)| host(&config, s, n))
        .collect::<Result<_>>()?;
    for compiler in compilers {
        let stub = windows && compiler != "pro4";
        out.extend([
            format!(
                "build build/compilers/{compiler}/mwcc.exe{}: download{}",
                if stub {
                    format!(" build/compilers/{compiler}/lmgr326b.dll")
                } else {
                    String::new()
                },
                if stub {
                    " | tools/lmgr_stub/lmgr326b.dll"
                } else {
                    ""
                }
            ),
            format!("  tool = {compiler}"),
        ]);
    }
    let mut objects = vec![];
    for (source, settings) in source_settings {
        let object = format!("{build}/compiled/{source}.obj");
        let input = format!("{prefix}{source}");
        let host = host(&config, settings, source)?;
        let compiler = format!("build/compilers/{host}/mwcc.exe");
        let mut args = compiler_arguments(&config, settings, define, Some(source), false, prefix)?;
        if host == "pro4" {
            args.extend([input.clone(), "-o".into(), object.clone()]);
            out.extend([
                format!(
                    "build {}: {rule_pro4} {} | {tool_dep}{compiler} || {lib_ok}",
                    ninja_path(&object),
                    ninja_path(&input)
                ),
                format!("  cflags = {}", join(&pro4_arguments(&args)?, platform)),
            ]);
        } else {
            out.extend([
                format!(
                    "build {}: {rule} {} | {tool_dep}{compiler} || {lib_ok}",
                    ninja_path(&object),
                    ninja_path(&input)
                ),
                format!("  cflags = {}", join(&args, platform)),
                format!(
                    "  stem = {}",
                    Path::new(source)
                        .file_stem()
                        .context("source lacks stem")?
                        .to_string_lossy()
                ),
            ]);
        }
        out.push(format!(
            "  compiler_command = {}",
            if windows {
                compiler.replace('/', "\\")
            } else {
                compiler
            }
        ));
        objects.push(object);
    }
    let mut objdiff_config = None;
    let mut link_inputs = None;
    if linking {
        let splits = executable::read_splits(&fs::read_to_string(root.join(&split_file))?)?;
        let units: Vec<_> = splits.units.iter().map(|u| u.name.clone()).collect();
        let split_objects: Vec<_> = units
            .iter()
            .map(|u| format!("{build}/obj/{u}.obj"))
            .collect();
        let extra = if splits.sections.contains_key(".rsrc") {
            vec![format!("{build}/obj/resources.res")]
        } else {
            vec![]
        };
        let mut inputs = vec![format!("{build}/obj/sections.obj")];
        inputs.extend(extra.clone());
        for unit in &units {
            inputs.push(if let Some(path) = library(unit) {
                path.into()
            } else if sources.get(unit).is_some()
                && compare::matching(&config, &sources, unit)
                && !request.split_only
            {
                format!("{build}/compiled/{unit}.obj")
            } else {
                format!("{build}/obj/{unit}.obj")
            });
        }
        let libraries = if config.get("imports").is_some() {
            strings(&config["imports"])?
        } else {
            vec!["lib/win32sdk/KERNEL32.LIB".into()]
        };
        let imports: Vec<_> = libraries
            .iter()
            .map(|p| {
                if p.ends_with(".def") {
                    format!(
                        "build/{}.lib",
                        Path::new(p).file_stem().unwrap().to_string_lossy()
                    )
                } else {
                    p.clone()
                }
            })
            .collect();
        inputs.extend(imports.clone());
        inputs.push("build/lmgr326b.lib".into());
        if request.oracle {
            let oracle = &config["oracle"];
            if request.rust_hooks {
                let hooks = optional_strings(&oracle["hooks"])?;
                ensure!(
                    crate::producer::HOOKS
                        .iter()
                        .filter(|name| !crate::producer::ARGUMENT_HOOKS.contains(name)
                            && !crate::producer::ARENA_HOOKS.contains(name)
                            && !crate::producer::SOURCE_HOOKS.contains(name))
                        .chain(crate::producer::frontend_hooks(version)?.iter())
                        .all(|name| hooks.iter().any(|h| h == name)),
                    "this version does not expose all migrated Rust hooks"
                );
            }
            let oracle_units = optional_strings(&oracle["units"])?;
            ensure!(
                !oracle_units.is_empty()
                    || oracle["hooks"].as_array().is_some_and(|a| !a.is_empty()),
                "config/{version}/config.json lists no oracle units or hooks"
            );
            let mut oracle_extra = vec!["src/oracle/OracleTrace.c".to_owned()];
            oracle_extra.extend(optional_strings(&oracle["extra"])?);
            if request.rust_hooks {
                oracle_extra.retain(|unit| unit != "src/oracle/Hooks.c");
            }
            let template = oracle["template"].as_str().unwrap_or_else(|| {
                oracle_units
                    .first()
                    .map_or("src/driver/CLWriteObjectFile.c", String::as_str)
            });
            let template = sources
                .get(template)
                .context("oracle template source is absent")?;
            let trace_object = format!("build/{version}/oracle/OracleTrace.obj");
            let mut oracle_inputs = inputs[..extra.len() + 1].to_vec();
            for unit in &units {
                oracle_inputs.push(if oracle_units.contains(unit) {
                    format!("build/{version}/oracle/compiled/{unit}.obj")
                } else if let Some(path) = library(unit) {
                    path.into()
                } else {
                    format!("build/{version}/obj/{unit}.obj")
                });
            }
            oracle_inputs.extend(imports.clone());
            oracle_inputs.push("build/lmgr326b.lib".into());
            let extra_objects: Vec<_> = oracle_extra
                .iter()
                .map(|u| {
                    if u == "src/oracle/OracleTrace.c" {
                        trace_object.clone()
                    } else {
                        format!("build/{version}/oracle/compiled/{u}.obj")
                    }
                })
                .collect();
            oracle_inputs.extend(extra_objects.clone());
            if request.rust_hooks {
                let object = format!("build/{version}/oracle/compiled/rust/hooks.obj");
                let dependencies = crate::producer::SOURCES
                    .iter()
                    .map(|name| format!("crates/mwcc-oracle-hooks/src/{name}"))
                    .collect::<Vec<_>>()
                    .join(" ");
                out.extend([
                    "rule rust_hooks".into(),
                    "  command = $tools build-hooks $out".into(),
                    "  description = RUST HOOKS $out".into(),
                    format!(
                        "build {object}: rust_hooks {dependencies} | $tools_dep rust-toolchain.toml"
                    ),
                ]);
                oracle_inputs.push(object);
                let frontend = format!("build/{version}/oracle/compiled/rust/frontend.obj");
                let frontend_dependencies = crate::producer::FRONTEND_SOURCES
                    .iter()
                    .map(|name| format!("crates/mwcc-oracle-hooks/src/{name}"))
                    .collect::<Vec<_>>()
                    .join(" ");
                out.extend([
                    "rule rust_frontend".into(),
                    format!("  command = $tools build-hooks $out --frontend-version {version}"),
                    "  description = RUST FRONTEND $out".into(),
                    format!("build {frontend}: rust_frontend {frontend_dependencies} | $tools_dep rust-toolchain.toml"),
                ]);
                oracle_inputs.push(frontend);
                let trace_dependencies = crate::producer::TRACE_SOURCES
                    .iter()
                    .map(|name| format!("crates/mwcc-oracle-hooks/src/{name}"))
                    .collect::<Vec<_>>()
                    .join(" ");
                out.extend([
                    "rule rust_trace".into(),
                    "  command = $tools build-hooks $out --trace-shim".into(),
                    "  description = RUST TRACE $out".into(),
                    format!("build {trace_object}: rust_trace {trace_dependencies} | $tools_dep rust-toolchain.toml"),
                ]);
            }
            let emitted =
                oracle_extra
                    .iter()
                    .cloned()
                    .zip(extra_objects)
                    .chain(oracle_units.iter().map(|u| {
                        (
                            u.clone(),
                            format!("build/{version}/oracle/compiled/{u}.obj"),
                        )
                    }));
            for (unit, object) in emitted {
                if request.rust_hooks && unit == "src/oracle/OracleTrace.c" {
                    continue;
                }
                let settings = sources.get(&unit).unwrap_or(template);
                let compiler = format!(
                    "build/compilers/{}/mwcc.exe",
                    host(&config, settings, &unit)?
                );
                let mut args =
                    compiler_arguments(&config, settings, version_index, None, true, "")?;
                if request.rust_hooks {
                    args.push("-DMWCC_ORACLE_RUST_HOOKS".into());
                    args.push("-DMWCC_ORACLE_RUST_FRONTEND".into());
                }
                out.extend([
                    format!(
                        "build {}: mwcc {} | {tool_dep}{compiler} || lib/ok",
                        ninja_path(&object),
                        ninja_path(&unit)
                    ),
                    format!("  cflags = {}", join(&args, platform)),
                    format!(
                        "  stem = {}",
                        Path::new(&unit).file_stem().unwrap().to_string_lossy()
                    ),
                    format!(
                        "  compiler_command = {}",
                        if windows {
                            compiler.replace('/', "\\")
                        } else {
                            compiler
                        }
                    ),
                ]);
            }
            out.extend(["rule oracle_link".into(),"  command = $tools link --oracle $version $out $in".into(),"  description = LINK ORACLE $out".into(),
                format!("build build/{version}/oracle/mwcceppc.exe: oracle_link {} | {tool_dep}build/compilers/pro53/mwld.exe $tools_dep || lib/ok",oracle_inputs.iter().map(|p|ninja_path(p)).collect::<Vec<_>>().join(" "))]);
        }
        out.extend(["rule split".into(),format!("  command = $tools split $version{}",if tree.vendored() {" --source"} else {""}),"  description = SPLIT $version".into(),"rule implib".into(),"  command = $tools implib $in $out".into(),"  description = IMPLIB $out".into(),
            "rule link".into(),format!("  command = $tools link $version $out $in{}",if tree.vendored() {" --source"} else {""}),"  description = LINK $out".into(),"rule report".into(),format!("  command = {objdiff_tool} report generate -o $out"),"  description = REPORT $version".into(),
            "build build/compilers/pro53/mwld.exe: download".into(),"  tool = mwld".into(),
            format!("build {build}/obj/ok {build}/obj/sections.obj {}: split | {} config/{version}/config.json {}/symbols.txt {split_file} $tools_dep",extra.iter().chain(split_objects.iter()).map(|p|ninja_path(p)).collect::<Vec<_>>().join(" "),ninja_path(text(&config,"original")?),tree.config_dir),
            "build build/lmgr326b.lib: implib config/lmgr326b.def | $tools_dep".into()]);
        for library in libraries.iter().filter(|p| p.ends_with(".def")) {
            out.push(format!(
                "build build/{}.lib: implib {} | $tools_dep",
                Path::new(library).file_stem().unwrap().to_string_lossy(),
                ninja_path(library)
            ));
        }
        out.extend([format!("build build/{version}/mwcceppc.exe | build/{version}/ok: link {} | {tool_dep}build/compilers/pro53/mwld.exe $tools_dep || lib/ok",inputs.iter().map(|p|ninja_path(p)).collect::<Vec<_>>().join(" ")),
            format!("build build/{version}/report.json: report | build/{version}/ok {objdiff_tool} {}",objects.iter().map(|p|ninja_path(p)).collect::<Vec<_>>().join(" "))]);
        objdiff_config = Some(objdiff(&tree, &config, &sources, &units));
        link_inputs = Some(inputs);
    } else {
        let configs = format!(
            "config/{version}/config.json config/{version}/functions.json config/{version}/bindings.json config/sources.json"
        );
        out.extend([
            "rule extract".into(),
            "  command = $tools compare extract $version".into(),
            "  description = EXTRACT $version".into(),
            "rule compare".into(),
            "  command = $tools compare compare $version".into(),
            "  description = COMPARE $version".into(),
            "rule report".into(),
            "  command = $tools compare report $version".into(),
            "  description = REPORT $version".into(),
            format!(
                "build build/{version}/target/ok: extract | {} {configs} $tools_dep",
                ninja_path(text(&config, "original")?)
            ),
            format!(
                "build build/{version}/ok objdiff.json: compare | build/{version}/target/ok {}",
                objects
                    .iter()
                    .map(|p| ninja_path(p))
                    .collect::<Vec<_>>()
                    .join(" ")
            ),
            format!(
                "build build/{version}/report.json: report | build/{version}/ok {objdiff_tool}"
            ),
        ]);
    }
    // (the compiler of the tree's sources alone, on request: ninja build/VERSION/<tree>/source-only/mwcceppc.exe)
    if tree.vendored()
        && let Some(own) = crate::source_only::tree(root, version)?
    {
        let mut inputs: Vec<String> = crate::source_only::IMPORT_LIBRARIES
            .iter()
            .map(|name| format!("{prefix}src/imports/{name}.def"))
            .collect();
        let resources = crate::source_only::RESOURCES;
        inputs.extend([
            format!("{prefix}{resources}"),
            format!("{prefix}{}", resources.replace(".rc", ".ico")),
            format!("{prefix}include/version.h"),
            format!("{prefix}{}", crate::source_only::LICENSING),
            format!("{}/functions.json", own.config_dir),
            format!("{}/bindings.json", own.config_dir),
            "build/compilers/pro53/mwld.exe".into(),
            lib_ok.clone(),
        ]);
        out.extend([
            "rule source_only".into(),
            "  command = $tools source-only $version".into(),
            "  description = LINK (source only) $out".into(),
            format!(
                "build {}: source_only {} | {tool_dep}{} $tools_dep",
                crate::source_only::output(&own),
                objects
                    .iter()
                    .map(|p| ninja_path(p))
                    .collect::<Vec<_>>()
                    .join(" "),
                inputs
                    .iter()
                    .map(|p| ninja_path(p))
                    .collect::<Vec<_>>()
                    .join(" ")
            ),
        ]);
        // (with the Rust hooks: the oracle without the original, on request: ninja
        // build/VERSION/<tree>/source-oracle/mwcceppc.exe)
        let [hooks, frontend, trace] = crate::source_oracle::rust_objects(&own);
        let sources = |names: &[&str]| {
            names
                .iter()
                .map(|name| format!("crates/mwcc-oracle-hooks/src/{name}"))
                .collect::<Vec<_>>()
                .join(" ")
        };
        inputs.extend([
            hooks.clone(),
            frontend.clone(),
            trace.clone(),
            format!("config/{version}/symbols.txt"),
            format!("config/{version}/config.json"),
            format!("{}/symbols.txt", own.config_dir),
            format!("{}/splits.txt", own.config_dir),
            format!("{}/state.json", own.config_dir),
        ]);
        out.extend([
            "rule rust_hooks".into(),
            "  command = $tools build-hooks $out".into(),
            "  description = RUST HOOKS $out".into(),
            format!(
                "build {hooks}: rust_hooks {} | $tools_dep rust-toolchain.toml",
                sources(&crate::producer::SOURCES)
            ),
            "rule rust_frontend".into(),
            format!("  command = $tools build-hooks $out --frontend-version {version}"),
            "  description = RUST FRONTEND $out".into(),
            format!(
                "build {frontend}: rust_frontend {} | $tools_dep rust-toolchain.toml",
                sources(&crate::producer::FRONTEND_SOURCES)
            ),
            "rule rust_trace".into(),
            "  command = $tools build-hooks $out --trace-shim".into(),
            "  description = RUST TRACE $out".into(),
            format!(
                "build {trace}: rust_trace {} | $tools_dep rust-toolchain.toml",
                sources(&crate::producer::TRACE_SOURCES)
            ),
            "rule source_oracle".into(),
            "  command = $tools source-only $version --oracle".into(),
            "  description = LINK (source-built oracle) $out".into(),
            format!(
                "build {}: source_oracle {} | {tool_dep}{} $tools_dep",
                crate::source_oracle::output(&own),
                objects
                    .iter()
                    .map(|p| ninja_path(p))
                    .collect::<Vec<_>>()
                    .join(" "),
                inputs
                    .iter()
                    .map(|p| ninja_path(p))
                    .collect::<Vec<_>>()
                    .join(" ")
            ),
        ]);
    }
    out.extend([format!("build all_source: phony {}",objects.iter().map(|p|ninja_path(p)).collect::<Vec<_>>().join(" ")),format!("build progress: phony build/{version}/report.json"),
        format!("build build.ninja: configure | $tools_dep {prefix}config/sources.json config/{version}/config.json{}{}",if tree.vendored() {format!(" {}/config.json {}/state.json",tree.config_dir,tree.config_dir)} else {String::new()},if linking {format!(" {split_file}")} else {String::new()}),format!("default build/{version}/ok all_source"),String::new()]);
    Ok(Generation {
        ninja: out.join("\n"),
        objdiff: objdiff_config,
        mode: mode.into(),
        link_inputs,
    })
}
pub fn run(root: &Path, request: &Request) -> Result<Generation> {
    let generated = generate(root, request)?;
    compare::write_text(root, "build.ninja", &generated.ninja)?;
    if let Some(objdiff) = &generated.objdiff {
        compare::write_json(root, "objdiff.json", objdiff)?;
    }
    Ok(generated)
}
