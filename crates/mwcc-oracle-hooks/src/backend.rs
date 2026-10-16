//! Actual retail branch observations. This does not claim creation coverage.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    pcode,
};
use core::ptr;

#[repr(C)]
pub struct Registers {
    edi: u32,
    esi: u32,
    ebp: u32,
    saved_esp: u32,
    ebx: u32,
    edx: u32,
    ecx: u32,
    eax: u32,
    eflags: u32,
}
const _: () = {
    assert!(core::mem::size_of::<Registers>() == 36);
    assert!(core::mem::offset_of!(Registers, saved_esp) == 12);
    assert!(core::mem::offset_of!(Registers, eax) == 28);
    assert!(core::mem::offset_of!(Registers, eflags) == 32);
};
impl Registers {
    pub fn slot(&self, index: usize) -> Option<u32> {
        match index {
            0 => Some(self.edi),
            1 => Some(self.esi),
            2 => Some(self.ebp),
            3 => Some(self.saved_esp),
            4 => Some(self.ebx),
            5 => Some(self.edx),
            6 => Some(self.ecx),
            7 => Some(self.eax),
            _ => None,
        }
    }
}
#[repr(C)]
pub struct Globals {
    class: *const i8,
    count: *const i32,
    physical: *const i32,
    first: *const i16,
    last: *const i32,
    special: *const i16,
}
static mut FUNCTION: *const u8 = ptr::null();
static mut ACTIVE: bool = false;
static mut PASS: u32 = 0;
static mut CANDIDATE: u32 = 0;

pub unsafe fn context(function: *const u8) -> *const u8 {
    unsafe {
        let old = ptr::addr_of!(FUNCTION).read();
        ptr::addr_of_mut!(FUNCTION).write(function);
        old
    }
}

#[unsafe(export_name = "OracleRust_BackendProbe")]
pub unsafe extern "C" fn observe(r: &Registers, site: u32, g: &Globals) {
    unsafe {
        if site == 7 {
            let active = wanted(c"MWCC_ORACLE_BACKEND");
            ptr::addr_of_mut!(ACTIVE).write(active);
            if active {
                ptr::addr_of_mut!(PASS).write(ptr::addr_of!(PASS).read().wrapping_add(1));
                ptr::addr_of_mut!(CANDIDATE).write(0);
            }
        }
        if !ptr::addr_of!(ACTIVE).read() {
            return;
        }
        let pass = ptr::addr_of!(PASS).read();
        let class = i32::from(g.class.read_unaligned());
        // The guarded function itself uses these same five-entry tables.
        if !(2..=4).contains(&class) {
            OracleTrace_Printf(
                c"pv.gap pass=%lu reason=unsupported_class cls=%d\n".as_ptr(),
                pass,
                class,
            );
            ptr::addr_of_mut!(ACTIVE).write(false);
            return;
        }
        let index = class as isize;
        if site == 7 {
            let function = ptr::addr_of!(FUNCTION).read();
            OracleTrace_Printf(c"pv.coalesce.begin schema=1 pass=%lu cls=%d count=%ld physical=%ld first=%d last=%ld special=%d function_id=%lu creation=direct_counter_writes link=%s\n".as_ptr(),
                pass, class, g.count.offset(index).read_unaligned(), g.physical.offset(index).read_unaligned(),
                i32::from(g.first.offset(index).read_unaligned()), g.last.offset(index).read_unaligned(),
                i32::from(g.special.read_unaligned()), crate::lifecycle::function_id(), if function.is_null() { c"?".as_ptr().cast() } else { pcode::link_name(function) });
            return;
        }
        let candidate = ptr::addr_of!(CANDIDATE).read();
        if site == 0x442 {
            OracleTrace_Printf(
                c"pv.coalesce.end pass=%lu candidates=%lu\n".as_ptr(),
                pass,
                candidate,
            );
            ptr::addr_of_mut!(ACTIVE).write(false);
            return;
        }
        // PUSHAD stores ESP after PUSHFD, so add four to recover retail ESP.
        let stack = r.saved_esp.wrapping_add(4) as *const u8;
        let instruction = read::<*const u8>(stack, 0xc);
        if site == 0x107 {
            let candidate = candidate.wrapping_add(1);
            ptr::addr_of_mut!(CANDIDATE).write(candidate);
            let operands = read::<i16>(instruction, 0x22);
            OracleTrace_Printf(c"pv.coalesce.candidate pass=%lu candidate=%lu instruction=%08x flags=%lx opcode=%d operands=%d destination_kind=%d destination_class=%d destination=%d source_kind=%d source_class=%d source=%d\n".as_ptr(),
                pass, candidate, instruction as u32, read::<u32>(instruction,0x14), i32::from(read::<i16>(instruction,0x20)), i32::from(operands),
                i32::from(read::<u8>(instruction,0x24)), i32::from(read::<u8>(instruction,0x25)), i32::from(read::<i16>(instruction,0x28)),
                if operands >= 2 { i32::from(read::<u8>(instruction,0x30)) } else {-1},
                if operands >= 2 { i32::from(read::<u8>(instruction,0x31)) } else {-1},
                if operands >= 2 { i32::from(read::<i16>(instruction,0x34)) } else {-1});
            return;
        }
        let zf = r.eflags & 0x40 != 0;
        let sf = r.eflags & 0x80 != 0;
        let of = r.eflags & 0x800 != 0;
        let (name, taken) = match site {
            0x10f => (c"copy_flag", !zf),
            0x17b => (c"destination_kind", !zf),
            0x18b => (c"register_class", !zf),
            0x1d6 => (c"roots_differ", !zf),
            0x244 => (c"interference", !zf),
            0x271 => (c"destination_below_window", sf != of),
            0x280 => (c"destination_above_window", !zf && sf == of),
            0x289 => (c"source_below_window", sf != of),
            0x291 => (c"source_above_window", !zf && sf == of),
            0x2b9 => (c"gpr_special_register", zf),
            // This site is AFTER the original root write, not a predicted union.
            0x2cb => {
                let parent = (r.eax as *const u16).add(r.edi as usize).read_unaligned();
                OracleTrace_Printf(c"pv.coalesce.merge pass=%lu candidate=%lu parent=%d child=%d observed_parent=%d\n".as_ptr(),
                    pass, candidate, i32::from(r.ecx as i16), i32::from(r.edi as i16), i32::from(parent));
                return;
            }
            _ => return,
        };
        OracleTrace_Printf(c"pv.coalesce.check pass=%lu candidate=%lu site=%lx condition=%s taken=%d eflags=%lx eax=%lx ebx=%lx ecx=%lx edx=%lx esi=%lx edi=%lx ebp=%lx\n".as_ptr(),
            pass, candidate, site, name.as_ptr(), i32::from(taken), r.eflags, r.eax,r.ebx,r.ecx,r.edx,r.esi,r.edi,r.ebp);
    }
}
