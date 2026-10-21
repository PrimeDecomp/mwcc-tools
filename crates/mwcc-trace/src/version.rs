use anyhow::{Result, bail};
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::sync::LazyLock;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Version {
    #[serde(rename = "GC_1_0")]
    Gc10,
    #[serde(rename = "GC_1_1")]
    Gc11,
    #[serde(rename = "GC_1_1p1")]
    Gc11p1,
    #[serde(rename = "GC_1_2_5")]
    Gc125,
    #[serde(rename = "GC_1_2_5n")]
    Gc125n,
    #[serde(rename = "GC_1_3")]
    Gc13,
    #[serde(rename = "GC_1_3_2")]
    Gc132,
    #[serde(rename = "GC_2_6")]
    Gc26,
    #[serde(rename = "GC_2_7")]
    Gc27,
}

impl Version {
    pub fn parse(text: &str) -> Result<Self> {
        match text.replace('\\', "/").as_str() {
            "GC_1_0" | "GC/1.0" => Ok(Self::Gc10),
            "GC_1_1" | "GC/1.1" => Ok(Self::Gc11),
            "GC_1_1p1" | "GC/1.1p1" => Ok(Self::Gc11p1),
            "GC_1_2_5" | "GC/1.2.5" => Ok(Self::Gc125),
            "GC_1_2_5n" | "GC/1.2.5n" => Ok(Self::Gc125n),
            "GC_1_3" | "GC/1.3" => Ok(Self::Gc13),
            "GC_1_3_2" | "GC/1.3.2" => Ok(Self::Gc132),
            "GC_2_6" | "GC/2.6" => Ok(Self::Gc26),
            "GC_2_7" | "GC/2.7" => Ok(Self::Gc27),
            _ => bail!("unsupported oracle compiler version {text}"),
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Gc10 => "GC_1_0",
            Self::Gc11 => "GC_1_1",
            Self::Gc11p1 => "GC_1_1p1",
            Self::Gc125 => "GC_1_2_5",
            Self::Gc125n => "GC_1_2_5n",
            Self::Gc13 => "GC_1_3",
            Self::Gc132 => "GC_1_3_2",
            Self::Gc26 => "GC_2_6",
            Self::Gc27 => "GC_2_7",
        }
    }

    pub fn compiler_directory(self) -> &'static str {
        match self {
            Self::Gc10 => "1.0",
            Self::Gc11 => "1.1",
            Self::Gc11p1 => "1.1p1",
            Self::Gc125 => "1.2.5",
            Self::Gc125n => "1.2.5n",
            Self::Gc13 => "1.3",
            Self::Gc132 => "1.3.2",
            Self::Gc26 => "2.6",
            Self::Gc27 => "2.7",
        }
    }

    pub fn has_oracle(self) -> bool {
        matches!(self, Self::Gc13 | Self::Gc132 | Self::Gc26 | Self::Gc27)
    }
}

pub fn class_name(class: u8) -> String {
    match class_str(class) {
        Some(name) => name.into(),
        None => format!("class{class}"),
    }
}

/// The name of a known register class (None for one [`class_name`] calls `class{N}`).
pub fn class_str(class: u8) -> Option<&'static str> {
    Some(match class {
        4 => "GPR",
        3 => "FPR",
        2 => "VR",
        1 => "CR",
        0 => "SPR",
        _ => return None,
    })
}

/// Whether register class CLASS is named NAME (`class_name(class) == name`, without allocating).
pub fn is_class(class: u8, name: &str) -> bool {
    match class_str(class) {
        Some(known) => known == name,
        None => class_name(class) == name,
    }
}

// Embed the exact versioned source table: installed tools do not require headers
// to be present at their current working directory.
pub fn opcode_names(version: Version) -> &'static BTreeMap<i32, String> {
    fn names(newer: bool) -> BTreeMap<i32, String> {
        let simple = Regex::new(r"^#define PC_(\w+) (\d+)").unwrap();
        let versioned = Regex::new(r"^#define PC_(\w+) BY_VERSION\((\d+), (\d+)\)").unwrap();
        let mut names = BTreeMap::new();
        for line in include_str!("../../../include/compiler/PCode.h").lines() {
            if let Some(c) = simple.captures(line) {
                names
                    .entry(c[2].parse().unwrap())
                    .or_insert_with(|| c[1].to_lowercase());
            } else if let Some(c) = versioned.captures(line) {
                names.insert(
                    c[if newer { 3 } else { 2 }].parse().unwrap(),
                    c[1].to_lowercase(),
                );
            }
        }
        names
    }
    static NEW: LazyLock<BTreeMap<i32, String>> = LazyLock::new(|| names(true));
    static OLD: LazyLock<BTreeMap<i32, String>> = LazyLock::new(|| names(false));
    if version.has_oracle() { &NEW } else { &OLD }
}

