#ifndef COMPILER_OBJECTS_H
#define COMPILER_OBJECTS_H

#include "compiler/common.h"
#include "compiler/tokens.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBJ_BASE(obj) ((ObjBase*)(obj))
#define OBJ_ENUM_CONST(obj) ((ObjEnumConst*)(obj))
#define OBJ_TYPE(obj) ((ObjType*)(obj))
#define OBJ_MEMBER_VAR(obj) ((ObjMemberVar*)(obj))
#define OBJ_TYPE_TAG(obj) ((ObjTypeTag*)(obj))
#define OBJ_NAMESPACE(obj) ((ObjNameSpace*)(obj))
#define OBJ_MEMBER_VAR_PATH(obj) ((ObjMemberVarPath*)(obj))
#define OBJECT(obj) ((Object*)(obj))
enum {
    OT_ENUMCONST,
    OT_TYPE,
    OT_TYPETAG,
    OT_NAMESPACE,
    OT_MEMBERVAR,
    OT_OBJECT,
    OT_ILLEGAL
};
// Single-byte enumeration; DALIAS is 6 in this build (CMangler_FunctionLinkName
// follows the alias chain on 6) and DFUNC/DVFUNC are 3/4.
enum {
    DDATA,
    DLOCAL,
    DABSOLUTE,
    DFUNC,
    DVFUNC,
    DINLINEFUNC,
    DALIAS,
    DEXPR,
    // 2.6 lacks Pro7's Mach-O DNONLAZYPTR: CMangler_GetLinkName answers 8 the way Pro7 answers DLABEL
    DLABEL
};
// Object.section, until ObjGen gives the object its ELF section's header index: Pro7's Section
// kinds (common.h) where 2.6 keeps them, and the kinds GetCurrentSection takes for the sections
// `#pragma section` sets. Defined as numbers: the halfword compared with a byte-sized enumerator
// compiles differently from one compared with a literal (assemblefunction).
typedef UInt16 Section;
#define SECT_DEFAULT 0
#define SECT_TEXT 1
#define SECT_DATA 2
#define SECT_OBJC_CAT_CLS_METH 7
#define SECT_OBJC_CAT_INST_METH 8
#define SECT_OBJC_STRING_OBJECT 9
#define SECT_OBJC_CSTRING_OBJECT 10
#define SECT_OBJC_MESSAGE_REFS 11
#define SECT_OBJC_SEL_FIXUP 12
#define SECT_OBJC_CLS_REFS 13
#define SECT_OBJC_CLASS 14
#define SECT_OBJC_META_CLASS 15
#define SECT_OBJC_CLS_METH 16
#define SECT_OBJC_INST_METH 17
#define SECT_OBJC_PROTOCOL 18
#define SECT_OBJC_CLASS_NAMES 19
#define SECT_OBJC_METH_VAR_TYPES 20
#define SECT_OBJC_METH_VAR_NAMES 21
#define SECT_OBJC_CATEGORY 22
#define SECT_OBJC_CLASS_VARS 23
#define SECT_OBJC_INSTANCE_VARS 24
#define SECT_OBJC_MODULE_INFO 25
#define SECT_OBJC_MSYMBOLS 26
// TODO: invented names, after `#pragma section`'s const_type, sdata_type and sconst_type
#define SECT_CONST_TYPE 0x20
#define SECT_SDATA_TYPE 0x21
#define SECT_SCONST_TYPE 0x22
enum {
    OBJECT_USED = 1,
    OBJECT_FLAGS_2 = 2,
    OBJECT_DEFINED = 4,
    OBJECT_LAZY = 8,
    OBJECT_INTERNAL = 0x10,
    OBJECT_IMPORT = 0x20,
    OBJECT_EXPORT = 0x40,
    OBJECT_LIB_EXPORT = 0x60
};
#pragma pack(push, 2)
struct ObjectList {
    ObjectList* next;
    Object* object;
};
// Common prefix: otype selects the concrete object payload.
struct ObjBase {
    UInt8 otype;
    AccessType access;
};
struct ObjEnumConst {
    UInt8 otype;
    AccessType access;
    ObjEnumConst* next;
    HashNameNode* name;
    Type* type;
    CInt64 val;
};
// OT_NAMESPACE object: an object that names a namespace (CPrec, CScope and four other views agree).
struct ObjNameSpace {
    UInt8 otype;
    AccessType access;
    NameSpace* nspace;
};
struct ObjType {
    UInt8 otype;
    AccessType access;
    Type* type;
    UInt32 qual;
};
struct ObjMemberVar {
    UInt8 otype;
    AccessType access;
    Boolean anonunion;
    Boolean has_path;
    ObjMemberVar* next;
    HashNameNode* name;
    Type* type;
    UInt32 qual;
    UInt32 offset;
};
// OT_TYPETAG object: a struct, union, class or enum tag (6 bytes).
struct ObjTypeTag {
    UInt8 otype;
    AccessType access;
    Type* type;
};
// An OT_MEMBERVAR brought in by a using-declaration, with the access path to its class.
struct ObjMemberVarPath {
    UInt8 otype;
    AccessType access;
    Boolean anonunion;
    Boolean has_path;
    ObjMemberVar* next;
    HashNameNode* name;
    Type* type;
    UInt32 qual;
    UInt32 offset;
    BClassList* path;
};
// An inline function's cross-references to one object: numxrefs (offset, varoffset) pairs.
struct InlineXRef {
    struct InlineXRef* next;
    Object* object;
    UInt16 xrefoffset;
    UInt16 numxrefs;
    struct {
        UInt32 offset;
        SInt32 varoffset;
    } xref[1];
};
// An OT_OBJECT: a variable, function or other named object (68 bytes)
struct Object {
    UInt8 otype;
    AccessType access;
    UInt8 datatype;
    // 2.6: a halfword (Pro7: a Section byte at offset 3): a section kind (SECT_*), then the ELF
    // section's header index
    UInt16 section;
    NameSpace* nspace;
    HashNameNode* name;
    Type* type;
    UInt32 qual;
    SInt16 sclass;
    UInt8 flags;
    struct ExtendedParam* extParam;
    union {
        struct Object* toc;
        // 2.6: DWARF's entry waiting for the object's definition
        struct DwarfNode* pendingEntry;
    };
    union {
        struct DwarfSymbol* symbol;
        struct DwarfNode* entry;
    } debugInfo;
    struct VarRecord* varptr;
    union {
        struct {
            union {
                CInt64 intconst;
                Float* floatconst;
                char* string;
                struct {
                    char* data;
                    SInt32 size;
                } switchtable;
            } u;
            union {
                VarInfo* info;
                struct InterruptGenerationRecord* interruptInfo;
            };
#if VERSION < VERSION_GC_2_5
            // a global register variable's register (Wii's Registers_GetRegInfo); 2.0 has it
            // before linkname
            ObjRegInfo reginfo;
#endif
            HashNameNode* linkname;
#if VERSION >= VERSION_GC_2_5
            // 2.6: a global register variable's register (Wii's Registers_GetRegInfo)
            ObjRegInfo reginfo;
#endif
            // TODO: Pro7's member at its place after 2.6's reginfo; Alias reads it as global memory
            Boolean islocalstatic;
        } data;
        struct {
            union {
                TemplateFunction* templ;
                // CPrec_GetObjectPatch selects the inline body when TypeFunc flags & 0x400 is clear
                // and Q_INLINE is set; CInline_GenFunc stores it
                struct CI_FuncData* ifuncdata;
                // Intrinsics_SetupRuntimeObjects
                SInt32 intrinsicid;
            } u;
            DefArgCtorInfo* defargdata;
            HashNameNode* linkname;
            TemplFuncInstance* inst;
            // pointer analysis: the function's partial transfer functions
            struct PTFList* ptfList;
            // pointer analysis: the function's argument objects
            ObjectList* argList;
            // 2.6: the function's #pragma interrupt / __declspec(interrupt) (DeclInfo's)
            UInt8 interruptflags;
        } func;
        struct {
            VarInfo* info;
            SInt32 uid;
            // pointer analysis: the argument object an argument copy stands for
            struct Object* realObj;
        } var;
        struct {
            UInt8* data; // (Pro7: char *)
            SInt32 size;
            InlineXRef* xrefs;
        } ifunc;
        struct {
            Object* object;
            BClassList* member;
            SInt32 offset;
        } alias;
        SInt16 intrinsic;
        ENode* expr;
        // an absolute object's address (DABSOLUTE)
        UInt32 address;
    } u;
};
// An object instantiated from a class template's member: the member it came from.
struct ObjectTemplated {
    Object object;
    Object* parent;
};
struct VarInfo {
    struct Object* func;
    SInt32 usage;
    TStreamElement deftoken;
    SInt16 varnumber;
    Boolean noregister;
    Boolean used;
    UInt8 flags;
    UInt8 rclass;
    SInt16 reg;
    SInt16 regHi;
};
#pragma pack(pop)
// VarInfo.flags
enum {
    VarInfoFlag1 = 1, // (in the parameter area / has a stack slot)
    VarInfoFlag2 = 2, // (bound to a register)
    VarInfoFlag4 = 4, // (bound to a register pair)
    VarInfoFlag20 = 0x20, // 2.6: a CodeGen temporary in use (Pro7 marks it in the object's uid)
    VarInfoFlag40 = 0x40,
    VarInfoFlag80 = 0x80
};

#define OBJECT_TEMPL(obj) ((ObjectTemplated*)(obj))

// 2.6 also accepts DABSOLUTE (Pro7: DDATA only): every call site of Pro7's Inline_IsObjectData
// (IroVars, IROUseDef, IroPropagate, IroPointerAnalysis) compares both in 2.6
inline Boolean Inline_IsObjectData(Object* object) {
    return object->datatype == DDATA || object->datatype == DABSOLUTE;
}

#ifdef __cplusplus
}
#endif

#endif
