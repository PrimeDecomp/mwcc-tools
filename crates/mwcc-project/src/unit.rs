//! A project's units as its build compiles them ([`Project`], [`Unit`]: objdiff.json's units, the build.ninja edges
//! that compile them, report.json's measures), and a unit's compile with an overlay of its files ([`compile_unit`]).
use crate::{
    cache::{self, CompileCache},
    compilers,
    ninja::{self, CompileRule},
    paths::display_path,
    process_tree,
};
use anyhow::{Context, Result, bail, ensure};
use mwcc_trace::{
    options::{TRACE_ENV, TraceOptions},
    version::Version,
};
use mwcc_wasm::{Overlay, PersistPool, pool};
use regex::Regex;
use serde::Serialize;
use serde_json::Value;
use sha1::{Digest, Sha1};
use std::{
    fs,
    path::{Path, PathBuf},
    process::Stdio,
    sync::{Arc, LazyLock},
    time::{Duration, Instant},
};

/// A project's path as its files spell it (`/` or `\`), on this host.
fn host_path(directory: &Path, relative: &str) -> PathBuf {
    directory.join(relative.replace(['\\', '/'], std::path::MAIN_SEPARATOR_STR))
}

/// A path as two spellings of it compare (on any host: the overlay's keys ignore case).
fn file_key(path: &Path) -> String {
    display_path(path).replace('\\', "/").to_ascii_lowercase()
}

/// A decomp project: its objdiff.json and its build.ninja's compile edges, read once.
pub struct Project {
    /// Its directory (objdiff.json's and build.ninja's), absolute.
    pub root: PathBuf,
    objdiff: Value,
    rules: Vec<CompileRule>,
    /// report.json, as its build.ninja writes it (the `report` edge's output).
    report: Option<PathBuf>,
}

/// A module's objects ([`Project::module_objects`]).
#[derive(Clone, Debug, Default, Serialize)]
pub struct ModuleObjects {
    pub targets: Vec<PathBuf>,
    pub linked: Vec<PathBuf>,
}

/// One unit, as the project's build compiles it.
#[derive(Clone, Debug, Serialize)]
pub struct Unit {
    pub name: String,
    /// The source, as the edge spells it (from `directory`).
    pub source: String,
    /// The edge's compiler flags, as the shell gives them (a `-pragma` value unquoted), without `-MMD`/`-MD`.
    pub flags: Vec<String>,
    /// The edge's `mw_version` (`GC/2.7`), and the version it names, if the oracle knows it.
    pub mw_version: Option<String>,
    pub version: Option<Version>,
    /// The project's compiler, and the sjiswrap that runs it (its sources are UTF-8, read as Shift JIS).
    pub compiler: PathBuf,
    pub sjiswrap: Option<PathBuf>,
    /// The edge precompiles a header (`-precompile`).
    pub precompile: bool,
    /// objdiff's target object and the build's object (absolute).
    pub target_path: Option<PathBuf>,
    pub base_path: PathBuf,
    /// The edge's working directory (build.ninja's).
    pub directory: PathBuf,
    /// The unit's entry of objdiff.json.
    pub metadata: Value,
}

impl Project {
    pub fn open(root: &Path) -> Result<Self> {
        let root = PathBuf::from(display_path(
            &fs::canonicalize(root)
                .with_context(|| format!("project directory {} missing", root.display()))?,
        ));
        let objdiff: Value = serde_json::from_slice(
            &fs::read(root.join("objdiff.json"))
                .with_context(|| format!("read {}/objdiff.json", root.display()))?,
        )?;
        let ninja = fs::read_to_string(root.join("build.ninja"))
            .with_context(|| format!("read {}/build.ninja", root.display()))?;
        static REPORT: LazyLock<Regex> =
            LazyLock::new(|| Regex::new(r"(?m)^build (\S+): report\b").unwrap());
        let report = REPORT.captures(&ninja).map(|c| host_path(&root, &c[1]));
        Ok(Self {
            rules: ninja::parse(&ninja)?,
            root,
            objdiff,
            report,
        })
    }

