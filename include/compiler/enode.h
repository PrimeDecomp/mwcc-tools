#ifndef COMPILER_ENODE_H
#define COMPILER_ENODE_H

#include "compiler/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENODE_IS(_enode, _etype) ((_enode)->type == (_etype))
#define ENODE_IS2(_enode, _etype1, _etype2) \
    (ENODE_IS(_enode, (_etype1)) || ENODE_IS(_enode, (_etype2)))
#define ENODE_IS3(_enode, _etype1, _etype2, _etype3) \
    (ENODE_IS2(_enode, _etype1, _etype2) || ENODE_IS(_enode, (_etype3)))
#define ENODE_IS4(_enode, _etype1, _etype2, _etype3, _etype4) \
    (ENODE_IS3(_enode, _etype1, _etype2, _etype3) || ENODE_IS(_enode, (_etype4)))
#define ENODE_IS_RANGE(_enode, _lo, _hi) (((_enode)->type >= (_lo)) && ((_enode)->type <= (_hi)))
#define ENODE_IS_ASSIGN(_enode) (ENODE_IS_RANGE((_enode), EASS, EORASS))
#define ENODE_IS_ASSIGN_TO(_enode, _etype) \
    (ENODE_IS_RANGE((_enode), EASS, EORASS) && \
     ENODE_IS((_enode)->data.diadic.left->data.monadic, (_etype)))
#define ENODE_QUALS(_enode) ((UInt32)((_enode)->flags & ENODE_FLAG_QUALS))
#define ENODE_IS_INDIRECT_TO(_enode, _etype) \
    (ENODE_IS((_enode), EINDIRECT) && ENODE_IS((_enode)->data.monadic, (_etype)))

// The case labels of a switch over expression kinds (Pro7's). 0 to 8, 0x30 to 0x31
#define ENODE_CASE_MONADIC \
    case EPOSTINC: \
    case EPOSTDEC: \
    case EPREINC: \
    case EPREDEC: \
    case EINDIRECT: \
    case EMONMIN: \
    case EBINNOT: \
    case ELOGNOT: \
    case EFORCELOAD: \
    case ETYPCON: \
    case EBITFIELD

// 9 to 0x1B
#define ENODE_CASE_DIADIC_1 \
    case EMUL: \
    case EMULV: \
    case EDIV: \
    case EMODULO: \
    case EADDV: \
    case ESUBV: \
    case EADD: \
    case ESUB: \
    case ESHL: \
    case ESHR: \
    case ELESS: \
    case EGREATER: \
    case ELESSEQU: \
    case EGREATEREQU: \
    case EEQU: \
    case ENOTEQU: \
    case EAND: \
    case EXOR: \
    case EOR

// 0x1E to 0x28, 0x2D, 0x2F
#define ENODE_CASE_ASSIGN \
    case EASS: \
    case EMULASS: \
    case EDIVASS: \
    case EMODASS: \
    case EADDASS: \
    case ESUBASS: \
    case ESHLASS: \
    case ESHRASS: \
    case EANDASS: \
    case EXORASS: \
    case EORASS: \
    case EBCLR: \
    case EBSET

// 9 to 0x2F
#define ENODE_CASE_DIADIC_ALL \
    case EMUL: \
    case EMULV: \
    case EDIV: \
    case EMODULO: \
    case EADDV: \
    case ESUBV: \
    case EADD: \
    case ESUB: \
    case ESHL: \
    case ESHR: \
    case ELESS: \
    case EGREATER: \
    case ELESSEQU: \
    case EGREATEREQU: \
    case EEQU: \
    case ENOTEQU: \
    case EAND: \
    case EXOR: \
    case EOR: \
    case ELAND: \
    case ELOR: \
    case EASS: \
    case EMULASS: \
    case EDIVASS: \
    case EMODASS: \
    case EADDASS: \
    case ESUBASS: \
    case ESHLASS: \
    case ESHRASS: \
    case EANDASS: \
    case EXORASS: \
    case EORASS: \
    case ECOMMA: \
    case EPMODULO: \
    case EROTL: \
    case EROTR: \
    case EBCLR: \
    case EBTST: \
    case EBSET

