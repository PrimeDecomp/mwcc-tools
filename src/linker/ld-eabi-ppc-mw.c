#include "compiler/common.h"
#include "driver/cc-eabi-ppc-mw.h"
#include "driver/CLPlugins.h"
#include "driver/ClientGlue.h"

/* The 2.x linker's static plugin, "Embedded PPC Linker" (the compilers' cc-eabi-ppc-mw.c has the compiler's in its
 * place), and its messages, "Linker Errors". Each of the plugin's descriptions is a getter's data. */

/* (the linker's own entry, in the range the split gives libimp-eabi-ppc.c) */
extern int __stdcall fn_0041e670(CWPluginPrivateContext *context);

#pragma scheduling off

static PluginDesc data_00476780 = {2, 'Link', 8, 0, '????', 12};

int __stdcall get_name_and_length(char **name, int *length)
{
    *name = (char *)&data_00476780;
    *length = 18;
    return 0;
}

static UInt32 data_00476794 = 'ePPC';
static UInt32 data_00476798 = 'EABI';
static TargetInfo data_0047679c = {1, 1, &data_00476794, 1, &data_00476798};

void *__stdcall set_next_to_global(struct ListNodeLink *node)
{
    node->next = (struct ListNodeLink *)&data_0047679c;
    return NULL;
}

static const char *data_004767c0 = "Embedded PPC Linker";

unsigned int __stdcall copy_global_to_value(unsigned int *value)
{
    *value = (unsigned int)data_004767c0;
    return 0U;
}

static const char *data_004767c4 = "Embedded PPC Linker";

unsigned int __stdcall get_stored_value(unsigned int *value)
{
    *value = (unsigned int)data_004767c4;
    return 0;
}

#if LINKER >= 27
/* (2.7: no mapping of the other text files, which are passed to the linker) */
static FileMap data_004767c8[3] = {
    {0, ".doc", 0x10000000},
    {'TEXT', ".lcf", 0},
    {0, ".bin", 0x20000000},
};
static FileMapInfo data_00476868 = {1, 3, data_004767c8};
#else
static FileMap data_004767c8[4] = {
    {0, ".doc", 0x10000000},
    {'TEXT', ".lcf", 0},
    {'TEXT', "", 0x10000000},
    {0, ".bin", 0x20000000},
};
static FileMapInfo data_00476868 = {1, 4, data_004767c8};
#endif

int __stdcall fn_0040be10(signed char **arguments)
{
    *arguments = (signed char *)&data_00476868;
    return 0;
}

#if LINKER >= 27
/* (2.7: the compiler's panel too) */
static const char *data_004768d4[6] = {"PPC EABI CodeGen", "PPC EABI Linker", "PPC EABI Disassembler",
                                       "PPC EABI Project", "CmdLine Extras EPPC", "C/C++ Compiler"};
static PluginDirectoryList data_004768e8 = {1, 6, (char **)data_004768d4};
#else
static const char *data_004768d4[5] = {"PPC EABI CodeGen", "PPC EABI Linker", "PPC EABI Disassembler",
                                       "PPC EABI Project", "CmdLine Extras EPPC"};
static PluginDirectoryList data_004768e8 = {1, 5, (char **)data_004768d4};
#endif

unsigned int __stdcall fn_0040be20(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_004768e8;
    return 0U;
}

/* (the 2.x plugins' versions are four shorts: 2.4.1, build 32) */
static UInt16 data_004768f0[4] = {2, 4, 1, 32};

unsigned int __stdcall fn_0040be30(struct ListLink *link)
{
    link->next = (struct ListLink *)&data_004768f0;
    return 0;
}

static char data_004768f8[] = "!<arch>\n";
static char data_00476904[] = "\177ELF";
/* (the ELF signature's length is 0x104 in the original, where the library importer's is 4) */
static FileSignature data_0047690c[2] = {{'MPLF', data_004768f8, 8, 0}, {'ELF ', data_00476904, 0x104, 0}};
static FileSignatureList data_00476928 = {2, data_0047690c};

void *__stdcall set_listnode_next_to_global(struct ListNode *node)
{
    node->next = (struct ListNode *)&data_00476928;
    return NULL;
}

#pragma scheduling reset

static void *data_00476930[9] = {(void *)fn_0041e670,
                                 (void *)get_name_and_length,
                                 (void *)get_stored_value,
                                 (void *)copy_global_to_value,
                                 (void *)fn_0040be20,
                                 NULL,
                                 NULL,
                                 (void *)fn_0040be30,
                                 (void *)set_listnode_next_to_global};
static void *data_00476954[6] = {(void *)set_next_to_global, (void *)fn_0040be10, NULL, NULL, NULL, NULL};

unsigned int fn_0040bed0(void)
{
    return ClientGlue_CreateAndAddPlugin(data_00476930, data_00476954);
}

static char *data_00478d68[] = {
#include "driver/LinkerErrors.h"
};

unsigned int fn_0040bf10(void)
{
    return ClientGlue_AddResourceStrings("Linker Errors", 11001, data_00478d68);
}
