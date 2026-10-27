//! `mwcc-tools run [sjiswrap.exe] EXE ARGS...`: a wrapper for a project's build, in the place of wibo (its
//! configure's `--wrapper`). A compiler whose module is built runs in this process, as the executable's process
//! would: the same arguments, directory and environment, its console output on stdout and stderr, its exit code, and
//! the files it writes (the object, `-MMD`'s `.d`). The compiler is known by the size and SHA-1 of its version's
//! original (`config/<version>/config.json`), wherever it is. Anything else (the linker, a compiler without a module)
//! runs as it is: natively on Windows, elsewhere through wibo: `MWCC_WIBO`, else the project's own
//! (`build/tools/wibo` in the directory the build runs in), else this checkout's, else the one on the path.
//!
//! A copy or link of `mwcc-tools` named `mwcc-run` is `mwcc-tools run`: one path, as a configure's `--wrapper` takes
//! (dtk-template writes its arguments back into build.ninja unquoted).
//!
//! `sjiswrap.exe` converts the UTF-8 sources and headers its compiler reads to Shift JIS. A compiler that runs in
//! process after it gets them converted as sjiswrap converts them (mwcc-wasm's `Request::transcode_sjis`).
//!
//! `mwcc-tools run ninja ...` builds a project's build.ninja in place of ninja ([`crate::ninja_cli`]).
use anyhow::{Context, Result, bail};
use mwcc_oracle_core::runner;
use std::{
    env,
    io::Write,
    path::{Path, PathBuf},
    time::Instant,
};

const USAGE: &str = "mwcc-tools run [sjiswrap.exe] EXE ARGS...\nmwcc-run [sjiswrap.exe] EXE ARGS...\n\
mwcc-tools run ninja [-C DIR] [-f FILE] [-j N] [-k N] [-n] [-v] [-d explain] [--json FILE] [TARGETS...]\n\
mwcc-tools run ninja [-C DIR] [-f FILE] -t commands [-s] [TARGETS...]\n\
mwcc-tools run ninja [-C DIR] [-f FILE] -t inputs [-E] [-0] [-d] [TARGETS...]\n\
mwcc-tools run ninja [-C DIR] [-f FILE] -t targets all\n\n\
Runs EXE with ARGS. A compiler or linker whose WebAssembly module is built (one with the size and SHA-1 in \
MWCC_ORACLE_ROOT's config/<version>/config.json), or released (its module is then downloaded to the user's cache, \
or MWCC_WASM_CACHE; not with MWCC_WASM_DOWNLOAD=0), runs in process; anything else runs as a process (off Windows \
through MWCC_WIBO, else build/tools/wibo here, else the checkout's, else wibo on the path). MWCC_IN_PROCESS=0 runs \
every executable, MWCC_IN_PROCESS_LINKER=0 the linkers. MWCC_RUN_TIMING=1 prints where the time went.\n\n\
`run ninja` builds the project's build.ninja in place of ninja, with ninja's options; configure the project \
without --wrapper. A command whose program is such a compiler or linker (also behind mwcc-run, wibo or sjiswrap, \
or first in `cmd /c A && B`) runs in process, up to -j at once; any other runs as a process. build.ninja is \
regenerated (configure.py) and loaded again while it is out of date. The build's record is .n2_db, not ninja's \
.ninja_log: the first build after one of ninja's runs everything again.";

/// The checkout the modules are in: `MWCC_ORACLE_ROOT`, else the one this executable was built in.
pub fn root() -> Result<PathBuf> {
    if let Some(root) = env::var_os("MWCC_ORACLE_ROOT") {
        return Ok(root.into());
    }
    let exe = env::current_exe()?;
    exe.ancestors()
        .find(|dir| dir.join("gc2").is_dir() && dir.join("Cargo.toml").is_file())
        .map(Path::to_path_buf)
        .context("set MWCC_ORACLE_ROOT to the mwcc checkout")
}

