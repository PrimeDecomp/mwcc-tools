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
    /// The compiler the rule's command runs, as spelled there (relative to the build's directory); the sjiswrap that
    /// runs it (Shift JIS sources from UTF-8), if one does; and whether it precompiles a header (`-precompile`).
    pub compiler: Option<String>,
    pub sjiswrap: Option<String>,
    pub precompile: bool,
}

/// What a compile rule's COMMAND (expanded with the edge's VARIABLES) runs: the compiler, whether through sjiswrap,
/// and whether it precompiles. Wrappers before the compiler (`cmd /c`, sjiswrap, wibo, wine) are skipped; what
/// follows `&&` (dtk's extab clean) is not the compile's.
fn command_parts(
    command: &str,
    variables: &BTreeMap<String, String>,
) -> (Option<String>, Option<String>, bool) {
    // (a word with ninja's own variables, `$in` or `$out`, is kept as it is)
    let words: Vec<String> = command
        .split_whitespace()
        .map(|word| expand(word, variables, 0).unwrap_or_else(|_| word.to_owned()))
        .collect();
    let words: Vec<&str> = words
        .iter()
        .map(String::as_str)
        .take_while(|w| *w != "&&")
        .collect();
    let mut at = 0;
    if words.first().is_some_and(|w| w.eq_ignore_ascii_case("cmd"))
        && words.get(1).is_some_and(|w| w.eq_ignore_ascii_case("/c"))
    {
        at = 2;
    }
    let mut sjiswrap = None;
    while let Some(word) = words.get(at) {
        let name = word
            .rsplit(['/', '\\'])
            .next()
            .unwrap_or(word)
            .to_ascii_lowercase();
        match name.as_str() {
            "sjiswrap.exe" | "sjiswrap" => sjiswrap = Some((*word).to_owned()),
            "wibo" | "wine" => {}
            _ => break,
        }
        at += 1;
    }
    let precompile = words.contains(&"-precompile");
    (words.get(at).map(|w| (*w).to_owned()), sjiswrap, precompile)
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
    // (each rule's command)
    let mut rules: BTreeMap<String, String> = BTreeMap::new();
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
        if let Some(name) = line.strip_prefix("rule ") {
            while i < lines.len() && (lines[i].starts_with(' ') || lines[i].starts_with('\t')) {
                if let Some(command) = lines[i].trim().strip_prefix("command = ") {
                    rules.insert(name.trim().to_owned(), command.to_owned());
                }
                i += 1;
            }
            continue;
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
            let (compiler, sjiswrap, precompile) = rules
                .get(rule.as_str())
                .map_or((None, None, false), |command| {
                    command_parts(command, &variables)
                });
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
                compiler,
                sjiswrap,
                precompile,
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

pub(crate) fn path_key(base: &Path, value: &str) -> String {
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

/// The first compile edge of the Ninja file PATH that EDGE says has WANTED among its paths (as their keys under the
/// file's directory compare: [`path_key`]), and that directory; MISSING says why there is none.
fn find_edge(
    path: &Path,
    wanted: &str,
    edge: impl Fn(&CompileRule, &dyn Fn(&str) -> bool) -> bool,
    missing: impl FnOnce() -> String,
) -> Result<(CompileRule, PathBuf)> {
    let absolute = fs::canonicalize(path).with_context(|| format!("read {}", path.display()))?;
    let root = absolute
        .parent()
        .context("Ninja file has no parent")?
        .to_owned();
    let wanted = path_key(&root, wanted);
    let is_wanted = |path: &str| path_key(&root, path) == wanted;
    let rule = parse(&fs::read_to_string(&absolute)?)?
        .into_iter()
        .find(|r| edge(r, &is_wanted))
        .with_context(missing)?;
    Ok((rule, root))
}

pub fn find_rule(path: &Path, source: &str) -> Result<(CompileRule, PathBuf)> {
    find_edge(
        path,
        source,
        |r, is_wanted| is_wanted(&r.source),
        || format!("{} has no mwcc rule for {source}", path.display()),
    )
}

pub fn find_output_rule(path: &Path, output: &str) -> Result<(CompileRule, PathBuf)> {
    find_edge(
        path,
        output,
        |r, is_wanted| r.outputs.iter().any(|o| is_wanted(o)),
        || format!("{} has no mwcc edge for {output}", path.display()),
    )
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
    fn rule_commands_give_the_compiler_and_its_wrappers() {
        let text = r"rule mwcc_sjis_extab
  command = cmd /c build\tools\sjiswrap.exe $
      build\compilers\$mw_version\mwcceppc.exe $cflags -MMD -c $in -o $basedir && build\tools\dtk.exe extab clean $out $out
  description = MWCC $out
rule mwcc_pch
  command = $wrapper build/compilers/$mw_version/mwcceppc.exe $cflags -MMD -c $in -o $basedir -precompile $basefilestem.mch
wrapper = build/tools/wibo
build a.o: mwcc_sjis_extab a.cpp
  mw_version = GC\2.7
  cflags = -O4,p
build b.o: mwcc_pch b.h
  mw_version = GC/1.3.2
  cflags = -O4,p
";
        let rules = parse(text).unwrap();
        assert_eq!(
            rules[0].compiler.as_deref(),
            Some(r"build\compilers\GC\2.7\mwcceppc.exe")
        );
        assert_eq!(
            rules[0].sjiswrap.as_deref(),
            Some(r"build\tools\sjiswrap.exe")
        );
        assert!(!rules[0].precompile);
        assert_eq!(
            rules[1].compiler.as_deref(),
            Some("build/compilers/GC/1.3.2/mwcceppc.exe")
        );
        assert!(rules[1].sjiswrap.is_none() && rules[1].precompile);
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
