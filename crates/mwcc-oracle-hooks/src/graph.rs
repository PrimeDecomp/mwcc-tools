//! Allocation graph observation; byte guards and call order match Hooks.c.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    lifecycle, pcode,
};
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
    static arguments: *const u8;
}
struct Globals {
    class: *const i8,
    start: *const i32,
    count: *const i32,
    graph: *const *mut OracleNode,
}
#[cfg(target_arch = "wasm32")]
unsafe extern "C" {
    // (gc2's names: the WebAssembly module has no x86 code to find them in)
    static coloring_class: i8;
    static n_real_registers: [i32; 5];
    static used_virtual_registers: [i32; 5];
    static interferencegraph: *mut *mut OracleNode;
    static first_fe_temporary_register: [i16; 5];
    static first_temporary_register: [i32; 5];
}
#[cfg(target_arch = "wasm32")]
unsafe fn globals() -> Option<Globals> {
    unsafe {
        Some(Globals {
            class: ptr::addr_of!(coloring_class),
            start: ptr::addr_of!(n_real_registers).cast(),
            count: ptr::addr_of!(used_virtual_registers).cast(),
            graph: ptr::addr_of!(interferencegraph).read_unaligned(),
        })
    }
}
#[cfg(not(target_arch = "wasm32"))]
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
// Register operands have a u16 number. One bounded bit set covers its complete
// domain, including physical registers, without rescanning instructions per node.
static mut VISIBLE: [u8; 8192] = [0; 8192];
unsafe fn capture(g: &Globals, class: i8, start: i32, count: i32) {
    unsafe {
        let Some(context) = lifecycle::graph_context(class) else {
            return;
        };
        OracleTrace_Printf(c"pv.graph.begin schema=1 function=%lu graph=%lu births=%lu cls=%d physical=%lu start=%ld count=%ld phase=%lu boundary=before_simplify category_ready=%d initial_last=%ld first=%ld last=%ld\n".as_ptr(),
            context.function, context.graph, context.births, i32::from(class), context.physical, start, count, context.phase,
            i32::from(context.category_ready),context.initial_last,context.first,context.last);
        let bits = ptr::addr_of_mut!(VISIBLE).cast::<u8>();
        ptr::write_bytes(bits, 0, 8192);
        let mut valid = start >= 0 && count >= start && count <= 65536;
        let mut instructions = 0u32;
        let mut blocks = 0u32;
        let mut block = ptr::addr_of!(pcode::gPCodeBlocks).read_unaligned();
        while valid && !block.is_null() {
            blocks += 1;
            if blocks > 1_000_000 {
                valid = false;
                break;
            }
            let mut instruction = read::<*const u8>(block, 20);
            while !instruction.is_null() {
                instructions += 1;
                let operands = read::<i16>(instruction, 34);
                if instructions > 1_000_000 || !(0..=256).contains(&operands) {
                    valid = false;
                    break;
                }
                pcode::instruction(c"pv.graph.instruction".as_ptr(), instruction);
                for index in 0..operands {
                    let operand = instruction.add(36 + index as usize * 12);
                    if read::<u8>(operand, 0) == 0 && read::<u8>(operand, 1) == class as u8 {
                        let register = usize::from(read::<u16>(operand, 4));
                        let byte = bits.add(register / 8);
                        byte.write(byte.read() | (1 << (register % 8)));
                    }
                }
                instruction = read(instruction, 0);
            }
            block = read(block, 0);
        }
        let mut nodes = 0u32;
        if valid {
            for index in start..count {
                let node = g.graph.offset(index as isize).read_unaligned();
                if node.is_null() {
                    valid = false;
                    break;
                }
                let neighbors = ptr::addr_of!((*node).neighbor_count).read_unaligned();
                if neighbors < 0 {
                    valid = false;
                    break;
                }
                OracleTrace_Printf(
                    c"pv.graph.node v=%ld visible=%d deg=%d flags=%x color=%d n=".as_ptr(),
                    index,
                    i32::from(
                        bits.add(index as usize / 8).read() & (1 << (index as usize % 8)) != 0,
                    ),
                    i32::from(ptr::addr_of!((*node).degree).read_unaligned()),
                    u32::from(ptr::addr_of!((*node).flags).read_unaligned()),
                    i32::from(ptr::addr_of!((*node).color).read_unaligned()),
                );
                for neighbor in 0..neighbors {
                    OracleTrace_Printf(
                        c"%u,".as_ptr(),
                        u32::from(
                            ptr::addr_of!((*node).neighbors)
                                .cast::<u16>()
                                .add(neighbor as usize)
                                .read_unaligned(),
                        ),
                    );
                }
                OracleTrace_Printf(c"\n".as_ptr());
                nodes += 1;
            }
        }
        OracleTrace_Printf(
            c"pv.graph.end function=%lu graph=%lu nodes=%lu instructions=%lu complete=%d\n"
                .as_ptr(),
            context.function,
            context.graph,
            nodes,
            instructions,
            i32::from(valid),
        );
    }
}
// ra.value (MWCC_ORACLE_NAMES): what each node of the class's graph holds, for the readers that name values: its
// object (0: a temporary of the code generator), whether that is an argument of the function, and whether the value
// can be rematerialized. ra.temporaries: the class's first front-end temporary and first temporary of the code
// generator (gc2's names, so only in the WebAssembly module).
#[cfg_attr(not(target_arch = "wasm32"), allow(unused_variables))]
unsafe fn values(g: &Globals, class: i8, start: i32, count: i32) {
    unsafe {
        #[cfg(target_arch = "wasm32")]
        OracleTrace_Printf(
            c"ra.temporaries cls=%d fe=%d first=%ld\n".as_ptr(),
            i32::from(class),
            i32::from(
                ptr::addr_of!(first_fe_temporary_register)
                    .cast::<i16>()
                    .add(class as usize)
                    .read_unaligned(),
            ),
            ptr::addr_of!(first_temporary_register)
                .cast::<i32>()
                .add(class as usize)
                .read_unaligned(),
        );
        for index in start..count {
            let node = g.graph.offset(index as isize).read_unaligned();
            let object = ptr::addr_of!((*node).f4).read_unaligned() as usize as *const u8;
            let mut argument = false;
            let mut list = ptr::addr_of!(arguments).read_unaligned();
            let mut guard = 0;
            while !object.is_null() && !list.is_null() && guard < 4096 {
                if read::<*const u8>(list, 4) == object {
                    argument = true;
                    break;
                }
                list = read(list, 0);
                guard += 1;
            }
            // (HashNameNode.name is an inline tail at byte 10)
            let name = if object.is_null() {
                c"-".as_ptr()
            } else {
                let name = read::<*const u8>(object, 10);
                if name.is_null() {
                    c"?".as_ptr()
                } else {
                    name.add(10).cast()
                }
            };
            OracleTrace_Printf(
                c"ra.value v=%d object=%08x remat=%d argument=%d name=%s\n".as_ptr(),
                index,
                object as u32,
                i32::from(ptr::addr_of!((*node).f8).read_unaligned() != 0),
                i32::from(argument),
                name,
            );
        }
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
            capture(g, class, start, count);
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
            if wanted(c"MWCC_ORACLE_NAMES") {
                values(g, class, start, count);
            }
        } else if wanted(c"MWCC_ORACLE_BACKEND") {
            OracleTrace_Printf(c"pv.gap reason=graph_byte_guard\n".as_ptr());
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
        // (st.precolor.*: the PCode select colours, MWCC_ORACLE_STAGES)
        if let Some(g) = &known
            && !ptr::addr_of!(crate::origins::ALLOCATING).read().is_null()
            && wanted(c"MWCC_ORACLE_STAGES")
        {
            let phase = if g.class.read_unaligned() == 3 {
                c"precolor.fpr"
            } else {
                c"precolor.gpr"
            };
            pcode::snapshot(
                c"st".as_ptr(),
                phase.as_ptr(),
                ptr::addr_of!(crate::origins::ALLOCATING).read(),
            );
        }
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
