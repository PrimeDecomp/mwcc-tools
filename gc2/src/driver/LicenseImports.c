// The import thunks of LMGR326B.dll's FLEXlm functions (2.6: 0x4284bc..0x4284ce, between
// CLCompilerLinkerDropin_V10.cpp and CLAccessPaths.c). The original links them from the DLL's
// import library, whose members are not C: Wii's later build names the module LMGR8C. Written as
// naked functions with -O4,s so they are packed 6 bytes apart. TODO: the unit's name is the
// repo's.
#include "driver/LicenseImports.h"

// (the import address table's entries the thunks jump through: the DLL's import library's)
extern void* _imp__lp_checkout;
extern void* _imp__lp_checkin;
extern void* _imp__lp_errstring;

__declspec(naked) int lp_checkout(
    LPCODE_HANDLE* code,
    int policy,
    const char* feature,
    const char* version,
    int nlic,
    const char* licensePath,
    LP_HANDLE** handle
) {
    asm {
        jmp [_imp__lp_checkout]
    }
}

__declspec(naked) void lp_checkin(LP_HANDLE* handle) {
    asm {
        jmp [_imp__lp_checkin]
    }
}

__declspec(naked) char* lp_errstring(LP_HANDLE* handle) {
    asm {
        jmp [_imp__lp_errstring]
    }
}
