//! Scalarization observers and a cdecl entry that preserves the original caller.
use crate::{
    abi::{OracleTrace_Printf, hex_into, read, wanted},
    expression::{object_name, type_id, type_size},
    pcode,
};
use core::ptr;
unsafe extern "C" {
    fn create_temp_object__retail(ty: *const u8) -> *const u8;
    fn fn_004601f0__retail(node: *const u8, var: *const u8, offset: i32, ty: *const u8) -> i32;
    fn fn_00460780__retail();
    static mut class_data_parts: *const u8;
    static mut iro_pointer_analysis: u8;
    pub static mut data_005875b8: *const u8;
}
static mut IN_SCALARIZE: u8 = 0;
static mut SCALAR_PASS: u32 = 0;
static mut SCALAR_BINDINGS: u32 = 0;
static mut SCALAR_FUNCTION: *const u8 = ptr::null();
static mut SCALAR_FUNCTION_ID: u32 = 0;
static mut SCALAR_ENABLED: bool = false;
static mut NAME_HEX: [u8; 513] = [0; 513];
#[derive(Clone, Copy)]
struct Pending {
    function: *const u8,
    passes: u32,
    bindings: u32,
    first: u32,
    last: u32,
    open: bool,
    valid: bool,
}
const EMPTY_PENDING: Pending = Pending {
    function: ptr::null(),
    passes: 0,
    bindings: 0,
    first: 0,
    last: 0,
    open: false,
    valid: false,
};
static mut PENDING: [Pending; 256] = [EMPTY_PENDING; 256];
static mut SCALAR_SLOT: usize = usize::MAX;

/// A missing summary means unknown, never zero frontend passes. Each entry is
/// consumed exactly once at the matching enclosing function end boundary.
pub(crate) unsafe fn take_summary(function: *const u8) -> Option<(u32, u32, u32, u32)> {
    unsafe {
        if function.is_null() {
            return None;
        }
        let entries = ptr::addr_of_mut!(PENDING).cast::<Pending>();
        for index in 0..256 {
            let entry = entries.add(index).read();
            if entry.function == function {
                entries.add(index).write(EMPTY_PENDING);
                return (entry.valid && !entry.open && entry.passes > 0).then_some((
                    entry.passes,
                    entry.bindings,
                    entry.first,
                    entry.last,
                ));
            }
        }
        None
    }
}
unsafe fn pending_begin(function: *const u8, pass: u32) {
    unsafe {
        let entries = ptr::addr_of_mut!(PENDING).cast::<Pending>();
        let mut found = usize::MAX;
        let mut free = usize::MAX;
        for index in 0..256 {
            let entry = entries.add(index).read();
            if entry.function == function && !function.is_null() {
                found = index;
                break;
            }
            if entry.function.is_null() && free == usize::MAX {
                free = index;
            }
        }
        if found == usize::MAX {
            found = free;
        }
        ptr::addr_of_mut!(SCALAR_SLOT).write(found);
        if found == usize::MAX || function.is_null() {
            return;
        }
        let slot = entries.add(found);
        let mut entry = slot.read();
        if entry.function.is_null() {
            entry = Pending {
                function,
                passes: 0,
                bindings: 0,
                first: pass,
                last: pass,
                open: false,
                valid: true,
            };
        }
        entry.valid &= !entry.open;
        if let Some(count) = entry.passes.checked_add(1) {
            entry.passes = count;
        } else {
            entry.valid = false;
        }
        entry.last = pass;
        entry.open = true;
        slot.write(entry);
    }
}
unsafe fn pending_end() {
    unsafe {
        let index = ptr::addr_of!(SCALAR_SLOT).read();
        if index < 256 {
            let slot = ptr::addr_of_mut!(PENDING).cast::<Pending>().add(index);
            let mut entry = slot.read();
            if let Some(count) = entry
                .bindings
                .checked_add(ptr::addr_of!(SCALAR_BINDINGS).read())
            {
                entry.bindings = count;
            } else {
                entry.valid = false;
            }
            entry.open = false;
            slot.write(entry);
        }
        ptr::addr_of_mut!(SCALAR_SLOT).write(usize::MAX);
    }
}

#[cfg(target_arch = "x86")]
#[unsafe(naked)]
#[unsafe(no_mangle)]
unsafe extern "C" fn create_temp_object(_ty: *const u8) -> *const u8 {
    // EBX is the retail scalarizer's current IROVarPart at its sole audited
    // temp-object call. Save the original value before Rust can use this register.
    core::arch::naked_asm!("push ebx", "push dword ptr [esp + 4]", "push dword ptr [esp + 12]", "call {handler}", "add esp, 12", "ret", handler = sym create_temp);
}
/// The scalarizer's call that gc2's probe announced (`include/compiler/OracleProbes.h`): its part.
static mut SOURCE_PART: *const u8 = ptr::null();
/// gc2's call of create_temp_object that makes the next temporary: its site (the low byte) and its function's copy
/// in the original (the next), as crate::temp_sites numbers them; 0 outside ORACLE_TEMP.
static mut TEMP_SITE: u32 = 0;

