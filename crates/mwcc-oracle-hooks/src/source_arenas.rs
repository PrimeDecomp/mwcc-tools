//! Compilation-wide arena observations. Allocation is separate from node type,
//! initialized contents, immutable revisions and original-source ancestry.
#![allow(non_snake_case)] // Exact existing retail entry names are the wrapper ABI.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    backend::Registers,
};
use core::{ffi::CStr, ptr};

#[repr(C)]
#[derive(Clone, Copy)]
struct Globals {
    pools: *const u8,
    lock: *const u16,
    version: u32,
}
#[repr(C)]
pub struct Descriptor {
    globals: *const Globals,
    site: u32,
    action: u32,
    pair: u32,
    pool: u32,
    size_kind: u32,
    size: u32,
    result_kind: u32,
    result: u32,
}
const _: () = {
    assert!(core::mem::size_of::<Globals>() == 12);
    assert!(core::mem::size_of::<Descriptor>() == 36);
    assert!(core::mem::offset_of!(Descriptor, site) == 4);
    assert!(core::mem::offset_of!(Descriptor, size_kind) == 20);
};
unsafe extern "C" {
    #[link_name = "OracleRust_ArenaGlobals"]
    static mut GLOBALS: Globals;
    fn CompilerTools_ResetPool__retail();
    fn freeaheap__retail();
    fn freelheap__retail();
    fn CompilerTools_ResetGallocAndPoolMemory__retail();
    fn releasegheap__retail();
    fn releaseheaps__retail();
    fn initheaps__retail(callback: u32) -> u32;
    fn CompilerTools_InitHeaps__retail(callback: u32) -> u32;
}
#[derive(Clone, Copy)]
struct Request {
    id: u32,
    pair: u32,
    pool: u32,
    size: u32,
    esp: u32,
    epoch: u32,
}
const EMPTY_REQUEST: Request = Request {
    id: 0,
    pair: 0,
    pool: 0,
    size: 0,
    esp: 0,
    epoch: 0,
};
#[derive(Clone, Copy)]
struct Reset {
    id: u32,
    parent: u32,
    op: u32,
    mask: u32,
    skip: u32,
}
const EMPTY_RESET: Reset = Reset {
    id: 0,
    parent: 0,
    op: 0,
    mask: 0,
    skip: 0,
};
const LIMIT: u32 = 64;
static mut ENABLED: i8 = -1;
static mut STARTED: bool = false;
static mut BUSY: bool = false;
static mut UNSUPPORTED: u32 = 0;
static mut EVENT: u32 = 0;
static mut REQUESTS: u32 = 0;
static mut ALLOCATIONS: u32 = 0;
static mut RESETS: u32 = 0;
static mut RESET_ENDS: u32 = 0;
static mut GAPS: u32 = 0;
static mut CHECKPOINTS: u32 = 0;
static mut REQUEST_DEPTH: u32 = 0;
static mut RESET_DEPTH: u32 = 0;
static mut EPOCHS: [u32; 5] = [0; 5];
static mut REQUEST_STACK: [Request; LIMIT as usize] = [EMPTY_REQUEST; LIMIT as usize];
static mut RESET_STACK: [Reset; LIMIT as usize] = [EMPTY_RESET; LIMIT as usize];

