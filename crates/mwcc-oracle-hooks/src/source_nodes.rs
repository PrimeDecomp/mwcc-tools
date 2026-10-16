//! Guarded source observations. Every read has a fresh snapshot identity;
//! allocation identity and actual clone edges never assert content equivalence.
#![allow(non_snake_case)] // Exact retail entry names are the wrapper ABI.
use crate::{
    abi::{OracleTrace_Printf, read, wanted},
    backend::Registers,
};
use core::{arch::naked_asm, ffi::CStr, ptr};

unsafe extern "C" {
    fn CFunc_ParseFuncDef__retail(a: u32, b: u32, c: u32, d: u32, e: u32, f: u32) -> u32;
    fn fn_00513040__retail(node: u32, mode: u32) -> u32;
    fn fn_00513240__retail(node: u32) -> u32;
    static mut evalMode: u8;
    static mut alloc_state: u8;
    static mut cscope_currentfunc: *const u8;
}
#[repr(C)]
pub struct Descriptor {
    version: u32,
    site: u32,
    kind: u32,
    statement_slot: u32,
}
const _: () = {
    assert!(core::mem::size_of::<Descriptor>() == 16);
};
#[derive(Clone, Copy)]
struct Owner {
    id: u32,
    parent: u32,
    function: u32,
}
const EMPTY_OWNER: Owner = Owner {
    id: 0,
    parent: 0,
    function: 0,
};
#[derive(Clone, Copy)]
struct Frame {
    id: u32,
    parent: u32,
    owner: u32,
    mode_scope: u32,
    input: u32,
    before: u32,
}
const EMPTY_FRAME: Frame = Frame {
    id: 0,
    parent: 0,
    owner: 0,
    mode_scope: 0,
    input: 0,
    before: 0,
};
const LIMIT: u32 = 64;
const TREE_DEPTH: usize = 32;
const TREE_LIMIT: u32 = 512;
static mut ENABLED: i8 = -1;
static mut BUSY: bool = false;
static mut STARTED: bool = false;
static mut UNSUPPORTED: u32 = 0;
static mut EVENT: u32 = 0;
static mut OWNERS: u32 = 0;
static mut OWNER_ENDS: u32 = 0;
static mut MODES: u32 = 0;
static mut MODE_ENDS: u32 = 0;
static mut CLONES: u32 = 0;
static mut CLONE_ENDS: u32 = 0;
static mut EMISSIONS: u32 = 0;
static mut EMISSION_ENDS: u32 = 0;
static mut SNAPSHOTS: u32 = 0;
static mut GAPS: u32 = 0;
static mut CHECKPOINTS: u32 = 0;
static mut OWNER_DEPTH: u32 = 0;
static mut MODE_DEPTH: u32 = 0;
static mut CLONE_DEPTH: u32 = 0;
static mut EMISSION_DEPTH: u32 = 0;
static mut OWNER_STACK: [Owner; LIMIT as usize] = [EMPTY_OWNER; LIMIT as usize];
static mut MODE_STACK: [Frame; LIMIT as usize] = [EMPTY_FRAME; LIMIT as usize];
static mut CLONE_STACK: [Frame; LIMIT as usize] = [EMPTY_FRAME; LIMIT as usize];
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
pub(crate) unsafe fn enabled() -> bool {
    unsafe {
        let mut value = ptr::addr_of!(ENABLED).read();
        if value < 0 {
            value = i8::from(wanted(c"MWCC_ORACLE_SOURCE_NODES"));
            ptr::addr_of_mut!(ENABLED).write(value);
        }
        value != 0
    }
}
unsafe fn gap(reason: &CStr, frame: u32, stage: u32, snapshot: u32, site: u32) {
    unsafe {
        ptr::addr_of_mut!(UNSUPPORTED).write(1);
        next(ptr::addr_of_mut!(GAPS));
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.gap schema=1 stream=1 event=%lu reason=%s owner=%lu frame=%lu stage=%lu snapshot=%lu site=%08lx\n".as_ptr(),event,reason.as_ptr(),owner().id,frame,stage,snapshot,site);
    }
}
unsafe fn enter() -> bool {
    unsafe {
        if !enabled() {
            return false;
        }
        if ptr::addr_of!(BUSY).read() {
            next(ptr::addr_of_mut!(GAPS));
            ptr::addr_of_mut!(UNSUPPORTED).write(1);
            return false;
        }
        ptr::addr_of_mut!(BUSY).write(true);
        if !ptr::addr_of!(STARTED).read() {
            ptr::addr_of_mut!(STARTED).write(true);
            let event = next(ptr::addr_of_mut!(EVENT));
            OracleTrace_Printf(c"pv.source.begin schema=1 stream=1 event=%lu scope=guarded_source_nodes_v1 version=%lu\n".as_ptr(),event,crate::source_arenas::version());
        }
        true
    }
}
unsafe fn leave() {
    unsafe { ptr::addr_of_mut!(BUSY).write(false) }
}
unsafe fn owner() -> Owner {
    unsafe {
        let depth = ptr::addr_of!(OWNER_DEPTH).read();
        if depth == 0 {
            EMPTY_OWNER
        } else {
            ptr::addr_of!(OWNER_STACK)
                .cast::<Owner>()
                .add((depth - 1) as usize)
                .read()
        }
    }
}
unsafe fn mode_scope() -> u32 {
    unsafe {
        let depth = ptr::addr_of!(MODE_DEPTH).read();
        if depth == 0 {
            0
        } else {
            ptr::addr_of!(MODE_STACK)
                .cast::<Frame>()
                .add((depth - 1) as usize)
                .read()
                .id
        }
    }
}
fn policy(tag: u8) -> (u32, usize) {
    match tag {
        0..=8 | 48 => (1, 1),
        9..=47 => (2, 2),
        53 => (3, 3),
        56 => (4, 0),
        50 => (5, 0),
        61 | 62 => (6, 0),
        67 => (7, 4),
        _ => (0, 0),
    }
}
struct Walk {
    frame: u32,
    stage: u32,
    count: u32,
    ancestors: [u32; TREE_DEPTH],
}
unsafe fn snapshot(w: &mut Walk, node: u32, parent: u32, edge: u32, depth: usize) -> u32 {
    unsafe {
        if node == 0 {
            return 0;
        }
        let id = next(ptr::addr_of_mut!(SNAPSHOTS));
        let mut reason = c"ok";
        let mut tag_known = 1u32;
        let mut tag = 0u8;
        let mut shape = 0;
        let mut children = 0;
        let mut flags = 0u32;
        let mut ty = 0u32;
        let mut data = [0u32; 4];
        if depth >= TREE_DEPTH {
            reason = c"depth";
            tag_known = 0;
        } else if w.count >= TREE_LIMIT {
            reason = c"capacity";
            tag_known = 0;
        } else if w.ancestors[..depth].contains(&node) {
            reason = c"cycle";
            tag_known = 0;
        } else if node < 0x10000 {
            reason = c"pointer";
            tag_known = 0;
        } else {
            tag = read::<u8>(node as *const u8, 0);
            (shape, children) = policy(tag);
            if shape == 0 {
                reason = c"unsupported";
            } else {
                flags = u32::from(read::<u16>(node as *const u8, 2));
                ty = read::<u32>(node as *const u8, 6);
                let words = if shape == 5 {
                    2
                } else if children == 0 {
                    1
                } else {
                    children
                };
                // Fixed unaligned loads keep LLVM from introducing a variable-sized
                // memcpy runtime dependency into this standalone observer object.
                if words >= 1 {
                    data[0] = read(node as *const u8, 14);
                }
                if words >= 2 {
                    data[1] = read(node as *const u8, 18);
                }
                if words >= 3 {
                    data[2] = read(node as *const u8, 22);
                }
                if words >= 4 {
                    data[3] = read(node as *const u8, 26);
                }
            }
        }
        w.count = w.count.saturating_add(1);
        let complete = u32::from(shape != 0 && tag_known != 0);
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.node schema=1 stream=1 event=%lu snapshot=%lu frame=%lu stage=%lu node=%08lx parent=%lu edge=%lu tag_known=%lu tag=%lu shape=%lu flags=%04lx type=%08lx data0=%08lx data1=%08lx data2=%08lx data3=%08lx complete=%lu reason=%s\n".as_ptr(),event,id,w.frame,w.stage,node,parent,edge,tag_known,u32::from(tag),shape,flags,ty,data[0],data[1],data[2],data[3],complete,reason.as_ptr());
        if complete != 0 && children != 0 && depth < TREE_DEPTH {
            w.ancestors[depth] = node;
            for (i, child) in data.iter().copied().enumerate().take(children) {
                snapshot(w, child, id, i as u32 + 1, depth + 1);
            }
        }
        id
    }
}
unsafe fn tree(frame: u32, stage: u32, node: u32) -> u32 {
    unsafe {
        let mut w = Walk {
            frame,
            stage,
            count: 0,
            ancestors: [0; TREE_DEPTH],
        };
        snapshot(&mut w, node, 0, 0, 0)
    }
}
unsafe fn checkpoint_inner() {
    unsafe {
        let checkpoint = next(ptr::addr_of_mut!(CHECKPOINTS));
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.checkpoint schema=1 stream=1 event=%lu checkpoints=%lu owners=%lu owner_ends=%lu modes=%lu mode_ends=%lu clones=%lu clone_ends=%lu emissions=%lu emission_ends=%lu snapshots=%lu gaps=%lu owner_depth=%lu mode_depth=%lu clone_depth=%lu emission_depth=%lu unsupported=%lu\n".as_ptr(),event,checkpoint,ptr::addr_of!(OWNERS).read(),ptr::addr_of!(OWNER_ENDS).read(),ptr::addr_of!(MODES).read(),ptr::addr_of!(MODE_ENDS).read(),ptr::addr_of!(CLONES).read(),ptr::addr_of!(CLONE_ENDS).read(),ptr::addr_of!(EMISSIONS).read(),ptr::addr_of!(EMISSION_ENDS).read(),ptr::addr_of!(SNAPSHOTS).read(),ptr::addr_of!(GAPS).read(),ptr::addr_of!(OWNER_DEPTH).read(),ptr::addr_of!(MODE_DEPTH).read(),ptr::addr_of!(CLONE_DEPTH).read(),ptr::addr_of!(EMISSION_DEPTH).read(),ptr::addr_of!(UNSUPPORTED).read());
    }
}
pub(crate) unsafe fn checkpoint() {
    unsafe {
        if enter() {
            checkpoint_inner();
            leave();
        }
    }
}
unsafe fn owner_begin(function: u32) -> u32 {
    unsafe {
        if !enter() {
            return 0;
        }
        let id = next(ptr::addr_of_mut!(OWNERS));
        let parent = owner().id;
        let depth = ptr::addr_of!(OWNER_DEPTH).read();
        if depth >= LIMIT {
            gap(c"owner_capacity", 0, 0, 0, 0);
            leave();
            return 0;
        }
        ptr::addr_of_mut!(OWNER_STACK)
            .cast::<Owner>()
            .add(depth as usize)
            .write(Owner {
                id,
                parent,
                function,
            });
        ptr::addr_of_mut!(OWNER_DEPTH).write(depth + 1);
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.owner.begin schema=1 stream=1 event=%lu owner=%lu parent_owner=%lu function=%08lx depth=%lu\n".as_ptr(),event,id,parent,function,depth+1);
        leave();
        id
    }
}
unsafe fn owner_end(id: u32) {
    unsafe {
        if id == 0 || !enter() {
            return;
        }
        next(ptr::addr_of_mut!(OWNER_ENDS));
        let item = owner();
        let depth = ptr::addr_of!(OWNER_DEPTH).read();
        if depth > 0 && item.id == id {
            ptr::addr_of_mut!(OWNER_DEPTH).write(depth - 1);
        } else {
            gap(c"owner_mismatch", 0, 0, 0, 0);
        }
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.owner.end schema=1 stream=1 event=%lu owner=%lu parent_owner=%lu function=%08lx depth=%lu\n".as_ptr(),event,id,item.parent,item.function,ptr::addr_of!(OWNER_DEPTH).read());
        checkpoint_inner();
        leave();
    }
}
unsafe fn frame_begin(input: u32, word: u32, caller: u32, clone: bool) -> u32 {
    unsafe {
        if !enter() {
            return 0;
        }
        let (stack, depthp, count) = if clone {
            (
                ptr::addr_of_mut!(CLONE_STACK).cast::<Frame>(),
                ptr::addr_of_mut!(CLONE_DEPTH),
                ptr::addr_of_mut!(CLONES),
            )
        } else {
            (
                ptr::addr_of_mut!(MODE_STACK).cast::<Frame>(),
                ptr::addr_of_mut!(MODE_DEPTH),
                ptr::addr_of_mut!(MODES),
            )
        };
        let id = next(count);
        let depth = depthp.read();
        if depth >= LIMIT {
            gap(c"frame_capacity", id, 0, 0, caller);
            leave();
            return 0;
        }
        let parent = if depth == 0 {
            0
        } else {
            stack.add((depth - 1) as usize).read().id
        };
        let own = owner().id;
        let scope = mode_scope();
        let event = next(ptr::addr_of_mut!(EVENT));
        if clone {
            OracleTrace_Printf(c"pv.source.clone.begin schema=1 stream=1 event=%lu invocation=%lu parent_clone=%lu owner=%lu mode_scope=%lu input=%08lx caller=%08lx eval_mode=%lu alloc_state=%lu depth=%lu\n".as_ptr(),event,id,parent,own,scope,input,caller,u32::from(ptr::addr_of!(evalMode).read()),u32::from(ptr::addr_of!(alloc_state).read()),depth+1);
        } else {
            OracleTrace_Printf(c"pv.source.mode.begin schema=1 stream=1 event=%lu invocation=%lu parent_mode=%lu owner=%lu input=%08lx mode_word=%lu eval_mode=%lu alloc_state=%lu depth=%lu\n".as_ptr(),event,id,parent,own,input,word,u32::from(ptr::addr_of!(evalMode).read()),u32::from(ptr::addr_of!(alloc_state).read()),depth+1);
        }
        stack.add(depth as usize).write(Frame {
            id,
            parent,
            owner: own,
            mode_scope: scope,
            input,
            before: 0,
        });
        depthp.write(depth + 1);
        let before = tree(id, if clone { 2 } else { 5 }, input);
        (*stack.add(depth as usize)).before = before;
        leave();
        id
    }
}
unsafe fn frame_end(id: u32, output: u32, clone: bool) {
    unsafe {
        if id == 0 || !enter() {
            return;
        }
        let (stack, depthp, count) = if clone {
            (
                ptr::addr_of!(CLONE_STACK).cast::<Frame>(),
                ptr::addr_of_mut!(CLONE_DEPTH),
                ptr::addr_of_mut!(CLONE_ENDS),
            )
        } else {
            (
                ptr::addr_of!(MODE_STACK).cast::<Frame>(),
                ptr::addr_of_mut!(MODE_DEPTH),
                ptr::addr_of_mut!(MODE_ENDS),
            )
        };
        next(count);
        let depth = depthp.read();
        let item = if depth == 0 {
            EMPTY_FRAME
        } else {
            stack.add((depth - 1) as usize).read()
        };
        if depth == 0 || item.id != id {
            gap(c"frame_mismatch", id, 0, 0, 0);
            leave();
            return;
        }
        let after = tree(id, if clone { 3 } else { 6 }, item.input);
        let result = tree(id, if clone { 4 } else { 7 }, output);
        depthp.write(depth - 1);
        let event = next(ptr::addr_of_mut!(EVENT));
        if clone {
            OracleTrace_Printf(c"pv.source.clone.end schema=1 stream=1 event=%lu invocation=%lu parent_clone=%lu owner=%lu mode_scope=%lu input=%08lx output=%08lx input_snapshot=%lu after_snapshot=%lu output_snapshot=%lu eval_mode=%lu alloc_state=%lu depth=%lu\n".as_ptr(),event,id,item.parent,item.owner,item.mode_scope,item.input,output,item.before,after,result,u32::from(ptr::addr_of!(evalMode).read()),u32::from(ptr::addr_of!(alloc_state).read()),depth-1);
        } else {
            OracleTrace_Printf(c"pv.source.mode.end schema=1 stream=1 event=%lu invocation=%lu parent_mode=%lu owner=%lu input=%08lx output=%08lx input_snapshot=%lu after_snapshot=%lu output_snapshot=%lu eval_mode=%lu alloc_state=%lu depth=%lu\n".as_ptr(),event,id,item.parent,item.owner,item.input,output,item.before,after,result,u32::from(ptr::addr_of!(evalMode).read()),u32::from(ptr::addr_of!(alloc_state).read()),depth-1);
            checkpoint_inner();
        }
        leave();
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn CFunc_ParseFuncDef(a: u32, b: u32, c: u32, d: u32, e: u32, f: u32) -> u32 {
    unsafe {
        let id = owner_begin(a);
        let result = CFunc_ParseFuncDef__retail(a, b, c, d, e, f);
        owner_end(id);
        result
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fn_00513040(node: u32, mode: u32) -> u32 {
    unsafe {
        let id = frame_begin(node, mode, 0, false);
        let result = fn_00513040__retail(node, mode);
        frame_end(id, result, false);
        result
    }
}
#[unsafe(naked)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn fn_00513240(_node: u32) -> u32 {
    naked_asm!("push dword ptr [esp]","push dword ptr [esp + 8]","call {handler}","add esp, 8","ret",handler=sym clone_node);
}
unsafe extern "C" fn clone_node(node: u32, caller: u32) -> u32 {
    unsafe {
        let id = frame_begin(node, 0, caller, true);
        let result = fn_00513240__retail(node);
        frame_end(id, result, true);
        result
    }
}
#[unsafe(export_name = "OracleRust_SourceProbeV1")]
pub unsafe extern "C" fn observe(r: &Registers, d: &Descriptor) {
    unsafe {
        if !enter() {
            return;
        }
        if d.version != crate::source_arenas::version() || d.kind != 1 || d.statement_slot != 4 {
            gap(c"descriptor", 0, 0, 0, d.site);
            leave();
            return;
        }
        let statement = r.slot(4).unwrap_or(0);
        if statement < 0x10000 {
            gap(c"statement", 0, 1, 0, d.site);
            leave();
            return;
        }
        let root = read::<u32>(statement as *const u8, 10);
        let own = owner();
        let current = ptr::addr_of!(cscope_currentfunc).read() as u32;
        let id = next(ptr::addr_of_mut!(EMISSIONS));
        let depth = ptr::addr_of!(EMISSION_DEPTH).read();
        ptr::addr_of_mut!(EMISSION_DEPTH).write(depth.saturating_add(1));
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.emission.begin schema=1 stream=1 event=%lu emission=%lu owner=%lu function=%08lx current_function=%08lx statement=%08lx root=%08lx site=%08lx depth=%lu\n".as_ptr(),event,id,own.id,own.function,current,statement,root,d.site,depth+1);
        let result = tree(id, 1, root);
        next(ptr::addr_of_mut!(EMISSION_ENDS));
        ptr::addr_of_mut!(EMISSION_DEPTH).write(depth);
        let event = next(ptr::addr_of_mut!(EVENT));
        OracleTrace_Printf(c"pv.source.emission.end schema=1 stream=1 event=%lu emission=%lu owner=%lu function=%08lx current_function=%08lx statement=%08lx root=%08lx site=%08lx snapshot=%lu depth=%lu\n".as_ptr(),event,id,own.id,own.function,current,statement,root,d.site,result,depth);
        leave();
    }
}
