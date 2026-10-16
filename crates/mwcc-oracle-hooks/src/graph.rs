//! Allocation graph observation; byte guards and call order match Hooks.c.
use crate::abi::{OracleTrace_Printf, read};
use core::{
    mem::{align_of, offset_of, size_of},
    ptr,
};

#[repr(C, packed(2))]
struct OracleNode {
    next: *mut OracleNode,
    f4: i32,
    f8: i32,
    spill_cost: i32,
    virtual_register: i16,
    degree: i16,
    color: i16,
    flags: u16,
    neighbor_count: i16,
    neighbors: [i16; 1],
}
const _: () = {
    assert!(size_of::<OracleNode>() == 28);
    assert!(align_of::<OracleNode>() == 2);
    assert!(offset_of!(OracleNode, spill_cost) == 12);
    assert!(offset_of!(OracleNode, virtual_register) == 16);
    assert!(offset_of!(OracleNode, degree) == 18);
    assert!(offset_of!(OracleNode, color) == 20);
    assert!(offset_of!(OracleNode, flags) == 22);
    assert!(offset_of!(OracleNode, neighbor_count) == 24);
    assert!(offset_of!(OracleNode, neighbors) == 26);
};
unsafe extern "C" {
    fn fn_00507070__retail() -> *mut OracleNode;
    fn fn_00506f50__retail(stack: *mut OracleNode) -> i32;
    fn fn_004fcb70(class: i8) -> i32;
    fn fn_004ceda0(class: i8);
    fn fn_004fcb20(class: i8) -> u32;
}
struct Globals {
    class: *const i8,
    start: *const i32,
    count: *const i32,
    graph: *const *mut OracleNode,
}
unsafe fn globals() -> Option<Globals> {
    unsafe {
        let code = fn_00507070__retail as *const () as *const u8;
        for (offset, expected) in [
            (7, 0xa0),
            (0x29, 0x8b),
            (0x2a, 0x0c),
            (0x2b, 0x85),
            (0x32, 0x8b),
            (0x33, 0x15),
            (0x8c, 0x3b),
            (0x8d, 0x0c),
            (0x8e, 0x85),
        ] {
            if *code.add(offset) != expected {
                return None;
            }
        }
        let graph_variable = read::<*const *const *mut OracleNode>(code, 0x34);
        Some(Globals {
            class: read(code, 8),
            start: read(code, 0x2c),
            count: read(code, 0x8f),
            graph: graph_variable.read_unaligned(),
        })
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00507070() -> *mut OracleNode {
    unsafe {
        let known = globals();
        if let Some(g) = &known {
            let class = g.class.read_unaligned();
            let start = g.start.offset(isize::from(class)).read_unaligned();
            let count = g.count.offset(isize::from(class)).read_unaligned();
            OracleTrace_Printf(
                c"ra.graph cls=%d k=%d start=%d count=%d\n".as_ptr(),
                i32::from(class),
                fn_004fcb70(class),
                start,
                count,
            );
            for index in start..count {
                let node = g.graph.offset(index as isize).read_unaligned();
                OracleTrace_Printf(
                    c"ra.node v=%d p=%08x deg=%d flags=%x color=%d n=".as_ptr(),
                    index,
                    node as u32,
                    i32::from(ptr::addr_of!((*node).degree).read_unaligned()),
                    u32::from(ptr::addr_of!((*node).flags).read_unaligned()),
                    i32::from(ptr::addr_of!((*node).color).read_unaligned()),
                );
                let count = ptr::addr_of!((*node).neighbor_count).read_unaligned();
                let neighbors = ptr::addr_of!((*node).neighbors).cast::<i16>();
                for index in 0..count {
                    OracleTrace_Printf(
                        c"%d,".as_ptr(),
                        i32::from(neighbors.offset(index as isize).read_unaligned()),
                    );
                }
                OracleTrace_Printf(c"\n".as_ptr());
            }
        }
        let stack = fn_00507070__retail();
        if let Some(g) = &known {
            OracleTrace_Printf(
                c"ra.order cls=%d v=".as_ptr(),
                i32::from(g.class.read_unaligned()),
            );
            let mut node = stack;
            while !node.is_null() {
                OracleTrace_Printf(
                    c"%08x:%d,".as_ptr(),
                    node as u32,
                    ptr::addr_of!((*node).spill_cost).read_unaligned(),
                );
                node = ptr::addr_of!((*node).next).read_unaligned();
            }
            OracleTrace_Printf(c"\n".as_ptr());
        }
        stack
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00506f50(stack: *mut OracleNode) -> i32 {
    unsafe {
        let known = globals();
        let before = if let Some(g) = &known {
            let class = g.class.read_unaligned();
            fn_004ceda0(class);
            fn_004fcb20(class)
        } else {
            0
        };
        let result = fn_00506f50__retail(stack);
        if let Some(g) = &known {
            let class = g.class.read_unaligned();
            OracleTrace_Printf(
                c"ra.select cls=%d mask=%08x after=%08x ok=%d v=".as_ptr(),
                i32::from(class),
                before,
                fn_004fcb20(class),
                result,
            );
            let mut node = stack;
            while !node.is_null() {
                OracleTrace_Printf(
                    c"%08x:%d:%x,".as_ptr(),
                    node as u32,
                    i32::from(ptr::addr_of!((*node).color).read_unaligned()),
                    u32::from(ptr::addr_of!((*node).flags).read_unaligned()),
                );
                node = ptr::addr_of!((*node).next).read_unaligned();
            }
            OracleTrace_Printf(c"\n".as_ptr());
        }
        result
    }
}