    fn units(&self) -> Result<&Vec<Value>> {
        self.objdiff["units"]
            .as_array()
            .context("objdiff.json has no units")
    }

    /// The names of objdiff.json's units.
    pub fn unit_names(&self) -> Result<Vec<String>> {
        Ok(self
            .units()?
            .iter()
            .filter_map(|u| u["name"].as_str().map(String::from))
            .collect())
    }

    /// The unit NAME: objdiff.json's entry, and the edge that builds its `base_path`.
    pub fn unit(&self, name: &str) -> Result<Unit> {
        let entry = self
            .units()?
            .iter()
            .find(|u| u["name"] == name)
            .with_context(|| format!("Unknown unit {name}"))?;
        let base = entry["base_path"]
            .as_str()
            .with_context(|| format!("{name} has no base_path"))?;
        let wanted = ninja::path_key(&self.root, base);
        let rule = self
            .rules
            .iter()
            .find(|r| {
                r.outputs
                    .iter()
                    .any(|o| ninja::path_key(&self.root, o) == wanted)
            })
            .with_context(|| format!("build.ninja has no mwcc edge for {base}"))?;
        let compiler = rule.compiler.as_deref().with_context(|| {
            format!(
                "the rule {} of {base} runs no compiler build.ninja names",
                rule.rule
            )
        })?;
        Ok(Unit {
            name: name.into(),
            source: rule.source.clone(),
            flags: rule
                .flags
                .iter()
                .filter(|f| !matches!(f.as_str(), "-MMD" | "-MD"))
                .cloned()
                .collect(),
            mw_version: rule.mw_version.clone(),
            version: rule
                .mw_version
                .as_deref()
                .and_then(|v| Version::parse(v).ok()),
            compiler: host_path(&self.root, compiler),
            sjiswrap: rule.sjiswrap.as_deref().map(|s| host_path(&self.root, s)),
            precompile: rule.precompile,
            target_path: entry["target_path"]
                .as_str()
                .map(|t| host_path(&self.root, t)),
            base_path: host_path(&self.root, base),
            directory: self.root.clone(),
            metadata: entry.clone(),
        })
    }

    /// The module of the unit NAME: the first part of its name (`main`, or a REL's name).
    pub fn module_of(name: &str) -> &str {
        name.split('/').next().unwrap_or(name)
    }

    /// The objects of MODULE's units: their targets, and the module as the project links it (each unit's build
    /// object where it is built, else its target). Each list is sorted, without the objects that are missing.
    pub fn module_objects(&self, module: &str) -> Result<ModuleObjects> {
        let mut objects = ModuleObjects::default();
        for entry in self.units()? {
            if entry["name"].as_str().map(Self::module_of) != Some(module) {
                continue;
            }
            let path = |key: &str| {
                entry[key]
                    .as_str()
                    .map(|p| host_path(&self.root, p))
                    .filter(|p| p.is_file())
            };
            let target = path("target_path");
            if let Some(target) = &target {
                objects.targets.push(target.clone());
            }
            if let Some(linked) = path("base_path").or(target) {
                objects.linked.push(linked);
            }
        }
        for list in [&mut objects.targets, &mut objects.linked] {
            list.sort();
            list.dedup();
        }
        Ok(objects)
    }

    /// report.json (the build writes it: `ninja <report>`), if it is there.
    pub fn report(&self) -> Result<Option<Value>> {
        let Some(path) = self.report.as_ref().filter(|p| p.is_file()) else {
            return Ok(None);
        };
        Ok(Some(
            serde_json::from_slice(&fs::read(path)?)
                .with_context(|| format!("read {}", path.display()))?,
        ))
    }

