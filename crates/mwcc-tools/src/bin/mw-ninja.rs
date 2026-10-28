//! `mw-ninja ARGS...` is `mwcc-tools run ninja ARGS...`: the `mwcc-tools` beside this executable (else the one on
//! the PATH), with the same arguments, exit status and Ctrl-C.
use std::{env, ffi::OsString, process::Command};

fn main() {
    let name = if cfg!(windows) {
        "mwcc-tools.exe"
    } else {
        "mwcc-tools"
    };
    // (canonical: an install linked onto the PATH runs the mwcc-tools of its own directory)
    let sibling = env::current_exe()
        .and_then(|p| p.canonicalize())
        .ok()
        .and_then(|p| Some(p.parent()?.join(name)))
        .filter(|p| p.is_file());
    let program = sibling.map_or_else(|| OsString::from("mwcc-tools"), OsString::from);
    let mut command = Command::new(&program);
    command.args(["run", "ninja"]).args(env::args_os().skip(1));
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        let error = command.exec();
        eprintln!(
            "mw-ninja: cannot run {}: {error}",
            program.to_string_lossy()
        );
        std::process::exit(127);
    }
    #[cfg(not(unix))]
    {
        // (Ctrl-C reaches every process of the console: this one waits for mwcc-tools to stop)
        ignore_ctrl_c();
        match command.status() {
            Ok(status) => std::process::exit(status.code().unwrap_or(1)),
            Err(error) => {
                eprintln!(
                    "mw-ninja: cannot run {}: {error}",
                    program.to_string_lossy()
                );
                std::process::exit(127);
            }
        }
    }
}

#[cfg(windows)]
fn ignore_ctrl_c() {
    // (a handler, not SetConsoleCtrlHandler(NULL, TRUE), which the child would inherit)
    unsafe extern "system" fn handled(_: u32) -> i32 {
        1
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn SetConsoleCtrlHandler(
            handler: Option<unsafe extern "system" fn(u32) -> i32>,
            add: i32,
        ) -> i32;
    }
    unsafe {
        SetConsoleCtrlHandler(Some(handled), 1);
    }
}

#[cfg(not(any(unix, windows)))]
fn ignore_ctrl_c() {}