unsafe fn next(counter: *mut u32) -> u32 {
    unsafe {
        let old = counter.read();
        if old == u32::MAX {
            ptr::addr_of_mut!(UNSUPPORTED).write(1);
        }
        let value = old.saturating_add(1);
        counter.write(value);
        value
    }
}
unsafe fn epoch(pool: u32) -> u32 {
    unsafe {
        ptr::addr_of!(EPOCHS)
            .cast::<u32>()
            .add(pool as usize)
            .read()
    }
}
unsafe fn globals() -> Globals {
    unsafe { ptr::addr_of!(GLOBALS).read() }
}
unsafe fn gap(reason: &CStr, site: u32) {
    unsafe {
        ptr::addr_of_mut!(UNSUPPORTED).write(1);
        next(ptr::addr_of_mut!(GAPS));
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(
            c"pv.arena.gap schema=1 stream=1 event=%lu mask=31 reason=%s site=%08lx\n".as_ptr(),
            event,
            reason.as_ptr(),
            site,
        );
    }
}
unsafe fn enter() -> bool {
    unsafe {
        let mut enabled = ptr::addr_of!(ENABLED).read();
        if enabled < 0 {
            enabled = i8::from(wanted(c"MWCC_ORACLE_SOURCE_ARENAS"));
            ptr::addr_of_mut!(ENABLED).write(enabled);
        }
        if enabled == 0 {
            return false;
        }
        if ptr::addr_of!(BUSY).read() {
            // Do not recursively format a record while the writer is active.
            // The independent count/unsupported checkpoint exposes this loss.
            next(ptr::addr_of_mut!(GAPS));
            ptr::addr_of_mut!(UNSUPPORTED).write(1);
            return false;
        }
        ptr::addr_of_mut!(BUSY).write(true);
        if !ptr::addr_of!(STARTED).read() {
            ptr::addr_of_mut!(STARTED).write(true);
            let g = globals();
            let event = next(ptr::addr_of_mut!(EVENT));
            OracleTrace_Printf(c"pv.arena.begin schema=1 stream=1 event=%lu scope=guarded_retail_arenas_v1 version=%lu pools=%08lx lock=%08lx\n".as_ptr(),event,g.version,g.pools as u32,g.lock as u32);
            if g.pools.is_null() || g.lock.is_null() || !(1..=3).contains(&g.version) {
                gap(c"invalid_globals", 0);
            }
        }
        true
    }
}
unsafe fn leave() {
    unsafe { ptr::addr_of_mut!(BUSY).write(false) }
}
fn pool_for_pair(pair: u32) -> Option<u32> {
    match pair {
        1 | 5..=9 => Some(4),
        2 => Some(3),
        3 => Some(2),
        4 => Some(1),
        _ => None,
    }
}
fn selected(r: &Registers, kind: u32, value: u32) -> Option<u32> {
    match kind {
        0 => Some(value),
        1 => r.slot(value as usize),
        _ => None,
    }
}

