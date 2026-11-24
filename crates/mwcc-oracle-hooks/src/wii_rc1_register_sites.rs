//! Verified counter writes in the Wii 1.0RC1 executable.

/// The writes to used_virtual_registers and the two CodeGen_Generator scope boundaries.
pub const STRUCTURAL: [u32; 6] = [
    0x5b127d, // close_temp_registers
    0x5b1337, // check_temp_registers
    0x5b17d0, // init_registers
    0x5b3f6f, // colorinstructions
    0x46a55a, // assignment to gFunction
    0x46b3c1, // normal CodeGen_Generator epilogue
];

pub const CALL_RESULT_UNIT: &str = "src/optimizer/FunctionCalls.c";

// Source line, register class, origin binding, native write, and alternate
// native write for an aggregate returned in GPRs.
pub const CALL_RESULT_WRITES: &[(u16, u8, u8, u32, Option<u32>)] = &[
    (788, 3, 2, 0x5fcd3e, None),
    (792, 2, 2, 0x5fcd80, None),
    (796, 3, 2, 0x5fcd3e, None), // RC1 shares the FPR allocation with scalar floats.
    (805, 4, 2, 0x5fcebd, None),
    (812, 4, 3, 0x5fce22, Some(0x5fd2a2)),
    (813, 4, 4, 0x5fce37, Some(0x5fd2b7)),
    (818, 4, 2, 0x5fd244, Some(0x5fd314)),
];

// The source unit, line, inlined call's line (or zero), native counter write,
// register class (u8::MAX for an indexed write), and the descriptor's object, operand and origin bindings.
type Write = (&'static str, u16, u16, u32, u8, [u8; 3]);
#[rustfmt::skip]
pub const WRITES: &[Write] = &[
    ("src/backend/InstrSelection.c", 3364, 0, 0x52078d, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3385, 0, 0x52056a, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3406, 0, 0x5204b6, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3524, 1305, 0x52749b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3651, 0, 0x51fb16, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3651, 430, 0x5299fa, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3702, 0, 0x51fa66, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3861, 0, 0x51f2cc, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5879, 0, 0x51b0a0, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6071, 0, 0x51a776, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6072, 0, 0x51a783, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6114, 0, 0x51ac4b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6118, 0, 0x51ac78, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6137, 0, 0x51acfe, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 6147, 0, 0x51ad4b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7692, 0, 0x51626e, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7786, 0, 0x51608c, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7799, 0, 0x51603e, 2, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7812, 0, 0x515f2f, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 41, 0, 0x5afe8a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 56, 0, 0x5afbba, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 79, 0, 0x5afc4c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 98, 0, 0x5afd07, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 117, 0, 0x5afdc7, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 183, 0, 0x5af8bb, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 222, 0, 0x5af4c9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 242, 0, 0x5af543, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 550, 0, 0x5ae927, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 574, 0, 0x5ae987, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 607, 0, 0x5aea65, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 608, 0, 0x5aea81, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 626, 0, 0x5aea3a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 673, 0, 0x5ae7d5, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 677, 0, 0x5ae7e9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 681, 0, 0x5ae7fd, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 688, 0, 0x5ae5e7, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 697, 0, 0x5ae82d, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 724, 0, 0x5ae846, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 753, 0, 0x5ae859, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 821, 0, 0x5adeb9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 824, 0, 0x5aded9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 866, 0, 0x5ae348, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 873, 0, 0x5adffd, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 880, 0, 0x5ae376, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 888, 0, 0x5ae3a6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 889, 0, 0x5ae3b8, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 914, 0, 0x5ae12e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 916, 0, 0x5ae44c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 917, 0, 0x5ae466, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 954, 0, 0x5adde6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 965, 0, 0x5addf9, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 992, 0, 0x5adcd6, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 997, 0, 0x5adcea, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 1003, 0, 0x5adcfd, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 1134, 0, 0x5ad79c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1237, 0, 0x5ad3d6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1241, 0, 0x5ad3e6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1257, 0, 0x5ad495, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1264, 0, 0x5ad4ad, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1289, 0, 0x5ad276, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1291, 0, 0x5ad28a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1354, 0, 0x5ad063, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1360, 0, 0x5ad0ef, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1391, 0, 0x5ace66, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1393, 0, 0x5ace7c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1401, 0, 0x5acf4f, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1406, 0, 0x5aceda, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1409, 0, 0x5acf96, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1441, 0, 0x5acca6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1449, 0, 0x5acd6d, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1454, 0, 0x5accf9, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1457, 0, 0x5acdb6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1477, 0, 0x5acb87, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1481, 0, 0x5acbfb, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1498, 0, 0x5acb0c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1727, 0, 0x5aff0e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1728, 0, 0x5aff1e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1733, 0, 0x5aff4c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1740, 0, 0x5affb9, 4, [0, 0, 0]),
    ("src/backend/PCodeUtilities.c", 324, 0, 0x5b281c, 4, [0, 0, 0]),
    ("src/backend/RegisterInfo.c", 411, 0, 0x5909d7, 4, [2, 0, 0]),
    ("src/backend/RegisterInfo.c", 412, 0, 0x5909e4, 4, [3, 0, 0]),
    ("src/backend/Registers.c", 73, 0, 0x5b16fa, u8::MAX, [1, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 26, 971, 0x592187, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 172, 0, 0x59362a, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 173, 0, 0x59363b, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 1008, 0, 0x592d32, 4, [0, 0, 0]),
    ("src/backend/StackFrame.c", 1632, 0, 0x5a248f, 4, [0, 0, 0]),
    ("src/backend/StackFrame.c", 1632, 1577, 0x5a27f1, 4, [0, 0, 0]),
    ("src/optimizer/LoopOptimization.c", 1030, 0, 0x6507b1, 4, [0, 0, 0]),
    ("src/optimizer/LoopOptimization.c", 1750, 0, 0x6501b7, 3, [0, 0, 1]),
    ("src/optimizer/LoopOptimization.c", 1752, 0, 0x6501ca, 4, [0, 0, 1]),
    ("src/optimizer/StructMoves.c", 31, 0, 0x5be399, 4, [0, 0, 0]),
    ("src/optimizer/StructMoves.c", 984, 0, 0x5bbe69, 4, [0, 0, 0]),
    ("src/optimizer/StructMoves.c", 1009, 0, 0x5bbdd9, 4, [0, 0, 0]),
];
