#ifndef DRIVER_OPTION_H
#define DRIVER_OPTION_H

#include "compiler/common.h"
#include "driver/Arguments.h"
#include "driver/option_system.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char curopt[1024];

extern Opt48* Args_Pop(SInt16 flags);
extern void Args_Push(SInt16 flags, void* first, void* second);
extern void Args_AddToToolArgs(PtrList* ta);
extern void Options_Init(void);
extern OptionList* Options_GetOptions(void);
extern void Options_SortOptions(void);
extern int Options_AddList(OptionList* optlst);
extern int Option_ForTool(Option* opt, int which);
extern int Option_ThisTool(void);
extern int Option_ForThisTool(Option* opt);
extern int Option_AlsoPassedFromThisTool(Option* opt);
extern int Options_Parse(OptionList* options, int flags);
extern void Option_ParamError(SInt16 id, va_list ap);
extern void Option_ParamWarning(SInt16 id, va_list ap);
extern void Option_Warning(SInt16 id, ...);
extern int Options_DisplayHelp(void);

#ifdef __cplusplus
}
#endif

#endif
