//! The generated-Ninja subset used by decomp kits. This is not a build engine.
use anyhow::{Context, Result, bail, ensure};
use serde::Serialize;
use std::{
    collections::BTreeMap,
    fs,
    path::{Component, Path, PathBuf},
};

#[derive(Clone, Debug, Serialize)]
pub struct CompileRule {
    pub outputs: Vec<String>,
    pub source: String,
    pub rule: String,
    pub mw_version: Option<String>,
    pub flags: Vec<String>,
}

/// POSIX shlex-compatible splitting, matching the Python oracle. Windows process
/// argument quoting happens later in Command; do not run this string in a shell.
pub fn split_flags(text: &str) -> Result<Vec<String>> {
    let mut args = vec![];
    let mut word = String::new();
    let mut quote = None;
    let mut active = false;
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if let Some(q) = quote {
            if c == q {
                quote = None;
            } else if c == '\\' && q == '"' && chars.peek().is_some_and(|n| *n == '\\' || *n == '"')
            {
                word.push(chars.next().unwrap());
            } else {
                word.push(c);
            }
        } else {
            match c {
                '\'' | '"' => {
                    quote = Some(c);
                    active = true;
                }
                '\\' => {
                    word.push(
                        chars
                            .next()
                            .context("trailing backslash in compiler flags")?,
                    );
                    active = true;
                }
                c if c.is_whitespace() => {
                    if active {
                        args.push(std::mem::take(&mut word));
                        active = false;
                    }
                }
                _ => {
                    word.push(c);
                    active = true;
                }
            }
        }
    }
    ensure!(quote.is_none(), "unclosed quote in compiler flags");
    if active {
        args.push(word);
    }
    Ok(args)
}

fn expand(text: &str, variables: &BTreeMap<String, String>, depth: usize) -> Result<String> {
    ensure!(depth < 32, "recursive Ninja variable expansion");
    let mut out = String::new();
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c != '$' {
            out.push(c);
            continue;
        }
        let next = chars.next().context("trailing Ninja escape")?;
        match next {
            '$' | ':' | ' ' => out.push(next),
            '{' => {
                let mut key = String::new();
                let mut closed = false;
                for c in chars.by_ref() {
                    if c == '}' {
                        closed = true;
                        break;
                    }
                    key.push(c);
                }
                ensure!(closed, "unclosed Ninja variable");
                out.push_str(&expand(
                    variables
                        .get(&key)
                        .with_context(|| format!("unknown Ninja variable {key}"))?,
                    variables,
                    depth + 1,
                )?);
            }
            c if c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '.' => {
                let mut key = String::from(c);
                while chars.peek().is_some_and(|c| {
                    c.is_ascii_alphanumeric() || *c == '_' || *c == '-' || *c == '.'
                }) {
                    key.push(chars.next().unwrap());
                }
                out.push_str(&expand(
                    variables
                        .get(&key)
                        .with_context(|| format!("unknown Ninja variable {key}"))?,
                    variables,
                    depth + 1,
                )?);
            }
            _ => bail!("unsupported Ninja escape ${next}"),
        }
    }
    Ok(out)
}

fn ninja_words(text: &str, variables: &BTreeMap<String, String>) -> Result<Vec<String>> {
    let mut words = vec![];
    let mut token = String::new();
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '$' {
            token.push(c);
            token.push(chars.next().context("trailing Ninja escape")?);
        } else if c.is_whitespace() {
            if !token.is_empty() {
                words.push(expand(&token, variables, 0)?);
                token.clear();
            }
        } else {
            token.push(c);
        }
    }
    if !token.is_empty() {
        words.push(expand(&token, variables, 0)?);
    }
    Ok(words)
}

