use crate::elf::Elf;
use anyhow::Result;

// (the words of the traces' models: mwcc-trace)
pub use mwcc_trace::ppc::{instruction_token, r1_offset, target_words};

pub fn function_words(data: &[u8], link: &str) -> Result<Option<Vec<u32>>> {
    let elf = Elf::parse(data)?;
    for symbol in elf.symbols()? {
        if symbol.name == link && symbol.kind == 2 && symbol.section < elf.sections.len() {
            return Ok(Some(elf.function_words(&symbol)?));
        }
    }
    Ok(None)
}