#[unsafe(export_name = "OracleRust_ArenaProbeV1")]
pub unsafe extern "C" fn observe(r: &Registers, d: &Descriptor) {
    unsafe {
        if !enter() {
            return;
        }
        if !ptr::eq(d.globals, ptr::addr_of!(GLOBALS)) {
            gap(c"invalid_descriptor", d.site);
            leave();
            return;
        }
        if d.action == 3 {
            gap(c"allocator_callback", d.site);
            leave();
            return;
        }
        let Some(pool) = pool_for_pair(d.pair) else {
            gap(c"invalid_pair", d.site);
            leave();
            return;
        };
        if pool != d.pool {
            gap(c"invalid_pool", d.site);
            leave();
            return;
        }
        let Some(esp) = r.slot(3).and_then(|v| v.checked_add(8)) else {
            gap(c"invalid_stack", d.site);
            leave();
            return;
        };
        let Some(size) = selected(r, d.size_kind, d.size) else {
            gap(c"invalid_size_selector", d.site);
            leave();
            return;
        };
        if d.action == 1 {
            let id = next(ptr::addr_of_mut!(REQUESTS));
            let depth = ptr::addr_of!(REQUEST_DEPTH).read();
            let stack = ptr::addr_of_mut!(REQUEST_STACK).cast::<Request>();
            let parent = if depth == 0 {
                0
            } else {
                stack.add((depth - 1) as usize).read().id
            };
            if depth < LIMIT {
                stack.add(depth as usize).write(Request {
                    id,
                    pair: d.pair,
                    pool,
                    size,
                    esp,
                    epoch: epoch(pool),
                });
                ptr::addr_of_mut!(REQUEST_DEPTH).write(depth + 1);
            } else {
                gap(c"request_capacity", d.site);
            }
            let event = next(ptr::addr_of_mut!(EVENT));
            OracleTrace_Printf(c"pv.arena.request schema=1 stream=1 event=%lu request=%lu parent_request=%lu pair=%lu pool=%lu requested=%lu retail_esp=%08lx site=%08lx epoch=%lu depth=%lu\n".as_ptr(),event,id,parent,d.pair,pool,size,esp,d.site,epoch(pool),depth+1);
        } else if d.action == 2 {
            let id = next(ptr::addr_of_mut!(ALLOCATIONS));
            let depth = ptr::addr_of!(REQUEST_DEPTH).read();
            let request = if depth == 0 {
                EMPTY_REQUEST
            } else {
                ptr::addr_of!(REQUEST_STACK)
                    .cast::<Request>()
                    .add((depth - 1) as usize)
                    .read()
            };
            let matched = depth > 0
                && request.pair == d.pair
                && request.pool == pool
                && request.esp == esp
                && request.epoch == epoch(pool);
            if matched {
                ptr::addr_of_mut!(REQUEST_DEPTH).write(depth - 1);
            } else {
                gap(c"request_commit_mismatch", d.site);
            }
            let address = selected(r, d.result_kind, d.result).unwrap_or(0);
            let g = globals();
            let (block, freep, free) = if g.pools.is_null() {
                (0, 0, -1)
            } else {
                let descriptor = g.pools.add(pool as usize * 20);
                (
                    read::<u32>(descriptor, 8),
                    read::<u32>(descriptor, 12),
                    read::<i32>(descriptor, 16),
                )
            };
            let rounded = (request.size & !7).checked_add(8);
            let valid = matched
                && d.result_kind == 1
                && size > 0
                && size <= i32::MAX as u32
                && rounded == Some(size)
                && request.size <= size
                && address != 0
                && address.checked_add(size) == Some(freep)
                && block != 0
                && free >= 0;
            if !valid {
                gap(c"invalid_allocation_span", d.site);
            }
            if ptr::addr_of!(RESET_DEPTH).read() != 0 {
                gap(c"allocation_during_reset", d.site);
            }
            let complete = u32::from(valid && ptr::addr_of!(UNSUPPORTED).read() == 0);
            let event = next(ptr::addr_of_mut!(EVENT));
            OracleTrace_Printf(c"pv.arena.alloc schema=1 stream=1 event=%lu allocation=%lu request=%lu pair=%lu pool=%lu requested=%lu requested_known=%lu reserved=%lu address=%08lx block=%08lx freep=%08lx free=%ld retail_esp=%08lx site=%08lx epoch=%lu complete=%lu\n".as_ptr(),event,id,request.id,d.pair,pool,request.size,u32::from(matched),size,address,block,freep,free,esp,d.site,epoch(pool),complete);
        } else {
            gap(c"invalid_action", d.site);
        }
        leave();
    }
}

