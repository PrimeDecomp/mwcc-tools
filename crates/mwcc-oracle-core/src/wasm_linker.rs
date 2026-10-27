//! The linkers of GC 1.3.2 and 2.7 (`mwldeppc.exe`) built from this repository's own sources for WebAssembly, to
//! run in process as the compiler module does (crate::wasm_build): `build/<linker>/wasm/mwldeppc.wasm`.
//!
//! A linker is the driver and C library of the 2.7 compiler with the linker plugin (`src/linker`) in place of the
//! compiler's (docs/linker in decomp/mwcc). Its units are `config/LD_2_6/splits.txt`'s (config.json `units_from`),
//! with the sources config.json's `replace` gives in place of some, compiled as the 2.7 compiler's are (its compilers'
//! flags, `-DVERSION` of GC_2_7) and with `-DLINKER=13` or `-DLINKER=27`. As in the compiler module, `wasm/libc`
//! replaces MSL and its runtime (`src/msl`, `src/runtime`), and `wasm/src` the startup.
//!
//! The sources say `#pragma options align=mac68k` (and `packed`, `native`, `reset`), which clang reads only for
//! Darwin targets. They are compiled from a mirror of `src/` and `include/` (`build/wasm-ld/tree`) with each of those
//! written as the `#pragma pack` that lays the structures out the same way ([`mirror`]); the sources themselves are
//! the ones the matching build compiles.
use crate::{
    build_config::{self, ninja_path as escape},
    executable,
    source_tree::read_json,
    wasm_build,
};
use anyhow::{Context, Result, bail, ensure};
use regex::Regex;
use std::{
    fs,
    path::{Path, PathBuf},
    sync::LazyLock,
};

/// The linkers built for WebAssembly, each with the compiler version it goes with.
pub const LINKERS: [(&str, &str); 2] = [("LD_1_3_2", "GC_1_3_2"), ("LD_2_7", "GC_2_7")];

/// The compilers built for WebAssembly from this repository's own sources (gc2 has none of them): the stock
/// compiler only, without the oracle's hooks (`build/<version>/wasm/mwcc.wasm`). Each is built as a linker is, from
/// its own split (`config/<version>/splits.txt`) and flags, with `-DVERSION` of its version; its COMMON block is
/// written from the split's `common` unit and `symbols.txt`, and its version information is the original's.
pub const COMPILERS: [&str; 1] = mwcc_project::compilers::ROOT_VERSIONS;

/// Whether PROGRAM (a linker's or a version's name) is one of [`COMPILERS`].
pub fn is_compiler(program: &str) -> bool {
    COMPILERS.contains(&program)
}

/// The compiler version whose driver and flags the linkers' shared sources are built with.
const COMPILER: &str = "GC_2_7";

/// The linker that goes with compiler VERSION, if one is built for WebAssembly.
pub fn for_compiler(version: &str) -> Option<&'static str> {
    LINKERS
        .iter()
        .find(|(_, compiler)| *compiler == version)
        .map(|(linker, _)| *linker)
}

/// The compiler version LINKER goes with.
pub fn compiler_of(linker: &str) -> Option<&'static str> {
    LINKERS
        .iter()
        .find(|(l, _)| *l == linker)
        .map(|(_, compiler)| *compiler)
}

/// The mirror of the sources the linkers compile.
pub const MIRROR: &str = "build/wasm-ld/tree";
const MIRROR_STAMP: &str = "build/wasm-ld/tree.stamp";

pub fn directory(linker: &str) -> String {
    format!("build/{linker}/wasm")
}

fn stem(program: &str) -> &'static str {
    if is_compiler(program) {
        "mwcc"
    } else {
        "mwldeppc"
    }
}

/// The program's module.
pub fn output(program: &str) -> String {
    format!("{}/{}.wasm", directory(program), stem(program))
}

/// The module's version information (mwcc-wasm's `version_info_path`).
pub fn version_output(program: &str) -> String {
    format!("{}/{}.version", directory(program), stem(program))
}