    /// The unit NAME's entry of report.json (its measures, sections and functions), if it has one.
    pub fn report_unit(&self, name: &str) -> Result<Option<Value>> {
        Ok(self.report()?.and_then(|report| {
            report["units"]
                .as_array()?
                .iter()
                .find(|u| u["name"] == name)
                .cloned()
        }))
    }
}

impl Unit {
    /// The source's host path (absolute).
    pub fn source_path(&self) -> PathBuf {
        host_path(&self.directory, &self.source)
    }

    /// Where dtk writes the target's asm, beside the target object (`build/<version>/obj/<path>.o` has
    /// `build/<version>/asm/<path>.s`).
    fn target_asm_path(&self) -> Option<PathBuf> {
        let target = self
            .target_path
            .as_ref()?
            .to_string_lossy()
            .replace('\\', "/");
        let (directory, object) = target.rsplit_once("/obj/")?;
        Some(PathBuf::from(format!("{directory}/asm/{object}")).with_extension("s"))
    }

    /// The target's asm, as dtk writes it beside the target object (`build/<version>/obj/<path>.o` has
    /// `build/<version>/asm/<path>.s`), if it is there.
    pub fn target_asm(&self) -> Option<PathBuf> {
        self.target_asm_path().filter(|asm| asm.is_file())
    }

    /// The target's asm ([`Unit::target_asm`]), else disassembled from the target object by the project's dtk
    /// (`build/tools/dtk elf disasm`) where dtk writes it, when the project has both. None: neither.
    pub fn target_asm_or_disassembled(&self) -> Option<PathBuf> {
        if let Some(asm) = self.target_asm() {
            return Some(asm);
        }
        let target = self.target_path.as_ref().filter(|t| t.is_file())?;
        let asm = self.target_asm_path()?;
        let dtk = ["build/tools/dtk.exe", "build/tools/dtk"]
            .iter()
            .map(|d| self.directory.join(d))
            .find(|d| d.is_file())?;
        std::fs::create_dir_all(asm.parent()?).ok()?;
        let status = std::process::Command::new(dtk)
            .args(["elf", "disasm"])
            .arg(target)
            .arg(&asm)
            .current_dir(&self.directory)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .ok()?;
        (status.success() && asm.is_file()).then_some(asm)
    }

    /// The precompiled header the unit's flags include (`-prefix X.mch`), if any.
    fn prefix(&self) -> Option<&str> {
        self.flags
            .windows(2)
            .find(|w| w[0] == "-prefix" && w[1].to_ascii_lowercase().ends_with(".mch"))
            .map(|w| w[1].as_str())
    }

    /// The compiler's arguments for a compile to OUTPUT: the flags (with INLINE_MAX_SIZE in place of the last
    /// `inline_max_size(N)`, or added), then `-c SOURCE -o OUTPUT`.
    pub fn arguments(
        &self,
        source: &str,
        output: &str,
        inline_max_size: Option<u32>,
    ) -> Vec<String> {
        let mut args = self.flags.clone();
        if let Some(size) = inline_max_size {
            let pragma = format!("inline_max_size({size})");
            match args
                .iter()
                .rposition(|f| f.trim_start().starts_with("inline_max_size("))
            {
                Some(at) => args[at] = pragma,
                None => args.extend(["-pragma".into(), pragma]),
            }
        }
        args.extend(["-c".into(), source.into(), "-o".into(), output.into()]);
        args
    }
}

/// How [`compile_unit`] compiles.
pub struct CompileOptions<'a> {
    /// The checkout of mwcc whose modules run the project's compiler where they can.
    pub oracle_root: &'a Path,
    /// In place of the flags' `inline_max_size`.
    pub inline_max_size: Option<u32>,
    /// A traced compile, with these streams (in process only).
    pub trace: Option<&'a TraceOptions>,
    pub timeout: Duration,
    /// Polled while it runs: true stops it.
    pub cancel: &'a dyn Fn() -> bool,
    /// Compiles kept, and answered while the files they read are unchanged (in process, untraced compiles only).
    pub cache: Option<&'a CompileCache>,
    /// Compile through persistent compilers (in process, untraced compiles of a unit that makes an object).
    pub persist: Option<Persist<'a>>,
}

