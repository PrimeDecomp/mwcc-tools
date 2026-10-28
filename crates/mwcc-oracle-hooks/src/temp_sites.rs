//! The original's calls of `create_temp_object`, by gc2's sites: what the WebAssembly oracle gives as a temporary's
//! creator (`ce.object caller=`), where the x86 oracle gives the call's return address.
//!
//! gc2's calls go through `ORACLE_TEMP(SITE, create_temp_object(...))` (`include/compiler/OracleProbes.h`). The
//! original inlines two functions with sites at several calls, `CExcept_GetETEMPObject` (sites 5 and 6) and
//! `CInline_LoadToTemp` (16): their calls go through `ORACLE_TEMP_INLINED(COPY, ...)`, and a site's row is the
//! copy's. Calls are in the same functions in GC 2.7, 2.6 and 1.3.2; the optimizer's are at the same addresses in
//! 2.7 and 2.6. `crates/mwcc-oracle-core/tests/temp_sites.rs` checks the rows against gc2's sources and the
//! original executables.

/// No call in that original: gc2's function is not in it (no caller, so dead-stripped).
pub const NONE: u32 = 0;
/// The original inlines `create_temp_object` itself there, so its hook sees no call: nothing is recorded.
pub const INLINED: u32 = 1;

/// A row of SITES: the site, the copy (0 outside the inlined functions), gc2's unit (its function), and the call's
/// return address in GC 2.7, GC 2.6 and GC 1.3.2.
pub type Site = (u8, u8, &'static str, u32, u32, u32);

#[rustfmt::skip]
pub static SITES: [Site; 52] = [
    (1, 0, "src/backend/GCCInlineAsm.c", 0x57fab9, 0x57efb9, 0x57d529), // gcc_parse_name
    (2, 0, "src/frontend/CABI.c", 0x55b19d, 0x55a69d, 0x55925d), // CABI_InitVBaseCtorOffsets
    (3, 0, "src/frontend/CException.c", 0x52197c, 0x521a5c, 0x520aec), // CExcept_ScanThrowExpression
    (4, 0, "src/frontend/CException.c", 0x521288, 0x521368, 0x5203f8), // CExcept_ScanTryBlock
    // (CExcept_GetETEMPObject, in CExcept_TempTrans_ETEMP and in CExcept_TempTransFuncCall)
    (5, 1, "src/frontend/CException.c", 0x52118b, 0x52126b, 0x5202fb),
    (6, 1, "src/frontend/CException.c", 0x52119d, 0x52127d, 0x52030d),
    (5, 2, "src/frontend/CException.c", 0x5208d9, 0x5209b9, 0x51fa49),
    (6, 2, "src/frontend/CException.c", 0x5208eb, 0x5209cb, 0x51fa5b),
    (7, 0, "src/frontend/CException.c", 0x520e93, 0x520f73, 0x520003), // CExcept_TransNewException
    (8, 0, "src/frontend/CException.c", 0x520cb3, 0x520d93, 0x51fe23), // CExcept_TransInitTryCatch
    (9, 0, "src/frontend/CException.c", 0x520944, 0x520a24, 0x51fab4), // CExcept_TempTransFuncCall
    (10, 0, "src/frontend/CException.c", 0x52042f, 0x52050f, 0x51f59f), // CExcept_TempTransform
    (11, 0, "src/frontend/CException.c", 0x5200a3, 0x520183, 0x51f213), // CExcept_InsertSpecificationActions
    (12, 0, "src/frontend/CExpr2.c", 0x4b75a2, 0x4b75a2, 0x4b67c2), // CExpr_NewExceptionSafeAlloc
    (13, 0, "src/frontend/CFunc.c", NONE, NONE, NONE), // CFunc_TempTransDestroy
    (14, 0, "src/frontend/CFunc.c", INLINED, INLINED, INLINED), // DestructorReturnTransform
    (15, 0, "src/frontend/CInit.c", 0x50bab2, 0x50bb92, 0x50ad32), // CInit_RefTempTransform, in CInit_InitReference
    // (CInline_LoadToTemp, in CInline_SerializeEFORCELOAD (in CInline_SerializeExpr), CInline_SerializeELOR's two
    // calls, CInline_SerializeELAND's, CInline_SerializeENULLCHECK's and CInline_SerializeECOND's)
    (16, 3, "src/frontend/CInline.c", 0x560041, 0x55f541, 0x55e131),
    (16, 4, "src/frontend/CInline.c", 0x56093e, 0x55fe3e, 0x55ea2e),
    (16, 5, "src/frontend/CInline.c", 0x5609f3, 0x55fef3, 0x55eae3),
    (16, 6, "src/frontend/CInline.c", 0x56075e, 0x55fc5e, 0x55e84e),
    (16, 7, "src/frontend/CInline.c", 0x560813, 0x55fd13, 0x55e903),
    (16, 8, "src/frontend/CInline.c", 0x5605a3, 0x55faa3, 0x55e693),
    (16, 9, "src/frontend/CInline.c", 0x560666, 0x55fb66, 0x55e756),
    (16, 10, "src/frontend/CInline.c", 0x56031e, 0x55f81e, 0x55e40e),
    (16, 11, "src/frontend/CInline.c", 0x560433, 0x55f933, 0x55e523),
    (17, 0, "src/frontend/CInline.c", NONE, NONE, NONE), // CInline_ForceReverseEvaluation
    (18, 0, "src/optimizer/CIRTransform.c", NONE, NONE, NONE), // CIRTrans_GetTemporary
    // (CIRTrans_SetupMultiAccessOperand, in CIRTrans_TransformOpAss)
    (19, 0, "src/optimizer/CIRTransform.c", 0x52ba25, 0x52bb05, 0x52ab75),
    (20, 0, "src/optimizer/IroExprRegeneration.c", 0x4aba19, 0x4aba19, 0x4aac89), // RebuildPossibleCondExpression
    (21, 0, "src/optimizer/IroExprRegeneration.c", 0x4a93c3, 0x4a93c3, 0x4a8633), // GeneratePossibleCondAss
    (22, 0, "src/optimizer/IroLinearForm.c", 0x46f5e8, 0x46f5e8, 0x46e9c8), // AssignCommaRToTemp
    (23, 0, "src/optimizer/IroLinearForm.c", NONE, NONE, NONE), // AssignDangerousArgumentToTemp
    (24, 0, "src/optimizer/IroLinearForm.c", 0x46f475, 0x46f475, 0x46e855), // CreateTempAssignmentToZero
    (25, 0, "src/optimizer/IroLinearForm.c", 0x46f315, 0x46f315, 0x46e6f5), // CreateTempAssignmentToOne
    // (GenerateNullcheckExprTempAssignment's two, one call in the original)
    (26, 0, "src/optimizer/IroLinearForm.c", 0x46f070, 0x46f070, 0x46e450),
    (27, 0, "src/optimizer/IroLinearForm.c", 0x46f070, 0x46f070, 0x46e450),
    (28, 0, "src/optimizer/IroLinearForm.c", 0x46ef18, 0x46ef18, 0x46e2f8), // GenerateExpr1TempAssignment
    (29, 0, "src/optimizer/IroLinearForm.c", 0x46edbd, 0x46edbd, 0x46e19d), // GenerateExpr2TempAssignment
    (30, 0, "src/optimizer/IroLinearForm.c", 0x46ec6d, 0x46ec6d, 0x46e04d), // GenerateForceLoadTempAssignment
    (31, 0, "src/optimizer/IroLoop.c", 0x4a798c, 0x4a798c, 0x4a6cec), // CreateRIS
    (32, 0, "src/optimizer/IroLoop.c", 0x4a7c30, 0x4a7c30, 0x4a6f90), // CreateRIS
    (33, 0, "src/optimizer/IroLoop.c", 0x4a5f51, 0x4a5f51, 0x4a52b1), // MyHandleLoop_Motion
    (34, 0, "src/optimizer/IroUnrollLoop.c", 0x4a11b3, 0x4a11b3, 0x4a0593), // BuildOrigIterationCount
    (35, 0, "src/optimizer/IroUnrollLoop.c", 0x4a0e11, 0x4a0e11, 0x4a01f1), // BuildOrigIterationCount_DoWhile
    (36, 0, "src/optimizer/IroUnrollLoop.c", 0x4a0cd4, 0x4a0cd4, 0x4a00b4), // BuildNewFinalvalue
    (37, 0, "src/optimizer/IroUnrollLoop.c", 0x4a0b9e, 0x4a0b9e, 0x49ff7e), // BuildPreAlignTemp
    (38, 0, "src/optimizer/IroUnrollLoop.c", 0x4a097f, 0x4a097f, 0x49fd5f), // BuildNewFinalvalue_DoWhile
    (39, 0, "src/optimizer/IroUnrollLoop.c", 0x4a07e7, 0x4a07e7, 0x49fbc7), // BuildUnrolledFinalvalue_DoWhile
    (40, 0, "src/optimizer/IROUseDef.c", 0x45dedf, 0x45dedf, 0x45d56f), // SplitOffRange
    (41, 0, "src/optimizer/IroUtil.c", 0x468721, 0x468721, 0x467b01), // IRO_GetTemp
    (42, 0, "src/optimizer/IroVars.c", 0x461748, 0x461748, 0x460b28), // IRO_ScalarizeClassDataMembers
];

/// The return address of the original's call at SITE in COPY, in the version source_arenas::version() numbers
/// VERSION (3 GC 2.7, 4 GC 2.6, 2 GC 1.3.2); NONE for none (an unknown site, or another version), or INLINED.
/// A site outside the copies is its own with any COPY (one a longjmp out of ORACLE_TEMP_INLINED left).
pub fn site(site: u8, copy: u8, version: u32) -> u32 {
    let mut index = 0;
    while index < SITES.len() {
        let (s, c, _, gc2_7, gc2_6, gc1_3_2) = SITES[index];
        if s == site && (c == copy || c == 0) {
            return match version {
                3 => gc2_7,
                4 => gc2_6,
                2 => gc1_3_2,
                _ => NONE,
            };
        }
        index += 1;
    }
    NONE
}
