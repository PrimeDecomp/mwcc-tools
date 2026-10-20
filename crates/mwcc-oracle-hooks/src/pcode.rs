//! Shared PCode observations. Historical layouts are checked by pcode_layout.c.
use crate::abi::{OracleTrace_Printf, getenv, read, sprintf, wanted};
use core::{ffi::c_char, ptr};

unsafe extern "C" {
    fn CMangler_GetLinkName(object: *const u8) -> *const u8;
    pub static mut gPCodeBlocks: *const u8;
}

#[unsafe(export_name = "OracleRust_LinkName")]
pub unsafe extern "C" fn link_name(mut object: *const u8) -> *const c_char {
    unsafe {
        while !object.is_null() && read::<u8>(object, 2) == 6 {
            object = read(object, 42);
        }
        if object.is_null() {
            return c"?".as_ptr();
        }
        if matches!(read::<u8>(object, 2), 3 | 4) {
            let mut name = read::<*const u8>(object, 50);
            if name.is_null() {
                name = CMangler_GetLinkName(object);
            }
            if !name.is_null() {
                return name.add(10).cast();
            }
        }
        let name = read::<*const u8>(object, 10);
        if name.is_null() {
            c"?".as_ptr()
        } else {
            name.add(10).cast()
        }
    }
}

#[unsafe(export_name = "OracleRust_PrintInstruction")]
pub unsafe extern "C" fn instruction(prefix: *const c_char, instruction: *const u8) {
    unsafe {
        let count = read::<i16>(instruction, 34);
        OracleTrace_Printf(
            c"%s p=%08x op=%d flags=%08x n=%d line=%d ops=".as_ptr(),
            prefix,
            instruction as u32,
            i32::from(read::<i16>(instruction, 32)),
            read::<u32>(instruction, 20),
            i32::from(count),
            read::<i32>(instruction, 28),
        );
        for operand in 0..count {
            let raw = instruction.add(36 + operand as usize * 12);
            for byte in 0..12 {
                OracleTrace_Printf(c"%02x".as_ptr(), u32::from(read::<u8>(raw, byte)));
            }
            OracleTrace_Printf(c" ".as_ptr());
        }
        OracleTrace_Printf(c"\n".as_ptr());
        if wanted(c"MWCC_ORACLE_NAMES") {
            names(instruction, count);
        }
    }
}

// The opcodes pc.opcode has named (one bit each).
static mut OPCODES: [u8; 64] = [0; 64];

// What a reader needs to spell an instruction as the compiler's own listings do (MWCC_ORACLE_NAMES): pc.opcode, the
// first time an opcode is printed, with its name and scheduling rank; pc.object, for each operand that refers to an
// object, its name.
unsafe fn names(instruction: *const u8, count: i16) {
    unsafe {
        let opcode = read::<i16>(instruction, 32);
        if (0..512).contains(&opcode) {
            let seen = ptr::addr_of_mut!(OPCODES)
                .cast::<u8>()
                .add(opcode as usize / 8);
            let bit = 1 << (opcode % 8);
            if seen.read() & bit == 0 {
                seen.write(seen.read() | bit);
                if let Some(table) = crate::schedule::opcode_table() {
                    let entry = table.add(opcode as usize * 18);
                    let name = read::<*const c_char>(entry, 0);
                    OracleTrace_Printf(
                        c"pc.opcode op=%d rank=%d name=%s\n".as_ptr(),
                        i32::from(opcode),
                        i32::from(read::<u8>(entry, 9)),
                        if name.is_null() { c"?".as_ptr() } else { name },
                    );
                }
            }
        }
        for operand in 0..count {
            let raw = instruction.add(36 + operand as usize * 12);
            if read::<u8>(raw, 0) != 3 {
                continue;
            }
            let object = read::<*const u8>(raw, 6);
            // (HashNameNode.name is an inline tail at byte 10)
            let name = if object.is_null() || read::<*const u8>(object, 10).is_null() {
                c"?".as_ptr()
            } else {
                read::<*const u8>(object, 10).add(10).cast()
            };
            OracleTrace_Printf(c"pc.object p=%08x name=%s\n".as_ptr(), object as u32, name);
        }
    }
}

/// gc2's probe: copypropagatestouse has decided whether COPY propagates into USE, by the test REASON
/// (`cp.use`, then the two instructions as `cp.copy` and `cp.into`; MWCC_ORACLE_COPYPROP).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_CopyUse(
    copy: *const u8,
    use_: *const u8,
    reason: i32,
    accepted: i32,
) {
    unsafe {
        if wanted(c"MWCC_ORACLE_COPYPROP") {
            OracleTrace_Printf(c"cp.use reason=%d accepted=%d\n".as_ptr(), reason, accepted);
            instruction(c"cp.copy".as_ptr(), copy);
            instruction(c"cp.into".as_ptr(), use_);
        }
    }
}

