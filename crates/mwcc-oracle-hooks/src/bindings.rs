//! Existing inline formal/actual bindings, with packed layouts checked in C.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    expression::{describe, object_name, type_id, type_size},
    pcode::link_name,
};
use core::{ffi::c_char, mem::MaybeUninit, ptr};
unsafe extern "C" {
    static mut data_0058245a: *const u8;
    fn setup_inline_locals_and_arguments__retail(
        function: *const u8,
        info: *const u8,
        arguments: *const u8,
    ) -> *const u8;
}
unsafe fn name(value: *const u8) -> *const c_char {
    if value.is_null() {
        c"?".as_ptr()
    } else {
        unsafe { value.add(10).cast() }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn setup_inline_locals_and_arguments(
    function: *const u8,
    info: *const u8,
    arguments: *const u8,
) -> *const u8 {
    unsafe {
        let result = setup_inline_locals_and_arguments__retail(function, info, arguments);
        if !wanted(c"MWCC_ORACLE_LOCALS") {
            return result;
        }
        let mut index = 0_i32;
        while index < i32::from(read::<i16>(info, 0)) {
            let slot = ptr::addr_of!(data_0058245a).read().add(index as usize * 12);
            let formal = read::<*const u8>(info, 2).add(index as usize * 16);
            let variable = read::<*const u8>(slot, 0);
            let mode = if read::<u8>(formal, 13) == 0 {
                c"unused"
            } else if !variable.is_null() {
                c"local"
            } else if !read::<*const u8>(slot, 8).is_null() {
                c"reference"
            } else {
                c"substituted"
            };
            let function_type = read::<*const u8>(function, 14);
            let mut declared = if read::<u8>(function_type, 0) == 6 {
                read::<*const u8>(function_type, 6)
            } else {
                ptr::null()
            };
            while !declared.is_null()
                && read::<*const u8>(declared, 4) != read::<*const u8>(formal, 0)
            {
                declared = read(declared, 0);
            }
            let mut argument = read::<*const u8>(slot, 4);
            if argument.is_null() {
                let mut list = arguments;
                let mut n = 0;
                while !list.is_null() && n < index {
                    list = read(list, 0);
                    n += 1;
                }
                argument = if list.is_null() {
                    ptr::null()
                } else {
                    read(list, 4)
                };
            }
            let mut buffer = MaybeUninit::<[u8; 256]>::uninit();
            let expression = buffer.as_mut_ptr().cast::<c_char>();
            expression.write(0);
            describe(expression, 248, argument, 4);
            loop {
                if argument.is_null() {
                    break;
                }
                match read::<u8>(argument, 0) {
                    41 => argument = read(argument, 18),
                    8 | 48 => argument = read(argument, 14),
                    54 => {
                        let args = read::<*const u8>(argument, 18);
                        if args.is_null() {
                            break;
                        }
                        let first = read::<*const u8>(args, 4);
                        if read::<u8>(first, 0) != 56 {
                            break;
                        }
                        argument = first;
                    }
                    _ => break,
                }
            }
            let object = if argument.is_null() {
                ptr::null()
            } else if read::<u8>(argument, 0) == 56 {
                read(argument, 14)
            } else if read::<u8>(argument, 0) == 4 && read::<u8>(read(argument, 14), 0) == 56 {
                read(read(argument, 14), 14)
            } else {
                ptr::null()
            };
            let ty = read::<*const u8>(formal, 4);
            let declared_type = if declared.is_null() {
                ptr::null()
            } else {
                read(declared, 12)
            };
            OracleTrace_Printf(c"lc.arg index=%d name=%s type=%d size=%ld ref=%d dirty=%d mode=%s var=%s object=%s declared=%d declared_size=%ld generated=%d argument=%d expression=%s link=%s\n".as_ptr(),
                index, name(read(formal, 0)), type_id(ty), type_size(ty),
                i32::from(!ty.is_null() && read::<u8>(ty, 0) == 11 && read::<u32>(ty, 10) & 0x20 != 0),
                i32::from(read::<u8>(formal, 14)), mode.as_ptr(), object_name(variable, c"-"), object_name(object, c"-"),
                type_id(declared_type), type_size(declared_type),
                i32::from(read::<u8>(function_type, 0) == 6 && read::<u32>(function_type, 22) & 0x100 != 0),
                if argument.is_null() { -1 } else { i32::from(read::<u8>(argument, 0)) },
                if expression.read() == 0 { c"-".as_ptr() } else { expression },
                if function.is_null() { c"-".as_ptr() } else { link_name(function) });
            index += 1;
        }
        result
    }
}
