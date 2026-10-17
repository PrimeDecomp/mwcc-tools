//! Fixed-size call islands execute the original counter write and then observe it.
//! Original sections never grow, so jump tables and existing code views stay valid.
use crate::{
    backend_audit::{self, Reference},
    backend_probe,
    coff::{Builder, Object, Relocation},
    disassembly,
    executable::{self, Original},
};
use anyhow::{Context, Result, bail, ensure};
use capstone::{
    RegId,
    arch::{
        DetailsArchInsn,
        x86::{X86OperandType, X86Reg},
    },
};
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
};

#[derive(Clone, Debug, Serialize)]
pub struct Site {
    pub address: u32,
    pub container: String,
    pub offset: u32,
    pub role: Role,
    pub selector: i32,
    pub bytes: Vec<u8>,
    pub field: Option<usize>,
    pub counter_class: Option<u8>,
    pub object_binding: ObjectBinding,
    pub operand_binding: OperandBinding,
    pub data_link_offset: u32,
    pub origin_binding: OriginBinding,
}
/// Read-only origins at actual counter writes. These identify guarded helper
/// mechanisms, not complete semantic lifetimes or arbitrary inline ancestry.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
#[repr(u32)]
#[serde(rename_all = "snake_case")]
pub enum OriginBinding {
    #[default]
    None = 0,
    ScalarArrayElement = 1,
    CallResultSingle = 2,
    CallResultFirst = 3,
    CallResultSecond = 4,
}
/// These roles are justified by the guarded retail Normalize path, not by a
/// register number, expression spelling or reconstructed function signature.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
#[repr(u32)]
#[serde(rename_all = "snake_case")]
pub enum OperandBinding {
    #[default]
    None = 0,
    SymbolAddressHigh = 1,
    SymbolAddressLow = 2,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
#[repr(u32)]
#[serde(rename_all = "snake_case")]
pub enum ObjectBinding {
    #[default]
    None = 0,
    Single = 1,
    PairFirst = 2,
    PairSecond = 3,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[repr(u32)]
#[serde(rename_all = "snake_case")]
pub enum Role {
    Increment = 1,
    RestoreHighWater = 2,
    Rewind = 3,
    InitialReset = 4,
    AllocationReset = 5,
    FunctionBegin = 6,
    FunctionEnd = 7,
    InitialRange = 8,
    WindowBegin = 9,
}
#[derive(Debug)]
pub struct Installed {
    pub objects: BTreeMap<String, Vec<u8>>,
    pub islands: Vec<u8>,
    pub sites: Vec<Site>,
}
const COUNTS: &str = "_data_005e6a7c";
const PHYSICAL: &str = "_data_005e6778";
const HANDLER: &str = "_OracleRust_CounterProbeV6";
// Roles: increment, close/high-water restore, rewind, initial reset, allocation
// reset, function begin, function end. These are actual writes, not guessed types.
fn role(version: &str, r: &Reference) -> Result<Role> {
    if r.instruction.starts_with("inc ") {
        return Ok(Role::Increment);
    }
    match r.function.as_str() {
        "_fn_004fca00" => Ok(Role::RestoreHighWater),
        "_fn_004fcc60" => Ok(Role::InitialReset),
        "_fn_00506cd0" => Ok(Role::AllocationReset),
        "_fn_004fca40" if version == "GC_1_3" => Ok(Role::Rewind),
        "_fn_004fd4f0" if version == "GC_1_3_2" => Ok(Role::Rewind),
        "_fn_004fe270" if ["GC_2_6", "GC_2_7"].contains(&version) => Ok(Role::Rewind),
        _ => bail!("unclassified retail counter write at {:#x}", r.address),
    }
}
pub fn plan(root: &Path, version: &str) -> Result<Vec<Site>> {
    let audit = backend_audit::capture(root, version)?;
    ensure!(
        audit.unresolved.is_empty()
            && audit
                .references
                .iter()
                .all(|r| r.access_known && !r.address_taken && r.function_decoded_completely),
        "counter audit has unresolved references or aliases"
    );
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    // The temp-object wrapper reads saved EBX only at the scalarizer's sole
    // audited call return. Guard both the consumer and the field constructor;
    // reconstructed IROVarPart declarations alone do not establish these bytes.
    for (name, size, expected) in [
        (
            "_fn_00460780",
            0x3a2,
            match version {
                "GC_1_3" => "88000852a003dd11fdb26a09aa0478f824455ed1085d95aaca22ca3c892be775",
                "GC_1_3_2" => "9be52856aced363fcf061bb65d04c374551da079d16f88bb35a4718d45cec2dc",
                "GC_2_6" => "661952cee2725f163136ce15fce88cd57992390f90cadeea7224709227a3e924",
                "GC_2_7" => "d5dc7b3e3f3af83612de6d9abbecc79e0007f709299e049bcc3d301fdab8481a",
                _ => unreachable!(),
            },
        ),
        (
            "_fn_004601f0",
            0x3fc,
            match version {
                "GC_1_3" => "50733c3c5312a6e3c44affbad1c4dd39ec3aef9d33bef0d0a38a4337622433f1",
                "GC_1_3_2" => "e08fcf3949adf20eb89dde10ccc0f05b671d1e97875642f412505aa9ba923483",
                "GC_2_6" => "2390155a13678b3dbcbaa6b7aee6b1ca4b2e186480cbac2e9dc17a2554cd663c",
                "GC_2_7" => "06de28c6947a8d5470191498dcff2b2cc29b955634759550edc67c6bde6afcfe",
                _ => unreachable!(),
            },
        ),
        (
            "_create_temp_object",
            0x76,
            match version {
                "GC_1_3" => "a2ee6b2cb813b5b24ff22f8764ae4ddaa4a787d7fa3a91f452e683838d0e79e1",
                "GC_1_3_2" => "cada9817ceac0a35abc55a941fec25e290316d57ea3010d000d574bdc8d36b33",
                "GC_2_6" => "30d603132533c3a708f3ad3da47d9e02ec595a331de0a9b9678648791e6b75b3",
                "GC_2_7" => "23d41eee3041387ab0cc7f97c6d8de2d628f029853b03293bebe59659f03aa29",
                _ => unreachable!(),
            },
        ),
    ] {
        let symbol = metadata
            .iter()
            .find(|s| s.name == name)
            .context("missing scalarization ABI symbol")?;
        ensure!(
            executable::size(&symbol.options)? == size
                && format!(
                    "{:x}",
                    Sha256::digest(pe.read(symbol.address, size as usize)?)
                ) == expected,
            "frontend scalarization layout guard failed for {name}"
        );
    }
    let mut decoder = disassembly::decoder()?;
    decoder.set_detail(true)?;
    let mut sites = vec![];
    // The reconstructed Object header still has the GC 1.3 data suffix in all
    // versions. Retail GC 2.6 and 2.7 omit that two-byte field. Use guarded retail
    // cache accesses, not sizeof/offsetof on that reconstructed declaration.
    let data_link_offset = if ["GC_2_6", "GC_2_7"].contains(&version) {
        54
    } else {
        56
    };
    let cache_function = metadata
        .iter()
        .find(|s| s.name == "_COptimizer_GetFunctionObject")
        .context("missing retail symbol cache accessor")?;
    ensure!(
        executable::size(&cache_function.options)? == 0x75,
        "symbol cache accessor size guard"
    );
    let cache = pe.read(cache_function.address, 0x75)?;
    ensure!(
        cache[0x36..0x3d] == [0x83, 0x7b, data_link_offset as u8, 0, 0x75, 0x0a, 0x53]
            && cache[0x43..0x4b]
                == [
                    0x89,
                    0x43,
                    data_link_offset as u8,
                    0x8b,
                    0x43,
                    data_link_offset as u8,
                    0x5b,
                    0xc3
                ],
        "retail data link-name cache layout guard"
    );
    let (normalize, normalize_hash, pair_sites) = match version {
        "GC_1_3" => (
            "_Operands_Normalize",
            "cdd84dd317bc1ccc73d71c61534230d8f725df0f972e7cfabe42fa3e2ae63b1d",
            [0x4e17da, 0x4e17e6],
        ),
        "GC_1_3_2" => (
            "_fn_004e10c0",
            "7d4343a886a40c4dff88f279a091567a016d93b834b6d4fe187fee93171b9d0e",
            [0x4e231a, 0x4e2326],
        ),
        "GC_2_6" => (
            "_fn_004e1d90",
            "7d4343a886a40c4dff88f279a091567a016d93b834b6d4fe187fee93171b9d0e",
            [0x4e316a, 0x4e3176],
        ),
        "GC_2_7" => (
            "_fn_004e1d90",
            "7d4343a886a40c4dff88f279a091567a016d93b834b6d4fe187fee93171b9d0e",
            [0x4e2fea, 0x4e2ff6],
        ),
        _ => unreachable!(),
    };
    ensure!(
        audit
            .functions
            .get(normalize)
            .is_some_and(|f| f.normalized_sha256 == normalize_hash),
        "symbol address operand ABI guard failed"
    );
    for (name, expected) in [
        (
            "_fn_004fcba0",
            "c7b3553183e2a39829ae89e562ce3bc1801d954270d1ba1e67e5017ecf4da38b",
        ),
        (
            "_Registers_BindVR",
            "bdde62ffa0f7872d0c8afa6518ddbf92b4b0270356083e1048ba87a2590886c5",
        ),
        (
            "_COpt_ArrayToRegister",
            "4492771ad6e0733be32e5c687da872cfbe999cf0be8b49b75930c3b0645a5ff8",
        ),
        (
            "_FunctionCalls_GenerateCall",
            "0aefa5c61a1dd82488fcce8b2480816f051613daa95f64ae7b543084307112bc",
        ),
    ] {
        ensure!(
            audit
                .functions
                .get(name)
                .is_some_and(|f| f.normalized_sha256 == expected),
            "object binding ABI guard failed for {name}"
        );
    }
    // Guard the constructor as well as the consumer: only these pinned retail
    // stores justify LoopVar's original object, sizes, count and slot meanings.
    let builder = metadata
        .iter()
        .find(|s| s.name == "_build_array_loopvars")
        .context("missing scalar array builder")?;
    let builder_hash = match version {
        "GC_1_3" => "084ecf59c2d0aaf301f2581df94d468013b1df8a3fff018327f6314e91b4cd37",
        "GC_1_3_2" => "b011ab9c2d7652b78e6f00abab6468fccf52f501a72beb39a3601cae6730e217",
        "GC_2_6" => "e9a67312f1baf4b3b9a5e554a987ac404c9a1497f6eb3c4c34e9ad43bd6034ba",
        "GC_2_7" => "aa78233ab4f31a3906968dad68aacb7470763b4f386871873e83c6a3083ae00b",
        _ => unreachable!(),
    };
    ensure!(
        executable::size(&builder.options)? == 0x1e3
            && format!("{:x}", Sha256::digest(pe.read(builder.address, 0x1e3)?)) == builder_hash,
        "scalar array builder layout guard failed"
    );
    for r in audit.references.iter().filter(|r| r.write) {
        let instructions =
            decoder.disasm_count(pe.read(r.address, 15)?, u64::from(r.address), 1)?;
        let i = instructions
            .iter()
            .next()
            .context("missing counter instruction")?;
        let detail = decoder.insn_detail(i)?;
        let arch = detail.arch_detail();
        let x86 = arch.x86().context("not x86")?;
        let operands: Vec<_> = x86.operands().collect();
        ensure!(i.bytes().len()>=5 && operands.iter().all(|op|!matches!(op.op_type,X86OperandType::Reg(RegId(r)) if r==X86Reg::X86_REG_ESP as u16)),
            "counter island cannot relocate an ESP operand");
        let mem = operands
            .iter()
            .find_map(|op| match op.op_type {
                X86OperandType::Mem(m) if op.access.is_some_and(|a| a.is_writable()) => {
                    Some((op.size, m))
                }
                _ => None,
            })
            .context("counter write has no memory destination")?;
        ensure!(
            mem.0 == 4 && mem.1.base() == RegId(0),
            "counter write uses an unsupported memory base or width"
        );
        let (selector, counter_class) = if mem.1.index() == RegId(0) {
            let class = audit
                .counters
                .iter()
                .find(|(_, a)| **a == mem.1.disp() as u32)
                .map(|(n, _)| match n.as_str() {
                    "VR" => 2,
                    "FPR" => 3,
                    "GPR" => 4,
                    _ => unreachable!(),
                })
                .context("absolute write outside known counter slots")?;
            (class, Some(class as u8))
        } else {
            ensure!(
                mem.1.scale() == 4 && mem.1.disp() as u32 == audit.table_base,
                "unexpected indexed counter address"
            );
            let slot = match mem.1.index().0 as u32 {
                x if x == X86Reg::X86_REG_EDI => 0,
                x if x == X86Reg::X86_REG_ESI => 1,
                x if x == X86Reg::X86_REG_EBP => 2,
                x if x == X86Reg::X86_REG_EBX => 4,
                x if x == X86Reg::X86_REG_EDX => 5,
                x if x == X86Reg::X86_REG_ECX => 6,
                x if x == X86Reg::X86_REG_EAX => 7,
                _ => bail!("unsupported counter index register"),
            };
            (-1 - slot, None)
        };
        ensure!(
            r.relocations.len() == 1,
            "counter write does not have one absolute fixup"
        );
        sites.push(Site {
            address: r.address,
            container: r.function.clone(),
            offset: r.offset,
            role: role(version, r)?,
            selector,
            bytes: i.bytes().to_vec(),
            field: Some((r.relocations[0] - r.address) as usize),
            counter_class,
            object_binding: match (r.function.as_str(), r.offset) {
                ("_fn_004fcba0", 0x28) => ObjectBinding::Single,
                ("_Registers_BindVR", 0x9a) => ObjectBinding::PairFirst,
                ("_Registers_BindVR", 0xa6) => ObjectBinding::PairSecond,
                _ => ObjectBinding::None,
            },
            operand_binding: if r.address == pair_sites[0] {
                OperandBinding::SymbolAddressHigh
            } else if r.address == pair_sites[1] {
                OperandBinding::SymbolAddressLow
            } else {
                OperandBinding::None
            },
            data_link_offset,
            origin_binding: match (r.function.as_str(), r.offset) {
                ("_COpt_ArrayToRegister", 0x332 | 0x345) => OriginBinding::ScalarArrayElement,
                ("_FunctionCalls_GenerateCall", 0x4c7 | 0x514 | 0x55d | 0x66d) => {
                    OriginBinding::CallResultSingle
                }
                ("_FunctionCalls_GenerateCall", 0x602) => OriginBinding::CallResultFirst,
                ("_FunctionCalls_GenerateCall", 0x612) => OriginBinding::CallResultSecond,
                _ => OriginBinding::None,
            },
        });
    }
    let expected = match version {
        "GC_1_3" => 570,
        "GC_1_3_2" => 571,
        "GC_2_6" | "GC_2_7" => 578,
        _ => unreachable!(),
    };
    ensure!(
        sites.len() == expected,
        "unexpected number of retail counter writes"
    );
    // The true generator is folded into larger metadata containers in later
    // versions. Its actual prologue, object store and epilogue were audited.
    let generator = match version {
        "GC_1_3" => 0x433590,
        "GC_1_3_2" => 0x433310,
        "GC_2_6" | "GC_2_7" => 0x4333f0,
        _ => unreachable!(),
    };
    ensure!(
        pe.read(generator, 10)? == [0x53, 0x56, 0x57, 0x55, 0x81, 0xec, 0xb8, 0, 0, 0],
        "generator prologue guard"
    );
    let container = metadata
        .iter()
        .filter(|s| {
            executable::option(&s.options, "type") == Some("function") && s.address <= generator
        })
        .filter_map(|s| executable::size(&s.options).ok().map(|size| (s, size)))
        .find(|(s, size)| generator + 0xdbf <= s.address + size)
        .context("generator is outside named retail containers")?
        .0;
    let begin = generator + 0x9d;
    let end = generator + 0xdb4;
    ensure!(pe.read(begin, 1)? == [0xa3], "generator object-store guard");
    ensure!(
        pe.read(end, 11)? == [0x81, 0xc4, 0xb8, 0, 0, 0, 0x5d, 0x5f, 0x5e, 0x5b, 0xc3],
        "generator epilogue guard"
    );
    for (address, length, role, field) in [
        (begin, 5, Role::FunctionBegin, Some(1)),
        (end, 6, Role::FunctionEnd, None),
    ] {
        sites.push(Site {
            address,
            container: container.name.clone(),
            offset: address - container.address,
            role,
            selector: 0,
            bytes: pe.read(address, length)?.to_vec(),
            field,
            counter_class: None,
            object_binding: ObjectBinding::None,
            operand_binding: OperandBinding::None,
            data_link_offset,
            origin_binding: OriginBinding::None,
        });
    }
    // Observe the completed snapshot/window, after its last actual memory write.
    // The equal-size island preserves interior branch targets as for counters.
    for (name, offset, role, target) in [
        (
            "_Registers_SnapshotInitialObjectRange",
            0x21,
            Role::InitialRange,
            "_gInitialObjectVRLast",
        ),
        (
            "_Registers_BeginCoalesceWindow",
            0x42,
            Role::WindowBegin,
            "_gVRCoalesceFirst",
        ),
    ] {
        let symbol = metadata
            .iter()
            .find(|s| s.name == name)
            .context("missing category snapshot function")?;
        let address = symbol.address + offset;
        let bytes = pe.read(address, 6)?.to_vec();
        ensure!(
            bytes[..2] == [0x66, 0xa3] && pe.read(address + 6, 1)? == [0xc3],
            "category snapshot epilogue guard"
        );
        let expected = metadata
            .iter()
            .find(|s| s.name == target)
            .context("missing category snapshot target")?
            .address;
        ensure!(
            u32::from_le_bytes(bytes[2..6].try_into()?) == expected,
            "category snapshot store target guard"
        );
        sites.push(Site {
            address,
            container: name.into(),
            offset,
            role,
            selector: 0,
            bytes,
            field: Some(2),
            counter_class: None,
            object_binding: ObjectBinding::None,
            operand_binding: OperandBinding::None,
            data_link_offset,
            origin_binding: OriginBinding::None,
        });
    }
    ensure!(
        sites
            .iter()
            .filter(|s| s.object_binding != ObjectBinding::None)
            .count()
            == 3,
        "missing object binding sites"
    );
    sites.sort_by_key(|s| s.address);
    let origins = sites
        .iter()
        .filter(|s| s.origin_binding != OriginBinding::None)
        .collect::<Vec<_>>();
    ensure!(
        origins.len() == 8
            && origins.iter().all(|s| s.role == Role::Increment
                && s.counter_class
                    == Some(match (s.container.as_str(), s.offset) {
                        ("_COpt_ArrayToRegister", 0x332)
                        | ("_FunctionCalls_GenerateCall", 0x4c7 | 0x55d) => 3,
                        ("_FunctionCalls_GenerateCall", 0x514) => 2,
                        _ => 4,
                    })),
        "missing or inconsistent guarded birth-origin sites"
    );
    ensure!(
        sites
            .iter()
            .filter(|s| s.operand_binding != OperandBinding::None)
            .all(|s| s.container == normalize
                && s.counter_class == Some(4)
                && s.role == Role::Increment)
            && sites
                .iter()
                .filter(|s| s.operand_binding != OperandBinding::None)
                .count()
                == 2,
        "missing symbolic address birth sites"
    );
    ensure!(
        sites
            .windows(2)
            .all(|p| p[0].address + p[0].bytes.len() as u32 <= p[1].address),
        "overlapping counter sites"
    );
    Ok(sites)
}

pub fn install(root: &Path, version: &str, inputs: &[(String, Vec<u8>)]) -> Result<Installed> {
    let sites = plan(root, version)?;
    let original = Original::read(root, version)?;
    let pe = original.pe()?;
    let fixups = executable::base_relocations(&pe)?;
    let metadata = executable::read_symbols(&fs::read_to_string(
        root.join(format!("config/{version}/symbols.txt")),
    )?)?;
    let scalar_helpers = ["_fn_00460780", "_fn_004601f0", "_create_temp_object"];
    let mut scalar_checked = BTreeSet::new();
    // Check the actual split inputs too: a pristine PE alone cannot establish
    // the ABI of a modified constructor/consumer supplied to the linker.
    for (_, bytes) in inputs {
        let object = Object::parse(bytes)?;
        for name in scalar_helpers {
            let Some(symbol) = object
                .symbols
                .values()
                .find(|s| s.section > 0 && s.name == name)
            else {
                continue;
            };
            ensure!(
                scalar_checked.insert(name),
                "duplicate scalarization helper {name}"
            );
            let reference = metadata
                .iter()
                .find(|s| s.name == name)
                .context("missing scalarization helper metadata")?;
            let length = executable::size(&reference.options)? as usize;
            let section = &object.sections[symbol.section as usize - 1];
            let mut expected = pe.read(reference.address, length)?.to_vec();
            let mut actual = section
                .data
                .get(symbol.value as usize..symbol.value as usize + length)
                .context("scalarization helper outside split section")?
                .to_vec();
            let mut fields = BTreeMap::new();
            for &at in fixups
                .iter()
                .filter(|&&at| reference.address <= at && at < reference.address + length as u32)
            {
                fields.insert((at - reference.address) as usize, 6);
            }
            for (at, target) in crate::x86::branches(&expected, reference.address)? {
                if !(reference.address..reference.address + length as u32).contains(&target) {
                    fields.insert(at as usize, 20);
                }
            }
            let mut seen = BTreeSet::new();
            let mut relocations = section
                .relocations
                .iter()
                .filter(|r| symbol.value <= r.offset && r.offset < symbol.value + length as u32)
                .collect::<Vec<_>>();
            relocations.sort_by_key(|r| r.offset);
            let mut identities = Sha256::new();
            for r in relocations {
                let at = (r.offset - symbol.value) as usize;
                ensure!(
                    fields.get(&at) == Some(&r.kind) && seen.insert(at),
                    "unexpected scalarization helper relocation"
                );
                let target = object
                    .symbols
                    .get(&r.symbol)
                    .context("scalarization relocation target is missing or auxiliary")?;
                // The split addend and named symbol binding are part of the ABI
                // guard. Zeroing relocation fields alone would admit retargeted
                // calls or data references with otherwise identical bodies.
                identities.update((at as u32).to_le_bytes());
                identities.update(r.kind.to_le_bytes());
                identities.update((target.name.len() as u32).to_le_bytes());
                identities.update(target.name.as_bytes());
                identities.update(target.value.to_le_bytes());
                identities.update(target.section.to_le_bytes());
                identities.update(target.kind.to_le_bytes());
                identities.update([target.storage]);
                identities.update((target.auxiliary.len() as u32).to_le_bytes());
                for auxiliary in &target.auxiliary {
                    identities.update(auxiliary);
                }
                identities.update(
                    actual
                        .get(at..at + 4)
                        .context("scalarization relocation addend crosses boundary")?,
                );
                actual
                    .get_mut(at..at + 4)
                    .context("scalarization helper fixup crosses boundary")?
                    .fill(0);
            }
            ensure!(
                seen.len() == fields.len(),
                "missing scalarization helper relocations"
            );
            let identity_hash = match (version, name) {
                ("GC_1_3", "_fn_00460780") => {
                    "ba192f74680639732bd1f2efeb58c4f3a8bcddd5b4c158a49bd98278475827b2"
                }
                ("GC_1_3_2", "_fn_00460780") => {
                    "646bd4f9909f58c6a7f4a5bea4d11b7142e167020f79fe14108a7f416540797f"
                }
                ("GC_2_6" | "GC_2_7", "_fn_00460780") => {
                    "c6033a48c09cb6aaabe3ed9ee04891d3067a1e90fc3497ec521ef5645d9cb4fe"
                }
                ("GC_1_3", "_fn_004601f0") => {
                    "4cab8f925dd94ff3a6622b2215291ff1019b18b106881ee991793c2e0521ab12"
                }
                ("GC_1_3_2", "_fn_004601f0") => {
                    "233250dac2d5aa99f7feb8d2b37c383df67a3a357944efcee5cbfb5d94896140"
                }
                ("GC_2_6" | "GC_2_7", "_fn_004601f0") => {
                    "e20d28166fb5c73263bead158939639bdd0039dc27e6fa137274224b7935c357"
                }
                ("GC_1_3", "_create_temp_object") => {
                    "b62e1a5b0015f057028dc8f73fc946d4b578e198c94aeb629fa6c66ba6e70b17"
                }
                ("GC_1_3_2", "_create_temp_object") => {
                    "e38b575766bf848f91a6893638daf76231e92d0d1ee4004858e28e62744be1ba"
                }
                ("GC_2_6", "_create_temp_object") => {
                    "de1a36122c4793c81071fd47ac8f184852712ccb63bf53fca14c86f13007d980"
                }
                ("GC_2_7", "_create_temp_object") => {
                    "be61889a29289834952d942e7419e9d2441430b169ac096773d1db2532d00404"
                }
                _ => bail!("unsupported scalarization helper identity guard"),
            };
            ensure!(
                format!("{:x}", identities.finalize()) == identity_hash,
                "scalarization helper relocation identity guard failed for {name}"
            );
            for at in fields.keys() {
                expected
                    .get_mut(*at..*at + 4)
                    .context("scalarization reference fixup crosses boundary")?
                    .fill(0);
            }
            ensure!(
                actual == expected,
                "scalarization helper byte guard failed for {name}"
            );
        }
    }
    ensure!(
        scalar_checked.len() == scalar_helpers.len(),
        "missing scalarization helper split inputs"
    );
    let mut islands = Builder::default();
    let code_section = islands.section(".text", 0x60500020, vec![], 0, vec![])?;
    let data_section = islands.section(".rdata", 0x40300040, vec![], 0, vec![])?;
    let counts = islands.external(COUNTS)?;
    let physical = islands.external(PHYSICAL)?;
    let handler = islands.external(HANDLER)?;
    let initial = islands.external("_gInitialObjectVRLast")?;
    let first = islands.external("_gVRCoalesceFirst")?;
    let last = islands.external("_gVRCoalesceLast")?;
    // The short tables and the int table must use class order VR,FPR,GPR.
    for names in [
        [
            "_gInitialObjectVRLast",
            "_gInitialObjectFPRLast",
            "_gInitialObjectGPRLast",
        ],
        [
            "_gVRCoalesceFirst",
            "_gFPRCoalesceFirst",
            "_gGPRCoalesceFirst",
        ],
        ["_gVRCoalesceLast", "_gFPRCoalesceLast", "_gGPRCoalesceLast"],
    ] {
        let width = if names[0] == "_gVRCoalesceLast" { 4 } else { 2 };
        let addresses =
            names.map(|name| metadata.iter().find(|s| s.name == name).map(|s| s.address));
        let start = addresses[0].context("missing category table")?;
        ensure!(
            addresses == [Some(start), Some(start + width), Some(start + 2 * width)],
            "category table layout guard"
        );
    }
    let mut objects = BTreeMap::new();
    let mut installed = BTreeSet::new();
    for (path, bytes) in inputs {
        let object = Object::parse(bytes)?;
        let mut patches = vec![];
        let mut checked = BTreeSet::new();
        for site in &sites {
            let Some(symbol) = object
                .symbols
                .values()
                .find(|s| s.section > 0 && s.name == site.container)
            else {
                continue;
            };
            ensure!(
                installed.insert(site.address),
                "counter site defined in multiple input objects"
            );
            let section_index = symbol.section as usize - 1;
            let section = &object.sections[section_index];
            if checked.insert(site.container.as_str()) {
                let reference = metadata
                    .iter()
                    .find(|s| s.name == site.container)
                    .context("missing retail container metadata")?;
                let length = executable::size(&reference.options)? as usize;
                let mut expected = pe.read(reference.address, length)?.to_vec();
                let mut actual = section
                    .data
                    .get(symbol.value as usize..symbol.value as usize + length)
                    .context("retail container outside split section")?
                    .to_vec();
                let mut fields = BTreeMap::new();
                for &at in fixups.iter().filter(|&&at| {
                    reference.address <= at && at < reference.address + length as u32
                }) {
                    fields.insert((at - reference.address) as usize, 6);
                }
                for (at, target) in crate::x86::branches(&expected, reference.address)? {
                    if !(reference.address..reference.address + length as u32).contains(&target) {
                        fields.insert(at as usize, 20);
                    }
                }
                let mut seen = BTreeSet::new();
                for r in section
                    .relocations
                    .iter()
                    .filter(|r| symbol.value <= r.offset && r.offset < symbol.value + length as u32)
                {
                    let offset = (r.offset - symbol.value) as usize;
                    ensure!(
                        fields.get(&offset) == Some(&r.kind) && seen.insert(offset),
                        "unexpected retail container relocation"
                    );
                    actual
                        .get_mut(offset..offset + 4)
                        .context("retail container fixup crosses boundary")?
                        .fill(0);
                }
                ensure!(
                    seen.len() == fields.len(),
                    "missing retail container relocations"
                );
                for at in fields.keys() {
                    expected
                        .get_mut(*at..*at + 4)
                        .context("retail reference fixup crosses boundary")?
                        .fill(0);
                }
                ensure!(
                    actual == expected,
                    "retail container byte guard failed for {}",
                    site.container
                );
            }
            let offset = symbol
                .value
                .checked_add(site.offset)
                .context("counter offset overflow")? as usize;
            let actual = section
                .data
                .get(offset..offset + site.bytes.len())
                .context("counter site outside split section")?;
            let mut expected = site.bytes.clone();
            let mut copied = actual.to_vec();
            let original_reloc = if let Some(field) = site.field {
                expected[field..field + 4].fill(0);
                let mut masked = copied.clone();
                masked[field..field + 4].fill(0);
                ensure!(
                    expected == masked,
                    "retail counter instruction byte guard failed at {:#x}",
                    site.address
                );
                let fixup = section
                    .relocations
                    .iter()
                    .find(|r| r.offset as usize == offset + field && r.kind == 6)
                    .context("counter island lacks its absolute relocation")?;
                let target = &object.symbols[&fixup.symbol];
                ensure!(
                    target.storage == 2,
                    "counter instruction references a local-only symbol"
                );
                let target_address = metadata
                    .iter()
                    .find(|s| s.name == target.name)
                    .context("counter fixup lacks retail symbol metadata")?
                    .address;
                let addend = u32::from_le_bytes(copied[field..field + 4].try_into()?);
                let expected_address = u32::from_le_bytes(site.bytes[field..field + 4].try_into()?);
                ensure!(
                    target_address.wrapping_add(addend) == expected_address,
                    "counter/context relocation target changed"
                );
                let external = islands.external(&target.name)?;
                Some((field, external))
            } else {
                ensure!(copied == expected, "retail epilogue byte guard failed");
                None
            };
            ensure!(
                section
                    .relocations
                    .iter()
                    .filter(|r| (offset..offset + copied.len()).contains(&(r.offset as usize)))
                    .count()
                    == usize::from(original_reloc.is_some()),
                "unexpected relocation in counter patch"
            );
            let descriptor_offset = islands.sections[data_section as usize - 1].data.len() as u32;
            let descriptor_name = format!("_OracleRust_CounterDescriptor_{:08x}", site.address);
            let descriptor =
                islands.add_symbol(&descriptor_name, descriptor_offset, data_section, 0, 3, &[])?;
            let mut desc = vec![0; 12];
            desc.extend(site.selector.to_le_bytes());
            desc.extend((site.role as u32).to_le_bytes());
            desc.extend([0; 12]);
            desc.extend((site.object_binding as u32).to_le_bytes());
            desc.extend((site.operand_binding as u32).to_le_bytes());
            desc.extend(site.data_link_offset.to_le_bytes());
            desc.extend((site.origin_binding as u32).to_le_bytes());
            let mut desc_relocations = vec![
                Relocation {
                    offset: descriptor_offset + 4,
                    symbol: counts,
                    kind: 6,
                },
                Relocation {
                    offset: descriptor_offset + 8,
                    symbol: physical,
                    kind: 6,
                },
            ];
            for (offset, symbol) in [(20, initial), (24, first), (28, last)] {
                desc_relocations.push(Relocation {
                    offset: descriptor_offset + offset,
                    symbol,
                    kind: 6,
                });
            }
            if !matches!(site.role, Role::FunctionBegin | Role::FunctionEnd) {
                let (field, external) = original_reloc.context("counter pointer missing")?;
                desc[..4].copy_from_slice(&copied[field..field + 4]);
                desc_relocations.push(Relocation {
                    offset: descriptor_offset,
                    symbol: external,
                    kind: 6,
                });
            }
            islands.sections[data_section as usize - 1]
                .data
                .extend(desc);
            islands.sections[data_section as usize - 1]
                .relocations
                .extend(desc_relocations);
            let island_offset = islands.sections[code_section as usize - 1].data.len() as u32;
            let island_name = format!("_OracleRust_CounterIsland_{:08x}", site.address);
            islands.add_symbol(&island_name, island_offset, code_section, 0x20, 2, &[])?;
            let mut emitted = vec![];
            let mut relocations = vec![];
            if site.role != Role::FunctionEnd {
                if let Some((field, external)) = original_reloc {
                    relocations.push(Relocation {
                        offset: island_offset + field as u32,
                        symbol: external,
                        kind: 6,
                    });
                }
                emitted.append(&mut copied);
            }
            let (probe, fields) = backend_probe::callback(site.address, descriptor, handler);
            let probe_offset = island_offset + emitted.len() as u32;
            relocations.extend(fields.into_iter().map(|mut r| {
                r.offset += probe_offset;
                r
            }));
            emitted.extend(probe);
            if site.role == Role::FunctionEnd {
                // Discard CALL's return slot, execute the exact retail stack
                // adjustment (including its flags), then resume at the POPs.
                emitted.extend([0x83, 0xc4, 4]);
                emitted.extend(actual);
                emitted.push(0xe9);
                let target = islands.external(&symbol.name)?;
                relocations.push(Relocation {
                    offset: island_offset + emitted.len() as u32,
                    symbol: target,
                    kind: 20,
                });
                emitted.extend((site.offset + site.bytes.len() as u32).to_le_bytes());
            } else {
                emitted.push(0xc3);
            }
            islands.sections[code_section as usize - 1]
                .data
                .extend(emitted);
            islands.sections[code_section as usize - 1]
                .relocations
                .extend(relocations);
            patches.push((section_index, offset, site.bytes.len(), island_name));
        }
        if patches.is_empty() {
            continue;
        }
        let mut rebuilt = Builder::default();
        for section in &object.sections {
            rebuilt.section(
                &section.name,
                section.flags,
                section.data.to_vec(),
                section.size,
                vec![],
            )?;
        }
        let mut symbols = BTreeMap::new();
        for (&index, s) in &object.symbols {
            let section_symbol = s.storage == 3
                && s.value == 0
                && s.section > 0
                && s.auxiliary.len() == 1
                && s.name == object.sections[s.section as usize - 1].name;
            let new = if section_symbol {
                rebuilt.section_symbols[&s.section]
            } else {
                ensure!(
                    s.auxiliary.is_empty(),
                    "unsupported counter input auxiliary symbol"
                );
                rebuilt.add_symbol(&s.name, s.value, s.section, s.kind, s.storage, &[])?
            };
            symbols.insert(index, new);
        }
        for (index, section) in object.sections.iter().enumerate() {
            rebuilt.sections[index].relocations = section
                .relocations
                .iter()
                .map(|r| Relocation {
                    offset: r.offset,
                    symbol: symbols[&r.symbol],
                    kind: r.kind,
                })
                .collect();
        }
        for (section, offset, length, name) in patches {
            let external = rebuilt.external(&name)?;
            let s = &mut rebuilt.sections[section];
            s.data[offset..offset + length].fill(0x90);
            s.data[offset] = 0xe8;
            s.data[offset + 1..offset + 5].fill(0);
            s.relocations
                .retain(|r| !(offset..offset + length).contains(&(r.offset as usize)));
            s.relocations.push(Relocation {
                offset: offset as u32 + 1,
                symbol: external,
                kind: 20,
            });
        }
        rebuilt.refresh_section_definitions()?;
        objects.insert(path.clone(), rebuilt.finish()?);
    }
    ensure!(
        installed.len() == sites.len(),
        "not all audited counter/context sites occur in retail link inputs ({} of {})",
        installed.len(),
        sites.len()
    );
    for section in &mut islands.sections {
        section.size = section.data.len().try_into()?;
    }
    islands.refresh_section_definitions()?;
    Ok(Installed {
        objects,
        islands: islands.finish()?,
        sites,
    })
}
