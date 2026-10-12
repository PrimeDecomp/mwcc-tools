#include "compiler/common.h"
#include "driver/ParserHelpers-cc.h"
#include "driver/AssertionFailure.h"
#include "driver/ParserErrors.h"
#include "driver/ParserFace.h"
#include "driver/ParserGlue-eabi-ppc-cc.h"
#include "driver/ParserHelpers.h"
#include <stdio.h>
#if VERSION >= VERSION_GC_1_3
/* (1.2.5 leaves it to the linker's COMMON block) */
short data_00588530;
#endif
#define OPTION_ASSERT(cond, line) ((cond) ? (void)0 : CLIO_ReportAssertionFailure(#cond, "ParserHelpers-cc.c", line))
#define PR_UNSET 0
int fn_0040d283(int unused, char *first, char *second)
{
    ParserHelpers_AppendText(&directive_storage, first);
    if (second != NULL) {
        ParserHelpers_AppendText(&directive_storage, second);
    }
    return 1;
}

int append_define_directive(char *name, char *value)
{
    char buf[0x400];
#if VERSION >= VERSION_GC_1_3
    if (pTool->lang == 'c++ ' || pTool->lang == 'Asm ' || pTool->lang == 'rez ')
#else
    if (pTool->lang == 'c++ ' || pTool->lang == 'Asm ')
#endif
        sprintf(buf, "#define %s %s\n", name, value ? value : "1");
    else if (pTool->lang == 'pasc')
        sprintf(buf, "{$definec %s %s}\n", name, value ? value : "1");
    else {
        sprintf(buf, "Option '-D|d' is not supported with this plugin");
        fn_0040ecb1(0x1c, buf);
        return 0;
    }
    ParserHelpers_AppendText(&directive_storage, buf);
    return 1;
}

#if VERSION >= VERSION_GC_1_3
extern int fn_0040d107(char *a, char *b, int c);

int fn_0040d1c9(char *a, char *b)
{
    return fn_0040d107(a, b, strlen(b));
}
#endif

int append_undef_directive(char *option, int unused, char *symbol)
{
    char buf[300];
    if (pTool->lang == 'c++ ' || pTool->lang == 'Asm ')
        sprintf(buf, "#undef %s\n", symbol);
    else if (pTool->lang == 'pasc')
        sprintf(buf, "{$undefc %s}\n", symbol);
    else {
        sprintf(buf, "Option -%s is not supported with this plugin", option);
        fn_0040ecb1(0x1c, buf);
        return 0;
    }
    ParserHelpers_AppendText(&directive_storage, buf);
    return 1;
}

#if VERSION >= VERSION_GC_1_3
extern int fn_0040d1c9(char *a, char *b);
#endif
int append_include_directive(char *option, void *handle, char *filename)
{
    char buf[300];
    struct StorageHandle **storage;
    if (!handle)
        storage = &directive_storage;
    else
        storage = (struct StorageHandle **)handle;
    if (*filename) {
#if VERSION >= VERSION_GC_1_3
        if (pTool->lang == 'c++ ' || pTool->lang == 'Asm ') {
            if (*filename == '<' && filename[strlen(filename) - 1] == '>')
                snprintf(buf, sizeof(buf), "#include %s\n", filename);
            else
                snprintf(buf, sizeof(buf), "#include \"%s\"\n", filename);
        } else if (pTool->lang == 'pasc')
            snprintf(buf, sizeof(buf), "{$I+}\n{$I %s}\n{$I-}\n", filename);
#else
        if (pTool->lang == 'c++ ' || pTool->lang == 'Asm ')
            sprintf(buf, "#include \"%s\"\n", filename);
        else if (pTool->lang == 'pasc')
            sprintf(buf, "{$I+}\n{$I %s}\n{$I-}\n", filename);
#endif
        else {
            sprintf(buf, "Option -%s is not supported with this plugin", option);
            fn_0040ecb1(0x1c, buf);
            return 0;
        }
#if VERSION >= VERSION_GC_1_3
        fn_0040d1c9((char *)storage, buf);
#else
        ParserHelpers_AppendText(storage, buf);
#endif
    }
    return 1;
}

static inline int PragmaHasSetting(const Pragma *pragma, char setting)
{
    const char *value = pragma->value;
    return *value == setting;
}

int ParserHelpers_cc_EmitPragmas(Pragma *pragmas)
{
    char buf[300];
    for (; pragmas->pragma; pragmas++) {
        if (pragmas->flags == 0 || pragmas->flags == 1) {
            const char *value = NULL;
            Boolean reverse = pragmas->flags == 1;
            char on = !reverse ? (char)1 : (char)2;
            char off = !reverse ? (char)2 : (char)1;
            if (PragmaHasSetting(pragmas, on))
                value = "on";
            else if (PragmaHasSetting(pragmas, off))
                value = "off";
            else if (PragmaHasSetting(pragmas, 3))
                value = "auto";
            else if (PragmaHasSetting(pragmas, 4))
                value = "reset";
            else
                OPTION_ASSERT(*((char *)pragmas->value) == PR_UNSET, BY_VERSION(181, 186));
            if (value) {
#if VERSION >= VERSION_GC_1_3
                snprintf(buf, sizeof(buf), "#pragma %s %s\n", pragmas->pragma, value);
#else
                sprintf(buf, "#pragma %s %s\n", pragmas->pragma, value);
#endif
                ParserHelpers_AppendText(&directive_storage, buf);
            }
        } else {
            OPTION_ASSERT(!"Can't handle pragma", BY_VERSION(190, 195));
        }
    }
    return 1;
}
