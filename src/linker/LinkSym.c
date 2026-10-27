/* The linker-defined symbols: the predefined ones (_stack_addr, _SDA_BASE_, _ctors...), those the command file
   assigns, and those made for each output section (_f_NAME, _f_NAME_rom, _e_NAME, .phdr_N.sec_M, .startof.NAME_N),
   each with the expression of its value.

   Built with -inline deferred, the compiler emits the functions in the reverse of their order here. The assertion
   names this file "LinkSym.c". */
#include "compiler/common.h"
#include "compiler/CompilerTools.h"

extern void CompilerTools_FormatMessageAndLongjmp(int message, int status, ...);
extern unsigned char CompilerTools_ReportDiagnostic(long diagnosticCode, ...);
extern int sprintf(char *s, const char *format, ...);
extern unsigned long strlen(const char *s);
extern char *strcpy(char *dst, const char *src);
extern char *strcat(char *dst, const char *src);
extern int strcmp(const char *a, const char *b);
extern void *memset(void *dst, int c, unsigned long n);

#pragma options align = mac68k

/* LinkerCmdFile.c's: an operator of an expression, and a term of one */
typedef struct LinkOperator {
    int precedence;
    int depth;
    unsigned char type;
    Boolean unary;
} LinkOperator;

typedef struct LinkExpr {
    struct LinkExpr *next;
    union {
        LinkOperator *op;
        struct LinkSection *section;
    } u;
    unsigned char kind;
} LinkExpr;

enum { LINK_OPERATOR = 1, LINK_ROMADDR = 5, LINK_ADDR = 6, LINK_SIZEOF = 9 };
enum { LINK_PLUS = 0xf };

/* A linker-defined symbol (26 bytes). */
typedef struct LinkSymbol {
    char *name;
    long index;               /* the predefined ones -1 to -12, the others from -14 down */
    Boolean x8;               /* (1 for the predefined tables: _rom_copy_info, _bss_init_info, _ctors, ...) */
    Boolean made;             /* a .startof. or .phdr symbol */
    struct LinkSymbol *next;
    LinkExpr *expr;           /* its value; none yet for a predefined one nothing defines */
    struct LinkSection *section;
    char x16[4];
} LinkSymbol;

#pragma options align = reset

/* LinkerCmdFile.c's output section, what this file reads of it. */
typedef struct LinkSection {
    char x0[8];
    char *name;
    char xc[0x7c - 0xc];
    long kind;                /* 8: no ROM copy (no _f_NAME_rom) */
    char x80[0xa8 - 0x80];
    long number;
    char xac[0xbc - 0xac];
    LinkSymbol *first;        /* _f_NAME */
    LinkSymbol *romFirst;     /* _f_NAME_rom */
    LinkSymbol *end;          /* _e_NAME */
} LinkSection;

/* The predefined symbols, which the other units also read; the others are a list after them. */
LinkSymbol LinkSym_Table[] = {
    {"_stack_addr", -1},
    {"_stack_end", -2},
    {"_heap_addr", -3},
    {"_heap_end", -4},
    {"_nbfunctions", -5},
#if LINKER < 27
    {"_rom_copy_info", -6, 1},
    {"_bss_init_info", -7, 1},
#endif
    {"_SDA_BASE_", -8},
    {"_SDA2_BASE_", -9},
#if LINKER >= 27
    /* (2.7: after the small data bases) */
    {"_rom_copy_info", -6, 1},
    {"_bss_init_info", -7, 1},
#endif
#if LINKER < 27
    /* (2.7's are symbols of its Linker Generated Symbol File, ELF_linker.c) */
    {"_ctors", -10, 1},
    {"_dtors", -11, 1},
#endif
    {"_eti_init_info", -12, 1},
    {NULL},
};

extern LinkSymbol *data_004a2934;  /* the list of the other symbols */
static LinkSymbol *LinkSym_Last;   /* its last */

