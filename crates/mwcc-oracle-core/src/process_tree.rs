//! Private process groups for commands whose children can keep writing after a timeout.
use anyhow::{Context, Result};
use std::process::{Child, Command};

#[derive(Debug)]
pub struct TerminationFailed(pub String);
impl std::fmt::Display for TerminationFailed {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}
impl std::error::Error for TerminationFailed {}

pub fn prepare(command: &mut Command) {
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        command.process_group(0);
    }
    #[cfg(not(unix))]
    let _ = command;
}
/// Kill the isolated command tree and reap its root before another build can start.
pub fn terminate(child: &mut Child) -> Result<()> {
    terminate_inner(child).map_err(|error| TerminationFailed(format!("{error:#}")).into())
}
fn terminate_inner(child: &mut Child) -> Result<()> {
    // Never send a PID-based signal after the root has already been reaped.
    if child.try_wait()?.is_some() {
        return Ok(());
    }
    #[cfg(unix)]
    {
        unsafe extern "C" {
            fn kill(pid: i32, signal: i32) -> i32;
        }
        let group = i32::try_from(child.id()).context("process group ID exceeds pid_t")?;
        // prepare() assigns this child its own process group before exec. SIGKILL
        // goes only to that group, including descendants of shells and Ninja.
        let result = unsafe { kill(-group, 9) };
        if result != 0 && child.try_wait()?.is_none() {
            return Err(TerminationFailed(format!(
                "terminate process group {group}: {}",
                std::io::Error::last_os_error()
            ))
            .into());
        }
    }
    #[cfg(windows)]
    {
        use std::process::Stdio;
        let system = std::env::var_os("SystemRoot").context("SystemRoot is unavailable")?;
        let status = Command::new(std::path::Path::new(&system).join("System32/taskkill.exe"))
            .args(["/PID", &child.id().to_string(), "/T", "/F"])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|error| TerminationFailed(format!("launch taskkill: {error}")))?;
        if !status.success() && child.try_wait()?.is_none() {
            return Err(TerminationFailed(format!(
                "taskkill failed for process {}: {status}",
                child.id()
            ))
            .into());
        }
    }
    #[cfg(not(any(unix, windows)))]
    child.kill()?;
    child.wait().context("reap timed-out command")?;
    Ok(())
}