/// Whether ROOT has what PROGRAM's module is built from.
pub fn available(root: &Path, program: &str) -> bool {
    let own = if is_compiler(program) {
        root.join(format!("config/{program}/splits.txt")).is_file()
            && root.join(format!("config/{program}/symbols.txt")).is_file()
    } else {
        compiler_of(program).is_some() && root.join("src/linker").is_dir()
    };
    own && root.join(format!("config/{program}/config.json")).is_file()
        && wasm_build::available(root, COMPILER)
}

/// The sources the module does without: MSL and the runtime (wasm/libc and wasm/src replace them), and the license
/// imports (wasm/src/platform.c).
fn skipped(unit: &str) -> bool {
    unit.starts_with("src/msl/")
        || unit.starts_with("src/runtime/")
        || unit == "src/driver/LicenseImports.c"
}

/// Warnings clang makes errors that MWCC's C accepted (the code they flag is the original's).
const RELAXED: &[&str] = &[
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=return-mismatch",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
];

/// PROGRAM's C and C++ units (sources of the mirror), in the split's order.
pub fn units(root: &Path, program: &str) -> Result<Vec<wasm_build::Unit>> {
    let config = read_json(&root.join(format!("config/{program}/config.json")))?;
    let from = config["units_from"].as_str().unwrap_or(program);
    let splits = executable::read_splits(
        &fs::read_to_string(root.join(format!("config/{from}/splits.txt")))
            .with_context(|| format!("read config/{from}/splits.txt"))?,
    )?;
    let replace = config["replace"].as_object();
    let sources = read_json(&root.join("config/sources.json"))?;
    let flags_of = if is_compiler(program) {
        program
    } else {
        COMPILER
    };
    let compiler = read_json(&root.join(format!("config/{flags_of}/config.json")))?;
    let define = build_config::VERSIONS
        .iter()
        .position(|v| *v == flags_of)
        .with_context(|| format!("{flags_of} is not a build version"))?;
    // (a linker's `-DLINKER`: 13 or 27)
    let linker_define = (!is_compiler(program)).then(|| {
        let number = program[3..].replace('_', "");
        number[..2].to_owned()
    });
    let prefix = format!("{MIRROR}/");
    let mut out = vec![];
    for unit in &splits.units {
        let unit = replace
            .and_then(|r| r.get(&unit.name))
            .and_then(|s| s.as_str())
            .unwrap_or(&unit.name);
        let Some(settings) = sources.get(unit) else {
            // (an object with no C source: the assembly, the import stubs)
            continue;
        };
        if skipped(unit) {
            continue;
        }
        let mut arguments = build_config::compiler_arguments(
            &compiler,
            settings,
            define,
            Some(unit),
            false,
            &prefix,
        )?;
        if let Some(number) = &linker_define {
            arguments.push(format!("-DLINKER={number}"));
        }
        let mut arguments = wasm_build::clang_arguments(unit, &prefix, &arguments);
        arguments.extend(RELAXED.iter().map(|s| s.to_string()));
        out.push(wasm_build::Unit {
            name: unit.to_owned(),
            input: format!("{prefix}{unit}"),
            arguments,
        });
    }
    ensure!(!out.is_empty(), "{program} has no units");
    Ok(out)
}

/// The module's platform sources (wasm/libc, wasm/src), compiled as the compiler module's are.
fn platform(root: &Path) -> Result<Vec<wasm_build::Unit>> {
    Ok(wasm_build::units(root, COMPILER)?
        .into_iter()
        .filter(|u| u.name.starts_with("wasm/"))
        .collect())
}

static PRAGMA: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^(\s*)#\s*pragma\s+options\s+align\s*=\s*(\w+)\s*(/\*.*\*/|//.*)?\s*$").unwrap()
});
static ANY_PRAGMA: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^\s*#\s*pragma\s+options\s+align").unwrap());

