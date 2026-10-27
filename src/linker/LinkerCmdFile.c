/* The linker command file: its parser and the evaluation of its expressions.

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here: the last
   function of this file is the first of the unit in the executable. */

typedef unsigned char Boolean;

#pragma options align = mac68k

/* An operator of an expression. */
typedef struct LCFOperator {
    int precedence;
    int depth;            /* the parenthesis depth of a '(' */
    unsigned char type;   /* 1 '(', 2 ')', 3 '=', else the operation EvalOperator does */
    Boolean unary;
} LCFOperator;

enum {
    EXPR_DELETED = 0,
    EXPR_OPERATOR = 1,
    EXPR_DOT = 2,
    EXPR_NUMBER = 3,
    EXPR_SYMBOL = 4,
    EXPR_ROMADDR = 5,
    EXPR_ADDR = 6,
    EXPR_ORIGIN = 7,
    EXPR_NEXT = 8,
    EXPR_SIZEOF = 9,
    EXPR_LENGTH = 10
};

/* A term of an expression: the expressions are lists of terms in their written order, reduced in place. */
typedef struct LCFExpr {
    struct LCFExpr *next;
    union {
        unsigned long value;
        LCFOperator *op;
        struct LCFExpr *expr;
        char *name;
        struct LCFSection *section;
        struct LCFMemory *memory;
        void *ptr;
    } u;
    unsigned char kind;
} LCFExpr;

#pragma options align = reset

/* What the scanner gives for an operator where the parser only needs its type. */
typedef struct LCFToken {
    void *next;
    unsigned long value;
    unsigned char kind;
} LCFToken;

/* A region of MEMORY. */
typedef struct LCFMemory {
    struct LCFMemory *next;
    char *name;
    unsigned long origin;
    long length;
    unsigned long end;
    char x14[4];
} LCFMemory;

#pragma pack(push, 2)

/* An item of an output section's contents: an input section pattern or an assignment. */
typedef struct LCFItem {
    struct LCFItem *next;
    void *x4;
    char *file;
    char *member;
    char *section;
    LCFExpr *expr;
    unsigned char kind;
    long type;
    unsigned char bss;
} LCFItem;

#pragma pack(pop)

/* An entry of SECTIONS: an output section, a GROUP or an assignment. */
typedef struct LCFSection {
    struct LCFSection *next;
    struct LCFSection *parent;
    char *name;
    unsigned long align;
    char x10[0x18 - 0x10];
    LCFExpr *expr;        /* BIND's, or an assignment's */
    char x1c[0x20 - 0x1c];
    LCFItem *items;
    LCFMemory *memory;
    unsigned long at;
    char x2c[0x60 - 0x2c];
    unsigned long address;
    char x64[0x6c - 0x64];
    unsigned long size;
    char x70[0x80 - 0x70];
    long type;
    char x84[0xa4 - 0x84];
    unsigned long romAddress;
    char xa8[0xc8 - 0xa8];
    unsigned char kind;
    char xc9;
    Boolean bss;
    Boolean excluded;
    Boolean xcc;
} LCFSection;

typedef struct LCFName {
    struct LCFName *next;
    char *name;
} LCFName;

#pragma pack(push, 2)

typedef struct LCFFileName {
    struct LCFFileName *next;
    char *name;
    char *member;
    char x0c[0x16 - 0xc];
} LCFFileName;

/* The command file. */
typedef struct LCFFile {
    LCFSection *assignments;
    LCFSection *sections;
    LCFMemory *memory;
    LCFName *forceActive;
    LCFFileName *forceFiles;
    LCFFileName *excludeFiles;
    LCFFileName *includeDwarf;
    Boolean exclude;
} LCFFile;

#pragma pack(pop)

typedef struct LCFSectionRef {
    struct LCFSectionRef *next;
    LCFSection *section;
} LCFSectionRef;

typedef struct LCFSymbol {
    char x0[8];
    long section;
    char xc[0x10 - 0xc];
    unsigned long value;
} LCFSymbol;

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern unsigned char CompilerTools_ReportDiagnostic(long diagnosticCode, ...);
extern void *galloc(long size);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);
extern int strcmp(const char *a, const char *b);
extern int strncmp(const char *a, const char *b, unsigned long n);
extern unsigned long strlen(const char *s);
extern int sscanf(const char *s, const char *format, ...);
extern unsigned char __lower_map[];

extern void __stdcall Memory_FreeHandle(char **handle);
extern void __stdcall fn_00413a00(char **handle);
extern void *fn_0041e410(int index);
extern long fn_00420260(void);
extern Boolean fn_00420350(void);
extern void CompilerTools_Abort(void);
extern void CompilerTools_ReportLimitedDiagnostic(int message, ...);
extern LCFSection *ELF_PPC_EABI_NewOutput(char *name, int type, int kind);
extern unsigned long ELF_PPC_EABI_LinkSymbolValue(long section);
extern LCFSymbol *LK_symbol_Find(char *name, long index);
#ifdef NONMATCHING /* (LinkSym.c's returns the symbol: a call through another type traps in wasm) */
extern void *LinkSym_Add(char *name, LCFExpr *expr, LCFSection *section, int flag);
#else
extern void LinkSym_Add(char *name, LCFExpr *expr, LCFSection *section, int flag);
#endif

extern LCFFile *data_004a28e4;
extern LCFSectionRef *data_004a2de6;
extern void *data_004a2e16;
extern void *data_004a2e1a;
extern void *data_004a2e1e;
extern Boolean data_004a2e45;
extern Boolean data_004a2e49;
extern Boolean data_004a2e53;

static char data_0049a9d8[256];
static long data_0049aad8;
static char **data_0049aadc;
static char *data_0049aae0;
static char *data_0049aae4;
static int data_0049aae8;

#define LCF_ASSERT(cond, line)                                                                                         \
    if (!(cond))                                                                                                       \
    CompilerTools_FormatMessageAndLongjmp(0x33, 2, "LinkerCmdFile.c", line)

#define IS_SPACE(c) ((c) == '\t' || (c) == ' ' || (c) == '\xca' || (c) == '\r' || (c) == '\n')
#define NOT_SPACE(c) ((c) != '\t' && (c) != ' ' && (c) != '\xca' && (c) != '\r' && (c) != '\n')

__inline int tolower(int c) { return c == -1 ? -1 : __lower_map[(unsigned char)c]; }

char *SkipSpace(char *p);
char *SkipWord(char *p);
char *GetWord(char *p);
char *GetToken(char *p, Boolean inList, char **out);
Boolean ScanOperator(char *p, LCFOperator *op, int *length);
char *ParseNumber(char *p, int *error, Boolean *isNumber, unsigned long *value);
char *ParseAlignValue(char *p, int *error, unsigned long *value);
Boolean IsSpecialSection(char *name, void *section);
Boolean CheckSectionName(LCFSection *section, LCFItem *input);
char *ParseSectionRef(char *p, int *error, LCFExpr *node);
char *OperatorName(unsigned char type);
int CheckOperators(LCFExpr *prev, LCFExpr *cur);
char *ParseExpression(char *p, int *error, LCFExpr **head, Boolean semicolon, Boolean *dot);
char *ParseSectionType(char *p, int *error, LCFSection *section);
char *ParseBindAlign(char *p, int *error, LCFSection *section);
char *ParseSectionContents(char *p, int *error, LCFSection *section);
char *ParseSectionAttributes(char *p, int *error, LCFSection *section);
char *ParseSections(char *p, int *error, LCFSection **list, LCFSection *parent);

static __inline Boolean IsSectionName(char *name) {
    LCFSection *entry;

    for (entry = data_004a28e4->sections; entry; entry = entry->next) {
        if (entry->name && name && !strcmp(entry->name, name))
            return 1;
    }
    return 0;
}

