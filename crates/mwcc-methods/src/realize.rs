//! `trace.realize`: a model edit of a function's colouring (as `trace.search` gives it) turned into bounded source
//! variants of the vregs it edits, each labelled `suggested` (the edit it realizes) and evaluated by
//! `experiment.run` like any other candidate. The vregs are found in the traced source through their definitions
//! and uses (the trace's instruction lines, inside the function's own braces) and their names (`--cse`'s: a local
//! of the source, a common subexpression's temporary, or another temporary). The rewrites, by edit:
//!
//! * `move` and `order` of a named local: its declaration's qualifier and signedness (`T* x` / `T* const x`,
//!   `int` / `unsigned int`), its declaration moved beside the anchor's, its initializer split from it, or the
//!   local made a temporary (its one use given its initializer); a `move`'s anchor, when a local, its
//!   declaration's qualifier and signedness too;
//! * `move` and `order` of a temporary: the temporary named (`__typeof__(e) temp = e;`, with and without
//!   `const`), and the declarations on its definition's statement as for a local;
//! * `drop`: a local made a temporary; a temporary's comparison with zero spelled `e ? 1 : 0`, or it named;
//! * `degree +`, `neighbours +` and `link`: a use extended (its last statement swapped with the next);
//! * `degree -`, `neighbours -` and `unlink`: the definition split (`x = a op b;` as `x = a; x op= b;`), started
//!   later, or a part of it named.
//!
//! The rewrites read the source as text (there is no C++ parser): a variant that does not compile is reported so
//! by its row. A statement swap is only made between statements without calls whose identifiers are disjoint, so
//! that it keeps the function's meaning; the others (a hoisted call, a changed type) can change it, and a row that
//! matches is a suggestion to review, not a proof.
use crate::{
    Call, Method,
    colouring::{class_of, describe, edits, loaded, names, one},
};
use anyhow::{Context, Result, ensure};
use mwcc_experiment::plan::Candidate;
use mwcc_oracle_core::{
    colouring::{self, Edit},
    cse,
    trace::{Allocation, Round},
    version::class_name,
};
use regex::Regex;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    sync::LazyLock,
};

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct RealizeParams {
    /// The loaded trace (`trace.load`), whose source text it rewrites.
    pub trace: String,
    pub function: Option<String>,
    /// GPR (default), FPR, VR or CR.
    pub class: Option<String>,
    /// One solution's edits, as `trace.search` gives them (`["move", 97, "after", 33]`, ...).
    #[schemars(with = "Vec<Value>")]
    pub edits: Vec<Value>,
    /// The candidates kept (default 32), taken from the edits in turn.
    pub limit: Option<usize>,
    /// Give the report's lines (`text`).
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct RealizeResult {
    pub function: String,
    pub class: String,
    /// The file the candidates change, as an experiment's plan names it.
    pub file: String,
    /// What each vreg the edits name is in the source: its name, kind, declaration and lines.
    #[schemars(with = "Vec<Value>")]
    pub vregs: Vec<Value>,
    /// `experiment.run`'s candidates (`space: {"list": candidates}`), complete files each labelled `suggested`.
    #[schemars(with = "Vec<Value>")]
    pub candidates: Vec<Value>,
    /// Each candidate's change, by name: the lines it changes, before and after.
    #[schemars(with = "BTreeMap<String, Value>")]
    pub changes: BTreeMap<String, Value>,
    /// What no rewrite was found for, and why.
    pub skipped: Vec<String>,
    /// The variants found before `limit` kept some.
    pub found: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

const KEYWORDS: &[&str] = &[
    "if", "else", "for", "while", "do", "switch", "case", "default", "return", "break", "continue",
    "goto", "sizeof", "new", "delete", "throw", "typedef", "operator", "this", "true", "false",
];
const QUALIFIERS: &[&str] = &[
    "const", "volatile", "static", "register", "unsigned", "signed", "struct", "class", "enum",
];

fn ident_char(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_'
}

/// A source: its text (LF newlines), and the same bytes with comments and literals blanked.
struct Code {
    text: String,
    code: Vec<u8>,
    starts: Vec<usize>,
}

impl Code {
    fn new(text: &str) -> Self {
        let text = text.replace("\r\n", "\n");
        let bytes = text.as_bytes();
        let mut code = bytes.to_vec();
        let (mut i, n) = (0, bytes.len());
        while i < n {
            let c = bytes[i];
            let next = bytes.get(i + 1).copied();
            if c == b'/' && next == Some(b'/') {
                while i < n && bytes[i] != b'\n' {
                    code[i] = b' ';
                    i += 1;
                }
            } else if c == b'/' && next == Some(b'*') {
                code[i] = b' ';
                code[i + 1] = b' ';
                i += 2;
                while i < n && !(bytes[i] == b'*' && bytes.get(i + 1) == Some(&b'/')) {
                    if bytes[i] != b'\n' {
                        code[i] = b' ';
                    }
                    i += 1;
                }
                code[i..(i + 2).min(n)].fill(b' ');
                i += 2;
            } else if c == b'"' || c == b'\'' {
                i += 1;
                while i < n && bytes[i] != c && bytes[i] != b'\n' {
                    if bytes[i] == b'\\' && i + 1 < n {
                        code[i] = b' ';
                        i += 1;
                    }
                    code[i] = b' ';
                    i += 1;
                }
                i += 1;
            } else if c == b'#'
                && bytes[..i]
                    .iter()
                    .rev()
                    .take_while(|&&b| b != b'\n')
                    .all(|b| b.is_ascii_whitespace())
            {
                // (a preprocessor line: not code)
                while i < n && bytes[i] != b'\n' {
                    code[i] = b' ';
                    i += 1;
                }
            } else {
                i += 1;
            }
        }
        let mut starts = vec![0];
        starts.extend(
            bytes
                .iter()
                .enumerate()
                .filter(|(_, b)| **b == b'\n')
                .map(|(i, _)| i + 1),
        );
        Self { text, code, starts }
    }
    /// The (1-based) line of OFFSET.
    fn line(&self, offset: usize) -> usize {
        self.starts.partition_point(|&s| s <= offset)
    }
    fn line_start(&self, line: usize) -> usize {
        self.starts[line - 1]
    }
    fn line_end(&self, line: usize) -> usize {
        self.starts.get(line).map_or(self.text.len(), |s| s - 1)
    }
    fn src(&self, from: usize, to: usize) -> &str {
        &self.text[from..to]
    }
    fn code(&self, from: usize, to: usize) -> &str {
        std::str::from_utf8(&self.code[from..to]).unwrap_or("")
    }
    /// The bracket matching the one at OPEN (forwards).
    fn matching(&self, open: usize) -> Option<usize> {
        let (o, c) = (
            self.code[open],
            match self.code[open] {
                b'(' => b')',
                b'[' => b']',
                b'{' => b'}',
                _ => return None,
            },
        );
        let mut depth = 0;
        for (i, &b) in self.code.iter().enumerate().skip(open) {
            if b == o {
                depth += 1;
            } else if b == c {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
        }
        None
    }
    /// The bracket matching the one at CLOSE (backwards).
    fn matching_back(&self, close: usize) -> Option<usize> {
        let (c, o) = (
            self.code[close],
            match self.code[close] {
                b')' => b'(',
                b']' => b'[',
                b'}' => b'{',
                _ => return None,
            },
        );
        let mut depth = 0;
        for i in (0..=close).rev() {
            let b = self.code[i];
            if b == c {
                depth += 1;
            } else if b == o {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
        }
        None
    }
    /// Each whole-word occurrence of WORD in FROM..TO.
    fn words(&self, word: &str, from: usize, to: usize) -> Vec<usize> {
        let w = word.as_bytes();
        let mut out = vec![];
        let mut at = from;
        while at + w.len() <= to {
            let Some(found) = self.code[at..to]
                .windows(w.len())
                .position(|x| x == w)
                .map(|p| p + at)
            else {
                break;
            };
            let before = found.checked_sub(1).map(|i| self.code[i]);
            let after = self.code.get(found + w.len()).copied();
            if !before.is_some_and(ident_char) && !after.is_some_and(ident_char) {
                out.push(found);
            }
            at = found + 1;
        }
        out
    }
    /// The innermost `{` around OFFSET at or after FROM.
    fn block(&self, from: usize, offset: usize) -> usize {
        let mut stack = vec![from];
        for i in from + 1..offset {
            match self.code[i] {
                b'{' => stack.push(i),
                b'}' if stack.len() > 1 => {
                    stack.pop();
                }
                _ => (),
            }
        }
        *stack.last().unwrap()
    }
}

/// A function's body in the source: its `{` and `}`.
#[derive(Clone, Copy, Debug)]
struct Span {
    open: usize,
    close: usize,
}

/// The definition of the function named SHORT (its last name) whose body has the most of LINES.
fn function_span(code: &Code, short: &str, lines: &BTreeSet<usize>) -> Option<Span> {
    let mut best: Option<(usize, Span)> = None;
    for at in code.words(short, 0, code.code.len()) {
        let mut i = at + short.len();
        while code.code.get(i).is_some_and(u8::is_ascii_whitespace) {
            i += 1;
        }
        if code.code.get(i) != Some(&b'(') {
            continue;
        }
        let Some(mut i) = code.matching(i) else {
            continue;
        };
        // (to the body: past qualifiers and a constructor's initializers)
        let open = loop {
            i += 1;
            match code.code.get(i) {
                Some(b'{') => break Some(i),
                Some(b';' | b'}' | b')') | None => break None,
                Some(b'(') => match code.matching(i) {
                    Some(close) => i = close,
                    None => break None,
                },
                _ => (),
            }
        };
        let Some(open) = open else { continue };
        let Some(close) = code.matching(open) else {
            continue;
        };
        let span = Span { open, close };
        let count = lines.range(code.line(at)..=code.line(close)).count();
        if count > 0 && best.is_none_or(|(n, _)| count > n) {
            best = Some((count, span));
        }
    }
    best.map(|(_, span)| span)
}

/// A statement: its text's range (to its `;`, or a header's `{`), and its first word.
#[derive(Clone, Debug)]
struct Statement {
    start: usize,
    end: usize,
    header: bool,
    word: String,
}

impl Statement {
    /// An expression statement or a declaration (not a header, a label or a jump).
    fn simple(&self) -> bool {
        !self.header
            && ![
                "if", "else", "for", "while", "do", "switch", "case", "default", "goto", "return",
                "break", "continue",
            ]
            .contains(&self.word.as_str())
    }
}

/// The statement around OFFSET (inside SPAN).
fn statement(code: &Code, span: Span, offset: usize) -> Option<Statement> {
    let (mut closes, mut unmatched) = (0usize, 0usize);
    let mut start = span.open + 1;
    for i in (span.open + 1..offset).rev() {
        match code.code[i] {
            b')' | b']' => closes += 1,
            b'(' | b'[' => {
                if closes > 0 {
                    closes -= 1;
                } else {
                    unmatched += 1;
                }
            }
            b';' if closes == 0 && unmatched == 0 => {
                start = i + 1;
                break;
            }
            b'{' | b'}' => {
                start = i + 1;
                break;
            }
            _ => (),
        }
    }
    while start < span.close && code.code[start].is_ascii_whitespace() {
        start += 1;
    }
    let mut depth = unmatched as i64;
    let mut end = None;
    let mut header = false;
    for i in offset.max(start)..span.close {
        match code.code[i] {
            b'(' | b'[' => depth += 1,
            b')' | b']' => depth -= 1,
            b';' if depth <= 0 => {
                end = Some(i + 1);
                break;
            }
            b'{' if depth <= 0 => {
                // (an initializer list `= {`, or a header's body)
                let before = code.code(start, i).trim_end();
                if before.ends_with('=') {
                    let close = code.matching(i)?;
                    let rest = code.code(close + 1, span.close);
                    let semi = rest.find(';')?;
                    end = Some(close + 1 + semi + 1);
                } else {
                    end = Some(i);
                    header = true;
                }
                break;
            }
            b'}' if depth <= 0 => return None,
            _ => (),
        }
    }
    let end = end?;
    let word: String = code.code[start..end]
        .iter()
        .take_while(|b| ident_char(**b))
        .map(|&b| b as char)
        .collect();
    // (a header without braces: `if (x) y;`)
    let header = header || ["if", "for", "while", "switch", "else", "do"].contains(&word.as_str());
    Some(Statement {
        start,
        end,
        header,
        word,
    })
}

/// The statement starting after END in the same block (none at the block's end).
fn next_statement(code: &Code, span: Span, end: usize) -> Option<Statement> {
    let mut i = end;
    while i < span.close && code.code[i].is_ascii_whitespace() {
        i += 1;
    }
    if i >= span.close || code.code[i] == b'}' {
        return None;
    }
    statement(code, span, i).filter(|s| s.start == i)
}

static IDENT: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"[A-Za-z_]\w*").unwrap());

fn identifiers(text: &str) -> BTreeSet<String> {
    IDENT
        .find_iter(text)
        .map(|m| m.as_str().to_owned())
        .filter(|w| !KEYWORDS.contains(&w.as_str()) && !w.as_bytes()[0].is_ascii_digit())
        .collect()
}

/// Whether TEXT (code) calls anything (a cast counts).
fn calls(text: &str) -> bool {
    let b = text.as_bytes();
    b.iter().enumerate().any(|(i, &c)| {
        if c != b'(' {
            return false;
        }
        let before = &text[..i];
        let trimmed = before.trim_end();
        match trimmed.as_bytes().last() {
            Some(b')' | b']' | b'>') => true,
            Some(&x) if ident_char(x) => {
                let word = trimmed
                    .rsplit(|ch: char| !(ch.is_ascii_alphanumeric() || ch == '_'))
                    .next()
                    .unwrap_or("");
                !["if", "while", "for", "switch", "sizeof", "return"].contains(&word)
            }
            _ => false,
        }
    })
}

/// A local's declaration: its statement, its type's range, its name's offset and its initializer's (after `=`).
#[derive(Clone, Debug)]
struct Declaration {
    statement: Statement,
    type_start: usize,
    name: usize,
    init: Option<(usize, usize)>,
}

static TYPE_PREFIX: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^(?:for\s*\(\s*)?((?:[A-Za-z_][\w]*(?:\s*::\s*[A-Za-z_]\w*)*(?:\s*<[^;(){}=]*>)?[\s*&]*)+)$")
        .unwrap()
});

/// NAME's declaration in SPAN: the last before OFFSET, or the first.
fn declaration(code: &Code, span: Span, name: &str, offset: usize) -> Option<Declaration> {
    let mut found = vec![];
    for at in code.words(name, span.open, span.close) {
        let Some(statement) = statement(code, span, at) else {
            continue;
        };
        let prefix = code.code(statement.start, at).trim_end();
        let Some(m) = TYPE_PREFIX.captures(prefix) else {
            continue;
        };
        let types = m.get(1).unwrap();
        let words = identifiers(types.as_str());
        let first = IDENT.find(types.as_str()).map_or("", |m| m.as_str());
        if words.is_empty()
            || KEYWORDS.contains(&first)
            || words.iter().all(|w| QUALIFIERS.contains(&w.as_str()))
        {
            continue;
        }
        let mut after = at + name.len();
        while code.code.get(after).is_some_and(u8::is_ascii_whitespace) {
            after += 1;
        }
        let init = match code.code.get(after) {
            Some(b'=') if code.code.get(after + 1) != Some(&b'=') => {
                Some((after + 1, statement.end - 1))
            }
            Some(b';' | b'[' | b'(') => None,
            _ => continue,
        };
        // (one declarator: `int a, b;` is left alone)
        if code.code(statement.start, statement.end).contains(',')
            && init.is_none_or(|(s, _)| code.code(statement.start, s).contains(','))
        {
            continue;
        }
        found.push(Declaration {
            type_start: statement.start + types.start(),
            statement,
            name: at,
            init,
        });
    }
    found
        .iter()
        .rev()
        .find(|d| d.name < offset)
        .or(found.first())
        .cloned()
}

/// One variant: what it changes (a description), the replacements of the text, and the edit it realizes.
#[derive(Clone, Debug)]
struct Variant {
    what: String,
    replacements: Vec<(usize, usize, String)>,
}

impl Variant {
    fn apply(&self, text: &str) -> String {
        let mut out = text.to_owned();
        let mut sorted = self.replacements.clone();
        sorted.sort_by_key(|r| std::cmp::Reverse(r.0));
        for (from, to, new) in sorted {
            out.replace_range(from..to, &new);
        }
        out
    }
}

fn indent_of(code: &Code, offset: usize) -> String {
    let start = code.line_start(code.line(offset));
    code.src(start, offset)
        .chars()
        .take_while(|c| c.is_whitespace())
        .collect()
}

/// Signed and unsigned spellings of the same width.
const SIGNS: &[(&str, &str)] = &[
    ("unsigned int", "int"),
    ("unsigned short", "short"),
    ("unsigned char", "signed char"),
    ("unsigned long", "long"),
    ("uint", "int"),
    ("u32", "s32"),
    ("u16", "s16"),
    ("u8", "s8"),
    ("unsigned", "int"),
];

static CONST: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\bconst\b\s*").unwrap());

/// The declaration's qualifier and signedness changed: `const` dropped or added, the type's sign swapped, both.
fn declaration_variants(code: &Code, d: &Declaration, name: &str) -> Vec<Variant> {
    let types = code.src(d.type_start, d.name);
    let line = code.line(d.name);
    let mut out = vec![];
    let toggled = if let Some(last) = CONST.find_iter(types).last() {
        format!("{}{}", &types[..last.start()], &types[last.end()..])
    } else if types.trim_end().ends_with(['*', '&']) {
        format!("{types}const ")
    } else {
        format!("const {types}")
    };
    let mut signed = None;
    for (a, b) in SIGNS {
        let swap = |from: &str, to: &str| -> Option<String> {
            let re = Regex::new(&format!(r"\b{}\b", regex::escape(from))).ok()?;
            re.find(types)
                .map(|m| format!("{}{to}{}", &types[..m.start()], &types[m.end()..]))
        };
        if let Some(s) = swap(a, b).or_else(|| swap(b, a)) {
            signed = Some(s);
            break;
        }
    }
    let mut push = |new: String| {
        if new != types {
            out.push(Variant {
                what: format!("line {line}: `{}{name}` as `{}{name}`", types, new),
                replacements: vec![(d.type_start, d.name, new)],
            });
        }
    };
    push(toggled.clone());
    if let Some(signed) = &signed {
        push(signed.clone());
        let both = if let Some(last) = CONST.find_iter(signed).last() {
            format!("{}{}", &signed[..last.start()], &signed[last.end()..])
        } else if signed.trim_end().ends_with(['*', '&']) {
            format!("{signed}const ")
        } else {
            format!("const {signed}")
        };
        push(both);
    }
    out
}

/// `T x = e;` as `T x;` and `x = e;` (not a constant or a reference).
fn split_initializer(code: &Code, d: &Declaration, name: &str) -> Option<Variant> {
    let (from, to) = d.init?;
    let types = code.src(d.type_start, d.name);
    // (not a `for` header's declaration)
    if d.statement.header
        || CONST.is_match(types)
        || types.contains('&')
        || code.code(from, to).trim_start().starts_with('{')
    {
        return None;
    }
    let indent = indent_of(code, d.statement.start);
    Some(Variant {
        what: format!(
            "line {}: `{name}`'s initializer split from its declaration",
            code.line(d.name)
        ),
        replacements: vec![(
            d.name + name.len(),
            d.statement.end,
            format!(";\n{indent}{name} ={};", code.src(from, to).trim_end()),
        )],
    })
}

/// The whole lines of a statement (when it has its lines to itself): their range, to remove it.
fn whole_lines(code: &Code, s: &Statement) -> Option<(usize, usize)> {
    let (first, last) = (code.line(s.start), code.line(s.end - 1));
    let start = code.line_start(first);
    let end = code.line_end(last);
    (code.code(start, s.start).trim().is_empty() && code.code(s.end, end).trim().is_empty())
        .then(|| (start, (end + 1).min(code.text.len())))
}

/// The local made a temporary: its one use (in the next statement) given its initializer.
fn inline_local(code: &Code, span: Span, d: &Declaration, name: &str) -> Option<Variant> {
    let (from, to) = d.init?;
    if d.statement.header {
        return None;
    }
    let uses: Vec<usize> = code
        .words(name, span.open, span.close)
        .into_iter()
        .filter(|&at| at != d.name)
        .collect();
    let [use_at] = uses[..] else { return None };
    let next = next_statement(code, span, d.statement.end)?;
    if !(next.start <= use_at && use_at < next.end) {
        return None;
    }
    // (a use that writes it, or takes its address, is not a read)
    let before = code.code(next.start, use_at).trim_end();
    let after = code.code(use_at + name.len(), next.end).trim_start();
    if before.ends_with('&') && !before.ends_with("&&")
        || after.starts_with("++")
        || after.starts_with("--")
        || (after.starts_with('=') && !after.starts_with("=="))
        || ["+=", "-=", "*=", "/=", "|=", "&=", "^=", "<<=", ">>="]
            .iter()
            .any(|op| after.starts_with(op))
    {
        return None;
    }
    let (start, end) = whole_lines(code, &d.statement)?;
    let init = code.src(from, to).trim();
    let simple = init.bytes().all(|b| ident_char(b) || b == b'.');
    Some(Variant {
        what: format!(
            "line {}: the local `{name}` made a temporary (its use given `{init}`)",
            code.line(d.name)
        ),
        replacements: vec![
            (start, end, String::new()),
            (
                use_at,
                use_at + name.len(),
                if simple {
                    init.to_owned()
                } else {
                    format!("({init})")
                },
            ),
        ],
    })
}

/// The declaration moved just before or after ANCHOR's (in the same block), its initializer left in place when
/// it moves earlier.
fn move_declaration(
    code: &Code,
    span: Span,
    d: &Declaration,
    name: &str,
    anchor: &Declaration,
    after: bool,
) -> Option<Variant> {
    let (start, end) = whole_lines(code, &d.statement)?;
    let (astart, aend) = whole_lines(code, &anchor.statement)?;
    if code.block(span.open, d.statement.start) != code.block(span.open, anchor.statement.start)
        || start == astart
    {
        return None;
    }
    let at = if after { aend } else { astart };
    if at == start || at == end {
        return None;
    }
    let lines = code.src(start, end).to_owned();
    let what = format!(
        "line {}: `{name}`'s declaration moved {} `{}`'s (line {})",
        code.line(d.name),
        if after { "after" } else { "before" },
        code.src(
            anchor.name,
            anchor.name + IDENT.find(&code.text[anchor.name..]).map_or(0, |m| m.end())
        ),
        code.line(anchor.name)
    );
    if at < start {
        // (earlier: `T x;` there, `x = e;` here)
        let declared = format!(
            "{}{name};\n",
            code.src(code.line_start(code.line(d.statement.start)), d.name)
        );
        let replacements = match d.init {
            None => vec![(start, end, String::new()), (at, at, lines)],
            Some((from, to)) => {
                if CONST.is_match(code.src(d.type_start, d.name))
                    || code.src(d.type_start, d.name).contains('&')
                {
                    return None;
                }
                let indent = indent_of(code, d.statement.start);
                vec![
                    (
                        start,
                        end,
                        format!("{indent}{name} ={};\n", code.src(from, to).trim_end()),
                    ),
                    (at, at, declared),
                ]
            }
        };
        Some(Variant { what, replacements })
    } else {
        // (later: only a declaration without an initializer, before any use of it)
        if d.init.is_some() || code.words(name, end, at).iter().any(|_| true) {
            return None;
        }
        Some(Variant {
            what,
            replacements: vec![(start, end, String::new()), (at, at, lines)],
        })
    }
}

/// The postfix expressions (calls, members, elements) and call arguments in a statement worth naming.
fn subexpressions(code: &Code, s: &Statement) -> Vec<(usize, usize)> {
    let mut out: Vec<(usize, usize)> = vec![];
    let text = code.code(s.start, s.end);
    // (only what is evaluated unconditionally: before any `&&`, `||` or `?`)
    let limit = ["&&", "||", "?"]
        .iter()
        .filter_map(|op| text.find(op))
        .min()
        .map_or(s.end, |p| s.start + p);
    let b = &code.code;
    let mut i = s.start;
    while i < limit {
        if b[i] == b'(' {
            let Some(close) = code.matching(i) else { break };
            // (a call: the name and the postfix chain before it)
            let mut from = i;
            let mut j = i;
            while j > s.start && b[j - 1].is_ascii_whitespace() {
                j -= 1;
            }
            if j > s.start && ident_char(b[j - 1]) {
                let mut k = j;
                loop {
                    while k > s.start && ident_char(b[k - 1]) {
                        k -= 1;
                    }
                    let word = code.code(k, j);
                    if KEYWORDS.contains(&word) || word.is_empty() {
                        break;
                    }
                    from = k;
                    // (`a.b`, `a->b`, `a::b`)
                    let chain = [".", "->", "::"]
                        .iter()
                        .find(|op| code.code(s.start, k).ends_with(**op));
                    match chain {
                        Some(op) => {
                            let mut m = k - op.len();
                            if m > s.start && matches!(b[m - 1], b')' | b']') {
                                let Some(open) = code.matching_back(m - 1) else {
                                    break;
                                };
                                m = open;
                                from = m;
                                k = m;
                                j = m;
                                if m > s.start && ident_char(b[m - 1]) {
                                    continue;
                                }
                                break;
                            }
                            k = m;
                            j = m;
                        }
                        None => break,
                    }
                }
                let word = code.code(from, i).trim();
                let callee = word.rsplit(['.', ':', '>', ' ']).next().unwrap_or("");
                if from < i && !KEYWORDS.contains(&callee) && !callee.ends_with("_cast") {
                    // (to the end of the chain after it)
                    let mut to = close + 1;
                    loop {
                        let rest = code.code(to, s.end);
                        if let Some(r) = rest.strip_prefix('.').or_else(|| rest.strip_prefix("->"))
                        {
                            let word = IDENT.find(r).filter(|m| m.start() == 0);
                            let Some(word) = word else { break };
                            to += rest.len() - r.len() + word.end();
                        } else if rest.starts_with('(') || rest.starts_with('[') {
                            let Some(c) = code.matching(to) else { break };
                            to = c + 1;
                        } else {
                            break;
                        }
                    }
                    out.push((from, to));
                }
            }
            // (each argument that is not a name or a literal)
            let mut depth = 0;
            let mut arg = i + 1;
            for (k, &byte) in b.iter().enumerate().take(close + 1).skip(i + 1) {
                match byte {
                    b'(' | b'[' | b'{' => depth += 1,
                    b')' | b']' | b'}' if depth > 0 => depth -= 1,
                    b',' | b')' if depth == 0 => {
                        let text = code.code(arg, k);
                        let trimmed = text.trim();
                        if !trimmed.is_empty()
                            && !trimmed.bytes().all(|c| ident_char(c) || c == b'.')
                        {
                            let lead = text.len() - text.trim_start().len();
                            out.push((arg + lead, arg + lead + trimmed.len()));
                        }
                        arg = k + 1;
                    }
                    _ => (),
                }
            }
        }
        i += 1;
    }
    // (an assignment or initializer's right side)
    let text = code.code(s.start, s.end);
    if let Some(eq) = text.find('=').filter(|&p| {
        let t = text.as_bytes();
        t.get(p + 1) != Some(&b'=')
            && !matches!(t.get(p.wrapping_sub(1)), Some(b'=' | b'!' | b'<' | b'>'))
    }) {
        let rhs = &text[eq + 1..text.len() - 1];
        let lead = rhs.len() - rhs.trim_start().len();
        let from = s.start + eq + 1 + lead;
        let to = from + rhs.trim().len();
        if from < to && from < limit {
            out.push((from, to));
        }
    }
    let mut seen = BTreeSet::new();
    // (not the statement's whole expression: a call statement's value may be void)
    let whole = code
        .code(s.start, s.end)
        .trim_end_matches(';')
        .trim_end()
        .len();
    out.retain(|&(from, to)| {
        let t = code.code(from, to);
        from < limit
            && !(from == s.start && to - from == whole)
            && to <= s.end
            && seen.insert((from, to))
            && !t.contains("++")
            && !t.contains("--")
            && !t.contains("&&")
            && !t.contains("||")
            && !Regex::new(r"[^=!<>]=[^=]").unwrap().is_match(t)
    });
    out
}

/// A temporary named at its statement: `__typeof__(e) temp = e;` (with and without `const`: GC 2.7 refuses
/// `__typeof__(e) const`) for each of its expressions, those sharing the tree's names first.
fn name_temporary(
    code: &Code,
    s: &Statement,
    v: u32,
    tree: Option<&str>,
    taken: &BTreeSet<String>,
) -> Vec<Variant> {
    // (a statement that starts its line: what is inserted before it stays out of a loop on the line)
    let line_start = code.line_start(code.line(s.start));
    if !(s.simple() || s.word == "return" || (s.word == "if" || s.word == "switch"))
        || !code.code(line_start, s.start).trim().is_empty()
    {
        return vec![];
    }
    let mut name = format!("temp{v}");
    while taken.contains(&name) {
        name.push('_');
    }
    let wanted = tree.map(identifiers).unwrap_or_default();
    let mut found = subexpressions(code, s);
    // (the expressions sharing most names with the temporary's tree first, then the smaller)
    found.sort_by_key(|&(from, to)| {
        let shared = identifiers(code.code(from, to))
            .intersection(&wanted)
            .count();
        (std::cmp::Reverse(shared), to - from)
    });
    let indent = indent_of(code, s.start);
    let mut out = vec![];
    for (from, to) in found {
        let e = code.src(from, to);
        for constant in ["", "const "] {
            out.push(Variant {
                what: format!(
                    "line {}: `{}` named {name}{}",
                    code.line(from),
                    e,
                    if constant.is_empty() { "" } else { " (const)" }
                ),
                replacements: vec![
                    (
                        line_start,
                        line_start,
                        format!("{indent}{constant}__typeof__({e}) {name} = {e};\n"),
                    ),
                    (from, to, name.clone()),
                ],
            });
        }
    }
    out
}

/// A comparison with zero spelled as a choice: `e != 0` as `e ? TRUE : FALSE` (each pair the file names, then
/// `true`/`false` and `1`/`0`), `e == 0` the other way round.
fn spell_boolean(code: &Code, s: &Statement) -> Vec<Variant> {
    static CMP: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"([A-Za-z_][\w.\->]*(?:\[[^\]]*\])?)\s*(!=|==)\s*0\b").unwrap()
    });
    let names = identifiers(std::str::from_utf8(&code.code).unwrap_or(""));
    let mut pairs: Vec<(String, String)> = names
        .iter()
        .filter_map(|n| {
            let yes = n.strip_suffix("FALSE").map(|p| format!("{p}TRUE"))?;
            names.contains(&yes).then(|| (yes, n.clone()))
        })
        .collect();
    pairs.push(("true".into(), "false".into()));
    pairs.push(("1".into(), "0".into()));
    let text = code.code(s.start, s.end);
    let mut out = vec![];
    for c in CMP.captures_iter(text) {
        let m = c.get(0).unwrap();
        let e = c.get(1).unwrap().as_str();
        for (yes, no) in &pairs {
            let (a, b) = if &c[2] == "!=" { (yes, no) } else { (no, yes) };
            let new = format!("{e} ? {a} : {b}");
            out.push(Variant {
                what: format!(
                    "line {}: `{}` as `{new}`",
                    code.line(s.start + m.start()),
                    m.as_str()
                ),
                replacements: vec![(s.start + m.start(), s.start + m.end(), new)],
            });
        }
    }
    out
}

