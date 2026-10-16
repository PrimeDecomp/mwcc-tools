//! The legacy bounded expression spelling, including its depth/length rules.
use crate::abi::{read, sprintf, strcat, strlen};
use core::{
    ffi::{CStr, c_char},
    mem::MaybeUninit,
};

pub unsafe fn object_name(object: *const u8, fallback: &CStr) -> *const c_char {
    unsafe {
        if object.is_null() {
            return fallback.as_ptr();
        }
        let name = read::<*const u8>(object, 10);
        if name.is_null() {
            fallback.as_ptr()
        } else {
            name.add(10).cast()
        }
    }
}
pub unsafe fn type_id(ty: *const u8) -> i32 {
    unsafe {
        if ty.is_null() {
            -1
        } else {
            i32::from(read::<u8>(ty, 0))
        }
    }
}
pub unsafe fn type_size(ty: *const u8) -> i32 {
    unsafe { if ty.is_null() { -1 } else { read(ty, 2) } }
}

#[unsafe(export_name = "OracleRust_DescribeExpression")]
pub unsafe extern "C" fn describe(buffer: *mut c_char, size: usize, expr: *const u8, depth: i32) {
    unsafe {
        if strlen(buffer) + 8 >= size {
            return;
        }
        if expr.is_null() {
            strcat(buffer, c"?".as_ptr());
            return;
        }
        if depth <= 0 {
            strcat(buffer, c"..".as_ptr());
            return;
        }
        let mut piece = MaybeUninit::<[u8; 64]>::uninit();
        let text = piece.as_mut_ptr().cast::<c_char>();
        match read::<u8>(expr, 0) {
            56 => {
                let object = read::<*const u8>(expr, 14);
                let name = read::<*const u8>(object, 10);
                sprintf(
                    text,
                    c"%.48s".as_ptr(),
                    if name.is_null() {
                        c"?".as_ptr()
                    } else {
                        name.add(10).cast::<c_char>()
                    },
                );
                strcat(buffer, text);
            }
            4 => {
                strcat(buffer, c"*".as_ptr());
                describe(buffer, size, read(expr, 14), depth - 1);
            }
            8 | 48 => describe(buffer, size, read(expr, 14), depth),
            kind @ (15 | 41 | 9) => {
                strcat(buffer, c"(".as_ptr());
                describe(buffer, size, read(expr, 14), depth - 1);
                strcat(
                    buffer,
                    match kind {
                        15 => c"+",
                        41 => c",",
                        _ => c"*",
                    }
                    .as_ptr(),
                );
                describe(buffer, size, read(expr, 18), depth - 1);
                strcat(buffer, c")".as_ptr());
            }
            54 => {
                let funcref = read::<*const u8>(expr, 14);
                if !funcref.is_null() && read::<u8>(funcref, 0) == 56 {
                    describe(buffer, size, funcref, 1);
                } else {
                    strcat(buffer, c"call".as_ptr());
                }
                strcat(buffer, c"(".as_ptr());
                let arguments = read::<*const u8>(expr, 18);
                if !arguments.is_null() {
                    describe(buffer, size, read(arguments, 4), depth - 1);
                    if !read::<*const u8>(arguments, 0).is_null() {
                        strcat(buffer, c",..".as_ptr());
                    }
                }
                strcat(buffer, c")".as_ptr());
            }
            50 => {
                sprintf(text, c"%ld".as_ptr(), read::<i32>(expr, 18));
                strcat(buffer, text);
            }
            kind => {
                sprintf(text, c"#%d".as_ptr(), i32::from(kind));
                strcat(buffer, text);
            }
        }
    }
}
