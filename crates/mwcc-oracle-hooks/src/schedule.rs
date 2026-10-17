//! Scheduler observations, preserving the original callback and trace order.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    pcode,
};
use core::ptr;

unsafe extern "C" {
    fn schedule_block__retail(block: *const u8);
    fn select_ready_coloring_node__retail(list: *const u8, cycle: u16) -> *const u8;
    static mut gVirtualRegistersActive: i32;
}
static mut DAG_PENDING: i32 = 0;
struct Globals {
    machine: *const u8,
    rank: *const u8,
}
#[cfg(target_arch = "wasm32")]
unsafe extern "C" {
    // (gc2's names: the WebAssembly module has no x86 code to find them in)
    static MI: *const u8;
    static opcodeinfo: [u8; 0];
}
#[cfg(target_arch = "wasm32")]
unsafe fn globals() -> Option<Globals> {
    unsafe {
        Some(Globals {
            machine: ptr::addr_of!(MI).read_unaligned(),
            // (OpcodeInfo.rank, in 18-byte entries)
            rank: ptr::addr_of!(opcodeinfo).cast::<u8>().add(9),
        })
    }
}
#[cfg(not(target_arch = "wasm32"))]
unsafe fn globals() -> Option<Globals> {
    unsafe {
        let code = select_ready_coloring_node__retail as *const () as *const u8;
        for (offset, byte) in [
            (0x26, 0x8b),
            (0x27, 0x2d),
            (0x100, 0x8a),
            (0x101, 0x04),
            (0x102, 0x55),
        ] {
            if *code.add(offset) != byte {
                return None;
            }
        }
        Some(Globals {
            machine: read::<*const *const u8>(code, 0x28).read_unaligned(),
            rank: read(code, 0x103),
        })
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn schedule_block(block: *const u8) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_SCHEDULE") {
            schedule_block__retail(block);
            return;
        }
        OracleTrace_Printf(
            c"sc.block p=%08x index=%d virtual=%d\n".as_ptr(),
            block as u32,
            read::<i32>(block, 28),
            ptr::addr_of!(gVirtualRegistersActive).read_unaligned(),
        );
        let mut instruction = read::<*const u8>(block, 20);
        while !instruction.is_null() {
            pcode::instruction(c"sc.i".as_ptr(), instruction);
            instruction = read(instruction, 0);
        }
        ptr::addr_of_mut!(DAG_PENDING).write(1);
        schedule_block__retail(block);
        ptr::addr_of_mut!(DAG_PENDING).write(0);
        OracleTrace_Printf(c"sc.after order=".as_ptr());
        instruction = read(block, 20);
        while !instruction.is_null() {
            OracleTrace_Printf(c"%08x,".as_ptr(), instruction as u32);
            instruction = read(instruction, 0);
        }
        OracleTrace_Printf(c"\n".as_ptr());
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn select_ready_coloring_node(list: *const u8, cycle: u16) -> *const u8 {
    unsafe {
        if !wanted(c"MWCC_ORACLE_SCHEDULE") {
            return select_ready_coloring_node__retail(list, cycle);
        }
        let known = globals();
        if ptr::addr_of!(DAG_PENDING).read() != 0 {
            ptr::addr_of_mut!(DAG_PENDING).write(0);
            let mut node = list;
            while !node.is_null() {
                OracleTrace_Printf(
                    c"sc.node i=%08x latency=%d height=%d latest=%d preds=%d succ=".as_ptr(),
                    read::<*const u8>(node, 12) as u32,
                    i32::from(read::<u16>(node, 16)),
                    i32::from(read::<u16>(node, 22)),
                    i32::from(read::<u16>(node, 20)),
                    i32::from(read::<u16>(node, 24)),
                );
                let mut edge = read::<*const u8>(node, 8);
                while !edge.is_null() {
                    let owner = read::<*const u8>(edge, 4);
                    OracleTrace_Printf(
                        c"%08x:%d,".as_ptr(),
                        read::<*const u8>(owner, 12) as u32,
                        i32::from(read::<u16>(edge, 8)),
                    );
                    edge = read(edge, 0);
                }
                OracleTrace_Printf(c"\n".as_ptr());
                node = read(node, 0);
            }
        }
        OracleTrace_Printf(c"sc.pick cycle=%d ready=".as_ptr(), i32::from(cycle));
        let mut node = list;
        while !node.is_null() {
            if read::<u16>(node, 24) == 0 {
                let mut count = 0i32;
                let mut edge = read::<*const u8>(node, 8);
                while !edge.is_null() {
                    if read::<u16>(read::<*const u8>(edge, 4), 24) == 1 {
                        count += 1;
                    }
                    edge = read(edge, 0);
                }
                let ins = read::<*const u8>(node, 12);
                let (issue, rank) = if let Some(g) = &known {
                    let check = read::<unsafe extern "C" fn(*const u8) -> i32>(g.machine, 20);
                    (
                        i32::from(check(ins) != 0),
                        i32::from(*g.rank.offset(isize::from(read::<i16>(ins, 32)) * 18)),
                    )
                } else {
                    (-1, -1)
                };
                OracleTrace_Printf(
                    c"%08x:%d:%d:%d:%d:%d:%d,".as_ptr(),
                    ins as u32,
                    i32::from(read::<u16>(node, 18)),
                    i32::from(read::<u16>(node, 20)),
                    i32::from(read::<u16>(node, 22)),
                    count,
                    issue,
                    rank,
                );
            }
            node = read(node, 0);
        }
        let picked = select_ready_coloring_node__retail(list, cycle);
        OracleTrace_Printf(
            c" pick=%08x\n".as_ptr(),
            if picked.is_null() {
                0
            } else {
                read::<*const u8>(picked, 12) as u32
            },
        );
        picked
    }
}