/// Persistent compiles of a unit's candidates: each is cut before its first difference from REFERENCE (the unit's
/// source as another candidate has it, the unmodified one say: [`mwcc_wasm::split_point`]), and the text before
/// is a context that POOL keeps parsed.
#[derive(Clone, Copy)]
pub struct Persist<'a> {
    pub pool: &'a PersistPool,
    pub reference: &'a [u8],
    /// Report no persistent failure without a normal compile's confirmation.
    pub confirm_failures: bool,
}

#[derive(Debug, Default)]
pub struct Compiled {
    /// The object, if it compiled.
    pub object: Option<Vec<u8>>,
    /// Its standard output and error, in the order written.
    pub console: Vec<u8>,
    pub exit_code: i32,
    pub timed_out: bool,
    pub cancelled: bool,
    /// The trace (a traced compile).
    pub trace: Vec<u8>,
    /// Whether it ran in process (the project's compiler's module), and how long it took.
    pub in_process: bool,
    pub time: Duration,
    /// The compile cache answered it.
    pub cached: bool,
    /// A persistent compiler gave it ([`CompileOptions::persist`]).
    pub fast: bool,
}

/// UNIT compiled as its edge compiles it, but to a private object, through OVERLAY (files read in place of the
/// disk's): in process where a module runs the project's compiler (mwcc's `module_of_copy`), else the executable as
/// a process (natively on Windows, through wibo elsewhere), where only the unit's source can be overlaid (a scratch
/// copy of it, its directory added after the flags' with `-i`; `__FILE__` names the copy). A unit that uses a precompiled header refuses an overlaid
/// header, which may be in it.
pub fn compile_unit(
    unit: &Unit,
    overlay: &Overlay,
    options: &CompileOptions<'_>,
) -> Result<Compiled> {
    let source = unit.source_path();
    let source_key = file_key(&source);
    let headers: Vec<&Path> = overlay
        .entries()
        .map(|(path, _)| path)
        .filter(|path| file_key(path) != source_key)
        .collect();
    if let Some(header) = headers.first()
        && let Some(precompiled) = unit
            .prefix()
            .or(unit.precompile.then_some(unit.source.as_str()))
    {
        bail!(
            "header {} is overlaid, but {} uses the precompiled header {precompiled}, which may have it",
            header.display(),
            unit.name
        );
    }
    let scratch = tempfile::Builder::new().prefix("mwcc-unit").tempdir()?;
    let output = scratch.path().join("unit.o");
    let output_text = display_path(&output);
    let started = Instant::now();
    let module = compilers::module_of_copy(options.oracle_root, &unit.compiler);
    let mut compiled = if let Some((_, module)) = module {
        let args = unit.arguments(&unit.source, &output_text, options.inline_max_size);
        let mut environment: Vec<(String, String)> = std::env::vars()
            .filter(|(key, _)| !TRACE_ENV.contains(&key.as_str()))
            .collect();
        if let Some(trace) = options.trace {
            environment.extend(trace.environment());
        }
        let program = display_path(&std::path::absolute(&unit.compiler)?);
        // (the cache's key: the module, the arguments with the output's path left out, and everything else the
        // compile is given)
        let cache = options.cache.filter(|_| options.trace.is_none());
        let key = cache.map(|_| {
            let stamp = fs::metadata(&module)
                .map(|m| format!("{} {:?}", m.len(), m.modified().ok()))
                .unwrap_or_default();
            let mut parts: Vec<Vec<u8>> = vec![
                module.to_string_lossy().as_bytes().to_vec(),
                stamp.into_bytes(),
                display_path(&unit.directory).into_bytes(),
                program.clone().into_bytes(),
                vec![u8::from(unit.sjiswrap.is_some())],
            ];
            parts.extend(args.iter().map(|a| {
                if *a == output_text {
                    b"<output>".to_vec()
                } else {
                    a.as_bytes().to_vec()
                }
            }));
            let mut environment: Vec<_> = environment.iter().collect();
            environment.sort();
            parts.extend(
                environment
                    .iter()
                    .map(|(k, v)| format!("{k}={v}").into_bytes()),
            );
            let mut files: Vec<(String, Option<Vec<u8>>)> = overlay
                .entries()
                .map(|(path, bytes)| {
                    (
                        path.to_string_lossy().to_lowercase(),
                        bytes.map(|b| Sha1::digest(b).to_vec()),
                    )
                })
                .collect();
            files.sort();
            for (path, hash) in files {
                parts.push(path.into_bytes());
                parts.push(hash.unwrap_or_else(|| b"<absent>".to_vec()));
            }
            cache::key(parts.iter().map(Vec::as_slice))
        });
        if let (Some(cache), Some(key)) = (cache, &key)
            && let Some(hit) = cache.lookup(key)
        {
            return Ok(Compiled {
                object: hit.object.map(|o| o.to_vec()),
                console: hit.console.to_vec(),
                exit_code: hit.exit_code,
                in_process: true,
                cached: true,
                time: started.elapsed(),
                ..Compiled::default()
            });
        }
        let loaded = mwcc_wasm::load(&module).map_err(|e| anyhow::anyhow!("{e:?}"))?;
        let transcode_sjis = unit.sjiswrap.is_some();
        let traced = std::cell::RefCell::new(Vec::new());
        let normal = || -> Result<pool::Compiled> {
            let outcome = loaded
                .run(
                    &mwcc_wasm::Request {
                        args: &args,
                        directory: &unit.directory,
                        environment: &environment,
                        program: &program,
                        version_info: &[],
                        timeout: Some(options.timeout),
                        cancel: None,
                        trace: options.trace.is_some(),
                        overlay: (!overlay.is_empty()).then_some(overlay),
                        transcode_sjis,
                        record_reads: cache.is_some(),
                    },
                    options.cancel,
                )
                .map_err(|e| anyhow::anyhow!("{e:?}"))?;
            let object = fs::read(&output).ok();
            let _ = fs::remove_file(&output);
            *traced.borrow_mut() = outcome.trace;
            if let (Some(cache), Some(key)) = (cache, &key)
                && !outcome.timed_out
                && !outcome.cancelled
            {
                cache.store(
                    *key,
                    cache::Hit {
                        object: object.as_deref().map(Arc::from),
                        console: Arc::from(outcome.console.as_slice()),
                        exit_code: outcome.exit_code,
                    },
                    &outcome.consulted,
                    scratch.path(),
                );
            }
            Ok(pool::Compiled {
                exit_code: outcome.exit_code,
                object,
                console: outcome.console,
                timed_out: outcome.timed_out,
                cancelled: outcome.cancelled,
                fast: false,
            })
        };
        // (a persistent compile: the source's text up to its first difference from the reference, and the rest)
        let persistent = options
            .persist
            .filter(|_| options.trace.is_none() && !unit.precompile)
            .and_then(|persist| {
                let text = match overlay
                    .entries()
                    .find(|(path, _)| file_key(path) == source_key)
                {
                    Some((_, bytes)) => bytes?.to_vec(),
                    None => fs::read(&source).ok()?,
                };
                let split = mwcc_wasm::split_point(persist.reference, &text);
                if pool::persist_log() {
                    eprintln!(
                        "mwcc-project: {} cut at byte {split} of {}",
                        unit.name,
                        text.len()
                    );
                }
                let context = String::from_utf8(text[..split].to_vec()).ok()?;
                (split > 0).then(|| {
                    let context = Arc::new(pool::Context {
                        module: loaded.clone(),
                        args: args.clone(),
                        output: output_text.clone(),
                        directory: unit.directory.clone(),
                        environment: environment.clone(),
                        program: program.clone(),
                        source: source.clone(),
                        text: context,
                        overlay: overlay.clone(),
                        transcode_sjis,
                    });
                    (persist, context, text[split..].to_vec())
                })
            });
        let compiled = match persistent {
            Some((persist, context, candidate)) => persist.pool.compile(
                &context,
                &candidate,
                options.timeout,
                persist.confirm_failures,
                &normal,
            )?,
            None => normal()?,
        };
        // (a traced compile is never persistent)
        let trace = traced.take();
        Compiled {
            object: compiled.object,
            console: compiled.console,
            exit_code: compiled.exit_code,
            timed_out: compiled.timed_out,
            cancelled: compiled.cancelled,
            trace,
            in_process: true,
            fast: compiled.fast,
            ..Compiled::default()
        }
    } else {
        ensure!(
            options.trace.is_none(),
            "a traced compile of {} needs a module of its compiler {}",
            unit.name,
            unit.compiler.display()
        );
        if let Some(header) = headers.first() {
            bail!(
                "header {} is overlaid, but {}'s compiler {} runs as a process: only its source can be",
                header.display(),
                unit.name,
                unit.compiler.display()
            );
        }
        // (the overlaid source as a scratch copy, its own directory added to the search paths)
        let mut args = vec![];
        let compiled_source = match overlay.entries().next() {
            Some((_, bytes)) => {
                let bytes = bytes.with_context(|| {
                    format!("the source of {} is overlaid as absent", unit.name)
                })?;
                let copy = scratch
                    .path()
                    .join(source.file_name().context("source has no name")?);
                fs::write(&copy, bytes)?;
                args.extend([
                    "-i".into(),
                    display_path(source.parent().context("source has no directory")?),
                ]);
                display_path(&copy)
            }
            None => unit.source.clone(),
        };
        let mut arguments = unit.arguments(&compiled_source, &output_text, options.inline_max_size);
        // (`-i` of the source's directory with the flags', before `-c`)
        let at = arguments.len() - 4;
        arguments.splice(at..at, args);
        run_process(unit, options, &arguments, scratch.path())?
    };
    compiled.time = started.elapsed();
    if !compiled.in_process {
        compiled.object = fs::read(&output).ok();
    }
    Ok(compiled)
}

