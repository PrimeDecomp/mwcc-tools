use super::*;
use crate::pe::{bytes, u32le};
use std::{
    collections::BTreeMap,
    io::{self, Read, Seek, SeekFrom},
};
pub struct Disc<'a, F: Fetch> {
    fetcher: &'a mut F,
    url: String,
    sector: usize,
    skip: usize,
    blocks: BTreeMap<u64, Vec<u8>>,
}
impl<'a, F: Fetch> Disc<'a, F> {
    pub fn new(fetcher: &'a mut F, url: &str, raw: bool) -> Self {
        Self {
            fetcher,
            url: url.into(),
            sector: if raw { 2352 } else { 2048 },
            skip: if raw { 24 } else { 0 },
            blocks: BTreeMap::new(),
        }
    }
    fn block(&mut self, index: u64) -> Result<&[u8]> {
        if !self.blocks.contains_key(&index) {
            let first = index
                .checked_mul(128 * self.sector as u64)
                .context("disc range offset overflow")?;
            let end = first
                .checked_add(128 * self.sector as u64 - 1)
                .context("disc range end overflow")?;
            let data = self.fetcher.fetch(&self.url, Some((first, end)))?;
            ensure!(
                data.len().is_multiple_of(self.sector),
                "truncated disc sector"
            );
            let mut block = vec![];
            for sector in data.chunks_exact(self.sector) {
                block.extend_from_slice(&sector[self.skip..self.skip + 2048]);
            }
            self.blocks.insert(index, block);
        }
        Ok(&self.blocks[&index])
    }
    pub fn read(&mut self, mut lba: u64, size: usize) -> Result<Vec<u8>> {
        ensure!(size as u64 <= MAX_BYTES, "disc read is too large");
        let mut data = Vec::with_capacity(size);
        while data.len() < size {
            let index = lba / 128;
            let offset = (lba % 128) as usize;
            let block = self.block(index)?;
            let chunk = block
                .get(offset * 2048..)
                .context("disc sector outside returned block")?;
            ensure!(!chunk.is_empty(), "truncated disc read");
            data.extend_from_slice(&chunk[..chunk.len().min(size - data.len())]);
            lba += 128 - offset as u64;
        }
        Ok(data)
    }
    pub fn file<'d>(&'d mut self, path: &str) -> Result<DiscFile<'d, 'a, F>> {
        let descriptor = self.read(16, 2048)?;
        ensure!(
            descriptor[0] == 1 && &descriptor[1..6] == b"CD001",
            "not an ISO9660 primary volume"
        );
        let mut record = bytes(&descriptor, 156, 34)?.to_vec();
        for part in path.split('/') {
            let directory =
                self.read(u64::from(u32le(&record, 2)?), u32le(&record, 10)? as usize)?;
            let mut cursor = 0;
            let mut found = None;
            while cursor < directory.len() {
                let size = directory[cursor] as usize;
                if size == 0 {
                    cursor = (cursor / 2048 + 1) * 2048;
                    continue;
                }
                let entry = bytes(&directory, cursor, size)?;
                ensure!(entry.len() >= 33, "truncated ISO directory record");
                let name = bytes(entry, 33, entry[32] as usize)?;
                if name == part.as_bytes() {
                    found = Some(entry.to_vec());
                    break;
                }
                cursor += size;
            }
            record = found.with_context(|| format!("{}: no {path}", self.url))?;
        }
        let lba = u64::from(u32le(&record, 2)?);
        let size = u64::from(u32le(&record, 10)?);
        Ok(DiscFile {
            disc: self,
            lba,
            size,
            position: 0,
        })
    }
}
pub struct DiscFile<'d, 'a, F: Fetch> {
    disc: &'d mut Disc<'a, F>,
    lba: u64,
    size: u64,
    position: u64,
}
impl<F: Fetch> Read for DiscFile<'_, '_, F> {
    fn read(&mut self, buffer: &mut [u8]) -> io::Result<usize> {
        let size = (buffer.len() as u64).min(self.size.saturating_sub(self.position)) as usize;
        if size == 0 {
            return Ok(0);
        }
        let skip = (self.position % 2048) as usize;
        let data = self
            .disc
            .read(self.lba + self.position / 2048, size + skip)
            .map_err(io::Error::other)?;
        buffer[..size].copy_from_slice(&data[skip..]);
        self.position += size as u64;
        Ok(size)
    }
}
impl<F: Fetch> Seek for DiscFile<'_, '_, F> {
    fn seek(&mut self, position: SeekFrom) -> io::Result<u64> {
        let next = match position {
            SeekFrom::Start(n) => i128::from(n),
            SeekFrom::Current(n) => i128::from(self.position) + i128::from(n),
            SeekFrom::End(n) => i128::from(self.size) + i128::from(n),
        };
        self.position = u64::try_from(next).map_err(io::Error::other)?;
        Ok(self.position)
    }
}
