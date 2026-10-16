//! In-process i686 hooks. Host builds deliberately contain no producer code.
//! The host CLI builds the 32-bit object separately with panic=abort and no runtime.
#![no_std]

#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod abi;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod bindings;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod blocks;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod cse;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod expression;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod frame;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod graph;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod instantiation;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod local_call;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod origins;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod pcode;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod scalar;
#[cfg(all(
    target_pointer_width = "32",
    not(any(feature = "trace-output", feature = "frontend"))
))]
mod schedule;
#[cfg(all(target_pointer_width = "32", feature = "trace-output"))]
mod trace_output;

#[cfg(all(target_pointer_width = "32", feature = "frontend"))]
mod frontend;
#[cfg(all(feature = "frontend", feature = "trace-output"))]
compile_error!("frontend and trace-output are separate objects");
#[cfg(all(
    feature = "frontend",
    not(any(
        feature = "frontend-gc13",
        feature = "frontend-gc132",
        feature = "frontend-gc27"
    ))
))]
compile_error!("select exactly one frontend version");
#[cfg(any(
    all(feature = "frontend-gc13", feature = "frontend-gc132"),
    all(feature = "frontend-gc13", feature = "frontend-gc27"),
    all(feature = "frontend-gc132", feature = "frontend-gc27")
))]
compile_error!("frontend versions cannot be combined");
