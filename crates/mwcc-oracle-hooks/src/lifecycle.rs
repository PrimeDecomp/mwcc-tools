//! Counter observations execute after the actual retail memory write. Value IDs
//! are function/sequence observations; semantic correspondence is a host task.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    backend::Registers,
    expression, pcode,
};
use core::ptr;
#[repr(C)]
pub struct Descriptor {
    counter: *const u32,
    counts: *const u32,
    physical: *const u32,
    selector: i32,
    role: u32,
    initial: *const i16,
    first: *const i16,
    last: *const i32,
    object_binding: u32,
}
const _: () = {
    assert!(core::mem::size_of::<Descriptor>() == 36);
    assert!(core::mem::align_of::<Descriptor>() == 4);
    assert!(core::mem::offset_of!(Descriptor, selector) == 12);
    assert!(core::mem::offset_of!(Descriptor, role) == 16);
    assert!(core::mem::offset_of!(Descriptor, object_binding) == 32);
};
#[derive(Clone, Copy)]
struct State {
    id: u32,
    object: *const u8,
    sequence: u32,
    expression: u32,
    expression_next: u32,
    phase: u32,
    graphs: u32,
    physical: *const u32,
    counts: [u32; 5],
    initial: *const i16,
    first: *const i16,
    last: *const i32,
    category_ready: u32,
    ranges: u32,
    origin_statement: *const u8,
    origin_node: *const u8,
    origin_id: u32,
}
const EMPTY: State = State {
    id: 0,
    object: ptr::null(),
    sequence: 0,
    expression: 0,
    expression_next: 0,
    phase: 0,
    graphs: 0,
    physical: ptr::null(),
    counts: [0; 5],
    initial: ptr::null(),
    first: ptr::null(),
    last: ptr::null(),
    category_ready: 0,
    ranges: 0,
    origin_statement: ptr::null(),
    origin_node: ptr::null(),
    origin_id: 0,
};
static mut STATE: State = EMPTY;
static mut STACK: [State; 16] = [EMPTY; 16];
static mut DEPTH: usize = 0;
static mut NEXT: u32 = 0;
static mut ENABLED: i8 = -1;
static mut TEXT: [u8; 2048] = [0; 2048];
static mut HEX: [u8; 4096] = [0; 4096];
unsafe extern "C" {
    static mut gCurrentStatement: *const u8;
}
unsafe fn enabled() -> bool {
    unsafe {
        let mut value = ptr::addr_of!(ENABLED).read();
        if value < 0 {
            value = i8::from(wanted(c"MWCC_ORACLE_BACKEND"));
            ptr::addr_of_mut!(ENABLED).write(value);
        }
        value != 0
    }
}
pub unsafe fn function_id() -> u32 {
    unsafe { ptr::addr_of!(STATE.id).read() }
}
pub unsafe fn phase(value: u32) {
    unsafe { ptr::addr_of_mut!(STATE.phase).write(value) }
}
pub struct GraphContext {
    pub function: u32,
    pub graph: u32,
    pub births: u32,
    pub phase: u32,
    pub physical: u32,
    pub category_ready: bool,
    pub initial_last: i32,
    pub first: i32,
    pub last: i32,
}
pub unsafe fn graph_context(class: i8) -> Option<GraphContext> {
    unsafe {
        if !enabled() {
            return None;
        }
        if function_id() == 0 || !(0..5).contains(&class) {
            OracleTrace_Printf(
                c"pv.gap reason=graph_context cls=%d\n".as_ptr(),
                i32::from(class),
            );
            return None;
        }
        let graph = ptr::addr_of!(STATE.graphs).read().wrapping_add(1);
        ptr::addr_of_mut!(STATE.graphs).write(graph);
        Some(GraphContext {
            function: function_id(),
            graph,
            births: ptr::addr_of!(STATE.sequence).read(),
            phase: ptr::addr_of!(STATE.phase).read(),
            // Target tables can initialize after generator entry. Read them at
            // this boundary rather than caching their initial contents.
            physical: ptr::addr_of!(STATE.physical)
                .read()
                .add(class as usize)
                .read_unaligned(),
            category_ready: class >= 2 && ptr::addr_of!(STATE.category_ready).read() == 3,
            initial_last: if class >= 2 {
                i32::from(
                    ptr::addr_of!(STATE.initial)
                        .read()
                        .add(class as usize - 2)
                        .read_unaligned(),
                )
            } else {
                -1
            },
            first: if class >= 2 {
                i32::from(
                    ptr::addr_of!(STATE.first)
                        .read()
                        .add(class as usize - 2)
                        .read_unaligned(),
                )
            } else {
                -1
            },
            last: if class >= 2 {
                ptr::addr_of!(STATE.last)
                    .read()
                    .add(class as usize - 2)
                    .read_unaligned()
            } else {
                -1
            },
        })
    }
}
pub unsafe fn expression_begin(node: *const u8) -> u32 {
    unsafe {
        if function_id() == 0 || !enabled() {
            return 0;
        }
        let previous = ptr::addr_of!(STATE.expression).read();
        let next = ptr::addr_of!(STATE.expression_next).read().wrapping_add(1);
        ptr::addr_of_mut!(STATE.expression_next).write(next);
        ptr::addr_of_mut!(STATE.expression).write(next);
        let statement = ptr::addr_of!(gCurrentStatement).read_unaligned();
        let buffer = ptr::addr_of_mut!(TEXT).cast::<u8>();
        buffer.write(0);
        // The historical speller checks a logical limit before appending a
        // piece. Reserve physical slack and retain boundedness as explicit data.
        expression::describe(buffer.cast(), 512, node, 8);
        let hex = ptr::addr_of_mut!(HEX).cast::<u8>();
        let mut length = 0usize;
        while length < 2047 && buffer.add(length).read() != 0 {
            let byte = buffer.add(length).read();
            let digit = |v: u8| if v < 10 { b'0' + v } else { b'a' + v - 10 };
            hex.add(length * 2).write(digit(byte >> 4));
            hex.add(length * 2 + 1).write(digit(byte & 15));
            length += 1;
        }
        hex.add(length * 2).write(0);
        OracleTrace_Printf(c"pv.expression.begin function=%lu expression=%lu node=%08x kind=%d line=%ld scope=statement_root complete=0 tree_hex=%s\n".as_ptr(),
            function_id(),next,node as u32,if node.is_null(){-1}else{i32::from(read::<u8>(node,0))},
            if statement.is_null(){-1}else{read::<i32>(statement,22)},hex.cast::<core::ffi::c_char>());
        previous
    }
}
pub unsafe fn expression_end(previous: u32) {
    unsafe {
        if function_id() == 0 || !enabled() {
            return;
        }
        OracleTrace_Printf(
            c"pv.expression.end function=%lu expression=%lu\n".as_ptr(),
            function_id(),
            ptr::addr_of!(STATE.expression).read(),
        );
        ptr::addr_of_mut!(STATE.expression).write(previous);
    }
}