/// gc2's probe: IRO_ScalarizeClassDataMembers is about to make PART's temporary.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_ScalarTemporary(part: *const u8) {
    unsafe { ptr::addr_of_mut!(SOURCE_PART).write(part) }
}

/// gc2's probe: the call it wraps is the original's at SITE, or in its copy COPY (ORACLE_TEMP, ORACLE_TEMP_INLINED).
/// Returns what OracleProbe_TempEnd restores when it returns.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_TempBegin(site: u32, copy: u32) -> u32 {
    unsafe {
        let previous = ptr::addr_of!(TEMP_SITE).read();
        let mut current = previous;
        if site != 0 {
            current = (current & !0xff) | (site & 0xff);
        }
        if copy != 0 {
            current = (current & 0xff) | ((copy & 0xff) << 8);
        }
        ptr::addr_of_mut!(TEMP_SITE).write(current);
        previous
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleProbe_TempEnd(previous: *mut u32) {
    unsafe { ptr::addr_of_mut!(TEMP_SITE).write(*previous) }
}

// (WebAssembly has neither the return address nor EBX: the call is the one gc2's probe names, which crate::temp_sites
// gives the original's return address of, and the scalarizer's part is the one its probe announced)
#[cfg(target_arch = "wasm32")]
#[unsafe(no_mangle)]
unsafe extern "C" fn create_temp_object(ty: *const u8) -> *const u8 {
    unsafe {
        let part = ptr::addr_of!(SOURCE_PART).read();
        ptr::addr_of_mut!(SOURCE_PART).write(ptr::null());
        let site = ptr::addr_of!(TEMP_SITE).read();
        let caller = crate::temp_sites::site(
            site as u8,
            (site >> 8) as u8,
            crate::source_arenas::version(),
        );
        if caller == crate::temp_sites::INLINED {
            // (the original's own copy, which its hook does not see)
            return create_temp_object__retail(ty);
        }
        create_temp(ty, caller, part)
    }
}
unsafe extern "C" fn create_temp(ty: *const u8, caller: u32, part: *const u8) -> *const u8 {
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
        if ptr::addr_of!(IN_SCALARIZE).read() != 0 && ptr::addr_of!(SCALAR_ENABLED).read() {
            scalar_binding(ty, caller, part, object);
        }
        object
    }
}

