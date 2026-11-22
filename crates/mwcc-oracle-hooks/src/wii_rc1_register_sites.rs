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
    (765, 3, 2, 0x5fcd3e, None),
    (769, 2, 2, 0x5fcd80, None),
    (773, 3, 2, 0x5fcd3e, None), // RC1 shares the FPR allocation with scalar floats.
    (782, 4, 2, 0x5fcebd, None),
    (789, 4, 3, 0x5fce22, Some(0x5fd2a2)),
    (790, 4, 4, 0x5fce37, Some(0x5fd2b7)),
    (795, 4, 2, 0x5fd244, Some(0x5fd314)),
];

// The source unit, line, inlined call's line (or zero), native counter write,
// register class (u8::MAX for an indexed write), and the descriptor's object, operand and origin bindings.
type Write = (&'static str, u16, u16, u32, u8, [u8; 3]);
#[rustfmt::skip]
pub const WRITES: &[Write] = &[
    ("src/backend/InstrSelection.c", 3237, 0, 0x52078d, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3258, 0, 0x52056a, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3279, 0, 0x5204b6, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3370, 1223, 0x52749b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3490, 0, 0x51fb16, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3490, 399, 0x5299fa, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3534, 0, 0x51fa66, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 3693, 0, 0x51f2cc, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5664, 0, 0x51b0a0, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5853, 0, 0x51a776, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5854, 0, 0x51a783, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5896, 0, 0x51ac4b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5900, 0, 0x51ac78, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5919, 0, 0x51acfe, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 5929, 0, 0x51ad4b, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7474, 0, 0x51626e, 3, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7568, 0, 0x51608c, 4, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7581, 0, 0x51603e, 2, [0, 0, 0]),
    ("src/backend/InstrSelection.c", 7594, 0, 0x515f2f, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 41, 0, 0x5afe8a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 56, 0, 0x5afbba, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 79, 0, 0x5afc4c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 98, 0, 0x5afd07, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 117, 0, 0x5afdc7, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 183, 0, 0x5af8bb, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 222, 0, 0x5af4c9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 242, 0, 0x5af543, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 498, 0, 0x5ae927, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 522, 0, 0x5ae987, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 555, 0, 0x5aea65, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 556, 0, 0x5aea81, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 574, 0, 0x5aea3a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 621, 0, 0x5ae7d5, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 625, 0, 0x5ae7e9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 629, 0, 0x5ae7fd, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 636, 0, 0x5ae5e7, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 645, 0, 0x5ae82d, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 672, 0, 0x5ae846, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 701, 0, 0x5ae859, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 769, 0, 0x5adeb9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 772, 0, 0x5aded9, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 814, 0, 0x5ae348, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 821, 0, 0x5adffd, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 828, 0, 0x5ae376, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 836, 0, 0x5ae3a6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 837, 0, 0x5ae3b8, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 862, 0, 0x5ae12e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 864, 0, 0x5ae44c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 865, 0, 0x5ae466, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 902, 0, 0x5adde6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 913, 0, 0x5addf9, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 940, 0, 0x5adcd6, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 945, 0, 0x5adcea, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 951, 0, 0x5adcfd, 2, [0, 0, 0]),
    ("src/backend/Operands.c", 1082, 0, 0x5ad79c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1185, 0, 0x5ad3d6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1189, 0, 0x5ad3e6, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1205, 0, 0x5ad495, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1212, 0, 0x5ad4ad, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1237, 0, 0x5ad276, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1239, 0, 0x5ad28a, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1302, 0, 0x5ad063, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1308, 0, 0x5ad0ef, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1339, 0, 0x5ace66, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1341, 0, 0x5ace7c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1349, 0, 0x5acf4f, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1354, 0, 0x5aceda, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1357, 0, 0x5acf96, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1389, 0, 0x5acca6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1397, 0, 0x5acd6d, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1402, 0, 0x5accf9, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1405, 0, 0x5acdb6, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1425, 0, 0x5acb87, 3, [0, 0, 0]),
    ("src/backend/Operands.c", 1429, 0, 0x5acbfb, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1446, 0, 0x5acb0c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1675, 0, 0x5aff0e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1676, 0, 0x5aff1e, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1681, 0, 0x5aff4c, 4, [0, 0, 0]),
    ("src/backend/Operands.c", 1688, 0, 0x5affb9, 4, [0, 0, 0]),
    ("src/backend/PCodeUtilities.c", 296, 0, 0x5b281c, 4, [0, 0, 0]),
    ("src/backend/RegisterInfo.c", 376, 0, 0x5909d7, 4, [2, 0, 0]),
    ("src/backend/RegisterInfo.c", 377, 0, 0x5909e4, 4, [3, 0, 0]),
    ("src/backend/Registers.c", 73, 0, 0x5b16fa, u8::MAX, [1, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 26, 971, 0x592187, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 172, 0, 0x59362a, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 173, 0, 0x59363b, 4, [0, 0, 0]),
    ("src/backend/SFPE_PPC_EABI.c", 1008, 0, 0x592d32, 4, [0, 0, 0]),
    ("src/backend/StackFrame.c", 1626, 0, 0x5a248f, 4, [0, 0, 0]),
    ("src/backend/StackFrame.c", 1626, 1571, 0x5a27f1, 4, [0, 0, 0]),
    ("src/optimizer/LoopOptimization.c", 856, 0, 0x6507b1, 4, [0, 0, 0]),
    ("src/optimizer/LoopOptimization.c", 1576, 0, 0x6501b7, 3, [0, 0, 1]),
    ("src/optimizer/LoopOptimization.c", 1578, 0, 0x6501ca, 4, [0, 0, 1]),
    ("src/optimizer/StructMoves.c", 27, 0, 0x5be399, 4, [0, 0, 0]),
    ("src/optimizer/StructMoves.c", 867, 0, 0x5bbe69, 4, [0, 0, 0]),
    ("src/optimizer/StructMoves.c", 892, 0, 0x5bbdd9, 4, [0, 0, 0]),
];