static __inline Boolean IsBadAbsName(char *name) {
    int length;
    int c;

    if (name == 0 || strncmp(name, ".abs.", 5) != 0)
        return 0;
    length = strlen(name + 5);
    if (length == 0 || length > 8)
        return 1;
    for (name += 5; *name;) {
        char ch = *name++;
        c = tolower(ch);
        switch (c) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            break;
        default:
            return 1;
        }
    }
    return 0;
}

static inline Boolean MemoryExists(char *name) {
    LCFMemory *memory;

    for (memory = data_004a28e4->memory; memory; memory = memory->next) {
        if (memory->name && name && !strcmp(memory->name, name))
            return 1;
    }
    return 0;
}

static inline LCFSectionRef *NewDebugRef(void) {
    LCFSectionRef *ref;

    ref = galloc(sizeof(LCFSectionRef));
    memset(ref, 0, sizeof(LCFSectionRef));
    ref->next = data_004a2de6;
    return data_004a2de6 = ref;
}

/* 0x0044a100 */
char *SkipSpace(char *p) {
    Boolean lineComment = 0;
    Boolean blockComment = 0;

    for (;;) {
        switch (*(unsigned char *)p) {
        case '\r':
            data_0049aad8++;
            lineComment = 0;
            p++;
            if (*p == '\n')
                p++;
            continue;
        case '\n':
            lineComment = 0;
            data_0049aad8++;
        case '\t':
        case ' ':
        case 0xca:
            p++;
            continue;
        case 0:
            if (blockComment) {
                CompilerTools_ReportLimitedDiagnostic(0x58, data_0049aad8);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            }
            return p;
        case '*':
            if (blockComment && p[1] == '/') {
                blockComment = 0;
                p += 2;
                continue;
            }
            if (blockComment && p[1] == 0) {
                CompilerTools_ReportLimitedDiagnostic(0x58, data_0049aad8);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            } else if (blockComment || lineComment) {
                p++;
                continue;
            }
            return p;
        case '/':
            if (p[1] == '/') {
                lineComment = 1;
                p += 2;
                continue;
            }
            if (p[1] == '*' && !lineComment) {
                blockComment = 1;
                p += 2;
                continue;
            }
        default:
            if (lineComment || blockComment) {
                p++;
                continue;
            }
            return p;
        }
    }
}

/* 0x00449e70 */
Boolean ScanOperator(char *p, LCFOperator *op, int *length) {
    unsigned char type = 0;
    int len = 0;
    int precedence = 0;
    Boolean unary = 0;

    *length = 0;
    switch (*p) {
    case '=':
        if (p[1] == '=') {
            type = 0x13; len = 2; precedence = 5;
        } else {
            type = 3; len = 1; precedence = 11;
        }
        break;
    case '+':
        if (p[1] == '=') {
            type = 4; len = 2; precedence = 11;
        } else if (p[1] == '+') {
            type = 0x1d; len = 2; precedence = 11;
        } else {
            type = 0xf; len = 1; precedence = 3;
        }
        break;
    case '-':
        if (p[1] == '=') {
            type = 5; len = 2; precedence = 11;
        } else if (p[1] == '-') {
            type = 0x1e; len = 2; precedence = 11;
        } else {
            type = 0x10; len = 1; precedence = 5;
        }
        break;
    case '*':
        if (p[1] == '=') {
            type = 6; len = 2; precedence = 11;
        } else {
            type = 0xc; len = 1; precedence = 2;
        }
        break;
    case '/':
        if (p[1] == '=') {
            type = 7; len = 2; precedence = 11;
        } else {
            type = 0xd; len = 1; precedence = 2;
        }
        break;
    case '!':
        if (p[1] == '=') {
            type = 0x14; len = 2; precedence = 5;
        } else {
            type = 8; precedence = len = 1; unary = 1;
        }
        break;
    case '<':
        if (p[1] == '=') {
            type = 0x17; len = 2; precedence = 5;
        } else if (p[1] == '<') {
            type = 0x12; len = 2; precedence = 4;
        } else {
            type = 0x16; len = 1; precedence = 5;
        }
        break;
    case '>':
        if (p[1] == '=') {
            type = 0x18; len = 2; precedence = 5;
        } else if (p[1] == '>') {
            type = 0x11; len = 2; precedence = 4;
        } else {
            type = 0x15; len = 1; precedence = 5;
        }
        break;
    case '&':
        if (p[1] == '&') {
            type = 0x1b; len = 2; precedence = 8;
        } else {
            type = 0x19; len = 1; precedence = 6;
        }
        break;
    case '|':
        if (p[1] == '|') {
            type = 0x1c; len = 2; precedence = 9;
        } else {
            type = 0x1a; len = 1; precedence = 7;
        }
        break;
    case '%':
        type = 0xe; len = 1; precedence = 2;
        break;
    case '~':
        type = 9; precedence = len = 1; unary = 1;
        break;
    case '(':
        type = 1; len = 1; precedence = 0;
        break;
    case ')':
        type = 2; len = 1; precedence = 0;
        break;
    case ',':
        type = 0x1f; len = 1; precedence = 0;
        break;
    }
    if (len) {
        *length = len;
        op->precedence = precedence;
        op->type = type;
        op->unary = unary;
        return 1;
    }
    return 0;
}

/* 0x00449da0 */
char *GetToken(char *p, Boolean inList, char **out) {
    Boolean quoted = 0;
    char *start;
    int length;
    int opLength;
    LCFOperator op;
    char *buffer;

    *out = 0;
    if (*p == '"') {
        quoted = 1;
        p++;
    }
    start = p;
    length = 0;
    for (;;) {
#ifdef NONMATCHING
        /* (the original reads on past the command file's end, through the heap, to a space: wasm stops there) */
        if (*p == 0)
            break;
#endif
        if (quoted) {
            if (*p == '"') {
                p++;
                break;
            }
        } else if (inList) {
            if (IS_SPACE(*p) || *p == '}') {
                if (start[length - 1] == ',')
                    length--;
                break;
            }
        } else {
            if (IS_SPACE(*p))
                break;
            if (ScanOperator(p, &op, &opLength) || *p == ';' || *p == ':' || *p == '}')
                break;
        }
        p++;
        length++;
    }
    buffer = galloc(length + 1);
    memcpy(buffer, start, length + 1);
    buffer[length] = 0;
    *out = buffer;
    return p;
}

/* 0x00449d20 */
char *GetWord(char *p) {
    int i;
    int length;
    LCFOperator op;

    for (i = 0; i < 256; i++) {
        if ((unsigned char)*p < ' ')
            break;
        if (!(NOT_SPACE(*p) && !ScanOperator(p, &op, &length) && *p != ';' && *p != ':' && *p != '{' && *p != '}'))
            break;
        data_0049a9d8[i] = *p++;
    }
    data_0049a9d8[i] = 0;
    return data_0049a9d8;
}

/* 0x00449cc0 */
char *SkipWord(char *p) {
    int length;
    LCFOperator op;

    for (;;) {
        if ((unsigned char)*p < ' ')
            return p;
        if (!(NOT_SPACE(*p) && !ScanOperator(p, &op, &length) && *p != ';' && *p != ':' && *p != '{' && *p != '}'))
            return p;
        p++;
    }
}

/* 0x00449bd0 */
char *ParseAlignValue(char *p, int *error, unsigned long *value) {
    Boolean digits = 0;
    Boolean done;

    *error = 0;
    if (*p == '(') {
        p = SkipSpace(p + 1);
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                p += 2;
                sscanf(p, "%x", value);
            } else {
                p++;
                sscanf(p, "%o", value);
            }
        } else {
            sscanf(p, "%u", value);
        }
        done = 0;
        do {
            switch (*p) {
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                digits = 1;
                break;
            case ')':
                done = 1;
                if (!digits)
                    *error = 0x4f;
                break;
            default:
                *error = 0x4f;
                return p;
            }
            p++;
        } while (!done);
        return p;
    }
    *error = 0x4d;
    data_0049aae0 = "(";
    return p;
}

/* 0x00449ae0 */
char *ParseNumber(char *p, int *error, Boolean *isNumber, unsigned long *value) {
    Boolean done;
    LCFOperator op;
    int length;

    *error = 0;
    *isNumber = 0;
    switch (*p) {
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                p += 2;
                sscanf(p, "%x", value);
            } else {
                p++;
                sscanf(p, "%o", value);
            }
        } else {
            sscanf(p, "%u", value);
        }
        *isNumber = 1;
        done = 0;
        do {
            if (ScanOperator(p, &op, &length))
                break;
            switch (*p) {
            case '\t': case '\n': case '\r': case ' ': case ',': case ';': case 0xca:
                done = 1;
                break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
                goto next;
            default:
                *error = 0x4f;
                done = *error;
                break;
            }
            if (done)
                break;
        next:
            p++;
        } while (!done);
    }
    return p;
}

/* 0x004498c0 */
Boolean IsSpecialSection(char *name, void *section) {
    if (name && !strcmp(name, "sinit"))
        return 1;
    if (name && !strncmp(name, ".rela", 5))
        return 1;
    if (name && !strncmp(name, ".rel", 4))
        return 1;
    if (name && !strcmp(name, ".strtab"))
        return 1;
    if (name && !strcmp(name, ".shstrtab"))
        return 1;
    if (name && !strcmp(name, ".symtab"))
        return 1;
    if (name && !strncmp(name, ".debug", 6))
        return 1;
    if (name && !strcmp(name, ".line"))
        return 1;
    if (name && !strcmp(name, ".comment"))
        return 1;
    if (name && !strcmp(name, "extabindex") && section) {
        if (data_004a2e16)
            return 1;
        data_004a2e16 = section;
    }
    if (name && !strcmp(name, ".ctors") && section) {
        if (data_004a2e1a)
            return 1;
        data_004a2e1a = section;
    }
    if (name && !strcmp(name, ".dtors") && section) {
        if (data_004a2e1e)
            return 1;
        data_004a2e1e = section;
    }
    return 0;
}

/* 0x00449580 */
Boolean CheckSectionName(LCFSection *section, LCFItem *input) {
    if (input->section && !strcmp(input->section, "extabindex")) {
        if (input->file)
            return 1;
        if (!section->name || strcmp(section->name, "extabindex")) {
            if (data_004a2e16)
                return 1;
            data_004a2e16 = section;
        }
        return 0;
    }
    if (input->section && !strcmp(input->section, ".ctors")) {
        if (input->file)
            return 1;
        if (!section->name || strcmp(section->name, ".ctors")) {
            if (data_004a2e1a)
                return 1;
            data_004a2e1a = section;
        }
        return 0;
    }
    if (input->section && !strcmp(input->section, ".dtors")) {
        if (input->file)
            return 1;
        if (!section->name || strcmp(section->name, ".dtors")) {
            if (data_004a2e1e)
                return 1;
            data_004a2e1e = section;
        }
        return 0;
    }
    if (input->section && !strcmp(input->section, ".sdata")) {
        if (!section->name || strcmp(section->name, ".sdata"))
            return 1;
        return 0;
    }
    if (input->section && !strcmp(input->section, ".sbss")) {
        if (!section->name || strcmp(section->name, ".sbss"))
            return 1;
        return 0;
    }
    if (input->section && !strcmp(input->section, ".sdata2")) {
        if (!section->name || strcmp(section->name, ".sdata2"))
            return 1;
        return 0;
    }
    if (input->section && !strcmp(input->section, ".sbss2")) {
        if (!section->name || strcmp(section->name, ".sbss2"))
            return 1;
        return 0;
    }
    if (input->section && !strcmp(input->section, ".PPC.EMB.sdata0")) {
        if (!section->name || strcmp(section->name, ".PPC.EMB.sdata0"))
            return 1;
        return 0;
    }
    if (input->section && !strcmp(input->section, ".PPC.EMB.sbss0")) {
        if (!section->name || strcmp(section->name, ".PPC.EMB.sbss0"))
            return 1;
        return 0;
    }
    return 0;
}

/* 0x00449450 */
char *ParseSectionRef(char *p, int *error, LCFExpr *node) {
    char *name;
    LCFSection *section;
    LCFMemory *memory;

    *error = 0;
    if (*p == '(') {
        p = SkipSpace(p + 1);
        p = GetToken(p, 0, &name);
        if (*p != ')') {
            *error = 0x4d;
            data_0049aae0 = ")";
            return p;
        }
        p++;
        if (!name || !*name) {
            *error = 0x51;
            return p;
        }
        for (section = data_004a28e4->sections; section; section = section->next) {
            if (section->name && name && !strcmp(section->name, name)) {
                node->u.ptr = section;
                return p;
            }
        }
        for (memory = data_004a28e4->memory; memory; memory = memory->next) {
            if (memory->name && name && !strcmp(memory->name, name)) {
                node->kind++;
                node->u.ptr = memory;
                return p;
            }
        }
        *error = 0x4c;
    } else {
        *error = 0x4d;
        data_0049aae0 = "(";
    }
    return p;
}

/* 0x00449380 */
char *OperatorName(unsigned char type) {
    switch (type) {
    case 19: return "==";
    case 3: return "=";
    case 4: return "+=";
    case 11: case 15: return "+";
    case 29: return "++";
    case 5: return "-=";
    case 10: case 16: return "-";
    case 30: return "--";
    case 6: return "*=";
    case 12: return "*";
    case 7: return "/=";
    case 13: return "/";
    case 20: return "!=";
    case 8: return "!";
    case 23: return "<=";
    case 18: return "<<";
    case 22: return "<";
    case 24: return ">=";
    case 17: return ">>";
    case 21: return ">";
    case 27: return "&&";
    case 25: return "&";
    case 28: return "||";
    case 26: return "|";
    case 14: return "%";
    case 9: return "~";
    case 1: return "(";
    case 2: return ")";
    }
    return "<UNKNOWN OPERATOR> ";
}

/* 0x00449290 */
int CheckOperators(LCFExpr *prev, LCFExpr *cur) {
    if (prev->kind == 1) {
        switch (prev->u.op->type) {
        case 4: case 5: case 6: case 7: case 29: case 30: case 31:
            data_0049aae4 = OperatorName(((LCFOperator *)prev->u.ptr)->type);
            return 0x49;
        }
        if (cur->kind == 1) {
            if (cur->u.op->type == 0x10) {
                cur->u.op->type = 10;
                cur->u.op->precedence = 1;
                cur->u.op->unary = 1;
            } else if (cur->u.op->type == 0xf) {
                cur->u.op->type = 11;
                cur->u.op->precedence = 1;
                cur->u.op->unary = 1;
            } else if (!cur->u.op->unary) {
                data_0049aae4 = OperatorName(((LCFOperator *)cur->u.ptr)->type);
                return 0x57;
            }
        }
    } else {
        if (cur->kind != 1) {
            data_0049aae4 = ";";
            return 0x56;
        }
        if (cur->u.op->unary) {
            data_0049aae4 = OperatorName(cur->u.op->type);
            return 0x57;
        }
    }
    return 0;
}

/* 0x00448d50 */
char *ParseExpression(char *p, int *error, LCFExpr **head, Boolean semicolon, Boolean *dot) {
    LCFExpr *prev;
    Boolean afterOpen;
    LCFExpr *tail;
    int depth;
    LCFExpr *node;
    LCFOperator *opnode;
    Boolean found;
    int length;
    LCFOperator op;
    Boolean saved;
    char *token;

    prev = 0;
    afterOpen = 1;
    depth = 0;
    *error = 0;
    *dot = 0;
    for (;;) {
        node = galloc(sizeof(LCFExpr));
        memset(node, 0, sizeof(LCFExpr));
        if (!*head) {
            *head = tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
        token = GetWord(p);
        if (!strcmp(token, "ADDR")) {
            afterOpen = 0;
            p = SkipWord(p);
            p = SkipSpace(p);
            node->kind = 6;
            p = ParseSectionRef(p, error, node);
            data_0049aae4 = "ADDR";
            if (!*error) {
                if (prev)
                    *error = CheckOperators(prev, node);
                prev = node;
            }
        } else if (!strcmp(token, "ROMADDR")) {
            afterOpen = 0;
            p = SkipSpace(SkipWord(p));
            node->kind = 5;
            p = ParseSectionRef(p, error, node);
            data_0049aae4 = "ROMADDR";
            if (!*error) {
                if (prev)
                    *error = CheckOperators(prev, node);
                prev = node;
            }
        } else if (!strcmp(token, "SIZEOF")) {
            afterOpen = 0;
            p = SkipSpace(SkipWord(p));
            node->kind = 9;
            p = ParseSectionRef(p, error, node);
            data_0049aae4 = "SIZEOF";
            if (!*error) {
                if (prev)
                    *error = CheckOperators(prev, node);
                prev = node;
            }
#if LINKER >= 27
        /* (2.7: ALIGN(n) too, the location counter aligned like NEXT's) */
        } else if (!strcmp(token, "NEXT") || !strcmp(token, "ALIGN")) {
#else
        } else if (!strcmp(token, "NEXT")) {
#endif
            afterOpen = 0;
            p = SkipSpace(SkipWord(p));
            node->kind = 8;
            if (*p == '(') {
                saved = *dot;
                p = ParseExpression(p, error, &node->u.expr, 0, dot);
                if (saved)
                    *dot = saved;
            } else {
                *error = 0x4d;
                data_0049aae4 = "NEXT";
                data_0049aae0 = "(";
                return p;
            }
            if (!*error) {
                if (prev)
                    *error = CheckOperators(prev, node);
                prev = node;
            }
        } else if (ScanOperator(p, &op, &length)) {
            node->kind = 1;
            p += length;
            opnode = galloc(sizeof(LCFOperator));
            memset(opnode, 0, sizeof(LCFOperator));
            *opnode = op;
            node->u.op = opnode;
            if (opnode->type == 1) {
                opnode->depth = depth++;
                afterOpen = 1;
            } else if (opnode->type == 2) {
                opnode->depth = --depth;
                if (afterOpen || depth < 0 || (prev && prev->kind == 1)) {
                    *error = 0x57;
                    data_0049aae4 = ")";
                    return p;
                }
                if (!depth && !semicolon)
                    break;
            } else {
                afterOpen = 0;
                if (prev) {
                    *error = CheckOperators(prev, node);
                } else if (opnode->type == 0x10) {
                    opnode->type = 10;
                    opnode->precedence = 1;
                    opnode->unary = 1;
                } else if (opnode->type == 0xf) {
                    opnode->type = 11;
                    opnode->precedence = 1;
                    opnode->unary = 1;
                } else if (!opnode->unary) {
                    *error = 0x57;
                    data_0049aae4 = OperatorName(opnode->type);
                }
                prev = node;
            }
        } else {
            afterOpen = 0;
            p = ParseNumber(p, error, &found, &node->u.value);
            node->kind = 3;
            if (!found && !*error) {
                node->kind = 4;
                p = GetToken(p, 0, &node->u.name);
                if (!strcmp(node->u.name, ".")) {
                    node->kind = 2;
                    *dot = 1;
                }
            }
            if (!*error) {
                if (prev)
                    *error = CheckOperators(prev, node);
                prev = node;
            }
        }
        if (*error)
            return p;
        p = SkipSpace(p);
        if (*p == ',') {
            *error = 0x57;
            data_0049aae4 = ",";
            return p;
        }
        if (*p == ';') {
            if (semicolon && !depth) {
                p++;
                break;
            }
            *error = 0x57;
            data_0049aae4 = ";";
            return p;
        }
    }
    return p;
}

/* 0x00448aa0 */
char *ParseSectionType(char *p, int *error, LCFSection *spec) {
    char *token;

    *error = 0;
    if (*p == '(') {
        p = SkipSpace(p + 1);
        token = GetWord(p);
        if (!strcmp(token, "TEXT")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 6;
            spec->bss = 0;
        } else if (!strcmp(token, "CODE")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 6;
            spec->bss = 0;
        } else if (!strcmp(token, "ZTEXT")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 0x40000006;
            spec->bss = 0;
        } else if (!strcmp(token, "ZCODE")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 0x40000006;
            spec->bss = 0;
        } else if (!strcmp(token, "MIXED")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 7;
            spec->bss = 0;
        } else if (!strcmp(token, "CONST")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 2;
            spec->bss = 0;
        } else if (!strcmp(token, "DATA")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 3;
            spec->bss = 0;
        } else if (!strcmp(token, "BSS")) {
            p = SkipSpace(SkipWord(p));
            spec->type = 3;
            spec->bss = 1;
        } else {
            *error = 0x54;
            return p;
        }
        if (*p != ')') {
            *error = 0x4d;
            data_0049aae0 = ")";
            return p;
        }
    } else {
        *error = 0x56;
        data_0049aae4 = "(";
        return p;
    }
    return p + 1;
}

/* 0x00448930 */
char *ParseBindAlign(char *p, int *error, LCFSection *spec) {
    Boolean more;
    Boolean dot = 0;
    char *token;

    *error = 0;
    do {
        more = 0;
        p = SkipSpace(p);
        token = GetWord(p);
        if (!strcmp(token, "BIND")) {
            more = 1;
            p = SkipWord(p);
            p = SkipSpace(p);
            if (*p == '(') {
                p = ParseExpression(p, error, &spec->expr, 0, &dot);
                if (*error)
                    return p;
            } else {
                *error = 0x4d;
                data_0049aae4 = "BIND";
                data_0049aae0 = "(";
                return p;
            }
            p = SkipSpace(p);
        } else if (!strcmp(token, "ALIGN")) {
            more = 1;
            p = SkipSpace(SkipWord(p));
            p = ParseAlignValue(p, error, &spec->align);
            data_0049aae4 = "ALIGN";
            if (*error)
                return p;
            p = SkipSpace(p);
        } else if (*p == ':') {
            p = SkipSpace(p + 1);
        } else {
            *error = 0x56;
            data_0049aae4 = ":";
        }
    } while (more);
    return p;
}

/* 0x00448270 */
char *ParseSectionContents(char *p, int *error, LCFSection *section) {
    LCFItem *tail;
    LCFItem *item;
    long x80 = section->type;
    Boolean first = 1;
    char *name;
    LCFOperator op;
    int length;
    char *q;
    Boolean flag = 0;

    *error = 0;
    p = SkipSpace(p);
    if (*p == '{') {
    p = SkipSpace(p + 1);
    while (*p != '}' || first) {
        first = 0;
        item = galloc(sizeof(LCFItem));
        memset(item, 0, sizeof(LCFItem));
        if (!section->items) {
            section->items = tail = item;
        } else {
            tail->next = item;
            tail = item;
        }
        item->type = x80;
        item->bss = section->bss;
        if (*p == '*') {
            item->kind = 1;
            p = SkipSpace(p + 1);
            if (*p == '(') {
            p = SkipSpace(p + 1);
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            item->section = name;
            if (IsSpecialSection(name, 0)) {
                *error = 0x75;
                data_0049aae4 = name;
                return p;
            }
            if (CheckSectionName(section, item)) {
#if LINKER >= 27
                /* (2.7: naming the output section) */
                *error = 0xa4;
                data_0049aae0 = section->name;
#else
                *error = 0x75;
#endif
                data_0049aae4 = name;
                return p;
            }
            if (IsBadAbsName(name)) {
                *error = 0x77;
                data_0049aae4 = name;
                return p;
            }
            p = SkipSpace(p);
            if (*p == ')') {
                p++;
            } else {
                *error = 0x4d;
                data_0049aae4 = "*";
                data_0049aae0 = ")";
                return p;
            }
            } else {
                *error = 0x4d;
                data_0049aae4 = "*";
                data_0049aae0 = "(";
                return p;
            }
        } else if (*p == '.') {
            name = ".";
            q = SkipSpace(p + 1);
            if (ScanOperator(q, &op, &length)) {
                if (op.type == 3) {
                    item->kind = 6;
                    item->section = name;
                    p = SkipSpace(q + length);
                    p = ParseExpression(p, error, &item->expr, 1, &flag);
                } else {
                    *error = 0x56;
                    data_0049aae4 = "=";
                    return q;
                }
            } else {
                *error = 0x56;
                data_0049aae4 = "=";
                return q;
            }
        } else if (*p == ',') {
            p = SkipSpace(p + 1);
        } else if (*p == '}') {
            item->kind = 1;
            item->section = section->name;
        } else {
            item->kind = 1;
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            item->file = name;
            p = SkipSpace(p);
            if (ScanOperator(p, &op, &length)) {
                if (op.type == 3) {
                    item->kind = 6;
                    item->section = name;
                    p = SkipSpace(p + length);
                    flag = 0;
                    p = ParseExpression(p, error, &item->expr, 1, &flag);
                    if (strcmp(name, "."))
                        LinkSym_Add(name, item->expr, section, 1);
                } else if (op.type == 1) {
                    p = SkipSpace(p + 1);
                    p = GetToken(p, 0, &name);
                    if (!name || !*name) {
                        *error = 0x51;
                        return p;
                    }
                    item->section = name;
                    p = SkipSpace(p);
                    if (*p != ')') {
                        *error = 0x56;
                        data_0049aae4 = ")";
                        return p;
                    }
                    p = SkipSpace(p + 1);
                    if (*p == '(') {
                        item->member = name;
                        p = SkipSpace(p + 1);
                        p = GetToken(p, 0, &name);
                        if (!name || !*name) {
                            *error = 0x51;
                            return p;
                        }
                        item->section = name;
                        p = SkipSpace(p);
                        if (*p == ')') {
                            p++;
                        } else {
                            *error = 0x56;
                            data_0049aae4 = ")";
                            return p;
                        }
                    }
                    if (IsSpecialSection(name, 0)) {
                        *error = 0x75;
                        data_0049aae4 = name;
                        return p;
                    }
                    if (CheckSectionName(section, item)) {
#if LINKER >= 27
                        /* (2.7: naming the output section) */
                        *error = 0xa4;
                        data_0049aae0 = section->name;
#else
                        *error = 0x75;
#endif
                        data_0049aae4 = name;
                        return p;
                    }
                    if (IsBadAbsName(name)) {
                        *error = 0x77;
                        data_0049aae4 = name;
                        return p;
                    }
                } else {
                    *error = 0x4a;
                    data_0049aae4 = "=";
                    data_0049aae0 = "(";
                    return p;
                }
            }
        }
        if (*error)
            return p;
        p = SkipSpace(p);
    }
    if (*p != '}') {
        *error = 0x56;
        data_0049aae0 = "}";
        return p;
    }
    return p + 1;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
        return p;
    }
}

/* 0x004480a0 */
char *ParseSectionAttributes(char *p, int *error, LCFSection *section) {
    LCFOperator op;
    int length;
    Boolean ok;
    char *name;
    LCFMemory *memory;

    *error = 0;
    if (*p == '(')
        p = SkipSpace(ParseSectionType(p, error, section));
    p = ParseBindAlign(p, error, section);
    if (*error)
        return p;
    p = SkipSpace(p);
    p = ParseSectionContents(p, error, section);
    if (*error)
        return p;
    p = SkipSpace(p);
    if (!data_0049aae8 && ScanOperator(p, &op, &length) && op.type == 3) {
        p = SkipSpace(p + length);
        p = ParseNumber(p, error, &ok, &section->at);
        if (!ok || *error) {
            *error = 0x4e;
            return p;
        }
    }
    p = SkipSpace(p);
    if (!data_0049aae8 && ScanOperator(p, &op, &length)) {
        if (op.type == 0x15) {
            if (!data_004a28e4->memory) {
                *error = 0x53;
                return p;
            }
            p = SkipSpace(p + length);
            p = GetToken(p, 0, &name);
            for (memory = data_004a28e4->memory; memory; memory = memory->next) {
                if (memory->name && name && !strcmp(memory->name, name)) {
                    section->memory = memory;
                    break;
                }
            }
            if (!memory) {
                *error = 0x4b;
                data_0049aae4 = name;
                return p;
            }
        } else {
            *error = 0x56;
            data_0049aae4 = ">";
            return p;
        }
    }
    return p;
}

static inline char *ParseAssignment(char *q, int *error, char *name, LCFSection *entry, Boolean *flag) {
    char *p;

    p = SkipSpace(q);
    p = ParseExpression(p, error, &entry->expr, 1, flag);
    if (strcmp(name, "."))
        LinkSym_Add(name, entry->expr, entry, 1);
    return p;
}

/* 0x00447970 */
char *ParseSections(char *p, int *error, LCFSection **list, LCFSection *parent) {
    LCFSection *entry;
    char *q;
    char *name;
    LCFOperator op;
    int length;
    Boolean ok;
    Boolean flag = 0;
    LCFSection *tail;
    LCFSection *section;
    LCFMemory *memory;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            section = ELF_PPC_EABI_NewOutput("", 0, 0);
            if (!*list) {
                *list = tail = section;
            } else {
                tail->next = section;
                tail = tail->next;
            }
            section->parent = parent;
            name = GetWord(p);
            if (!strcmp(name, "GROUP")) {
                p = SkipWord(p);
                q = SkipSpace(p);
                if (!*q) {
                    *error = 0x55;
                    return q;
                }
                section = ELF_PPC_EABI_NewOutput("", 0, 5);
                if (!*list)
                    *list = tail = section;
                else
                    tail->next = section;
                section->parent = parent;
                *error = 0;
                p = ParseBindAlign(q, error, section);
                if (*error) {
                    if (*error)
                        return p;
                }
                p = SkipSpace(p);
                data_0049aae8++;
                p = ParseSections(p, error, &section->next, section);
                if (*error)
                    return p;
                for (tail = section; tail->next; tail = tail->next)
                    ;
                data_0049aae8--;
                p = SkipSpace(p);
                if (!data_0049aae8 && ScanOperator(p, &op, &length) && op.type == 3) {
                    p = SkipSpace(p + length);
                    p = ParseNumber(p, error, &ok, &section->at);
                    if (!ok || *error) {
                        *error = 0x4e;
                        return p;
                    }
                }
                p = SkipSpace(p);
                if (!data_0049aae8 && ScanOperator(p, &op, &length)) {
                    if (op.type == 0x15) {
                        if (!data_004a28e4->memory) {
                            *error = 0x53;
                            return p;
                        }
                        p = SkipSpace(p + length);
                        p = GetToken(p, 0, &name);
                        for (memory = data_004a28e4->memory; memory; memory = memory->next) {
                            if (memory->name && name && !strcmp(memory->name, name)) {
                                section->memory = memory;
                                break;
                            }
                        }
                        if (!memory) {
                            *error = 0x4b;
                            data_0049aae4 = name;
                            return p;
                        }
                    } else {
                        *error = 0x56;
                        data_0049aae4 = ">";
                        return p;
                    }
                }
            } else {
                p = GetToken(p, 0, &name);
                if (!name || !*name) {
                    *error = 0x51;
                    return p;
                }
                q = SkipSpace(p);
                if (ScanOperator(q, &op, &length)) {
                    if (op.type == 3) {
                        entry = ELF_PPC_EABI_NewOutput(name, 0, 6);
                        if (!*list) {
                            *list = tail = entry;
                        } else {
                            tail->next = entry;
                            tail = tail->next;
                        }
                        entry->parent = parent;
                        q += length;
                        p = ParseAssignment(q, error, name, entry, &flag);
                        goto next;
                    } else if (op.type == 1) {
                        section = ELF_PPC_EABI_NewOutput(name, 0, 1);
                        if (IsSectionName(name)) {
                            *error = 0x6e;
                            data_0049aae4 = name;
                            return q;
                        }
                        if (IsSpecialSection(name, section)) {
                            *error = 0x75;
                            data_0049aae4 = name;
                            return q;
                        }
                        if (IsBadAbsName(name)) {
                            *error = 0x77;
                            data_0049aae4 = name;
                            return q;
                        }
                        if (!*list) {
                            *list = tail = section;
                        } else {
                            tail->next = section;
                            tail = tail->next;
                        }
                    } else {
                        *error = 0x4a;
                        data_0049aae4 = "=";
                        data_0049aae0 = "(";
                        return q;
                    }
                } else {
                    section = ELF_PPC_EABI_NewOutput(name, 0, 1);
                    if (IsSectionName(name)) {
                        *error = 0x6e;
                        data_0049aae4 = name;
                        return q;
                    }
                    if (IsSpecialSection(name, section)) {
                        *error = 0x75;
                        data_0049aae4 = name;
                        return q;
                    }
                    if (IsBadAbsName(name)) {
                        *error = 0x77;
                        data_0049aae4 = name;
                        return q;
                    }
                    if (!*list) {
                        *list = tail = section;
                    } else {
                        tail->next = section;
                        tail = tail->next;
                    }
                }
                section->parent = parent;
                p = ParseSectionAttributes(q, error, section);
            }
        next:
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x00447530 */
char *ParseMemory(char *p, int *error, LCFMemory **list) {
    LCFMemory *node;
    LCFMemory *tail;
    char *name;
    LCFToken token;
    int length;
    Boolean ok;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            node = galloc(sizeof(LCFMemory));
            memset(node, 0, sizeof(LCFMemory));
            if (!*list) {
                *list = tail = node;
            } else {
                tail->next = node;
                tail = tail->next;
            }
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            if (MemoryExists(name)) {
                *error = 0x6f;
                data_0049aae4 = name;
                return p;
            }
            node->name = name;
            p = SkipSpace(p);
            if (*p != ':') {
                *error = 0x56;
                data_0049aae4 = ":";
                return p;
            }
            p = SkipSpace(p + 1);
            name = GetWord(p);
            if (!strcmp(name, "origin") || !strcmp(name, "org") || !strcmp(name, "o")) {
                p = SkipSpace(SkipWord(p));
                if (!*p) {
                    *error = 0x55;
                    return p;
                }
                if (ScanOperator(p, (LCFOperator *)&token, &length)) {
                    if (token.kind == 3) {
                        p = SkipSpace(p + length);
                        p = ParseNumber(p, error, &ok, &node->origin);
                        if (!ok || *error) {
                            *error = 0x4e;
                            return p;
                        }
                        if (data_004a2e49)
                            node->origin = 0;
                        p = SkipSpace(p);
                        if (*p == ',')
                            p++;
                    } else {
                        *error = 0x56;
                        data_0049aae4 = "=";
                        return p;
                    }
                } else {
                    *error = 0x56;
                    data_0049aae4 = "=";
                    return p;
                }
            } else {
                *error = 0x50;
                return p;
            }
            p = SkipSpace(p);
            node->length = -1;
            name = GetWord(p);
            if (!strcmp(name, "length") || !strcmp(name, "len") || !strcmp(name, "l")) {
                p = SkipSpace(SkipWord(p));
                if (!*p) {
                    *error = 0x55;
                    return p;
                }
                if (ScanOperator(p, (LCFOperator *)&token, &length)) {
                    if (token.kind == 3) {
                        p = SkipSpace(p + length);
                        p = ParseNumber(p, error, &ok, (unsigned long *)&node->length);
                        if (!ok || *error) {
                            *error = 0x4e;
                            return p;
                        }
                        if (data_004a2e49)
                            node->length = -1;
                        if (*p == ',')
                            p++;
                    } else {
                        *error = 0x56;
                        data_0049aae4 = "=";
                        return p;
                    }
                } else {
                    *error = 0x56;
                    data_0049aae4 = "=";
                    return p;
                }
            }
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x00447460 */
char *ParseExclude(char *p, int *error) {
    char *name;
    LCFSection *section;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            for (section = data_004a28e4->sections; section; section = section->next) {
                if (section->name && name && !strcmp(section->name, name)) {
                    section->excluded = 1;
                    break;
                }
            }
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x004472d0 */
char *ParseFileList(char *p, int *error, LCFFileName **list, Boolean single) {
    LCFFileName *node;
    LCFFileName *tail;
    char *name;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            node = galloc(sizeof(LCFFileName));
            memset(node, 0, sizeof(LCFFileName));
            if (!*list) {
                *list = tail = node;
            } else if (single) {
                *error = 0x83;
                return p;
            } else {
                tail->next = node;
                tail = tail->next;
            }
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            node->name = name;
            p = SkipSpace(p);
            if (*p == '(') {
                p = SkipSpace(p + 1);
                p = GetToken(p, 0, &name);
                if (!name || !*name) {
                    *error = 0x51;
                    return p;
                }
                node->member = name;
                p = SkipSpace(p);
                if (*p == ')') {
                    p++;
                } else {
                    *error = 0x56;
                    data_0049aae4 = ")";
                    return p;
                }
            }
            p = SkipSpace(p);
            if (*p == ',')
                p++;
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x00447160 */
char *ParseForceFiles(char *p, int *error, LCFFileName **list) {
    LCFFileName *node;
    LCFFileName *tail;
    char *name;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            node = galloc(sizeof(LCFFileName));
            memset(node, 0, sizeof(LCFFileName));
            if (!*list) {
                *list = tail = node;
            } else {
                tail->next = node;
                tail = tail->next;
            }
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            node->name = name;
            p = SkipSpace(p);
            if (*p == '(') {
                p = SkipSpace(p + 1);
                p = GetToken(p, 0, &name);
                if (!name || !*name) {
                    *error = 0x51;
                    return p;
                }
                node->member = name;
                p = SkipSpace(p);
                if (*p == ')') {
                    p++;
                } else {
                    *error = 0x56;
                    data_0049aae4 = ")";
                    return p;
                }
            }
            p = SkipSpace(p);
            if (*p == ',')
                p++;
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x00447080 */
char *ParseForceActive(char *p, int *error, LCFName **list) {
    LCFName *node;
    LCFName *tail;
    char *name;

    *error = 0;
    if (*p == '{') {
        p = SkipSpace(p + 1);
        while (*p != '}') {
            node = galloc(sizeof(LCFName));
            memset(node, 0, sizeof(LCFName));
            if (!*list) {
                *list = tail = node;
            } else {
                tail->next = node;
                tail = tail->next;
            }
            p = GetToken(p, 1, &name);
            if (!name || !*name) {
                *error = 0x51;
                return p;
            }
            node->name = name;
            p = SkipSpace(p);
            if (*p == ',')
                p++;
            if (*error)
                return p;
            p = SkipSpace(p);
        }
        p++;
    } else {
        *error = 0x56;
        data_0049aae4 = "{";
    }
    return p;
}

/* 0x00446760 */
void ParseLinkerCommandFile(void) {
    LCFSection *tail;
    LCFSection **list;
    char *p;
    LCFMemory *abs;
    LCFSection *last;
    Boolean seen = 0;
    int error;
    unsigned long address;
    char *name;
    LCFToken token;
    int length;
    LCFSection *section;
    Boolean bad;

    data_0049aae8 = 0;
    fn_00413a00(data_0049aadc);
    p = *data_0049aadc;
    data_004a28e4 = galloc(sizeof(LCFFile));
    memset(data_004a28e4, 0, sizeof(LCFFile));
    abs = galloc(sizeof(LCFMemory));
    memset(abs, 0, sizeof(LCFMemory));
    abs->origin = 0;
    abs->length = -1;
    data_0049aad8 = 1;
    error = 0;
    do {
        if (fn_00420350())
            CompilerTools_Abort();
        p = SkipSpace(p);
        if (!*p)
            break;
        name = GetWord(p);
        if (!strcmp(name, "MEMORY")) {
            if (data_004a28e4->memory) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "MEMORY");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseMemory(p, &error, &data_004a28e4->memory);
        } else if (!strcmp(name, "SECTIONS")) {
            if (data_004a28e4->sections) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "SECTIONS");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            list = &data_004a28e4->sections;
            if (data_004a28e4->assignments) {
                data_004a28e4->sections = data_004a28e4->assignments;
                list = &tail->next;
            }
            p = ParseSections(p, &error, list, 0);
        } else if (!strcmp(name, "EXCLUDE")) {
            if (!data_004a28e4->sections) {
                CompilerTools_ReportLimitedDiagnostic(0x72);
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            if (data_004a28e4->exclude) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "EXCLUDE");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            data_004a28e4->exclude = 1;
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseExclude(p, &error);
        } else if (!strcmp(name, "EXCLUDEFILES")) {
            if (data_004a28e4->excludeFiles) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "EXCLUDEFILES");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseFileList(p, &error, &data_004a28e4->excludeFiles, 0);
        } else if (!strcmp(name, "INCLUDEDWARF")) {
            if (data_004a28e4->includeDwarf) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "INCLUDEDWARF");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseFileList(p, &error, &data_004a28e4->includeDwarf, 1);
        } else if (!strcmp(name, "FORCEFILES")) {
            if (data_004a28e4->forceFiles) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "FORCEFILES");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseForceFiles(p, &error, &data_004a28e4->forceFiles);
        } else if (!strcmp(name, "FORCEACTIVE")) {
            if (data_004a28e4->forceActive) {
                CompilerTools_ReportLimitedDiagnostic(0x59, "FORCEACTIVE");
                CompilerTools_FormatMessageAndLongjmp(0xd, 2);
                break;
            }
            p = SkipSpace(SkipWord(p));
            if (!*p) {
                error = 0x55;
                break;
            }
            p = ParseForceActive(p, &error, &data_004a28e4->forceActive);
        } else if (!strcmp(name, "SHORTEN_NAMES_FOR_TOR_101")) {
            data_004a2e53 = 1;
            p = SkipSpace(SkipWord(p));
        } else {
            p = GetToken(p, 0, &name);
            if (!name || !*name) {
                error = 0x51;
                break;
            }
            if (!strcmp(name, ".")) {
                error = 0x5a;
                break;
            }
            p = SkipSpace(p);
            if (ScanOperator(p, (LCFOperator *)&token, &length)) {
                if (token.kind == 3) {
                    section = ELF_PPC_EABI_NewOutput(name, 0, 6);
                    if (!data_004a28e4->sections && !data_004a28e4->assignments) {
                        list = &data_004a28e4->assignments;
                    } else if (data_004a28e4->sections && !seen) {
                        seen = 1;
                        for (tail = data_004a28e4->sections; tail->next; tail = tail->next)
                            ;
                        list = &tail->next;
                    }
                    if (!*list) {
                        *list = tail = section;
                    } else {
                        tail->next = section;
                        tail = tail->next;
                    }
                    p = SkipSpace(p + length);
                    bad = 0;
                    p = ParseExpression(p, &error, &section->expr, 1, &bad);
                    if (bad) {
                        error = 0x5a;
                        break;
                    }
                    LinkSym_Add(name, section->expr, section, 1);
                } else {
                    error = 0x56;
                    data_0049aae4 = "=";
                    break;
                }
            } else {
                error = 0x56;
                data_0049aae4 = "=";
                break;
            }
        }
    } while (*p && !error);

    Memory_FreeHandle(data_0049aadc);
    data_0049aadc = 0;
    if (error) {
        switch (error) {
        case 0x4e: case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x5a: case 0x83:
            CompilerTools_ReportLimitedDiagnostic(error, data_0049aad8);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            break;
        case 0x4b: case 0x4c: case 0x4f: case 0x56: case 0x57: case 0x58: case 0x6e: case 0x6f: case 0x75: case 0x77:
            CompilerTools_ReportLimitedDiagnostic(error, data_0049aae4, data_0049aad8);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            break;
#if LINKER >= 27
        case 0xa4:
#endif
        case 0x4a: case 0x4d:
            CompilerTools_ReportLimitedDiagnostic(error, data_0049aae4, data_0049aae0, data_0049aad8);
            CompilerTools_FormatMessageAndLongjmp(0xd, 2);
            break;
        }
    }

    abs->next = data_004a28e4->memory;
    data_004a28e4->memory = abs;
    for (section = data_004a28e4->sections; section; section = section->next) {
        if (!section->parent && !section->memory) {
            section->memory = abs;
            section->xcc = 1;
            if (section->kind == 1 && section->name && !strncmp(section->name, ".abs.", 5)) {
                sscanf(section->name + 5, "%x", &address);
                section->memory = galloc(sizeof(LCFMemory));
                memset(section->memory, 0, sizeof(LCFMemory));
                section->memory->origin = data_004a2e49 ? 0 : address;
                section->memory->length = -1;
            }
        }
        last = section;
    }
    if (data_004a28e4->sections) {
        LCFSection *s;
        LCFSection *prev;
        prev = last;
        last = ELF_PPC_EABI_NewOutput(".debug", 0x14, 2);
        prev->next = last;
        prev = last;
        NewDebugRef()->section = prev;
        s = ELF_PPC_EABI_NewOutput(".debug_sfnames", 0x14, 2);
        prev->next = s;
        prev = s;
        NewDebugRef()->section = s;
        s = ELF_PPC_EABI_NewOutput(".debug_srcinfo", 0x14, 2);
        prev->next = s;
        prev = s;
        NewDebugRef()->section = s;
        s = ELF_PPC_EABI_NewOutput(".line", 0x15, 2);
        prev->next = s;
        prev = s;
        s = ELF_PPC_EABI_NewOutput(".shstrtab", 0x18, 3);
        prev->next = s;
        prev = s;
        s = ELF_PPC_EABI_NewOutput(".strtab", 0x17, 3);
        prev->next = s;
        prev = s;
        s = ELF_PPC_EABI_NewOutput(".symtab", 0x16, 3);
        prev->next = s;
        prev = s;
        if (data_004a2e45)
            prev->next = ELF_PPC_EABI_NewOutput(".PPC.EMB.seginfo", 0x19, 3);
    }
}

/* 0x004466b0 */
Boolean ReadLinkerCommandFile(void) {
    int i;
    long count;
    Boolean found;

    count = fn_00420260();
    found = 0;
    data_004a28e4 = 0;
    for (i = 0; i < count; i++) {
        if ((data_0049aadc = fn_0041e410(i)) != 0) {
            if (found) {
                CompilerTools_ReportDiagnostic(0x47);
                break;
            }
            ParseLinkerCommandFile();
            found = 1;
        }
    }
    if (!found) {
        CompilerTools_ReportLimitedDiagnostic(0x48);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    found = data_004a28e4 ? data_004a28e4->sections != 0 : 0;
    if (data_004a28e4 && !found) {
        CompilerTools_ReportLimitedDiagnostic(0x76);
        CompilerTools_FormatMessageAndLongjmp(0xd, 2);
    }
    return found;
}

/* 0x00446580 */
unsigned long EvalOperator(LCFExpr *left, LCFExpr *op, LCFExpr *right) {
    switch (op->u.op->type) {
    case 3: return right->u.value;
    case 8: return !right->u.value;
    case 9: return ~right->u.value;
    case 10: return -right->u.value;
    case 11: return right->u.value;
    case 6: case 12: return left->u.value * right->u.value;
    case 7: case 13: return left->u.value / right->u.value;
    case 14: return left->u.value % right->u.value;
    case 4: case 15: return left->u.value + right->u.value;
    case 5: case 16: return left->u.value - right->u.value;
    case 18: return left->u.value << right->u.value;
    case 17: return left->u.value >> right->u.value;
    case 19: return left->u.value == right->u.value;
    case 20: return left->u.value != right->u.value;
    case 21: return left->u.value > right->u.value;
    case 22: return left->u.value < right->u.value;
    case 23: return left->u.value <= right->u.value;
    case 24: return left->u.value >= right->u.value;
    case 25: return left->u.value & right->u.value;
    case 26: return left->u.value | right->u.value;
    case 27: return left->u.value && right->u.value;
    case 28: return left->u.value || right->u.value;
    }
    return 0;
}

/* 0x004464e0 */
LCFExpr *RemoveParens(LCFExpr *list) {
    LCFExpr *open;
    Boolean changed;
    LCFExpr *node;
    LCFExpr *prev;

    do {
        changed = 0;
        while (list) {
            if (list->kind != EXPR_DELETED)
                break;
            list = list->next;
        }
        for (node = list, prev = 0; node; node = node->next) {
            if (node->kind == EXPR_DELETED)
                prev->next = node->next;
            else if (!prev)
                prev = list;
            else
                prev = prev->next;
        }
        for (node = list; node; node = node->next) {
            if (node->kind == EXPR_OPERATOR && node->u.op->type == 1) {
                open = node;
            } else if (node->kind == EXPR_OPERATOR && node->u.op->type == 2) {
                if (open->next->next == node || open->next == node) {
                    changed = 1;
                    node->kind = EXPR_DELETED;
                    open->kind = node->kind;
                }
            }
        }
    } while (changed);
    return list;
}

#if LINKER >= 27
/* A copy of expression LIST, to evaluate it again (2.7 evaluates a linker-defined symbol's each time, reporting
   an undefined symbol in it each time). (Not an original function.) */
LCFExpr *LCF_CopyExpr(LCFExpr *list) {
    LCFExpr *head = 0;
    LCFExpr *tail = 0;
    LCFExpr *node;

    for (; list; list = list->next) {
        node = galloc(sizeof(LCFExpr));
        *node = *list;
        node->next = 0;
        if (node->kind == EXPR_NEXT)
            node->u.expr = LCF_CopyExpr(list->u.expr);
        if (!head)
            head = node;
        else
            tail->next = node;
        tail = node;
    }
    return head;
}
#endif

/* 0x00446330 */
LCFExpr *ReduceExpr(LCFExpr *list) {
    LCFExpr *right;
    LCFExpr *node;
    LCFExpr *op;
    LCFExpr *open;
    LCFExpr *left;
    LCFExpr *lastNumber;
    int depth;
    unsigned char kind;
    Boolean unary = 0;

    list = RemoveParens(list);
    while (list && list->next) {
        open = left = op = right = 0;
        depth = 0;
        for (node = list; node; node = node->next) {
            kind = node->kind;
            if (kind == EXPR_OPERATOR && node->u.op->type == 1) {
                if (!open) {
                    open = node;
                    left = op = right = 0;
                    depth = 0;
                    continue;
                }
                if (node->u.op->depth > open->u.op->depth) {
                    open = node;
                    left = op = right = 0;
                    depth = 0;
                    continue;
                }
            } else if (kind == EXPR_OPERATOR && node->u.op->type == 2) {
                depth = open->u.op->depth + 1;
            }
            if (depth)
                continue;
            if (kind == EXPR_NUMBER) {
                lastNumber = node;
                if (!left)
                    left = node;
                else if (!right)
                    right = node;
            } else if (kind == EXPR_OPERATOR) {
                unary = node->u.op->unary;
                if (unary) {
                    right = node->next;
                    op = node;
                    if (right->kind == EXPR_OPERATOR)
                        continue;
                    break;
                } else if (!op) {
                    op = node;
                } else if (node->u.op->precedence < op->u.op->precedence) {
                    op = node;
                    right = 0;
                    left = lastNumber;
                }
            } else {
                LCF_ASSERT(0, 2126);
            }
        }
        if (unary) {
            right->u.value = EvalOperator(0, op, right);
            op->kind = EXPR_DELETED;
        } else {
            left->u.value = EvalOperator(left, op, right);
            op->kind = EXPR_DELETED;
            right->kind = EXPR_DELETED;
        }
        list = RemoveParens(list);
    }
    return list;
}

/* 0x004461b0 */
LCFExpr *EvalExpr(LCFExpr *list, LCFSection *section) {
    LCFExpr *node;
    LCFSymbol *symbol;
    unsigned long align;
    LCFMemory *memory;
    long length;
    long index;

    for (node = list; node; node = node->next) {
        switch (node->kind) {
        case EXPR_NEXT:
            node->u.expr = EvalExpr(node->u.expr, section);
            LCF_ASSERT(node->u.expr->kind == EXPR_NUMBER, 2155);
            align = node->u.expr->u.value;
            node->u.value = (section->address + section->size + (align - 1)) & ~(align - 1);
            node->kind = EXPR_NUMBER;
            break;
        case EXPR_ROMADDR:
            node->kind = EXPR_NUMBER;
            node->u.value = node->u.section->romAddress;
            break;
        case EXPR_ADDR:
            node->kind = EXPR_NUMBER;
            node->u.value = node->u.section->address;
            break;
        case EXPR_SIZEOF:
            node->kind = EXPR_NUMBER;
            node->u.value = node->u.section->size;
            break;
        case EXPR_ORIGIN:
            node->kind = EXPR_NUMBER;
            node->u.value = node->u.memory->origin;
            break;
        case EXPR_LENGTH:
            node->kind = EXPR_NUMBER;
            memory = node->u.memory;
            length = memory->length;
            if (length == -1)
                node->u.value = memory->end;
            else
                node->u.value = length;
            break;
        case EXPR_DOT:
            node->kind = EXPR_NUMBER;
            node->u.value = section->address + section->size;
            break;
        case EXPR_SYMBOL:
            node->kind = EXPR_NUMBER;
            symbol = LK_symbol_Find(node->u.name, -1);
            if (symbol) {
                index = symbol->section;
                if (index < 0)
                    node->u.value = ELF_PPC_EABI_LinkSymbolValue(index);
                else
                    node->u.value = symbol->value;
            } else {
                CompilerTools_ReportDiagnostic(0x70, node->u.name);
                node->u.value = 0;
            }
            break;
        }
    }
    if (list)
        list = ReduceExpr(list);
    LCF_ASSERT(!list || list->kind == EXPR_NUMBER, 2207);
    return list;
}
