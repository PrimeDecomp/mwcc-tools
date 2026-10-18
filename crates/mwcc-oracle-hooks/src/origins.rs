//! Existing inline-call ancestry and allocation/statement entry observers.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    cse, pcode,
};
use core::{ffi::c_char, mem::MaybeUninit, ptr};

const SIZE: usize = 0x10000;
#[derive(Clone, Copy)]
#[repr(C, packed(2))]
struct Origin {
    funcref: *const u8,
    parent: i32,
    link: *const c_char,
}
static mut ORIGINS: [Origin; SIZE] = [Origin {
    funcref: ptr::null(),
    parent: 0,
    link: ptr::null(),
}; SIZE];
static mut USED: i32 = 0;
static mut CURRENT: *const u8 = ptr::null();
unsafe extern "C" {
    fn fn_00506cd0__retail(function: *const u8);
    fn generate_inline_statements__retail(
        function: *const u8,
        tail: *const u8,
        args: *const u8,
        result: *const u8,
        return_label: *const u8,
        return_object: *const u8,
        append_statement: u8,
    ) -> *const u8;
    fn inline_statement__retail(statement: *const u8) -> *const u8;
}
unsafe fn reset() {
    unsafe {
        ptr::write_bytes(ptr::addr_of_mut!(ORIGINS), 0, 1);
        ptr::addr_of_mut!(USED).write(0);
    }
}
unsafe fn slot(index: usize) -> *mut u8 {
    unsafe {
        ptr::addr_of_mut!(ORIGINS)
            .cast::<Origin>()
            .add(index)
            .cast()
    }
}
#[unsafe(export_name = "OracleRust_OriginFind")]
pub unsafe extern "C" fn find(funcref: *const u8) -> i32 {
    unsafe {
        let mut index = ((funcref as usize) >> 2) & (SIZE - 1);
        let mut tries = 0;
        while tries < SIZE && !read::<*const u8>(slot(index), 0).is_null() {
            if read::<*const u8>(slot(index), 0) == funcref {
                return index as i32;
            }
            index = (index + 1) & (SIZE - 1);
            tries += 1;
        }
        -1
    }
}
unsafe fn add(funcref: *const u8, parent: i32, link: *const c_char) {
    unsafe {
        if ptr::addr_of!(USED).read() >= (SIZE / 4 * 3) as i32 {
            return;
        }
        let mut index = ((funcref as usize) >> 2) & (SIZE - 1);
        while !read::<*const u8>(slot(index), 0).is_null()
            && read::<*const u8>(slot(index), 0) != funcref
        {
            index = (index + 1) & (SIZE - 1);
        }
        if read::<*const u8>(slot(index), 0).is_null() {
            ptr::addr_of_mut!(USED).write(ptr::addr_of!(USED).read() + 1);
        }
        slot(index).cast::<*const u8>().write_unaligned(funcref);
        slot(index).add(4).cast::<i32>().write_unaligned(parent);
        slot(index)
            .add(8)
            .cast::<*const c_char>()
            .write_unaligned(link);
    }
}
#[unsafe(export_name = "OracleRust_OriginWalk")]
pub unsafe extern "C" fn walk(expr: *const u8, parent: i32, link: *const c_char, level: i32) {
    unsafe {
        if expr.is_null() || level > 200 {
            return;
        }
        match read::<u8>(expr, 0) {
            0..=8 | 48 | 49 => walk(read(expr, 14), parent, link, level + 1),
            9..=47 | 57 | 58 => {
                walk(read(expr, 14), parent, link, level + 1);
                walk(read(expr, 18), parent, link, level + 1);
            }
            53 | 76 => {
                walk(read(expr, 14), parent, link, level + 1);
                walk(read(expr, 18), parent, link, level + 1);
                walk(read(expr, 22), parent, link, level + 1);
            }
            54 | 55 => {
                let funcref = read::<*const u8>(expr, 14);
                if !funcref.is_null() && read::<u8>(funcref, 0) == 56 {
                    add(funcref, parent, link);
                } else {
                    walk(funcref, parent, link, level + 1);
                }
                let mut arg = read::<*const u8>(expr, 18);
                while !arg.is_null() {
                    walk(read(arg, 4), parent, link, level + 1);
                    arg = read(arg, 0);
                }
            }
            _ => {}
        }
    }
}
#[unsafe(export_name = "OracleRust_PrintChain")]
pub unsafe extern "C" fn print_chain(funcref: *const u8) {
    unsafe {
        let mut path = MaybeUninit::<[i32; 32]>::uninit();
        let path = path.as_mut_ptr().cast::<i32>();
        let mut count = 0;
        let mut entry = find(funcref);
        while entry >= 0 && count < 32 {
            path.add(count).write(entry);
            count += 1;
            entry = read(slot(entry as usize), 4);
        }
        while count > 0 {
            count -= 1;
            OracleTrace_Printf(
                c"%s%s".as_ptr(),
                read::<*const c_char>(slot(path.add(count).read() as usize), 8),
                if count > 0 { c"|" } else { c"" }.as_ptr(),
            );
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn generate_inline_statements(
    function: *const u8,
    tail: *const u8,
    args: *const u8,
    result: *const u8,
    return_label: *const u8,
    return_object: *const u8,
    append_statement: u8,
) -> *const u8 {
    unsafe {
        let parent = if result.is_null() {
            -1
        } else {
            find(read(result, 14))
        };
        let link = pcode::link_name(function);
        let last = generate_inline_statements__retail(
            function,
            tail,
            args,
            result,
            return_label,
            return_object,
            append_statement,
        );
        let mut statement = tail;
        while !statement.is_null() {
            if matches!(read::<u8>(statement, 4), 4..=8 | 15) {
                walk(read(statement, 10), parent, link, 0);
            }
            if statement == last {
                break;
            }
            statement = read(statement, 0);
        }
        last
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn inline_statement(statement: *const u8) -> *const u8 {
    unsafe {
        let saved = ptr::addr_of!(CURRENT).read();
        ptr::addr_of_mut!(CURRENT).write(statement);
        let result = inline_statement__retail(statement);
        ptr::addr_of_mut!(CURRENT).write(saved);
        result
    }
}
#[unsafe(export_name = "OracleRust_InlineLine")]
pub unsafe extern "C" fn inline_line() -> i32 {
    unsafe {
        let current = ptr::addr_of!(CURRENT).read();
        if current.is_null() {
            -1
        } else {
            read(current, 22)
        }
    }
}
/// gc2's probe: CInline_Expand is about to expand FUNCTION's body (`inl.expand`, MWCC_ORACLE_NAMES).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_Expand(function: *const u8) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_NAMES") {
            return;
        }
        // (HashNameNode.name is an inline tail at byte 10)
        let name = if function.is_null() || read::<*const u8>(function, 10).is_null() {
            c"?".as_ptr()
        } else {
            read::<*const u8>(function, 10).add(10).cast()
        };
        OracleTrace_Printf(c"inl.expand function=%s\n".as_ptr(), name);
    }
}
/// gc2's probe: the first scheduling pass is about to begin (`st.presched`, MWCC_ORACLE_STAGES).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_Presched(function: *const u8) {
    unsafe {
        if wanted(c"MWCC_ORACLE_STAGES") {
            pcode::snapshot(c"st".as_ptr(), c"presched".as_ptr(), function);
        }
    }
}
/// The function whose registers are being allocated.
pub static mut ALLOCATING: *const u8 = ptr::null();
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00506cd0(function: *const u8) {
    unsafe {
        ptr::addr_of_mut!(ALLOCATING).write(function);
        reset();
        cse::cse_vregs();
        pcode::snapshot(c"ra".as_ptr(), c"before".as_ptr(), function);
        let previous = crate::backend::context(function);
        crate::lifecycle::phase(3);
        fn_00506cd0__retail(function);
        crate::lifecycle::phase(4);
        crate::backend::context(previous);
        pcode::snapshot(c"ra".as_ptr(), c"after".as_ptr(), function);
    }
}