/// gc2's probe: propagateandremovecopy has removed COPY, or kept it (`cp.remove`, then `cp.copy`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_CopyRemove(copy: *const u8, removed: i32) {
    unsafe {
        if wanted(c"MWCC_ORACLE_COPYPROP") {
            OracleTrace_Printf(c"cp.remove removed=%d\n".as_ptr(), removed);
            instruction(c"cp.copy".as_ptr(), copy);
        }
    }
}

// The peephole's patterns, as installpattern named them (once each, for the opcodes they are installed for):
// (function, name).
static mut PATTERNS: [(usize, *const c_char); 128] = [(0, ptr::null()); 128];
static mut PATTERN_COUNT: usize = 0;
// The instruction a pattern is trying, as it was before (a PCode of up to 256 operands), and the pattern.
static mut TRIED: [u8; 36 + 12 * 256] = [0; 36 + 12 * 256];
static mut TRIED_PATTERN: usize = 0;

/// gc2's probe: installpattern has installed the pattern FUNC, NAME.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_PeepholePattern(func: *const u8, name: *const c_char) {
    unsafe {
        let count = ptr::addr_of!(PATTERN_COUNT).read();
        let patterns = ptr::addr_of!(PATTERNS).cast::<(usize, *const c_char)>();
        if (0..count).any(|index| patterns.add(index).read().0 == func as usize) {
            return;
        }
        if count < 128 {
            ptr::addr_of_mut!(PATTERNS)
                .cast::<(usize, *const c_char)>()
                .add(count)
                .write((func as usize, name));
            ptr::addr_of_mut!(PATTERN_COUNT).write(count + 1);
        }
    }
}

/// gc2's probe: the pattern FUNC is about to try INSTRUCTION (MWCC_ORACLE_PEEPHOLE).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_PeepholeTry(instruction: *const u8, func: *const u8) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_PEEPHOLE") {
            return;
        }
        let count = read::<i16>(instruction, 34);
        let copied = (0..=256).contains(&count);
        // (volatile byte copies: the retail compiler exports no memcpy for LLVM's)
        if copied {
            let tried = ptr::addr_of_mut!(TRIED).cast::<u8>();
            for byte in 0..36 + 12 * count as usize {
                tried
                    .add(byte)
                    .write_volatile(instruction.add(byte).read_volatile());
            }
        }
        ptr::addr_of_mut!(TRIED_PATTERN).write(if copied { func as usize } else { 0 });
    }
}

/// gc2's probe: the pattern has changed INSTRUCTION, or removed it (`ph.hit` with the pattern's name, then the
/// instruction before as `ph.before` and, unless it was removed, after as `ph.after`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_PeepholeHit(instruction: *const u8) {
    unsafe {
        let pattern = ptr::addr_of!(TRIED_PATTERN).read();
        if pattern == 0 || !wanted(c"MWCC_ORACLE_PEEPHOLE") {
            return;
        }
        let mut name = c"?".as_ptr();
        for index in 0..ptr::addr_of!(PATTERN_COUNT).read() {
            let (func, named) = ptr::addr_of!(PATTERNS)
                .cast::<(usize, *const c_char)>()
                .add(index)
                .read();
            if func == pattern {
                name = named;
                break;
            }
        }
        OracleTrace_Printf(c"ph.hit rule=%s\n".as_ptr(), name);
        self::instruction(c"ph.before".as_ptr(), ptr::addr_of!(TRIED).cast());
        if !read::<*const u8>(instruction, 8).is_null() {
            self::instruction(c"ph.after".as_ptr(), instruction);
        }
    }
}

/// gc2's probe: INSTRUCTION is dead and about to be removed (`ph.dead`, then it as `ph.before`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_PeepholeDead(instruction: *const u8) {
    unsafe {
        if wanted(c"MWCC_ORACLE_PEEPHOLE") {
            OracleTrace_Printf(c"ph.dead\n".as_ptr());
            self::instruction(c"ph.before".as_ptr(), instruction);
        }
    }
}

// The number of aliases of the table gc2's probe is reporting (the bits of a set's members).
static mut ALIAS_COUNT: i32 = 0;

// An object's name (HashNameNode.name is an inline tail at byte 10), or `-` for none.
unsafe fn object_name(object: *const u8) -> *const c_char {
    unsafe {
        if object.is_null() {
            c"-".as_ptr()
        } else if read::<*const u8>(object, 10).is_null() {
            c"?".as_ptr()
        } else {
            read::<*const u8>(object, 10).add(10).cast()
        }
    }
}

