use super::*;
use crate::{
    coff::Object,
    fsutil::files,
    pe::{Pe, u32le},
    x86,
};
use regex::{Regex, bytes::Regex as BytesRegex};
pub struct Image<'a> {
    pub pe: Pe<'a>,
    pub fixups: Vec<u32>,
    pub functions: Vec<Value>,
    pub bindings: IndexMap<String, u32>,
    pub imports: Vec<Import>,
    pub definitions: IndexMap<String, String>,
    pub decorated: IndexMap<String, String>,
    pub deltas: IndexMap<String, i64>,
}
#[derive(Clone, Debug, Serialize)]
pub struct Import {
    pub slot: u32,
    pub symbol: String,
    pub dll: String,
}
pub fn section_end(section: &crate::pe::Section) -> Result<u32> {
    section
        .virtual_address
        .checked_add(section.virtual_size)
        .context("section address overflow")
}
pub fn compiled_symbols(
    root: &Path,
    version: &str,
) -> Result<(IndexMap<String, String>, IndexMap<String, String>)> {
    let compiled = root.join(format!("build/{}/compiled", investigate::folder(version)));
    let mut definitions = IndexMap::new();
    let mut names = IndexMap::new();
    let mut definition_clash = BTreeSet::new();
    let mut name_clash = BTreeSet::new();
    let undecorate = Regex::new(r"@\d+$")?;
    for path in files(&compiled, true)?
        .into_iter()
        .filter(|p| p.extension().is_some_and(|e| e == "obj"))
    {
        let source = path
            .strip_prefix(&compiled)?
            .to_string_lossy()
            .replace('\\', "/");
        let source = source.strip_suffix(".obj").unwrap();
        let data = fs::read(&path)?;
        let object = Object::parse(&data)?;
        let code: BTreeSet<_> = object
            .sections
            .iter()
            .enumerate()
            .filter(|(_, s)| s.name.starts_with(".text") && s.flags & 0x20 != 0)
            .map(|(i, _)| i as i16 + 1)
            .collect();
        for symbol in object.symbols.values() {
            let name = &symbol.name;
            if code.contains(&symbol.section)
                && [2, 3].contains(&symbol.storage)
                && symbol.kind & 0xf0 == 0x20
            {
                let plain = if name.starts_with('?') {
                    name.clone()
                } else {
                    let plain = undecorate.replace(name, "");
                    plain.strip_prefix('_').unwrap_or(&plain).to_owned()
                };
                if definitions
                    .entry(plain.clone())
                    .or_insert_with(|| source.to_owned())
                    != source
                {
                    definition_clash.insert(plain);
                }
            }
            if name.is_empty() || [".", "@", "$", "?"].iter().any(|p| name.starts_with(p)) {
                continue;
            }
            let plain = undecorate.replace(name, "");
            let plain = plain.strip_prefix('_').unwrap_or(&plain).to_owned();
            if names.entry(plain.clone()).or_insert_with(|| name.clone()) != name {
                name_clash.insert(plain);
            }
        }
    }
    definitions.retain(|k, _| !definition_clash.contains(k));
    names.retain(|k, _| !name_clash.contains(k));
    Ok((definitions, names))
}
pub fn import_table(root: &Path, pe: &Pe<'_>) -> Result<Vec<Import>> {
    let directory = u32le(pe.data, u32le(pe.data, 0x3c)? as usize + 24 + 96 + 8)?;
    if directory == 0 {
        return Ok(vec![]);
    }
    let mut decorated = IndexMap::new();
    let regex = BytesRegex::new(r"__imp__([A-Za-z0-9_]+)@(\d+)")?;
    for path in files(&root.join("lib/win32sdk"), false)?
        .into_iter()
        .filter(|p| p.extension().is_some_and(|e| e == "LIB"))
    {
        for c in regex.captures_iter(&fs::read(path)?) {
            let name = std::str::from_utf8(&c[1])?;
            decorated.insert(
                name.to_owned(),
                format!("{name}@{}", std::str::from_utf8(&c[2])?),
            );
        }
    }
    let regex = Regex::new(r"(?m)^\s+(\w+)@(\d+)\s+@")?;
    for path in files(&root.join("config"), false)?
        .into_iter()
        .filter(|p| p.extension().is_some_and(|e| e == "def"))
    {
        for c in regex.captures_iter(&fs::read_to_string(path)?) {
            decorated.insert(c[1].to_owned(), format!("{}@{}", &c[1], &c[2]));
        }
    }
    let regex = Regex::new(r"^\s*(\w+)\s*@(\d+)\s+NONAME")?;
    let mut ordinals = IndexMap::new();
    for line in fs::read_to_string(root.join("config/lmgr326b.def"))?.lines() {
        if let Some(c) = regex.captures(line) {
            ordinals.insert(c[2].parse::<u32>()?, c[1].to_owned());
        }
    }
    let mut out = vec![];
    let mut at = pe
        .image_base
        .checked_add(directory)
        .context("import directory address overflow")?;
    loop {
        let descriptor = pe.read(at, 20)?;
        let original = u32le(descriptor, 0)?;
        let name = u32le(descriptor, 12)?;
        let first = u32le(descriptor, 16)?;
        if name == 0 {
            break;
        }
        let table = pe
            .image_base
            .checked_add(if first != 0 { first } else { original })
            .context("import table address overflow")?;
        let dll = pe.read(
            pe.image_base
                .checked_add(name)
                .context("import name overflow")?,
            32,
        )?;
        let dll = std::str::from_utf8(dll.split(|b| *b == 0).next().unwrap())?.to_owned();
        let mut slot = table;
        loop {
            let value = u32le(pe.read(slot, 4)?, 0)?;
            if value == 0 {
                break;
            }
            let symbol = if value >> 31 != 0 {
                format!(
                    "__imp__{}",
                    ordinals
                        .get(&(value & 0xffff))
                        .context("unknown ordinal import")?
                )
            } else {
                let name = pe.read(
                    pe.image_base
                        .checked_add(value)
                        .and_then(|a| a.checked_add(2))
                        .context("import hint/name overflow")?,
                    64,
                )?;
                let name = std::str::from_utf8(name.split(|b| *b == 0).next().unwrap())?;
                format!(
                    "__imp__{}",
                    decorated.get(name).map_or(name, String::as_str)
                )
            };
            out.push(Import {
                slot,
                symbol,
                dll: dll.clone(),
            });
            slot = slot
                .checked_add(4)
                .context("import slot address overflow")?;
        }
        at = at
            .checked_add(20)
            .context("import descriptor address overflow")?;
    }
    Ok(out)
}
impl<'a> Image<'a> {
    pub fn new(root: &Path, version: &str, data: &'a [u8]) -> Result<Self> {
        let pe = Pe::parse(data)?;
        let fixups = executable::base_relocations(&pe)?;
        let directory = root.join("config").join(investigate::folder(version));
        let mut functions: Vec<Value> =
            serde_json::from_slice(&fs::read(directory.join("functions.json"))?)?;
        for f in &mut functions {
            ensure!(f.is_object(), "function metadata is not an object");
            let _: u32 = f["size"]
                .as_u64()
                .context("function lacks a nonnegative size")?
                .try_into()?;
            f["start"] = json!(executable::hex(
                f["address"]
                    .as_str()
                    .context("function lacks hexadecimal address")?
            )?);
        }
        functions.sort_by_key(start);
        let bindings: IndexMap<String, Value> =
            serde_json::from_slice(&fs::read(directory.join("bindings.json"))?)?;
        let bindings = bindings
            .into_iter()
            .map(|(n, v)| {
                Ok((
                    n,
                    executable::hex(v.as_str().context("binding lacks hexadecimal address")?)?,
                ))
            })
            .collect::<Result<_>>()?;
        let imports = import_table(root, &pe)?;
        let (definitions, decorated) = compiled_symbols(root, version)?;
        let mut image = Self {
            pe,
            fixups,
            functions,
            bindings,
            imports,
            definitions,
            decorated,
            deltas: IndexMap::new(),
        };
        image.add_missing()?;
        Ok(image)
    }
    pub fn section(&self, name: &str) -> Result<&crate::pe::Section> {
        self.pe
            .sections
            .iter()
            .find(|s| s.name == name)
            .with_context(|| format!("image has no {name} section"))
    }
    pub fn operand(&self, address: u32) -> bool {
        let k = self.fixups.partition_point(|a| *a <= address);
        k > 0 && u64::from(address) < u64::from(self.fixups[k - 1]) + 4
    }
    pub fn thunk(&self, address: u32) -> Result<Option<u32>> {
        let code = self.pe.read(address, 6)?;
        (code[..2] == [0xff, 0x25])
            .then(|| u32le(code, 2))
            .transpose()
    }
    fn add_missing(&mut self) -> Result<()> {
        let mut extra = vec![];
        for k in 0..self.functions.len() {
            let mut end = start(&self.functions[k])
                .checked_add(size(&self.functions[k]))
                .context("function end overflow")?;
            let Some(following) = self.functions.get(k + 1).map(start).filter(|a| *a > end) else {
                continue;
            };
            let data = self.pe.read(end, (following - end) as usize)?;
            if data.iter().all(|b| [0, 0xcc].contains(b)) {
                continue;
            }
            let mut aligned = (end + 15) & !15;
            if end != aligned && ![0, 0xcc].contains(&data[0]) {
                let run = data
                    .iter()
                    .enumerate()
                    .position(|(i, b)| [0, 0xcc].contains(b) && !self.operand(end + i as u32))
                    .unwrap_or(data.len());
                self.functions[k]["size"] = json!(size(&self.functions[k]) + run as u32);
                end += run as u32;
                aligned = aligned.max((end + 15) & !15);
            }
            let mut at = aligned.max(end);
            while at < following {
                let chunk = self.pe.read(at, (following - at) as usize)?;
                let Some(first) = chunk.iter().position(|b| ![0, 0xcc].contains(b)) else {
                    break;
                };
                at += first as u32;
                let chunk = &chunk[first..];
                if chunk.starts_with(&[0xff, 0x25]) && chunk.len() >= 6 {
                    let slot = u32le(chunk, 2)?;
                    let name = self
                        .imports
                        .iter()
                        .find(|i| i.slot == slot)
                        .map_or("__imp__unknown", |i| i.symbol.as_str())
                        .replacen("__imp__", "_", 1);
                    extra.push(json!({"name":name,"address":format!("0x{at:08x}"),"size":6,"start":at,"thunk":true}));
                    at += 6;
                    continue;
                }
                let last = chunk
                    .iter()
                    .enumerate()
                    .rposition(|(i, b)| ![0, 0xcc].contains(b) || self.operand(at + i as u32))
                    .context("missing non-padding code")?;
                extra.push(json!({"name":format!("fn_{at:08x}"),"address":format!("0x{at:08x}"),"size":last+1,"start":at}));
                break;
            }
        }
        self.functions.extend(extra);
        self.functions.sort_by_key(start);
        Ok(())
    }
    pub fn references(&self, owners: &IndexMap<u32, usize>) -> Result<(References, BTreeSet<u32>)> {
        let mut refs: IndexMap<u32, BTreeSet<usize>> = IndexMap::new();
        for f in &self.functions {
            let address = start(f);
            let end = address
                .checked_add(size(f))
                .context("function end overflow")?;
            let unit = owners[&address];
            for at in self
                .fixups
                .iter()
                .copied()
                .filter(|a| *a >= address && *a < end)
            {
                let value = u32le(self.pe.read(at, 4)?, 0)?;
                refs.entry(value).or_default().insert(unit);
            }
            if let Ok(branches) = x86::branches(self.pe.read(address, size(f) as usize)?, address) {
                for (_, target) in branches {
                    if !(address..end).contains(&target) {
                        refs.entry(target).or_default().insert(unit);
                    }
                }
            }
        }
        let everywhere = self
            .fixups
            .iter()
            .filter_map(|at| self.pe.read(*at, 4).ok().and_then(|b| u32le(b, 0).ok()))
            .collect();
        Ok((refs, everywhere))
    }
}