/// The unit's compiler run as a process (through sjiswrap where its edge runs it so), its console merged.
fn run_process(
    unit: &Unit,
    options: &CompileOptions<'_>,
    args: &[String],
    scratch: &Path,
) -> Result<Compiled> {
    let program = unit.sjiswrap.as_ref().unwrap_or(&unit.compiler);
    let mut command = compilers::project_launcher(&unit.directory, options.oracle_root, program)?;
    if unit.sjiswrap.is_some() {
        command.arg(&unit.compiler);
    }
    let console_path = scratch.join("console.txt");
    let console = fs::File::create(&console_path)?;
    command
        .args(args)
        .current_dir(&unit.directory)
        .stdin(Stdio::null())
        .stdout(console.try_clone()?)
        .stderr(console);
    // (its own process group, so that a stopped compile takes sjiswrap's child with it)
    process_tree::prepare(&mut command);
    let mut child = command
        .spawn()
        .with_context(|| format!("run {}", program.display()))?;
    let process_tree::Waited {
        status,
        timed_out,
        cancelled,
    } = process_tree::wait(&mut child, options.timeout, options.cancel)?;
    let stopped = timed_out || cancelled;
    Ok(Compiled {
        console: fs::read(&console_path)?,
        exit_code: status.code().filter(|_| !stopped).unwrap_or(-1),
        timed_out,
        cancelled,
        ..Compiled::default()
    })
}