/// TEXT (a C source or header) with MWCC's alignment pragmas as clang's: `mac68k` (2-byte alignment at most) as
/// `pack(push, 2)`, `packed` as `pack(push, 1)`, `native` and `power` (natural alignment) as `pack(push, 8)`, and
/// `reset` (back to the last setting) as `pack(pop)`. Any other is an error.
pub fn rewrite_pragmas(text: &str) -> Result<String> {
    let mut out = String::with_capacity(text.len());
    for (number, line) in text.split('\n').enumerate() {
        if number > 0 {
            out.push('\n');
        }
        let (body, cr) = match line.strip_suffix('\r') {
            Some(body) => (body, "\r"),
            None => (line, ""),
        };
        if let Some(m) = PRAGMA.captures(body) {
            let pack = match &m[2] {
                "mac68k" => "#pragma pack(push, 2)",
                "packed" => "#pragma pack(push, 1)",
                "native" | "power" => "#pragma pack(push, 8)",
                "reset" => "#pragma pack(pop)",
                other => bail!("line {}: unsupported alignment {other}", number + 1),
            };
            out.push_str(&m[1]);
            out.push_str(pack);
            out.push_str(cr);
        } else {
            ensure!(
                !ANY_PRAGMA.is_match(body),
                "line {}: unsupported {}",
                number + 1,
                body.trim()
            );
            out.push_str(line);
        }
    }
    Ok(out)
}

fn is_source(path: &Path) -> bool {
    path.extension()
        .and_then(|e| e.to_str())
        .is_some_and(|e| ["c", "cpp", "cp", "h", "hpp"].contains(&e))
}

/// The files the mirror is made of: ROOT's `src/` and `include/`, as paths relative to ROOT.
fn mirrored_files(root: &Path) -> Result<Vec<PathBuf>> {
    let mut out = vec![];
    let mut stack: Vec<PathBuf> = ["src", "include"].map(PathBuf::from).to_vec();
    while let Some(directory) = stack.pop() {
        for entry in fs::read_dir(root.join(&directory))
            .with_context(|| format!("list {}", directory.display()))?
        {
            let entry = entry?;
            let path = directory.join(entry.file_name());
            if entry.file_type()?.is_dir() {
                stack.push(path);
            } else if is_source(&path) {
                out.push(path);
            }
        }
    }
    out.sort();
    Ok(out)
}

/// Writes ROOT's sources and headers to DESTINATION (under ROOT) with their pragmas rewritten
/// ([`rewrite_pragmas`]); a file whose mirror is already the same is left alone (its time kept, so that only what
/// changed is compiled again). Then touches STAMP.
pub fn mirror(root: &Path, destination: &str, stamp: Option<&str>) -> Result<()> {
    for path in mirrored_files(root)? {
        let source = fs::read(root.join(&path))?;
        // (the sources are Windows-1252: each byte as a char, and back)
        let text: String = source.iter().map(|&b| b as char).collect();
        let rewritten = rewrite_pragmas(&text).with_context(|| format!("{}", path.display()))?;
        let bytes: Vec<u8> = rewritten.chars().map(|c| c as u8).collect();
        let target = root.join(destination).join(&path);
        if fs::read(&target).is_ok_and(|old| old == bytes) {
            continue;
        }
        fs::create_dir_all(target.parent().unwrap())?;
        fs::write(&target, bytes)?;
    }
    if let Some(stamp) = stamp {
        fs::write(root.join(stamp), b"")?;
    }
    Ok(())
}

/// LINKER's version information: its resource script's (config.json `resources`) VS_VERSIONINFO, which the driver's
/// banner reads.
pub fn version_info(root: &Path, linker: &str) -> Result<Vec<u8>> {
    let config = read_json(&root.join(format!("config/{linker}/config.json")))?;
    if is_compiler(linker) {
        return original_version_info(root, &config);
    }
    let script = config["resources"]
        .as_str()
        .with_context(|| format!("config/{linker}/config.json names no resources"))?;
    let bytes = fs::read(root.join(script)).with_context(|| format!("read {script}"))?;
    let text: String = bytes.iter().map(|&b| b as char).collect();
    // (its block comments, which compile_resources does not read)
    let text = Regex::new(r"(?s)/\*.*?\*/")?.replace_all(&text, "");
    let resources = crate::source_only::compile_resources(&text, "", 0, &[])?;
    Ok(resources
        .into_iter()
        .find(|r| r.kind == crate::pe::ResourceName::Number(16))
        .context("the linker's resource script has no version information")?
        .data)
}