unsafe fn statement_origin(statement: *const u8) -> u32 {
    unsafe {
        let explicit = ptr::addr_of!(STATE.expression).read();
        if explicit != 0 {
            return explicit;
        }
        if statement.is_null() || ptr::addr_of!(STATE.phase).read() != 0 {
            return 0;
        }
        // Assembly payloads are not ENodes; inspect only expression-bearing
        // statement kinds, using the audited mac68k Statement layout.
        if !matches!(read::<u8>(statement,4),4..=8|12..=15) {
            return 0;
        }
        let node = read::<*const u8>(statement, 10);
        if node.is_null() {
            return 0;
        }
        if ptr::addr_of!(STATE.origin_statement).read() == statement
            && ptr::addr_of!(STATE.origin_node).read() == node
        {
            return ptr::addr_of!(STATE.origin_id).read();
        }
        let previous = expression_begin(node);
        let id = ptr::addr_of!(STATE.expression).read();
        expression_end(previous);
        ptr::addr_of_mut!(STATE.origin_statement).write(statement);
        ptr::addr_of_mut!(STATE.origin_node).write(node);
        ptr::addr_of_mut!(STATE.origin_id).write(id);
        id
    }
}

unsafe fn object_binding(r: &Registers, d: &Descriptor, function: u32, value: u32) {
    unsafe {
        if d.object_binding == 0 {
            return;
        }
        let (object, info) = match d.object_binding {
            // The island's CALL return slot is in addition to PUSHFD's slot.
            1 => (
                read::<*const u8>(r.slot(3).unwrap().wrapping_add(8) as *const u8, 0x1c),
                r.slot(4).unwrap() as *const u8,
            ),
            2 | 3 => (
                r.slot(4).unwrap() as *const u8,
                r.slot(0).unwrap() as *const u8,
            ),
            _ => {
                OracleTrace_Printf(c"pv.gap reason=object_binding_descriptor\n".as_ptr());
                return;
            }
        };
        if object.is_null() || info.is_null() {
            OracleTrace_Printf(
                c"pv.gap reason=object_binding_null function=%lu value=%lu\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let name = read::<*const u8>(object, 10);
        let text = if name.is_null() {
            ptr::null()
        } else {
            name.add(10)
        };
        let hex = ptr::addr_of_mut!(HEX).cast::<u8>();
        let mut length = 0usize;
        if !text.is_null() {
            while length < 256 && text.add(length).read() != 0 {
                let byte = text.add(length).read();
                let digit = |v: u8| if v < 10 { b'0' + v } else { b'a' + v - 10 };
                hex.add(length * 2).write(digit(byte >> 4));
                hex.add(length * 2 + 1).write(digit(byte & 15));
                length += 1;
            }
        }
        hex.add(length * 2).write(0);
        let ty = read::<*const u8>(object, 14);
        OracleTrace_Printf(c"pv.value.object function=%lu value=%lu object=%08x info=%08x part=%lu datatype=%d storage=%u flags_before=%u type_kind=%d type_size=%ld name_known=%d name_complete=%d name_hex=%s\n".as_ptr(),
            function,value,object as u32,info as u32,d.object_binding,i32::from(read::<u8>(object,2)),u32::from(read::<u16>(object,22)),
            u32::from(read::<u8>(info,36)),expression::type_id(ty),expression::type_size(ty),i32::from(!name.is_null()),
            i32::from(!name.is_null() && (length < 256 || text.add(length).read() == 0)),hex.cast::<core::ffi::c_char>());
    }
}

#[unsafe(export_name = "OracleRust_CounterProbeV2")]
pub unsafe extern "C" fn observe(r: &Registers, site: u32, d: &Descriptor) {
    unsafe {
        if !enabled() {
            return;
        }
        if d.role == 6 {
            let parent = ptr::addr_of!(STATE).read();
            let depth = ptr::addr_of!(DEPTH).read();
            if parent.id != 0 {
                if depth >= 16 {
                    OracleTrace_Printf(
                        c"pv.gap reason=function_context_depth site=%lx\n".as_ptr(),
                        site,
                    );
                    return;
                }
                ptr::addr_of_mut!(STACK)
                    .cast::<State>()
                    .add(depth)
                    .write(parent);
                ptr::addr_of_mut!(DEPTH).write(depth + 1);
            }
            let id = ptr::addr_of!(NEXT).read().wrapping_add(1);
            ptr::addr_of_mut!(NEXT).write(id);
            let mut state = EMPTY;
            state.id = id;
            state.object = r.slot(7).unwrap_or(0) as *const u8;
            state.initial = d.initial;
            state.first = d.first;
            state.last = d.last;
            state.physical = d.physical;
            for i in 0..5 {
                state.counts[i] = d.counts.add(i).read_unaligned();
            }
            ptr::addr_of_mut!(STATE).write(state);
            OracleTrace_Printf(c"pv.function.begin schema=1 function=%lu parent=%lu site=%lx c0=%lu c1=%lu c2=%lu c3=%lu c4=%lu coverage=direct_counter_writes link=%s\n".as_ptr(),
                id,parent.id,site,state.counts[0],state.counts[1],state.counts[2],state.counts[3],state.counts[4],
                if state.object.is_null(){c"?".as_ptr().cast()}else{pcode::link_name(state.object)});
            return;
        }
        if d.role == 7 {
            let state = ptr::addr_of!(STATE).read();
            if state.id == 0 {
                OracleTrace_Printf(
                    c"pv.gap reason=function_end_without_begin site=%lx\n".as_ptr(),
                    site,
                );
                return;
            }
            OracleTrace_Printf(
                c"pv.function.end function=%lu births=%lu graphs=%lu ranges=%lu c0=%lu c1=%lu c2=%lu c3=%lu c4=%lu\n"
                    .as_ptr(),
                state.id,
                state.sequence,
                state.graphs,
                state.ranges,
                state.counts[0],
                state.counts[1],
                state.counts[2],
                state.counts[3],
                state.counts[4],
            );
            let depth = ptr::addr_of!(DEPTH).read();
            if depth > 0 {
                let mut previous = ptr::addr_of!(STACK).cast::<State>().add(depth - 1).read();
                for i in 0..5 {
                    previous.counts[i] = d.counts.add(i).read_unaligned();
                }
                ptr::addr_of_mut!(STATE).write(previous);
                ptr::addr_of_mut!(DEPTH).write(depth - 1);
            } else {
                ptr::addr_of_mut!(STATE).write(EMPTY);
            }
            return;
        }
        if d.role == 8 || d.role == 9 {
            let state = ptr::addr_of!(STATE).read();
            if state.id == 0 {
                OracleTrace_Printf(c"pv.gap reason=category_without_function\n".as_ptr());
                return;
            }
            ptr::addr_of_mut!(STATE.ranges).write(state.ranges.wrapping_add(1));
            if d.role == 8 {
                ptr::addr_of_mut!(STATE.category_ready).write(1);
                OracleTrace_Printf(
                    c"pv.category.initial function=%lu births=%lu i2=%d i3=%d i4=%d\n".as_ptr(),
                    state.id,
                    state.sequence,
                    i32::from(d.initial.read_unaligned()),
                    i32::from(d.initial.add(1).read_unaligned()),
                    i32::from(d.initial.add(2).read_unaligned()),
                );
            } else {
                ptr::addr_of_mut!(STATE.category_ready).write(state.category_ready | 2);
                OracleTrace_Printf(c"pv.category.window function=%lu births=%lu f2=%d f3=%d f4=%d l2=%ld l3=%ld l4=%ld\n".as_ptr(), state.id,state.sequence,
                    i32::from(d.first.read_unaligned()),i32::from(d.first.add(1).read_unaligned()),i32::from(d.first.add(2).read_unaligned()),
                    d.last.read_unaligned(),d.last.add(1).read_unaligned(),d.last.add(2).read_unaligned());
            }
            return;
        }
        let class = if d.selector >= 0 {
            d.selector as u32
        } else {
            r.slot((-1 - d.selector) as usize).unwrap_or(u32::MAX)
        };
        if class >= 5 {
            OracleTrace_Printf(
                c"pv.gap reason=counter_class site=%lx cls=%lu\n".as_ptr(),
                site,
                class,
            );
            return;
        }
        let counter = if d.selector >= 0 {
            d.counter
        } else {
            d.counter.add(class as usize)
        };
        let after = counter.read_unaligned();
        let physical = d.physical.add(class as usize).read_unaligned();
        let state = ptr::addr_of!(STATE).read();
        if state.id == 0 {
            OracleTrace_Printf(
                c"pv.counter.orphan site=%lx role=%lu cls=%lu after=%lu\n".as_ptr(),
                site,
                d.role,
                class,
                after,
            );
            return;
        }
        let old = state.counts[class as usize];
        if d.role == 1 {
            if old.wrapping_add(1) != after {
                OracleTrace_Printf(c"pv.gap reason=counter_discontinuity function=%lu cls=%lu old=%lu after=%lu site=%lx\n".as_ptr(),state.id,class,old,after,site);
            }
            let sequence = state.sequence.wrapping_add(1);
            ptr::addr_of_mut!(STATE.sequence).write(sequence);
            let statement = ptr::addr_of!(gCurrentStatement).read_unaligned();
            let origin = statement_origin(statement);
            OracleTrace_Printf(c"pv.value.birth schema=1 function=%lu value=%lu cls=%lu register=%lu count=%lu physical=%lu site=%lx expression=%lu phase=%lu statement=%08x line=%ld category=unknown\n".as_ptr(),
                state.id,sequence,class,after.wrapping_sub(1),after,physical,site,origin,state.phase,statement as u32,
                if statement.is_null(){-1}else{read::<i32>(statement,22)});
            object_binding(r, d, state.id, sequence);
        } else {
            OracleTrace_Printf(c"pv.counter.change function=%lu role=%lu cls=%lu old=%lu after=%lu physical=%lu site=%lx phase=%lu\n".as_ptr(),
                state.id,d.role,class,old,after,physical,site,state.phase);
        }
        ptr::addr_of_mut!(STATE.counts)
            .cast::<u32>()
            .add(class as usize)
            .write(after);
    }
}
