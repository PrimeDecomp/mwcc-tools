//! The legacy output API, in a separate object from its variadic Rust callers.
use core::{arch::naked_asm, ffi::c_char, ptr};

unsafe extern "C" {
    fn getenv(name: *const c_char) -> *const c_char;
    fn fopen(path: *const c_char, mode: *const c_char) -> *mut u8;
    fn vfprintf(file: *mut u8, format: *const c_char, arguments: *const u8) -> i32;
    fn fflush(file: *mut u8) -> i32;
    static mut __files: u8;
}
static mut FILE: *mut u8 = ptr::null_mut();
static mut OPENED: i32 = 0;
// GC 1.3/1.3.2/2.7 use the same 84-byte MSL FILE; stderr is __files[2].
const STDERR_OFFSET: usize = 168;

#[unsafe(export_name = "OracleTrace_Enabled")]
extern "C" fn enabled() -> i32 {
    1
}

#[unsafe(naked)]
#[unsafe(export_name = "OracleTrace_Printf")]
unsafe extern "C" fn printf(_format: *const c_char) {
    naked_asm!("lea eax, [esp + 8]", "push eax", "push dword ptr [esp + 8]", "call {handler}", "add esp, 8", "ret", handler = sym write);
}

unsafe extern "C" fn write(format: *const c_char, arguments: *const u8) {
    unsafe {
        if ptr::addr_of!(OPENED).read() == 0 {
            let path = getenv(c"MWCC_ORACLE_TRACE_FILE".as_ptr());
            ptr::addr_of_mut!(OPENED).write(1);
            let file = if path.is_null() {
                ptr::null_mut()
            } else {
                fopen(path, c"a".as_ptr())
            };
            ptr::addr_of_mut!(FILE).write(if file.is_null() {
                ptr::addr_of_mut!(__files).wrapping_add(STDERR_OFFSET)
            } else {
                file
            });
        }
        let file = ptr::addr_of!(FILE).read();
        vfprintf(file, format, arguments);
        // The C API flushes even if formatting fails, and ignores both results.
        fflush(file);
    }
}
