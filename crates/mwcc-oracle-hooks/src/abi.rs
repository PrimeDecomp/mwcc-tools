use core::{ffi::CStr, ptr};
unsafe extern "C" {
    pub fn OracleTrace_Printf(format: *const core::ffi::c_char, ...);
    #[link_name = "getenv"]
    fn retail_getenv(name: *const core::ffi::c_char) -> *const u8;
    pub fn sprintf(output: *mut core::ffi::c_char, format: *const core::ffi::c_char, ...) -> i32;
    pub fn strncmp(a: *const core::ffi::c_char, b: *const core::ffi::c_char, size: usize) -> i32;
    pub fn fopen(path: *const u8, mode: *const core::ffi::c_char) -> *mut u8;
    pub fn vfprintf(file: *mut u8, format: *const core::ffi::c_char, arguments: *const u8) -> i32;
}
// LLVM recognizes getenv and can merge repeated queries. The C observers query
// at distinct points, including twice at CSE entry. An opaque volatile function
// pointer preserves that order and permits options to change between queries.
static ENVIRONMENT_QUERY: unsafe extern "C" fn(*const core::ffi::c_char) -> *const u8 =
    retail_getenv;
pub unsafe fn getenv(name: *const core::ffi::c_char) -> *const u8 {
    unsafe { ptr::addr_of!(ENVIRONMENT_QUERY).read_volatile()(name) }
}
// The retail compiler inlines strlen and exports no callable copy. Volatile
// byte reads keep LLVM from synthesizing an unresolved strlen library call.
pub unsafe fn strlen(value: *const core::ffi::c_char) -> usize {
    unsafe {
        let mut length = 0;
        while value.add(length).read_volatile() != 0 {
            length += 1;
        }
        length
    }
}
// LLVM turns constant strcat calls into strlen plus stores, even when our own
// length loop is volatile. Keep appends local too; the legacy image has no
// strlen export and must not acquire a second C runtime.
pub unsafe fn strcat(output: *mut core::ffi::c_char, value: *const core::ffi::c_char) {
    unsafe {
        let mut destination = output.add(strlen(output));
        let mut source = value;
        loop {
            let byte = source.read_volatile();
            destination.write_volatile(byte);
            if byte == 0 {
                return;
            }
            source = source.add(1);
            destination = destination.add(1);
        }
    }
}
pub unsafe fn wanted(name: &CStr) -> bool {
    unsafe {
        let value = getenv(name.as_ptr());
        !value.is_null() && *value != 0 && *value != b'0'
    }
}
/// Read a packed field or unaligned x86 instruction operand without making references.
pub unsafe fn read<T: Copy>(base: *const u8, offset: usize) -> T {
    unsafe { ptr::read_unaligned(base.add(offset).cast::<T>()) }
}
