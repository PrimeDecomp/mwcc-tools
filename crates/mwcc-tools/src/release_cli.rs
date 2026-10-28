use anyhow::{Context, Result, ensure};
use std::path::PathBuf;

pub fn run(args: &[String]) -> Result<()> {
    let mut root = std::env::current_dir()?;
    let mut binary = None;
    let mut target = None;
    let mut revision = None;
    let mut output = None;
    let mut notices = None;
    let mut ninja = None;
    let mut i = 0;
    while i < args.len() {
        let option = &args[i];
        if !option.starts_with('-') {
            ensure!(
                output.is_none(),
                "package-release accepts one output archive"
            );
            output = Some(PathBuf::from(option));
        } else {
            ensure!(
                [
                    "--root",
                    "--binary",
                    "--target",
                    "--source-revision",
                    "--notices",
                    "--ninja"
                ]
                .contains(&option.as_str()),
                "unknown package-release option {option}"
            );
            let value = crate::option_value(args, &mut i)?;
            match option.as_str() {
                "--root" => root = value.into(),
                "--binary" => binary = Some(PathBuf::from(value)),
                "--target" => target = Some(value.as_str()),
                "--notices" => notices = Some(PathBuf::from(value)),
                "--ninja" => ninja = Some(PathBuf::from(value)),
                _ => revision = Some(value.as_str()),
            }
        }
        i += 1;
    }
    let output = output.context("package-release OUTPUT.zip --binary FILE --target TARGET [--ninja FILE] [--root DIR] [--source-revision COMMIT] [--notices DIR]")?;
    ensure!(
        output.extension().is_some_and(|e| e == "zip"),
        "release output must end in .zip"
    );
    let binary = binary.context("--binary is required")?;
    let target = target.context("--target is required")?;
    let notices = notices.unwrap_or_else(|| root.join("build/notices").join(target));
    // (mw-ninja: by default the one built beside the executable, where there is one)
    let ninja = ninja.or_else(|| {
        let name = if target.contains("windows") {
            "mw-ninja.exe"
        } else {
            "mw-ninja"
        };
        Some(binary.with_file_name(name)).filter(|p| p.is_file())
    });
    let digest = mwcc_oracle_core::release::write_with_notices(
        &root,
        &binary,
        ninja.as_deref(),
        target,
        revision,
        &output,
        &notices,
    )?;
    println!("{digest}  {}", output.display());
    Ok(())
}