pub fn parse(text: &str) -> Result<Vec<CompileRule>> {
    let text = text.replace("\r\n", "\n");
    let mut logical = String::new();
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '$' && chars.peek() == Some(&'\n') {
            chars.next();
            while chars.peek().is_some_and(|c| *c == ' ' || *c == '\t') {
                chars.next();
            }
        } else {
            logical.push(c);
            if c == '$' && chars.peek() == Some(&'$') {
                logical.push(chars.next().unwrap());
            }
        }
    }
    let lines: Vec<_> = logical.lines().collect();
    let mut globals = BTreeMap::new();
    let mut out = vec![];
    let mut i = 0;
    while i < lines.len() {
        let line = lines[i];
        i += 1;
        if line.starts_with("include ") || line.starts_with("subninja ") {
            bail!(
                "Ninja include/subninja is not supported; give a self-contained generated build.ninja"
            );
        }
        if let Some(build) = line.strip_prefix("build ") {
            let mut escaped = false;
            let mut colon = None;
            for (at, c) in build.char_indices() {
                if escaped {
                    escaped = false;
                    continue;
                }
                if c == '$' {
                    escaped = true;
                } else if c == ':' {
                    colon = Some(at);
                    break;
                }
            }
            let colon = colon.context("Ninja build edge missing colon")?;
            let mut variables = globals.clone();
            let mut body = vec![];
            while i < lines.len() && (lines[i].starts_with(' ') || lines[i].starts_with('\t')) {
                body.push(lines[i].trim());
                i += 1;
            }
            // Variables are normally fully expanded by project configuration.
            // Evaluate edge assignments against their inherited outer scope.
            let raw_rule = build[colon + 1..]
                .split_whitespace()
                .next()
                .context("Ninja edge has no rule")?;
            if !raw_rule.starts_with("mwcc") {
                continue;
            }
            for line in body {
                if let Some((k, v)) = line.split_once(" = ") {
                    if matches!(k, "cflags" | "mw_version") {
                        let value = expand(v, &variables, 0)?;
                        variables.insert(k.into(), value.replace('$', "$$"));
                    } else {
                        variables.insert(k.into(), v.into());
                    }
                }
            }
            let inputs = ninja_words(&build[colon + 1..], &variables)?;
            let rule = inputs.first().context("Ninja edge has no rule")?;
            let source = inputs
                .get(1)
                .filter(|s| !matches!(s.as_str(), "|" | "||"))
                .context("mwcc edge has no source")?
                .clone();
            // Windows-generated Ninja edges use backslashes even when read by
            // a Linux/wibo host. Keep native Windows spelling, but make the
            // source usable by host filesystem/provenance reads on Unix.
            #[cfg(not(windows))]
            let source = source.replace('\\', "/");
            let flags = variables.get("cflags").context("mwcc edge has no cflags")?;
            out.push(CompileRule {
                outputs: ninja_words(&build[..colon], &variables)?,
                source,
                rule: rule.clone(),
                mw_version: variables
                    .get("mw_version")
                    .map(|v| expand(v, &variables, 0))
                    .transpose()?
                    .map(|v| v.replace('\\', "/")),
                flags: split_flags(&expand(flags, &variables, 0)?)?,
            });
        } else if !line.starts_with([' ', '\t', '#'])
            && let Some((k, v)) = line.split_once(" = ")
        {
            // Preserve lazy variable references like Ninja; resolve at each edge.
            globals.insert(k.to_owned(), v.to_owned());
        }
    }
    Ok(out)
}

fn path_key(base: &Path, value: &str) -> String {
    let path = base.join(value.replace(['/', '\\'], std::path::MAIN_SEPARATOR_STR));
    let mut clean = PathBuf::new();
    for part in path.components() {
        match part {
            Component::CurDir => (),
            Component::ParentDir => {
                clean.pop();
            }
            other => clean.push(other.as_os_str()),
        }
    }
    let key = clean.to_string_lossy().into_owned();
    if cfg!(windows) {
        key.to_lowercase()
    } else {
        key
    }
}

pub fn find_rule(path: &Path, source: &str) -> Result<(CompileRule, PathBuf)> {
    let absolute = fs::canonicalize(path).with_context(|| format!("read {}", path.display()))?;
    let root = absolute
        .parent()
        .context("Ninja file has no parent")?
        .to_owned();
    let wanted = path_key(&root, source);
    let rule = parse(&fs::read_to_string(&absolute)?)?
        .into_iter()
        .find(|r| path_key(&root, &r.source) == wanted)
        .with_context(|| format!("{} has no mwcc rule for {source}", path.display()))?;
    Ok((rule, root))
}

pub fn find_output_rule(path: &Path, output: &str) -> Result<(CompileRule, PathBuf)> {
    let absolute = fs::canonicalize(path)?;
    let root = absolute
        .parent()
        .context("Ninja file has no parent")?
        .to_owned();
    let wanted = path_key(&root, output);
    let rule = parse(&fs::read_to_string(&absolute)?)?
        .into_iter()
        .find(|r| r.outputs.iter().any(|o| path_key(&root, o) == wanted))
        .with_context(|| format!("{} has no mwcc edge for {output}", path.display()))?;
    Ok((rule, root))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ninja_escapes_continuations_global_and_edge_versions() {
        let text = "mw_version = GC/1.3.2\ncflags = -i \"include dir\" -DVALUE=$$1\nbuild build/a$ b.o: mwcc src/a$ b.cpp | tool\n  cflags = $cflags $\n    -O4,p\nbuild F$:/out.o: mwcc F$:/src.cpp\n  mw_version = GC/2.7\n  cflags = -c\n";
        let rules = parse(text).unwrap();
        assert_eq!(rules.len(), 2);
        assert_eq!(rules[0].source, "src/a b.cpp");
        assert_eq!(rules[0].outputs, ["build/a b.o"]);
        assert_eq!(rules[0].flags, ["-i", "include dir", "-DVALUE=$1", "-O4,p"]);
        assert_eq!(rules[0].mw_version.as_deref(), Some("GC/1.3.2"));
        assert_eq!(rules[1].mw_version.as_deref(), Some("GC/2.7"));
        assert_eq!(rules[1].source, "F:/src.cpp");
    }
    #[test]
    fn quotes_match_posix_shlex_without_shell_execution() {
        assert_eq!(
            split_flags("-D 'two words' \"\" a\\ b \"C:\\src\"").unwrap(),
            ["-D", "two words", "", "a b", "C:\\src"]
        );
        assert!(split_flags("'unclosed").is_err());
        assert!(parse("subninja other.ninja").is_err());
        let rules =
            parse("cflags = -O4,p\nbuild build\\test.o: mwcc src\\folder\\test.cpp\n").unwrap();
        assert_eq!(
            rules[0].source,
            if cfg!(windows) {
                "src\\folder\\test.cpp"
            } else {
                "src/folder/test.cpp"
            }
        );
    }
}
