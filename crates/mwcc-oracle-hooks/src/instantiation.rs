//! Version-independent template/body queue observers; retail remains the engine.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    pcode::link_name,
};
use core::{arch::naked_asm, ffi::c_char, ptr};

unsafe extern "C" {
    static mut pendingInlineWork: *const u8;
    static mut pending_prec_nodes: *const u8;
    static mut deferredInlineNodes: *const u8;
    static mut cscope_currentfunc: *const u8;
    static mut data_005dfb62: *const u8;
    fn CInline_AddFunctionPrecNode__retail(
        function: *const u8,
        value: *const u8,
        key: *const u8,
        pair: *const u8,
        flag: u8,
    );
    fn CInline_AddSpecialization__retail(
        function: *const u8,
        definition: *const u8,
        specialization: *const u8,
    );
    fn CTemplateNew_InstantiateInlineTemplateObject__retail(object: *const u8) -> u8;
    fn CTemplateNew_InstantiateFunction__retail(
        definition: *const u8,
        specialization: *const u8,
        report: u8,
    ) -> u8;
    fn fn_0050f240__retail(object: *const u8);
    fn generate_inline_code__retail(object: *const u8, input: *const u8, mode: i8);
}
unsafe fn maybe_link(object: *const u8) -> *const c_char {
    unsafe {
        if object.is_null() {
            c"-".as_ptr()
        } else {
            link_name(object)
        }
    }
}
unsafe fn context() {
    unsafe {
        OracleTrace_Printf(
            c"from=%s ".as_ptr(),
            maybe_link(ptr::addr_of!(cscope_currentfunc).read()),
        );
        OracleTrace_Printf(
            c"expanding=%s ".as_ptr(),
            maybe_link(ptr::addr_of!(data_005dfb62).read()),
        );
    }
}
unsafe fn pending(flag: u8) -> *const u8 {
    unsafe {
        if flag != 0 {
            ptr::addr_of!(pendingInlineWork).read()
        } else {
            ptr::addr_of!(pending_prec_nodes).read()
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn CInline_AddFunctionPrecNode(
    function: *const u8,
    value: *const u8,
    key: *const u8,
    pair: *const u8,
    flag: u8,
) {
    unsafe {
        let before = pending(flag);
        CInline_AddFunctionPrecNode__retail(function, value, key, pair, flag);
        if !wanted(c"MWCC_ORACLE_INSTANTIATION") {
            return;
        }
        OracleTrace_Printf(
            c"tp.queue kind=0 pending=%d new=%d ".as_ptr(),
            i32::from(flag),
            i32::from(pending(flag) != before),
        );
        context();
        OracleTrace_Printf(c"link=%s\n".as_ptr(), link_name(function));
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn CInline_AddSpecialization(
    function: *const u8,
    definition: *const u8,
    specialization: *const u8,
) {
    unsafe {
        let before = ptr::addr_of!(pendingInlineWork).read();
        CInline_AddSpecialization__retail(function, definition, specialization);
        if !wanted(c"MWCC_ORACLE_INSTANTIATION") {
            return;
        }
        OracleTrace_Printf(
            c"tp.queue kind=2 pending=1 new=%d ".as_ptr(),
            i32::from(ptr::addr_of!(pendingInlineWork).read() != before),
        );
        context();
        OracleTrace_Printf(c"link=%s\n".as_ptr(), link_name(function));
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn CTemplateNew_InstantiateInlineTemplateObject(object: *const u8) -> u8 {
    unsafe {
        let result = CTemplateNew_InstantiateInlineTemplateObject__retail(object);
        if wanted(c"MWCC_ORACLE_INSTANTIATION") {
            OracleTrace_Printf(c"tp.need result=%d ".as_ptr(), i32::from(result));
            context();
            OracleTrace_Printf(c"link=%s\n".as_ptr(), link_name(object));
        }
        result
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn CTemplateNew_InstantiateFunction(
    definition: *const u8,
    specialization: *const u8,
    report: u8,
) -> u8 {
    unsafe {
        if wanted(c"MWCC_ORACLE_INSTANTIATION") {
            OracleTrace_Printf(c"tp.func ".as_ptr());
            context();
            let object = if specialization.is_null() {
                ptr::null()
            } else {
                read(specialization, 4)
            };
            OracleTrace_Printf(c"link=%s\n".as_ptr(), maybe_link(object));
        }
        CTemplateNew_InstantiateFunction__retail(definition, specialization, report)
    }
}
// Read EIP before any Rust prologue, just as the C wrapper's [ebp+4] does.
#[unsafe(naked)]
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_0050f240(_object: *const u8) {
    naked_asm!("push dword ptr [esp]", "push dword ptr [esp + 8]", "call {handler}", "add esp, 8", "ret", handler = sym emit);
}
unsafe extern "C" fn emit(object: *const u8, caller: u32) {
    unsafe {
        let before = ptr::addr_of!(deferredInlineNodes).read();
        fn_0050f240__retail(object);
        if wanted(c"MWCC_ORACLE_INSTANTIATION")
            && ptr::addr_of!(deferredInlineNodes).read() != before
        {
            OracleTrace_Printf(c"tp.emit caller=%lx ".as_ptr(), caller);
            context();
            OracleTrace_Printf(c"link=%s\n".as_ptr(), maybe_link(object));
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn generate_inline_code(object: *const u8, input: *const u8, mode: i8) {
    unsafe {
        if wanted(c"MWCC_ORACLE_INSTANTIATION") {
            OracleTrace_Printf(c"tp.gen link=%s\n".as_ptr(), maybe_link(object));
        }
        generate_inline_code__retail(object, input, mode);
    }
}