/// gc2's probe: gather_alias_info has left FUNCTION's table of COUNT aliases (`al.begin`, then each alias, each
/// access and `al.end`; MWCC_ORACLE_ALIAS).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_AliasBegin(function: *const u8, coloring: i32, count: i32) {
    unsafe {
        if wanted(c"MWCC_ORACLE_ALIAS") {
            ptr::addr_of_mut!(ALIAS_COUNT).write(count);
            OracleTrace_Printf(
                c"al.begin coloring=%d count=%d function=%s link=%s\n".as_ptr(),
                i32::from(coloring != 0),
                count,
                object_name(function),
                link_name(function),
            );
        }
    }
}

/// gc2's probe: the alias INDEX of TYPE (0 an object, 1 a part of one, 2 a set), its object, offset and size, and a
/// set's MEMBERS, a bit vector over the table's indices (`al.alias`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_Alias(
    index: i32,
    kind: i32,
    object: *const u8,
    offset: i32,
    size: i32,
    members: *const u32,
) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_ALIAS") {
            return;
        }
        OracleTrace_Printf(
            c"al.alias index=%d type=%d object=%08x offset=%d size=%d members=".as_ptr(),
            index,
            kind,
            object as u32,
            offset,
            size,
        );
        if !members.is_null() {
            for member in 0..ptr::addr_of!(ALIAS_COUNT).read().max(0) {
                if members.add(member as usize / 32).read_unaligned() & (1 << (member % 32)) != 0 {
                    OracleTrace_Printf(c"%d,".as_ptr(), member);
                }
            }
        }
        OracleTrace_Printf(c" name=%s\n".as_ptr(), object_name(object));
    }
}

/// gc2's probe: PCODE has the alias INDEX (`al.access`, then the instruction as `al.i`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_AliasAccess(pcode: *const u8, index: i32) {
    unsafe {
        if wanted(c"MWCC_ORACLE_ALIAS") {
            OracleTrace_Printf(c"al.access alias=%d\n".as_ptr(), index);
            instruction(c"al.i".as_ptr(), pcode);
        }
    }
}

/// gc2's probe: the table is reported (`al.end`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_AliasEnd() {
    unsafe {
        if wanted(c"MWCC_ORACLE_ALIAS") {
            OracleTrace_Printf(c"al.end\n".as_ptr());
        }
    }
}

/// gc2's probe, at MWCC_ORACLE_ALIAS=2: whether A may alias B, or OBJECT (KIND 0 may_alias, 1
/// may_alias_scheduling, 2 may_alias_object), is RESULT (`al.query`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_AliasQuery(
    kind: i32,
    a: *const u8,
    b: *const u8,
    object: *const u8,
    result: i32,
) {
    unsafe {
        let level = getenv(c"MWCC_ORACLE_ALIAS".as_ptr());
        if level.is_null() || *level != b'2' {
            return;
        }
        if object.is_null() {
            OracleTrace_Printf(
                c"al.query kind=%d a=%08x b=%08x result=%d\n".as_ptr(),
                kind,
                a as u32,
                b as u32,
                i32::from(result != 0),
            );
        } else {
            OracleTrace_Printf(
                c"al.query kind=%d a=%08x object=%08x result=%d name=%s\n".as_ptr(),
                kind,
                a as u32,
                object as u32,
                i32::from(result != 0),
                object_name(object),
            );
        }
    }
}

#[unsafe(export_name = "OracleRust_Snapshot")]
pub unsafe extern "C" fn snapshot(
    prefix: *const c_char,
    phase: *const c_char,
    function: *const u8,
) {
    unsafe {
        let link = link_name(function);
        let name = read::<*const u8>(function, 10).add(10).cast::<c_char>();
        OracleTrace_Printf(
            c"%s.%s function=%s link=%s\n".as_ptr(),
            prefix,
            phase,
            name,
            link,
        );
        let mut block = ptr::addr_of!(gPCodeBlocks).read_unaligned();
        let mut line_prefix = [0u8; 8];
        while !block.is_null() {
            OracleTrace_Printf(
                c"%s.block p=%08x index=%d weight=%d flags=%04x succ=".as_ptr(),
                prefix,
                block as u32,
                read::<i32>(block, 28),
                read::<i32>(block, 36),
                u32::from(read::<u16>(block, 42)),
            );
            let mut successor = read::<*const u8>(block, 16);
            while !successor.is_null() {
                OracleTrace_Printf(c"%08x,".as_ptr(), read::<*const u8>(successor, 4) as u32);
                successor = read(successor, 0);
            }
            OracleTrace_Printf(c"\n".as_ptr());
            // The internal C callers use the same short prefixes as Hooks.c (ra/st).
            sprintf(line_prefix.as_mut_ptr().cast(), c"%s.i".as_ptr(), prefix);
            let mut ins = read::<*const u8>(block, 20);
            while !ins.is_null() {
                instruction(line_prefix.as_ptr().cast(), ins);
                ins = read(ins, 0);
            }
            block = read(block, 0);
        }
        OracleTrace_Printf(c"%s.end\n".as_ptr(), prefix);
    }
}
