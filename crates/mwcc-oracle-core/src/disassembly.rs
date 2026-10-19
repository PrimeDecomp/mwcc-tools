//! Capstone-compatible x86 listings, instruction diffs and nearest-function search.
use crate::{coff::Object, executable, investigate, pe::Pe, sequence};
use anyhow::{Context, Result, ensure};
use capstone::prelude::*;
use regex::Regex;
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
    sync::OnceLock,
};
#[derive(Clone, Debug, Serialize)]
pub struct Instruction {
    pub address: u64,
    pub size: usize,
    pub mnemonic: String,
    pub text: String,
    pub field: bool,
}
pub fn decoder() -> Result<Capstone> {
    Ok(Capstone::new()
        .x86()
        .mode(arch::x86::ArchMode::Mode32)
        .syntax(arch::x86::ArchSyntax::Intel)
        .build()?)
}
pub fn decode(
    decoder: &Capstone,
    code: &[u8],
    address: u64,
    wild: &BTreeSet<usize>,
) -> Result<Vec<Instruction>> {
    let instructions = decoder.disasm_all(code, address)?;
    Ok(instructions
        .iter()
        .map(|i| Instruction {
            address: i.address(),
            size: i.bytes().len(),
            mnemonic: i.mnemonic().unwrap_or("").into(),
            text: format!(
                "{} {}",
                i.mnemonic().unwrap_or(""),
                i.op_str().unwrap_or("")
            )
            .trim()
            .into(),
            field: (i.address() as usize..i.address() as usize + i.bytes().len())
                .any(|at| wild.contains(&at)),
        })
        .collect())
}
pub fn shape(text: &str, field: bool) -> String {
    static NUMBERS: OnceLock<Regex> = OnceLock::new();
    let regex = NUMBERS.get_or_init(|| Regex::new(r"0x[0-9a-f]+|\b\d+\b").unwrap());
    format!(
        "{}{}",
        regex.replace_all(text, "N"),
        if field { " R" } else { "" }
    )
}
fn image(root: &Path, version: &str) -> Result<Vec<u8>> {
    let config: Value = serde_json::from_slice(&fs::read(root.join(format!(
        "config/{}/config.json",
        investigate::folder(version)
    )))?)?;
    Ok(fs::read(
        root.join(
            config["original"]
                .as_str()
                .context("config lacks original")?,
        ),
    )?)
}
fn symbol_text(root: &Path, version: &str) -> Result<String> {
    let path = root.join(format!(
        "config/{}/symbols.txt",
        investigate::folder(version)
    ));
    fs::read_to_string(&path).with_context(|| format!("read {}", path.display()))
}
pub fn symbols_of(text: &str) -> Result<BTreeMap<u32, (String, u32)>> {
    let regex = Regex::new(r"^(\S+) = \.(\w+):0x([0-9A-Fa-f]+);(?:.*size:0x([0-9A-Fa-f]+))?")?;
    let mut symbols = BTreeMap::new();
    for line in text.lines() {
        if let Some(c) = regex.captures(line) {
            symbols.insert(
                executable::hex(&c[3])?,
                (
                    c[1].into(),
                    c.get(4).map_or(Ok(0), |m| executable::hex(m.as_str()))?,
                ),
            );
        }
    }
    Ok(symbols)
}
pub fn disasm(root: &Path, version: &str, address: u32, size: Option<usize>) -> Result<String> {
    let data = image(root, version)?;
    let pe = Pe::parse(&data)?;
    let symbols = symbols_of(&symbol_text(root, version)?)?;
    let size = size.unwrap_or_else(|| {
        symbols
            .get(&address)
            .filter(|s| s.1 != 0)
            .map_or(0x100, |s| s.1 as usize)
    });
    let regex = Regex::new(r"0x[0-9a-f]{5,8}")?;
    let mut output = String::new();
    let decoder = decoder()?;
    for instruction in decode(
        &decoder,
        pe.read(address, size)?,
        u64::from(address),
        &BTreeSet::new(),
    )? {
        let annotated = regex.replace_all(&instruction.text, |c: &regex::Captures<'_>| {
            let value = u32::from_str_radix(&c[0][2..], 16).unwrap();
            let label = symbols
                .range(..=value)
                .next_back()
                .filter(|(start, (_, size))| {
                    value - **start <= *size + if *size == 0 { 0x10 } else { 0 }
                });
            if let Some((start, (name, _))) = label.filter(|_| value >= 0x400000) {
                let offset = value - start;
                format!(
                    "{} <{name}{}>",
                    &c[0],
                    if offset == 0 {
                        String::new()
                    } else {
                        format!("+{offset:#x}")
                    }
                )
            } else {
                c[0].to_owned()
            }
        });
        output.push_str(&format!("{:08x}  {annotated}\n", instruction.address));
    }
    Ok(output)
}
fn ratio<T: Ord>(a: &[T], b: &[T]) -> f64 {
    if a.is_empty() && b.is_empty() {
        return 1.0;
    }
    2.0 * sequence::matching_blocks_with_autojunk(a, b, false)
        .iter()
        .map(|(_, _, n)| n)
        .sum::<usize>() as f64
        / (a.len() + b.len()) as f64
}
pub fn fdiff(root: &Path, version: &str, source: &str, name: &str, address: u32) -> Result<String> {
    let data = image(root, version)?;
    let pe = Pe::parse(&data)?;
    let object = fs::read(root.join(format!(
        "build/{}/compiled/{source}.obj",
        investigate::folder(version)
    )))?;
    let function = investigate::functions_of(&Object::parse(&object)?)?
        .into_iter()
        .find(|f| f.name == name)
        .context("candidate function not emitted")?;
    let text = pe
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("image has no .text section")?;
    let end = text
        .virtual_address
        .checked_add(text.virtual_size)
        .context("text address overflow")?;
    ensure!(address <= end, "function address is beyond .text");
    let blob = pe.read(
        address,
        (function.code.len() * 2 + 64).min((end - address) as usize),
    )?;
    let decoder = decoder()?;
    let mut lines = vec![];
    let mut size = 0;
    for instruction in decode(&decoder, blob, 0, &BTreeSet::new())? {
        let next = instruction.address as usize + instruction.size;
        let stop = instruction.mnemonic == "ret" && next as i64 >= function.code.len() as i64 - 64;
        lines.push(instruction);
        if stop {
            size = next;
            let following = &blob[next..(next + 3).min(blob.len())];
            if following
                .first()
                .is_some_and(|b| [0x55, 0xcc, 0x90].contains(b))
                || following == [0, 0, 0]
            {
                break;
            }
        }
    }
    let mine = decode(&decoder, &function.code, 0, &function.wild)?;
    let a: Vec<_> = mine
        .iter()
        .map(|i| {
            let value = shape(&i.text, i.field);
            value.strip_suffix(" R").unwrap_or(&value).to_owned()
        })
        .collect();
    let b: Vec<_> = lines.iter().map(|i| shape(&i.text, false)).collect();
    let mut output = format!(
        "{name}: {} instructions, original at {address:#x}: {} ({size} bytes); ratio {:.3}\n",
        mine.len(),
        lines.len(),
        ratio(&a, &b)
    );
    let mut i = 0;
    let mut j = 0;
    for (ai, bj, n) in sequence::matching_blocks_with_autojunk(&a, &b, false) {
        for line in &mine[i..ai] {
            output.push_str(&format!("  - {:4x}  {}\n", line.address, line.text));
        }
        for line in &lines[j..bj] {
            output.push_str(&format!("  + {:4x}  {}\n", line.address, line.text));
        }
        for k in 0..n {
            let line = &mine[ai + k];
            let mark = if line.text == lines[bj + k].text {
                "="
            } else {
                "~"
            };
            output.push_str(&format!("  {mark} {:4x}  {}\n", line.address, line.text));
        }
        i = ai + n;
        j = bj + n;
    }
    Ok(output)
}
struct Candidate {
    name: String,
    address: u32,
    size: usize,
    mnemonics: Vec<String>,
}
fn quick_ratio(a: &[String], b: &[String]) -> f64 {
    let mut available = BTreeMap::new();
    for name in b {
        *available.entry(name.as_str()).or_insert(0usize) += 1;
    }
    let mut matches = 0;
    for name in a {
        if let Some(n) = available.get_mut(name.as_str())
            && *n > 0
        {
            matches += 1;
            *n -= 1;
        }
    }
    if a.is_empty() && b.is_empty() {
        1.0
    } else {
        2.0 * matches as f64 / (a.len() + b.len()) as f64
    }
}
pub fn nearest(root: &Path, version: &str, sources: &[String]) -> Result<String> {
    let data = image(root, version)?;
    let pe = Pe::parse(&data)?;
    let text = pe
        .sections
        .iter()
        .find(|s| s.name == ".text")
        .context("image has no .text section")?;
    let blob = pe.read(text.virtual_address, text.virtual_size as usize)?;
    let decoder = decoder()?;
    let mnemonics = |code: &[u8]| -> Result<Vec<String>> {
        Ok(decode(&decoder, code, 0, &BTreeSet::new())?
            .into_iter()
            .map(|i| i.mnemonic)
            .collect())
    };
    let regex =
        Regex::new(r"^(\S+) = \.text:0x([0-9A-Fa-f]+);.*type:function size:0x([0-9A-Fa-f]+)")?;
    let mut candidates = vec![];
    for line in symbol_text(root, version)?.lines() {
        if let Some(c) = regex.captures(line) {
            let address = executable::hex(&c[2])?;
            let size = executable::hex(&c[3])? as usize;
            // Preserve Python's bounded text slice; a symbol can extend into padding.
            let begin = address
                .checked_sub(text.virtual_address)
                .context("candidate starts before .text")? as usize;
            let code = blob
                .get(begin..(begin + size).min(blob.len()))
                .unwrap_or_default();
            candidates.push(Candidate {
                name: c[1].into(),
                address,
                size,
                mnemonics: mnemonics(code)?,
            });
        }
    }
    let mut output = String::new();
    for source in sources {
        output.push_str(&format!("{source}:\n"));
        let object = fs::read(root.join(format!(
            "build/{}/compiled/{source}.obj",
            investigate::folder(version)
        )))?;
        for function in investigate::functions_of(&Object::parse(&object)?)? {
            if !investigate::find(blob, &function.code, &function.wild, 4).is_empty() {
                continue;
            }
            let mine = mnemonics(&function.code)?;
            let mut scored = vec![];
            for candidate in &candidates {
                if candidate.size == 0
                    || candidate.size > 3 * function.code.len() + 64
                    || 3 * candidate.size + 64 < function.code.len()
                {
                    continue;
                }
                let theirs = &candidate.mnemonics;
                let total = mine.len() + theirs.len();
                let real_quick = if total == 0 {
                    1.0
                } else {
                    2.0 * mine.len().min(theirs.len()) as f64 / total as f64
                };
                if real_quick < 0.5 || quick_ratio(&mine, theirs) < 0.5 {
                    continue;
                }
                scored.push((ratio(&mine, theirs), candidate));
            }
            scored.sort_by(|a, b| {
                b.0.total_cmp(&a.0)
                    .then_with(|| b.1.name.cmp(&a.1.name))
                    .then_with(|| b.1.address.cmp(&a.1.address))
                    .then_with(|| b.1.size.cmp(&a.1.size))
            });
            let best = scored
                .iter()
                .take(3)
                .map(|(r, c)| format!("{r:.2} {}@{:#x}/{}", c.name, c.address, c.size))
                .collect::<Vec<_>>()
                .join(", ");
            output.push_str(&format!(
                "  {:44} {:5}  {best}\n",
                function.name,
                function.code.len()
            ));
        }
    }
    Ok(output)
}