LinkSymbol *LinkSym_Find(char *name);
LinkSymbol *LinkSym_Add(char *name, LinkExpr *expr, LinkSection *section, Boolean assigned);

/* A term of an expression of a kind, cleared: TERM reused, else a new one. */
static LinkExpr *LinkSym_Term(LinkExpr *term, unsigned char kind) {
    if (!term)
        term = (LinkExpr *)galloc(sizeof(LinkExpr));
    memset(term, 0, sizeof(LinkExpr));
    term->kind = kind;
    return term;
}

/* The value a section's symbol has already, to be made again (none: a new one is made). */
static LinkExpr *LinkSym_OldValue(LinkSymbol *symbol) {
    return symbol ? symbol->expr : NULL;
}

/* Gives the symbol NAME made for a section (SYMBOL, when one has that name already) the value EXPR: one already
   there, predefined or assigned by the command file, is reported and takes it. */
static LinkSymbol *LinkSym_Define(LinkSymbol *symbol, char *name, LinkExpr *expr, LinkSection *section) {
    if (!symbol)
        return LinkSym_Add(name, expr, section, 0);
    CompilerTools_ReportDiagnostic(0x79, name);
    symbol->expr = expr;
    symbol->section = section;
    symbol->name = name;
    return symbol;
}

/* Gives NAME the value EXPR, adding it to the list when it is not there: one that has a value already is reported
   (0x6d when the command file assigns it, 0x6c otherwise) and keeps it. */
/* 0x4528d0 */
LinkSymbol *LinkSym_Add(char *name, LinkExpr *expr, LinkSection *section, Boolean assigned) {
    LinkSymbol *symbol;

    symbol = LinkSym_Find(name);
    if (symbol) {
        if (symbol->expr) {
            CompilerTools_ReportDiagnostic(assigned ? 0x6d : 0x6c, name);
            return symbol;
        }
    } else {
        symbol = (LinkSymbol *)galloc(sizeof(LinkSymbol));
        memset(symbol, 0, sizeof(LinkSymbol));
        if (!data_004a2934) {
            symbol->index = -14;
            LinkSym_Last = symbol;
            data_004a2934 = symbol;
        } else {
            symbol->index = LinkSym_Last->index - 1;
            LinkSym_Last->next = symbol;
            LinkSym_Last = LinkSym_Last->next;
        }
    }
    symbol->expr = expr;
    symbol->section = section;
    symbol->name = name;
    return symbol;
}

/* NAME with its dots made underscores (NAME itself when it has none). */
/* 0x452860 */
char *LinkSym_Underscores(char *name) {
    char *at;
    char *copy;
    char *to;

    for (at = name; *at; at++) {
        if (*at == '.')
            break;
    }
    if (!*at)
        return name;
    copy = (char *)galloc(strlen(name) + 1);
    for (to = copy; *name; name++, to++) {
        *to = *name;
        if (*to == '.')
            *to = '_';
    }
    *to = 0;
    return copy;
}

/* Makes .startof.NAME_N, the address of a section (its _f_NAME's value): N is its number, of 3 digits. */
/* 0x452710 */
LinkSymbol *LinkSym_AddStartOf(LinkSection *section) {
    char number[12];
    char *name;
    LinkSymbol *symbol;

    if (!section->first)
        CompilerTools_FormatMessageAndLongjmp(0x33, 2, "LinkSym.c", 0x79);
    sprintf(number, "_%.3i", section->number);
    name = (char *)galloc(strlen(section->name) + strlen(".startof.") + strlen(number) + 1);
    strcpy(name, ".startof.");
    strcat(name, section->name);
    strcat(name, number);
    symbol = LinkSym_Define(LinkSym_Find(name), name, section->first->expr, section);
    symbol->made = 1;
    return symbol;
}

