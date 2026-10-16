//! Legacy split.py instruction lengths, used to discover rel32 branch fixups.
use anyhow::{Context, Result, ensure};
#[derive(Clone, Copy)]
enum Immediate {
    Bytes(usize),
    Operand,
    Address,
    Far,
    Group3,
}
fn one(op: u8) -> Option<(bool, Immediate)> {
    use Immediate::*;
    Some(match op {
        0x00..=0x3f => match op & 7 {
            0..=3 => (true, Bytes(0)),
            4 => (false, Bytes(1)),
            5 => (false, Operand),
            _ => (false, Bytes(0)),
        },
        0x40..=0x61
        | 0x6c..=0x6f
        | 0x90..=0x99
        | 0x9b..=0x9f
        | 0xa4..=0xa7
        | 0xaa..=0xaf
        | 0xc3
        | 0xc9
        | 0xcb
        | 0xcc
        | 0xce
        | 0xcf
        | 0xd6
        | 0xd7
        | 0xec..=0xef
        | 0xf1
        | 0xf4
        | 0xf5
        | 0xf8..=0xfd => (false, Bytes(0)),
        0x62 | 0x63 | 0x84..=0x8f | 0xc4 | 0xc5 | 0xd0..=0xd3 | 0xd8..=0xdf | 0xfe | 0xff => {
            (true, Bytes(0))
        }
        0x68 | 0xa9 | 0xb8..=0xbf | 0xe8 | 0xe9 => (false, Operand),
        0x69 | 0x81 | 0xc7 => (true, Operand),
        0x6a | 0x70..=0x7f | 0xa8 | 0xb0..=0xb7 | 0xcd | 0xd4 | 0xd5 | 0xe0..=0xe7 | 0xeb => {
            (false, Bytes(1))
        }
        0x6b | 0x80 | 0x82 | 0x83 | 0xc0 | 0xc1 | 0xc6 => (true, Bytes(1)),
        0x9a | 0xea => (false, Far),
        0xa0..=0xa3 => (false, Address),
        0xc2 | 0xca => (false, Bytes(2)),
        0xc8 => (false, Bytes(3)),
        0xf6 | 0xf7 => (true, Group3),
        _ => return None,
    })
}
fn two(op: u8) -> Option<(bool, Immediate)> {
    use Immediate::*;
    Some(match op {
        0x00..=0x03
        | 0x0d
        | 0x10..=0x17
        | 0x1f
        | 0x20..=0x23
        | 0x28..=0x2f
        | 0x40..=0x6f
        | 0x74..=0x76
        | 0x7e
        | 0x7f
        | 0x90..=0x9f
        | 0xa3
        | 0xa5
        | 0xab
        | 0xad
        | 0xaf
        | 0xb0
        | 0xb1
        | 0xb3
        | 0xb6
        | 0xb7
        | 0xbb..=0xbf
        | 0xc0
        | 0xc1
        | 0xc7
        | 0xd0..=0xff => (true, Bytes(0)),
        0x06
        | 0x08
        | 0x09
        | 0x0b
        | 0x0e
        | 0x31
        | 0x77
        | 0xa0..=0xa2
        | 0xa8
        | 0xa9
        | 0xc8..=0xcf => (false, Bytes(0)),
        0x0f | 0x70..=0x73 | 0xa4 | 0xac | 0xba => (true, Bytes(1)),
        0x80..=0x8f => (false, Operand),
        _ => return None,
    })
}
fn prefix(byte: u8) -> bool {
    matches!(
        byte,
        0x26 | 0x2e | 0x36 | 0x3e | 0x64..=0x67 | 0xf0 | 0xf2 | 0xf3
    )
}
pub fn branches(code: &[u8], base: u32) -> Result<Vec<(u32, u32)>> {
    let mut out = vec![];
    let mut offset = 0usize;
    while offset < code.len() {
        let start = offset;
        let mut operand16 = false;
        let mut address16 = false;
        while code.get(offset).is_some_and(|b| prefix(*b)) {
            operand16 |= code[offset] == 0x66;
            address16 |= code[offset] == 0x67;
            offset += 1;
        }
        let mut opcode = u16::from(*code.get(offset).context("truncated x86 prefix sequence")?);
        offset += 1;
        let (modrm, mut immediate) = if opcode == 0x0f {
            let byte = *code.get(offset).context("truncated x86 two-byte opcode")?;
            opcode = 0x0f00 | u16::from(byte);
            offset += 1;
            two(byte)
        } else {
            one(opcode as u8)
        }
        .with_context(|| format!("unsupported x86 opcode {opcode:#x} at {start:#x}"))?;
        if modrm {
            let byte = *code.get(offset).context("truncated x86 ModRM")?;
            offset += 1;
            let mode = byte >> 6;
            let rm = byte & 7;
            if matches!(immediate, Immediate::Group3) {
                immediate = if (byte >> 3) & 7 <= 1 {
                    if opcode == 0xf6 {
                        Immediate::Bytes(1)
                    } else {
                        Immediate::Operand
                    }
                } else {
                    Immediate::Bytes(0)
                };
            }
            if address16 {
                offset += match mode {
                    0 => usize::from(rm == 6) * 2,
                    1 => 1,
                    2 => 2,
                    _ => 0,
                };
            } else {
                if mode != 3 && rm == 4 {
                    let sib = *code.get(offset).context("truncated x86 SIB")?;
                    offset += 1 + usize::from(mode == 0 && sib & 7 == 5) * 4;
                }
                offset += match mode {
                    0 => usize::from(rm == 5) * 4,
                    1 => 1,
                    2 => 4,
                    _ => 0,
                };
            }
        }
        let field = offset;
        let relative = matches!(opcode, 0xe8 | 0xe9 | 0x0f80..=0x0f8f);
        offset += match immediate {
            Immediate::Operand => {
                if operand16 && !relative {
                    2
                } else {
                    4
                }
            }
            Immediate::Address => {
                if address16 {
                    2
                } else {
                    4
                }
            }
            Immediate::Far => 6,
            Immediate::Bytes(n) => n,
            Immediate::Group3 => unreachable!(),
        };
        if relative {
            ensure!(
                field + 4 <= code.len(),
                "truncated x86 branch at {start:#x}"
            );
            let displacement = i32::from_le_bytes(code[field..field + 4].try_into()?);
            out.push((
                u32::try_from(field)?,
                base.wrapping_add(u32::try_from(offset)?)
                    .wrapping_add(displacement as u32),
            ));
        }
        // Some retail symbol ranges stop inside a final non-branch instruction.
        // Like split.py, scan starts inside the range but do not invent a next
        // instruction or a fixup beyond it; a branch still needs its full field.
    }
    Ok(out)
}
