//! Bounds-checked readers of the binary formats (PE, COFF, ELF).
use anyhow::{Context, Result};

/// The LEN bytes of DATA at OFFSET.
pub(crate) fn bytes(data: &[u8], offset: usize, len: usize) -> Result<&[u8]> {
    let end = offset.checked_add(len).context("binary range overflow")?;
    data.get(offset..end).context("binary range outside file")
}
pub(crate) fn u16le(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(bytes(data, offset, 2)?.try_into()?))
}
pub(crate) fn u32le(data: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(bytes(data, offset, 4)?.try_into()?))
}
pub(crate) fn u16be(data: &[u8], offset: usize) -> Result<u16> {
    Ok(u16::from_be_bytes(bytes(data, offset, 2)?.try_into()?))
}
pub(crate) fn u32be(data: &[u8], offset: usize) -> Result<u32> {
    Ok(u32::from_be_bytes(bytes(data, offset, 4)?.try_into()?))
}
/// The NUL-terminated Latin-1 string at OFFSET of a FORMAT (`ELF`, `COFF`) string table.
pub(crate) fn latin1_string(data: &[u8], offset: usize, format: &str) -> Result<String> {
    let tail = data
        .get(offset..)
        .with_context(|| format!("{format} string offset outside table"))?;
    let end = tail
        .iter()
        .position(|b| *b == 0)
        .with_context(|| format!("unterminated {format} string"))?;
    Ok(tail[..end].iter().map(|b| char::from(*b)).collect())
}
