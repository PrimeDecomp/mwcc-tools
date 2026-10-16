use crate::abi::{OracleTrace_Printf, wanted};
use core::{
    mem::{align_of, offset_of, size_of},
    ptr,
};

#[repr(C, packed(2))]
struct IroLinearPrefix {
    node_type: u8,
    expression_type: u8,
    flags: u32,
    node_flags: u16,
    index: u16,
}
const _: () = {
    assert!(size_of::<*mut IroLinearPrefix>() == 4);
    assert!(size_of::<IroLinearPrefix>() == 10);
    assert!(align_of::<IroLinearPrefix>() == 2);
    assert!(offset_of!(IroLinearPrefix, flags) == 2);
    assert!(offset_of!(IroLinearPrefix, index) == 8);
};
unsafe extern "C" {
    fn fn_00460c90__retail(node: *mut IroLinearPrefix) -> u8;
}

#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00460c90(node: *mut IroLinearPrefix) -> u8 {
    unsafe {
        let result = fn_00460c90__retail(node);
        if result != 0 && wanted(c"MWCC_ORACLE_LOCALS") {
            let index = ptr::addr_of!((*node).index).read_unaligned();
            OracleTrace_Printf(c"lc.call node=%d\n".as_ptr(), i32::from(index));
        }
        result
    }
}