/// The statement swapped with the next one (both without calls, their identifiers disjoint).
fn swap_next(code: &Code, span: Span, s: &Statement, why: &str) -> Option<Variant> {
    let next = next_statement(code, span, s.end)?;
    if !s.simple() || !next.simple() {
        return None;
    }
    let (a, b) = (code.code(s.start, s.end), code.code(next.start, next.end));
    if calls(a) || calls(b) || !identifiers(a).is_disjoint(&identifiers(b)) {
        return None;
    }
    Some(Variant {
        what: format!(
            "lines {} and {} swapped ({why})",
            code.line(s.start),
            code.line(next.start)
        ),
        replacements: vec![
            (s.start, s.end, code.src(next.start, next.end).to_owned()),
            (next.start, next.end, code.src(s.start, s.end).to_owned()),
        ],
    })
}

/// `[T] x = a op b;` as `[T] x = a; x op= b;` at the last top-level binary operator.
fn split_expression(code: &Code, s: &Statement) -> Option<Variant> {
    if !s.simple() {
        return None;
    }
    let text = code.code(s.start, s.end);
    let eq = text
        .find('=')
        .filter(|&p| text.as_bytes().get(p + 1) != Some(&b'='))?;
    let left = text[..eq].trim_end();
    if left.ends_with(['!', '<', '>', '+', '-', '*', '/', '|', '&', '^', '%'])
        || CONST.is_match(left)
        || left.contains('&')
    {
        return None;
    }
    let lhs = IDENT.find_iter(left).last()?.as_str();
    if !left.ends_with(lhs) {
        return None;
    }
    let rhs_from = eq + 1;
    let bytes = text.as_bytes();
    let mut depth = 0;
    let mut split = None;
    for k in rhs_from..text.len() - 1 {
        match bytes[k] {
            b'(' | b'[' => depth += 1,
            b')' | b']' => depth -= 1,
            b'?' | b':' if depth == 0 => return None,
            op @ (b'+' | b'-' | b'*' | b'|' | b'^' | b'&') if depth == 0 => {
                let prev = text[rhs_from..k].trim_end().as_bytes().last().copied();
                let doubled = bytes.get(k + 1) == Some(&op) || (k > 0 && bytes[k - 1] == op);
                let arrow = op == b'-' && bytes.get(k + 1) == Some(&b'>');
                if !doubled
                    && !arrow
                    && prev.is_some_and(|p| ident_char(p) || p == b')' || p == b']')
                {
                    split = Some(k);
                }
            }
            _ => (),
        }
    }
    let k = split?;
    let (a, b) = (text[rhs_from..k].trim(), text[k + 1..text.len() - 1].trim());
    if identifiers(b).contains(lhs) || a.is_empty() || b.is_empty() {
        return None;
    }
    let op = bytes[k] as char;
    let indent = indent_of(code, s.start);
    let source = code.src(s.start, s.end);
    let head = &source[..eq];
    Some(Variant {
        what: format!(
            "line {}: `{}` split at `{op}`",
            code.line(s.start),
            source.trim()
        ),
        replacements: vec![(
            s.start,
            s.end,
            format!(
                "{}= {};\n{indent}{lhs} {op}= {};",
                head,
                code.src(s.start + rhs_from, s.start + k).trim(),
                code.src(s.start + k + 1, s.end - 1).trim()
            ),
        )],
    })
}

