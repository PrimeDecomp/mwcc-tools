//! Checked legacy PE32 address mapping. Keep host x86 images distinct from PPC ELF.
use anyhow::{Context, Result, bail, ensure};
use serde::Serialize;
mod resources;
pub use resources::{Resource, ResourceName, res_file, resources};
// (the readers' old home)
pub(crate) use crate::bytes::{bytes, u16le, u32le};
#[derive(Clone, Debug, Serialize)]
pub struct Section {
    pub name: String,
    pub virtual_address: u32,
    pub virtual_size: u32,
    pub file_offset: u32,
    pub file_size: u32,
    pub characteristics: u32,
}
#[derive(Clone, Debug)]
pub struct Pe<'a> {
    pub data: &'a [u8],
    pub image_base: u32,
    pub sections: Vec<Section>,
}
impl<'a> Pe<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self> {
        ensure!(data.starts_with(b"MZ"), "not a PE executable (missing MZ)");
        let signature = u32le(data, 0x3c)? as usize;
        ensure!(
            bytes(data, signature, 4)? == b"PE\0\0",
            "not a PE executable"
        );
        let coff = signature + 4;
        ensure!(u16le(data, coff)? == 0x14c, "expected i386 PE32 image");
        let count = u16le(data, coff + 2)? as usize;
        let optional = u16le(data, coff + 16)? as usize;
        ensure!(optional >= 32, "truncated PE optional header");
        ensure!(
            u16le(data, coff + 20)? == 0x10b,
            "expected PE32 optional header"
        );
        bytes(data, coff + 20, optional)?;
        let image_base = u32le(data, coff + 48)?;
        let table = coff + 20 + optional;
        bytes(
            data,
            table,
            count.checked_mul(40).context("PE section count overflow")?,
        )?;
        let mut sections = Vec::with_capacity(count);
        for i in 0..count {
            let at = table + i * 40;
            let raw = bytes(data, at, 8)?;
            let end = raw.iter().position(|b| *b == 0).unwrap_or(8);
            ensure!(raw[..end].is_ascii(), "PE section name is not ASCII");
            let file_offset = u32le(data, at + 20)?;
            let file_size = u32le(data, at + 16)?;
            bytes(data, file_offset as usize, file_size as usize)?;
            sections.push(Section {
                name: String::from_utf8(raw[..end].to_vec())?,
                virtual_address: image_base
                    .checked_add(u32le(data, at + 12)?)
                    .context("PE address overflow")?,
                virtual_size: u32le(data, at + 8)?,
                file_offset,
                file_size,
                characteristics: u32le(data, at + 36)?,
            });
        }
        Ok(Self {
            data,
            image_base,
            sections,
        })
    }
    pub fn section_for_address(&self, address: u32) -> Result<&Section> {
        self.sections
            .iter()
            .find(|s| {
                u64::from(s.virtual_address) <= u64::from(address)
                    && u64::from(address)
                        < u64::from(s.virtual_address) + u64::from(s.virtual_size.max(s.file_size))
            })
            .with_context(|| format!("address 0x{address:x} is not in a section"))
    }
    pub fn address_to_offset(&self, address: u32) -> Result<usize> {
        let section = self.section_for_address(address)?;
        let delta = address - section.virtual_address;
        ensure!(
            delta < section.file_size,
            "address 0x{address:x} has no file-backed data"
        );
        Ok(section.file_offset as usize + delta as usize)
    }
    pub fn offset_to_address(&self, offset: usize) -> Result<u32> {
        for section in &self.sections {
            if (section.file_offset as usize
                ..section.file_offset as usize + section.file_size as usize)
                .contains(&offset)
            {
                return section
                    .virtual_address
                    .checked_add((offset - section.file_offset as usize) as u32)
                    .context("PE address overflow");
            }
        }
        bail!("file offset 0x{offset:x} is not in a section")
    }
    pub fn read(&self, address: u32, size: usize) -> Result<&'a [u8]> {
        bytes(self.data, self.address_to_offset(address)?, size)
    }
    pub fn contains(&self, address: u32, needle: &[u8]) -> bool {
        self.read(address, needle.len())
            .is_ok_and(|data| data == needle)
    }
    /// The addresses of NEEDLE in the initialized sections, up to LIMIT of them (as Python's search: an empty NEEDLE
    /// is at every offset, which tests/binary.rs's fixture pins).
    pub fn find(&self, needle: &[u8], limit: Option<usize>) -> Vec<u32> {
        let mut out = vec![];
        for section in &self.sections {
            if section.characteristics & 0x20000000 != 0 {
                continue;
            }
            let data = &self.data[section.file_offset as usize
                ..section.file_offset as usize + section.file_size as usize];
            if needle.len() > data.len() {
                continue;
            }
            for offset in 0..=data.len() - needle.len() {
                if limit.is_some_and(|limit| out.len() >= limit) {
                    return out;
                }
                if data[offset..].starts_with(needle)
                    && let Some(address) = section.virtual_address.checked_add(offset as u32)
                {
                    out.push(address);
                }
            }
        }
        out
    }
}
