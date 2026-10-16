//! Scalarization observers and a cdecl entry that preserves the original caller.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    expression::{object_name, type_id, type_size},
    pcode,
};
use core::{arch::naked_asm, ptr};
unsafe extern "C" {
    fn create_temp_object__retail(ty: *const u8) -> *const u8;
    fn fn_004601f0__retail(node: *const u8, var: *const u8, offset: i32, ty: *const u8) -> i32;
    fn fn_00460780__retail();
    static mut class_data_parts: *const u8;
    static mut iro_pointer_analysis: u8;
    pub static mut data_005875b8: *const u8;
}
static mut IN_SCALARIZE: u8 = 0;

#[unsafe(naked)]
#[unsafe(no_mangle)]
unsafe extern "C" fn create_temp_object(_ty: *const u8) -> *const u8 {
    naked_asm!("push dword ptr [esp]", "push dword ptr [esp + 8]", "call {handler}", "add esp, 8", "ret", handler = sym create_temp);
}
unsafe extern "C" fn create_temp(ty: *const u8, caller: u32) -> *const u8 {
    unsafe {
        let object = create_temp_object__retail(ty);
        if wanted(c"MWCC_ORACLE_CSE") {
            OracleTrace_Printf(
                c"ce.object name=%s caller=%lx\n".as_ptr(),
                object_name(object, c"?"),
                caller,
            );
        }
        if ptr::addr_of!(IN_SCALARIZE).read() != 0 && wanted(c"MWCC_ORACLE_LOCALS") {
            OracleTrace_Printf(c"lc.temp name=%s\n".as_ptr(), object_name(object, c"?"));
        }
        object
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_004601f0(
    node: *const u8,
    var: *const u8,
    offset: i32,
    ty: *const u8,
) -> i32 {
    unsafe {
        if ptr::addr_of!(IN_SCALARIZE).read() != 0 && wanted(c"MWCC_ORACLE_LOCALS") {
            OracleTrace_Printf(
                c"lc.use node=%d kind=%d offset=%ld type=%d size=%ld var=%s\n".as_ptr(),
                i32::from(read::<u16>(node, 8)),
                i32::from(read::<u8>(node, 0)),
                offset,
                type_id(ty),
                type_size(ty),
                object_name(read(var, 2), c"?"),
            );
        }
        fn_004601f0__retail(node, var, offset, ty)
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00460780() {
    unsafe {
        ptr::addr_of_mut!(IN_SCALARIZE).write(1);
        fn_00460780__retail();
        ptr::addr_of_mut!(IN_SCALARIZE).write(0);
        if !wanted(c"MWCC_ORACLE_LOCALS") {
            return;
        }
        let function = ptr::addr_of!(data_005875b8).read_unaligned();
        OracleTrace_Printf(
            c"lc.pass pointers=%d link=%s\n".as_ptr(),
            i32::from(ptr::addr_of!(iro_pointer_analysis).read()),
            if function.is_null() {
                c"-".as_ptr()
            } else {
                pcode::link_name(function)
            },
        );
        let mut part = ptr::addr_of!(class_data_parts).read_unaligned();
        while !part.is_null() {
            let var = read::<*const u8>(part, 6);
            let object = read::<*const u8>(var, 2);
            let ty = read::<*const u8>(part, 26);
            let mut uses = 0i32;
            let mut usage = read::<*const u8>(part, 34);
            while !usage.is_null() {
                uses += 1;
                usage = read(usage, 4);
            }
            OracleTrace_Printf(c"lc.part blocked=%d noregister=%d bits=%ld..%ld type=%d size=%ld uses=%d var_size=%ld var=%s\n".as_ptr(),
                i32::from(read::<u8>(part, 4) & 1), i32::from(read::<i8>(var, 11)), read::<i32>(part, 18), read::<i32>(part, 22),
                type_id(ty), type_size(ty), uses, if object.is_null() { -1 } else { type_size(read(object, 14)) }, object_name(object, c"?"));
            part = read(part, 30);
        }
    }
}
