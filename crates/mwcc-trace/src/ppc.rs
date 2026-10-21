//! PowerPC words: an instruction's token for comparing (its displacements masked), a load or store's offset from r1,
//! and a function's words in a target listing (dtk's `.fn` assembly).
use regex::Regex;
use std::sync::LazyLock;

pub fn instruction_token(word: u32) -> u32 {
    let op = word >> 26;
    if matches!(op, 14 | 15 | 24 | 25 | 32..=57 | 60 | 61) {
        return word
            & !if matches!(op, 56 | 57 | 60 | 61) {
                0xfff
            } else {
                0xffff
            };
    }
    match op {
        18 => word & 0xfc000003,
        16 => word & 0xffff0003,
        _ => word,
    }
}
pub fn r1_offset(word: u32) -> Option<i32> {
    let op = word >> 26;
    if !matches!(op, 14 | 32..=57 | 60 | 61) || (word >> 16) & 31 != 1 {
        return None;
    }
    Some(if matches!(op, 56 | 57 | 60 | 61) {
        ((word & 0xfff) as i32) - (((word & 0x800) << 1) as i32)
    } else {
        i32::from(word as u16 as i16)
    })
}
/// The lines of function LINK's body in a target listing (dtk's assembly): those after its `.fn` up to its `.endfn`,
/// without the `.fn` and `.endfn` lines of any function.
pub(crate) fn fn_body<'a>(text: &'a str, link: &'a str) -> impl Iterator<Item = &'a str> + 'a {
    static MARKER: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r#"^\s*\.(fn|endfn)\s+("[^"]*"|[^\s,]+)"#).unwrap());
    let mut inside = false;
    text.lines()
        .map_while(move |line| {
            if let Some(c) = MARKER.captures(line) {
                if c[2].trim_matches('"') == link {
                    if &c[1] == "endfn" {
                        return None;
                    }
                    inside = true;
                }
                return Some(None);
            }
            Some(inside.then_some(line))
        })
        .flatten()
}

pub fn target_words(text: &str, link: &str) -> Option<Vec<u32>> {
    static WORD: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"/\*.*?\b((?:[0-9A-Fa-f]{2} ){3}[0-9A-Fa-f]{2})\s*\*/").unwrap()
    });
    let words: Vec<u32> = fn_body(text, link)
        .filter_map(|line| WORD.captures(line))
        .map(|c| u32::from_str_radix(&c[1].replace(' ', ""), 16).unwrap())
        .collect();
    (!words.is_empty()).then_some(words)
}
