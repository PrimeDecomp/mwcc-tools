//! Version-specific frontend observers. All compiler work stays in retail code.
use crate::abi::{OracleTrace_Printf, read, sprintf, strlen as length, wanted};
use core::{ffi::c_char, mem::MaybeUninit, ptr};
// COpts always_inline, inline_bottom_up (traced as auto=), inline limit; CInlineInfo kind.
#[cfg(feature = "frontend-gc13")]
const OFFSETS: (usize, usize, usize, usize) = (0xd5, 0x72, 0x68, 0xa0);
#[cfg(feature = "frontend-gc132")]
const OFFSETS: (usize, usize, usize, usize) = (0xd8, 0x72, 0x68, 0xa2);
#[cfg(any(feature = "frontend-gc26", feature = "frontend-gc27"))]
const OFFSETS: (usize, usize, usize, usize) = (0xdc, 0x76, 0x6c, 0xa4);
unsafe extern "C" {
    fn OracleRust_LinkName(object: *const u8) -> *const c_char;
    fn OracleRust_OriginFind(reference: *const u8) -> i32;
    fn OracleRust_OriginWalk(expression: *const u8, parent: i32, link: *const c_char, level: i32);
    fn OracleRust_PrintChain(reference: *const u8);
    fn OracleRust_InlineLine() -> i32;
    static mut copts: u8;
    static mut data_00582468: i16;
    static mut data_005dfb66: u8;
    static mut data_005dfb68: i32;
    static mut data_005dfb62: *const u8;
    static mut pending_prec_nodes: *const u8;
    static mut anyerrors: u8;
    #[cfg_attr(feature = "frontend-gc13", link_name = "can_inline__retail")]
    #[cfg_attr(feature = "frontend-gc132", link_name = "fn_0055ed70__retail")]
    #[cfg_attr(
        any(feature = "frontend-gc26", feature = "frontend-gc27"),
        link_name = "fn_00560c80__retail"
    )]
    fn decision_retail(node: *const u8) -> u8;
    #[cfg_attr(
        feature = "frontend-gc13",
        link_name = "inline_call_expression__retail"
    )]
    #[cfg_attr(feature = "frontend-gc132", link_name = "fn_0055f350__retail")]
    #[cfg_attr(
        any(feature = "frontend-gc26", feature = "frontend-gc27"),
        link_name = "fn_00561260__retail"
    )]
    fn expansion_retail(node: *const u8) -> *const u8;
    #[cfg_attr(
        feature = "frontend-gc13",
        link_name = "CInline_DispatchNextDeferredNode__retail"
    )]
    #[cfg_attr(feature = "frontend-gc132", link_name = "fn_0055a7f0__retail")]
    #[cfg_attr(
        any(feature = "frontend-gc26", feature = "frontend-gc27"),
        link_name = "fn_0055c730__retail"
    )]
    fn dispatch_retail() -> u8;
    fn fn_0055e540__retail(info: *const u8, running: i32, depth: i32) -> i32;
    fn fn_0055e600__retail(expression: *const u8, running: i32, depth: i32) -> i32;
}
static mut NAME: [u8; 512] = [0; 512];
static mut CALLS: [u8; 4096] = [0; 4096];
static mut NEST: i32 = 0;
static mut QUIET: i32 = 0;
const FULL_LIMIT: i32 = 0x3fffffff;
unsafe fn copy(out: *mut u8, text: *const c_char, n: usize) {
    unsafe {
        for i in 0..n {
            out.add(i).write_volatile(text.add(i).read_volatile() as u8);
        }
    }
}
unsafe fn callee_name(mut function: *const u8) -> *const c_char {
    unsafe {
        if !function.is_null() && read::<u8>(function, 2) == 6 {
            function = read(function, 42);
        }
        if function.is_null() || read::<*const u8>(function, 10).is_null() {
            return c"?".as_ptr();
        }
        let mut parts = [ptr::null::<c_char>(); 16];
        let mut count = 0;
        let mut scope = read::<*const u8>(function, 6);
        while !scope.is_null() && count < 16 {
            let name = read::<*const u8>(scope, 4);
            if !name.is_null() {
                parts[count] = name.add(10).cast();
                count += 1;
            }
            scope = read(scope, 0);
        }
        let buffer = ptr::addr_of_mut!(NAME).cast::<u8>();
        let mut used = 0;
        while count > 0 {
            count -= 1;
            let size = length(parts[count]);
            if used + size + 2 >= 511 {
                break;
            }
            copy(buffer.add(used), parts[count], size);
            buffer.add(used + size).write(b':');
            buffer.add(used + size + 1).write(b':');
            used += size + 2;
        }
        let text = read::<*const u8>(function, 10).add(10).cast::<c_char>();
        let size = length(text).min(511 - used);
        copy(buffer.add(used), text, size);
        buffer.add(used + size).write(0);
        buffer.cast()
    }
}
unsafe fn candidate(object: *const u8, reference: *const u8) -> bool {
    unsafe {
        let ty = read::<*const u8>(object, 14);
        !ty.is_null()
            && read::<u8>(ty, 0) == 6
            && (read::<u32>(object, 18) & 0x10 != 0 || read::<u32>(ty, 22) & 0x800 != 0)
            && (read::<u8>(object, 2) == 3
                || read::<u8>(object, 2) == 4 && read::<u16>(reference, 2) & 0x80 != 0)
    }
}
#[cfg_attr(feature = "frontend-gc13", unsafe(export_name = "can_inline"))]
#[cfg_attr(feature = "frontend-gc132", unsafe(export_name = "fn_0055ed70"))]
#[cfg_attr(
    any(feature = "frontend-gc26", feature = "frontend-gc27"),
    unsafe(export_name = "fn_00560c80")
)]
unsafe extern "C" fn decision(node: *const u8) -> u8 {
    unsafe {
        let mut function = read::<*const u8>(node, 14);
        if read::<u8>(function, 2) == 6 {
            function = read(function, 42);
        }
        let info = if candidate(function, node) {
            read::<*const u8>(function, 42)
        } else {
            ptr::null()
        };
        let max = ptr::addr_of!(data_005dfb68).read();
        let result = decision_retail(node);
        let options = ptr::addr_of!(copts);
        let name = callee_name(function);
        let nesting = i32::from(ptr::addr_of!(data_00582468).read());
        let inline_bit = i32::from(read::<u32>(function, 18) & 0x10 != 0);
        let always = i32::from(read::<u8>(options, OFFSETS.0));
        let auto = i32::from(read::<u8>(options, OFFSETS.1));
        let limit = i32::from(read::<i16>(options, OFFSETS.2));
        let nstmts = if info.is_null() {
            -1
        } else {
            i32::from(read::<u16>(info, 12))
        };
        let line = OracleRust_InlineLine();
        // Mangling can allocate a hash entry and emit an arena record. Finish
        // compiler lookups before starting this multipart output record.
        let link = OracleRust_LinkName(function);
        OracleTrace_Printf(c"inl.can function=%s result=%d depth=%d inline_bit=%d always=%d auto=%d limit=%d body=%d nstmts=%d line=%d link=".as_ptr(),
            name, i32::from(result), nesting, inline_bit, always, auto, limit,
            i32::from(!info.is_null()), nstmts, line);
        OracleTrace_Printf(c"%s chain=".as_ptr(), link);
        OracleRust_PrintChain(node);
        OracleTrace_Printf(c"\n".as_ptr());
        // (the plain names, and what decided: an inline candidate or not, the expansion pass and the size limit)
        if wanted(c"MWCC_ORACLE_NAMES") {
            OracleTrace_Printf(
                c"inl.decide caller=%s callee=%s candidate=%d pass=%d max=%ld\n".as_ptr(),
                plain_name(ptr::addr_of!(data_005dfb62).read()),
                plain_name(function),
                i32::from(candidate(function, node)),
                nesting,
                max,
            );
            // (which of the check's returns decided, in its order: an inline candidate; always inline; bottom-up,
            // a body and the size estimate; else, in a nested pass without an inline depth, a body and the
            // statement limits of the pass)
            let reason = if !candidate(function, node) {
                c"not_inline_candidate"
            } else if always != 0 {
                c"always_inline"
            } else if auto != 0 {
                if info.is_null() {
                    c"no_body_yet"
                } else if result == 0 {
                    c"too_big"
                } else {
                    c"ok"
                }
            } else if nesting > 0 && limit == 0 {
                if info.is_null() {
                    c"no_body_yet"
                } else if nstmts > 10 {
                    c"stmt_limit_10"
                } else if nesting > 1 && nstmts > 7 {
                    c"stmt_limit_7"
                } else if nesting > 2 && nstmts > 3 {
                    c"stmt_limit_3"
                } else {
                    c"ok"
                }
            } else {
                c"ok"
            };
            OracleTrace_Printf(c"inl.reason reason=%s\n".as_ptr(), reason.as_ptr());
        }
        result
    }
}
/// An object's own name (HashNameNode.name is an inline tail at byte 10).
unsafe fn plain_name(object: *const u8) -> *const c_char {
    unsafe {
        if object.is_null() || read::<*const u8>(object, 10).is_null() {
            c"?".as_ptr()
        } else {
            read::<*const u8>(object, 10).add(10).cast()
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_0055e540(info: *const u8, running: i32, depth: i32) -> i32 {
    unsafe {
        if ptr::addr_of!(NEST).read() == 0
            && ptr::addr_of!(QUIET).read() == 0
            && wanted(c"MWCC_ORACLE_FULL_COST")
        {
            let met = ptr::addr_of!(data_005dfb66).read();
            ptr::addr_of_mut!(QUIET).write(1);
            let result = fn_0055e540__retail(info, running, depth);
            ptr::addr_of_mut!(QUIET).write(0);
            let after = ptr::addr_of!(data_005dfb66).read();
            let threshold = ptr::addr_of!(data_005dfb68).read();
            ptr::addr_of_mut!(data_005dfb66).write(met);
            ptr::addr_of_mut!(data_005dfb68).write(FULL_LIMIT);
            ptr::addr_of_mut!(QUIET).write(1);
            let limited = fn_0055e540__retail(info, running, depth) > FULL_LIMIT;
            ptr::addr_of_mut!(QUIET).write(0);
            ptr::addr_of_mut!(data_005dfb66).write(1);
            let full = fn_0055e540__retail(info, running, depth);
            ptr::addr_of_mut!(data_005dfb68).write(threshold);
            ptr::addr_of_mut!(data_005dfb66).write(after);
            OracleTrace_Printf(c"inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d nest=%d full=%d limit=%d\n".as_ptr(),
                if info.is_null() { -1 } else { i32::from(read::<u16>(info, 12)) }, running, depth, result, threshold, ptr::addr_of!(NEST).read(), full, i32::from(limited));
            return result;
        }
        let result = fn_0055e540__retail(info, running, depth);
        if ptr::addr_of!(QUIET).read() == 0 {
            OracleTrace_Printf(
                c"inl.cost nstmts=%d running=%d depth=%d cost=%d threshold=%d nest=%d\n".as_ptr(),
                if info.is_null() {
                    -1
                } else {
                    i32::from(read::<u16>(info, 12))
                },
                running,
                depth,
                result,
                ptr::addr_of!(data_005dfb68).read(),
                ptr::addr_of!(NEST).read(),
            );
        }
        result
    }
}
unsafe fn append(buffer: *mut u8, used: &mut i32, text: *const c_char) {
    unsafe {
        let mut size = length(text) as i32;
        if *used + size >= 255 {
            size = 254 - *used;
        }
        if size <= 0 {
            return;
        }
        copy(buffer.add(*used as usize), text, size as usize);
        *used += size;
        buffer.add(*used as usize).write(0);
    }
}
unsafe fn describe(expr: *const u8, buffer: *mut u8, used: &mut i32, level: i32) {
    unsafe {
        if expr.is_null() {
            append(buffer, used, c"?".as_ptr());
            return;
        }
        if level > 5 {
            append(buffer, used, c"..".as_ptr());
            return;
        }
        let mut number = MaybeUninit::<[u8; 16]>::uninit();
        let text = number.as_mut_ptr().cast::<c_char>();
        let kind = read::<u8>(expr, 0);
        match kind {
            56 => {
                let object = read::<*const u8>(expr, 14);
                let name = if object.is_null() {
                    ptr::null()
                } else {
                    read::<*const u8>(object, 10)
                };
                append(
                    buffer,
                    used,
                    if name.is_null() {
                        c"?".as_ptr()
                    } else {
                        name.add(10).cast()
                    },
                );
            }
            50 => {
                sprintf(text, c"%d".as_ptr(), read::<i32>(expr, 18));
                append(buffer, used, text);
            }
            61 | 62 => {
                sprintf(
                    text,
                    c"%s%d".as_ptr(),
                    if kind == 61 {
                        c"arg".as_ptr()
                    } else {
                        c"loc".as_ptr()
                    },
                    read::<i32>(expr, 14),
                );
                append(buffer, used, text);
            }
            4 => {
                append(buffer, used, c"*(".as_ptr());
                describe(read(expr, 14), buffer, used, level + 1);
                append(buffer, used, c")".as_ptr());
            }
            15 | 30 => {
                describe(read(expr, 14), buffer, used, level + 1);
                append(
                    buffer,
                    used,
                    if kind == 15 {
                        c"+".as_ptr()
                    } else {
                        c"=".as_ptr()
                    },
                );
                describe(read(expr, 18), buffer, used, level + 1);
            }
            54 | 55 => {
                let reference = read::<*const u8>(expr, 14);
                append(
                    buffer,
                    used,
                    if !reference.is_null() && read::<u8>(reference, 0) == 56 {
                        callee_name(read(reference, 14))
                    } else {
                        c"(*)".as_ptr()
                    },
                );
                append(buffer, used, c"()".as_ptr());
            }
            _ => {
                sprintf(text, c"e%d".as_ptr(), i32::from(kind));
                append(buffer, used, text);
                if kind <= 7 || kind == 48 {
                    append(buffer, used, c"(".as_ptr());
                    describe(read(expr, 14), buffer, used, level + 1);
                    append(buffer, used, c")".as_ptr());
                }
            }
        }
    }
}
unsafe fn body_calls(node: *const u8, depth: i32, buffer: *mut u8, used: &mut usize) {
    unsafe {
        if node.is_null() {
            return;
        }
        match read::<u8>(node, 0) {
            0..=8 | 48 | 49 => body_calls(read(node, 14), depth, buffer, used),
            9..=47 | 57 | 58 => {
                body_calls(read(node, 14), depth, buffer, used);
                body_calls(read(node, 18), depth, buffer, used);
            }
            54 | 55 => {
                let reference = read::<*const u8>(node, 14);
                if read::<u8>(reference, 0) == 56 {
                    let mut callee = read::<*const u8>(reference, 14);
                    if candidate(callee, reference) {
                        let mark = if read::<*const u8>(callee, 42).is_null() {
                            b'?'
                        } else if depth == 0 {
                            b'!'
                        } else {
                            0
                        };
                        while read::<u8>(callee, 2) == 6 && !read::<*const u8>(callee, 42).is_null()
                        {
                            callee = read(callee, 42);
                        }
                        let link = OracleRust_LinkName(callee);
                        let size = length(link);
                        if *used + size + 3 < 4000 {
                            if *used != 0 {
                                buffer.add(*used).write(b'|');
                                *used += 1;
                            }
                            if mark != 0 {
                                buffer.add(*used).write(mark);
                                *used += 1;
                            }
                            copy(buffer.add(*used), link, size);
                            *used += size;
                            buffer.add(*used).write(0);
                        }
                    }
                }
                let mut arg = read::<*const u8>(node, 18);
                while !arg.is_null() {
                    body_calls(read(arg, 4), depth, buffer, used);
                    arg = read(arg, 0);
                }
            }
            53 => {
                body_calls(read(node, 14), depth, buffer, used);
                body_calls(read(node, 18), depth, buffer, used);
                body_calls(read(node, 22), depth, buffer, used);
            }
            69 => {
                let list = read::<*const u8>(node, 14);
                let member = read::<*const u8>(list, 4);
                if !member.is_null() {
                    body_calls(member, depth, buffer, used);
                }
            }
            _ => (),
        }
    }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn fn_0055e600(expr: *const u8, running: i32, depth: i32) -> i32 {
    unsafe {
        if ptr::addr_of!(QUIET).read() != 0
            || !(wanted(c"MWCC_ORACLE_INLINE_COST") || wanted(c"MWCC_ORACLE_FULL_COST"))
        {
            return fn_0055e600__retail(expr, running, depth);
        }
        ptr::addr_of_mut!(NEST).write(ptr::addr_of!(NEST).read().wrapping_add(1));
        let result = fn_0055e600__retail(expr, running, depth);
        ptr::addr_of_mut!(NEST).write(ptr::addr_of!(NEST).read().wrapping_sub(1));
        let mut what = MaybeUninit::<[u8; 256]>::uninit();
        let buffer = what.as_mut_ptr().cast::<u8>();
        buffer.write(0);
        describe(expr, buffer, &mut 0, 0);
        let nesting = ptr::addr_of!(NEST).read();
        let expression_kind = if expr.is_null() {
            -1
        } else {
            i32::from(read::<u8>(expr, 0))
        };
        let mut call_metadata = false;
        let mut link = c"?".as_ptr();
        let mut flags = 0u32;
        let mut cand = false;
        let mut body = false;
        let mut limited = false;
        if !expr.is_null() && matches!(read::<u8>(expr, 0), 54 | 55) {
            let reference = read::<*const u8>(expr, 14);
            if !reference.is_null()
                && read::<u8>(reference, 0) == 56
                && !read::<*const u8>(reference, 14).is_null()
            {
                let mut callee = read::<*const u8>(reference, 14);
                cand = candidate(callee, reference);
                body = cand && !read::<*const u8>(callee, 42).is_null();
                limited = depth == 0 && body && callee != ptr::addr_of!(data_005dfb62).read();
                while read::<u8>(callee, 2) == 6 && !read::<*const u8>(callee, 42).is_null() {
                    callee = read(callee, 42);
                }
                let ty = read::<*const u8>(callee, 14);
                link = OracleRust_LinkName(callee);
                flags = if !ty.is_null() && read::<u8>(ty, 0) == 6 {
                    read::<u32>(ty, 22)
                } else {
                    0
                };
                call_metadata = true;
            }
        }
        let calls = ptr::addr_of_mut!(CALLS).cast::<u8>();
        calls.write(0);
        let mut used = 0;
        // Both this walk and the direct callee lookup above can allocate while
        // obtaining link names. Keep their order, ahead of every output prefix.
        body_calls(expr, depth, calls, &mut used);
        OracleTrace_Printf(
            c"inl.expr nest=%d type=%d cost=%d ".as_ptr(),
            nesting,
            expression_kind,
            result.wrapping_sub(running),
        );
        if call_metadata {
            OracleTrace_Printf(
                c"link=%s flags=%x cand=%d body=%d %s".as_ptr(),
                link,
                flags,
                i32::from(cand),
                i32::from(body),
                if limited {
                    c"limit=1 ".as_ptr()
                } else {
                    c"".as_ptr()
                },
            );
        }
        if used != 0 {
            OracleTrace_Printf(c"calls=%s ".as_ptr(), calls);
        }
        OracleTrace_Printf(c"what=%s\n".as_ptr(), buffer);
        result
    }
}
#[cfg_attr(
    feature = "frontend-gc13",
    unsafe(export_name = "inline_call_expression")
)]
#[cfg_attr(feature = "frontend-gc132", unsafe(export_name = "fn_0055f350"))]
#[cfg_attr(
    any(feature = "frontend-gc26", feature = "frontend-gc27"),
    unsafe(export_name = "fn_00561260")
)]
unsafe extern "C" fn expansion(expr: *const u8) -> *const u8 {
    unsafe {
        let mut function = read::<*const u8>(read(expr, 14), 14);
        if !function.is_null() && read::<u8>(function, 2) == 6 {
            function = read(function, 42);
        }
        let info = if !function.is_null()
            && matches!(read::<u8>(function, 2), 3 | 4)
            && read::<usize>(function, 42) >= 0x10000
        {
            read::<*const u8>(function, 42)
        } else {
            ptr::null()
        };
        let name = callee_name(function);
        let nesting = i32::from(ptr::addr_of!(data_00582468).read());
        let kind = if info.is_null() {
            -1
        } else {
            i32::from(read::<u8>(info, OFFSETS.3))
        };
        let line = OracleRust_InlineLine();
        let link = if function.is_null() {
            c"?".as_ptr()
        } else {
            OracleRust_LinkName(function)
        };
        OracleTrace_Printf(
            c"inl.begin function=%s depth=%d body=%d kind=%d line=%d link=".as_ptr(),
            name,
            nesting,
            i32::from(!info.is_null()),
            kind,
            line,
        );
        OracleTrace_Printf(c"%s\n".as_ptr(), link);
        let parent = OracleRust_OriginFind(read(expr, 14));
        let result = expansion_retail(expr);
        OracleTrace_Printf(
            c"inl.end function=%s expanded=%d\n".as_ptr(),
            callee_name(function),
            i32::from(result != expr),
        );
        if result != expr && !function.is_null() {
            OracleRust_OriginWalk(result, parent, OracleRust_LinkName(function), 0);
        }
        result
    }
}
#[cfg_attr(
    feature = "frontend-gc13",
    unsafe(export_name = "CInline_DispatchNextDeferredNode")
)]
#[cfg_attr(feature = "frontend-gc132", unsafe(export_name = "fn_0055a7f0"))]
#[cfg_attr(
    any(feature = "frontend-gc26", feature = "frontend-gc27"),
    unsafe(export_name = "fn_0055c730")
)]
unsafe extern "C" fn dispatch() -> u8 {
    unsafe {
        let work = ptr::addr_of!(pending_prec_nodes).read();
        if !wanted(c"MWCC_ORACLE_INSTANTIATION") {
            return dispatch_retail();
        }
        if !work.is_null() && ptr::addr_of!(anyerrors).read() == 0 {
            let object = read::<*const u8>(work, 4);
            OracleTrace_Printf(
                c"tp.parse.begin kind=%d link=%s\n".as_ptr(),
                i32::from(read::<u8>(work, 34)),
                if object.is_null() {
                    c"-".as_ptr()
                } else {
                    OracleRust_LinkName(object)
                },
            );
        }
        let result = dispatch_retail();
        if !work.is_null() && result != 0 && ptr::addr_of!(pending_prec_nodes).read() != work {
            let object = read::<*const u8>(work, 4);
            OracleTrace_Printf(
                c"tp.parse kind=%d body=%d link=%s\n".as_ptr(),
                i32::from(read::<u8>(work, 34)),
                i32::from(!object.is_null() && read::<usize>(object, 42) >= 0x10000),
                if object.is_null() {
                    c"-".as_ptr()
                } else {
                    OracleRust_LinkName(object)
                },
            );
        }
        result
    }
}