unsafe fn scalar_gap(reason: *const core::ffi::c_char, site: u32) {
    unsafe {
        let index = ptr::addr_of!(SCALAR_SLOT).read();
        if index < 256 {
            ptr::addr_of_mut!(PENDING)
                .cast::<Pending>()
                .add(index)
                .cast::<u8>()
                .add(core::mem::offset_of!(Pending, valid))
                .cast::<bool>()
                .write(false);
        }
        OracleTrace_Printf(
            c"pv.scalar.gap schema=1 pass=%lu function=%lu function_object=%08x site=%lx reason=%s\n".as_ptr(),
            ptr::addr_of!(SCALAR_PASS).read(),
            ptr::addr_of!(SCALAR_FUNCTION_ID).read(),
            ptr::addr_of!(SCALAR_FUNCTION).read() as u32,
            site,
            reason,
        );
    }
}
unsafe fn scalar_binding(ty: *const u8, site: u32, part: *const u8, temporary: *const u8) {
    unsafe {
        if !matches!(site, 0x4608a8 | 0x460b28 | 0x461748)
            || part.is_null()
            || temporary.is_null()
            || ty.is_null()
        {
            scalar_gap(c"scalar_return_inputs".as_ptr(), site);
            return;
        }
        let variable = read::<*const u8>(part, 6);
        let source = read::<*const u8>(part, 0);
        if variable.is_null() || source.is_null() {
            scalar_gap(c"scalar_source_inputs".as_ptr(), site);
            return;
        }
        let object = read::<*const u8>(variable, 2);
        if object.is_null() {
            scalar_gap(c"scalar_source_object".as_ptr(), site);
            return;
        }
        let source_type = read::<*const u8>(object, 14);
        let source_info = read::<*const u8>(object, 42);
        let temporary_info = read::<*const u8>(temporary, 42);
        let temporary_type = read::<*const u8>(temporary, 14);
        let flags = read::<u8>(part, 4);
        let offset = read::<i32>(part, 10);
        let size = read::<i32>(part, 14);
        let source_size = type_size(source_type);
        if source_type.is_null()
            || source_info.is_null()
            || temporary_info.is_null()
            || flags & 1 != 0
            || read::<u8>(variable, 11) != 0
            || read::<u8>(object, 2) != 1
            || temporary_type != ty
            || read::<*const u8>(part, 26) != ty
            || type_size(ty) != size
            || !matches!(type_id(ty), 1 | 2 | 11)
            || !matches!(type_id(source_type), 4 | 5 | 12)
            || offset < 0
            || size <= 0
            || offset.checked_add(size).is_none_or(|end| end > source_size)
        {
            scalar_gap(c"scalar_binding_layout".as_ptr(), site);
            return;
        }
        let element = if type_id(source_type) == 12 {
            read::<*const u8>(source_type, 6)
        } else {
            ptr::null()
        };
        let name = read::<*const u8>(object, 10);
        let text = if name.is_null() {
            ptr::null()
        } else {
            name.add(10)
        };
        let hex = ptr::addr_of_mut!(NAME_HEX).cast::<u8>();
        let (_, complete) = hex_into(hex, text, 256);
        let binding = ptr::addr_of!(SCALAR_BINDINGS).read().wrapping_add(1);
        ptr::addr_of_mut!(SCALAR_BINDINGS).write(binding);
        OracleTrace_Printf(c"pv.scalar.binding schema=1 pass=%lu binding=%lu function=%lu function_object=%08x site=%lx part=%08x source_linear=%08x source_index=%lu source_kind=%lu variable=%08x source_object=%08x source_info=%08x source_type=%08x source_type_kind=%d source_type_size=%ld source_datatype=%lu source_storage=%lu source_name_known=%d source_name_complete=%d source_name_hex=%s byte_offset=%ld byte_size=%ld bit_first=%ld bit_last=%ld flags=%lu temporary=%08x temporary_info=%08x temporary_type=%08x temporary_type_kind=%d temporary_type_size=%ld source_element_known=%d source_element_type_kind=%d source_element_type_size=%ld\n".as_ptr(),
            ptr::addr_of!(SCALAR_PASS).read(),binding,ptr::addr_of!(SCALAR_FUNCTION_ID).read(),ptr::addr_of!(SCALAR_FUNCTION).read() as u32,site,
            part as u32,source as u32,u32::from(read::<u16>(source,8)),u32::from(read::<u8>(source,0)),variable as u32,
            object as u32,source_info as u32,source_type as u32,type_id(source_type),source_size,
            u32::from(read::<u8>(object,2)),u32::from(read::<u16>(object,22)),i32::from(!name.is_null()),
            i32::from(complete),hex.cast::<core::ffi::c_char>(),
            offset,size,read::<i32>(part,18),read::<i32>(part,22),u32::from(flags),temporary as u32,
            temporary_info as u32,temporary_type as u32,type_id(temporary_type),type_size(temporary_type),
            i32::from(!element.is_null()),type_id(element),type_size(element));
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
        let enabled = crate::lifecycle::enabled();
        ptr::addr_of_mut!(SCALAR_ENABLED).write(enabled);
        if enabled {
            let function = ptr::addr_of!(data_005875b8).read_unaligned();
            let pass = ptr::addr_of!(SCALAR_PASS).read().wrapping_add(1);
            ptr::addr_of_mut!(SCALAR_PASS).write(pass);
            ptr::addr_of_mut!(SCALAR_FUNCTION).write(function);
            let function_id = crate::lifecycle::function_for_object(function);
            ptr::addr_of_mut!(SCALAR_FUNCTION_ID).write(function_id);
            ptr::addr_of_mut!(SCALAR_BINDINGS).write(0);
            pending_begin(function, pass);
            OracleTrace_Printf(c"pv.scalar.begin schema=1 pass=%lu function=%lu function_object=%08x coverage=guarded_temp_returns_v1 link=%s\n".as_ptr(),
                pass,function_id,function as u32,if function.is_null(){c"?".as_ptr()}else{pcode::link_name(function)});
            if function.is_null() {
                scalar_gap(c"scalar_function_object".as_ptr(), 0);
            }
            if function_id == 0 {
                scalar_gap(c"scalar_function_context".as_ptr(), 0);
            }
            if ptr::addr_of!(SCALAR_SLOT).read() == usize::MAX {
                scalar_gap(c"scalar_summary_capacity".as_ptr(), 0);
            }
        }
        ptr::addr_of_mut!(IN_SCALARIZE).write(1);
        fn_00460780__retail();
        ptr::addr_of_mut!(IN_SCALARIZE).write(0);
        if enabled {
            OracleTrace_Printf(c"pv.scalar.end schema=1 pass=%lu function=%lu function_object=%08x bindings=%lu coverage=guarded_temp_returns_v1\n".as_ptr(),
                ptr::addr_of!(SCALAR_PASS).read(),ptr::addr_of!(SCALAR_FUNCTION_ID).read(),ptr::addr_of!(SCALAR_FUNCTION).read() as u32,ptr::addr_of!(SCALAR_BINDINGS).read());
            pending_end();
        }
        ptr::addr_of_mut!(SCALAR_ENABLED).write(false);
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
