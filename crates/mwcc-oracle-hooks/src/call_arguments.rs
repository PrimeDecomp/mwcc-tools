//! Guarded regular-call argument contexts; this is creation-role evidence, not lifetime ancestry.
use crate::{
    abi::{OracleTrace_Printf, read},
    lifecycle, pcode,
};
use core::{ffi::CStr, ptr};

#[derive(Clone, Copy)]
pub(crate) struct Counters {
    pub calls: u32,
    pub arguments: u32,
    pub episodes: u32,
    pub births: u32,
    pub operands: u32,
    pub instructions: u32,
    pub gaps: u32,
}
pub(crate) const EMPTY_COUNTERS: Counters = Counters {
    calls: 0,
    arguments: 0,
    episodes: 0,
    births: 0,
    operands: 0,
    instructions: 0,
    gaps: 0,
};
#[derive(Clone, Copy)]
struct Argument {
    context: *const u8,
    node: *const u8,
    primary: u32,
}
const NO_ARGUMENT: Argument = Argument {
    context: ptr::null(),
    node: ptr::null(),
    primary: 0,
};
#[derive(Clone, Copy)]
struct Call {
    id: u32,
    function: u32,
    item: *const u8,
    count: usize,
    bound: bool,
    complete: bool,
    episode_base: usize,
    episodes: u32,
    births: u32,
    operands: u32,
    instructions: u32,
    args: [Argument; 64],
}
const NO_CALL: Call = Call {
    id: 0,
    function: 0,
    item: ptr::null(),
    count: 0,
    bound: false,
    complete: false,
    episode_base: 0,
    episodes: 0,
    births: 0,
    operands: 0,
    instructions: 0,
    args: [NO_ARGUMENT; 64],
};
#[derive(Clone, Copy)]
struct Episode {
    id: u32,
    function: u32,
    call: usize,
    ordinal: usize,
    block: *const u8,
    tail: *const u8,
}
const NO_EPISODE: Episode = Episode {
    id: 0,
    function: 0,
    call: 0,
    ordinal: 0,
    block: ptr::null(),
    tail: ptr::null(),
};
static mut CALLS: [Call; 32] = [NO_CALL; 32];
static mut CALL_DEPTH: usize = 0;
static mut NEXT_CALL: u32 = 0;
static mut EPISODES: [Episode; 256] = [NO_EPISODE; 256];
static mut EPISODE_DEPTH: usize = 0;
static mut NEXT_EPISODE: u32 = 0;
unsafe extern "C" {
    fn FunctionCalls_GenerateCall__retail(item: u32, output: u32) -> u32;
    fn assign_argument_locations__retail(a: u32, b: u32, c: u32, d: u32, e: u32, f: u32) -> u32;
    fn load_argument_registers__retail(context: u32) -> u32;
    fn store_argument_on_stack__retail(context: u32) -> u32;
    fn OracleRust_ArgumentForceGprRetail(operand: u32, ty: u32, requested: u32) -> u32;
    static mut gCurrentBlock: *const u8;
}
unsafe fn call(index: usize) -> *mut Call {
    unsafe { ptr::addr_of_mut!(CALLS).cast::<Call>().add(index) }
}
// All callers establish ordinal < count <= 64 before using this pointer.
unsafe fn argument(c: *mut Call, ordinal: usize) -> *mut Argument {
    unsafe { ptr::addr_of_mut!((*c).args).cast::<Argument>().add(ordinal) }
}
unsafe fn current() -> Option<usize> {
    unsafe {
        let d = ptr::addr_of!(CALL_DEPTH).read();
        if d > 0 && (*call(d - 1)).function == lifecycle::function_id() {
            Some(d - 1)
        } else {
            None
        }
    }
}
unsafe fn active() -> Option<Episode> {
    unsafe {
        let d = ptr::addr_of!(EPISODE_DEPTH).read();
        if d == 0 {
            return None;
        }
        let e = ptr::addr_of!(EPISODES).cast::<Episode>().add(d - 1).read();
        (e.function == lifecycle::function_id()).then_some(e)
    }
}
unsafe fn gap(index: usize, ordinal: usize, episode: u32, reason: &CStr) {
    unsafe {
        let c = call(index);
        (*c).complete = false;
        (*lifecycle::argument_counters()).gaps += 1;
        OracleTrace_Printf(
            c"pv.argument.gap schema=1 function=%lu call=%lu ordinal=%lu episode=%lu reason=%s\n"
                .as_ptr(),
            (*c).function,
            (*c).id,
            ordinal as u32,
            episode,
            reason.as_ptr(),
        );
    }
}
unsafe fn endpoints() -> (*const u8, *const u8) {
    unsafe {
        let block = ptr::addr_of!(gCurrentBlock).read_unaligned();
        (
            block,
            if block.is_null() {
                ptr::null()
            } else {
                read(block, 24)
            },
        )
    }
}
unsafe fn begin(index: usize, ordinal: usize, stage: u32) -> u32 {
    unsafe {
        let c = call(index);
        if ordinal >= (*c).count {
            return 0;
        }
        let depth = ptr::addr_of!(EPISODE_DEPTH).read();
        if depth == 256 {
            gap(index, ordinal, 0, c"episode_depth");
            return 0;
        }
        let a = *argument(c, ordinal);
        if a.context.is_null() || read::<*const u8>(a.context, 4) != a.node {
            gap(index, ordinal, 0, c"argument_context_changed");
            return 0;
        }
        let parent = active().map_or(0, |e| e.id);
        let id = ptr::addr_of!(NEXT_EPISODE).read().wrapping_add(1);
        ptr::addr_of_mut!(NEXT_EPISODE).write(id);
        let (block, tail) = endpoints();
        ptr::addr_of_mut!(EPISODES)
            .cast::<Episode>()
            .add(depth)
            .write(Episode {
                id,
                function: (*c).function,
                call: index,
                ordinal,
                block,
                tail,
            });
        ptr::addr_of_mut!(EPISODE_DEPTH).write(depth + 1);
        (*c).episodes += 1;
        (*lifecycle::argument_counters()).episodes += 1;
        if stage == 1 || stage == 2 {
            (*argument(c, ordinal)).primary += 1
        }
        OracleTrace_Printf(c"pv.argument.begin schema=1 function=%lu call=%lu ordinal=%lu episode=%lu parent_episode=%lu stage=%s context=%08x node=%08x operand=%08x births_before=%lu block=%08x tail=%08x\n".as_ptr(),(*c).function,(*c).id,ordinal as u32,id,parent,match stage{1=>c"register",2=>c"stack",3=>c"dispatch",_=>c"force_gpr"}.as_ptr(),a.context as u32,a.node as u32,a.context.add(8) as u32,lifecycle::birth_sequence(),block as u32,tail as u32);
        id
    }
}
unsafe fn operand(e: Episode, stage: &CStr) {
    unsafe {
        let c = call(e.call);
        let p = (*argument(c, e.ordinal)).context.add(8);
        let kind = p.read();
        if kind > 11 {
            gap(e.call, e.ordinal, e.id, c"unsupported_operand_kind");
            return;
        }
        OracleTrace_Printf(c"pv.argument.operand schema=1 function=%lu call=%lu ordinal=%lu episode=%lu stage=%s operand=%08x kind=%lu".as_ptr(),e.function,(*c).id,e.ordinal as u32,e.id,stage.as_ptr(),p as u32,u32::from(kind));
        match kind {
            0 | 5 | 6 | 7 => {
                OracleTrace_Printf(
                    c" cls=%lu register=%lu".as_ptr(),
                    match kind {
                        5 => 3u32,
                        6 => 2,
                        7 => 1,
                        _ => 4,
                    },
                    u32::from(read::<u16>(p, 2)),
                );
            }
            3 => OracleTrace_Printf(
                c" cls=4 register=%lu second=%lu".as_ptr(),
                u32::from(read::<u16>(p, 2)),
                u32::from(read::<u16>(p, 4)),
            ),
            1 | 9 => OracleTrace_Printf(
                c" cls=4 register=%lu displacement=%ld".as_ptr(),
                u32::from(read::<u16>(p, 2)),
                read::<i32>(p, 8),
            ),
            2 | 10 => OracleTrace_Printf(
                c" cls=4 register=%lu index=%lu".as_ptr(),
                u32::from(read::<u16>(p, 2)),
                u32::from(read::<u16>(p, 6)),
            ),
            4 => OracleTrace_Printf(c" immediate=%ld".as_ptr(), read::<i32>(p, 12)),
            8 | 11 => OracleTrace_Printf(
                c" object=%08x displacement=%ld".as_ptr(),
                read::<u32>(p, 16),
                read::<i32>(p, 8),
            ),
            _ => {}
        }
        OracleTrace_Printf(c"\n".as_ptr());
        (*c).operands += 1;
        (*lifecycle::argument_counters()).operands += 1;
    }
}
unsafe fn end(token: u32) {
    unsafe {
        if token == 0 {
            return;
        }
        let Some(e) = active() else { return };
        if e.id != token {
            gap(e.call, e.ordinal, e.id, c"episode_unbalanced");
            return;
        }
        let c = call(e.call);
        let (block, tail) = endpoints();
        let mut complete = !block.is_null()
            && block == e.block
            && (e.tail.is_null() || read::<*const u8>(e.tail, 8) == block)
            && (tail.is_null() || read::<*const u8>(tail, 8) == block);
        let mut count = 0u32;
        if complete {
            let mut next = if e.tail.is_null() {
                read::<*const u8>(block, 20)
            } else {
                read(e.tail, 0)
            };
            if tail == e.tail {
                next = ptr::null()
            }
            while !next.is_null() {
                if count == 4096 || read::<*const u8>(next, 8) != block {
                    complete = false;
                    break;
                }
                let n = read::<i16>(next, 34);
                if !(0..=256).contains(&n) {
                    complete = false;
                    break;
                }
                OracleTrace_Printf(c"pv.argument.instruction schema=1 function=%lu call=%lu ordinal=%lu episode=%lu index=%lu ".as_ptr(),e.function,(*c).id,e.ordinal as u32,e.id,count);
                pcode::instruction(c"".as_ptr(), next);
                count += 1;
                if next == tail {
                    break;
                }
                next = read(next, 0);
                if next.is_null() {
                    complete = false
                }
            }
            if tail != e.tail && count == 0 {
                complete = false
            }
        }
        if !complete {
            gap(e.call, e.ordinal, e.id, c"pcode_interval_incomplete")
        }
        OracleTrace_Printf(c"pv.argument.end schema=1 function=%lu call=%lu ordinal=%lu episode=%lu births_after=%lu block=%08x tail=%08x instructions=%lu pcode_complete=%u\n".as_ptr(),e.function,(*c).id,e.ordinal as u32,e.id,lifecycle::birth_sequence(),block as u32,tail as u32,count,u32::from(complete));
        (*c).instructions += count;
        (*lifecycle::argument_counters()).instructions += count;
        ptr::addr_of_mut!(EPISODE_DEPTH).write(ptr::addr_of!(EPISODE_DEPTH).read() - 1);
    }
}
pub(crate) unsafe fn dispatch_begin(node: *const u8, output: *const u8) -> u32 {
    unsafe {
        let Some(i) = current() else { return 0 };
        let c = call(i);
        for ordinal in 0..(*c).count {
            let a = *argument(c, ordinal);
            if a.node == node && a.context.add(8) == output {
                return begin(i, ordinal, 3);
            }
        }
        0
    }
}
pub(crate) unsafe fn dispatch_end(token: u32, abandoned: bool) {
    unsafe {
        if token == 0 {
            return;
        }
        if let Some(e) = active() {
            if e.id == token {
                if abandoned {
                    gap(e.call, e.ordinal, e.id, c"dispatch_unwind")
                } else {
                    operand(e, c"dispatch_return")
                }
                end(token)
            }
        }
    }
}
pub(crate) unsafe fn birth(function: u32, value: u32, class: u32, register: u32) {
    unsafe {
        if let Some(e) = active() {
            if e.function != function {
                return;
            }
            let c = call(e.call);
            OracleTrace_Printf(c"pv.argument.birth schema=1 function=%lu call=%lu ordinal=%lu episode=%lu value=%lu cls=%lu register=%lu\n".as_ptr(),function,(*c).id,e.ordinal as u32,e.id,value,class,register);
            (*c).births += 1;
            (*lifecycle::argument_counters()).births += 1
        }
    }
}
unsafe fn find(operand_or_context: *const u8, is_operand: bool) -> Option<(usize, usize)> {
    unsafe {
        let i = current()?;
        let c = call(i);
        for ordinal in 0..(*c).count {
            let a = *argument(c, ordinal);
            if (if is_operand {
                a.context.add(8)
            } else {
                a.context
            }) == operand_or_context
            {
                return Some((i, ordinal));
            }
        }
        None
    }
}
unsafe fn bind(head: *const u8, list: *const u8, skip: *const u8) {
    unsafe {
        let Some(i) = current() else { return };
        let c = call(i);
        if (*c).bound {
            gap(i, 0, 0, c"duplicate_argument_assignment");
            return;
        }
        (*c).bound = true;
        let mut context = head;
        let mut source = list;
        while !context.is_null() {
            let ordinal = (*c).count;
            if ordinal == 64 {
                gap(i, ordinal, 0, c"argument_bound");
                break;
            }
            for previous in 0..ordinal {
                if (*argument(c, previous)).context == context {
                    gap(i, ordinal, 0, c"argument_cycle");
                    return;
                }
            }
            let mut skipped = 0;
            while !source.is_null() && read::<*const u8>(source, 4) == skip {
                source = read(source, 0);
                skipped += 1;
                if skipped == 64 {
                    gap(i, ordinal, 0, c"source_list_bound");
                    return;
                }
            }
            if source.is_null() {
                gap(i, ordinal, 0, c"missing_source_argument");
                return;
            }
            let node = read::<*const u8>(context, 4);
            if node.is_null() || read::<*const u8>(source, 4) != node {
                gap(i, ordinal, 0, c"source_context_mismatch");
                return;
            }
            let ty = read::<*const u8>(node, 6);
            if ty.is_null() {
                gap(i, ordinal, 0, c"null_argument_type");
                return;
            }
            (*argument(c, ordinal)) = Argument {
                context,
                node,
                primary: 0,
            };
            (*c).count += 1;
            (*lifecycle::argument_counters()).arguments += 1;
            let flags = read::<u16>(context, 46);
            let evaluated = read::<u16>(context, 44);
            OracleTrace_Printf(c"pv.argument.bind schema=1 function=%lu call=%lu ordinal=%lu list=%08x context=%08x node=%08x kind=%lu type=%08x type_kind=%lu type_size=%ld hascall=%lu flags=%lu gpr=%d second_gpr=%d fpr=%d vector=%d stack_offset=%ld evaluated=%lu\n".as_ptr(),(*c).function,(*c).id,ordinal as u32,source as u32,context as u32,node as u32,u32::from(node.read()),ty as u32,u32::from(ty.read()),read::<i32>(ty,2),u32::from(read::<u8>(node,5)),u32::from(flags),i32::from(read::<i16>(context,36)),i32::from(read::<i16>(context,38)),i32::from(read::<i16>(context,40)),i32::from(read::<i16>(context,42)),read::<i32>(context,32),u32::from(evaluated));
            if evaluated > 1 || !matches!(flags & 0x27, 1 | 2 | 4 | 32) {
                gap(i, ordinal, 0, c"unsupported_argument_location")
            }
            context = read(context, 0);
            source = read(source, 0);
        }
        if !source.is_null() {
            let mut skipped = 0;
            while !source.is_null() && read::<*const u8>(source, 4) == skip {
                source = read(source, 0);
                skipped += 1;
                if skipped == 64 {
                    gap(i, (*c).count, 0, c"source_list_bound");
                    return;
                }
            }
            if !source.is_null() {
                gap(i, (*c).count, 0, c"unbound_source_arguments")
            }
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn FunctionCalls_GenerateCall(item: u32, output: u32) -> u32 {
    unsafe {
        if !lifecycle::enabled() || lifecycle::function_id() == 0 {
            return FunctionCalls_GenerateCall__retail(item, output);
        }
        let depth = ptr::addr_of!(CALL_DEPTH).read();
        (*lifecycle::argument_counters()).calls += 1;
        if depth == 32 {
            gap(depth - 1, 0, 0, c"call_depth");
            return FunctionCalls_GenerateCall__retail(item, output);
        }
        let id = ptr::addr_of!(NEXT_CALL).read().wrapping_add(1);
        ptr::addr_of_mut!(NEXT_CALL).write(id);
        let parent = current().map_or(0, |i| (*call(i)).id);
        let c = call(depth);
        // Do not copy the large argument array or synthesize a runtime memcpy.
        // Only slots below count are observable, and bind initializes each slot.
        (*c).count = 0;
        (*c).bound = false;
        (*c).episodes = 0;
        (*c).births = 0;
        (*c).operands = 0;
        (*c).instructions = 0;
        (*c).id = id;
        (*c).function = lifecycle::function_id();
        (*c).item = item as *const u8;
        (*c).complete = true;
        (*c).episode_base = ptr::addr_of!(EPISODE_DEPTH).read();
        ptr::addr_of_mut!(CALL_DEPTH).write(depth + 1);
        let p = item as *const u8;
        let kind = if p.is_null() { 0 } else { p.read() };
        let supported = matches!(kind, 54 | 55) && output != 0;
        let callee = if supported {
            read::<*const u8>(p, 14)
        } else {
            ptr::null()
        };
        let ck = if callee.is_null() { 0 } else { callee.read() };
        let object = if ck == 56 {
            read::<*const u8>(callee, 14)
        } else {
            ptr::null()
        };
        let ty = if supported {
            read::<*const u8>(p, 22)
        } else {
            ptr::null()
        };
        let ret = if ty.is_null() {
            ptr::null()
        } else {
            read::<*const u8>(ty, 14)
        };
        let cached = if !object.is_null() && matches!(read::<u8>(object, 2), 3 | 4) {
            read::<*const u8>(object, 50)
        } else {
            ptr::null()
        };
        let name = if cached.is_null() {
            ptr::null()
        } else {
            cached.add(10)
        };
        let mut bytes = 0usize;
        let mut complete = false;
        if !name.is_null() {
            while bytes < 256 {
                if name.add(bytes).read() == 0 {
                    complete = true;
                    break;
                }
                bytes += 1
            }
        }
        OracleTrace_Printf(c"pv.call.begin schema=1 function=%lu function_object=%08x call=%lu parent_call=%lu parent_expression=%lu item=%08x kind=%lu callee_node=%08x callee_kind=%lu callee_object=%08x callee_known=%u callee_complete=%u callee_hex=".as_ptr(),(*c).function,lifecycle::function_object()as u32,id,parent,lifecycle::expression_id(),item,u32::from(kind),callee as u32,u32::from(ck),object as u32,u32::from(!name.is_null()),u32::from(complete));
        for index in 0..bytes {
            OracleTrace_Printf(c"%02x".as_ptr(), u32::from(name.add(index).read()))
        }
        OracleTrace_Printf(c" return_type=%08x type_kind=%ld type_size=%ld output=%08x scope=guarded_regular_call_arguments_v1 ordinal_kind=lowered_argument_context_order\n".as_ptr(),ret as u32,if ret.is_null(){-1}else{i32::from(ret.read())},if ret.is_null(){-1}else{read::<i32>(ret,2)},output);
        if !supported || ret.is_null() {
            gap(depth, 0, 0, c"unsupported_call_item")
        }
        let result = FunctionCalls_GenerateCall__retail(item, output);
        if ptr::addr_of!(EPISODE_DEPTH).read() != (*c).episode_base {
            gap(depth, 0, 0, c"call_episode_unbalanced");
            ptr::addr_of_mut!(EPISODE_DEPTH).write((*c).episode_base)
        }
        if !(*c).bound {
            gap(depth, 0, 0, c"missing_argument_assignment")
        }
        for ordinal in 0..(*c).count {
            if (*argument(c, ordinal)).primary == 0 {
                gap(depth, ordinal, 0, c"missing_primary_argument_episode")
            }
        }
        OracleTrace_Printf(c"pv.call.end schema=1 function=%lu call=%lu arguments=%lu episodes=%lu births=%lu operands=%lu instructions=%lu complete=%u\n".as_ptr(),(*c).function,id,(*c).count as u32,(*c).episodes,(*c).births,(*c).operands,(*c).instructions,u32::from((*c).complete));
        ptr::addr_of_mut!(CALL_DEPTH).write(depth);
        result
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn assign_argument_locations(
    a: u32,
    b: u32,
    c: u32,
    d: u32,
    e: u32,
    f: u32,
) -> u32 {
    unsafe {
        let result = assign_argument_locations__retail(a, b, c, d, e, f);
        if lifecycle::enabled() {
            if let Some(i) = current() {
                if !(*call(i)).item.is_null() && read::<u32>((*call(i)).item, 18) == b {
                    bind(result as *const u8, b as *const u8, a as *const u8)
                } else {
                    gap(i, 0, 0, c"assign_call_item_mismatch")
                }
            }
        }
        result
    }
}
unsafe fn primary(context: u32, stack: bool) -> u32 {
    unsafe {
        let token = if lifecycle::enabled() {
            find(context as *const u8, false)
                .map_or(0, |(i, o)| begin(i, o, if stack { 2 } else { 1 }))
        } else {
            0
        };
        let result = if stack {
            store_argument_on_stack__retail(context)
        } else {
            load_argument_registers__retail(context)
        };
        if token != 0 {
            if let Some(e) = active() {
                if e.id == token {
                    operand(e, c"helper_return")
                }
            }
            end(token)
        }
        result
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn load_argument_registers(context: u32) -> u32 {
    unsafe { primary(context, false) }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn store_argument_on_stack(context: u32) -> u32 {
    unsafe { primary(context, true) }
}
#[unsafe(export_name = "OracleRust_ArgumentForceGpr")]
unsafe extern "C" fn force(operand_ptr: u32, ty: u32, requested: u32) -> u32 {
    unsafe {
        let token = if lifecycle::enabled() {
            match find(operand_ptr as *const u8, true) {
                Some((i, o)) => {
                    let a = *argument(call(i), o);
                    if read::<u32>(a.node, 6) != ty
                        || read::<u16>(a.context, 36) != (requested as u16)
                        || read::<u16>(a.context, 46) & 0x27 != 1
                    {
                        gap(i, o, 0, c"force_argument_context_mismatch");
                        0
                    } else {
                        begin(i, o, 4)
                    }
                }
                None => {
                    if let Some(i) = current() {
                        gap(i, 0, 0, c"force_argument_context_missing")
                    }
                    0
                }
            }
        } else {
            0
        };
        if token != 0 {
            if let Some(e) = active() {
                operand(e, c"force_before")
            }
        }
        let result = OracleRust_ArgumentForceGprRetail(operand_ptr, ty, requested);
        if token != 0 {
            if let Some(e) = active() {
                operand(e, c"force_after")
            }
            end(token)
        }
        result
    }
}