/* Makes .phdr_SEGMENT.sec_N, the address of section N of a program segment (its _f_NAME's value). */
/* 0x4525a0 */
LinkSymbol *LinkSym_AddSegment(LinkSection *section, long segment) {
    char sectionNumber[12];
    char segmentNumber[12];
    char *name;
    LinkSymbol *symbol;

    sprintf(sectionNumber, "%i", section->number);
    sprintf(segmentNumber, "%i", segment);
    name = (char *)galloc(strlen(".phdr_") + strlen(segmentNumber) + strlen(".sec_") + strlen(sectionNumber) + 1);
    strcpy(name, ".phdr_");
    strcat(name, segmentNumber);
    strcat(name, ".sec_");
    strcat(name, sectionNumber);
    symbol = LinkSym_Define(LinkSym_Find(name), name, section->first->expr, section);
    symbol->made = 1;
    return symbol;
}

/* Makes a section's symbols, its name's dots made underscores: _f_NAME, ADDR(section); _f_NAME_rom, its ROM
   address, unless the section has no ROM copy; and _e_NAME, ADDR(section) + SIZEOF(section). */
/* 0x452210 */
void LinkSym_AddSectionSymbols(LinkSection *section) {
    Boolean rom;
    char *name;
    unsigned long length;
    char *symbolName;
    LinkExpr *expr;
    LinkExpr *plus;
    LinkExpr *size;
    LinkOperator *op;

    rom = section->kind != 8;
    name = LinkSym_Underscores(section->name);
    length = strlen(name);

    symbolName = (char *)galloc(length + 4);
    strcpy(symbolName, "_f");
    strcat(symbolName, name);
    section->first = LinkSym_Find(symbolName);
    expr = LinkSym_Term(LinkSym_OldValue(section->first), LINK_ADDR);
    expr->u.section = section;
    section->first = LinkSym_Define(section->first, symbolName, expr, section);

    if (rom) {
        symbolName = (char *)galloc(length + 8);
        strcpy(symbolName, "_f");
        strcat(symbolName, name);
        strcat(symbolName, "_rom");
        section->romFirst = LinkSym_Find(symbolName);
        expr = LinkSym_Term(LinkSym_OldValue(section->romFirst), LINK_ROMADDR);
        expr->u.section = section;
        section->romFirst = LinkSym_Define(section->romFirst, symbolName, expr, section);
    }

    symbolName = (char *)galloc(length + 4);
    strcpy(symbolName, "_e");
    strcat(symbolName, name);
    section->end = LinkSym_Find(symbolName);
    expr = LinkSym_Term(LinkSym_OldValue(section->end), LINK_ADDR);
    expr->u.section = section;
    plus = LinkSym_Term(NULL, LINK_OPERATOR);
    expr->next = plus;
    op = (LinkOperator *)galloc(sizeof(LinkOperator));
    memset(op, 0, sizeof(LinkOperator));
    op->type = LINK_PLUS;
    op->precedence = 3;
    plus->u.op = op;
    size = LinkSym_Term(NULL, LINK_SIZEOF);
    plus->next = size;
    size->u.section = section;
    section->end = LinkSym_Define(section->end, symbolName, expr, section);
}

/* The symbol of an index (a symbol table's index of a linker-defined symbol), or NULL. */
/* 0x4521c0 */
LinkSymbol *LinkSym_FindIndex(long index) {
    LinkSymbol *symbol;
    int i;

    for (i = 0; LinkSym_Table[i].name; i++) {
        if (LinkSym_Table[i].index == index)
            return &LinkSym_Table[i];
    }
    for (symbol = data_004a2934; symbol; symbol = symbol->next) {
        if (symbol->index == index)
            return symbol;
    }
    return NULL;
}

/* The symbol of a name, or NULL. */
/* 0x452150 */
LinkSymbol *LinkSym_Find(char *name) {
    LinkSymbol *symbol;
    int i;

    for (i = 0; LinkSym_Table[i].name; i++) {
        if (!strcmp(name, LinkSym_Table[i].name))
            return &LinkSym_Table[i];
    }
    for (symbol = data_004a2934; symbol; symbol = symbol->next) {
        if (!strcmp(name, symbol->name))
            return symbol;
    }
    return NULL;
}
