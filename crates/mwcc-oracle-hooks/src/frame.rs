//! Frame observation. Offsets are checked by abi/frame_layout.c on all versions.
use crate::abi::{OracleTrace_Printf, read};
use core::{ffi::CStr, ptr};

unsafe extern "C" {
    fn fn_004f7fb0__retail();
    pub static mut locals: *const u8;
    pub static mut temporary_objects: *const u8;
}

// These are byte views of the historical packed records, never Rust references.
// ObjectList and TemporaryObjectEntry have identical fields at 0/4, but the
// latter has alignment 1. HashNameNode.name is an inline tail at byte 10.
unsafe fn objects(kind: &CStr, mut list: *const u8) {
    unsafe {
        while !list.is_null() {
            let object = read::<*const u8>(list, 4);
            if !object.is_null() && read::<u8>(object, 2) == 1 {
                let name = read::<*const u8>(object, 10);
                let ty = read::<*const u8>(object, 14);
                let info = read::<*const u8>(object, 42);
                let name = if name.is_null() {
                    c"?".as_ptr()
                } else {
                    name.add(10).cast()
                };
                let size = if ty.is_null() { -1 } else { read::<i32>(ty, 2) };
                let (register, used) = if info.is_null() {
                    (-1, -1)
                } else {
                    (
                        i32::from(read::<i16>(info, 38)),
                        i32::from(read::<u8>(info, 35)),
                    )
                };
                OracleTrace_Printf(
                    c"fr.%s name=%s offset=%d size=%d reg=%d used=%d\n".as_ptr(),
                    kind.as_ptr(),
                    name,
                    read::<i32>(object, 46),
                    size,
                    register,
                    used,
                );
            }
            list = read(list, 0);
        }
    }
}

#[cfg(target_arch = "wasm32")]
unsafe extern "C" {
    // (gc2's names: the WebAssembly module has no x86 code to find them in)
    static linkage_area_size: i32;
    static parameter_area_size: i32;
    static local_data_size: i32;
    static nonvolatile_save_size: i32;
    static frame_size: i32;
}

#[cfg(target_arch = "wasm32")]
unsafe fn layout() {
    unsafe {
        OracleTrace_Printf(
            c"fr.layout linkage=%d outgoing=%d locals=%d above=%d size=%d
"
            .as_ptr(),
            ptr::addr_of!(linkage_area_size).read_unaligned(),
            ptr::addr_of!(parameter_area_size).read_unaligned(),
            ptr::addr_of!(local_data_size).read_unaligned(),
            ptr::addr_of!(nonvolatile_save_size).read_unaligned(),
            ptr::addr_of!(frame_size).read_unaligned(),
        );
    }
}

#[cfg(not(target_arch = "wasm32"))]
unsafe fn layout() {
    unsafe {
        let code = fn_004f7fb0__retail as *const () as *const u8;
        for (offset, expected) in [
            (0x2da, 0xc7),
            (0x2db, 0x05),
            (0x2e0, 8),
            (0x2e4, 0x8b),
            (0x2e5, 0x15),
            (0x2ed, 0x03),
            (0x2ee, 0x15),
            (0x2f3, 0xa1),
            (0x2fa, 0xa3),
        ] {
            if *code.add(offset) != expected {
                return;
            }
        }
        OracleTrace_Printf(
            c"fr.layout linkage=%d outgoing=%d locals=%d above=%d size=%d\n".as_ptr(),
            read::<*const i32>(code, 0x2dc).read_unaligned(),
            read::<*const i32>(code, 0x2e6).read_unaligned(),
            read::<*const i32>(code, 0x2ef).read_unaligned(),
            read::<*const i32>(code, 0x2f4).read_unaligned(),
            read::<*const i32>(code, 0x2fb).read_unaligned(),
        );
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn fn_004f7fb0() {
    unsafe {
        fn_004f7fb0__retail();
        OracleTrace_Printf(c"fr.begin\n".as_ptr());
        layout();
        objects(c"local", ptr::addr_of!(locals).read_unaligned());
        objects(c"temp", ptr::addr_of!(temporary_objects).read_unaligned());
        OracleTrace_Printf(c"fr.end\n".as_ptr());
    }
}
