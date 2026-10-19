//! The variants format of the kit's try-variants (exact text, whitespace included; several replacements per
//! variant):
//!
//! ```text
//! ### name of the variant
//! @@@ old
//! text to replace (must occur exactly once in the source)
//! @@@ new
//! replacement text
//! ```
//!
//! A block's final newline belongs to the separator, so an old block can end mid-line.
use anyhow::{Result, bail};
use serde::{Deserialize, Serialize};

/// One variant: its name and its replacements (old, new).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Variant {
    pub name: String,
    pub edits: Vec<(String, String)>,
}

/// The variants of TEXT, and the warnings (an edit that replaces text with itself).
pub fn parse(text: &str) -> Result<(Vec<Variant>, Vec<String>)> {
    let mut variants: Vec<Variant> = vec![];
    // (the block being read: old or new)
    let mut mode: Option<bool> = None;
    for (number, line) in text.split_inclusive('\n').enumerate() {
        let number = number + 1;
        let stripped = line.trim_end_matches(['\r', '\n']);
        if let Some(name) = stripped.strip_prefix("### ") {
            variants.push(Variant {
                name: name.trim().to_owned(),
                edits: vec![],
            });
            mode = None;
        } else if stripped.starts_with("@@@") {
            if stripped != "@@@ old" && stripped != "@@@ new" {
                bail!(
                    "variants line {number}: unknown separator {stripped:?} (use \"@@@ old\" / \"@@@ new\")"
                );
            }
            let Some(current) = variants.last_mut() else {
                bail!("variants line {number}: {stripped:?} before the first \"### name\"");
            };
            let old = stripped == "@@@ old";
            mode = Some(old);
            if old {
                current.edits.push((String::new(), String::new()));
            } else if current.edits.last().is_none_or(|(_, new)| !new.is_empty()) {
                bail!("variants line {number}: \"@@@ new\" without a preceding \"@@@ old\"");
            }
        } else if let (Some(current), Some(old)) = (variants.last_mut(), mode) {
            let edit = current.edits.last_mut().unwrap();
            let line = line.replace("\r\n", "\n");
            if old {
                edit.0.push_str(&line);
            } else {
                edit.1.push_str(&line);
            }
        } else if !stripped.trim().is_empty() && !variants.is_empty() {
            bail!("variants line {number}: text outside an \"@@@ old\" / \"@@@ new\" block");
        }
    }
    let mut warnings = vec![];
    for variant in &mut variants {
        if variant.edits.is_empty() {
            bail!("variant {:?} has no \"@@@ old\" block", variant.name);
        }
        for (old, new) in &mut variant.edits {
            for block in [&mut *old, &mut *new] {
                if block.ends_with('\n') {
                    block.pop();
                }
            }
            if old.is_empty() {
                bail!("variant {:?} has an empty \"@@@ old\" block", variant.name);
            }
            if old == new {
                warnings.push(format!(
                    "variant {:?} replaces text with itself",
                    variant.name
                ));
            }
        }
    }
    Ok((variants, warnings))
}

/// SOURCE with EDITS applied: exact replacements on LF-normalized text, each old text occurring exactly once; the
/// source's own newline style is restored.
pub fn apply(source: &str, edits: &[(String, String)]) -> Result<String> {
    let newline = if source.contains("\r\n") {
        "\r\n"
    } else {
        "\n"
    };
    let mut text = source.replace("\r\n", "\n");
    for (old, new) in edits {
        let count = text.matches(old.as_str()).count();
        if count != 1 {
            let shown: String = old.chars().take(80).collect();
            bail!("old text occurs {count} times (must be exactly once): {shown:?}");
        }
        text = text.replacen(old.as_str(), new, 1);
    }
    Ok(text.replace('\n', newline))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn variants_parse_as_try_variants_parses_them() {
        let text = "### one\r\n@@@ old\r\nint a;\r\n@@@ new\r\nint b;\r\n@@@ old\r\nx\n@@@ new\n\n### two\n@@@ old\nsame\n@@@ new\nsame\n";
        let (variants, warnings) = parse(text).unwrap();
        assert_eq!(variants.len(), 2);
        assert_eq!(
            variants[0].edits,
            [("int a;".into(), "int b;".into()), ("x".into(), "".into())]
        );
        assert_eq!(variants[1].edits, [("same".into(), "same".into())]);
        assert_eq!(warnings.len(), 1);
        assert!(parse("@@@ old\nx\n").is_err());
        assert!(parse("### a\n@@@ new\nx\n").is_err());
        assert!(parse("### a\n@@@ old\n\n@@@ new\nx\n").is_err());
        assert!(parse("### a\n").is_err());
        assert!(parse("### a\n@@@ odd\n").is_err());
        assert!(parse("### a\nstray\n@@@ old\nx\n").is_err());
    }

    #[test]
    fn edits_apply_once_and_keep_the_newline_style() {
        let source = "int a;\r\nint b;\r\n";
        let out = apply(
            source,
            &[("int a;\nint b;".into(), "int c;\nint d;".into())],
        )
        .unwrap();
        assert_eq!(out, "int c;\r\nint d;\r\n");
        assert!(apply("x x", &[("x".into(), "y".into())]).is_err());
        assert!(apply("x", &[("z".into(), "y".into())]).is_err());
    }
}