/// The root a run's process looks in (for wibo): [`root`], else the download cache.
pub fn run_root() -> Result<PathBuf> {
    match root() {
        Ok(root) => Ok(root),
        #[cfg(feature = "wasm")]
        Err(_) => Ok(crate::wasm_download::cache()?.to_path_buf()),
        #[cfg(not(feature = "wasm"))]
        Err(e) => Err(e),
    }
}

/// The command to run, when this process is `mwcc-tools run COMMAND` or `mwcc-run COMMAND`.
pub fn wrapped_command() -> Option<Vec<String>> {
    let mut args = env::args();
    let program = args.next()?;
    let named = Path::new(&program)
        .file_stem()
        .is_some_and(|stem| stem.eq_ignore_ascii_case("mwcc-run"));
    let args: Vec<String> = args.collect();
    if named {
        Some(args)
    } else if args.first().is_some_and(|a| a == "run") {
        Some(args[1..].to_vec())
    } else {
        None
    }
}

fn is_sjiswrap(path: &str) -> bool {
    Path::new(path)
        .file_stem()
        .is_some_and(|stem| stem.eq_ignore_ascii_case("sjiswrap"))
}

/// The exit code of the command ARGS.
pub fn run(args: &[String]) -> Result<i32> {
    if args.is_empty() || matches!(args[0].as_str(), "--help" | "-h") {
        println!("{USAGE}");
        return Ok(0);
    }
    if args[0] == "ninja" {
        return crate::ninja_cli::run(&args[1..]);
    }
    let began = Instant::now();
    let compiler = if is_sjiswrap(&args[0]) { 1 } else { 0 };
    let (executable, compiler_args) = match args.get(compiler) {
        Some(executable) => (Path::new(executable), &args[compiler + 1..]),
        None => bail!("{} needs the compiler to run", args[0]),
    };
    #[cfg(feature = "wasm")]
    if let Some((root, version, module)) =
        crate::wasm_download::module_for(root().ok().as_deref(), executable)?
    {
        let timing = env::var_os("MWCC_RUN_TIMING").is_some_and(|v| v == "1");
        let loading = Instant::now();
        mwcc_oracle_core::in_process::preload(&module)?;
        let loaded = loading.elapsed();
        let environment: Vec<(String, String)> = env::vars().collect();
        // (the compiler's command line as its process would get it: ours from its executable on, where sjiswrap does
        // not make it)
        let verbatim = if compiler == 0 {
            own_command_line().and_then(|line| command_line_from(&line, executable, compiler_args))
        } else {
            None
        };
        let outcome = mwcc_oracle_core::in_process::run(
            &mwcc_oracle_core::in_process::Run {
                root: &root,
                version,
                module: &module,
                program: executable,
                args: compiler_args,
                directory: &env::current_dir()?,
                environment: &environment,
                timeout: None,
                trace: mwcc_oracle_core::in_process::traces(&root, executable),
                overlay: None,
                transcode_sjis: compiler == 1,
                command_line: verbatim.as_deref(),
                files: None,
            },
            &|| false,
        )
        .inspect_err(|e| {
            // (what the run wrote before a trap, as the process's console would show it)
            if let Some(trapped) = e.downcast_ref::<mwcc_oracle_core::in_process::Trapped>() {
                let _ = std::io::stdout().write_all(&trapped.console);
                let _ = std::io::stdout().flush();
            }
        })?;
        std::io::stdout().write_all(&outcome.stdout)?;
        std::io::stdout().flush()?;
        std::io::stderr().write_all(&outcome.stderr)?;
        if timing {
            eprintln!(
                "mwcc-tools run: {} in process ({}): {:.1} ms, of which {:.1} ms loading the module",
                executable.display(),
                version.name(),
                began.elapsed().as_secs_f64() * 1e3,
                loaded.as_secs_f64() * 1e3
            );
        }
        return Ok(outcome.exit_code);
    }
    #[cfg(not(feature = "wasm"))]
    let _ = compiler_args;
    let root = run_root()?;
    let status = runner::project_launcher(&env::current_dir()?, &root, Path::new(&args[0]))?
        .args(&args[1..])
        .status()
        .with_context(|| format!("launch {}", args[0]))?;
    Ok(status.code().unwrap_or(1))
}