/// The original executable (CONFIG's `original`, checked against its `sha1`): its VS_VERSIONINFO resource.
fn original_version_info(root: &Path, config: &serde_json::Value) -> Result<Vec<u8>> {
    let original = config["original"]
        .as_str()
        .context("the config names no original executable")?;
    let data = fs::read(root.join(original)).with_context(|| format!("read {original}"))?;
    let digest = {
        use sha1::{Digest, Sha1};
        format!("{:x}", Sha1::digest(&data))
    };
    ensure!(
        config["sha1"].as_str() == Some(digest.as_str()),
        "{original} is not the original (SHA-1 {digest})"
    );
    let pe = crate::pe::Pe::parse(&data)?;
    // (none: GC 1.2.5n has no resources, and its version information is empty, which the host fails as Windows
    // does)
    let Some(section) = pe.sections.iter().find(|s| s.name == ".rsrc") else {
        return Ok(vec![]);
    };
    Ok(crate::pe::resources(&pe, section)?
        .into_iter()
        .find(|r| r.kind == crate::pe::ResourceName::Number(16))
        .with_context(|| format!("{original} has no version information"))?
        .data)
}

/// Writes LINKER's version information to OUTPUT (under ROOT).
pub fn write_version_info(root: &Path, linker: &str, output: &str) -> Result<()> {
    let data = version_info(root, linker)?;
    let output = root.join(output);
    fs::create_dir_all(output.parent().unwrap())?;
    fs::write(output, data)?;
    Ok(())
}

/// The COMMON block of LINKER (config.json `assembly.common`: x86 assembly that lays out the original's uninitialized
/// data, each name at its offset) as WebAssembly assembly: the same names at the same offsets and sizes.
///
/// The original's uninitialized globals are COMMON symbols, merged into this block, and its code relies on the layout:
/// `initialize_cmdline_environment` clears 0x198 bytes from `pluginPrivateContext` on, and some names are inside
/// others (`clState`'s room is 4 bytes, the structure 1340). So each name is a strong definition here and the block
/// is linked first: with `--allow-multiple-definition`, the first definition is the one kept, and a unit's C
/// definition of the same name is left unused.
pub fn common_assembly(text: &str) -> Result<String> {
    let mut out = String::from(
        "# Written by mwcc-tools wasm-common from the linker's COMMON block.\n\t.section .bss.linker_common,\"\",@\n",
    );
    let mut pending: Option<String> = None;
    for (number, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') || line == ".bss" || line.starts_with(".globl")
        {
            continue;
        }
        if let Some(align) = line.strip_prefix(".p2align") {
            out.push_str(&format!("\t.p2align {}\n", align.trim()));
        } else if let Some(label) = line.strip_suffix(':') {
            ensure!(
                pending.is_none(),
                "line {}: {label} follows a label without a size",
                number + 1
            );
            let name = label.strip_prefix('_').unwrap_or(label);
            pending = Some(name.to_string());
        } else if let Some(size) = line.strip_prefix(".zero") {
            let size: u32 = size
                .trim()
                .parse()
                .with_context(|| format!("line {}: {line}", number + 1))?;
            let Some(name) = pending.take() else {
                bail!("line {}: .zero without a label", number + 1);
            };
            out.push_str(&format!(
                "\t.globl {name}\n\t.type {name},@object\n\t.size {name}, {size}\n{name}:\n\t.skip {size}\n"
            ));
        } else {
            bail!("line {}: unsupported directive {line}", number + 1);
        }
    }
    ensure!(
        pending.is_none(),
        "the COMMON block ends with a label without a size"
    );
    Ok(out)
}

