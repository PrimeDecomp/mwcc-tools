//! CSE/propagation and dump observers. No Rust formatting or allocator is used.
use crate::{
    abi::{
        OracleTrace_Printf, fopen, getenv, read, sprintf, strcat, strlen, strncmp, vfprintf, wanted,
    },
    expression::{describe, object_name},
    frame, pcode, scalar,
};
use core::{
    ffi::{CStr, c_char},
    mem::MaybeUninit,
    ptr,
};
unsafe extern "C" {
    fn fn_00454030__retail();
    fn IRO_CommonSubs__retail();
    fn move_common_sub__retail(group: *const u8);
    static mut iro_dump_output: *mut u8;
    static mut INT_005882b8: u8;
    static mut linear_head: *const u8;
}
static mut FUNCTION: *const u8 = ptr::null();
static mut PASS: i32 = 0;
static mut MOVED: i32 = 0;

#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00454030() {
    unsafe {
        let path = getenv(c"MWCC_ORACLE_IRO_DUMP".as_ptr());
        if !path.is_null() && *path != 0 {
            if ptr::addr_of!(iro_dump_output).read_unaligned().is_null() {
                ptr::addr_of_mut!(iro_dump_output).write_unaligned(fopen(path, c"a".as_ptr()));
            }
            ptr::addr_of_mut!(INT_005882b8).write(u8::from(
                !ptr::addr_of!(iro_dump_output).read_unaligned().is_null(),
            ));
            return;
        }
        fn_00454030__retail();
    }
}
unsafe fn line(linear: *const u8) -> i32 {
    unsafe {
        if linear.is_null() {
            return -1;
        }
        let stmt = read::<*const u8>(linear, 10);
        if stmt.is_null() { -1 } else { read(stmt, 22) }
    }
}
unsafe fn describe_linear(buffer: *mut c_char, size: usize, linear: *const u8, depth: i32) {
    unsafe {
        if strlen(buffer) + 12 >= size {
            return;
        }
        if linear.is_null() {
            strcat(buffer, c"?".as_ptr());
            return;
        }
        if depth <= 0 {
            strcat(buffer, c"..".as_ptr());
            return;
        }
        let mut piece = MaybeUninit::<[u8; 32]>::uninit();
        let text = piece.as_mut_ptr().cast::<c_char>();
        let kind = read::<u8>(linear, 0);
        let index = i32::from(read::<u16>(linear, 8));
        match kind {
            1 => describe(buffer, size, read(linear, 32), 3),
            2 | 3 => {
                sprintf(
                    text,
                    c"(%d#%d ".as_ptr(),
                    i32::from(read::<u8>(linear, 1)),
                    index,
                );
                strcat(buffer, text);
                describe_linear(buffer, size, read(linear, 32), depth - 1);
                if kind == 3 {
                    strcat(buffer, c" ".as_ptr());
                    describe_linear(buffer, size, read(linear, 36), depth - 1);
                }
                strcat(buffer, c")".as_ptr());
            }
            _ => {
                sprintf(text, c"[kind%d#%d]".as_ptr(), i32::from(kind), index);
                strcat(buffer, text);
            }
        }
    }
}
unsafe fn linear_list() {
    unsafe {
        let mut linear = ptr::addr_of!(linear_head).read_unaligned();
        while !linear.is_null() {
            let mut text = MaybeUninit::<[u8; 160]>::uninit();
            let buffer = text.as_mut_ptr().cast::<c_char>();
            buffer.write(0);
            let kind = read::<u8>(linear, 0);
            if kind == 1 {
                describe(buffer, 160, read(linear, 32), 3);
            }
            let a = if matches!(kind, 2 | 3) {
                read::<*const u8>(linear, 32)
            } else {
                ptr::null()
            };
            let b = if kind == 3 {
                read::<*const u8>(linear, 36)
            } else {
                ptr::null()
            };
            let expr = read::<*const u8>(linear, 18);
            OracleTrace_Printf(
                c"ce.l i=%d kind=%d op=%d a=%d b=%d line=%ld expr=%d text=%s\n".as_ptr(),
                i32::from(read::<u16>(linear, 8)),
                i32::from(kind),
                i32::from(read::<u8>(linear, 1)),
                if a.is_null() {
                    -1
                } else {
                    i32::from(read::<u16>(a, 8))
                },
                if b.is_null() {
                    -1
                } else {
                    i32::from(read::<u16>(b, 8))
                },
                line(linear),
                if expr.is_null() {
                    -1
                } else {
                    i32::from(read::<u16>(expr, 2))
                },
                buffer,
            );
            linear = read(linear, 52);
        }
    }
}
#[unsafe(export_name = "IRO_CommonSubs")]
unsafe extern "C" fn common_subs() {
    unsafe {
        let level = getenv(c"MWCC_ORACLE_CSE".as_ptr());
        if !wanted(c"MWCC_ORACLE_CSE") {
            IRO_CommonSubs__retail();
            return;
        }
        let function = ptr::addr_of!(scalar::data_005875b8).read_unaligned();
        if ptr::addr_of!(FUNCTION).read() != function {
            ptr::addr_of_mut!(FUNCTION).write(function);
            ptr::addr_of_mut!(PASS).write(0);
        }
        let pass = ptr::addr_of!(PASS).read() + 1;
        ptr::addr_of_mut!(PASS).write(pass);
        OracleTrace_Printf(
            c"ce.pass n=%d link=%s\n".as_ptr(),
            pass,
            if function.is_null() {
                c"-".as_ptr()
            } else {
                pcode::link_name(function)
            },
        );
        if *level == b'2' {
            linear_list();
        }
        IRO_CommonSubs__retail();
        OracleTrace_Printf(c"ce.end\n".as_ptr());
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn move_common_sub(group: *const u8) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_CSE") {
            move_common_sub__retail(group);
            return;
        }
        let mut tree = MaybeUninit::<[u8; 512]>::uninit();
        let buffer = tree.as_mut_ptr().cast::<c_char>();
        buffer.write(0);
        let linear = read::<*const u8>(group, 4);
        describe_linear(buffer, 512, linear, 8);
        OracleTrace_Printf(
            c"ce.move expr=%d line=%ld tree=%s\n".as_ptr(),
            if linear.is_null() {
                -1
            } else {
                i32::from(read::<u16>(linear, 8))
            },
            line(linear),
            buffer,
        );
        ptr::addr_of_mut!(MOVED).write(0);
        move_common_sub__retail(group);
        let temp = read::<*const u8>(group, 8);
        OracleTrace_Printf(
            c"ce.temp name=%s path=%s\n".as_ptr(),
            object_name(temp, c"-"),
            if temp.is_null() {
                c"none"
            } else if ptr::addr_of!(MOVED).read() != 0 {
                c"comma"
            } else {
                c"replace"
            }
            .as_ptr(),
        );
    }
}
unsafe fn propagation(kind: &CStr, index: i32) {
    unsafe {
        let mut linear = ptr::addr_of!(linear_head).read_unaligned();
        while !linear.is_null() && i32::from(read::<u16>(linear, 8)) != index {
            linear = read(linear, 52);
        }
        let mut tree = MaybeUninit::<[u8; 512]>::uninit();
        let buffer = tree.as_mut_ptr().cast::<c_char>();
        buffer.write(0);
        if !linear.is_null() {
            describe_linear(buffer, 512, linear, 6);
        }
        OracleTrace_Printf(
            c"pp.%s at=%d line=%ld tree=%s\n".as_ptr(),
            kind.as_ptr(),
            index,
            if linear.is_null() { 0 } else { line(linear) },
            buffer,
        );
    }
}
#[cfg(target_arch = "x86")]
#[unsafe(naked)]
#[unsafe(export_name = "IroDump_Print")]
unsafe extern "C" fn dump_print(_message: *const c_char) {
    core::arch::naked_asm!("lea eax, [esp + 8]", "push eax", "push dword ptr [esp + 8]", "call {handler}", "add esp, 8", "ret", handler = sym dump);
}
// (WebAssembly: wasm/src/oracle.c's variadic IroDump_Print passes its va_list, a pointer to the arguments)
#[cfg_attr(target_arch = "wasm32", unsafe(export_name = "IroDump_PrintArguments"))]
unsafe extern "C" fn dump(message: *const c_char, args: *const u8) {
    unsafe {
        if strncmp(message, c"Moving common sub".as_ptr(), 17) == 0 {
            ptr::addr_of_mut!(MOVED).write(1);
        }
        if strncmp(message, c"Replacing common sub at".as_ptr(), 23) == 0
            && wanted(c"MWCC_ORACLE_CSE")
        {
            OracleTrace_Printf(
                c"ce.replace at=%d with=%d\n".as_ptr(),
                read::<i32>(args, 0),
                read::<i32>(args, 4),
            );
        }
        if wanted(c"MWCC_ORACLE_CSE") {
            if strncmp(message, c"Found propagatable assignment".as_ptr(), 29) == 0 {
                propagation(c"copy", read(args, 0));
            } else if strncmp(message, c"Found propagatable expression".as_ptr(), 29) == 0 {
                propagation(c"expression", read(args, 0));
            } else if strncmp(message, c"Removing dead assignment".as_ptr(), 24) == 0 {
                propagation(c"dead", read(args, 0));
            }
        }
        if ptr::addr_of!(INT_005882b8).read() != 0 {
            vfprintf(
                ptr::addr_of!(iro_dump_output).read_unaligned(),
                message,
                args,
            );
        }
    }
}
unsafe fn vregs(mut list: *const u8) {
    unsafe {
        while !list.is_null() {
            let object = read::<*const u8>(list, 4);
            if !object.is_null() && read::<u8>(object, 2) == 1 {
                let info = read::<*const u8>(object, 42);
                if !info.is_null() {
                    OracleTrace_Printf(
                        c"ce.vreg name=%s reg=%d class=%d noregister=%d\n".as_ptr(),
                        object_name(object, c"?"),
                        i32::from(read::<i16>(info, 38)),
                        i32::from(read::<u8>(info, 37)),
                        i32::from(read::<u8>(info, 34)),
                    );
                }
            }
            list = read(list, 0);
        }
    }
}
#[unsafe(export_name = "cse_vregs")]
pub(crate) unsafe extern "C" fn cse_vregs() {
    unsafe {
        if !wanted(c"MWCC_ORACLE_CSE") {
            return;
        }
        vregs(ptr::addr_of!(frame::locals).read_unaligned());
        vregs(ptr::addr_of!(frame::temporary_objects).read_unaligned());
    }
}
