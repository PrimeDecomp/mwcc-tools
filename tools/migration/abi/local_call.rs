//! Bounded ABI spike: the lc.call hook, using the retained C varargs output shim.
//! Compile as one no_std i686 COFF object; never link a host Rust runtime into MWCC.
#![no_std]

use core::{
    ffi::c_void,
    mem::{align_of, offset_of, size_of},
    ptr,
};

// IroLoop.h uses mac68k alignment. Only the prefix read by this hook is modelled.
#[repr(C, packed(2))]
pub struct IroLinearPrefix {
    node_type: u8,
    expression_type: u8,
    flags: u32,
    node_flags: u16,
    index: u16,
}
const _: () = {
    assert!(size_of::<*mut c_void>() == 4);
    assert!(size_of::<IroLinearPrefix>() == 10);
    assert!(align_of::<IroLinearPrefix>() == 2);
    assert!(offset_of!(IroLinearPrefix, flags) == 2);
    assert!(offset_of!(IroLinearPrefix, index) == 8);
};

unsafe extern "C" {
    fn fn_00460c90__retail(node: *mut IroLinearPrefix) -> u8;
    fn getenv(name: *const u8) -> *const u8;
    fn OracleTrace_Printf(format: *const u8, ...);
}

/// # Safety
/// MWCC supplies a valid live IROLinear and the matching retail function/runtime.
/// No references, allocations, unwinding or Rust-owned values cross this ABI.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fn_00460c90(node: *mut IroLinearPrefix) -> u8 {
    unsafe {
        let result = fn_00460c90__retail(node);
        if result != 0 {
            let enabled = getenv(c"MWCC_ORACLE_LOCALS".as_ptr().cast());
            if !enabled.is_null() && *enabled != 0 && *enabled != b'0' {
                let index = ptr::addr_of!((*node).index).read_unaligned();
                OracleTrace_Printf(c"lc.call node=%d\n".as_ptr().cast(), i32::from(index));
            }
        }
        result
    }
}
