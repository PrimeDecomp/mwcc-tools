//! Shared PCode observations. Historical layouts are checked by pcode_layout.c.
use crate::abi::{OracleTrace_Printf, read, sprintf};
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