/// This process's command line, as Windows gives it.
#[cfg(windows)]
fn own_command_line() -> Option<String> {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetCommandLineW() -> *const u16;
    }
    // (SAFETY: the process's command line, NUL-terminated, lives as long as the process)
    unsafe {
        let line = GetCommandLineW();
        if line.is_null() {
            return None;
        }
        let length = (0..).take_while(|&i| *line.add(i) != 0).count();
        String::from_utf16(std::slice::from_raw_parts(line, length)).ok()
    }
}

#[cfg(not(windows))]
fn own_command_line() -> Option<String> {
    None
}

/// The arguments of a Windows command line, each with where its text starts (as the C runtime splits them).
pub(crate) fn split_command_line(line: &str) -> Vec<(usize, String)> {
    let chars: Vec<(usize, char)> = line.char_indices().collect();
    let blank = |c: char| c == ' ' || c == '\t';
    let mut out = vec![];
    let mut i = 0;
    while i < chars.len() {
        while i < chars.len() && blank(chars[i].1) {
            i += 1;
        }
        if i >= chars.len() {
            break;
        }
        let start = chars[i].0;
        let mut arg = String::new();
        let mut quoted = false;
        while i < chars.len() && (quoted || !blank(chars[i].1)) {
            let mut backslashes = 0;
            while i < chars.len() && chars[i].1 == '\\' {
                backslashes += 1;
                i += 1;
            }
            if i < chars.len() && chars[i].1 == '"' {
                arg.extend(std::iter::repeat_n('\\', backslashes / 2));
                if backslashes % 2 == 1 {
                    arg.push('"');
                } else if quoted && i + 1 < chars.len() && chars[i + 1].1 == '"' {
                    arg.push('"');
                    i += 1;
                } else {
                    quoted = !quoted;
                }
                i += 1;
            } else {
                arg.extend(std::iter::repeat_n('\\', backslashes));
                if i < chars.len() && (quoted || !blank(chars[i].1)) {
                    arg.push(chars[i].1);
                    i += 1;
                }
            }
        }
        out.push((start, arg));
    }
    out
}

/// LINE from the argument EXECUTABLE on, when the arguments after it are ARGS (else None).
fn command_line_from(line: &str, executable: &Path, args: &[String]) -> Option<String> {
    let split = split_command_line(line);
    let executable = executable.to_str()?;
    let at = split.iter().rposition(|(_, a)| a == executable)?;
    let rest: Vec<&str> = split[at + 1..].iter().map(|(_, a)| a.as_str()).collect();
    let args: Vec<&str> = args.iter().map(String::as_str).collect();
    (rest == args).then(|| line[split[at].0..].trim_end().to_owned())
}

#[cfg(test)]
mod command_line_tests {
    use super::*;

    #[test]
    fn the_compilers_command_line_is_ours_from_it_on() {
        let line =
            r#"C:\x\mwcc-run.exe build\cc\mwcceppc.exe -pragma "inline_max_size(256)" -c "a b.c""#;
        let args = ["-pragma", "inline_max_size(256)", "-c", "a b.c"].map(String::from);
        assert_eq!(
            command_line_from(line, Path::new(r"build\cc\mwcceppc.exe"), &args).as_deref(),
            Some(r#"build\cc\mwcceppc.exe -pragma "inline_max_size(256)" -c "a b.c""#)
        );
        assert_eq!(
            command_line_from(line, Path::new(r"build\cc\mwcceppc.exe"), &args[..2]),
            None
        );
        assert_eq!(split_command_line(r#"a\\"b c" d"#)[0].1, r"a\b c");
    }
}
