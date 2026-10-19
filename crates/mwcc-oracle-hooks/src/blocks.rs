//! Block/stage observers. C layout and flag values are probed in blocks_layout.c.
use crate::{
    abi::{OracleTrace_Printf, getenv, read, wanted},
    pcode,
};
use core::{ffi::CStr, ptr};

unsafe extern "C" {
    fn fn_00436390__retail(expression: *const u8);
    fn COptimizer_Optimize__retail(function: *const u8);
    fn Peephole_MergeAdjacentBlocks__retail(object: *const u8, merge_prologue: u8);
    static mut gCurrentStatement: *const u8;
    static mut gCurrentBlock: *const u8;
}
unsafe fn maybe_link(object: *const u8) -> *const core::ffi::c_char {
    unsafe {
        if object.is_null() {
            c"-".as_ptr()
        } else {
            pcode::link_name(object)
        }
    }
}
unsafe fn list_count(mut link: *const u8) -> i32 {
    unsafe {
        let mut count = 0;
        while !link.is_null() {
            count += 1;
            link = read(link, 0);
        }
        count
    }
}
unsafe fn block_list(phase: &CStr, function: *const u8) {
    unsafe {
        let level = getenv(c"MWCC_ORACLE_BLOCKS".as_ptr());
        OracleTrace_Printf(
            c"bk.list phase=%s link=%s\n".as_ptr(),
            phase.as_ptr(),
            maybe_link(function),
        );
        let mut block = ptr::addr_of!(pcode::gPCodeBlocks).read_unaligned();
        while !block.is_null() {
            let successors = list_count(read(block, 16));
            let predecessors = list_count(read(block, 12));
            OracleTrace_Printf(
                c"bk.block p=%08x index=%d count=%d flags=%04x succ=%d pred=%d\n".as_ptr(),
                block as u32,
                read::<i32>(block, 28),
                i32::from(read::<i16>(block, 40)),
                u32::from(read::<u16>(block, 42)),
                successors,
                predecessors,
            );
            if !level.is_null() && *level == b'2' {
                let mut ins = read::<*const u8>(block, 20);
                while !ins.is_null() {
                    pcode::instruction(c"bk.i".as_ptr(), ins);
                    ins = read(ins, 0);
                }
            }
            block = read(block, 0);
        }
    }
}

#[unsafe(no_mangle)]
unsafe extern "C" fn fn_00436390(expression: *const u8) {
    unsafe {
        let before = ptr::addr_of!(gCurrentBlock).read_unaligned();
        let count = if before.is_null() {
            -1
        } else {
            i32::from(read::<i16>(before, 40))
        };
        let previous = crate::lifecycle::expression_begin(expression);
        fn_00436390__retail(expression);
        crate::lifecycle::expression_end(previous);
        if wanted(c"MWCC_ORACLE_BLOCKS") {
            let statement = ptr::addr_of!(gCurrentStatement).read_unaligned();
            let after = ptr::addr_of!(gCurrentBlock).read_unaligned();
            OracleTrace_Printf(
                c"bk.stmt line=%ld before=%08x:%d after=%08x:%d\n".as_ptr(),
                if statement.is_null() {
                    -1
                } else {
                    read::<i32>(statement, 22)
                },
                before as u32,
                count,
                after as u32,
                if after.is_null() {
                    -1
                } else {
                    i32::from(read::<i16>(after, 40))
                },
            );
        }
    }
}

#[unsafe(export_name = "COptimizer_Optimize")]
unsafe extern "C" fn optimize(function: *const u8) {
    unsafe {
        crate::lifecycle::phase(1);
        if wanted(c"MWCC_ORACLE_BLOCKS") {
            block_list(c"raw", function);
        }
        if wanted(c"MWCC_ORACLE_STAGES") {
            pcode::snapshot(c"st".as_ptr(), c"codegen".as_ptr(), function);
        }
        COptimizer_Optimize__retail(function);
        crate::lifecycle::phase(2);
        if wanted(c"MWCC_ORACLE_STAGES") {
            pcode::snapshot(c"st".as_ptr(), c"optimized".as_ptr(), function);
        }
        if wanted(c"MWCC_ORACLE_BLOCKS") {
            block_list(c"optimized", function);
        }
    }
}

#[unsafe(export_name = "Peephole_MergeAdjacentBlocks")]
unsafe extern "C" fn merge(object: *const u8, merge_prologue: u8) {
    unsafe {
        if !wanted(c"MWCC_ORACLE_BLOCKS") {
            Peephole_MergeAdjacentBlocks__retail(object, merge_prologue);
            return;
        }
        OracleTrace_Printf(
            c"bk.merge prologue=%d link=%s\n".as_ptr(),
            i32::from(merge_prologue),
            maybe_link(object),
        );
        let mut block = ptr::addr_of!(pcode::gPCodeBlocks).read_unaligned();
        while !block.is_null() {
            let next = read::<*const u8>(block, 0);
            if next.is_null() {
                break;
            }
            let count = i32::from(read::<i16>(block, 40));
            let next_count = i32::from(read::<i16>(next, 40));
            let mut why = if merge_prologue == 0 && read::<u16>(block, 42) & 1 != 0 {
                Some(c"prologue")
            } else if count <= 0 {
                Some(c"empty")
            } else {
                None
            };
            let mut ins = read::<*const u8>(block, 20);
            while why.is_none() && !ins.is_null() {
                if read::<u32>(ins, 20) & 0x108 != 0 {
                    why = Some(c"call");
                }
                ins = read(ins, 0);
            }
            let reverse = read::<*const u8>(block, 24);
            if why.is_none()
                && !reverse.is_null()
                && read::<u32>(reverse, 20) & 1 != 0
                && read::<i16>(reverse, 32) != 0
            {
                why = Some(c"branch");
            }
            if why.is_none() {
                let successors = read::<*const u8>(block, 16);
                if successors.is_null()
                    || read::<*const u8>(successors, 4) != next
                    || !read::<*const u8>(successors, 0).is_null()
                {
                    why = Some(c"successors");
                }
            }
            if why.is_none() {
                let predecessors = read::<*const u8>(next, 12);
                if predecessors.is_null()
                    || read::<*const u8>(predecessors, 4) != block
                    || !read::<*const u8>(predecessors, 0).is_null()
                {
                    why = Some(c"predecessors");
                }
            }
            if why.is_none() && merge_prologue == 0 && read::<u16>(next, 42) & 2 != 0 {
                why = Some(c"epilogue");
            }
            if why.is_none() && read::<u16>(next, 42) & 0x8000 != 0 {
                why = Some(c"flag");
            }
            if why.is_none() && count + next_count > 100 {
                why = Some(c"over100");
            }
            OracleTrace_Printf(
                c"bk.pair a=%08x b=%08x counts=%d+%d why=%s\n".as_ptr(),
                block as u32,
                next as u32,
                count,
                next_count,
                why.unwrap_or(c"-").as_ptr(),
            );
            block = next;
        }
        Peephole_MergeAdjacentBlocks__retail(object, merge_prologue);
        block_list(c"merged", object);
    }
}