// The expression node kinds, in the order and with the names of the compiler's own table of them
// (DumpIR's, at 0x55268c: index = kind).
//
// The order is 2.x's (2.0 through 2.7, and 1.3.2), as each one's own name table says (the strings
// at 0x5bc980 in 2.6, which IroDump.c fills): Ninji's 2.4.5 order. 1.2.5 carries an older table as
// well, with EQUALNAME and ETEMPX and without EINITTRYCATCH, from a unit not rebuilt since; this
// header followed that one until 2.6's names were checked against its table.
typedef enum ENodeType {
    EPOSTINC,
    EPOSTDEC,
    EPREINC,
    EPREDEC,
    EINDIRECT,
    EMONMIN,
    EBINNOT,
    ELOGNOT,
    EFORCELOAD,
    EMUL,
    EMULV,
    EDIV,
    EMODULO,
    EADDV,
    ESUBV,
    EADD,
    ESUB,
    ESHL,
    ESHR,
    ELESS,
    EGREATER,
    ELESSEQU,
    EGREATEREQU,
    EEQU,
    ENOTEQU,
    EAND,
    EXOR,
    EOR,
    ELAND,
    ELOR,
    EASS,
    EMULASS,
    EDIVASS,
    EMODASS,
    EADDASS,
    ESUBASS,
    ESHLASS,
    ESHRASS,
    EANDASS,
    EXORASS,
    EORASS,
    ECOMMA,
    EPMODULO,
    EROTL,
    EROTR,
    EBCLR,
    EBTST,
    EBSET,
    ETYPCON,
    EBITFIELD,
    EINTCONST,
    EFLOATCONST,
    ESTRINGCONST,
    ECOND,
    EFUNCCALL,
    EFUNCCALLP,
    EOBJREF,
    EMFPOINTER,
    ENULLCHECK,
    EPRECOMP,
    ETEMP,
    EARGOBJ,
    ELOCOBJ,
    ELABEL,
    ESETCONST,
    ENEWEXCEPTION,
    ENEWEXCEPTIONARRAY,
    EINITTRYCATCH,
    EOBJLIST,
    EMEMBER,
    ETEMPLDEP,
    EINSTRUCTION,
    EDEFINE,
    EREUSE,
    EASSBLK,
    EVECTOR128CONST,
    ECONDASS,
    MAXEXPR
} ENodeType;
enum {
    ENODE_FLAG_CONST = Q_CONST,
    ENODE_FLAG_VOLATILE = Q_VOLATILE,
    ENODE_FLAG_QUALS = Q_CONST | Q_VOLATILE,
    ENODE_FLAG_10 = 0x10,
    ENODE_FLAG_80 = 0x80
};
#pragma pack(push, 2)
struct EMemberInfo {
    BClassList* path;
    ENode* expr;
    NameSpaceObjectList* list;
    TemplArg* templargs;
    // TODO: invented name (Pro7: pr_1D): the name was qualified
    Boolean is_qualified;
    // TODO: invented name (Pro7: x11): the member's address is taken
    Boolean addressTaken;
    Boolean isambig;
};
struct ENodeList {
    ENodeList* next;
    ENode* node;
};
typedef enum TemplDepSubType {
    TDE_PARAM,
    TDE_SIZEOF,
    TDE_ALIGNOF,
    TDE_CAST,
    TDE_QUALNAME,
    TDE_OBJ,
    TDE_SOURCEREF,
    TDE_ADDRESS_OF
} TemplDepSubType;
union ENodeUnion {
    CInt64 intval;
    Float floatval;
    MWVector128 vector128val;
    SInt32 longval;
    ENode* monadic;
    EMemberInfo* emember;
    Object* objref;
    struct CLabel* label;
    struct {
        struct NameSpaceObjectList* list;
        TemplArg* templargs;
        struct HashNameNode* name;
    } objlist;
    struct {
        ENode* left;
        ENode* right;
    } diadic;
    struct {
        ENode* accessnode;
        ENode* mfpointer;
    } mfpointer;
    struct {
        ENode* nullcheckexpr;
        ENode* condexpr;
        SInt32 precompid;
    } nullcheck;
    SInt32 precompid;
    struct {
        ENode* cond;
        ENode* expr1;
        ENode* expr2;
    } cond;
    struct {
        ENode* funcref;
        ENodeList* args;
        TypeFunc* functype;
    } funccall;
    struct {
        Type* type;
        SInt32 uniqueid;
        Boolean needs_dtor;
    } temp;
    struct {
        SInt32 size;
        char* data;
        unsigned char ispascal;
    } string;
    struct {
        Object* objref;
        SInt32 offset;
    } addr;
    struct {
        ENode* initexpr;
        ENode* tryexpr;
        Object* pointertemp;
        Object* deletefunc;
    } newexception;
    struct {
        ENode* initexpr;
        ENode* tryexpr;
        ENode* catchexpr;
        ENode* result;
    } itc;
    struct {
        union {
            TemplParamID pid;
            struct {
                Type* type;
            } typeexpr;
            struct {
                ENodeList* args;
                Type* type;
                UInt32 qual;
            } cast;
            struct {
                struct TypeTemplDep* type;
                HashNameNode* name;
            } qual;
            struct {
                ENode* expr;
                struct TStreamElement* token;
            } sourceref;
            ENode* monadic;
            Object* obj;
        } u;
        TemplDepSubType subtype;
    } templdep;
};
// sizeof(ENode) is 0x1e: 2.6's allocation sites request 30 bytes (galloc(26) was an overrun).
struct ENode {
    ENodeType type;
    UInt8 cost;
    UInt16 flags;
    Boolean ignored;
    Boolean hascall;
    Type* rtype;
    struct PointsToFunction* pointsTo; // (IrOptimizer's BuildExpr, from IROLinear pointsToFunction)
    ENodeUnion data;
};
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif
