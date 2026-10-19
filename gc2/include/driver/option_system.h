#ifndef DRIVER_OPTION_SYSTEM_H
#define DRIVER_OPTION_SYSTEM_H

#include "compiler/common.h"
#include "driver/oslib.h"

#ifdef __cplusplus
extern "C" {
#endif

// The option system's types (Pro7's option_system.h): the parameter kinds, the options and their
// lists, packed to 1 byte (Pro7's `#pragma options align=packed`; 2.6's tables have the 10-byte
// PARAM_T header and unaligned fields).
#pragma pack(push, 1)
enum {
    OptParseError = 0,
    OptParseSuccess = 1
};

typedef struct PARAM_T {
    char which;
    char flags;
    char* myname;
    struct PARAM_T* next;
} PARAM_T;
typedef struct MASK_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 ormask;
    UInt32 andmask;
    void* num;
} MASK_T;
typedef struct STRING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    SInt16 maxlen;
    Boolean pstring;
    char* str;
} STRING_T;
typedef struct SET_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 value;
    char* num;
} SET_T;
typedef struct SETSTRING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char* value;
    char pstring;
    void* var;
} SETSTRING_T;
typedef struct GENERIC_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    int (*parse)(const char* opt, void* var, const char* pstr, int flags);
    void* var;
    char* help;
} GENERIC_T;
typedef struct SETTING_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    int (*parse)(const char* a, const char* b); // (Pro7: "TODO name these args")
    char* valuename;
} SETTING_T;
typedef struct TOGGLE_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    UInt32 mask;
    void* num;
} TOGGLE_T;
typedef struct NUM_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char size;
    char fit;
    UInt32 lo;
    UInt32 hi;
    void* num;
} NUM_T;
typedef struct FILEPATH_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    char fflags;
    char* defaultstr;
    void* filename;
    int maxlen;
} FILEPATH_T;
typedef struct IFARG_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    PARAM_T* parg;
    char* helpa;
    PARAM_T* pnone;
    char* helpn;
} IFARG_T;
typedef struct ONOFF_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    unsigned char* var;
} ONOFF_T;
typedef struct OFFON_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    unsigned char* var;
} OFFON_T;
typedef struct FTYPE_T {
    char which;
    char flags;
    char* myname;
    PARAM_T* next;
    OSType* fc;
    Boolean iscreator;
} FTYPE_T;
typedef struct OptionList {
    char* help;
    int flags;
    struct Option** list;
} OptionList;
typedef struct Option {
    char* names;
    int avail;
    PARAM_T* param;
    OptionList* sub;
    OptionList* conflicts;
    char* help;
} Option;

// TODO: the numbered names are Pro7's placeholders for flags whose meaning is not known
enum {
    HELPFLAGS_1 = 1,
    HELPFLAGS_IGNORED = 2,
    HELPFLAGS_OBSOLETE = 4,
    HELPFLAGS_DEPRECATED = 8,
    HELPFLAGS_SECRET = 0x10,
    HELPFLAGS_MEANINGLESS = 0x20,
    HELPFLAGS_COMPATIBLE = 0x40,
    HELPFLAGS_NORMAL = 0x80,
    HELPFLAGS_SPACES = 0x100,
    HELPFLAGS_TOOL = 0x200,
    HELPFLAGS_TOOL_THIS = 0x400,
    HELPFLAGS_TOOL_OTHER = 0x800,
    HELPFLAGS_TOOL_BOTH = 0xC00,
    HELPFLAGS_1000 = 0x1000,
    HELPFLAGS_2000 = 0x2000,
    HELPFLAGS_USAGE = 0x4000,
    HELPFLAGS_8000 = 0x8000
};
enum {
    OTF_GLOBAL = 1,
    OTF_STICKY = 2,
    OTF_CASED = 4,
    OTF_OBSOLETE = 8,
    OTF_SUBSTITUTED = 0x10,
    OTF_DEPRECATED = 0x20,
    OTF_TOOL_LINKER = 0x40,
    OTF_TOOL_DISASSEMBLER = 0x80,
    OTF_TOOL_COMPILER = 0x100,
    OTF_TOOL_MASK = OTF_TOOL_LINKER | OTF_TOOL_DISASSEMBLER | OTF_TOOL_COMPILER,
    OTF200 = 0x200,
    OTF400 = 0x400,
    OTF_IGNORED = 0x800,
    OTF_SECRET = 0x1000,
    OTF_HIDE_DEFAULT = 0x2000,
    OTF_COMPATIBILITY = 0x4000,
    OTF_HAS_SUB_OPTIONS = 0x8000,
    OTF_SUB_OPTIONS_OPTIONAL = 0x10000,
    OTF_ONLY_ONCE = 0x20000,
    OTF_HAS_CONFLICTS = 0x40000,
    OTF_WARNING = 0x80000,
    OTF_SLFLAGS_8 = 0x100000,
    OTF_SLFLAGS_10 = 0x200000,
    OTF_SLFLAGS_20 = 0x400000,
    OTF_SLFLAGS_MASK = OTF_SLFLAGS_8 | OTF_SLFLAGS_10 | OTF_SLFLAGS_20,
    OTF_MEANINGLESS = 0x800000,
    OTF_ALL_HIDDEN_BY_DEFAULT = OTF_OBSOLETE |
        OTF_DEPRECATED |
        OTF_IGNORED |
        OTF_SECRET |
        OTF_MEANINGLESS,
    OTF1000000 = 0x1000000,
    OTF2000000 = 0x2000000,
    OTF4000000 = 0x4000000,
    OTF8000000 = 0x8000000,
    OTF10000000 = 0x10000000,
    OTF20000000 = 0x20000000,
    OTF_INTERNAL_SEEN_CONFLICTED = 0x40000000,
    OTF_INTERNAL_ALREADY_SEEN = 0x80000000
};
enum {
    PARAMWHICH_None = 0,
    PARAMWHICH_FTypeCreator = 1,
    PARAMWHICH_FilePath = 2,
    PARAMWHICH_Number = 3,
    PARAMWHICH_String = 4,
    PARAMWHICH_Id = 5,
    PARAMWHICH_Sym = 6,
    PARAMWHICH_OnOff = 7,
    PARAMWHICH_OffOn = 8,
    PARAMWHICH_Mask = 9,
    PARAMWHICH_Toggle = 0xA,
    PARAMWHICH_Set = 0xB,
    PARAMWHICH_SetString = 0xC,
    PARAMWHICH_Generic = 0xD,
    PARAMWHICH_IfArg = 0xE,
    PARAMWHICH_Setting = 0xF,
    PARAMWHICH_MAX = 0x10
};
enum {
    PARAMFLAGS_1 = 1,
    PARAMFLAGS_2 = 2,
    PARAMFLAGS_3 = 3,
    PARAMFLAGS_4 = 4,
    PARAMFLAGS_8 = 8,
    PARAMFLAGS_10 = 0x10,
    PARAMFLAGS_12 = 0x12
};
enum {
    PARAMPARSEFLAGS_0 = 0,
    PARAMPARSEFLAGS_1 = 1,
    PARAMPARSEFLAGS_2 = 2,
    PARAMPARSEFLAGS_4 = 4,
    PARAMPARSEFLAGS_8 = 8,
    PARAMPARSEFLAGS_10 = 0x10,
    PARAMPARSEFLAGS_20 = 0x20,
    PARAMPARSEFLAGS_40 = 0x40,
    PARAMPARSEFLAGS_80 = 0x80,
    PARAMPARSEFLAGS_100 = 0x100
};
enum {
    SLFLAGS_1 = 1,
    SLFLAGS_2 = 2,
    SLFLAGS_4 = 4, // displays =...
    SLFLAGS_8 = 8, // displays [no]: e.g. [no]err[or] | [no]iserr[or], [no]implicit[conv]
    SLFLAGS_10 = 0x10, // displays [-]
    SLFLAGS_20 = 0x20, // displays [no-]
    SLFLAGS_40 = 0x40
};
enum {
    LISTFLAGS_NONE = 0,
    LISTFLAGS_EXCLUSIVE = 1,
    LISTFLAGS_ALLOW_UNKNOWNS = 2,
    LISTFLAGS_4 = 4,
    LISTFLAGS_COMPILER = 0x100,
    LISTFLAGS_LINKER = 0x200,
    LISTFLAGS_DISASSEMBLER = 0x400,
    LISTFLAGS_TOOL_MASK = LISTFLAGS_COMPILER | LISTFLAGS_LINKER | LISTFLAGS_DISASSEMBLER
};
enum {
    OFLAGS_1 = 1,
    OFLAGS_2 = 2,
    OFLAGS_4 = 4,
    OFLAGS_8 = 8,
    OFLAGS_10 = 0x10,
    OFLAGS_20 = 0x20,
    OFLAGS_40 = 0x40,
    OFLAGS_80 = 0x80
};
enum {
    PFLAGS_1 = 1,
    PFLAGS_2 = 2,
    PFLAGS_4 = 4,
    PFLAGS_8 = 8,
    PFLAGS_10 = 0x10,
    PFLAGS_20 = 0x20,
    PFLAGS_40 = 0x40,
    PFLAGS_80 = 0x80
};
#pragma pack(pop)

/* Arguments.c */
typedef struct ArgToken {
    SInt16 val;
    char* text;
} ArgToken;
enum {
    ATK_END,
    ATK_ARG_END,
    ATK_ARG,
    ATK_OPTION,
    ATK_EQUALS,
    ATK_COMMA
};

/* Option.c */
// An entry of the option stack (Args_Push): an option list, an option and its spelling, or a
// parameter. TODO: Opt48/Opt50/Opt52 are Pro7's placeholder names.
typedef struct Opt50 {
    void* first;
    void* second;
} Opt50;
typedef struct Opt52 {
    Option* opt;
    char* curopt;
} Opt52;
typedef struct Opt48 {
    union {
        Opt50 v;
        OptionList* lst;
        Opt52 o;
        char* param;
    } e;
    SInt16 flags;
} Opt48;

#ifdef __cplusplus
}
#endif

#endif
