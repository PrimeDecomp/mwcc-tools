use mwcc_oracle_core::process_tree;
#[cfg(windows)]
use std::fs;
use std::{
    process::{Command, Stdio},
    thread,
    time::{Duration, Instant},
};

#[test]
fn cancellation_stops_descendants_before_they_can_write_later() {
    let scratch = tempfile::tempdir().unwrap();
    let marker = scratch.path().join("late-write");
    let ready = scratch.path().join("ready");
    #[cfg(unix)]
    let mut command = {
        let mut command = Command::new("/bin/sh");
        command
            .args([
                "-c",
                "( sleep 2; printf leaked > \"$1\" ) & printf ready > \"$2\"; wait",
                "process-tree-test",
            ])
            .arg(&marker)
            .arg(&ready);
        command
    };
    #[cfg(windows)]
    let mut command = {
        let powershell = std::path::PathBuf::from(std::env::var_os("SystemRoot").unwrap())
            .join("System32/WindowsPowerShell/v1.0/powershell.exe");
        let quote = |path: &std::path::Path| path.to_string_lossy().replace('\'', "''");
        let child = scratch.path().join("child.ps1");
        let parent = scratch.path().join("parent.ps1");
        fs::write(
            &child,
            format!(
                "Start-Sleep -Seconds 2\nSet-Content -LiteralPath '{}' -Value leaked\n",
                quote(&marker)
            ),
        )
        .unwrap();
        fs::write(&parent, format!("Start-Process -FilePath '{}' -WindowStyle Hidden -ArgumentList '-NoProfile -NonInteractive -File \"{}\"' | Out-Null\nSet-Content -LiteralPath '{}' -Value ready\nStart-Sleep -Seconds 20\n", quote(&powershell), quote(&child), quote(&ready))).unwrap();
        let mut command = Command::new(powershell);
        command
            .args([
                "-NoLogo",
                "-NoProfile",
                "-NonInteractive",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
            ])
            .arg(parent);
        command
    };
    process_tree::prepare(&mut command);
    let mut child = command
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();
    let began = Instant::now();
    while !ready.is_file() && began.elapsed() < Duration::from_secs(10) {
        thread::sleep(Duration::from_millis(10));
    }
    let was_ready = ready.is_file();
    process_tree::terminate(&mut child).unwrap();
    assert!(was_ready, "parent did not start its child");
    assert!(child.try_wait().unwrap().is_some());
    thread::sleep(Duration::from_secs(3));
    assert!(
        !marker.exists(),
        "descendant outlived cancellation and wrote after its parent was reaped"
    );
}