unsafe fn checkpoint_inner(function: u32) {
    unsafe {
        let checkpoints = next(ptr::addr_of_mut!(CHECKPOINTS));
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.arena.checkpoint schema=1 stream=1 event=%lu checkpoints=%lu requests=%lu allocations=%lu resets=%lu reset_ends=%lu gaps=%lu request_depth=%lu reset_depth=%lu unsupported=%lu function=%lu epoch0=%lu epoch1=%lu epoch2=%lu epoch3=%lu epoch4=%lu\n".as_ptr(),event,checkpoints,ptr::addr_of!(REQUESTS).read(),ptr::addr_of!(ALLOCATIONS).read(),ptr::addr_of!(RESETS).read(),ptr::addr_of!(RESET_ENDS).read(),ptr::addr_of!(GAPS).read(),ptr::addr_of!(REQUEST_DEPTH).read(),ptr::addr_of!(RESET_DEPTH).read(),ptr::addr_of!(UNSUPPORTED).read(),function,epoch(0),epoch(1),epoch(2),epoch(3),epoch(4));
    }
}
pub unsafe fn checkpoint(function: u32) {
    unsafe {
        if enter() {
            checkpoint_inner(function);
            leave();
        }
    }
}
fn mask(op: u32) -> u32 {
    match op {
        1 => 2,
        2 => 4,
        3 => 8,
        4 | 5 => 16,
        6..=8 => 31,
        _ => 0,
    }
}
unsafe fn reset_begin(op: u32) -> u32 {
    unsafe {
        if !enter() {
            return 0;
        }
        let id = next(ptr::addr_of_mut!(RESETS));
        let depth = ptr::addr_of!(RESET_DEPTH).read();
        let stack = ptr::addr_of_mut!(RESET_STACK).cast::<Reset>();
        let parent = if depth == 0 {
            0
        } else {
            stack.add((depth - 1) as usize).read().id
        };
        let g = globals();
        let lock = if g.lock.is_null() {
            0
        } else {
            g.lock.read_unaligned()
        };
        let skip = u32::from(op == 3 && lock != 0);
        let mask = mask(op);
        if skip == 0 {
            for pool in 0..5 {
                if mask & (1 << pool) != 0 {
                    next(ptr::addr_of_mut!(EPOCHS).cast::<u32>().add(pool as usize));
                }
            }
        }
        if depth < LIMIT {
            stack.add(depth as usize).write(Reset {
                id,
                parent,
                op,
                mask,
                skip,
            });
            ptr::addr_of_mut!(RESET_DEPTH).write(depth + 1);
        } else {
            gap(c"reset_capacity", 0);
        }
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.arena.reset.begin schema=1 stream=1 event=%lu reset=%lu parent_reset=%lu op=%lu mask=%lu lock_value=%lu skip=%lu request_depth=%lu depth=%lu epoch0=%lu epoch1=%lu epoch2=%lu epoch3=%lu epoch4=%lu\n".as_ptr(),event,id,parent,op,mask,u32::from(lock),skip,ptr::addr_of!(REQUEST_DEPTH).read(),depth+1,epoch(0),epoch(1),epoch(2),epoch(3),epoch(4));
        leave();
        id
    }
}
unsafe fn reset_end(id: u32, status_known: u32, status: u32) {
    unsafe {
        if id == 0 || !enter() {
            return;
        }
        next(ptr::addr_of_mut!(RESET_ENDS));
        let depth = ptr::addr_of!(RESET_DEPTH).read();
        let frame = if depth == 0 {
            EMPTY_RESET
        } else {
            ptr::addr_of!(RESET_STACK)
                .cast::<Reset>()
                .add((depth - 1) as usize)
                .read()
        };
        if frame.id == id && depth > 0 {
            ptr::addr_of_mut!(RESET_DEPTH).write(depth - 1);
        } else {
            gap(c"reset_mismatch", 0);
        }
        if status_known != 0 && status != 0 {
            gap(c"initialization_failed", 0);
        }
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.arena.reset.end schema=1 stream=1 event=%lu reset=%lu parent_reset=%lu op=%lu mask=%lu skip=%lu status_known=%lu status=%lu depth=%lu request_depth=%lu epoch0=%lu epoch1=%lu epoch2=%lu epoch3=%lu epoch4=%lu\n".as_ptr(),event,id,frame.parent,frame.op,frame.mask,frame.skip,status_known,status,ptr::addr_of!(RESET_DEPTH).read(),ptr::addr_of!(REQUEST_DEPTH).read(),epoch(0),epoch(1),epoch(2),epoch(3),epoch(4));
        checkpoint_inner(0);
        leave();
    }
}
macro_rules! reset_wrapper {
    ($name:ident,$retail:ident,$op:literal) => {
        #[unsafe(no_mangle)]
        pub unsafe extern "C" fn $name() {
            unsafe {
                let id = reset_begin($op);
                $retail();
                reset_end(id, 0, 0);
            }
        }
    };
}
reset_wrapper!(CompilerTools_ResetPool, CompilerTools_ResetPool__retail, 1);
reset_wrapper!(freeaheap, freeaheap__retail, 2);
reset_wrapper!(freelheap, freelheap__retail, 3);
reset_wrapper!(
    CompilerTools_ResetGallocAndPoolMemory,
    CompilerTools_ResetGallocAndPoolMemory__retail,
    4
);
reset_wrapper!(releasegheap, releasegheap__retail, 5);
reset_wrapper!(releaseheaps, releaseheaps__retail, 6);
#[unsafe(no_mangle)]
pub unsafe extern "C" fn initheaps(callback: u32) -> u32 {
    unsafe {
        let id = reset_begin(7);
        let status = initheaps__retail(callback);
        reset_end(id, 1, status);
        status
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn CompilerTools_InitHeaps(callback: u32) -> u32 {
    unsafe {
        let id = reset_begin(8);
        let status = CompilerTools_InitHeaps__retail(callback);
        reset_end(id, 1, status);
        status
    }
}
