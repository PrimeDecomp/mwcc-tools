#![no_std]
use core::ffi::c_char;
type Printer = unsafe extern "C" fn(*const c_char, ...);
#[unsafe(no_mangle)]
pub unsafe extern "C" fn OracleAbi_RustTraceEmit(printer: Printer, text: *const c_char, special: i32) {
    unsafe {
        printer(c"rust %d %x %g %llx %s %p".as_ptr(), -2147483000i32, 0xfedcba98u32,
            if special == 0 { -1.25f64 } else { f64::from_bits(0x7ff8123456789abc) },
            0x8123456789abcdefu64, text, core::ptr::null::<u8>());
        printer(c"promoted %d %d %g".as_ptr(), i32::from(128u8), i32::from(-32760i16), f64::from(-0.5f32));
    }
}
