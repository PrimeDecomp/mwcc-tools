#ifndef COMPILER_CMIDDLELAYER_H
#define COMPILER_CMIDDLELAYER_H

#include "compiler/common.h"

struct FSSpec;
struct CIRStreamFunctionCode;
struct CIRStreamFunctionHeader;

enum {
    IPAOff,
    IPAFile,
    IPAProgram
};

void CMid_Setup(void);
Boolean CMid_InIPA(void);
void CMid_ObjectAddrRef(Object* object);
void CMid_DefineFunction(Object* object, CI_FuncData* body, UInt8 oldStyleParameters);
Boolean CMid_LoadPrecompiledFunction(
    Object* object,
    struct CIRStreamFunctionCode* code,
    const struct CIRStreamFunctionHeader* header
);
void CMid_DefineData(
    Object* object, const void* data, OLinkList* references, SInt32 size, Boolean readOnly,
    Boolean deferSequentialString
);
void CMid_DefineThunkObject(
    Object* object, Object* target, SInt32 thisDelta, SInt32 returnDelta, SInt32 ctorOffset
);
void CMid_Finish(void);
void CMid_FinishSequentialStrings(void);
Boolean CMid_IsIRFile(const struct FSSpec* spec);
void CMid_CompileEnded(void);

#endif