pub fn function_at(version: Version, address: u32) -> Option<&'static str> {
    type Function = (u32, u32, String);
    fn table(text: &str) -> Vec<Function> {
        let pattern = Regex::new(
            r"^_?(\S+) = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)",
        )
        .unwrap();
        let mut functions: Vec<_> = text
            .lines()
            .filter_map(|line| pattern.captures(line))
            .map(|c| {
                (
                    u32::from_str_radix(&c[2], 16).unwrap(),
                    u32::from_str_radix(&c[3], 16).unwrap(),
                    c[1].to_owned(),
                )
            })
            .collect();
        functions.sort();
        functions
    }
    static GC13: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_1_3/symbols.txt")));
    static GC132: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_1_3_2/symbols.txt")));
    static GC26: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_2_6/symbols.txt")));
    static GC27: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_2_7/symbols.txt")));
    let functions = &match version {
        Version::Gc13 => &*GC13,
        Version::Gc132 => &*GC132,
        Version::Gc26 => &*GC26,
        Version::Gc27 => &*GC27,
        _ => return None,
    };
    containing(functions, address)
}

fn containing(functions: &[(u32, u32, String)], address: u32) -> Option<&str> {
    let i = functions
        .partition_point(|(start, _, _)| *start <= address)
        .checked_sub(1)?;
    let (start, size, name) = &functions[i];
    (u64::from(address) < u64::from(*start) + u64::from(*size)).then_some(name.as_str())
}

/// The function containing `address` as a reader would name it: `function_at`'s symbol, or for an unnamed
/// (`fn_`) one, the name `config/<version>/names.txt` gives the function there followed by the symbol, which
/// hooks and the matches on `function_at` keep using.
pub fn function_name(version: Version, address: u32) -> Option<String> {
    type Function = (u32, u32, String);
    fn table(text: &str) -> Vec<Function> {
        let mut functions: Vec<_> = text
            .lines()
            .filter(|line| !line.starts_with('#'))
            .filter_map(|line| {
                let mut fields = line.split_whitespace();
                let start = u32::from_str_radix(fields.next()?.strip_prefix("0x")?, 16).ok()?;
                let size = u32::from_str_radix(fields.next()?.strip_prefix("0x")?, 16).ok()?;
                Some((start, size, fields.next()?.to_owned()))
            })
            .collect();
        functions.sort();
        functions
    }
    static GC13: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_1_3/names.txt")));
    static GC132: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_1_3_2/names.txt")));
    static GC26: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_2_6/names.txt")));
    static GC27: LazyLock<Vec<Function>> =
        LazyLock::new(|| table(include_str!("../../../config/GC_2_7/names.txt")));
    let symbol = function_at(version, address)?;
    let names = match version {
        Version::Gc13 => &*GC13,
        Version::Gc132 => &*GC132,
        Version::Gc26 => &*GC26,
        Version::Gc27 => &*GC27,
        _ => return Some(symbol.into()),
    };
    Some(match containing(names, address) {
        Some(name) if symbol.starts_with("fn_") => format!("{name} ({symbol})"),
        _ => symbol.into(),
    })
}

pub fn enode_names() -> &'static BTreeMap<i64, String> {
    static NAMES: LazyLock<BTreeMap<i64, String>> = LazyLock::new(|| {
        let text = include_str!("../../../include/compiler/enode.h");
        let begin = text.find("typedef enum ENodeType {").unwrap();
        let end = text.find("} ENodeType;").unwrap();
        let comments = Regex::new(r"/\*.*?\*/").unwrap();
        let mut names = vec![];
        for line in text[begin..end].lines().skip(1) {
            let line = comments.replace_all(line, "");
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            names.extend(
                line.split(',')
                    .map(str::trim)
                    .filter(|s| !s.is_empty())
                    .map(str::to_owned),
            );
        }
        names
            .into_iter()
            .enumerate()
            .map(|(n, name)| (n as i64, name))
            .collect()
    });
    &NAMES
}