/// A vreg as the source has it.
#[derive(Clone, Debug, Default)]
struct Vreg {
    v: u32,
    name: Option<String>,
    /// `local`, `cse` (a common subexpression's temporary), `temporary` (another's), or `unnamed`.
    kind: &'static str,
    tree: Option<String>,
    defs: BTreeSet<usize>,
    uses: BTreeSet<usize>,
    declaration: Option<Declaration>,
}

fn vreg_lines(allocation: &Allocation, class: &str, v: u32) -> (BTreeSet<usize>, BTreeSet<usize>) {
    let (mut defs, mut uses) = (BTreeSet::new(), BTreeSet::new());
    for block in &allocation.before.blocks {
        for i in &block.instructions {
            let Some(line) = i.line.filter(|l| *l > 0) else {
                continue;
            };
            for o in &i.operands {
                if o.is_register() && o.register() == v && o.class() == class {
                    if o.defines() {
                        defs.insert(line as usize);
                    }
                    if o.uses() {
                        uses.insert(line as usize);
                    }
                }
            }
        }
    }
    (defs, uses)
}

fn realize(call: &mut Call<'_>, params: RealizeParams) -> Result<RealizeResult> {
    let input = loaded(call, &params.trace)?;
    let class = class_of(&params.class)?;
    let allocation = one(&input, params.function.as_deref())?;
    let source = input.text.as_deref().context(
        "the trace has no source text (a unit's trace has it; a saved trace needs its PREFIX.source)",
    )?;
    let file = input
        .source
        .clone()
        .context("the trace names no source file")?
        .replace('\\', "/");
    let round: &Round = allocation
        .before
        .rounds
        .iter()
        .rfind(|r| class_name(r.class) == class)
        .with_context(|| {
            format!(
                "{}: no {class} colouring traced",
                allocation.before.function
            )
        })?;
    let names = names(&input, allocation)?;
    let parsed = cse::parse(&input.trace.records)?;
    let function = parsed.get(&allocation.before.link);
    let edits = edits(&params.edits)?;
    ensure!(!edits.is_empty(), "no edits to realize");
    let code = Code::new(source);
    let short = allocation
        .before
        .function
        .rsplit("::")
        .next()
        .unwrap_or(&allocation.before.function)
        .split('<')
        .next()
        .unwrap_or("")
        .to_owned();
    let all_lines: BTreeSet<usize> = allocation
        .before
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .filter_map(|i| i.line.filter(|l| *l > 0).map(|l| l as usize))
        .collect();
    let span = function_span(&code, &short, &all_lines).with_context(|| {
        format!(
            "{} is not defined in {file} (the trace's lines are not in its body)",
            allocation.before.function
        )
    })?;
    let (first, last) = (code.line(span.open), code.line(span.close));
    let taken = identifiers(code.code(span.open, span.close));

    let mut vregs: BTreeMap<u32, Vreg> = BTreeMap::new();
    let mut vreg = |v: u32| -> Vreg {
        vregs
            .entry(v)
            .or_insert_with(|| {
                let (defs, uses) = vreg_lines(allocation, &class, v);
                let inside = |l: &usize| (first..=last).contains(l);
                let (defs, uses): (BTreeSet<_>, BTreeSet<_>) = (
                    defs.into_iter().filter(inside).collect(),
                    uses.into_iter().filter(inside).collect(),
                );
                let name = names.get(&(class.clone(), v)).cloned();
                let (kind, tree) = match (&name, function) {
                    (None, _) => ("unnamed", None),
                    (Some(n), Some(f)) => {
                        let temp = f
                            .passes
                            .iter()
                            .flat_map(|p| p["temps"].as_array().cloned().unwrap_or_default())
                            .find(|t| t["name"] == n.as_str());
                        if let Some(t) = temp {
                            (
                                "cse",
                                t["tree"].as_str().map(|t| {
                                    cse::tree(t, mwcc_oracle_core::version::enode_names(), false)
                                }),
                            )
                        } else if f.objects.iter().any(|(o, _)| o == n) {
                            ("temporary", None)
                        } else {
                            ("local", None)
                        }
                    }
                    (Some(_), None) => ("local", None),
                };
                let at = defs
                    .iter()
                    .chain(&uses)
                    .min()
                    .map_or(span.close, |l| code.line_start(*l) + 1);
                let declaration = (kind == "local")
                    .then(|| declaration(&code, span, name.as_deref().unwrap(), at))
                    .flatten();
                Vreg {
                    v,
                    name,
                    kind,
                    tree,
                    defs,
                    uses,
                    declaration,
                }
            })
            .clone()
    };

    let mut skipped = vec![];
    let mut per_edit: Vec<(String, Vec<Variant>)> = vec![];
    // (the statements of a vreg's definitions, or its uses when it has none here)
    let statements_of = |r: &Vreg| -> Vec<Statement> {
        let lines = if r.defs.is_empty() { &r.uses } else { &r.defs };
        let mut out: Vec<Statement> = vec![];
        for l in lines {
            let start = code.line_start(*l);
            let end = code.line_end(*l);
            let first = (start..end).find(|&i| !code.code[i].is_ascii_whitespace());
            if let Some(s) = first.and_then(|i| statement(&code, span, i))
                && !out.iter().any(|o| o.start == s.start)
            {
                out.push(s);
            }
        }
        out
    };
    let locals_declared_in = |s: &Statement| -> Vec<(String, Declaration)> {
        identifiers(code.code(s.start, s.end))
            .into_iter()
            .filter_map(|n| {
                declaration(&code, span, &n, s.end)
                    .filter(|d| d.statement.start == s.start)
                    .map(|d| (n, d))
            })
            .collect()
    };
    for edit in &edits {
        let said = describe(edit, &class, &names, round);
        let mut variants = vec![];
        let mut on = |v: u32,
                      anchor: Option<(u32, bool)>,
                      kind: &str,
                      variants: &mut Vec<Variant>,
                      skipped: &mut Vec<String>| {
            let r = vreg(v);
            let label = format!(
                "%{v}{}",
                r.name.as_ref().map_or(String::new(), |n| format!(" ({n})"))
            );
            let before = variants.len();
            match (kind, r.kind) {
                ("number", "local") | ("drop", "local") => {
                    let (Some(d), Some(name)) = (&r.declaration, &r.name) else {
                        skipped.push(format!("{said}: {label} is not declared in the function (a local of an inlined function?)"));
                        return;
                    };
                    if kind == "drop" {
                        variants.extend(inline_local(&code, span, d, name));
                        variants.extend(declaration_variants(&code, d, name));
                        return;
                    }
                    variants.extend(declaration_variants(&code, d, name));
                    if let Some((a, after)) = anchor {
                        let ar = vreg(a);
                        if let (Some(ad), Some(_)) = (&ar.declaration, &ar.name) {
                            variants.extend(move_declaration(&code, span, d, name, ad, after));
                        }
                    }
                    variants.extend(split_initializer(&code, d, name));
                    variants.extend(inline_local(&code, span, d, name));
                }
                // (a move is relative: the anchor's number can move instead)
                ("anchor", "local") => {
                    if let (Some(d), Some(name)) = (&r.declaration, &r.name) {
                        variants.extend(declaration_variants(&code, d, name));
                    }
                    return;
                }
                ("anchor", _) => return,
                ("number", _) | ("drop", _) => {
                    let statements = statements_of(&r);
                    if statements.is_empty() {
                        skipped.push(format!(
                            "{said}: {label} has no line in the function's body"
                        ));
                        return;
                    }
                    for s in &statements {
                        for (name, d) in locals_declared_in(s) {
                            variants.extend(declaration_variants(&code, &d, &name));
                            if kind == "drop" {
                                variants.extend(inline_local(&code, span, &d, &name));
                            }
                        }
                        if kind == "drop" {
                            variants.extend(spell_boolean(&code, s));
                        }
                        variants.extend(name_temporary(&code, s, v, r.tree.as_deref(), &taken));
                    }
                }
                ("longer", _) => {
                    let last_line = r.defs.iter().chain(&r.uses).max();
                    if let Some(l) = last_line {
                        let start = code.line_start(*l);
                        let i = (start..code.line_end(*l))
                            .find(|&i| !code.code[i].is_ascii_whitespace());
                        if let Some(s) = i.and_then(|i| statement(&code, span, i)) {
                            variants.extend(swap_next(
                                &code,
                                span,
                                &s,
                                &format!("{label}'s last use later"),
                            ));
                        }
                    }
                }
                ("shorter", _) => {
                    for s in statements_of(&r) {
                        variants.extend(split_expression(&code, &s));
                        variants.extend(swap_next(
                            &code,
                            span,
                            &s,
                            &format!("{label}'s definition later"),
                        ));
                        variants.extend(name_temporary(&code, &s, v, r.tree.as_deref(), &taken));
                    }
                }
                _ => (),
            }
            if variants.len() == before {
                skipped.push(format!(
                    "{said}: no rewrite of {label} ({}) applies",
                    r.kind
                ));
            }
        };
        match edit {
            Edit::Move {
                node,
                after,
                anchor,
            } => {
                on(
                    *node,
                    Some((*anchor, *after)),
                    "number",
                    &mut variants,
                    &mut skipped,
                );
                on(*anchor, None, "anchor", &mut variants, &mut skipped);
            }
            Edit::Order(order) => {
                for v in colouring::renumbered(order) {
                    on(v, None, "number", &mut variants, &mut skipped);
                }
            }
            Edit::Drop(v) => on(*v, None, "drop", &mut variants, &mut skipped),
            Edit::Degree(v, d) | Edit::Neighbours(v, d) => on(
                *v,
                None,
                if *d > 0 { "longer" } else { "shorter" },
                &mut variants,
                &mut skipped,
            ),
            Edit::Link(a, b) => {
                on(*a, None, "longer", &mut variants, &mut skipped);
                on(*b, None, "longer", &mut variants, &mut skipped);
            }
            Edit::Unlink(a, b) => {
                on(*a, None, "shorter", &mut variants, &mut skipped);
                on(*b, None, "shorter", &mut variants, &mut skipped);
            }
        }
        per_edit.push((said, variants));
    }

    // (the edits' variants in turn, each text once)
    let limit = params.limit.unwrap_or(32);
    let found: usize = per_edit.iter().map(|(_, v)| v.len()).sum();
    let mut seen: BTreeSet<String> = BTreeSet::from([code.text.clone()]);
    let newline = if source.contains("\r\n") {
        "\r\n"
    } else {
        "\n"
    };
    let (mut candidates, mut changes, mut lines) = (vec![], BTreeMap::new(), vec![]);
    let longest = per_edit.iter().map(|(_, v)| v.len()).max().unwrap_or(0);
    'all: for k in 0..longest {
        for (said, variants) in &per_edit {
            let Some(variant) = variants.get(k) else {
                continue;
            };
            if candidates.len() >= limit {
                break 'all;
            }
            let text = variant.apply(&code.text);
            if !seen.insert(text.clone()) {
                continue;
            }
            let name = format!("suggested {}: {}", candidates.len() + 1, variant.what);
            let changed = Code::new(&text);
            let (from, to) = (
                code.line(variant.replacements.iter().map(|r| r.0).min().unwrap_or(0)),
                code.line(variant.replacements.iter().map(|r| r.1).max().unwrap_or(0)),
            );
            let grown = changed.starts.len() as i64 - code.starts.len() as i64;
            let new_to = (to as i64 + grown).max(from as i64) as usize;
            let before: Vec<&str> = (from..=to)
                .map(|l| code.src(code.line_start(l), code.line_end(l)))
                .collect();
            let after: Vec<&str> = (from..=new_to.min(changed.starts.len()))
                .map(|l| changed.src(changed.line_start(l), changed.line_end(l)))
                .collect();
            changes.insert(name.clone(), json!({"edit": said, "what": variant.what, "line": from, "before": before, "after": after}));
            lines.push(format!("{name}\n   realizes: {said}"));
            candidates.push(serde_json::to_value(Candidate {
                name,
                files: BTreeMap::from([(file.clone(), Some(text.replace('\n', newline)))]),
                suggested: Some(said.clone()),
                ..Candidate::default()
            })?);
        }
    }
    let vregs_out: Vec<Value> = vregs
        .values()
        .map(|r| {
            json!({
                "vreg": r.v,
                "name": r.name,
                "kind": r.kind,
                "tree": r.tree,
                "defs": r.defs,
                "uses": r.uses,
                "declaration": r.declaration.as_ref().map(|d| code.line(d.name)),
            })
        })
        .collect();
    let text = params.text.then(|| {
        let mut out = vec![format!(
            "== {}: {} candidates from {} edits ({found} variants found) in {file} lines {first}-{last} ==",
            allocation.before.function,
            candidates.len(),
            edits.len()
        )];
        out.extend(lines);
        out.extend(skipped.iter().map(|s| format!("   skipped: {s}")));
        out
    });
    Ok(RealizeResult {
        function: allocation.before.function.clone(),
        class,
        file,
        vregs: vregs_out,
        candidates,
        changes,
        skipped,
        found,
        text,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![crate::method!(
        "trace.realize",
        "a model edit of a function's colouring (trace.search's) as bounded source variants of the vregs it edits: experiment.run's candidates, each labelled `suggested`",
        RealizeParams,
        RealizeResult,
        realize
    )]
}

#[cfg(test)]
mod tests {
    use super::*;

    const SOURCE: &str = "// f\nint f(S* s) {\n    int n = s->n * 3;\n    void* dest = Get();\n    const uint size = Size(); /* ; */\n    int k = n + s->m;\n    a = 1;\n    b = 2;\n    Use(dest, size, n != 0);\n    return k;\n}\n";

    fn span(code: &Code) -> Span {
        function_span(code, "f", &BTreeSet::from([3, 9])).unwrap()
    }

    #[test]
    fn statements_and_declarations_are_found_in_the_function() {
        let code = Code::new(SOURCE);
        let span = span(&code);
        assert_eq!(code.line(span.open), 2);
        let d = declaration(&code, span, "dest", span.close).unwrap();
        assert_eq!(code.src(d.type_start, d.name), "void* ");
        assert!(d.init.is_some());
        let s = statement(&code, span, code.line_start(5) + 6).unwrap();
        assert_eq!(code.src(s.start, s.end), "const uint size = Size();");
        assert!(declaration(&code, span, "a", span.close).is_none());
    }

    #[test]
    fn declarations_change_qualifier_and_sign() {
        let code = Code::new(SOURCE);
        let span = span(&code);
        let d = declaration(&code, span, "dest", span.close).unwrap();
        let v = declaration_variants(&code, &d, "dest");
        assert!(v[0].apply(&code.text).contains("void* const dest = Get();"));
        let d = declaration(&code, span, "size", span.close).unwrap();
        let texts: Vec<String> = declaration_variants(&code, &d, "size")
            .iter()
            .map(|v| v.apply(&code.text))
            .collect();
        assert!(texts[0].contains("    uint size = Size();"));
        assert!(texts[1].contains("const int size = Size();"));
        assert!(texts[2].contains("    int size = Size();"));
    }

    #[test]
    fn temporaries_are_named_and_expressions_split() {
        let code = Code::new(SOURCE);
        let span = span(&code);
        let s = statement(&code, span, code.line_start(9) + 4).unwrap();
        let named = name_temporary(&code, &s, 40, Some("Get()"), &BTreeSet::new());
        assert!(
            named.iter().any(|v| v
                .apply(&code.text)
                .contains("    __typeof__(n != 0) temp40 = n != 0;\n    Use(dest, size, temp40);"))
        );
        assert!(named.iter().any(|v| {
            v.apply(&code.text)
                .contains("    const __typeof__(n != 0) temp40 = n != 0;")
        }));
        assert!(
            !named
                .iter()
                .any(|v| v.apply(&code.text).contains("__typeof__(Use("))
        );
        let spelled: Vec<String> = spell_boolean(&code, &s)
            .iter()
            .map(|v| v.apply(&code.text))
            .collect();
        assert!(spelled[0].contains("Use(dest, size, n ? true : false);"));
        assert!(spelled[1].contains("Use(dest, size, n ? 1 : 0);"));
        let s = statement(&code, span, code.line_start(6) + 4).unwrap();
        assert!(
            split_expression(&code, &s)
                .unwrap()
                .apply(&code.text)
                .contains("    int k = n;\n    k += s->m;")
        );
        let s = statement(&code, span, code.line_start(7) + 4).unwrap();
        assert!(
            swap_next(&code, span, &s, "")
                .unwrap()
                .apply(&code.text)
                .contains("    b = 2;\n    a = 1;")
        );
        let s = statement(&code, span, code.line_start(4) + 4).unwrap();
        assert!(swap_next(&code, span, &s, "").is_none());
        // (a statement after others on its line is not named before them)
        let code = Code::new(
            "void g() {
    for (;;) { x = F(a + b); }
}
",
        );
        let span = function_span(&code, "g", &BTreeSet::from([2])).unwrap();
        let s = statement(&code, span, code.line_start(2) + 15).unwrap();
        assert_eq!(code.src(s.start, s.end), "x = F(a + b);");
        assert!(name_temporary(&code, &s, 40, None, &BTreeSet::new()).is_empty());
    }

    #[test]
    fn locals_are_inlined_split_and_moved() {
        let code = Code::new(SOURCE);
        let span = span(&code);
        let k = declaration(&code, span, "k", span.close).unwrap();
        assert!(inline_local(&code, span, &k, "k").is_none());
        let dest = declaration(&code, span, "dest", span.close).unwrap();
        assert!(
            split_initializer(&code, &dest, "dest")
                .unwrap()
                .apply(&code.text)
                .contains("    void* dest;\n    dest = Get();")
        );
        let n = declaration(&code, span, "n", span.close).unwrap();
        let moved = move_declaration(&code, span, &dest, "dest", &n, false)
            .unwrap()
            .apply(&code.text);
        assert!(
            moved.contains("{\n    void* dest;\n    int n = s->n * 3;\n    dest = Get();"),
            "{moved}"
        );
    }
}
