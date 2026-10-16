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
    operand_binding: u32,
    data_link_offset: u32,
    origin_binding: u32,
}
const _: () = {
    assert!(core::mem::size_of::<Descriptor>() == 48);
    assert!(core::mem::align_of::<Descriptor>() == 4);
    assert!(core::mem::offset_of!(Descriptor, selector) == 12);
    assert!(core::mem::offset_of!(Descriptor, role) == 16);
    assert!(core::mem::offset_of!(Descriptor, object_binding) == 32);
    assert!(core::mem::offset_of!(Descriptor, operand_binding) == 36);
    assert!(core::mem::offset_of!(Descriptor, data_link_offset) == 40);
    assert!(core::mem::offset_of!(Descriptor, origin_binding) == 44);
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
    dispatches: u32,
    dispatch_returns: u32,
    materializations: u32,
    array_promotions: u32,
    call_results: u32,
    origin_statement: *const u8,
    origin_node: *const u8,
    origin_id: u32,
    call_arguments: crate::call_arguments::Counters,
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
    dispatches: 0,
    dispatch_returns: 0,
    materializations: 0,
    array_promotions: 0,
    call_results: 0,
    origin_statement: ptr::null(),
    origin_node: ptr::null(),
    origin_id: 0,
    call_arguments: crate::call_arguments::EMPTY_COUNTERS,
};
static mut STATE: State = EMPTY;
static mut STACK: [State; 16] = [EMPTY; 16];
static mut DEPTH: usize = 0;
static mut NEXT: u32 = 0;
static mut ENABLED: i8 = -1;
static mut TEXT: [u8; 2048] = [0; 2048];
static mut HEX: [u8; 4096] = [0; 4096];
static mut OUTPUT_HEX: [u8; 49] = [0; 49];
#[repr(C)]
pub struct ExpressionDescriptor {
    retail_target: u32,
    return_point: u32,
}
const _: () = assert!(core::mem::size_of::<ExpressionDescriptor>() == 8);
#[derive(Clone, Copy)]
struct DispatchFrame {
    function: u32,
    expression: u32,
    previous: u32,
    stack: u32,
    returned: u32,
    output: *const u8,
    target: u32,
    argument_episode: u32,
}
const NO_FRAME: DispatchFrame = DispatchFrame {
    function: 0,
    expression: 0,
    previous: 0,
    stack: 0,
    returned: 0,
    output: ptr::null(),
    target: 0,
    argument_episode: 0,
};
static mut DISPATCH_FRAMES: [DispatchFrame; 1024] = [NO_FRAME; 1024];
static mut DISPATCH_DEPTH: usize = 0;
unsafe extern "C" {
    static mut gCurrentStatement: *const u8;
    fn abort() -> !;
}

unsafe fn abandon_dispatch(frame: DispatchFrame) {
    unsafe {
        crate::call_arguments::dispatch_end(frame.argument_episode, true);
        OracleTrace_Printf(
            c"pv.gap function=%lu reason=dispatch_unwind expression=%lu\n".as_ptr(),
            frame.function,
            frame.expression,
        );
        if frame.function == function_id() {
            OracleTrace_Printf(
                c"pv.expression.end function=%lu expression=%lu abandoned=1\n".as_ptr(),
                frame.function,
                frame.expression,
            );
            ptr::addr_of_mut!(STATE.expression).write(frame.previous);
        }
    }
}
unsafe fn dispatch_output(frame: DispatchFrame) {
    unsafe {
        if frame.output.is_null() {
            OracleTrace_Printf(
                c"pv.gap function=%lu reason=null_dispatch_output expression=%lu\n".as_ptr(),
                frame.function,
                frame.expression,
            );
            return;
        }
        let p = frame.output;
        let kind = u32::from(p.read());
        OracleTrace_Printf(
            c"pv.expression.result function=%lu expression=%lu operand=%08x kind=%lu".as_ptr(),
            frame.function,
            frame.expression,
            p as u32,
            kind,
        );
        // Read only fields defined for this Operand tag. Inactive fields and
        // padding can be uninitialized and are not observation data.
        match kind {
            0 | 1 | 2 | 3 | 5 | 6 | 7 | 9 | 10 => {
                let class = match kind {
                    5 => 3,
                    6 => 2,
                    7 => 1,
                    _ => 4,
                };
                OracleTrace_Printf(
                    c" cls=%lu register=%lu".as_ptr(),
                    class,
                    u32::from(read::<u16>(p, 2)),
                );
                if kind == 3 {
                    OracleTrace_Printf(c" second=%lu".as_ptr(), u32::from(read::<u16>(p, 4)));
                }
                if kind == 2 || kind == 10 {
                    OracleTrace_Printf(c" index=%lu".as_ptr(), u32::from(read::<u16>(p, 6)));
                }
            }
            4 => OracleTrace_Printf(c" immediate=%ld".as_ptr(), read::<i32>(p, 12)),
            8 | 11 => OracleTrace_Printf(c" object=%08x".as_ptr(), read::<u32>(p, 16)),
            _ => {}
        }
        if matches!(kind, 1 | 8 | 9 | 11) {
            OracleTrace_Printf(c" displacement=%ld".as_ptr(), read::<i32>(p, 8));
        }
        OracleTrace_Printf(c"\n".as_ptr());
    }
}