/// The COMMON block of an executable as decomp/mwcc's `tools/common_source.py` writes it (the x86 assembly
/// [`common_assembly`] reads), from its split (SPLITS: the `common` unit's `.bss` ranges) and SYMBOLS (the C names in
/// them): each name at its offset, as large as the room up to the next. Room before the first name has a name of its
/// own, `_mwcc_common_start`.
pub fn common_from_split(splits: &str, symbols: &str) -> Result<String> {
    let splits = executable::read_splits(splits)?;
    let ranges: Vec<(u32, u32)> = splits
        .units
        .iter()
        .filter(|u| u.name == "common")
        .flat_map(|u| u.ranges.iter())
        .filter(|r| r.section == ".bss")
        .map(|r| (r.start, r.end))
        .collect();
    ensure!(!ranges.is_empty(), "the split has no common unit");
    let start = ranges.iter().map(|r| r.0).min().unwrap();
    let end = ranges.iter().map(|r| r.1).max().unwrap();
    let mut names: Vec<(u32, String)> = executable::read_symbols(symbols)?
        .into_iter()
        .filter(|s| {
            s.section == ".bss" && s.name.starts_with('_') && (start..end).contains(&s.address)
        })
        .map(|s| (s.address, s.name))
        .collect();
    names.sort();
    if names.first().is_none_or(|(address, _)| *address > start) {
        names.insert(0, (start, "_mwcc_common_start".into()));
    }
    let mut out = String::from("\t.bss\n\t.p2align 4\n");
    for (i, (address, name)) in names.iter().enumerate() {
        let following = names.get(i + 1).map_or(end, |n| n.0);
        out.push_str(&format!(
            "\t.globl {name}\n{name}:\n\t.zero {}\n",
            following - address
        ));
    }
    Ok(out)
}

/// PROGRAM's COMMON block, as x86 assembly ([`common_assembly`] reads it): a linker's source file, a compiler's
/// written from its split ([`common_from_split`]).
fn common_text(root: &Path, program: &str) -> Result<String> {
    if is_compiler(program) {
        let read = |name: &str| {
            fs::read_to_string(root.join(format!("config/{program}/{name}")))
                .with_context(|| format!("read config/{program}/{name}"))
        };
        return common_from_split(&read("splits.txt")?, &read("symbols.txt")?);
    }
    let source = common_source(root, program)?;
    fs::read_to_string(root.join(&source)).with_context(|| format!("read {source}"))
}

/// Writes PROGRAM's COMMON block as WebAssembly assembly ([`common_assembly`]) to OUTPUT.
pub fn write_common(root: &Path, program: &str, output: &str) -> Result<()> {
    let text = common_text(root, program)?;
    let output = root.join(output);
    fs::create_dir_all(output.parent().unwrap())?;
    fs::write(
        output,
        common_assembly(&text).with_context(|| format!("{program}'s COMMON block"))?,
    )?;
    Ok(())
}

/// The files PROGRAM's COMMON block is written from.
fn common_inputs(root: &Path, program: &str) -> Result<Vec<String>> {
    if is_compiler(program) {
        return Ok(vec![
            format!("config/{program}/splits.txt"),
            format!("config/{program}/symbols.txt"),
        ]);
    }
    Ok(vec![common_source(root, program)?])
}

fn common_source(root: &Path, linker: &str) -> Result<String> {
    let config = read_json(&root.join(format!("config/{linker}/config.json")))?;
    config["assembly"]["common"]
        .as_str()
        .map(String::from)
        .with_context(|| format!("config/{linker}/config.json has no assembly.common"))
}

/// The Win32 functions the linker module imports from its host (crates/mwcc-wasm); any other symbol it leaves
/// undefined fails its link.
pub const IMPORTS: &str = "wasm/linker-imports.txt";