/// Machine islands preserve all caller arguments, GPRs, flags and FPU/SSE
/// state. No reconstructed C signature is used to call the retail generator.
#[unsafe(export_name = "OracleRust_ExpressionProbeV1")]
pub unsafe extern "C" fn expression_probe(r: &Registers, site: u32, d: &ExpressionDescriptor) {
    unsafe {
        let stack = r.slot(3).unwrap_or(0).wrapping_add(4);
        let frames = ptr::addr_of_mut!(DISPATCH_FRAMES).cast::<DispatchFrame>();
        let mut depth = ptr::addr_of!(DISPATCH_DEPTH).read();
        if site == 1 {
            if !enabled() || function_id() == 0 {
                return;
            }
            while depth > 0 && frames.add(depth - 1).read().stack <= stack {
                depth -= 1;
                abandon_dispatch(frames.add(depth).read());
            }
            ptr::addr_of_mut!(DISPATCH_DEPTH).write(depth);
            if depth == 1024 {
                OracleTrace_Printf(
                    c"pv.gap function=%lu reason=dispatch_depth\n".as_ptr(),
                    function_id(),
                );
                return;
            }
            let node = read::<*const u8>(stack as *const u8, 4);
            let a = read::<u32>(stack as *const u8, 8);
            let b = read::<u32>(stack as *const u8, 12);
            let output = read::<*const u8>(stack as *const u8, 16);
            let previous = expression_scope(node, c"codegen_dispatch", d.retail_target, a, b);
            let expression = ptr::addr_of!(STATE.expression).read();
            frames.add(depth).write(DispatchFrame {
                function: function_id(),
                expression,
                previous,
                stack,
                returned: read(stack as *const u8, 0),
                output,
                target: d.retail_target,
                argument_episode: crate::call_arguments::dispatch_begin(node, output),
            });
            ptr::addr_of_mut!(DISPATCH_DEPTH).write(depth + 1);
            ptr::addr_of_mut!(STATE.dispatches)
                .write(ptr::addr_of!(STATE.dispatches).read().wrapping_add(1));
            (stack as *mut u32).write_unaligned(d.return_point);
        } else if site == 2 {
            let mut matched = None;
            for index in (0..depth).rev() {
                let frame = frames.add(index).read();
                if frame.stack == stack
                    && frame.target == d.retail_target
                    && frame.function == function_id()
                {
                    matched = Some(index);
                    break;
                }
            }
            let Some(index) = matched else {
                OracleTrace_Printf(
                    c"pv.gap reason=dispatch_return_stack function=%lu target=%lx\n".as_ptr(),
                    function_id(),
                    d.retail_target,
                );
                abort();
            };
            for discarded in (index + 1..depth).rev() {
                abandon_dispatch(frames.add(discarded).read());
            }
            let frame = frames.add(index).read();
            dispatch_output(frame);
            crate::call_arguments::dispatch_end(frame.argument_episode, false);
            expression_end(frame.previous);
            ptr::addr_of_mut!(STATE.dispatch_returns)
                .write(ptr::addr_of!(STATE.dispatch_returns).read().wrapping_add(1));
            ptr::addr_of_mut!(DISPATCH_DEPTH).write(index);
            (stack as *mut u32).write_unaligned(frame.returned);
        }
    }
}
pub(crate) unsafe fn birth_sequence() -> u32 {
    unsafe { ptr::addr_of!(STATE.sequence).read() }
}
pub(crate) unsafe fn function_object() -> *const u8 {
    unsafe { ptr::addr_of!(STATE.object).read() }
}
pub(crate) unsafe fn expression_id() -> u32 {
    unsafe { ptr::addr_of!(STATE.expression).read() }
}
pub(crate) unsafe fn argument_counters() -> *mut crate::call_arguments::Counters {
    unsafe { ptr::addr_of_mut!(STATE.call_arguments) }
}
pub(crate) unsafe fn enabled() -> bool {
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
pub(crate) unsafe fn function_for_object(object: *const u8) -> u32 {
    unsafe {
        let state = ptr::addr_of!(STATE).read();
        if !object.is_null() && state.object == object {
            state.id
        } else {
            0
        }
    }
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
    unsafe { expression_scope(node, c"statement_root", 0, 0, 0) }
}
unsafe fn expression_scope(
    node: *const u8,
    scope: &core::ffi::CStr,
    target: u32,
    a: u32,
    b: u32,
) -> u32 {
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
        OracleTrace_Printf(c"pv.expression.begin function=%lu expression=%lu node=%08x kind=%d line=%ld scope=%s complete=0 parent=%lu target=%lx a=%lx b=%lx tree_hex=%s\n".as_ptr(),
            function_id(),next,node as u32,if node.is_null(){-1}else{i32::from(read::<u8>(node,0))},
            if statement.is_null(){-1}else{read::<i32>(statement,22)},scope.as_ptr(),previous,target,a,b,hex.cast::<core::ffi::c_char>());
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

/// Observe the exact source Operand still held in EBX, with its Object in EBP,
/// at the two guarded Normalize counter writes. Do not invoke the mangler or
/// otherwise populate compiler caches merely to obtain a prettier identity.
unsafe fn operand_binding(r: &Registers, d: &Descriptor, function: u32, value: u32) {
    unsafe {
        if d.operand_binding == 0 {
            return;
        }
        let operand = r.slot(4).unwrap_or(0) as *const u8;
        let object = r.slot(2).unwrap_or(0) as *const u8;
        if !matches!(d.operand_binding, 1 | 2)
            || operand.is_null()
            || object.is_null()
            || !matches!(operand.read(), 8 | 11)
            || read::<*const u8>(operand, 16) != object
        {
            OracleTrace_Printf(
                c"pv.gap reason=symbol_operand_binding function=%lu value=%lu\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let count = ptr::addr_of!(STATE.materializations).read();
        ptr::addr_of_mut!(STATE.materializations).write(count.wrapping_add(1));
        let datatype = read::<u8>(object, 2);
        let link = match datatype {
            0 => read::<*const u8>(object, d.data_link_offset as usize),
            3 | 4 => read::<*const u8>(object, 50),
            _ => ptr::null(),
        };
        let text = if link.is_null() {
            ptr::null()
        } else {
            link.add(10)
        };
        let hex = ptr::addr_of_mut!(HEX).cast::<u8>();
        let mut length = 0;
        if !text.is_null() {
            while length < 512 && text.add(length).read() != 0 {
                let byte = text.add(length).read();
                let digit = |v: u8| if v < 10 { b'0' + v } else { b'a' + v - 10 };
                hex.add(length * 2).write(digit(byte >> 4));
                hex.add(length * 2 + 1).write(digit(byte & 15));
                length += 1;
            }
        }
        hex.add(length * 2).write(0);
        OracleTrace_Printf(c"pv.value.materialization schema=1 function=%lu value=%lu part=%lu operand=%08x kind=%lu object=%08x displacement=%ld datatype=%lu storage=%lu symbol_known=%d symbol_complete=%d symbol_hex=%s\n".as_ptr(),
            function, value, d.operand_binding, operand as u32, u32::from(operand.read()), object as u32,
            read::<i32>(operand, 8), u32::from(datatype), u32::from(read::<u16>(object, 22)),
            i32::from(!link.is_null()), i32::from(!link.is_null() && (length < 512 || text.add(length).read() == 0)),
            hex.cast::<core::ffi::c_char>());
    }
}

unsafe fn bounded_hex(text: *const u8, limit: usize) -> (usize, bool) {
    unsafe {
        let hex = ptr::addr_of_mut!(HEX).cast::<u8>();
        let mut length = 0;
        if !text.is_null() {
            while length < limit && text.add(length).read() != 0 {
                let byte = text.add(length).read();
                let digit = |v: u8| if v < 10 { b'0' + v } else { b'a' + v - 10 };
                hex.add(length * 2).write(digit(byte >> 4));
                hex.add(length * 2 + 1).write(digit(byte & 15));
                length += 1;
            }
        }
        hex.add(length * 2).write(0);
        (
            length,
            !text.is_null() && (length < limit || text.add(length).read() == 0),
        )
    }
}

/// These fields are read at the two actual scalar-array counter increments,
/// before the retail store writes EAX into values[ECX]. The guarded constructor
/// and consuming loop establish the original object and element meanings.
unsafe fn array_origin(r: &Registers, function: u32, value: u32, class: u32, register: u32) {
    unsafe {
        let promotion = r.slot(1).unwrap_or(0) as *const u8; // ESI
        let index = r.slot(6).unwrap_or(u32::MAX); // ECX
        if promotion.is_null() {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=array_origin_null\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let object = read::<*const u8>(promotion, 4);
        let element_size = read::<i32>(promotion, 12);
        let array_size = read::<i32>(promotion, 16);
        let count = read::<i32>(promotion, 20);
        let flags = read::<u8>(promotion, 8);
        if object.is_null()
            || !(1..=8).contains(&count)
            || index >= count as u32
            || element_size <= 0
            || element_size.checked_mul(count) != Some(array_size)
            || flags & 1 != 0
            || !matches!(class, 3 | 4)
            || (flags & 2 != 0) != (class == 3)
            || r.slot(7) != Some(register)
        {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=array_origin_bounds\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let ty = read::<*const u8>(object, 14);
        if ty.is_null() || expression::type_id(ty) != 12 || expression::type_size(ty) != array_size
        {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=array_origin_type\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let element = read::<*const u8>(ty, 6);
        if element.is_null() || expression::type_size(element) != element_size {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=array_origin_element\n".as_ptr(),
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
        let (_, complete) = bounded_hex(text, 256);
        ptr::addr_of_mut!(STATE.array_promotions)
            .write(ptr::addr_of!(STATE.array_promotions).read().wrapping_add(1));
        OracleTrace_Printf(c"pv.value.array schema=1 function=%lu value=%lu promotion=%08x object=%08x register=%lu element_index=%lu element_size=%ld array_size=%ld element_count=%ld flags=%lu datatype=%lu storage=%lu type_kind=%d type_size=%ld name_known=%d name_complete=%d name_hex=%s\n".as_ptr(),
            function,value,promotion as u32,object as u32,register,index,element_size,array_size,count,u32::from(flags),
            u32::from(read::<u8>(object,2)),u32::from(read::<u16>(object,22)),expression::type_id(ty),expression::type_size(ty),
            i32::from(!name.is_null()),i32::from(complete),ptr::addr_of!(HEX).cast::<core::ffi::c_char>());
    }
}

/// The helper's original frame and result pointer are guarded by its full
/// retail body. The operand has only its tag set at this point; raw inactive
/// bytes are retained for audit but never interpreted as final output state.
unsafe fn call_result_origin(
    r: &Registers,
    function: u32,
    value: u32,
    class: u32,
    register: u32,
    binding: u32,
) {
    unsafe {
        let stack = r.slot(3).unwrap_or(0).wrapping_add(8) as *const u8;
        let item = read::<*const u8>(stack, 0x64);
        let output = r.slot(7).unwrap_or(0) as *const u8;
        let return_type = read::<*const u8>(stack, 0xc);
        let part = binding - 1;
        if item.is_null()
            || output.is_null()
            || return_type.is_null()
            || read::<*const u8>(stack, 0x68) != output
            || !matches!(item.read(), 54 | 55)
        {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=call_result_origin_input\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let function_type = read::<*const u8>(item, 22);
        let callee = read::<*const u8>(item, 14);
        let expected_kind = match (part, class) {
            (1, 3) => 5,
            (1, 2) => 6,
            (1, 4) => 0,
            (2 | 3, 4) => 3,
            _ => u8::MAX,
        };
        if function_type.is_null()
            || callee.is_null()
            || read::<*const u8>(function_type, 14) != return_type
            || output.read() != expected_kind
        {
            OracleTrace_Printf(
                c"pv.gap function=%lu value=%lu reason=call_result_origin_type\n".as_ptr(),
                function,
                value,
            );
            return;
        }
        let raw = ptr::addr_of_mut!(OUTPUT_HEX).cast::<u8>();
        for index in 0..24 {
            // Keep this externally owned partial snapshot scalar. Vectorizing
            // fixed-size hex conversion emits public SIMD constants into the
            // COFF support surface and combines individual observations.
            let byte = output.add(index).read_volatile();
            let digit = |v: u8| if v < 10 { b'0' + v } else { b'a' + v - 10 };
            raw.add(index * 2).write_volatile(digit(byte >> 4));
            raw.add(index * 2 + 1).write_volatile(digit(byte & 15));
        }
        raw.add(48).write(0);
        let text = ptr::addr_of_mut!(TEXT).cast::<u8>();
        text.write(0);
        expression::describe(text.cast(), 512, item, 8);
        bounded_hex(text, 2047);
        ptr::addr_of_mut!(STATE.call_results)
            .write(ptr::addr_of!(STATE.call_results).read().wrapping_add(1));
        OracleTrace_Printf(c"pv.value.call_result schema=1 function=%lu value=%lu part=%lu register=%lu item=%08x kind=%lu callee=%08x return_type=%08x type_kind=%d type_size=%ld output=%08x output_kind=%lu output_complete=0 output_hex=%s tree_known=1 tree_complete=0 tree_hex=%s\n".as_ptr(),
            function,value,part,register,item as u32,u32::from(item.read()),callee as u32,return_type as u32,
            expression::type_id(return_type),expression::type_size(return_type),output as u32,u32::from(output.read()),
            raw.cast::<core::ffi::c_char>(),ptr::addr_of!(HEX).cast::<core::ffi::c_char>());
    }
}

#[unsafe(export_name = "OracleRust_CounterProbeV6")]
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
            OracleTrace_Printf(c"pv.function.begin schema=1 function=%lu parent=%lu function_object=%08x site=%lx c0=%lu c1=%lu c2=%lu c3=%lu c4=%lu coverage=direct_counter_writes dispatch=guarded_table_v1 materialization=guarded_normalize_pair_v1 array_promotion=guarded_scalar_v1 call_result=guarded_call_result_v1 scalarization=guarded_temp_returns_v1 call_arguments=guarded_regular_call_arguments_v1 link=%s\n".as_ptr(),
                id,parent.id,state.object as u32,site,state.counts[0],state.counts[1],state.counts[2],state.counts[3],state.counts[4],
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
            let scalar = crate::scalar::take_summary(state.object);
            let (scalar_passes, scalar_bindings, scalar_first, scalar_last) =
                scalar.unwrap_or((0, 0, 0, 0));
            OracleTrace_Printf(
                c"pv.function.end function=%lu births=%lu graphs=%lu ranges=%lu dispatches=%lu dispatch_returns=%lu materializations=%lu array_promotions=%lu call_results=%lu scalarization=%s scalar_passes=%lu scalar_bindings=%lu scalar_first_pass=%lu scalar_last_pass=%lu call_arguments=%s call_count=%lu argument_count=%lu argument_episodes=%lu argument_births=%lu argument_operands=%lu argument_instructions=%lu c0=%lu c1=%lu c2=%lu c3=%lu c4=%lu\n"
                    .as_ptr(),
                state.id,
                state.sequence,
                state.graphs,
                state.ranges,
                state.dispatches,
                state.dispatch_returns,
                state.materializations,
                state.array_promotions,
                state.call_results,
                if scalar.is_some(){c"guarded_temp_returns_v1".as_ptr()}else{c"unknown".as_ptr()},
                scalar_passes,scalar_bindings,scalar_first,scalar_last,
                if state.call_arguments.gaps==0{c"guarded_regular_call_arguments_v1".as_ptr()}else{c"unknown".as_ptr()},
                state.call_arguments.calls,state.call_arguments.arguments,state.call_arguments.episodes,state.call_arguments.births,state.call_arguments.operands,state.call_arguments.instructions,
                state.counts[0],
                state.counts[1],
                state.counts[2],
                state.counts[3],
                state.counts[4],
            );
            let depth = ptr::addr_of!(DEPTH).read();
            if depth > 0 {
                // Keep the bounded observer state copy independent of compiler runtime helpers.
                let previous = ptr::addr_of!(STACK)
                    .cast::<State>()
                    .add(depth - 1)
                    .cast::<core::mem::MaybeUninit<u8>>();
                let destination = ptr::addr_of_mut!(STATE).cast::<core::mem::MaybeUninit<u8>>();
                for byte in 0..core::mem::size_of::<State>() {
                    destination
                        .add(byte)
                        .write_volatile(previous.add(byte).read_volatile());
                }
                for i in 0..5 {
                    ptr::addr_of_mut!(STATE.counts)
                        .cast::<u32>()
                        .add(i)
                        .write(d.counts.add(i).read_unaligned());
                }
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
            crate::call_arguments::birth(state.id, sequence, class, after.wrapping_sub(1));
            object_binding(r, d, state.id, sequence);
            operand_binding(r, d, state.id, sequence);
            match d.origin_binding {
                0 => {}
                1 => array_origin(r, state.id, sequence, class, after.wrapping_sub(1)),
                2..=4 => call_result_origin(
                    r,
                    state.id,
                    sequence,
                    class,
                    after.wrapping_sub(1),
                    d.origin_binding,
                ),
                _ => {
                    OracleTrace_Printf(
                        c"pv.gap function=%lu value=%lu reason=birth_origin_descriptor\n".as_ptr(),
                        state.id,
                        sequence,
                    );
                }
            }
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