/// The edges of the linkers' modules for compiler VERSION's build: the mirror (once), and the linker's objects, its
/// module and version information. The rules are wasm_build's.
pub fn edges(root: &Path, version: &str) -> Result<(Vec<String>, Vec<String>)> {
    let linker = if is_compiler(version) {
        version
    } else if let Some(linker) = for_compiler(version) {
        linker
    } else {
        return Ok((vec![], vec![]));
    };
    if !available(root, linker) {
        return Ok((vec![], vec![]));
    }
    let files: Vec<String> = mirrored_files(root)?
        .iter()
        .map(|p| p.to_string_lossy().replace('\\', "/"))
        .collect();
    let inputs: Vec<String> = files.iter().map(|p| escape(p)).collect();
    // (every mirrored file is an output, so that ninja knows the units' sources and headers come from it; the mirror
    // leaves unchanged files alone, and restat compiles again only what changed)
    let mirrored: Vec<String> = files
        .iter()
        .map(|p| escape(&format!("{MIRROR}/{p}")))
        .collect();
    let mut out = vec![
        "rule wasm_mirror".into(),
        format!("  command = $tools wasm-mirror {MIRROR} {MIRROR_STAMP}"),
        "  description = MIRROR the linkers' sources".into(),
        "  restat = 1".into(),
        format!(
            "build {MIRROR_STAMP} {}: wasm_mirror | $tools_dep {}",
            mirrored.join(" "),
            inputs.join(" ")
        ),
        "rule wasm_linker_version".into(),
        "  command = $tools wasm-linker-version $linker $out".into(),
        "  description = VERSION $out".into(),
    ];
    let directory = format!("{}/obj", directory(linker));
    let mut objects = vec![];
    for unit in units(root, linker)? {
        let object = format!("{directory}/{}.o", unit.name);
        out.push(format!(
            "build {}: wasm_cc {} | {MIRROR_STAMP}",
            escape(&object),
            escape(&unit.input)
        ));
        out.push(format!(
            "  cflags = {}",
            unit.arguments
                .iter()
                .map(|a| wasm_build::quote(a))
                .collect::<Vec<_>>()
                .join(" ")
        ));
        objects.push(object);
    }
    for unit in platform(root)? {
        let object = format!("{directory}/{}.o", unit.name);
        out.push(format!(
            "build {}: wasm_cc {}",
            escape(&object),
            escape(&unit.input)
        ));
        out.push(format!(
            "  cflags = {}",
            unit.arguments
                .iter()
                .map(|a| wasm_build::quote(a))
                .collect::<Vec<_>>()
                .join(" ")
        ));
        objects.push(object);
    }
    let common_inputs: Vec<String> = common_inputs(root, linker)?
        .iter()
        .map(|p| escape(p))
        .collect();
    let common = format!("{directory}/common.s");
    out.extend([
        "rule wasm_common".into(),
        "  command = $tools wasm-common $linker $out".into(),
        "  description = COMMON $out".into(),
        format!(
            "build {}: wasm_common {} | $tools_dep",
            escape(&common),
            common_inputs.join(" ")
        ),
        format!("  linker = {linker}"),
        format!("build {directory}/common.o: wasm_cc {}", escape(&common)),
        "  cflags = --target=wasm32-unknown-unknown".into(),
    ]);
    // (first: the first definition of a name is the one kept, [`common_assembly`])
    objects.insert(0, format!("{directory}/common.o"));
    let tag = format!("{directory}/{}.o", wasm_build::TAG);
    out.extend([
        format!("build {}: wasm_cc {}", escape(&tag), wasm_build::TAG),
        "  cflags = --target=wasm32-unknown-unknown -mexception-handling".into(),
    ]);
    objects.push(tag);
    out.extend([
        "rule wasm_linker_ld".into(),
        format!(
            "  command = $wasm_ld {} --allow-undefined-file={IMPORTS} @$out.rsp -o $out",
            wasm_build::LINK_FLAGS
                .iter()
                .filter(|f| **f != "--allow-undefined")
                .copied()
                .collect::<Vec<_>>()
                .join(" ")
        ),
        "  rspfile = $out.rsp".into(),
        "  rspfile_content = $in".into(),
        "  description = WASM-LD $out".into(),
    ]);
    out.push(format!(
        "build {}: wasm_linker_ld {} | {IMPORTS}",
        escape(&wasm_build::linked(&output(linker))),
        objects
            .iter()
            .map(|o| escape(o))
            .collect::<Vec<_>>()
            .join(" ")
    ));
    let config = read_json(&root.join(format!("config/{linker}/config.json")))?;
    // (a compiler's version information is its original's)
    let script = if is_compiler(linker) {
        config["original"].as_str().unwrap_or_default()
    } else {
        config["resources"].as_str().unwrap_or_default()
    };
    out.extend([
        format!(
            "build {}: wasm_linker_version {} | $tools_dep",
            escape(&version_output(linker)),
            escape(script)
        ),
        format!("  linker = {linker}"),
        String::new(),
    ]);
    Ok((out, vec![output(linker), version_output(linker)]))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_common_block_keeps_its_offsets() {
        let text =
            "# x\n\t.bss\n\t.p2align 4\n\t.globl _a\n_a:\n\t.zero 8\n\t.globl _b\n_b:\n\t.zero 4\n";
        let out = common_assembly(text).unwrap();
        assert!(out.contains(
            "\t.p2align 4\n\t.globl a\n\t.type a,@object\n\t.size a, 8\na:\n\t.skip 8\n\t.globl b\n"
        ));
        assert!(common_assembly("_a:\n\t.long 1\n").is_err());
    }

    #[test]
    fn a_compilers_common_block_comes_from_its_split() {
        let splits = "Sections:
	.bss        type:bss align:8

common:
	.bss        start:0x00001000 end:0x00001010 common:1
";
        let symbols = concat!(
            "_a = .bss:0x00001004; // type:object size:0x4
",
            "_b = .bss:0x00001008; // type:object size:0x4
",
            "local = .bss:0x0000100C; // type:object size:0x4
",
            "_c = .bss:0x00002000; // type:object size:0x4
",
        );
        let out = common_from_split(splits, symbols).unwrap();
        assert_eq!(
            out,
            concat!(
                "	.bss
	.p2align 4
	.globl _mwcc_common_start
_mwcc_common_start:
	.zero 4
",
                "	.globl _a
_a:
	.zero 4
	.globl _b
_b:
	.zero 8
",
            )
        );
        // (what common_assembly reads)
        assert!(common_assembly(&out).unwrap().contains(
            "a:
	.skip 4
"
        ));
        assert!(
            common_from_split(
                "Sections:
	.bss        type:bss align:8
",
                symbols
            )
            .is_err()
        );
    }

    #[test]
    fn the_alignment_pragmas_become_packs() {
        let text = "a\r\n#pragma options align=mac68k\r\nstruct s;\n  #pragma options align = reset /* x */\n\
                    #pragma options align=packed\n#pragma options align=native\n";
        let out = rewrite_pragmas(text).unwrap();
        assert_eq!(
            out,
            "a\r\n#pragma pack(push, 2)\r\nstruct s;\n  #pragma pack(pop)\n#pragma pack(push, 1)\n\
             #pragma pack(push, 8)\n"
        );
        assert!(rewrite_pragmas("#pragma options align=other\n").is_err());
        assert!(rewrite_pragmas("#pragma options align=mac68k junk\n").is_err());
        // (in a string, it is no pragma)
        assert!(rewrite_pragmas("puts(\"#pragma options align=packed\\n\");\n").is_ok());
    }

    #[test]
    fn each_linker_has_its_compiler() {
        assert_eq!(for_compiler("GC_2_7"), Some("LD_2_7"));
        assert_eq!(for_compiler("GC_1_3_2"), Some("LD_1_3_2"));
        assert_eq!(for_compiler("GC_2_6"), None);
        assert_eq!(compiler_of("LD_1_3_2"), Some("GC_1_3_2"));
    }
}
