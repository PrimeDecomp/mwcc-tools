#include "compiler/CPrec.h"
#include "compiler/CError.h"
#include "compiler/CFunc.h"
#include "compiler/CInit.h"
#include "compiler/CInline.h"
#include "compiler/CMachine.h"
#include "compiler/CObjC.h"
#include "compiler/CompilerTools.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CScope.h"
#include "compiler/CSOM.h"
#include "compiler/CTemplateNew.h"
#include "compiler/enode.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/objc.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "compiler/som.h"
#include "compiler/templates.h"
#include "compiler/tokens.h"
#include "compiler/types.h"
#if VERSION == VERSION_WII_1_0RC1
#include "compiler/CABI.h"
#include "compiler/CClass.h"
#include "compiler/CDecl.h"
#include "compiler/CExpr.h"
#include "compiler/CIRStreamCode.h"
#include "compiler/CIRStreamTools.h"
#include "compiler/CMangler.h"
#include "compiler/CMiddleLayer.h"
#include "compiler/CPrecTransport.h"
#include "compiler/ObjGen_PPC_EABI.h"
#endif
#include "driver/CCompiler.h"
#include "driver/COSToolsCLT.h"
#include "driver/CWPlugins.h"
#include "driver/CWPluginsPrivate.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include "driver/oslib.h"
#include <stdio.h>
#include <string.h>

#pragma pack(push, 2)
typedef struct StaticData {
    struct StaticData* next;
    Object* object;
    void* buffer;
    OLinkList* links;
    SInt32 size;
} StaticData;

#if VERSION == VERSION_WII_1_0RC1
// Preserve the source PCH record layout by using the high bit of the byte count.
#define CPREC_STATIC_READ_ONLY 0x80000000u
#define CPREC_SOURCE_VERSION_WITH_RUNTIME_OBJECTS 5149
#define CPREC_SOURCE_VERSION 5148
#define CPREC_SOURCE_VERSION_WITHOUT_FILES 5147
#define CPREC_SOURCE_VERSION_WITHOUT_SAVED_UIDS 5146
enum { CPrec_AddedRuntimeBuiltinCount = 4 };
#elif VERSION < VERSION_GC_2_0
#define CPREC_SOURCE_VERSION 1047
#else
#define CPREC_SOURCE_VERSION 5145
#endif

typedef struct Header {
    UInt32 magic;
    UInt16 version;
    UInt16 x6;
    char target;
    Boolean check_header_flags;
    Boolean cplusplus;
    UInt32 xC;
    UInt32 x10;
    UInt32 x14;
    UInt32 x18;
    UInt32 x1C;
    UInt32 x20;
    UInt32 x24;
    UInt32 rawsize;
    UInt32 rawdatasize;
    UInt32 rawdataoffset;
    UInt32 compressedPatchCount;
    UInt32 compressedPatchSize;
    UInt32 compressedPatchOffset;
    UInt32 builtinPatchSize;
    UInt32 builtinPatchOffset;
    UInt32 tokenStreamPatchSize;
    UInt32 tokenStreamPatchOffset;
    UInt32 root_names;
    NameSpaceList* usings;
    TemplClass* ctempl_templates;
    CSOMStub* csom_stubs;
    StaticData* cprec_staticdata;
    UInt32 uniqueID;
    CallbackAction* callbackactions;
    Type* cobjc_type_class;
    Type* cobjc_type_id;
    Type* cobjc_type_sel;
    struct ObjCSelector** cobjc_selhashtable;
    struct BClassList* cobjc_classdefs;
    ObjCProtocol* cobjc_protocols;
    UInt32 cobjc_selrefcount;
    UInt32 cobjc_classrefcount;
    UInt32 cobjc_stringcount;
    InitExpr* init_expressions;
    CI_Action* cinline_tactionlist;
    TemplateFunction* ctempl_templatefuncs;
#if VERSION == VERSION_WII_1_0RC1
    ObjectList* tentativeData; // Previously reserved in the source PCH header.
#else
    UInt32 x9C;
#endif
    UInt32 xA0;
    UInt32 xA4;
    UInt32 xA8;
    UInt32 xAC;
    UInt32 xB0;
    UInt32 xB4;
    UInt32 xB8;
    UInt32 xBC;
    UInt32 xC0;
    UInt32 xC4;
#if VERSION == VERSION_WII_1_0RC1
    UInt32 includedFiles;
#else
    UInt32 xC8;
#endif
    UInt32 xCC;
    UInt32 xD0;
    UInt32 xD4;
#if VERSION == VERSION_WII_1_0RC1
    // These words were reserved in the legacy source PCH header.
    UInt32 backendSize;
    UInt32 backendOffset;
    UInt32 sectionPatchSize;
    UInt32 sectionPatchOffset;
#else
    UInt32 xD8;
    UInt32 xDC;
    UInt32 xE0;
    UInt32 xE4;
#endif
    HashNameNode* nametable[0x800];
    Macro* macrotable[0x800];
    NameSpaceName* root_nametable[0x400];
} Header;

typedef struct Patch {
    struct Patch* next;
    SInt32 offset;
} Patch;

typedef struct AddrPatch {
    struct AddrPatch* next;
    void* addr;
    void* value;
} AddrPatch;

typedef struct BuiltIn {
    void* target;
    SInt32 idx;
    Patch* patches;
} BuiltIn;

typedef struct TokenPatch {
    struct TokenPatch* next;
    TStreamElement* tokens;
    SInt32 count;
} TokenPatch;

typedef struct PointerHash {
    struct PointerHash* next;
    TypePointer* tptr;
    TypePointer* prec_tptr;
} PointerHash;

static short cprec_refnum;
static GList cprec_glist;
static Header* cprec_header;
static AddrPatch** cprec_addrhash;
static Patch* cprec_patch_list;
#if VERSION == VERSION_WII_1_0RC1
static Patch* cprec_sectionpatches;
#endif
static BuiltIn* cprec_builtin;
static PointerHash** cprec_pointerhash;
static StaticData* cprec_staticdata;
static TokenPatch* cprec_tokenpatches;
static void** cprec_builtin_array;
static int cprec_builtins;
static SInt32 cprec_offset;
static SInt32 cprec_zero_offset;
static void* cprec_buffer;
static void* cprec_rawbuffer;
static OSErr cprec_ioerror;
static Boolean cprec_dowrite;
static Boolean cprec_exportargnames;
static SInt32 flushmax;
char* precomp_target_str;
#pragma pack(pop)

struct Macro** macrohashtable;

static NameSpace* CPrec_GetNameSpacePatch(NameSpace* nspace);
static ObjEnumConst* CPrec_GetObjEnumConstPatch(ObjEnumConst* obj);
static Object* CPrec_GetObjectPatch(Object* obj);
static ObjBase* CPrec_GetObjBasePatch(ObjBase* obj);
static ObjMemberVar* CPrec_GetObjMemberVarPatch(ObjMemberVar* ivar);
static Type* CPrec_GetTypePatch(Type* type);
static ENode* CPrec_GetExpressionPatch(ENode* expr);
static ObjCMethod* CPrec_GetObjCMethodPatch(ObjCMethod* meth);
static ObjCProtocolList* CPrec_GetObjCProtocolListPatch(ObjCProtocolList* lst);
static TemplArg* CPrec_GetTemplateArgPatch(TemplArg* arg);
static NameSpaceObjectList* CPrec_GetNameSpaceObjectListPatch(NameSpaceObjectList* nsol);
static OSErr CPrec_FlushBufferCheck(void);

void SetupPrecompiler(Boolean isPrecompiling) {
    cprec_refnum = 0;
    cprec_glist.data = NULL;
    cprec_header = NULL;
    cprec_staticdata = NULL;
    cprec_ioerror = noErr;
}

void CleanupPrecompiler(void) {
    if (cprec_refnum) {
        COS_FileClose(cprec_refnum);
        cprec_refnum = 0;
    }

    if (cprec_glist.data) {
        FreeGList(&cprec_glist);
    }
}

static OLinkList* CPrec_OLinkListCopy(OLinkList* list) {
    OLinkList* copy;

    if (!list) {
        return NULL;
    }

    copy = galloc(sizeof(OLinkList));
    *copy = *list;
    copy->next = CPrec_OLinkListCopy(copy->next);
    return copy;
}

#if VERSION == VERSION_WII_1_0RC1
void PreComp_StaticData(
    Object* obj, const void* data, OLinkList* links, SInt32 size, Boolean readOnly
) {
#else
void PreComp_StaticData(Object* obj, const void* data, OLinkList* links, SInt32 size) {
#endif
    StaticData* entry;

    if (obj->sclass != TK_STATIC && !(obj->qual & (Q_IMPLICIT_WEAK | Q_WEAK))) {
        CError_Error(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

    entry = galloc(sizeof(StaticData));
    entry->object = obj;
    entry->size = size;
#if VERSION == VERSION_WII_1_0RC1
    if (readOnly) {
        entry->size = (SInt32)((UInt32)size | CPREC_STATIC_READ_ONLY);
    }
#endif

    entry->next = cprec_staticdata;
    cprec_staticdata = entry;

    if (data) {
#if VERSION == VERSION_WII_1_0RC1
        entry->buffer = galloc(size);
        memcpy(entry->buffer, data, size);
#else
        entry->buffer = galloc(obj->type->size);
        memcpy(entry->buffer, data, obj->type->size);
#endif
    } else {
        entry->buffer = NULL;
    }

    entry->links = CPrec_OLinkListCopy(links);
}

#if VERSION == VERSION_WII_1_0RC1
static void CPrec_DeclareStaticData(const StaticData* entry) {
    UInt32 size = (UInt32)entry->size;

    if (size & CPREC_STATIC_READ_ONLY) {
        CInit_DeclareReadOnlyData(
            entry->object, entry->buffer, entry->links, (SInt32)(size & ~CPREC_STATIC_READ_ONLY)
        );
    } else {
        CInit_DeclareData(entry->object, entry->buffer, entry->links, entry->size);
    }
}

static void CPrec_DeclareSourceStaticData(StaticData* entry) {
    StaticData* sequence = NULL;
    StaticData* next;

    for (; entry; entry = next) {
        next = entry->next;
        if (CMangler_IsLocalString(entry->object)) {
            // The source header stores definitions in reverse order.
            entry->next = sequence;
            sequence = entry;
        } else {
            CPrec_DeclareStaticData(entry);
        }
    }
    for (; sequence; sequence = sequence->next) {
        CPrec_DeclareStaticData(sequence);
    }
}
#endif

static void CPrec_InitAddressHashTable(void) {
    cprec_addrhash = lalloc(0x4000 * sizeof(AddrPatch*));
    memclrw(cprec_addrhash, 0x4000 * sizeof(AddrPatch*));
}

static void CPrec_InitPointerHashTable(void) {
    cprec_pointerhash = lalloc(0x400 * sizeof(PointerHash*));
    memclrw(cprec_pointerhash, 0x400 * sizeof(PointerHash*));
}

static int CPrec_AddressHashVal(void* addr) {
    union {
        SInt32 v;
        UInt8 b[4];
    } u;
    u.v = (SInt32)addr;
    return ((UInt32)addr + u.b[0] + u.b[1] + u.b[2] + u.b[3]) & 0x3FFF;
}

static AddrPatch* CPrec_FindAddrPatch(void* addr) {
    AddrPatch* scan;

    for (scan = cprec_addrhash[CPrec_AddressHashVal(addr)]; scan; scan = scan->next) {
        if (scan->addr == addr) {
            return scan;
        }
    }

    return NULL;
}

static AddrPatch* CPrec_NewAddrPatch(void* addr, void* value) {
    AddrPatch** loc;
    AddrPatch* patch;

    loc = cprec_addrhash + CPrec_AddressHashVal(addr);
    patch = lalloc(sizeof(AddrPatch));
    patch->addr = addr;
    patch->value = value;
    patch->next = *loc;
    *loc = patch;
    return patch;
}

static void CPrec_SetupBuiltInArray(void) {
    int filled;
    int total;
    Boolean counting;
    void** array;

#define REG_BUILTIN(a) \
    if (!counting) { \
        array[filled++] = (a); \
    } else { \
        total++; \
    }

    for (filled = total = 0, counting = TRUE;;) {
        REG_BUILTIN(cscope_root);
        REG_BUILTIN(&stvoid);
        REG_BUILTIN(&stbool);
        REG_BUILTIN(&stchar);
        REG_BUILTIN(&stsignedchar);
        REG_BUILTIN(&stunsignedchar);
        REG_BUILTIN(&stwchar);
        REG_BUILTIN(&stsignedshort);
        REG_BUILTIN(&stunsignedshort);
        REG_BUILTIN(&stsignedint);
        REG_BUILTIN(&stunsignedint);
        REG_BUILTIN(&stsignedlong);
        REG_BUILTIN(&stunsignedlong);
        REG_BUILTIN(&stsignedlonglong);
        REG_BUILTIN(&stunsignedlonglong);
        REG_BUILTIN(&stfloat);
        REG_BUILTIN(&stshortdouble);
        REG_BUILTIN(&stdouble);
        REG_BUILTIN(&stlongdouble);
        REG_BUILTIN(&elipsis);
        REG_BUILTIN(&oldstyle);
        REG_BUILTIN(&stillegal);
        REG_BUILTIN(&sttemplexpr);
        REG_BUILTIN(&stvoid);
        REG_BUILTIN(&void_ptr);
        REG_BUILTIN(&rt_func);
        REG_BUILTIN(&catchinfostruct);
        REG_BUILTIN(newh_func);
        REG_BUILTIN(delh_func);
        REG_BUILTIN(copy_func);
        REG_BUILTIN(clear_func);
        REG_BUILTIN(Rgtid_func);
        REG_BUILTIN(Rdync_func);
        REG_BUILTIN(rt_ptmf_cast);
        REG_BUILTIN(rt_ptmf_cmpr);
        REG_BUILTIN(rt_ptmf_test);
        REG_BUILTIN(rt_ptmf_call);
        REG_BUILTIN(rt_ptmf_scall);
        REG_BUILTIN(rt_ptmf_null);
        REG_BUILTIN(rt_som_glue1);
        REG_BUILTIN(rt_som_glue2);
        REG_BUILTIN(rt_som_glue3);
        REG_BUILTIN(rt_som_check);
        REG_BUILTIN(rt_som_new);
        REG_BUILTIN(rt_som_newcheck);
        REG_BUILTIN(rt_ptmf_call4);
        REG_BUILTIN(rt_ptmf_scall4);
        REG_BUILTIN(carr_func);
        REG_BUILTIN(cnar_func);
        REG_BUILTIN(darr_func);
        REG_BUILTIN(dnar_func);
        REG_BUILTIN(dnar3_func);
        REG_BUILTIN(Xgreg_func);
        REG_BUILTIN(Xthrw_func);
        REG_BUILTIN(Xicth_func);
        REG_BUILTIN(Xecth_func);
        REG_BUILTIN(Xunex_func);
        REG_BUILTIN(&stvectorunsignedchar);
        REG_BUILTIN(&stvectorsignedchar);
        REG_BUILTIN(&stvectorboolchar);
        REG_BUILTIN(&stvectorunsignedshort);
        REG_BUILTIN(&stvectorsignedshort);
        REG_BUILTIN(&stvectorboolshort);
        REG_BUILTIN(&stvectorunsignedlong);
        REG_BUILTIN(&stvectorsignedlong);
        REG_BUILTIN(&stvectorboollong);
        REG_BUILTIN(&stvectorfloat);
        REG_BUILTIN(&stvectorpixel);
        REG_BUILTIN(&stvec2x32float);
#if VERSION == VERSION_WII_1_0RC1
        for (int spe = 0; spe < SPEBuiltinTypeCount; spe++) {
            REG_BUILTIN(&stspevector[spe]);
        }
        REG_BUILTIN(rt_reg_class);
        REG_BUILTIN(rt_unreg_class);
        REG_BUILTIN(vla_alloc_func);
        REG_BUILTIN(vla_free_func);
#endif

        if (counting) {
            array = lalloc(sizeof(void*) * total);
            cprec_builtin_array = array;
            cprec_builtins = total;
            counting = FALSE;
        } else {
            return;
        }
    }
}

static void CPrec_SetupBuiltIn(void) {
    int x;

    CPrec_SetupBuiltInArray();
    cprec_builtin = lalloc(sizeof(BuiltIn) * cprec_builtins);
    memclrw(cprec_builtin, sizeof(BuiltIn) * cprec_builtins);

    for (x = 0; x < cprec_builtins; x++) {
        cprec_builtin[x].target = cprec_builtin_array[x];
        cprec_builtin[x].idx = ~x;
        CPrec_NewAddrPatch(cprec_builtin[x].target, (void*)cprec_builtin[x].idx);
    }
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_NewPointerPatch(void* src, void* ptr) {
    if (cprec_dowrite) {
        Patch* patch = lalloc(sizeof(Patch));
        patch->offset = (SInt32)src;
        CError_ASSERT(523, (patch->offset & 0x80000001) == 0);

        if ((SInt32)ptr < 0) {
            ptr = (void*)~((SInt32)ptr);
            CError_ASSERT(529, (SInt32)ptr < cprec_builtins);

            patch->next = cprec_builtin[(int)ptr].patches;
            cprec_builtin[(int)ptr].patches = patch;
            ptr = NULL;
        } else {
            patch->next = cprec_patch_list;
            cprec_patch_list = patch;
        }

        src = (void*)((char*)src - cprec_zero_offset);
        CError_ASSERT(541, (SInt32)src >= 0 && (SInt32)src <= cprec_glist.size);
        *((void**)(*cprec_glist.data + (SInt32)src)) = ptr;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_ExistingPointerPatch(void* src, void* ptr) {
    SInt32 key = (SInt32)ptr;
    if (cprec_dowrite) {
        AddrPatch* addrPatch;
        Patch* patch;

        CError_ASSERT(559, addrPatch = CPrec_FindAddrPatch((void*)key));

        patch = lalloc(sizeof(Patch));
        patch->offset = (SInt32)src;
        patch->next = cprec_patch_list;
        cprec_patch_list = patch;

        CError_ASSERT(564, (patch->offset & 0x80000001) == 0);

        src = (void*)((char*)src - cprec_zero_offset);
        CError_ASSERT(568, (SInt32)src >= 0 && (SInt32)src <= cprec_glist.size);
        *((void**)(*cprec_glist.data + (SInt32)src)) = addrPatch->value;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_NamePatch(void* src, HashNameNode* name) {
    name->id = 1;
    CPrec_ExistingPointerPatch(src, name);
}

static SInt32 CPrec_AppendAlign(void) {
    if (cprec_dowrite) {
        while (cprec_offset & 3) {
            AppendGListByte(&cprec_glist, 0);
            ++cprec_offset;
        }
    }

    return cprec_offset;
}

static UInt32 CPrec_AppendByte(UInt8 v) {
    if (cprec_dowrite) {
        AppendGListByte(&cprec_glist, v);
    }
    return cprec_offset++;
}

static UInt32 CPrec_AppendWord16(UInt16 v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListWord(&cprec_glist, v);
    }
    offset = cprec_offset;
    cprec_offset += 2;
    return offset;
}

static UInt32 CPrec_AppendWord32(UInt32 v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListLong(&cprec_glist, v);
    }
    offset = cprec_offset;
    cprec_offset += 4;
    return offset;
}

static UInt32 CPrec_AppendPointer(void* v) {
    UInt32 offset;
    if (cprec_dowrite) {
        AppendGListLong(&cprec_glist, (SInt32)v);
    }
    offset = cprec_offset;
    cprec_offset += 4;
    return offset;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static UInt32 CPrec_AppendPointerPatch(void* v) {
    AddrPatch* addrPatch;

    if (v) {
        CError_ASSERT(660, addrPatch = CPrec_FindAddrPatch(v));

        if (cprec_dowrite) {
            Patch* patch = lalloc(sizeof(Patch));
            patch->offset = cprec_offset;
            patch->next = cprec_patch_list;
            cprec_patch_list = patch;
            CError_ASSERT(667, (patch->offset & 0x80000001) == 0);
        }

        return CPrec_AppendPointer(addrPatch->value);
    } else {
        return CPrec_AppendPointer(NULL);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_AppendNamePatch(HashNameNode* name) {
    if (name) {
        CPrec_AppendPointerPatch(name);
        name->id = 1;
    }
}

static void CPrec_AppendString(const char* str) {
    int len = strlen(str) + 1;
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, str, len);
    }
    cprec_offset += len;
}

static void CPrec_AppendData(const void* data, int len) {
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, data, len);
    }
    cprec_offset += len;
}

static void CPrec_RawMemPatch(void* source, const void* data, int len) {
    SInt32 ptr = CPrec_AppendAlign();
    CPrec_AppendData(data, len);
    CPrec_NewPointerPatch(source, (void*)ptr);
}

static void CPrec_DumpNameTable(void) {
    HashNameNode* name;
    int i;
    HashNameNode* p;
    SInt32 next;

    if (cprec_dowrite) {
        i = 0;
        do {
            name = name_hash_nodes[i];
            while (name && name->id == 0) {
                name = name->next;
            }

            if (name) {
                p = (void*)CPrec_AppendAlign();
                cprec_header->nametable[i] = p;

                while (1) {
                    CPrec_NewAddrPatch(name, p);
                    CPrec_AppendPointer(NULL);
                    CPrec_AppendWord32(0);
                    CPrec_AppendWord16(name->hashval);
                    CPrec_AppendString(name->name);

                    name = name->next;
                    while (name && name->id == 0) {
                        name = name->next;
                    }

                    if (!name) {
                        break;
                    }

                    CPrec_AppendAlign();
                    next = cprec_offset;
                    CPrec_NewPointerPatch(&p->next, (void*)next);
                    p = (HashNameNode*)next;
                }
            }
        } while (++i < 0x800);
    } else {
        i = 0;
        do {
            if ((name = name_hash_nodes[i])) {
                p = (void*)CPrec_AppendAlign();
                while (1) {
                    CPrec_NewAddrPatch(name, p);
                    CPrec_AppendPointer(NULL);
                    CPrec_AppendWord32(0);
                    CPrec_AppendWord16(name->hashval);
                    CPrec_AppendString(name->name);

                    name = name->next;
                    if (!name) {
                        break;
                    }

                    CPrec_AppendAlign();
                    next = cprec_offset;
                    CPrec_NewPointerPatch(&p->next, (void*)next);
                    p = (HashNameNode*)next;
                }
            }
        } while (++i < 0x800);
    }
}

static void CPrec_DumpMacroTable(void) {
    Macro* macro;
    SInt32 j;
    SInt32 i;
    SInt32 next;
    SInt32 p;

    i = 0;
    do {
        for (macro = macrohashtable[i]; macro; macro = macro->next) {
            if (macro->text) {
                CPrec_NewAddrPatch(macro->text, (void*)cprec_offset);
                CPrec_AppendString(macro->text);
            }
        }
    } while (++i < 0x800);

    i = 0;
    do {
        if (!(macro = macrohashtable[i])) {
            continue;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        p = next;
        if (cprec_dowrite) {
            cprec_header->macrotable[i] = (Macro*)next;
        }
        for (;;) {
            CPrec_AppendPointer(NULL);
            CPrec_AppendNamePatch(macro->name);
            CPrec_AppendPointerPatch(macro->text);
            CPrec_AppendWord16(macro->nargs);
            CPrec_AppendByte(macro->is_special);
            CPrec_AppendByte(macro->flags);

            for (j = 1; j < (macro->nargs & 0x7FFF); j++) {
                CPrec_AppendNamePatch(macro->names[j - 1]);
            }

            macro = macro->next;
            if (!macro) {
                break;
            }
            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch((void*)p, (void*)next);
            p = next;
        }
    } while (++i < 0x800);
}

static BClassList* CPrec_GetClassAccessPatch(BClassList* path) {
    AddrPatch* addrPatch;
    BClassList* first;
    BClassList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(path))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(path, first = current = (void*)CPrec_AppendAlign());

    while (1) {
        CPrec_AppendData(path, sizeof(BClassList));
        CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(path->type));
        if (!path->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (BClassList*)next;
        path = path->next;
    }

    return first;
}

static int CPrec_PointerHash(TypePointer* type) {
    Type* target;
    int work;
    FuncArg* arg;

    work = type->qual;
    target = type->target;

restart:
    switch (target->type) {
        case TYPECLASS:
            if (TYPE_CLASS(target)->classname) {
                work += TYPE_CLASS(target)->classname->hashval;
            }
            break;
        case TYPEENUM:
            if (TYPE_ENUM(target)->enumname) {
                work += TYPE_ENUM(target)->enumname->hashval;
            }
            work += 3;
            target = TYPE_ENUM(target)->enumtype;
        case TYPEINT:
        case TYPEFLOAT:
            work += TYPE_INTEGRAL(target)->integral;
            break;
        case TYPEPOINTER:
            work += TYPE_POINTER(target)->qual;
            target = TYPE_POINTER(target)->target;
            goto restart;
        case TYPEARRAY:
            work += target->size;
            target = TYPE_POINTER(target)->target;
            goto restart;
        case TYPEFUNC:
            work += TYPE_FUNC(target)->functype->type;
            work += TYPE_FUNC(target)->functype->size;
            for (arg = TYPE_FUNC(target)->args; arg; arg = arg->next) {
                if (arg->type) {
                    work += arg->type->type;
                    work += arg->type->size;
                }
            }
            break;
        default:
            break;
    }

    work += target->type + target->size;
    return (work + (work >> 24) + (work >> 16) + (work >> 8)) & 0x3FF;
}

static TypePointer* CPrec_GetTypePointerPatch(TypePointer* tptr) {
    SInt32 p;
    int hash;
    PointerHash* phash;

    if (tptr->qual & Q_IS_OBJC_ID) {
        CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
        CPrec_AppendData(tptr, sizeof(TypeObjCID));
        if (TYPE_OBJC_ID(tptr)->protocols) {
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeObjCID, protocols)),
                CPrec_GetObjCProtocolListPatch(TYPE_OBJC_ID(tptr)->protocols)
            );
        }
    } else {
        if (!copts.faster_pch_gen && cprec_dowrite && tptr->size > 0) {
            hash = CPrec_PointerHash(tptr);
            for (phash = cprec_pointerhash[hash]; phash; phash = phash->next) {
                TypePointer* scan1 = tptr;
                TypePointer* scan2 = phash->tptr;
                do {
                    if (scan1->type != scan2->type ||
                        scan1->size != scan2->size ||
                        scan1->qual != scan2->qual) {
                        break;
                    }
                    scan1 = TYPE_POINTER(TPTR_TARGET(scan1));
                    scan2 = TYPE_POINTER(TPTR_TARGET(scan2));
                    if (scan1->type != TYPEPOINTER || scan1->type != TYPEARRAY) {
                        if (scan1 == scan2) {
                            return phash->prec_tptr;
                        }
                        break;
                    }
                } while (1);
            }
            CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
            phash = lalloc(sizeof(PointerHash));
            phash->tptr = tptr;
            phash->prec_tptr = (TypePointer*)p;
            phash->next = cprec_pointerhash[hash];
            cprec_pointerhash[hash] = phash;
        } else {
            CPrec_NewAddrPatch(tptr, (void*)(p = CPrec_AppendAlign()));
        }
        CPrec_AppendData(tptr, sizeof(TypePointer));
    }
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypePointer, target)), CPrec_GetTypePatch(tptr->target)
    );
    return (TypePointer*)p;
}

static TypeEnum* CPrec_GetTypeEnumPatch(TypeEnum* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeEnum));

    if (type->nspace) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeEnum, nspace)), CPrec_GetNameSpacePatch(type->nspace)
        );
    }
    if (type->enumlist) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeEnum, enumlist)), CPrec_GetObjEnumConstPatch(type->enumlist)
        );
    }

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeEnum, enumtype)), CPrec_GetTypePatch(type->enumtype)
    );
    if (type->enumname) {
        CPrec_NamePatch((void*)(p + offsetof(TypeEnum, enumname)), type->enumname);
    }

    return (TypeEnum*)p;
}

static TypeBitfield* CPrec_GetTypeBitfieldPatch(TypeBitfield* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    if (cprec_dowrite) {
        AppendGListData(&cprec_glist, type, sizeof(TypeBitfield));
    }
    cprec_offset += sizeof(TypeBitfield);
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeBitfield, bitfieldtype)), CPrec_GetTypePatch(type->bitfieldtype)
    );

    return (TypeBitfield*)p;
}

static TypeStruct* CPrec_GetTypeStructPatch(TypeStruct* tstruct) {
    StructMember* member;
    TypeStruct* p;
    SInt32 next;
    StructMember* current;

    CPrec_NewAddrPatch(tstruct, current = (void*)(p = (void*)CPrec_AppendAlign()));
    CPrec_AppendData(tstruct, sizeof(TypeStruct));

    if (tstruct->name) {
        CPrec_NamePatch(&p->name, tstruct->name);
    }

    if ((member = tstruct->members)) {
        current = (void*)CPrec_AppendAlign();
        CPrec_NewPointerPatch(&p->members, current);

        while (1) {
            CPrec_AppendData(member, sizeof(StructMember));
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(member->type));
            CPrec_NamePatch(&current->name, member->name);

            if (!member->next) {
                break;
            }

            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch(&current->next, (void*)next);
            current = (StructMember*)next;

            member = member->next;
        }
    }

    return p;
}

static ExceptSpecList* CPrec_GetExceptSpecPatch(ExceptSpecList* exspec) {
    ExceptSpecList* first;
    ExceptSpecList* current;
    SInt32 next;

    first = current = (void*)CPrec_AppendAlign();
    while (exspec) {
        CPrec_AppendData(exspec, sizeof(ExceptSpecList));
        if (exspec->type) {
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(exspec->type));
        }

        if (!exspec->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ExceptSpecList*)next;
        exspec = exspec->next;
    }

    return first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static FuncArg* CPrec_GetArgListPatch(FuncArg* lst, Boolean includeNames) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    first = current = CPrec_AppendAlign();
    while (1) {
        if (!includeNames) {
            lst->name = NULL;
        }
        CPrec_AppendData(lst, sizeof(FuncArg));
        if (includeNames && lst->name) {
            CPrec_NamePatch((void*)(current + offsetof(FuncArg, name)), lst->name);
        }

        if (lst->dexpr) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(FuncArg, dexpr)), CPrec_GetExpressionPatch(lst->dexpr)
            );
        }
        if (lst->type) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(FuncArg, type)), CPrec_GetTypePatch(lst->type)
            );
        } else {
            CError_FATAL(1183);
        }

        if (!lst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(lst->next))) {
            CPrec_NewPointerPatch((void*)(current + offsetof(FuncArg, next)), addrPatch->value);
            break;
        }

        next = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(current + offsetof(FuncArg, next)), (void*)next);
        current = next;
        lst = lst->next;
        CPrec_NewAddrPatch(lst, (void*)current);
    }

    return (FuncArg*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TypeFunc* CPrec_GetTypeFuncPatch(TypeFunc* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, (type->flags & FUNC_METHOD) ? sizeof(TypeMemberFunc) : sizeof(TypeFunc));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeFunc, functype)), CPrec_GetTypePatch(type->functype)
    );
    if (type->args) {
        Boolean includeNames = (type->flags & FUNC_IS_TEMPL_ANY) != 0;
#if VERSION == VERSION_WII_1_0RC1
        if ((type->flags & FUNC_METHOD) && IS_TEMPL_CLASS(TYPE(TYPE_METHOD(type)->theclass))) {
            includeNames = TRUE;
        }
#endif
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeFunc, args)), CPrec_GetArgListPatch(type->args, includeNames)
        );
    }
    if (type->exspecs) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeFunc, exspecs)), CPrec_GetExceptSpecPatch(type->exspecs)
        );
    }
    if (type->flags & FUNC_METHOD) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(TypeMemberFunc, theclass)),
            CPrec_GetTypePatch((Type*)TYPE_METHOD(type)->theclass)
        );
    }

    return (TypeFunc*)p;
}

static TypeMemberPointer* CPrec_GetTypeMemberPointerPatch(TypeMemberPointer* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeMemberPointer));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeMemberPointer, ty1)), CPrec_GetTypePatch(type->ty1)
    );
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(TypeMemberPointer, ty2)), CPrec_GetTypePatch(type->ty2)
    );

    return (TypeMemberPointer*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TypeTemplDep* CPrec_GetTypeTemplDepPatch(TypeTemplDep* type) {
    SInt32 p;

    CPrec_NewAddrPatch(type, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(type, sizeof(TypeTemplDep));

    switch (type->dtype) {
        case TEMPLDEP_ARGUMENT:
            break;
        case TEMPLDEP_QUALNAME:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qual.type)),
                CPrec_GetTypeTemplDepPatch(type->u.qual.type)
            );
            CPrec_NamePatch((void*)(p + offsetof(TypeTemplDep, u.qual.name)), type->u.qual.name);
            break;
        case TEMPLDEP_TEMPLATE:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.templ.templ)),
                CPrec_GetTypePatch((Type*)type->u.templ.templ)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.templ.args)),
                CPrec_GetTemplateArgPatch(type->u.templ.args)
            );
            break;
        case TEMPLDEP_ARRAY:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.array.type)),
                CPrec_GetTypePatch(type->u.array.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.array.index)),
                CPrec_GetExpressionPatch(type->u.array.index)
            );
            break;
        case TEMPLDEP_QUALTEMPL:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qualtempl.type)),
                CPrec_GetTypeTemplDepPatch(type->u.qualtempl.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.qualtempl.args)),
                CPrec_GetTemplateArgPatch(type->u.qualtempl.args)
            );
            break;
        case TEMPLDEP_BITFIELD:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.bitfield.type)),
                CPrec_GetTypePatch(type->u.bitfield.type)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(TypeTemplDep, u.bitfield.size)),
                CPrec_GetExpressionPatch(type->u.bitfield.size)
            );
            break;
        default:
            CError_FATAL(1311);
    }

    return (TypeTemplDep*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ClassList* CPrec_GetClassListPatch(ClassList* cl) {
    AddrPatch* addrPatch;
    ClassList* first;
    ClassList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(cl))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(cl, first = current = (void*)CPrec_AppendAlign());

    do {
        CPrec_AppendData(cl, sizeof(ClassList));
        CPrec_NewPointerPatch(&current->base, CPrec_GetTypePatch((Type*)cl->base));

        if (!cl->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(cl->next))) {
            CPrec_NewPointerPatch(&current->next, addrPatch->value);
            break;
        } else {
            CPrec_AppendAlign();
            next = cprec_offset;
            CPrec_NewPointerPatch(&current->next, (void*)next);
            current = (ClassList*)next;
            cl = cl->next;
        }
    } while (1);

    return first;
}

static VClassList* CPrec_GetVClassListPatch(VClassList* vcl) {
    VClassList* first;
    SInt32 next;
    VClassList* current;

    first = current = (void*)CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(vcl, sizeof(VClassList));
        CPrec_NewPointerPatch(&current->base, CPrec_GetTypePatch((Type*)vcl->base));
        if (!vcl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (VClassList*)next;
        vcl = vcl->next;
    }
    return first;
}

static ClassFriend* CPrec_GetClassFriendPatch(ClassFriend* cf) {
    ClassFriend* first;
    ClassFriend* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(cf, sizeof(ClassFriend));
        if (cf->isclass) {
            CPrec_NewPointerPatch(&current->u.theclass, CPrec_GetTypePatch(TYPE(cf->u.theclass)));
        } else {
            CPrec_NewPointerPatch(&current->u.obj, CPrec_GetObjectPatch(cf->u.obj));
        }

        if (!cf->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ClassFriend*)next;
        cf = cf->next;
    } while (1);

    return first;
}

static BClassList* CPrec_GetBClassListPatch(BClassList* bcl) {
    BClassList* first;
    SInt32 next;
    BClassList* current;
    first = current = (void*)CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(bcl, sizeof(BClassList));
        CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch((Type*)bcl->type));

        if (!bcl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (BClassList*)next;
        bcl = bcl->next;
    }

    return first;
}

static VTable* CPrec_GetVTablePatch(VTable* vtbl) {
    VTable* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(vtbl, sizeof(VTable));

    if (vtbl->object) {
        CPrec_NewPointerPatch(&p->object, CPrec_GetObjectPatch(vtbl->object));
    }
    if (vtbl->owner) {
        CPrec_NewPointerPatch(&p->owner, CPrec_GetTypePatch((Type*)vtbl->owner));
    }

    return p;
}

static SOMReleaseOrder* CPrec_GetSOMReleaseOrderPatch(SOMReleaseOrder* sro) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(sro, sizeof(SOMReleaseOrder));
        CPrec_NamePatch((void*)(current + offsetof(SOMReleaseOrder, name)), sro->name);
        if (!sro->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(SOMReleaseOrder, next)), (void*)next);
        current = next;
        sro = sro->next;
    }

    return (SOMReleaseOrder*)first;
}

static SOMInfo* CPrec_GetSOMInfoPatch(SOMInfo* som) {
    SOMInfo* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(som, sizeof(SOMInfo));

    if (som->metaclass) {
        CPrec_NewPointerPatch(&p->metaclass, CPrec_GetTypePatch((Type*)som->metaclass));
    }
    if (som->classdataobject) {
        CPrec_NewPointerPatch(&p->classdataobject, CPrec_GetObjectPatch(som->classdataobject));
    }
    if (som->order) {
        CPrec_NewPointerPatch(&p->order, CPrec_GetSOMReleaseOrderPatch(som->order));
    }

    return p;
}

static ObjCMethodArg* CPrec_GetObjCMethodArgPatch(ObjCMethodArg* arg) {
    ObjCMethodArg* first;
    ObjCMethodArg* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(arg, sizeof(ObjCMethod));
        if (arg->selector) {
            CPrec_NamePatch(&current->selector, arg->selector);
        }
        if (arg->name) {
            CPrec_NamePatch(&current->name, arg->name);
        }
        if (arg->type) {
            CPrec_NewPointerPatch(&current->type, CPrec_GetTypePatch(arg->type));
        }

        if (!arg->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ObjCMethodArg*)next;
        arg = arg->next;
    } while (1);

    return first;
}

static ObjCMethodList* CPrec_GetObjCMethodListPatch(ObjCMethodList* lst) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(lst, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(lst, sizeof(ObjCMethodList));
        if (lst->method) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethodList, method)),
                CPrec_GetObjCMethodPatch(lst->method)
            );
        }

        if (!lst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(lst->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethodList, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethodList, next)), (void*)next);
            current = next;
            lst = lst->next;
            CPrec_NewAddrPatch(lst, (void*)current);
        }
    } while (1);

    return (ObjCMethodList*)first;
}

static ObjCSelector* CPrec_GetObjCSelectorPatch(ObjCSelector* sel) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(sel))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(sel, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(sel, sizeof(ObjCSelector));
        if (sel->selobject) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, selobject)),
                CPrec_GetObjectPatch(sel->selobject)
            );
        }
        CPrec_NamePatch((void*)(current + offsetof(ObjCSelector, name)), sel->name);
        if (sel->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, methods)),
                CPrec_GetObjCMethodListPatch(sel->methods)
            );
        }

        if (!sel->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(sel->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCSelector, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCSelector, next)), (void*)next);
            current = next;
            sel = sel->next;
            CPrec_NewAddrPatch(sel, (void*)current);
        }
    } while (1);

    return (ObjCSelector*)first;
}

static ObjCMethod* CPrec_GetObjCMethodPatch(ObjCMethod* meth) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(meth))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(meth, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(meth, sizeof(ObjCMethod));
        if (meth->object) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, object)), CPrec_GetObjectPatch(meth->object)
            );
        }
        if (meth->functype) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, functype)),
                CPrec_GetTypePatch((Type*)meth->functype)
            );
        }
        if (meth->selector) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, selector)),
                CPrec_GetObjCSelectorPatch(meth->selector)
            );
        }
        if (meth->return_type) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, return_type)),
                CPrec_GetTypePatch(meth->return_type)
            );
        }
        if (meth->selector_args) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCMethod, selector_args)),
                CPrec_GetObjCMethodArgPatch(meth->selector_args)
            );
        }

        if (!meth->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(meth->next))) {
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethod, next)), addrPatch->value);
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCMethod, next)), (void*)next);
            current = next;
            meth = meth->next;
            CPrec_NewAddrPatch(meth, (void*)current);
        }
    } while (1);

    return (ObjCMethod*)first;
}

static ObjCProtocol* CPrec_GetObjCProtocolPatch(ObjCProtocol* prot) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(prot))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(prot, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(prot, sizeof(ObjCProtocol));
        CPrec_NamePatch((void*)(current + offsetof(ObjCProtocol, name)), prot->name);
        if (prot->protocols) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, protocols)),
                CPrec_GetObjCProtocolListPatch(prot->protocols)
            );
        }
        if (prot->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, methods)),
                CPrec_GetObjCMethodPatch(prot->methods)
            );
        }
        if (prot->object) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, object)),
                CPrec_GetObjectPatch(prot->object)
            );
        }

        if (!prot->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(prot->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCProtocol, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCProtocol, next)), (void*)next);
            current = next;
            prot = prot->next;
            CPrec_NewAddrPatch(prot, (void*)current);
        }
    } while (1);

    return (ObjCProtocol*)first;
}

static ObjCProtocolList* CPrec_GetObjCProtocolListPatch(ObjCProtocolList* lst) {
    AddrPatch* addrPatch;
    ObjCProtocolList* first;
    ObjCProtocolList* current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(lst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(lst, first = current = (void*)CPrec_AppendAlign());

    do {
        CPrec_AppendData(lst, sizeof(ObjCProtocolList));
        CPrec_NewPointerPatch(&current->protocol, CPrec_GetObjCProtocolPatch(lst->protocol));

        if (!lst->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (ObjCProtocolList*)next;
        lst = lst->next;
    } while (1);

    return first;
}

static ObjCCategory* CPrec_GetObjCCategoryPatch(ObjCCategory* cat) {
    AddrPatch* addrPatch;
    SInt32 current;
    SInt32 first;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(cat))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(cat, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(cat, sizeof(ObjCCategory));
        CPrec_NamePatch((void*)(current + offsetof(ObjCCategory, name)), cat->name);
        if (cat->protocols) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, protocols)),
                CPrec_GetObjCProtocolListPatch(cat->protocols)
            );
        }
        if (cat->methods) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, methods)),
                CPrec_GetObjCMethodPatch(cat->methods)
            );
        }

        if (!cat->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(cat->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjCCategory, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjCCategory, next)), (void*)next);
            current = next;
            cat = cat->next;
            CPrec_NewAddrPatch(cat, (void*)current);
        }
    } while (1);

    return (ObjCCategory*)first;
}

static ObjCInfo* CPrec_GetObjCInfoPatch(ObjCInfo* info) {
    ObjCInfo* p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(info, sizeof(ObjCInfo));

    if (info->classobject) {
        CPrec_NewPointerPatch(&p->classobject, CPrec_GetObjectPatch(info->classobject));
    }
    if (info->metaobject) {
        CPrec_NewPointerPatch(&p->metaobject, CPrec_GetObjectPatch(info->metaobject));
    }
    if (info->classrefobj) {
        CPrec_NewPointerPatch(&p->classrefobj, CPrec_GetObjectPatch(info->classrefobj));
    }
    if (info->methods) {
        CPrec_NewPointerPatch(&p->methods, CPrec_GetObjCMethodPatch(info->methods));
    }
    if (info->protocols) {
        CPrec_NewPointerPatch(&p->protocols, CPrec_GetObjCProtocolListPatch(info->protocols));
    }
    if (info->categories) {
        CPrec_NewPointerPatch(&p->categories, CPrec_GetObjCCategoryPatch(info->categories));
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplArg* CPrec_GetTemplateArgPatch(TemplArg* arg) {
    TemplArg* first;
    TemplArg* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(arg, sizeof(TemplArg));
        switch (arg->pid.type) {
            case TPT_TYPE:
                if (arg->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        &current->data.typeparam.type, CPrec_GetTypePatch(arg->data.typeparam.type)
                    );
                }
                break;
            case TPT_NONTYPE:
                if (arg->data.paramdecl.expr) {
                    CPrec_NewPointerPatch(
                        &current->data.paramdecl.expr,
                        CPrec_GetExpressionPatch(arg->data.paramdecl.expr)
                    );
                }
                break;
            case TPT_TEMPLATE:
                if (arg->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        &current->data.typeparam.type, CPrec_GetTypePatch(arg->data.typeparam.type)
                    );
                }
                break;
            default:
                CError_FATAL(1895);
        }

        if (!arg->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplArg*)next;
        arg = arg->next;
    } while (1);

    return first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplParam* CPrec_GetTemplateParamPatch(TemplParam* param) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    do {
        CPrec_AppendData(param, sizeof(TemplParam));
        if (param->name) {
            CPrec_NamePatch((void*)(current + offsetof(TemplParam, name)), param->name);
        }

        switch (param->pid.type) {
            case TPT_TYPE:
                if (param->data.typeparam.type) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.typeparam.type)),
                        CPrec_GetTypePatch(param->data.typeparam.type)
                    );
                }
                break;
            case TPT_NONTYPE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplParam, data.paramdecl.type)),
                    CPrec_GetTypePatch(param->data.paramdecl.type)
                );
                if (param->data.paramdecl.defaultarg) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.paramdecl.defaultarg)),
                        CPrec_GetExpressionPatch(param->data.paramdecl.defaultarg)
                    );
                }
                break;
            case TPT_TEMPLATE:
                if (param->data.templparam.plist) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplParam, data.templparam.plist)),
                        CPrec_GetTemplateParamPatch(param->data.templparam.plist)
                    );
                }
                CError_ASSERT(1969, !param->data.templparam.defaultarg);
                break;
            default:
                CError_FATAL(1974);
        }

        if (!param->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(TemplParam, next)), (void*)next);
        current = next;
        param = param->next;
    } while (1);

    return (TemplParam*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TStreamElement* CPrec_GetTStreamPatch(TStreamElement* tokens, SInt32 count) {
    TStreamElement elem;
    SInt32 first;
    SInt32 current;
    TStreamElement* scan;
    SInt32 x;

    scan = tokens;
    x = 0;
    while (x < count) {
        elem = *scan;
        memclrw(scan, sizeof(TStreamElement));

        switch (scan->tokentype = elem.tokentype) {
            case TK_IDENTIFIER:
                scan->data.tkidentifier = elem.data.tkidentifier;
                break;
            case TK_INTCONST:
                scan->subtype = elem.subtype;
                scan->data.tkintconst = elem.data.tkintconst;
                break;
            case TK_FLOATCONST:
                scan->subtype = elem.subtype;
                scan->data.tkfloatconst = elem.data.tkfloatconst;
                break;
            case TK_STRING:
            case TK_STRING_WIDE:
                scan->subtype = elem.subtype;
                scan->data.tkstring = elem.data.tkstring;
                break;
        }
        x++;
        scan++;
    }

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(tokens, count * sizeof(TStreamElement));

    if (cprec_dowrite) {
        TokenPatch* tp = lalloc(sizeof(TokenPatch));
        tp->tokens = (TStreamElement*)current;
        tp->count = count;
        tp->next = cprec_tokenpatches;
        cprec_tokenpatches = tp;
    }

    scan = tokens;
    x = 0;
    while (x < count) {
        switch (scan->tokentype) {
            case TK_IDENTIFIER:
                CPrec_NamePatch(
                    (void*)(current + offsetof(TStreamElement, data.tkidentifier)),
                    scan->data.tkidentifier
                );
                break;
            case TK_INTCONST:
            case TK_FLOATCONST:
                break;
            case TK_STRING:
            case TK_STRING_WIDE:
                CPrec_RawMemPatch(
                    (void*)(current + offsetof(TStreamElement, data.tkstring.data)),
                    scan->data.tkstring.data,
                    scan->data.tkstring.size
                );
                break;
            case TK_EOL:
                break;
            default:
                if (scan->tokentype < 0) {
                    CError_FATAL(2079);
                }
        }
        x++;
        scan++;
        current += sizeof(TStreamElement);
    }

    return (TStreamElement*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TemplFuncInstance* CPrec_GetTemplFuncInstancePatch(TemplFuncInstance* inst) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(inst))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(inst, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(inst, sizeof(TemplFuncInstance));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplFuncInstance, object)),
            CPrec_GetObjectPatch(inst->object)
        );
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplFuncInstance, args)),
            CPrec_GetTemplateArgPatch(inst->args)
        );

        if (!inst->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(inst->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplFuncInstance, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplFuncInstance, next)), (void*)next
            );
            current = next;
            inst = inst->next;
            CPrec_NewAddrPatch(inst, (void*)current);
        }
    } while (1);

    return (TemplFuncInstance*)first;
}

static TemplateMember* CPrec_GetTemplateMemberPatch(TemplateMember* memb) {
    TemplateMember* first;
    SInt32 next;
    TemplateMember* current;
    first = current = (void*)CPrec_AppendAlign();

    do {
        memclrw(&memb->fileoffset, sizeof(FileOffsetInfo));
        memb->srcfile = NULL;
        memb->startoffset = 0;
        memb->endoffset = 0;

        CPrec_AppendData(memb, sizeof(TemplateMember));
        if (memb->params) {
            CPrec_NewPointerPatch(&current->params, CPrec_GetTemplateParamPatch(memb->params));
        }
        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(memb->object));
        if (memb->stream.firsttoken) {
            CPrec_NewPointerPatch(
                &current->stream.firsttoken,
                CPrec_GetTStreamPatch(memb->stream.firsttoken, memb->stream.tokens)
            );
        }

        if (!memb->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplateMember*)next;
        memb = memb->next;
    } while (1);

    return first;
}

static TemplPartialSpec* CPrec_GetTemplPartialSpecPatch(TemplPartialSpec* pspec) {
    TemplPartialSpec* first;
    TemplPartialSpec* current;
    SInt32 next;
    first = current = (void*)CPrec_AppendAlign();

    do {
        CPrec_AppendData(pspec, sizeof(TemplPartialSpec));
        if (pspec->templ) {
            CPrec_NewPointerPatch(&current->templ, CPrec_GetTypePatch((Type*)pspec->templ));
        }
        if (pspec->args) {
            CPrec_NewPointerPatch(&current->args, CPrec_GetTemplateArgPatch(pspec->args));
        }

        if (!pspec->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (TemplPartialSpec*)next;
        pspec = pspec->next;
    } while (1);

    return first;
}

static TemplateFriend* CPrec_GetTemplateFriendPatch(TemplateFriend* frnd) {
    TemplateFriend* p;

    memclrw(&frnd->fileoffset, sizeof(FileOffsetInfo));
    p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(frnd, sizeof(TemplateFriend));

    if (frnd->decl.thetype) {
        CPrec_NewPointerPatch(&p->decl.thetype, CPrec_GetTypePatch(frnd->decl.thetype));
    }
    if (frnd->decl.nspace) {
        CPrec_NewPointerPatch(&p->decl.nspace, CPrec_GetNameSpacePatch(frnd->decl.nspace));
    }
    if (frnd->decl.name) {
        CPrec_NamePatch(&p->decl.name, frnd->decl.name);
    }
    if (frnd->decl.expltargs) {
        CPrec_NewPointerPatch(&p->decl.expltargs, CPrec_GetTemplateArgPatch(frnd->decl.expltargs));
    }
    if (frnd->stream.firsttoken) {
        CPrec_NewPointerPatch(
            &p->stream.firsttoken,
            CPrec_GetTStreamPatch(frnd->stream.firsttoken, frnd->stream.tokens)
        );
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static TemplateAction* CPrec_GetTemplateActionPatch(TemplateAction* act) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        memclrw(&act->source_ref, sizeof(act->source_ref));
        CPrec_AppendData(act, sizeof(*act));
        switch (act->type) {
            case TAT_NESTEDCLASS:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.tclasstype)),
                    CPrec_GetTypePatch(TYPE(act->u.tclasstype))
                );
                break;
            case TAT_ENUMTYPE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.enumtype)),
                    CPrec_GetTypePatch(TYPE(act->u.enumtype))
                );
                break;
            case TAT_FRIEND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.tfriend)),
                    CPrec_GetTemplateFriendPatch(act->u.tfriend)
                );
                break;
            case TAT_ENUMERATOR:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.enumerator.objenumconst)),
                    CPrec_GetObjEnumConstPatch(act->u.enumerator.objenumconst)
                );
                if (act->u.enumerator.initexpr) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplateAction, u.enumerator.initexpr)),
                        CPrec_GetExpressionPatch(act->u.enumerator.initexpr)
                    );
                }
                break;
            case TAT_BASE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.base.type)),
                    CPrec_GetTypePatch(act->u.base.type)
                );
                if (act->u.base.insert_after) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(TemplateAction, u.base.insert_after)),
                        CPrec_GetClassListPatch(act->u.base.insert_after)
                    );
                }
                break;
            case TAT_OBJECTINIT:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.objectinit.object)),
                    CPrec_GetObjectPatch(act->u.objectinit.object)
                );
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.objectinit.initexpr)),
                    CPrec_GetExpressionPatch(act->u.objectinit.initexpr)
                );
                break;
            case TAT_USINGDECL:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.usingdecl.type)),
                    CPrec_GetTypeTemplDepPatch(act->u.usingdecl.type)
                );
                break;
            case TAT_OBJECTDEF:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplateAction, u.refobj)),
                    CPrec_GetObjBasePatch(act->u.refobj)
                );
                break;
            default:
                CError_FATAL(2426);
                break;
        }
        if (act->next == NULL) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)current, (void*)next);
        current = next;
        act = act->next;
    }
    return (TemplateAction*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static TemplateFunction* CPrec_GetTemplateFunctionPatch(TemplateFunction* tf) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(tf))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(tf, (void*)(first = current = CPrec_AppendAlign()));

    do {
        memclrw(&tf->deftoken, sizeof(TStreamElement));
        tf->srcfile = NULL;
        tf->startoffset = 0;
        tf->endoffset = 0;

        CPrec_AppendData(tf, sizeof(TemplateFunction));
        if (tf->original) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, original)),
                CPrec_GetTemplateFunctionPatch(tf->original)
            );
        }
        CPrec_NamePatch((void*)(current + offsetof(TemplateFunction, name)), tf->name);
        if (tf->params) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, params)),
                CPrec_GetTemplateParamPatch(tf->params)
            );
        }
        if (tf->stream.firsttoken) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, stream.firsttoken)),
                CPrec_GetTStreamPatch(tf->stream.firsttoken, tf->stream.tokens)
            );
        }
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(TemplateFunction, tfunc)), CPrec_GetObjectPatch(tf->tfunc)
        );
        if (tf->instances) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, instances)),
                CPrec_GetTemplFuncInstancePatch(tf->instances)
            );
        }

        if (!tf->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(tf->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplateFunction, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(TemplateFunction, next)), (void*)next);
            current = next;
            tf = tf->next;
            CPrec_NewAddrPatch(tf, (void*)current);
        }
    } while (1);

    return (TemplateFunction*)first;
}

static TypeClass* CPrec_GetTypeClassPatch(TypeClass* tclass) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    SInt32 nextInst;
    AddrPatch* addrPatch;
    Boolean hasNextTempl;
    Boolean hasNextTemplInst;
    HashNameNode* name;

    first = current = CPrec_AppendAlign();

    for (;;) {
        hasNextTempl = hasNextTemplInst = FALSE;
        CPrec_NewAddrPatch(tclass, (void*)current);

        if (tclass->flags & CLASS_IS_TEMPL) {
            CPrec_AppendData(tclass, sizeof(TemplClass));
            if (TEMPL_CLASS(tclass)->next) {
                hasNextTempl = TRUE;
            }
            if (TEMPL_CLASS(tclass)->templ_parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, templ_parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->templ_parent)
                );
            }
            if (TEMPL_CLASS(tclass)->inst_parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, inst_parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->inst_parent)
                );
            }
            if (TEMPL_CLASS(tclass)->templ__params) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, templ__params)),
                    CPrec_GetTemplateParamPatch(TEMPL_CLASS(tclass)->templ__params)
                );
            }
            if (TEMPL_CLASS(tclass)->members) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, members)),
                    CPrec_GetTemplateMemberPatch(TEMPL_CLASS(tclass)->members)
                );
            }
            if (TEMPL_CLASS(tclass)->instances) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, instances)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->instances)
                );
            }
            if (TEMPL_CLASS(tclass)->pspec_owner) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, pspec_owner)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS(tclass)->pspec_owner)
                );
            }
            if (TEMPL_CLASS(tclass)->pspecs) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, pspecs)),
                    CPrec_GetTemplPartialSpecPatch(TEMPL_CLASS(tclass)->pspecs)
                );
            }
            if (TEMPL_CLASS(tclass)->actions) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, actions)),
                    CPrec_GetTemplateActionPatch(TEMPL_CLASS(tclass)->actions)
                );
            }
        } else if (tclass->flags & CLASS_IS_TEMPL_INST) {
            CPrec_AppendData(tclass, sizeof(TemplClassInst));
            if (TEMPL_CLASS_INST(tclass)->next) {
                hasNextTemplInst = TRUE;
            }
            if (TEMPL_CLASS_INST(tclass)->parent) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, parent)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS_INST(tclass)->parent)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->templ) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, templ)),
                    CPrec_GetTypePatch((Type*)TEMPL_CLASS_INST(tclass)->templ)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->inst_args) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, inst_args)),
                    CPrec_GetTemplateArgPatch(TEMPL_CLASS_INST(tclass)->inst_args)
                );
            }
            if (TEMPL_CLASS_INST(tclass)->oargs) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, oargs)),
                    CPrec_GetTemplateArgPatch(TEMPL_CLASS_INST(tclass)->oargs)
                );
            }
        } else {
            CPrec_AppendData(tclass, sizeof(TypeClass));
        }

        if (tclass->nspace) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, nspace)),
                CPrec_GetNameSpacePatch(tclass->nspace)
            );
        }
        if ((name = tclass->classname)) {
            name->id = 1;
            CPrec_ExistingPointerPatch((void*)(current + offsetof(TypeClass, classname)), name);
        }
        if (tclass->bases) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, bases)),
                CPrec_GetClassListPatch(tclass->bases)
            );
        }
        if (tclass->vbases) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, vbases)),
                CPrec_GetVClassListPatch(tclass->vbases)
            );
        }
        if (tclass->ivars) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, ivars)),
                CPrec_GetObjMemberVarPatch(tclass->ivars)
            );
        }
        if (tclass->friends) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, friends)),
                CPrec_GetClassFriendPatch(tclass->friends)
            );
        }
        if (tclass->vtable) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, vtable)), CPrec_GetVTablePatch(tclass->vtable)
            );
        }
        if (tclass->sominfo) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, sominfo)),
                CPrec_GetSOMInfoPatch(tclass->sominfo)
            );
        }
        if (tclass->objcinfo) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TypeClass, objcinfo)),
                CPrec_GetObjCInfoPatch(tclass->objcinfo)
            );
        }

        if (hasNextTempl) {
            if (!(addrPatch = CPrec_FindAddrPatch(TEMPL_CLASS(tclass)->next))) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClass, next)),
                    (void*)(next = CPrec_AppendAlign())
                );
                current = next;
                tclass = (TypeClass*)TEMPL_CLASS(tclass)->next;
                continue;
            }
            CPrec_NewPointerPatch((void*)(current + offsetof(TemplClass, next)), addrPatch->value);
        }

        if (hasNextTemplInst) {
            if (!(addrPatch = CPrec_FindAddrPatch(TEMPL_CLASS_INST(tclass)->next))) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(TemplClassInst, next)),
                    (void*)(nextInst = CPrec_AppendAlign())
                );
                current = nextInst;
                tclass = (TypeClass*)TEMPL_CLASS_INST(tclass)->next;
                continue;
            }
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(TemplClassInst, next)), addrPatch->value
            );
        }
        break;
    }

    return (TypeClass*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static Type* CPrec_GetTypePatch(Type* type) {
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(type))) {
        return addrPatch->value;
    }

    switch (type->type) {
        case TYPEPOINTER:
        case TYPEARRAY:
            return (Type*)CPrec_GetTypePointerPatch(TYPE_POINTER(type));
        case TYPEENUM:
            return (Type*)CPrec_GetTypeEnumPatch(TYPE_ENUM(type));
        case TYPEBITFIELD:
            return (Type*)CPrec_GetTypeBitfieldPatch(TYPE_BITFIELD(type));
        case TYPESTRUCT:
            return (Type*)CPrec_GetTypeStructPatch(TYPE_STRUCT(type));
        case TYPEFUNC:
            return (Type*)CPrec_GetTypeFuncPatch(TYPE_FUNC(type));
        case TYPEMEMBERPOINTER:
            return (Type*)CPrec_GetTypeMemberPointerPatch(TYPE_MEMBER_POINTER(type));
        case TYPETEMPLATE:
            return (Type*)CPrec_GetTypeTemplDepPatch(TYPE_TEMPLATE(type));
        case TYPECLASS:
            return (Type*)CPrec_GetTypeClassPatch(TYPE_CLASS(type));
        case TYPEVOID:
        case TYPEINT:
        case TYPEFLOAT:
        case TYPELABEL:
        case TYPEOBJCID:
        case TYPETEMPLDEPEXPR:
        default:
            CError_FATAL(2812);
            return NULL;
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ExceptionAction* CPrec_GetExceptionPatch(ExceptionAction* exc) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(exc, sizeof(ExceptionAction));
        switch (exc->type) {
            case EAT_DESTROYLOCAL:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local.dtor)
                );
                break;
            case EAT_DESTROYLOCALCOND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_cond.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_cond.dtor)
                );
                break;
            case EAT_DESTROYLOCALOFFSET:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_offset.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_offset.dtor)
                );
                break;
            case EAT_DESTROYLOCALPOINTER:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_pointer.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_pointer.dtor)
                );
                break;
            case EAT_DESTROYLOCALARRAY:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_local_array.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_local_array.dtor)
                );
                break;
            case EAT_DESTROYPARTIALARRAY:
                break;
            case EAT_DESTROYMEMBER:
            case EAT_DESTROYBASE:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member.dtor)
                );
                break;
            case EAT_DESTROYMEMBERCOND:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member_cond.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member_cond.dtor)
                );
                break;
            case EAT_DESTROYMEMBERARRAY:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.destroy_member_array.dtor)),
                    CPrec_GetObjectPatch(exc->data.destroy_member_array.dtor)
                );
                break;
            case EAT_DELETEPOINTER:
            case EAT_DELETELOCALPOINTER:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ExceptionAction, data.delete_pointer.deletefunc)),
                    CPrec_GetObjectPatch(exc->data.delete_pointer.deletefunc)
                );
                break;
            case EAT_DELETEPOINTERCOND:
                CPrec_NewPointerPatch(
                    (void*)(current +
                            offsetof(ExceptionAction, data.delete_pointer_cond.deletefunc)),
                    CPrec_GetObjectPatch(exc->data.delete_pointer_cond.deletefunc)
                );
                break;
            case EAT_CATCHBLOCK:
                if (exc->data.catch_block.catch_typeid) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.catch_block.catch_typeid)),
                        CPrec_GetObjectPatch(exc->data.catch_block.catch_typeid)
                    );
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.catch_block.catch_type)),
                        CPrec_GetTypePatch(exc->data.catch_block.catch_type)
                    );
                }
                break;
            case EAT_ACTIVECATCHBLOCK:
                break;
            case EAT_SPECIFICATION:
                if (exc->data.specification.unexp_id) {
                    int x;
                    SInt32 ptrs;
                    CPrec_AppendAlign();
                    ptrs = cprec_offset;
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(ExceptionAction, data.specification.unexp_id)),
                        (void*)ptrs
                    );
                    CPrec_AppendData(
                        exc->data.specification.unexp_id,
                        sizeof(Object*) * exc->data.specification.unexp_ids
                    );
                    for (x = 0; x < exc->data.specification.unexp_ids; x++) {
                        CPrec_NewPointerPatch(
                            (void*)(ptrs + x * sizeof(Object*)),
                            CPrec_GetObjectPatch(exc->data.specification.unexp_id[x])
                        );
                    }
                }
                break;
            case EAT_TERMINATE:
                break;
            default:
                CError_FATAL(2921);
        }

        if (!exc->prev) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(ExceptionAction, prev)), (void*)next);
        current = next;
        exc = exc->prev;
    }
    return (ExceptionAction*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ENodeList* CPrec_GetExpressionListPatch(ENodeList* lst) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(lst, sizeof(ENodeList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ENodeList, node)), CPrec_GetExpressionPatch(lst->node)
        );

        if (!lst->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(ENodeList, next)), (void*)next);
        current = next;
        lst = lst->next;
    }

    return (ENodeList*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static EMemberInfo* CPrec_GetEMemberInfoPatch(EMemberInfo* emember) {
    EMemberInfo* p;

    CError_FATAL(2969);

    p = (void*)CPrec_AppendAlign();
    CPrec_AppendData(emember, sizeof(EMemberInfo));

    if (emember->path) {
        CPrec_NewPointerPatch(&p->path, CPrec_GetClassAccessPatch(emember->path));
    }
    if (emember->expr) {
        CPrec_NewPointerPatch(&p->expr, CPrec_GetExpressionPatch(emember->expr));
    }
    CError_ASSERT(2984, !emember->templargs);
    CPrec_NewPointerPatch(&p->list, CPrec_GetNameSpaceObjectListPatch(emember->list));

    return p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ENode* CPrec_GetExpressionPatch(ENode* expr) {
    SInt32 p;

    if (ENODE_IS(expr, ETEMPLDEP) && expr->data.templdep.subtype == TDE_SOURCEREF) {
        expr->data.templdep.u.sourceref.token = NULL;
    }

    p = CPrec_AppendAlign();
    CPrec_AppendData(expr, sizeof(ENode));

    CPrec_NewPointerPatch((void*)(p + offsetof(ENode, rtype)), CPrec_GetTypePatch(expr->rtype));

    switch (expr->type) {
        case EPOSTINC:
        case EPOSTDEC:
        case EPREINC:
        case EPREDEC:
        case EINDIRECT:
        case EMONMIN:
        case EBINNOT:
        case ELOGNOT:
        case EFORCELOAD:
        case ETYPCON:
        case EBITFIELD:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.monadic)),
                CPrec_GetExpressionPatch(expr->data.monadic)
            );
            break;
        case EMUL:
        case EMULV:
        case EDIV:
        case EMODULO:
        case EADDV:
        case ESUBV:
        case EADD:
        case ESUB:
        case ESHL:
        case ESHR:
        case ELESS:
        case EGREATER:
        case ELESSEQU:
        case EGREATEREQU:
        case EEQU:
        case ENOTEQU:
        case EAND:
        case EXOR:
        case EOR:
        case ELAND:
        case ELOR:
        case EASS:
        case EMULASS:
        case EDIVASS:
        case EMODASS:
        case EADDASS:
        case ESUBASS:
        case ESHLASS:
        case ESHRASS:
        case EANDASS:
        case EXORASS:
        case EORASS:
        case ECOMMA:
        case EPMODULO:
        case EROTL:
        case EROTR:
        case EBCLR:
        case EBTST:
        case EBSET:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.left)),
                CPrec_GetExpressionPatch(expr->data.diadic.left)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.right)),
                CPrec_GetExpressionPatch(expr->data.diadic.right)
            );
            break;
        case ECOND:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.cond)),
                CPrec_GetExpressionPatch(expr->data.cond.cond)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.expr1)),
                CPrec_GetExpressionPatch(expr->data.cond.expr1)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.cond.expr2)),
                CPrec_GetExpressionPatch(expr->data.cond.expr2)
            );
            break;
        case ESTRINGCONST:
            CPrec_RawMemPatch(
                (void*)(p + offsetof(ENode, data.string.data)),
                expr->data.string.data,
                expr->data.string.size
            );
            break;
        case EOBJREF:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.objref)), CPrec_GetObjectPatch(expr->data.objref)
            );
            break;
#if VERSION == VERSION_WII_1_0RC1
        case ESCOPE:
            if (expr->data.scope.object) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(ENode, data.scope.object)),
                    CPrec_GetExpressionPatch(expr->data.scope.object)
                );
            }
            break;
#endif
        case EOBJLIST:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.objlist.list)),
                CPrec_GetNameSpaceObjectListPatch(expr->data.objlist.list)
            );
            CError_ASSERT(3059, !expr->data.objlist.templargs);
            if (expr->data.objlist.name) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(ENode, data.objlist.name)), expr->data.objlist.name
                );
            }
            break;
        case EMFPOINTER:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.left)),
                CPrec_GetExpressionPatch(expr->data.diadic.left)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.diadic.right)),
                CPrec_GetExpressionPatch(expr->data.diadic.right)
            );
            break;
        case ENULLCHECK:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.nullcheck.nullcheckexpr)),
                CPrec_GetExpressionPatch(expr->data.nullcheck.nullcheckexpr)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.nullcheck.condexpr)),
                CPrec_GetExpressionPatch(expr->data.nullcheck.condexpr)
            );
            break;
        case ETEMP:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.temp.type)),
                CPrec_GetTypePatch(expr->data.temp.type)
            );
            break;
        case EFUNCCALL:
        case EFUNCCALLP:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.funccall.funcref)),
                CPrec_GetExpressionPatch(expr->data.funccall.funcref)
            );
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.funccall.functype)),
                CPrec_GetTypePatch(TYPE(expr->data.funccall.functype))
            );
            if (expr->data.funccall.args) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(ENode, data.funccall.args)),
                    CPrec_GetExpressionListPatch(expr->data.funccall.args)
                );
            }
            break;
        case EMEMBER:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(ENode, data.emember)),
                CPrec_GetEMemberInfoPatch(expr->data.emember)
            );
            break;
        case ETEMPLDEP:
            switch (expr->data.templdep.subtype) {
                case TDE_PARAM:
                    break;
                case TDE_SIZEOF:
                case TDE_ALIGNOF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.typeexpr.type)),
                        CPrec_GetTypePatch(expr->data.templdep.u.typeexpr.type)
                    );
                    break;
                case TDE_CAST:
                    if (expr->data.templdep.u.cast.args) {
                        CPrec_NewPointerPatch(
                            (void*)(p + offsetof(ENode, data.templdep.u.cast.args)),
                            CPrec_GetExpressionListPatch(expr->data.templdep.u.cast.args)
                        );
                    }
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.cast.type)),
                        CPrec_GetTypePatch(expr->data.templdep.u.cast.type)
                    );
                    break;
                case TDE_QUALNAME:
                    CPrec_NamePatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.qual.name)),
                        expr->data.templdep.u.qual.name
                    );
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.qual.type)),
                        CPrec_GetTypePatch(TYPE(expr->data.templdep.u.qual.type))
                    );
                    break;
                case TDE_OBJ:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.obj)),
                        CPrec_GetObjectPatch(expr->data.templdep.u.obj)
                    );
                    break;
                case TDE_SOURCEREF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.sourceref.expr)),
                        CPrec_GetExpressionPatch(expr->data.templdep.u.sourceref.expr)
                    );
                    break;
                case TDE_ADDRESS_OF:
                    CPrec_NewPointerPatch(
                        (void*)(p + offsetof(ENode, data.templdep.u.monadic)),
                        CPrec_GetExpressionPatch(expr->data.templdep.u.monadic)
                    );
                    break;
                default:
                    CError_FATAL(3152);
            }
            break;
        case EINTCONST:
        case EFLOATCONST:
        case EPRECOMP:
        case EARGOBJ:
        case ELOCOBJ:
        case ELABEL:
        case EINSTRUCTION:
        case EVECTOR128CONST:
            break;
        default:
            CError_FATAL(3158);
    }

    return (ENode*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static CI_Switch* CPrec_GetSwitchInfoPatch(CI_Switch* si) {
    SInt32 p = CPrec_AppendAlign();
    CPrec_AppendData(si, sizeof(CI_Switch) + sizeof(CI_SwitchCase) * (si->numcases - 1));

    CPrec_NewPointerPatch(
        (void*)(p + offsetof(CI_Switch, expr)), CPrec_GetExpressionPatch(si->expr)
    );
    CPrec_NewPointerPatch(
        (void*)(p + offsetof(CI_Switch, exprtype)), CPrec_GetTypePatch(si->exprtype)
    );
    return (CI_Switch*)p;
}

static InlineAsm* CPrec_GetInlineAsmPatch(InlineAsm* ia, SInt32 size) {
    SInt32 p;
    SInt32 index;
    SInt32 offset;
    Object* object;

    p = CPrec_AppendAlign();
    CPrec_AppendData(ia, size);

    for (index = 0;; index++) {
        object = InlineAsm_GetObjectOffset(ia, index, &offset);
        if (!object) {
            break;
        }

        CPrec_NewPointerPatch((void*)(p + offset), CPrec_GetObjectPatch(object));
    }

    return (InlineAsm*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
#if VERSION < VERSION_GC_2_5
#undef CError_LINE_SHIFT
#define CError_LINE_SHIFT (-51 + CError_LINE_DELTA)
#endif
static CI_Statement* CPrec_GetStatementPatch(CI_Statement* stmt, short count) {
    short i;
    SInt32 first;
    SInt32 current;

    for (i = 0; i < count; i++) {
        stmt[i].sourcefilepath = NULL;
        stmt[i].sourceoffset = -1;
    }

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(stmt, sizeof(CI_Statement) * count);

    for (i = 0; i < count; i++) {
#if VERSION >= VERSION_GC_2_5
        if (stmt->sourcefilepath) {
            CPrec_NamePatch(
                (void*)(current + offsetof(CI_Statement, sourcefilepath)), stmt->sourcefilepath
            );
        }
#endif
        if (stmt->dobjstack) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(CI_Statement, dobjstack)),
                CPrec_GetExceptionPatch(stmt->dobjstack)
            );
        }

        switch (stmt->type) {
            case ST_EXPRESSION:
            case ST_RETURN:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
                if (stmt->u.expr) {
                    CPrec_NewPointerPatch(
                        (void*)(current + offsetof(CI_Statement, u.expr)),
                        CPrec_GetExpressionPatch(stmt->u.expr)
                    );
                }
                break;
            case ST_IFGOTO:
            case ST_IFNGOTO:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.ifgoto.expr)),
                    CPrec_GetExpressionPatch(stmt->u.ifgoto.expr)
                );
                break;
            case ST_SWITCH:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.switchdata)),
                    CPrec_GetSwitchInfoPatch(stmt->u.switchdata)
                );
                break;
            case ST_ASM:
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(CI_Statement, u.asmdata.data)),
                    CPrec_GetInlineAsmPatch(stmt->u.asmdata.data, stmt->u.asmdata.size)
                );
                break;
            case ST_NOP:
            case ST_LABEL:
            case ST_GOTO:
                break;
            default:
                CError_FATAL(3328);
        }

        current += sizeof(CI_Statement);
        stmt++;
    }

    return (CI_Statement*)first;
}

static CI_Var* CPrec_GetLocObjectPatch(CI_Var* obj, short count) {
    SInt32 first;
    SInt32 current;
    short i;

    first = current = CPrec_AppendAlign();
    CPrec_AppendData(obj, sizeof(CI_Var) * count);

    for (i = 0; i < count; i++) {
        CPrec_NamePatch((void*)(current + offsetof(CI_Var, name)), obj->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(CI_Var, type)), CPrec_GetTypePatch(obj->type)
        );
        current += sizeof(CI_Var);
        obj++;
    }

    return (CI_Var*)first;
}

static CI_FuncData* CPrec_GetInlineFuncPatch(CI_FuncData* ifunc) {
    CI_FuncData* p;

    memclrw(&ifunc->fileoffset, sizeof(FileOffsetInfo));
    ifunc->symdecloffset = 0;
    ifunc->functionbodyoffset = 0;
    ifunc->functionbodypath = NULL;
    ifunc->symdeclend = 0;

    p = (void*)CPrec_AppendAlign();
#if VERSION == VERSION_WII_1_0RC1
    {
        SInt32* emptyUIDs = NULL;
        CI_FuncData body;
        memcpy(&body, ifunc, offsetof(CI_FuncData, saved_variable_uids));
        body.flags &= ~CI_HasSavedVariableTokens;
        CPrec_AppendData(&body, offsetof(CI_FuncData, saved_variable_uids));
        CPrec_AppendData(&emptyUIDs, sizeof(emptyUIDs));
        if (ifunc->flags & CI_HasSavedVariableUIDs) {
            SInt32 start = CPrec_AppendAlign();
            SInt32 count = ifunc->numarguments + ifunc->numlocals;
            CPrec_AppendData(ifunc->saved_variable_uids, count * sizeof(SInt32));
            CPrec_NewPointerPatch(&p->saved_variable_uids, (void*)start);
        }
    }
#else
    CPrec_AppendData(ifunc, sizeof(CI_FuncData));
#endif

    if (ifunc->numarguments) {
        CPrec_NewPointerPatch(
            &p->arguments, CPrec_GetLocObjectPatch(ifunc->arguments, ifunc->numarguments)
        );
    }
    if (ifunc->numlocals) {
        CPrec_NewPointerPatch(&p->locals, CPrec_GetLocObjectPatch(ifunc->locals, ifunc->numlocals));
    }
    if (ifunc->numstatements) {
        CPrec_NewPointerPatch(
            &p->statements, CPrec_GetStatementPatch(ifunc->statements, ifunc->numstatements)
        );
    }

    return p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ObjEnumConst* CPrec_GetObjEnumConstPatch(ObjEnumConst* obj) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(obj, sizeof(ObjEnumConst));
        if (cprec_dowrite) {
            CError_ASSERT(3416, obj->access != 255);
            obj->access = 255;
        }
        CPrec_NamePatch((void*)(current + offsetof(ObjEnumConst, name)), obj->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ObjEnumConst, type)), CPrec_GetTypePatch(obj->type)
        );

        if (!obj->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(obj->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjEnumConst, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(ObjEnumConst, next)), (void*)next);
            current = next;
            obj = obj->next;
            CPrec_NewAddrPatch(obj, (void*)current);
        }
    } while (1);

    return (ObjEnumConst*)first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static ObjType* CPrec_GetObjTypePatch(ObjType* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(obj, sizeof(ObjType));
    CPrec_NewPointerPatch((void*)(p + offsetof(ObjType, type)), CPrec_GetTypePatch(obj->type));
    return (ObjType*)p;
}

static ObjTypeTag* CPrec_GetObjTypeTagPatch(ObjTypeTag* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(obj, sizeof(ObjTypeTag));
    CPrec_NewPointerPatch((void*)(p + offsetof(ObjTypeTag, type)), CPrec_GetTypePatch(obj->type));
    return (ObjTypeTag*)p;
}

static ObjMemberVar* CPrec_GetObjMemberVarPatch(ObjMemberVar* ivar) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(ivar))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(ivar, (void*)(first = current = CPrec_AppendAlign()));

    while (1) {
        if (ivar->has_path) {
            CPrec_AppendData(ivar, sizeof(ObjMemberVarPath));
            if (OBJ_MEMBER_VAR_PATH(ivar)->path) {
                CPrec_NewPointerPatch(
                    (void*)(current + offsetof(ObjMemberVarPath, path)),
                    CPrec_GetClassAccessPatch(OBJ_MEMBER_VAR_PATH(ivar)->path)
                );
            }
        } else {
            CPrec_AppendData(ivar, sizeof(ObjMemberVar));
        }

        if (ivar->name) {
            CPrec_NamePatch((void*)(current + offsetof(ObjMemberVar, name)), ivar->name);
        }
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(ObjMemberVar, type)), CPrec_GetTypePatch(ivar->type)
        );

        if (!ivar->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(ivar->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(ObjMemberVar, next)), addrPatch->value
            );
            break;
        }

        next = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(current + offsetof(ObjMemberVar, next)), (void*)next);

        current = next;
        ivar = ivar->next;
        CPrec_NewAddrPatch(ivar, (void*)current);
    }

    return (ObjMemberVar*)first;
}

static DefArgCtorInfo* CPrec_GetDefArgCtorInfoPatch(DefArgCtorInfo* dac) {
    DefArgCtorInfo* p = (DefArgCtorInfo*)CPrec_AppendAlign();
    CPrec_AppendData(dac, sizeof(DefArgCtorInfo));
    CPrec_NewPointerPatch(&p->default_func, CPrec_GetObjectPatch(dac->default_func));
    CPrec_NewPointerPatch(&p->default_arg, CPrec_GetExpressionPatch(dac->default_arg));
    return p;
}

static InlineXRef* CPrec_GetInlineXRefPatch(InlineXRef* ix) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(ix, sizeof(InlineXRef) + sizeof(ix->xref[0]) * (ix->numxrefs - 1));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(InlineXRef, object)), CPrec_GetObjectPatch(ix->object)
        );

        if (!ix->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(InlineXRef, next)), (void*)next);
        current = next;
        ix = ix->next;
    }

    return (InlineXRef*)first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static Object* CPrec_GetObjectPatch(Object* obj) {
    AddrPatch* addrPatch;
    SInt32 p;

    if (CWUserBreak(cparams.context) != cwNoErr) {
        CError_UserBreak();
    }

    if ((addrPatch = CPrec_FindAddrPatch(obj))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(obj, (void*)(p = CPrec_AppendAlign()));

#if VERSION == VERSION_WII_1_0RC1
    if (cprec_dowrite && obj->section) {
        Patch* patch = lalloc(sizeof(*patch));
        patch->offset = p + offsetof(Object, section);
        patch->next = cprec_sectionpatches;
        cprec_sectionpatches = patch;
    }
#endif
    obj->toc = NULL;
    obj->debugInfo.symbol = NULL;

    if ((obj->qual & Q_IS_TEMPLATED) && obj->datatype != DALIAS) {
        CPrec_AppendData(obj, sizeof(ObjectTemplated));
        CPrec_NewPointerPatch(
            &((ObjectTemplated*)p)->parent, CPrec_GetObjectPatch(((ObjectTemplated*)obj)->parent)
        );
    } else {
        CPrec_AppendData(obj, sizeof(Object));
    }

    if (obj->nspace) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(Object, nspace)), CPrec_GetNameSpacePatch(obj->nspace)
        );
    }
    if (obj->name) {
        CPrec_NamePatch((void*)(p + offsetof(Object, name)), obj->name);
    }
    CPrec_NewPointerPatch((void*)(p + offsetof(Object, type)), CPrec_GetTypePatch(obj->type));

    switch (obj->datatype) {
        case DABSOLUTE:
            break;

        case DLOCAL:
            CError_FATAL(3647);
            break;

        case DFUNC:
        case DVFUNC:
            if (IS_TEMPL_FUNC(obj->type)) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.u.templ)),
                    CPrec_GetTemplateFunctionPatch(obj->u.func.u.templ)
                );
            } else if ((obj->qual & Q_INLINE) && obj->u.func.u.ifuncdata) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.u.ifuncdata)),
                    CPrec_GetInlineFuncPatch(obj->u.func.u.ifuncdata)
                );
            }

            if (obj->u.func.defargdata) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.defargdata)),
                    CPrec_GetDefArgCtorInfoPatch(obj->u.func.defargdata)
                );
            }
            if (obj->u.func.linkname) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(Object, u.func.linkname)), obj->u.func.linkname
                );
            }
            if (obj->u.func.inst) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.func.inst)),
                    CPrec_GetTemplFuncInstancePatch(obj->u.func.inst)
                );
            }
            break;

        case DDATA:
            CError_ASSERT(3689, !obj->u.data.info);
            if (obj->qual & Q_INLINE_DATA) {
                switch (obj->type->type) {
                    case TYPEINT:
                    case TYPEENUM:
                    case TYPEPOINTER:
                        break;
                    case TYPEFLOAT:
                        CPrec_RawMemPatch(
                            (void*)(p + offsetof(Object, u.data.u.floatconst)),
                            obj->u.data.u.floatconst,
                            sizeof(Float)
                        );
                        break;
                    default:
                        CError_FATAL(3705);
                }
            }
            if (obj->u.data.linkname) {
                CPrec_NamePatch(
                    (void*)(p + offsetof(Object, u.data.linkname)), obj->u.data.linkname
                );
            }
            break;

        case DINLINEFUNC:
            CPrec_RawMemPatch(
                (void*)(p + offsetof(Object, u.ifunc.data)), obj->u.ifunc.data, obj->u.ifunc.size
            );
            if (obj->u.ifunc.xrefs) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.ifunc.xrefs)),
                    CPrec_GetInlineXRefPatch(obj->u.ifunc.xrefs)
                );
            }
            break;

        case DALIAS:
            CPrec_NewPointerPatch(
                (void*)(p + offsetof(Object, u.alias.object)),
                CPrec_GetObjectPatch(obj->u.alias.object)
            );
            if (obj->u.alias.member) {
                CPrec_NewPointerPatch(
                    (void*)(p + offsetof(Object, u.alias.member)),
                    CPrec_GetClassAccessPatch(obj->u.alias.member)
                );
            }
            break;

        default:
            CError_FATAL(3744);
    }

    if (cprec_dowrite) {
        obj->datatype = -1;
    }

    return (Object*)p;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static ObjBase* CPrec_GetObjBasePatch(ObjBase* obj) {
    switch (obj->otype) {
        default:
            CError_FATAL(3761);
        case OT_ENUMCONST:
            return (ObjBase*)CPrec_GetObjEnumConstPatch((ObjEnumConst*)obj);
        case OT_TYPE:
            return (ObjBase*)CPrec_GetObjTypePatch((ObjType*)obj);
        case OT_TYPETAG:
            return (ObjBase*)CPrec_GetObjTypeTagPatch((ObjTypeTag*)obj);
        case OT_NAMESPACE: {
            AddrPatch* addrPatch;
            ObjNameSpace* p;
            ObjNameSpace* q;

            if ((addrPatch = CPrec_FindAddrPatch(obj))) {
                p = addrPatch->value;
            } else {
                q = p = (void*)CPrec_AppendAlign();
                CPrec_NewAddrPatch(obj, q);
                CPrec_AppendData(obj, sizeof(ObjNameSpace));
                CPrec_NewPointerPatch(
                    &p->nspace, CPrec_GetNameSpacePatch(((ObjNameSpace*)obj)->nspace)
                );
            }
            return (ObjBase*)p;
        }
        case OT_MEMBERVAR:
            return (ObjBase*)CPrec_GetObjMemberVarPatch((ObjMemberVar*)obj);
        case OT_OBJECT:
            return (ObjBase*)CPrec_GetObjectPatch((Object*)obj);
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static NameSpaceObjectList* CPrec_GetNameSpaceObjectListPatch(NameSpaceObjectList* nsol) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(nsol))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nsol, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(nsol, sizeof(NameSpaceObjectList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceObjectList, object)),
            CPrec_GetObjBasePatch(nsol->object)
        );

        if (!nsol->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(nsol->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceObjectList, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceObjectList, next)), (void*)next
            );
            current = next;
            nsol = nsol->next;
            CPrec_NewAddrPatch(nsol, (void*)current);
        }
    } while (1);

    return (NameSpaceObjectList*)first;
}

static NameSpaceName* CPrec_GetNameSpaceNamePatch(NameSpaceName* nsn, Boolean flushBuffer) {
    AddrPatch* addrPatch;
    SInt32 first;
    SInt32 current;
    SInt32 next;

    if ((addrPatch = CPrec_FindAddrPatch(nsn))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nsn, (void*)(first = current = CPrec_AppendAlign()));

    do {
        CPrec_AppendData(nsn, sizeof(NameSpaceName));
        CPrec_NamePatch((void*)(current + offsetof(NameSpaceName, name)), nsn->name);
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceName, first.object)),
            CPrec_GetObjBasePatch(nsn->first.object)
        );
        if (nsn->first.next) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceName, first.next)),
                CPrec_GetNameSpaceObjectListPatch(nsn->first.next)
            );
        }

        if (!nsn->next) {
            break;
        }

        if ((addrPatch = CPrec_FindAddrPatch(nsn->next))) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(NameSpaceName, next)), addrPatch->value
            );
            break;
        } else {
            next = CPrec_AppendAlign();
            CPrec_NewPointerPatch((void*)(current + offsetof(NameSpaceName, next)), (void*)next);
            current = next;
            nsn = nsn->next;
            CPrec_NewAddrPatch(nsn, (void*)current);
        }
    } while (!flushBuffer || !cprec_dowrite || CPrec_FlushBufferCheck() == noErr);

    return (NameSpaceName*)first;
}

static NameSpaceList* CPrec_GetNameSpaceListPatch(NameSpaceList* nsl) {
    SInt32 first;
    SInt32 current;
    SInt32 next;
    first = current = CPrec_AppendAlign();

    for (;;) {
        CPrec_AppendData(nsl, sizeof(NameSpaceList));
        CPrec_NewPointerPatch(
            (void*)(current + offsetof(NameSpaceList, nspace)), CPrec_GetNameSpacePatch(nsl->nspace)
        );

        if (!nsl->next) {
            break;
        }
        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(NameSpaceList, next)), (void*)next);
        current = next;
        nsl = nsl->next;
    }

    return (NameSpaceList*)first;
}

static NameSpace* CPrec_GetNameSpacePatch(NameSpace* nspace) {
    SInt32 p;
    AddrPatch* addrPatch;

    if ((addrPatch = CPrec_FindAddrPatch(nspace))) {
        return addrPatch->value;
    }

    CPrec_NewAddrPatch(nspace, (void*)(p = CPrec_AppendAlign()));
    CPrec_AppendData(nspace, sizeof(NameSpace));

    if (nspace->parent) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, parent)), CPrec_GetNameSpacePatch(nspace->parent)
        );
    }
    if (nspace->name) {
        CPrec_NamePatch((void*)(p + offsetof(NameSpace, name)), nspace->name);
    }
    if (nspace->usings) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, usings)), CPrec_GetNameSpaceListPatch(nspace->usings)
        );
    }
    if (nspace->theclass) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, theclass)), CPrec_GetTypePatch((Type*)nspace->theclass)
        );
    }

    if (nspace->is_hash) {
        SInt32 hash;
        SInt32 i;
        hash = CPrec_AppendAlign();
        CPrec_NewPointerPatch((void*)(p + offsetof(NameSpace, data.hash)), (void*)hash);
        CPrec_AppendData(nspace->data.hash, sizeof(NameSpaceName*) * 1024);
        for (i = 0; i < 1024; i++) {
            if (nspace->data.hash[i]) {
                CPrec_NewPointerPatch(
                    (void*)(hash + i * sizeof(NameSpaceName*)),
                    CPrec_GetNameSpaceNamePatch(nspace->data.hash[i], FALSE)
                );
            }
        }
    } else if (nspace->data.list) {
        CPrec_NewPointerPatch(
            (void*)(p + offsetof(NameSpace, data.list)),
            CPrec_GetNameSpaceNamePatch(nspace->data.list, FALSE)
        );
    }

    return (NameSpace*)p;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static void CPrec_DumpRootNameSpace(void) {
    NameSpaceList* nslist;
    NameSpaceName* nsname;
    SInt32 i;

    CError_ASSERT(3972, cscope_root->is_hash);

    if (cscope_root->usings) {
        nslist = CPrec_GetNameSpaceListPatch(cscope_root->usings);
        if (cprec_dowrite) {
            cprec_header->usings = nslist;
        }
    }

    if (cprec_dowrite) {
        cprec_header->root_names = cscope_root->names;
    }

    for (i = 0; i < 0x400; i++) {
        if (cscope_root->data.hash[i]) {
            nsname = CPrec_GetNameSpaceNamePatch(cscope_root->data.hash[i], TRUE);
            if (cprec_dowrite) {
                if (cprec_ioerror != noErr) {
                    return;
                }
                cprec_header->root_nametable[i] = nsname;
            }
        }
    }
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static CSOMStub* CPrec_GetSOMPatch(CSOMStub* stub) {
    CSOMStub* first;
    SInt32 next;
    CSOMStub* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(stub, sizeof(CSOMStub));

        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(stub->object));
        CPrec_NewPointerPatch(&current->tclass, CPrec_GetTypePatch(TYPE(stub->tclass)));

        if (!stub->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CSOMStub*)next;
        stub = stub->next;
    }

    return first;
}

static OLinkList* CPrec_GetOLinkPatch(OLinkList* olink) {
    SInt32 first;
    SInt32 current;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    for (;;) {
        CPrec_AppendData(olink, sizeof(OLinkList));

        CPrec_NewPointerPatch(
            (void*)(current + offsetof(OLinkList, obj)), CPrec_GetObjectPatch(olink->obj)
        );

        if (!olink->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(OLinkList, next)), (void*)next);
        current = next;
        olink = olink->next;
    }

    return (OLinkList*)first;
}

static StaticData* CPrec_GetStaticDataPatch(StaticData* sd) {
    SInt32 current;
    SInt32 first;
    SInt32 next;

    first = current = CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(sd, sizeof(StaticData));

        CPrec_NewPointerPatch(
            (void*)(current + offsetof(StaticData, object)), CPrec_GetObjectPatch(sd->object)
        );
        if (sd->buffer) {
#if VERSION == VERSION_WII_1_0RC1
            SInt32 size = (SInt32)((UInt32)sd->size & ~CPREC_STATIC_READ_ONLY);
#else
            SInt32 size = sd->object->type->size;
#endif
            CPrec_RawMemPatch((void*)(current + offsetof(StaticData, buffer)), sd->buffer, size);
        }
        if (sd->links) {
            CPrec_NewPointerPatch(
                (void*)(current + offsetof(StaticData, links)), CPrec_GetOLinkPatch(sd->links)
            );
        }

        if (!sd->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch((void*)(current + offsetof(StaticData, next)), (void*)next);
        current = next;
        sd = sd->next;
    }

    return (StaticData*)first;
}

#if VERSION == VERSION_WII_1_0RC1
static ObjectList* CPrec_GetTentativeDataPatch(const ObjectList* list) {
    ObjectList* first = NULL;
    ObjectList* previous = NULL;
    const ObjectList record = {0};

    for (; list; list = list->next) {
        ObjectList* current;

        if (list->object->flags & OBJECT_DEFINED) {
            continue;
        }
        current = (ObjectList*)CPrec_AppendAlign();
        CPrec_AppendData(&record, sizeof(record));
        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(list->object));
        if (previous) {
            CPrec_NewPointerPatch(&previous->next, current);
        } else {
            first = current;
        }
        previous = current;
    }
    return first;
}
#endif

static CallbackAction* CPrec_GetCallbackPatch(CallbackAction* ca) {
    CallbackAction* first;
    SInt32 next;
    CallbackAction* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(ca, sizeof(CallbackAction));

        CPrec_NewPointerPatch(&current->obj, CPrec_GetObjectPatch(ca->obj));
        CPrec_NewPointerPatch(&current->tclass, CPrec_GetTypePatch(TYPE(ca->tclass)));

        if (!ca->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CallbackAction*)next;
        ca = ca->next;
    }

    return first;
}

static ObjCSelector** CPrec_GetSelHashTablePatch(ObjCSelector** table) {
    SInt32 first;
    int i;
    SInt32 current;
    ObjCSelector* sel;

    CPrec_AppendAlign();
    first = current = cprec_offset;
    CPrec_AppendData(table, sizeof(ObjCSelector*) * 0x400);

    for (i = 0; (unsigned int)i < 0x400U; i++, current += 4) {
        if ((sel = table[i])) {
            CPrec_NewPointerPatch((void*)current, CPrec_GetObjCSelectorPatch(sel));
        }
    }

    return (ObjCSelector**)first;
}

static InitExpr* CPrec_GetIExpressionPatch(InitExpr* initexpr) {
    InitExpr* first;
    SInt32 next;
    InitExpr* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        CPrec_AppendData(initexpr, sizeof(InitExpr));

        CPrec_NewPointerPatch(&current->object, CPrec_GetObjectPatch(initexpr->object));
        CPrec_NewPointerPatch(&current->expr, CPrec_GetExpressionPatch(initexpr->expr));

        if (!initexpr->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (InitExpr*)next;
        initexpr = initexpr->next;
    }

    return first;
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static CI_Action* CPrec_GetInlineActionPatch(CI_Action* act) {
    CI_Action* first;
    SInt32 next;
    CI_Action* current;

    first = current = (void*)CPrec_AppendAlign();
    while (1) {
        if (act->actiontype == CI_ActionInlineFunc) {
            memclrw(&act->u.inlinefunc.fileoffset, sizeof(FileOffsetInfo));
        }

        CPrec_AppendData(act, sizeof(CI_Action));

        CPrec_NewPointerPatch(&current->obj, CPrec_GetObjectPatch(act->obj));

        switch (act->actiontype) {
            case CI_ActionInlineFunc:
                if (act->u.inlinefunc.stream.firsttoken) {
                    CPrec_NewPointerPatch(
                        &current->u.inlinefunc.stream.firsttoken,
                        CPrec_GetTStreamPatch(
                            act->u.inlinefunc.stream.firsttoken, act->u.inlinefunc.stream.tokens
                        )
                    );
                }
                if (act->u.inlinefunc.tclass) {
                    CPrec_NewPointerPatch(
                        &current->u.inlinefunc.tclass,
                        CPrec_GetTypePatch(TYPE(act->u.inlinefunc.tclass))
                    );
                }
                break;

            case CI_ActionMemberFunc:
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.templ, CPrec_GetTypePatch(TYPE(act->u.memberfunc.templ))
                );
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.inst, CPrec_GetTypePatch(TYPE(act->u.memberfunc.inst))
                );
                CPrec_NewPointerPatch(
                    &current->u.memberfunc.tmemb,
                    CPrec_GetTemplateMemberPatch(act->u.memberfunc.tmemb)
                );
                break;

            case CI_ActionTemplateFunc:
                CPrec_NewPointerPatch(
                    &current->u.templatefunc.func,
                    CPrec_GetTemplateFunctionPatch(act->u.templatefunc.func)
                );
                CPrec_NewPointerPatch(
                    &current->u.templatefunc.inst,
                    CPrec_GetTemplFuncInstancePatch(act->u.templatefunc.inst)
                );
                break;

            case CI_ActionDefaultFunc:
                break;

            default:
                CError_FATAL(4244);
        }

        if (!act->next) {
            break;
        }

        CPrec_AppendAlign();
        next = cprec_offset;
        CPrec_NewPointerPatch(&current->next, (void*)next);
        current = (CI_Action*)next;
        act = act->next;
    }

    return first;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

static void CPrec_GenerateBuiltinPatches(void) {
    Patch* scan;
    int x;
    int y;

    for (x = 0; x < cprec_builtins; x++) {
        y = 0;
        for (scan = cprec_builtin[x].patches; scan; scan = scan->next) {
            ++y;
        }

        if (y) {
#if VERSION == VERSION_WII_1_0RC1
            if (x >= cprec_builtins - CPrec_AddedRuntimeBuiltinCount) {
                cprec_header->version = CPREC_SOURCE_VERSION_WITH_RUNTIME_OBJECTS;
            }
#endif
            CPrec_AppendWord32(y);
            CPrec_AppendWord32(x);
            for (scan = cprec_builtin[x].patches; scan; scan = scan->next) {
                CPrec_AppendWord32(scan->offset);
            }
        }
    }

    CPrec_AppendWord32(0);
}

static void CPrec_GenerateTokenStreamPatches(void) {
    TokenPatch* scan;

    for (scan = cprec_tokenpatches; scan; scan = scan->next) {
        CPrec_AppendWord32((UInt32)scan->tokens);
        CPrec_AppendWord32((UInt32)scan->count);
    }
    CPrec_AppendWord32(0);
}

static OSErr CPrec_CompressWrite(const char* data, SInt32 size) {
    char buf[2048 + 256];
    OSErr err;
    int bufpos = 0;
    int blockstart;
    int c;
    const char* p = data;
    const char* end = data + size;

    for (;;) {
        if (p < end) {
            if (!*p) {
                c = 224;
                while (!*p && p < end && c < 256) {
                    c++;
                    p++;
                }
                buf[bufpos++] = c - 1;
            } else {
                blockstart = bufpos++;
                c = 0;
                while (p < end && c < 224) {
                    if (!p[0] && !p[1]) {
                        break;
                    } else {
                        buf[bufpos++] = *(p++);
                        c++;
                    }
                }
                buf[blockstart] = c - 1;
            }
        }

        if (p >= end || bufpos > 2048) {
            if ((err = COS_FileWrite(cprec_refnum, buf, bufpos))) {
                return err;
            }

            if (p >= end) {
                break;
            } else {
                bufpos = 0;
            }
        }
    }

    return noErr;
}

static OSErr CPrec_FlushRawBuffer(void) {
    OSErr err;

    if (cprec_dowrite) {
        CPrec_AppendAlign();
        cprec_zero_offset += cprec_glist.size;
        COS_LockHandle(cprec_glist.data);
        err = CPrec_CompressWrite(*cprec_glist.data, cprec_glist.size);
        COS_UnlockHandle(cprec_glist.data);
        cprec_glist.size = 0;

        return err;
    } else {
        return noErr;
    }
}

static OSErr CPrec_FlushBufferCheck(void) {
    OSErr err;

    if (cprec_glist.size > flushmax) {
        flushmax = cprec_glist.size;
    }

    if (cprec_glist.size > 10000) {
        if ((err = CPrec_FlushRawBuffer())) {
            return cprec_ioerror = err;
        }
    }

    return noErr;
}

static void CPrec_AppendBigEndianWord32(SInt32 v) {
    union {
        SInt32 l;
        UInt8 b[4];
    } u;
    u.l = v;
    CPrec_AppendByte(u.b[3]);
    CPrec_AppendByte(u.b[2]);
    CPrec_AppendByte(u.b[1]);
    CPrec_AppendByte(u.b[0]);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
static int CPrec_CompressPatches(void) {
    Patch* scan;
    int count;
    SInt32 value;
    SInt32 last;
    SInt32 delta;

    cprec_glist.size = 0;

    scan = cprec_patch_list;
    count = last = 0;
    while (scan) {
        CError_ASSERT(4406, (scan->offset & 0x80000001) == 0);

        delta = scan->offset - last;
        value = scan->offset;
        if (delta >= -128 && delta <= 126) {
            CPrec_AppendByte((delta >> 1) | 0x80);
        } else {
            CPrec_AppendBigEndianWord32(value);
        }

        last = scan->offset;
        scan = scan->next;
        count++;
    }

    return count;
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#if VERSION == VERSION_WII_1_0RC1
static UInt32 CPrec_AppendIncludedFileName(const char* text) {
    HashNameNode* name;
    UInt32 offset;

    if (!text) {
        return 0;
    }
    offset = CPrec_AppendAlign();
    name = GetHashNameNodeExport(text);
    CPrec_AppendWord32(0);
    CPrec_AppendWord32(0);
    CPrec_AppendWord16(name->hashval);
    CPrec_AppendString(text);
    return offset;
}

static int CPrec_AppendIncludedFile(const CPrecStockFile* file, void* context) {
    static const UInt8 padding[CPrecStockFileMetadataOffset - CPrecStockFileKindOffset] = {0};
    UInt8 flags[CPrecStockFileMetadataSize - 12] = {0};
    UInt32* first = context;
    UInt32 path = CPrec_AppendIncludedFileName(file->path);
    UInt32 name = CPrec_AppendIncludedFileName(file->includeName);
    UInt32 condition = 0;
    UInt32 metadata;
    UInt32 record;

    if (file->condition) {
        condition = cprec_offset;
        CPrec_AppendString(file->condition);
    }
    metadata = CPrec_AppendAlign();
    CPrec_AppendWord32(0);
    CPrec_AppendWord32(name);
    CPrec_AppendWord32(condition);
    flags[CPrecStockFileIncludeOnceOffset - 12] = file->includeOnce != 0;
    flags[CPrecStockFileSystemIncludeOffset - 12] = file->systemInclude != 0;
    CPrec_AppendData(flags, sizeof(flags));

    // These records retain raw offsets and are read before relocating the enclosing graph.
    record = CPrec_AppendAlign();
    CPrec_AppendWord32(path);
    CPrec_AppendWord32(*first);
    CPrec_AppendData(padding, sizeof(padding));
    CPrec_AppendWord32(metadata);
    *first = record;
    return CPrec_FlushBufferCheck() == noErr;
}
#endif

static OSErr CPrec_DumpColorSymbolTable(Boolean doWrite) {
    OSErr err;

    freelheap();

    CPrec_InitAddressHashTable();
    CPrec_InitPointerHashTable();

    cprec_patch_list = NULL;
    cprec_tokenpatches = NULL;
#if VERSION == VERSION_WII_1_0RC1
    cprec_sectionpatches = NULL;
#endif
    cprec_offset = 0;
    cprec_zero_offset = 0;
    cprec_dowrite = doWrite;
    cprec_ioerror = noErr;

    CPrec_SetupBuiltIn();
    CPrec_AppendWord32(0);

    CPrec_DumpNameTable();
    if ((err = CPrec_FlushRawBuffer()) != noErr) {
        return err;
    }

    CPrec_DumpMacroTable();
    if ((err = CPrec_FlushRawBuffer()) != noErr) {
        return err;
    }

    CPrec_DumpRootNameSpace();
    if (doWrite) {
        if (cprec_ioerror != noErr) {
            return cprec_ioerror;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (ctempl_templates) {
        TemplClass* p = TEMPL_CLASS(CPrec_GetTypePatch(TYPE(ctempl_templates)));
        if (doWrite) {
            cprec_header->ctempl_templates = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (ctempl_templatefuncs) {
        TemplateFunction* p = CPrec_GetTemplateFunctionPatch(ctempl_templatefuncs);
        if (doWrite) {
            cprec_header->ctempl_templatefuncs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (csom_stubs) {
        CSOMStub* p = CPrec_GetSOMPatch(csom_stubs);
        if (doWrite) {
            cprec_header->csom_stubs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cprec_staticdata) {
        StaticData* p = CPrec_GetStaticDataPatch(cprec_staticdata);
        if (doWrite) {
            cprec_header->cprec_staticdata = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

#if VERSION == VERSION_WII_1_0RC1
    if (cinit_tentative) {
        ObjectList* p = CPrec_GetTentativeDataPatch(cinit_tentative);
        if (doWrite) {
            cprec_header->tentativeData = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }
#endif

    if (callbackactions) {
        CallbackAction* p = CPrec_GetCallbackPatch(callbackactions);
        if (doWrite) {
            cprec_header->callbackactions = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_class) {
        Type* p = CPrec_GetTypePatch(cobjc_type_class);
        if (doWrite) {
            cprec_header->cobjc_type_class = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_id) {
        Type* p = CPrec_GetTypePatch(cobjc_type_id);
        if (doWrite) {
            cprec_header->cobjc_type_id = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_type_sel) {
        Type* p = CPrec_GetTypePatch(cobjc_type_sel);
        if (doWrite) {
            cprec_header->cobjc_type_sel = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_selhashtable) {
        ObjCSelector** p = CPrec_GetSelHashTablePatch(cobjc_selhashtable);
        if (doWrite) {
            cprec_header->cobjc_selhashtable = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_classdefs) {
        struct BClassList* p =
            (struct BClassList*)CPrec_GetBClassListPatch((BClassList*)cobjc_classdefs);
        if (doWrite) {
            cprec_header->cobjc_classdefs = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cobjc_protocols) {
        ObjCProtocol* p = CPrec_GetObjCProtocolPatch(cobjc_protocols);
        if (doWrite) {
            cprec_header->cobjc_protocols = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (init_expressions) {
        InitExpr* p = CPrec_GetIExpressionPatch(init_expressions);
        if (doWrite) {
            cprec_header->init_expressions = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

    if (cinline_tactionlist) {
        CI_Action* p = CPrec_GetInlineActionPatch(cinline_tactionlist);
        if (doWrite) {
            cprec_header->cinline_tactionlist = p;
        }
        if ((err = CPrec_FlushRawBuffer()) != noErr) {
            return err;
        }
    }

#if VERSION == VERSION_WII_1_0RC1
    if (doWrite) {
        UInt32 first = 0;
        if (!CPrep_VisitIncludedFiles(CPrec_AppendIncludedFile, &first)) {
            return cprec_ioerror;
        }
        cprec_header->includedFiles = first;
        return CPrec_FlushRawBuffer();
    }
#endif
    return noErr;
}

static OSErr CPrec_FileAlign(short refnum, SInt32* len) {
    char buf[8];
    SInt32 n;
    OSErr err;

    if ((*len & 7) == 0) {
        return noErr;
    }

    n = 8 - (*len & 7);
    memclrw(buf, 8);
    err = COS_FileWrite(refnum, buf, n);
    *len += n;
    return err;
}

static OSErr CPrec_WriteFile(void) {
    SInt32 offset;
    char str[128];
    HashNameNode* name;
    int i;
    OSErr err;

    if (InitGList(&cprec_glist, 0x40000)) {
        CError_NoMem();
    }

    CompilerGetCString(10, str);
    CWShowStatus(cparams.context, str, "");
    CPrep_RemoveSpecialMacros();

    i = 0;
    do {
        for (name = name_hash_nodes[i]; name; name = name->next) {
            name->id = 0;
        }
    } while (++i < 0x800);

    if ((err = CPrec_DumpColorSymbolTable(FALSE)) != noErr) {
        return err;
    }

    CompilerGetCString(11, str);
    CWShowStatus(cparams.context, str, "");

    cprec_header = galloc(sizeof(Header));
    memclrw(cprec_header, sizeof(Header));

    cprec_header->magic = 0xBEEFFACE;
    cprec_header->version = CPREC_SOURCE_VERSION;
    cprec_header->target = 2;
    cprec_header->check_header_flags = copts.checkprecompflags;
    cprec_header->cplusplus = copts.cplusplus;
    cprec_header->uniqueID = CParser_GetUniqueID();
    cprec_header->cobjc_selrefcount = cobjc_selrefcount;
    cprec_header->cobjc_classrefcount = cobjc_classrefcount;
    cprec_header->cobjc_stringcount = cobjc_stringcount;

    if ((err = COS_FileWrite(cprec_refnum, cprec_header, sizeof(Header))) != noErr) {
        return err;
    }

    offset = sizeof(Header);

    if ((err = CPrec_DumpColorSymbolTable(TRUE)) != noErr) {
        return err;
    }

    cprec_header->rawsize = cprec_offset;
    cprec_header->rawdataoffset = offset;

    if ((err = COS_FileGetPos(cprec_refnum, &offset)) != noErr) {
        return err;
    }

    cprec_header->rawdatasize = offset - cprec_header->rawdataoffset;

    cprec_header->compressedPatchCount = CPrec_CompressPatches();
    cprec_header->compressedPatchSize = cprec_glist.size;
    cprec_header->compressedPatchOffset = offset;

    if (cprec_header->compressedPatchCount) {
        if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
            return err;
        }
        offset += cprec_glist.size;
    }

    if ((err = CPrec_FileAlign(cprec_refnum, &offset)) != noErr) {
        return err;
    }

    cprec_glist.size = 0;
    CPrec_GenerateBuiltinPatches();
    cprec_header->builtinPatchSize = cprec_glist.size;
    cprec_header->builtinPatchOffset = offset;

    if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
        return err;
    }
    offset += cprec_glist.size;

    if (cprec_tokenpatches) {
        cprec_glist.size = 0;
        CPrec_GenerateTokenStreamPatches();
        cprec_header->tokenStreamPatchSize = cprec_glist.size;
        cprec_header->tokenStreamPatchOffset = offset;

        if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
            return err;
        }
        offset += cprec_glist.size;
    }

#if VERSION == VERSION_WII_1_0RC1
    cprec_glist.size = 0;
    CodeGen_WritePrecompilerData(&cprec_glist);
    cprec_header->backendSize = cprec_glist.size;
    cprec_header->backendOffset = offset;
    if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
        return err;
    }
    offset += cprec_glist.size;
    if (cprec_sectionpatches) {
        Patch* patch;
        cprec_glist.size = 0;
        for (patch = cprec_sectionpatches; patch; patch = patch->next) {
            AppendGListLong(&cprec_glist, patch->offset);
        }
        cprec_header->sectionPatchSize = cprec_glist.size;
        cprec_header->sectionPatchOffset = offset;
        if ((err = COS_FileWrite(cprec_refnum, *cprec_glist.data, cprec_glist.size)) != noErr) {
            return err;
        }
    }
#endif

    if ((err = COS_FileSetPos(cprec_refnum, 0)) != noErr) {
        return err;
    }

    if ((err = COS_FileWrite(cprec_refnum, cprec_header, sizeof(Header))) != noErr) {
        return err;
    }

    return noErr;
}

void PrecompilerWrite(void) {
    OSErr err;
    short strindex;
    CWFileSpec spec;
    char str[128];

    spec = cparamblkptr->sourcefile;
    if (CWGetPrecompiledHeaderSpec(cparamblkptr->context, &spec, precomp_target_str) != cwNoErr) {
        return;
    }

    strindex = 3;
    err = COS_FileNew(&spec, &cprec_refnum, copts.appltype, copts.headtype);
    if (err == noErr) {
        strindex = 4;
        err = CPrec_WriteFile();
    }
    CleanupPrecompiler();

    if (err != noErr) {
        CompilerGetCString(strindex, str);
        sprintf(string, str, err);
        CWReportMessage(cparamblkptr->context, NULL, string, NULL, messagetypeError, 0);
    } else {
        CWFileTime time = 0;
        CWSetModDate(cparamblkptr->context, &spec, &time, TRUE);
    }
}

static void CPrec_ReadData(SInt32 offset, void* buffer, SInt32 size) {
    if (COS_FileSetPos(cprec_refnum, offset) != noErr ||
        COS_FileRead(cprec_refnum, buffer, size) != noErr) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
}

#if VERSION == VERSION_WII_1_0RC1
static const UInt8* CPrec_ReadRawSection(UInt32 imageSize, UInt32 offset, UInt32 size) {
    if (offset < sizeof(Header) || offset > imageSize || size > imageSize - offset) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    if (cprec_buffer) {
        return (const UInt8*)cprec_buffer + offset;
    } else {
        UInt8* data = lalloc(size);
        CPrec_ReadData(offset, data, size);
        return data;
    }
}

static void CPrec_ReadRawBuffer(UInt32 imageSize) {
    UInt32 rawOffset = cprec_header->rawdataoffset;
    UInt32 storedSize = cprec_header->rawdatasize;
    UInt32 rawSize = cprec_header->rawsize;
    const UInt8* input;

    if (rawSize > 0x7fffffffu || ((rawSize + 31) >> 5) > storedSize) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }

    input = CPrec_ReadRawSection(imageSize, rawOffset, storedSize);
    cprec_rawbuffer = galloc(rawSize);
    if (!CPrec_ExpandRawData(input, storedSize, cprec_rawbuffer, rawSize)) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
}

static void CPrec_ReadBackend(UInt32 imageSize) {
    PrecompilerSectionMap remap;
    UInt32 size = cprec_header->backendSize;
    const UInt8* data;
    UInt32 i;

    if (!size) {
        return;
    }
    data = CPrec_ReadRawSection(imageSize, cprec_header->backendOffset, size);
    if (!CodeGen_ReadPrecompilerData(data, size, &remap)) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    size = cprec_header->sectionPatchSize;
    if (!size) {
        return;
    }
    if (size & 3) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    data = CPrec_ReadRawSection(imageSize, cprec_header->sectionPatchOffset, size);
    for (i = 0; i < size; i += 4) {
        UInt32 offset = CIRStream_ReadWord(data + i);
        UInt16* field;
        SInt16 index;

        if (offset > cprec_header->rawsize ||
            cprec_header->rawsize - offset < sizeof(*field) ||
            (offset & 1)) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
        field = (UInt16*)((UInt8*)cprec_rawbuffer + offset);
        index = (SInt16)*field;
        if (index < 0) {
            UInt32 selector = -(SInt32)index;
            if (selector > remap.selectorCount) {
                CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
            }
            *field = remap.selectors[selector];
        } else {
            if ((UInt32)index >= remap.attributeCount) {
                CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
            }
            *field = remap.attributes[index];
        }
    }
}
static int CPrec_InstallStockMacro(const CPrecStockMacro* entry, void* context) {
    UInt32 count = entry->argumentCount ? entry->argumentCount - 1 : 0;
    size_t remaining = entry->replacementSize;
    const UInt8* stream = entry->replacement;
    Macro* macro;
    char* text = NULL;
    UInt32 length = 0;
    UInt32 i;
    Boolean pasting = FALSE;
    Boolean lastComma = FALSE;

    (void)context;
    if (count > 127 || ((entry->flags & CPrecStockMacroVariadic) && !count)) {
        return 0;
    }
    macro = galloc(offsetof(Macro, names) + count * sizeof(macro->names[0]));
    macro->name = GetHashNameNodeExport(entry->name);
    macro->nargs = entry->argumentCount;
    if (entry->flags & CPrecStockMacroVariadic) {
        macro->nargs |= 0x8000;
    }
    macro->is_special = FALSE;
    macro->flags = (entry->flags & CPrecStockMacroGnuVariadic) ? MacroGnuVariadic : 0;
    for (i = 0; i < count; ++i) {
        const char* name = CPrec_StockMacroArgument(entry, i);
        if (!name || !*name) {
            return 0;
        }
        macro->names[i] = GetHashNameNodeExport(name);
    }
    if (remaining) {
        UInt32 capacity = remaining < 0x10000 ? remaining * 2 + 1 : 0x20000;
        text = galloc(capacity);
        while (remaining) {
            CPrecMacroToken token;
            Boolean space;
            Boolean gnuComma;
            size_t prefix;
            size_t bytes;
            if (!decodemacrotoken(&stream, &remaining, &token)) {
                return 0;
            }
            if (token.kind == CPrecMacroPaste && !(token.flags & CPrecMacroParameter)) {
                if (!length || pasting || !remaining) {
                    return 0;
                }
                pasting = TRUE;
                continue;
            }
            space = length && !pasting && (token.flags & CPrecMacroLeadingSpace);
            gnuComma = pasting && lastComma && (macro->flags & MacroGnuVariadic) &&
                (token.flags & CPrecMacroParameter) && (UInt32)token.kind == count;
            pasting = FALSE;
            lastComma = token.kind == ',' && !(token.flags & CPrecMacroParameter);
            if (gnuComma) {
                --length;
            }
            prefix = token.quote ? strlen(token.prefix) : 0;
            bytes = token.flags & CPrecMacroParameter ? 2
                : token.spelling ? token.length + prefix + (token.quote ? 2 : 0)
                                 : 1;
            if (bytes >= capacity - length - space) {
                return 0;
            }
            if (space) {
                text[length++] = ' ';
            }
            if (token.flags & CPrecMacroParameter) {
                if (token.kind < 1 || (UInt32)token.kind > count) {
                    return 0;
                }
                if (gnuComma) {
                    text[length++] = TK_MACRO_ARG_GNU_COMMA;
                } else if (token.flags & CPrecMacroStringify) {
                    text[length++] = TK_MACRO_ARG_HASH;
                } else if (token.flags & CPrecMacroUnexpanded) {
                    text[length++] = TK_MACRO_ARG_PASTED;
                } else {
                    text[length++] = TK_MACRO_ARG_EXPANDED;
                }
                text[length++] = token.kind;
            } else if (token.spelling) {
                size_t j;
                for (j = 0; j < token.length; ++j) {
                    unsigned char byte = token.spelling[j];
                    if (byte >= TK_MACRO_ARG_EXPANDED && byte <= TK_MACRO_ARG_GNU_COMMA) {
                        return 0;
                    }
                }
                if (token.quote) {
                    memcpy(text + length, token.prefix, prefix);
                    length += prefix;
                    text[length++] = token.quote;
                }
                memcpy(text + length, token.spelling, token.length);
                length += token.length;
                if (token.quote) {
                    text[length++] = token.quote;
                }
            } else {
                text[length++] = token.kind;
            }
        }
        text[length] = 0;
    }
    macro->text = length ? text : NULL;
    CPrep_InstallPrecompiledMacro(macro);
    return 1;
}

typedef struct CPrecStockType {
    struct CPrecStockType* next;
    struct CPrecStockType* pending;
    UInt32 offset;
    Type* type;
    UInt32 pointerDepth;
    UInt32 aggregateDepth;
    Boolean decoding;
} CPrecStockType;

typedef struct CPrecStockObject {
    struct CPrecStockObject* next;
    UInt32 offset;
    ObjBase* object;
    Boolean pendingData;
} CPrecStockObject;

typedef struct CPrecStockScope {
    struct CPrecStockScope* next;
    struct CPrecStockScope* pending;
    const UInt8* record;
    NameSpace* scope;
} CPrecStockScope;

typedef struct CPrecStockExpression {
    struct CPrecStockExpression* next;
    UInt32 offset;
    ENode* node;
} CPrecStockExpression;

typedef struct CPrecStockDefaultArgument {
    struct CPrecStockDefaultArgument* next;
    FuncArg* argument;
    UInt32 offset;
} CPrecStockDefaultArgument;

typedef struct CPrecStockFunction {
    struct CPrecStockFunction* next;
    Object* object;
    const UInt8* package;
} CPrecStockFunction;

typedef struct CPrecStockGraph {
    const UInt8* raw;
    UInt32 rawSize;
    UInt8* builtinIndices;
    PrecompilerSectionMap sections;
    CPrecStockType* types[256];
    CPrecStockObject* objects[256];
    CPrecStockScope* scopes[256];
    CPrecStockExpression* expressions[256];
    CPrecStockType* pendingTemplateClasses;
    CPrecStockType* pendingClassClones;
    CPrecStockScope* pendingScopes;
    CPrecStockDefaultArgument* pendingDefaults;
    CPrecStockFunction* pendingFunctions;
    UInt32 objectsRemaining;
    UInt32 dataDefinitions;
    UInt32 functionTemplates;
    UInt32 functionInstances;
    UInt32 pointerDepth;
    UInt32 aggregateDepth;
} CPrecStockGraph;

enum {
    StockBuiltinRoot = 0,
    StockBuiltinEllipsis = 25,
    StockBuiltinOldStyle = 26,
    StockTypeEnum = 4,
    StockTypeStruct = 5,
    StockTypeClass = 6,
    StockTypeFunction = 7,
    StockTypeBitfield = 8,
    StockTypeTemplate = 10,
    StockTypeMemberPointer = 11,
    StockTypePointer = 12,
    StockTypeArray = 13,
    StockDependentEnumValue = 8,
    StockTemplateTemplateParam = 11,
    StockTemplateUsingStorage = 0x166,
    StockFunctionDeferredDefinition = 0x800000,
    StockFunctionCppLinkage = 0x40000000,
    StockClassTrivialCopy = 0x20000,
    StockClassTemplateClone = 0x200,
    StockClassTemplateHeader = 0x34,
    StockClassTemplateSize = 0x56,
    StockClassInstanceSize = 0x48,
    StockFunctionInstanceSize = 0x1c,
    StockTemplateActionSize = 0x1c,
    StockExpressionTemplate = 0x4c,
    StockExpressionSize = 0x2e,
    StockExpressionDepth = 256
};

static Type* CPrec_StockBuiltinType(int index) {
    static TypePointer charPointer = {TYPEPOINTER, 4, TYPE(&stchar), 0};
    // The stock order comes from the builtin registration function at 0x47b350.
    static Type* const types[] = {
        [1] = &stvoid,
        [2] = TYPE(&stbool),
        [3] = TYPE(&stchar),
        [4] = TYPE(&stsignedchar),
        [5] = TYPE(&stunsignedchar),
        [6] = TYPE(&stwchar),
        [7] = TYPE(&stsignedshort),
        [8] = TYPE(&stunsignedshort),
        [9] = TYPE(&stsignedint),
        [10] = TYPE(&stunsignedint),
        [11] = TYPE(&stsignedlong),
        [12] = TYPE(&stunsignedlong),
        [13] = TYPE(&stsignedlonglong),
        [14] = TYPE(&stunsignedlonglong),
        [15] = TYPE(&stfloat),
        [16] = TYPE(&stshortdouble),
        [17] = TYPE(&stdouble),
        [18] = TYPE(&stlongdouble),
        [27] = &stillegal,
        [28] = &sttemplexpr,
        [29] = &stvoid,
        [30] = TYPE(&void_ptr),
        [31] = TYPE(&charPointer),
        [32] = TYPE(&rt_func),
        [33] = TYPE(&catchinfostruct),
        [34] = TYPE(&stvectorunsignedchar),
        [35] = TYPE(&stvectorsignedchar),
        [36] = TYPE(&stvectorboolchar),
        [37] = TYPE(&stvectorunsignedshort),
        [38] = TYPE(&stvectorsignedshort),
        [39] = TYPE(&stvectorboolshort),
        [40] = TYPE(&stvectorunsignedlong),
        [41] = TYPE(&stvectorsignedlong),
        [42] = TYPE(&stvectorboollong),
        [43] = TYPE(&stvectorfloat),
        [44] = TYPE(&stvectorpixel),
        [45] = TYPE(&stvec2x32float),
        [46] = TYPE(&stspevector[0]),
        [47] = TYPE(&stspevector[1]),
        [48] = TYPE(&stspevector[2]),
        [49] = TYPE(&stspevector[3]),
        [50] = TYPE(&stspevector[4]),
        [51] = TYPE(&stspevector[5]),
        [52] = TYPE(&stspevector[6]),
        [53] = TYPE(&stspevector[7])
    };
    return (unsigned int)index < sizeof(types) / sizeof(types[0]) ? types[index] : NULL;
}

static const UInt8* CPrec_StockRecord(const CPrecStockGraph* graph, UInt32 offset, UInt32 size) {
    if (!offset || (offset & 1) || offset > graph->rawSize || size > graph->rawSize - offset) {
        return NULL;
    }
    return graph->raw + offset;
}

static UInt16 CPrec_StockHalfword(const UInt8* field) {
    return (UInt16)(field[0] | (field[1] << 8));
}

static Boolean CPrec_ReadStockParamID(const UInt8* record, TemplParamID* pid) {
    if (record[3] > TPT_TEMPLATE) {
        return FALSE;
    }
    pid->index = CPrec_StockHalfword(record);
    pid->nindex = record[2];
    pid->type = (TemplParamType)record[3];
    return TRUE;
}

static int CPrec_StockBuiltin(const CPrecStockGraph* graph, const UInt8* slot) {
    return (int)graph->builtinIndices[(slot - graph->raw) / 2] - 1;
}

static Boolean CPrec_ReadStockName(
    const CPrecStockGraph* graph, const UInt8* slot, HashNameNode** name
) {
    UInt32 offset = CIRStream_ReadWord(slot);
    const char* text;

    *name = NULL;
    if (offset) {
        text = CPrec_StockHashName(graph->raw, graph->rawSize, offset);
        if (!text) {
            return FALSE;
        }
        *name = GetHashNameNodeExport(text);
    }
    return TRUE;
}

static Type* CPrec_ReadStockType(CPrecStockGraph* graph, const UInt8* slot, UInt32 depth);
static Type* CPrec_ReadStockTypeAt(CPrecStockGraph* graph, UInt32 offset, UInt32 depth);
static Object* CPrec_ReadStockSymbol(CPrecStockGraph* graph, UInt32 offset, UInt32 depth);
static ENode* CPrec_ReadStockExpression(CPrecStockGraph* graph, UInt32 offset, UInt32 depth);
static Boolean CPrec_ReadStockTemplateMember(CPrecStockGraph* graph, UInt32 offset, Object* object);
static Boolean CPrec_ReadStockTemplateArgs(
    CPrecStockGraph* graph, UInt32 offset, TemplArg** tail, UInt32 depth
);

static Type* CPrec_ReadStockEmbeddedTemplate(CPrecStockGraph* graph, UInt32 offset, UInt32 depth) {
    return offset > StockClassTemplateHeader
        ? CPrec_ReadStockTypeAt(graph, offset - StockClassTemplateHeader, depth)
        : NULL;
}

static NameSpace* CPrec_ReadStockScope(CPrecStockGraph* graph, const UInt8* slot, UInt32 depth) {
    int builtin = CPrec_StockBuiltin(graph, slot);
    UInt32 offset = CIRStream_ReadWord(slot);
    const UInt8* record;
    CPrecStockScope** bucket;
    CPrecStockScope* entry;
    HashNameNode* name;
    NameSpace* parent;

    if (builtin >= 0) {
        return builtin == StockBuiltinRoot ? cscope_root : NULL;
    }
    if (depth >= 128) {
        return NULL;
    }
    record = CPrec_StockRecord(graph, offset, 0x24);
    if (!record || record[0x21] != 1 || record[0x23] || CIRStream_ReadWord(record + 0x10)) {
        return NULL;
    }
    bucket = &graph->scopes[(offset >> 2) % (sizeof(graph->scopes) / sizeof(graph->scopes[0]))];
    for (entry = *bucket; entry; entry = entry->next) {
        if (entry->record == record) {
            return entry->scope;
        }
    }
    entry = lalloc(sizeof(*entry));
    entry->record = record;
    entry->scope = NULL;
    entry->next = *bucket;
    *bucket = entry;
    parent = CPrec_ReadStockScope(graph, record, depth + 1);
    if (!parent || !CPrec_ReadStockName(graph, record + 4, &name)) {
        return NULL;
    }
    entry->scope =
        record[0x20] ? CScope_NewHashNameSpace(name) : CScope_NewListNameSpace(name, TRUE);
    entry->scope->parent = parent;
    entry->scope->is_unnamed = record[0x22] != 0;
    entry->pending = graph->pendingScopes;
    graph->pendingScopes = entry;
    return entry->scope;
}

static Boolean CPrec_ReadStockFuncArgs(
    CPrecStockGraph* graph, const UInt8* slot, FuncArg** tail, UInt32 depth
) {
    UInt32 remaining = graph->rawSize / 0x1c;
    for (;;) {
        int builtin = CPrec_StockBuiltin(graph, slot);
        UInt32 offset = CIRStream_ReadWord(slot);
        const UInt8* record;
        FuncArg* arg;

        if (builtin >= 0) {
            if (builtin != StockBuiltinEllipsis && builtin != StockBuiltinOldStyle) {
                return FALSE;
            }
            *tail = builtin == StockBuiltinEllipsis ? &elipsis : &oldstyle;
            return TRUE;
        }
        if (!offset) {
            return TRUE;
        }
        record = CPrec_StockRecord(graph, offset, 0x1c);
        if (!record || !remaining--) {
            return FALSE;
        }
        arg = CParser_NewFuncArg();
        arg->type = CPrec_ReadStockType(graph, record + 0x0c, depth);
        if (!arg->type || !CPrec_ReadStockName(graph, record + 4, &arg->name)) {
            return FALSE;
        }
        arg->qual = CIRStream_ReadWord(record + 0x10);
        arg->sclass = (SInt16)CPrec_StockHalfword(record + 0x18);
        arg->is_array = record[0x1a] != 0;
        if (CIRStream_ReadWord(record + 8)) {
            CPrecStockDefaultArgument* pending = lalloc(sizeof(*pending));
            pending->argument = arg;
            pending->offset = CIRStream_ReadWord(record + 8);
            pending->next = graph->pendingDefaults;
            graph->pendingDefaults = pending;
        }
        *tail = arg;
        tail = &arg->next;
        slot = record;
    }
}

static Boolean CPrec_ReadStockExceptions(
    CPrecStockGraph* graph, UInt32 offset, ExceptSpecList** tail, UInt32 depth
) {
    UInt32 remaining = graph->rawSize / 12;
    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 12);
        ExceptSpecList* entry;

        if (!record || !remaining--) {
            return FALSE;
        }
        entry = galloc(sizeof(*entry));
        entry->next = NULL;
        entry->type = NULL;
        entry->qual = CIRStream_ReadWord(record + 8);
        offset = CIRStream_ReadWord(record);
        if (CIRStream_ReadWord(record + 4) || CPrec_StockBuiltin(graph, record + 4) >= 0) {
            entry->type = CPrec_ReadStockType(graph, record + 4, depth);
            if (!entry->type) {
                return FALSE;
            }
        } else if (offset) {
            return FALSE;
        }
        *tail = entry;
        tail = &entry->next;
    }
    return TRUE;
}

static CPrecStockObject** CPrec_FindStockObject(CPrecStockGraph* graph, UInt32 offset) {
    CPrecStockObject** link =
        &graph->objects[(offset >> 2) % (sizeof(graph->objects) / sizeof(graph->objects[0]))];
    while (*link && (*link)->offset != offset) {
        link = &(*link)->next;
    }
    return link;
}

static void CPrec_RememberStockObject(CPrecStockObject** link, UInt32 offset, ObjBase* object) {
    CPrecStockObject* entry = lalloc(sizeof(*entry));
    entry->next = *link;
    entry->offset = offset;
    entry->object = object;
    entry->pendingData = FALSE;
    *link = entry;
}

static const UInt8* CPrec_StockDependentEnumValue(
    CPrecStockGraph* graph, const UInt8* slot, UInt32 enumOffset
) {
    const UInt8* record = CPrec_StockRecord(graph, CIRStream_ReadWord(slot), 0x10);
    return record &&
            record[0] == StockTypeTemplate &&
            record[6] == StockDependentEnumValue &&
            CIRStream_ReadWord(record + 8) == enumOffset
        ? record
        : NULL;
}

static Type* CPrec_ReadStockEnum(
    CPrecStockGraph* graph, const UInt8* record, SInt32 size, UInt32 depth
) {
    TypeEnum* result = galloc(sizeof(*result));
    ObjEnumConst** tail = &result->enumlist;
    UInt32 offset = CIRStream_ReadWord(record + 0x0a);
    UInt32 enumOffset = (UInt32)(record - graph->raw);
    Boolean isTemplate;

    memset(result, 0, sizeof(*result));
    result->type = TYPEENUM;
    result->size = size;
    result->nspace = CPrec_ReadStockScope(graph, record + 6, 0);
    result->enumtype = CPrec_ReadStockType(graph, record + 0x0e, depth);
    if (!result->nspace ||
        !result->enumtype ||
        !IS_TYPE_INT(result->enumtype) ||
        size != result->enumtype->size ||
        !CPrec_ReadStockName(graph, record + 0x12, &result->enumname)) {
        return NULL;
    }
    isTemplate = result->nspace->theclass && IS_TEMPL_CLASS(TYPE(result->nspace->theclass));
    while (offset) {
        const UInt8* enumerator = CPrec_StockRecord(graph, offset, 0x16);
        CPrecStockObject** link = CPrec_FindStockObject(graph, offset);
        ObjEnumConst* object;
        if (!enumerator || *link || enumerator[0] != OT_ENUMCONST || enumerator[1] > ACCESSNONE) {
            return NULL;
        }
        if (CIRStream_ReadWord(enumerator + 0x0a) != enumOffset) {
            Type* builtin = CPrec_StockBuiltinType(CPrec_StockBuiltin(graph, enumerator + 0x0a));
            if (!isTemplate ||
                ((!builtin || !IS_TYPE_INT(builtin)) &&
                 !CPrec_StockDependentEnumValue(graph, enumerator + 0x0a, enumOffset))) {
                return NULL;
            }
        }
        object = galloc(sizeof(*object));
        memset(object, 0, sizeof(*object));
        if (!CPrec_ReadStockName(graph, enumerator + 6, &object->name) || !object->name) {
            return NULL;
        }
        object->otype = OT_ENUMCONST;
        object->access = enumerator[1];
        object->type = TYPE(result);
        object->val.hi = (SInt32)CIRStream_ReadWord(enumerator + 0x0e);
        object->val.lo = CIRStream_ReadWord(enumerator + 0x12);
        *tail = object;
        tail = &object->next;
        CPrec_RememberStockObject(link, offset, OBJ_BASE(object));
        offset = CIRStream_ReadWord(enumerator + 2);
    }
    return TYPE(result);
}

static Boolean CPrec_ReadStockMembers(
    CPrecStockGraph* graph, UInt32 offset, TypeStruct* result, UInt32 depth
) {
    StructMember** tail = &result->members;
    UInt32 remaining = graph->rawSize / 0x18;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x18);
        StructMember* member;
        if (!record || !remaining--) {
            return FALSE;
        }
        member = galloc(sizeof(*member));
        memset(member, 0, sizeof(*member));
        member->type = CPrec_ReadStockType(graph, record + 4, depth);
        member->offset = (SInt32)CIRStream_ReadWord(record + 0x10);
        member->qual = CIRStream_ReadWord(record + 0x14);
        if (!member->type ||
            member->offset < 0 ||
            member->offset > result->size ||
            !CPrec_ReadStockName(graph, record + 8, &member->name)) {
            return FALSE;
        }
        *tail = member;
        tail = &member->next;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockBases(
    CPrecStockGraph* graph, UInt32 offset, TypeClass* result, UInt32 depth
) {
    ClassList** tail = &result->bases;
    UInt32 remaining = graph->rawSize / 0x12;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x12);
        ClassList* base;
        Type* target;
        if (!record || !remaining-- || record[0x10] > ACCESSNONE || record[0x11] > 1) {
            return FALSE;
        }
        target = CPrec_ReadStockType(graph, record + 4, depth);
        if (!target ||
            !IS_TYPE_CLASS(target) ||
            CIRStream_ReadWord(record + 8) > (UInt32)result->size) {
            return FALSE;
        }
        base = galloc(sizeof(*base));
        memset(base, 0, sizeof(*base));
        base->base = TYPE_CLASS(target);
        base->offset = (SInt32)CIRStream_ReadWord(record + 8);
        base->voffset = (SInt32)CIRStream_ReadWord(record + 0x0c);
        base->access = record[0x10];
        base->is_virtual = record[0x11];
        *tail = base;
        tail = &base->next;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockVirtualBases(
    CPrecStockGraph* graph, UInt32 offset, TypeClass* result, UInt32 depth
) {
    VClassList** tail = &result->vbases;
    UInt32 remaining = graph->rawSize / 0x12;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x12);
        VClassList* base;
        Type* target;
        if (!record || !remaining--) {
            return FALSE;
        }
        target = CPrec_ReadStockType(graph, record + 4, depth);
        if (!target ||
            !IS_TYPE_CLASS(target) ||
            CIRStream_ReadWord(record + 8) > (UInt32)result->size) {
            return FALSE;
        }
        base = galloc(sizeof(*base));
        memset(base, 0, sizeof(*base));
        base->base = TYPE_CLASS(target);
        base->offset = (SInt32)CIRStream_ReadWord(record + 8);
        base->voffset = (SInt32)CIRStream_ReadWord(record + 0x0c);
        base->has_override = record[0x10] != 0;
        *tail = base;
        tail = &base->next;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockVTable(
    CPrecStockGraph* graph, TypeClass* result, UInt32 offset, UInt32 depth
) {
    const UInt8* record;
    Object* object;
    SInt32 size;
    SInt32 displacement;

    if (!offset) {
        return TRUE;
    }
    record = CPrec_StockRecord(graph, offset, 0x10);
    if (!record) {
        return FALSE;
    }
    object = CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(record), depth);
    size = (SInt32)CIRStream_ReadWord(record + 4);
    displacement = (SInt32)CIRStream_ReadWord(record + 0x0c);
    if (!object ||
        object->datatype != DDATA ||
        !object->nspace->theclass ||
        size < 0 ||
        object->type->size != size ||
        displacement < 0 ||
        displacement > result->size) {
        return FALSE;
    }
    CABI_AddVTable(result);
    result->vtable->object = object;
    result->vtable->owner = object->nspace->theclass;
    result->vtable->size = size;
    result->vtable->offset = displacement;
    if (result->action == CLASS_ACTION_NONE && result->vtable->owner == result) {
        CClass_MakeStaticActionClass(result);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockClassMembers(
    CPrecStockGraph* graph, UInt32 offset, TypeClass* result, UInt32 depth
) {
    ObjMemberVar** tail = &result->ivars;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x1c);
        CPrecStockObject** link = CPrec_FindStockObject(graph, offset);
        ObjMemberVar* member;
        if (!record || *link || record[0] != OT_MEMBERVAR || record[1] > ACCESSNONE || record[3]) {
            return FALSE;
        }
        member = galloc(sizeof(*member));
        memset(member, 0, sizeof(*member));
        member->otype = OT_MEMBERVAR;
        member->access = record[1];
        member->anonunion = record[2];
        member->type = CPrec_ReadStockType(graph, record + 0x10, depth);
        member->qual = CIRStream_ReadWord(record + 0x14);
        member->offset = CIRStream_ReadWord(record + 0x18);
        if (!member->type ||
            (!(result->flags & CLASS_IS_TEMPL) && member->offset > (UInt32)result->size) ||
            !CPrec_ReadStockName(graph, record + 8, &member->name)) {
            return FALSE;
        }
        *tail = member;
        tail = &member->next;
        CPrec_RememberStockObject(link, offset, OBJ_BASE(member));
        offset = CIRStream_ReadWord(record + 4);
    }
    return TRUE;
}

static Type* CPrec_ReadStockClass(
    CPrecStockGraph* graph, CPrecStockType* entry, const UInt8* record, SInt32 size, UInt32 depth
) {
    UInt32 flags = CIRStream_ReadWord(record + 0x22);
    UInt16 align = CPrec_StockHalfword(record + 0x2a);
    UInt32 scopeOffset = CIRStream_ReadWord(record + 6);
    const UInt8* scope = CPrec_StockRecord(graph, scopeOffset, 0x24);
    TypeClass* result;
    size_t classSize = sizeof(TypeClass);
    Boolean valid;

    if (flags & CLASS_IS_TEMPL) {
        if (!CPrec_StockRecord(graph, entry->offset, StockClassTemplateSize) ||
            (flags & CLASS_IS_TEMPL_INST)) {
            return NULL;
        }
        classSize = sizeof(TemplClass);
        if (flags & StockClassTemplateClone) {
            // RC1 gives a nested template proxy an empty scope under the outer instance.
            if (!scope ||
                CIRStream_ReadWord(scope + 0x0c) != entry->offset ||
                CIRStream_ReadWord(scope + 0x1c) ||
                !CIRStream_ReadWord(record + 0x38)) {
                return NULL;
            }
            result = galloc(classSize);
            memset(result, 0, classSize);
            result->type = TYPECLASS;
            result->flags = (UInt16)flags;
            entry->pending = graph->pendingClassClones;
            graph->pendingClassClones = entry;
            return TYPE(result);
        }
    } else if (flags & CLASS_IS_TEMPL_INST) {
        if (!CPrec_StockRecord(graph, entry->offset, StockClassInstanceSize)) {
            return NULL;
        }
        classSize = sizeof(TemplClassInst);
    }
    if (!scope ||
        CIRStream_ReadWord(scope + 0x0c) != entry->offset ||
        size < 0 ||
        (!align && (!(flags & CLASS_IS_TEMPL_INST) || (flags & CLASS_COMPLETED))) ||
        align > 0x4000 ||
        (align & (align - 1)) ||
        record[0x2e] > CLASS_MODE_CLASS ||
        record[0x2f] > CLASS_ACTION_JAVA ||
        CIRStream_ReadWord(record + 0x1e)) {
        return NULL;
    }
    result = galloc(classSize);
    memset(result, 0, classSize);
    result->type = TYPECLASS;
    result->size = size;
    result->align = align;
    result->flags = (UInt16)flags;
    result->mode = record[0x2e];
    result->action = record[0x2f];
    result->eflags = record[0x2c];
    if (flags & CLASS_COMPLETED) {
        result->irCopyKind = flags & StockClassTrivialCopy ? ClassCopyTrivial : ClassCopyNontrivial;
    }
    result->nspace = CPrec_ReadStockScope(graph, record + 6, 0);
    if (!result->nspace ||
        result->nspace->theclass ||
        !CPrec_ReadStockName(graph, record + 0x0a, &result->classname)) {
        return NULL;
    }
    result->nspace->theclass = result;
    entry->type = TYPE(result);
    ++graph->aggregateDepth;
    valid = CPrec_ReadStockBases(graph, CIRStream_ReadWord(record + 0x0e), result, depth) &&
        CPrec_ReadStockVirtualBases(graph, CIRStream_ReadWord(record + 0x12), result, depth) &&
        CPrec_ReadStockClassMembers(graph, CIRStream_ReadWord(record + 0x16), result, depth) &&
        CPrec_ReadStockVTable(graph, result, CIRStream_ReadWord(record + 0x1a), depth);
    --graph->aggregateDepth;
    if (valid && (flags & CLASS_IS_TEMPL_ANY)) {
        entry->pending = graph->pendingTemplateClasses;
        graph->pendingTemplateClasses = entry;
    }
    return valid ? TYPE(result) : NULL;
}

static Type* CPrec_DecodeStockType(CPrecStockGraph* graph, CPrecStockType* entry, UInt32 depth) {
    UInt32 offset = entry->offset;
    const UInt8* record = CPrec_StockRecord(graph, offset, 6);
    UInt8 kind;
    SInt32 size;

    if (!record) {
        return NULL;
    }
    kind = record[0];
    size = (SInt32)CIRStream_ReadWord(record + 2);
    if (kind == StockTypeEnum) {
        return CPrec_StockRecord(graph, offset, 0x16)
            ? CPrec_ReadStockEnum(graph, record, size, depth)
            : NULL;
    }
    if (kind == StockTypeClass) {
        return CPrec_StockRecord(graph, offset, 0x30)
            ? CPrec_ReadStockClass(graph, entry, record, size, depth)
            : NULL;
    }
    if (kind == StockTypeStruct) {
        TypeStruct* result;
        UInt16 align;
        Boolean valid;
        if (!CPrec_StockRecord(graph, offset, 0x12) ||
            size < 0 ||
            record[0x10] > STRUCT_TYPE_UNION) {
            return NULL;
        }
        align = CPrec_StockHalfword(record + 0x0e);
        if (!align || align > 0x4000 || (align & (align - 1))) {
            return NULL;
        }
        result = TYPE_STRUCT(CDecl_NewStructType(size, align));
        result->stype = record[0x10];
        if (!CPrec_ReadStockName(graph, record + 6, &result->name)) {
            return NULL;
        }
        entry->type = TYPE(result);
        ++graph->aggregateDepth;
        valid = CPrec_ReadStockMembers(graph, CIRStream_ReadWord(record + 0x0a), result, depth);
        --graph->aggregateDepth;
        return valid ? TYPE(result) : NULL;
    }
    if (kind == StockTypeTemplate) {
        TypeTemplDep* result;

        if (!CPrec_StockRecord(graph, offset, 0x10)) {
            return NULL;
        }
        result = galloc(sizeof(*result));
        memset(result, 0, sizeof(*result));
        result->type = TYPETEMPLATE;
        result->size = size;
        result->dtype = record[6] == StockTemplateTemplateParam ? TEMPLDEP_ARGUMENT
                                                                : (TypeTemplDepType)record[6];
        switch (result->dtype) {
            case TEMPLDEP_ARGUMENT:
                return CPrec_ReadStockParamID(record + 8, &result->u.pid) ? TYPE(result) : NULL;
            case TEMPLDEP_QUALNAME: {
                Type* parent = CPrec_ReadStockType(graph, record + 8, depth);
                if (!parent ||
                    !IS_TYPE_TEMPLATE(parent) ||
                    !CPrec_ReadStockName(graph, record + 0x0c, &result->u.qual.name) ||
                    !result->u.qual.name) {
                    return NULL;
                }
                result->u.qual.type = TYPE_TEMPLATE(parent);
                return TYPE(result);
            }
            case TEMPLDEP_TEMPLATE: {
                Type* target = CPrec_ReadStockType(graph, record + 8, depth);
                if (!target ||
                    !IS_TEMPL_CLASS(target) ||
                    !CPrec_ReadStockTemplateArgs(
                        graph, CIRStream_ReadWord(record + 0x0c), &result->u.templ.args, depth
                    )) {
                    return NULL;
                }
                result->u.templ.templ = TEMPL_CLASS(target);
                return TYPE(result);
            }
            case TEMPLDEP_ARRAY:
                result->u.array.type = CPrec_ReadStockType(graph, record + 8, depth);
                result->u.array.index =
                    CPrec_ReadStockExpression(graph, CIRStream_ReadWord(record + 0x0c), 0);
                return result->u.array.type && result->u.array.index ? TYPE(result) : NULL;
            case TEMPLDEP_QUALTEMPL: {
                Type* target = CPrec_ReadStockType(graph, record + 8, depth);
                if (!target ||
                    !IS_TYPE_TEMPLATE(target) ||
                    !CPrec_ReadStockTemplateArgs(
                        graph, CIRStream_ReadWord(record + 0x0c), &result->u.qualtempl.args, depth
                    )) {
                    return NULL;
                }
                result->u.qualtempl.type = TYPE_TEMPLATE(target);
                return TYPE(result);
            }
            case TEMPLDEP_BITFIELD:
                result->u.bitfield.type = CPrec_ReadStockType(graph, record + 8, depth);
                result->u.bitfield.size =
                    CPrec_ReadStockExpression(graph, CIRStream_ReadWord(record + 0x0c), 0);
                return result->u.bitfield.type && result->u.bitfield.size ? TYPE(result) : NULL;
            default:
                return NULL;
        }
    }
    if (kind == StockTypeBitfield) {
        TypeBitfield* result;
        Type* base;
        if (!CPrec_StockRecord(graph, offset, 0x0c) ||
            size < 1 ||
            size > 8 ||
            record[0x0a] + record[0x0b] > size * 8) {
            return NULL;
        }
        base = CPrec_ReadStockType(graph, record + 6, depth);
        if (!base || !IS_TYPE_INT_OR_ENUM(base) || size != base->size) {
            return NULL;
        }
        result = galloc(sizeof(*result));
        memset(result, 0, sizeof(*result));
        result->type = TYPEBITFIELD;
        result->size = size;
        result->bitfieldtype = base;
        result->offset = record[0x0a];
        result->bitlength = record[0x0b];
        return TYPE(result);
    }
    if (kind == StockTypePointer || kind == StockTypeArray) {
        TypePointer* result;
        Type* target;
        if (!CPrec_StockRecord(graph, offset, kind == StockTypeArray ? 0x14 : 0x0e) ||
            (kind == StockTypePointer ? size != 4 : size < 0)) {
            return NULL;
        }
        result = TYPE_POINTER(
            kind == StockTypePointer ? CDecl_NewPointerType(NULL) : CDecl_NewArrayType(NULL, size)
        );
        result->qual = CIRStream_ReadWord(record + 0x0a);
        entry->type = TYPE(result);
        if (kind == StockTypePointer) {
            ++graph->pointerDepth;
        }
        target = CPrec_ReadStockType(graph, record + 6, depth);
        if (kind == StockTypePointer) {
            --graph->pointerDepth;
        }
        result->target = target;
        return target ? TYPE(result) : NULL;
    }
    if (kind == StockTypeMemberPointer) {
        TypeMemberPointer* result;
        if (!CPrec_StockRecord(graph, offset, 0x12)) {
            return NULL;
        }
        result = galloc(sizeof(*result));
        memset(result, 0, sizeof(*result));
        result->type = TYPEMEMBERPOINTER;
        result->size = size;
        result->qual = CIRStream_ReadWord(record + 0x0e);
        entry->type = TYPE(result);
        ++graph->pointerDepth;
        result->ty1 = CPrec_ReadStockType(graph, record + 6, depth);
        result->ty2 = CPrec_ReadStockType(graph, record + 0x0a, depth);
        --graph->pointerDepth;
        if (!result->ty1 ||
            !result->ty2 ||
            !IS_TYPE_CLASS(result->ty2) ||
            size != (IS_TYPE_FUNC(result->ty1) ? 12 : 4)) {
            return NULL;
        }
        return TYPE(result);
    }
    if (kind == StockTypeFunction) {
        TypeFunc* result;
        UInt32 flags;
        if (!CPrec_StockRecord(graph, offset, 0x24)) {
            return NULL;
        }
        flags = CIRStream_ReadWord(record + 0x1a) &
            ~(StockFunctionDeferredDefinition | StockFunctionCppLinkage);
        if ((flags & FUNC_METHOD) && !CPrec_StockRecord(graph, offset, 0x30)) {
            return NULL;
        }
        if (flags & FUNC_METHOD) {
            TypeMemberFunc* method = galloc(sizeof(*method));
            memset(method, 0, sizeof(*method));
            result = TYPE_FUNC(method);
        } else {
            result = galloc(sizeof(*result));
            memset(result, 0, sizeof(*result));
        }
        result->type = TYPEFUNC;
        result->size = size;
        entry->type = TYPE(result);
        result->functype = CPrec_ReadStockType(graph, record + 0x0e, depth);
        result->qual = CIRStream_ReadWord(record + 0x16);
        // RC1 marks ordinary C++ function linkage separately from the declaration's name.
        result->flags = flags;
        if (!result->functype ||
            !CPrec_ReadStockFuncArgs(graph, record + 6, &result->args, depth) ||
            !CPrec_ReadStockExceptions(
                graph, CIRStream_ReadWord(record + 0x0a), &result->exspecs, depth
            )) {
            return NULL;
        }
        if (flags & FUNC_METHOD) {
            TypeMemberFunc* method = TYPE_METHOD(result);
            Type* owner = CPrec_ReadStockType(graph, record + 0x22, depth);
            if (!owner || !IS_TYPE_CLASS(owner)) {
                return NULL;
            }
            method->theclass = TYPE_CLASS(owner);
            method->vtbl_index = (SInt32)CIRStream_ReadWord(record + 0x26);
            method->funcid = (SInt32)CIRStream_ReadWord(record + 0x2a);
            method->is_static = record[0x2e] != 0;
        }
        return TYPE(result);
    }
    return NULL;
}

static Type* CPrec_ReadStockType(CPrecStockGraph* graph, const UInt8* slot, UInt32 depth) {
    int builtin = CPrec_StockBuiltin(graph, slot);
    return builtin >= 0 ? CPrec_StockBuiltinType(builtin)
                        : CPrec_ReadStockTypeAt(graph, CIRStream_ReadWord(slot), depth);
}

static Type* CPrec_ReadStockTypeAt(CPrecStockGraph* graph, UInt32 offset, UInt32 depth) {
    CPrecStockType** bucket;
    CPrecStockType* entry;

    if (!offset || depth >= 128) {
        return NULL;
    }
    bucket = &graph->types[(offset >> 2) % (sizeof(graph->types) / sizeof(graph->types[0]))];
    for (entry = *bucket; entry; entry = entry->next) {
        if (entry->offset == offset) {
            // A recursive C declarator must pass through an aggregate and a pointer.
            if (entry->decoding &&
                (entry->pointerDepth == graph->pointerDepth ||
                 entry->aggregateDepth == graph->aggregateDepth)) {
                return NULL;
            }
            return entry->type;
        }
    }
    entry = lalloc(sizeof(*entry));
    entry->offset = offset;
    entry->type = NULL;
    entry->pending = NULL;
    entry->pointerDepth = graph->pointerDepth;
    entry->aggregateDepth = graph->aggregateDepth;
    entry->decoding = TRUE;
    entry->next = *bucket;
    *bucket = entry;
    entry->type = CPrec_DecodeStockType(graph, entry, depth + 1);
    entry->decoding = FALSE;
    return entry->type;
}

static BClassList* CPrec_GetStockBasePath(Type* derived, TypeClass* base) {
    short depth;
    Boolean ambiguous;
    BClassList* path;

    if (!derived || !IS_TYPE_CLASS(derived) || !base) {
        return NULL;
    }
    path = CClass_GetBasePath(TYPE_CLASS(derived), base, &depth, &ambiguous);
    return path && !ambiguous ? CClass_GetPathCopy(path, TRUE) : NULL;
}

static Object* CPrec_ReadStockAlias(CPrecStockGraph* graph, const UInt8* record, UInt32 depth) {
    Object* target = CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(record + 0x40), depth + 1);
    BClassList* path = NULL;
    Object* alias;
    if (!target) {
        return NULL;
    }
    if (CIRStream_ReadWord(record + 0x44)) {
        Type* derived = CPrec_ReadStockType(graph, record + 0x44, depth);
        path = CPrec_GetStockBasePath(derived, target->nspace->theclass);
        if (!path) {
            return NULL;
        }
    }
    alias = galloc(sizeof(*alias));
    *alias = *target;
    alias->access = record[1];
    alias->datatype = DALIAS;
    alias->u.alias.object = target;
    alias->u.alias.member = path;
    alias->u.alias.offset = (SInt32)CIRStream_ReadWord(record + 0x48);
    return alias;
}

static Boolean CPrec_ReadStockInlineData(
    CPrecStockGraph* graph, const UInt8* record, Object* object
) {
    if (IS_TYPE_FLOAT(object->type)) {
        const UInt8* value = CPrec_StockRecord(graph, CIRStream_ReadWord(record), 8);
        Float* constant;

        if (!value) {
            return FALSE;
        }
        constant = galloc(sizeof(*constant));
        constant->data.words[0] = (SInt32)CIRStream_ReadWord(value);
        constant->data.words[1] = (SInt32)CIRStream_ReadWord(value + 4);
        object->u.data.u.floatconst = constant;
        return TRUE;
    }
    if (IS_TYPE_INT_OR_ENUM(object->type) || IS_TYPE_POINTER_ONLY(object->type)) {
        object->u.data.u.intconst.hi = (SInt32)CIRStream_ReadWord(record);
        object->u.data.u.intconst.lo = CIRStream_ReadWord(record + 4);
        return TRUE;
    }
    return FALSE;
}

static Object* CPrec_ReadStockSymbol(CPrecStockGraph* graph, UInt32 offset, UInt32 depth) {
    const UInt8* record = CPrec_StockRecord(graph, offset, 0x60);
    CPrecStockObject** saved;
    HashNameNode* name;
    HashNameNode* linkName;
    NameSpace* owner;
    Type* type;
    Object* object;
    SInt32 section;
    Boolean isFunction;
    Boolean isTemplate;
    Boolean inlineData;
    UInt32 dataDefinition;
    UInt32 functionDefinition;
    UInt32 functionInstance;
    UInt32 templateMember;
    const UInt8* package;
    UInt32 qual;

    if (depth >= 128 ||
        !record ||
        record[0] != OT_OBJECT ||
        record[1] > ACCESSNONE ||
        (record[2] != DDATA && record[2] != DFUNC && record[2] != DVFUNC && record[2] != DALIAS)) {
        return NULL;
    }
    saved = CPrec_FindStockObject(graph, offset);
    if (*saved) {
        return OBJECT((*saved)->object);
    }
    if (record[2] == DALIAS) {
        object = CPrec_ReadStockAlias(graph, record, depth);
        if (object) {
            CPrec_RememberStockObject(saved, offset, OBJ_BASE(object));
        }
        return object;
    }
    isFunction = record[2] != DDATA;
    qual = CIRStream_ReadWord(record + 0x14);
    inlineData = !isFunction && (qual & Q_INLINE_DATA);
    dataDefinition = isFunction ? 0 : CIRStream_ReadWord(record + 0x4c);
    functionDefinition = isFunction ? CIRStream_ReadWord(record + 0x40) : 0;
    functionInstance = isFunction ? CIRStream_ReadWord(record + 0x48) : 0;
    if ((dataDefinition && !CPrec_StockRecord(graph, dataDefinition, 0x1a)) ||
        CIRStream_ReadWord(record + 0x1c) ||
        (!isFunction && !inlineData && CIRStream_ReadWord(record + 0x40))) {
        return NULL;
    }
    owner = CPrec_ReadStockScope(graph, record + 8, 0);
    type = CPrec_ReadStockType(graph, record + 0x10, depth);
    isTemplate = type && IS_TEMPL_FUNC(type);
    if (functionInstance) {
        const UInt8* instance =
            CPrec_StockRecord(graph, functionInstance, StockFunctionInstanceSize);
        if (isTemplate || !instance || CIRStream_ReadWord(instance + 0x18) != offset) {
            return NULL;
        }
    }
    templateMember =
        !isTemplate && owner && owner->theclass && IS_TEMPL_CLASS(TYPE(owner->theclass))
        ? CIRStream_ReadWord(record + (isFunction ? 0x4c : 0x50))
        : 0;
    package = CPrec_StockRecord(
        graph, functionDefinition, isTemplate ? 0x36 : 0x0c + CIRStreamFunctionOptionsSize
    );
    section = (SInt32)CIRStream_ReadWord(record + 4);
    if (!owner ||
        !type ||
        isFunction != IS_TYPE_FUNC(type) ||
        ((record[0x1a] & OBJECT_DEFINED) && !dataDefinition && !package && !templateMember) ||
        (functionDefinition && !package) ||
        (isTemplate && (!package || CIRStream_ReadWord(package + 0x2e) != offset)) ||
        (isFunction && (TYPE_FUNC(type)->flags & FUNC_DEFINED) && !package && !templateMember) ||
        (record[2] == DVFUNC && !(TYPE_FUNC(type)->flags & FUNC_METHOD)) ||
        section < -(SInt32)graph->sections.selectorCount ||
        section >= (SInt32)graph->sections.attributeCount ||
        !CPrec_ReadStockName(graph, record + 0x0c, &name) ||
        !name ||
        !CPrec_ReadStockName(graph, record + (isFunction ? 0x44 : 0x48), &linkName)) {
        return NULL;
    }
    if (qual & Q_IS_TEMPLATED) {
        ObjectTemplated* templated;
        Object* parent;
        if (!CPrec_StockRecord(graph, offset, 0x64) ||
            !owner->theclass ||
            !IS_TEMPL_CLASS_INST(TYPE(owner->theclass))) {
            return NULL;
        }
        parent = CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(record + 0x60), depth + 1);
        if (!parent ||
            !parent->nspace->theclass ||
            !IS_TEMPL_CLASS(TYPE(parent->nspace->theclass))) {
            return NULL;
        }
        templated = galloc(sizeof(*templated));
        memset(templated, 0, sizeof(*templated));
        templated->parent = parent;
        object = &templated->object;
        object->otype = OT_OBJECT;
    } else {
        object = isFunction ? CParser_NewCompilerDefFunctionObject()
                            : CParser_NewCompilerDefDataObject();
    }
    object->datatype = record[2];
    object->name = name;
    object->nspace = owner;
    object->access = record[1];
    object->type = type;
    object->qual = qual;
    object->sclass = (SInt16)CPrec_StockHalfword(record + 0x18);
    object->flags = CPrec_StockHalfword(record + 0x1a);
    object->section =
        section < 0 ? graph->sections.selectors[-section] : graph->sections.attributes[section];
    if (templateMember &&
        (package ||
         dataDefinition ||
         !CPrec_ReadStockTemplateMember(graph, templateMember, object))) {
        return NULL;
    }
    if (isFunction) {
        object->u.func.linkname = linkName;
        object->u.func.interruptflags = CPrec_StockHalfword(record + 0x5c);
        if (functionInstance) {
            TYPE_FUNC(type)->flags |= FUNC_IS_TEMPL_INSTANCE;
            ++graph->functionInstances;
        }
        if (isTemplate) {
            ++graph->functionTemplates;
        } else if (package) {
            CPrecStockFunction* entry = lalloc(sizeof(*entry));
            entry->object = object;
            entry->package = package;
            entry->next = graph->pendingFunctions;
            graph->pendingFunctions = entry;
            object->flags &= ~OBJECT_DEFINED;
        }
    } else {
        object->u.data.linkname = linkName;
        if (inlineData && !CPrec_ReadStockInlineData(graph, record + 0x40, object)) {
            return NULL;
        }
    }
    CPrec_RememberStockObject(saved, offset, OBJ_BASE(object));
    if (dataDefinition) {
        (*saved)->pendingData = TRUE;
        ++graph->dataDefinitions;
    }
    return object;
}

static ObjBase* CPrec_ReadStockMemberAlias(
    CPrecStockGraph* graph, NameSpace* scope, UInt32 offset
) {
    const UInt8* record = CPrec_StockRecord(graph, offset, 0x24);
    CPrecStockObject* saved;
    Type* base;
    BClassList* path;
    ObjMemberVarPath* entry;

    if (!record || !record[3] || !scope->theclass) {
        return NULL;
    }
    base = CPrec_ReadStockType(graph, record + 0x1c, 0);
    saved = *CPrec_FindStockObject(graph, CIRStream_ReadWord(record + 0x20));
    if (!base || !IS_TYPE_CLASS(base) || !saved || saved->object->otype != OT_MEMBERVAR) {
        return NULL;
    }
    path = CPrec_GetStockBasePath(TYPE(scope->theclass), TYPE_CLASS(base));
    if (!path) {
        return NULL;
    }
    entry = galloc(sizeof(*entry));
    *OBJ_MEMBER_VAR(entry) = *OBJ_MEMBER_VAR(saved->object);
    entry->access = record[1];
    entry->has_path = TRUE;
    entry->path = path;
    return OBJ_BASE(entry);
}

static Boolean CPrec_ReadStockObject(
    CPrecStockGraph* graph, NameSpace* scope, HashNameNode* name, UInt32 offset, Object* runtime
) {
    const UInt8* record = CPrec_StockRecord(graph, offset, 2);
    const UInt8* function;
    Object* object;
    Type* type;
    NameSpace* owner;
    CPrecStockObject** saved;

    if (!record || record[1] > ACCESSNONE) {
        return FALSE;
    }
    if (record[0] == OT_TYPE) {
        ObjType* alias;
        if (!CPrec_StockRecord(graph, offset, 0x0c)) {
            return FALSE;
        }
        saved = CPrec_FindStockObject(graph, offset);
        if (*saved) {
            CScope_AddObject(scope, name, (*saved)->object);
            return TRUE;
        }
        alias = galloc(sizeof(*alias));
        alias->otype = OT_TYPE;
        alias->access = record[1];
        alias->type = CPrec_ReadStockType(graph, record + 4, 0);
        alias->qual = CIRStream_ReadWord(record + 8);
        if (!alias->type) {
            return FALSE;
        }
        CPrec_RememberStockObject(saved, offset, OBJ_BASE(alias));
        CScope_AddObject(scope, name, OBJ_BASE(alias));
        return TRUE;
    }
    if (record[0] == OT_TYPETAG) {
        ObjTypeTag* tag;
        if (!CPrec_StockRecord(graph, offset, 6)) {
            return FALSE;
        }
        type = CPrec_ReadStockType(graph, record + 2, 0);
        if (!type || (!IS_TYPE_STRUCT(type) && !IS_TYPE_ENUM(type) && !IS_TYPE_CLASS(type))) {
            return FALSE;
        }
        tag = galloc(sizeof(*tag));
        tag->otype = OT_TYPETAG;
        tag->access = record[1];
        tag->type = type;
        CScope_AddObject(scope, name, OBJ_BASE(tag));
        return TRUE;
    }
    if (record[0] == OT_NAMESPACE) {
        ObjNameSpace* ns;
        if (!CPrec_StockRecord(graph, offset, 6)) {
            return FALSE;
        }
        owner = CPrec_ReadStockScope(graph, record + 2, 0);
        if (!owner) {
            return FALSE;
        }
        ns = galloc(sizeof(*ns));
        ns->otype = OT_NAMESPACE;
        ns->access = record[1];
        ns->nspace = owner;
        CScope_AddObject(scope, name, OBJ_BASE(ns));
        return TRUE;
    }
    if (record[0] == OT_ENUMCONST) {
        CPrecStockObject* entry;
        if (!CPrec_StockRecord(graph, offset, 0x16)) {
            return FALSE;
        }
        entry = *CPrec_FindStockObject(graph, offset);
        if (!entry) {
            CPrec_ReadStockType(graph, record + 0x0a, 0);
            entry = *CPrec_FindStockObject(graph, offset);
        }
        if (!entry ||
            entry->object->otype != OT_ENUMCONST ||
            OBJ_ENUM_CONST(entry->object)->name != name) {
            return FALSE;
        }
        CScope_AddObject(scope, name, entry->object);
        return TRUE;
    }
    saved = CPrec_FindStockObject(graph, offset);
    if (*saved) {
        CScope_AddObject(scope, name, (*saved)->object);
        return TRUE;
    }
    if (record[0] == OT_MEMBERVAR) {
        ObjBase* alias = CPrec_ReadStockMemberAlias(graph, scope, offset);
        if (!alias) {
            return FALSE;
        }
        CPrec_RememberStockObject(saved, offset, alias);
        CScope_AddObject(scope, name, alias);
        return TRUE;
    }
    if (record[0] != OT_OBJECT || !CPrec_StockRecord(graph, offset, 0x60)) {
        return FALSE;
    }
    if (record[2] == DFUNC &&
        CPrec_StockHalfword(record + 0x18) == StockTemplateUsingStorage &&
        scope->theclass &&
        IS_TEMPL_CLASS(TYPE(scope->theclass)) &&
        !CIRStream_ReadWord(record + 8)) {
        // The source instantiator binds this dependent name through its using action.
        type = CPrec_ReadStockType(graph, record + 0x10, 0);
        return type &&
            IS_TYPE_TEMPLATE(type) &&
            TYPE_TEMPLATE(type)->dtype == TEMPLDEP_QUALNAME &&
            TYPE_TEMPLATE(type)->u.qual.name == name;
    }
    function = CPrec_StockRecord(graph, CIRStream_ReadWord(record + 0x10), 0x24);
    if (record[2] == DFUNC &&
        function &&
        function[0] == StockTypeFunction &&
        (CIRStream_ReadWord(function + 0x1a) & FUNC_INTRINSIC)) {
        if (runtime) {
            CPrec_RememberStockObject(saved, offset, OBJ_BASE(runtime));
        }
        return TRUE; // CParser setup has already created the target's intrinsic objects.
    }
    if (runtime &&
        CParser_IsPublicRuntimeObject(runtime) &&
        record[2] == DFUNC &&
        !CPrec_StockHalfword(record + 0x18) &&
        record[0x1a] == 0) {
        CPrec_RememberStockObject(saved, offset, OBJ_BASE(runtime));
        return TRUE;
    }
    object = CPrec_ReadStockSymbol(graph, offset, 0);
    if (!object) {
        return FALSE;
    }
    CScope_AddObject(scope, name, OBJ_BASE(object));
    return TRUE;
}

static Boolean CPrec_ReadStockNameChain(
    CPrecStockGraph* graph, NameSpace* scope, UInt32 entry, UInt32* namesRemaining
) {
    for (; entry;) {
        const UInt8* record = CPrec_StockRecord(graph, entry, 0x10);
        const UInt8* list;
        HashNameNode* name;
        Object* runtime;
        if (!record ||
            !*namesRemaining ||
            !CPrec_ReadStockName(graph, record + 4, &name) ||
            !name) {
            return FALSE;
        }
        --*namesRemaining;
        runtime = scope == cscope_root ? CParser_FindPublicRuntimeObject(name) : NULL;
        list = record + 8;
        for (;;) {
            UInt32 next = CIRStream_ReadWord(list);
            if (!graph->objectsRemaining-- ||
                !CPrec_ReadStockObject(graph, scope, name, CIRStream_ReadWord(list + 4), runtime)) {
                return FALSE;
            }
            if (!next) {
                break;
            }
            list = CPrec_StockRecord(graph, next, 8);
            if (!list) {
                return FALSE;
            }
        }
        entry = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockNameTable(
    CPrecStockGraph* graph, NameSpace* scope, const UInt8* table, UInt32 namesRemaining
) {
    UInt32 i;
    for (i = 0; i < 0x400; ++i) {
        if (!CPrec_ReadStockNameChain(
                graph, scope, CIRStream_ReadWord(table + i * 4), &namesRemaining
            )) {
            return FALSE;
        }
    }
    return namesRemaining == 0;
}

static NameSpaceName* CPrec_ReverseStockNames(NameSpaceName* list) {
    NameSpaceName* reversed = NULL;
    while (list) {
        NameSpaceName* next = list->next;
        list->next = reversed;
        reversed = list;
        list = next;
    }
    return reversed;
}

static Boolean CPrec_ReadStockUsings(CPrecStockGraph* graph, NameSpace* scope, UInt32 offset) {
    NameSpaceList** tail = &scope->usings;
    UInt32 remaining = graph->rawSize / 8;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 8);
        NameSpaceList* item;
        NameSpace* target;
        if (!record || !remaining--) {
            return FALSE;
        }
        target = CPrec_ReadStockScope(graph, record + 4, 0);
        if (!target) {
            return FALSE;
        }
        item = galloc(sizeof(*item));
        item->nspace = target;
        item->next = NULL;
        *tail = item;
        tail = &item->next;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockScopeContents(CPrecStockGraph* graph) {
    while (graph->pendingScopes) {
        CPrecStockScope* entry = graph->pendingScopes;
        const UInt8* record = entry->record;
        NameSpace* scope = entry->scope;
        UInt32 namesRemaining = CIRStream_ReadWord(record + 0x1c);
        UInt32 namesOffset = CIRStream_ReadWord(record + 0x18);
        graph->pendingScopes = entry->pending;
        if (CIRStream_ReadWord(record + 0x0c)) {
            Type* owner = CPrec_ReadStockType(graph, record + 0x0c, 0);
            if (!owner || !IS_TYPE_CLASS(owner) || TYPE_CLASS(owner)->nspace != scope) {
                return FALSE;
            }
        }
        if (!CPrec_ReadStockUsings(graph, scope, CIRStream_ReadWord(record + 8))) {
            return FALSE;
        }
        if (scope->is_hash) {
            const UInt8* table = CPrec_StockRecord(graph, namesOffset, 0x1000);
            UInt32 i;
            if (!namesOffset && !namesRemaining) {
                continue;
            }
            if (!table || !CPrec_ReadStockNameTable(graph, scope, table, namesRemaining)) {
                return FALSE;
            }
            for (i = 0; i < 0x400; ++i) {
                scope->data.hash[i] = CPrec_ReverseStockNames(scope->data.hash[i]);
            }
        } else if (!CPrec_ReadStockNameChain(graph, scope, namesOffset, &namesRemaining) ||
                   namesRemaining) {
            return FALSE;
        } else {
            scope->data.list = CPrec_ReverseStockNames(scope->data.list);
        }
    }
    return TRUE;
}

static Boolean CPrec_ReadStockClassList(CPrecStockGraph* graph, UInt32 offset) {
    UInt32 remaining = graph->rawSize / 0x0c;
    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x0c);
        Type* type;
        if (!record || !remaining--) {
            return FALSE;
        }
        type = CPrec_ReadStockType(graph, record + 4, 0);
        if (!type || !IS_TYPE_CLASS(type)) {
            return FALSE;
        }
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockFriendList(
    CPrecStockGraph* graph, TypeClass* owner, UInt32 offset, Boolean isClass, UInt32* remaining
) {
    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 8);

        if (!record || !*remaining) {
            return FALSE;
        }
        --*remaining;
        if (isClass) {
            Type* type = CPrec_ReadStockType(graph, record + 4, 0);
            if (!type || !IS_TYPE_CLASS(type)) {
                return FALSE;
            }
            CDecl_AddFriend(owner, NULL, TYPE_CLASS(type));
        } else {
            Object* function = CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(record + 4), 0);
            if (!function || !function->type || !IS_TYPE_FUNC(function->type)) {
                return FALSE;
            }
            CDecl_AddFriend(owner, function, NULL);
        }
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockFriends(CPrecStockGraph* graph, UInt32 offset) {
    UInt32 remaining = graph->rawSize / 8;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x10);
        Type* type;

        if (!record || !remaining) {
            return FALSE;
        }
        --remaining;
        type = CPrec_ReadStockType(graph, record + 4, 0);
        if (!type ||
            !IS_TYPE_CLASS(type) ||
            !CPrec_ReadStockFriendList(
                graph, TYPE_CLASS(type), CIRStream_ReadWord(record + 8), FALSE, &remaining
            ) ||
            !CPrec_ReadStockFriendList(
                graph, TYPE_CLASS(type), CIRStream_ReadWord(record + 0x0c), TRUE, &remaining
            )) {
            return FALSE;
        }
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Object* CPrec_ReadStockObjectRef(CPrecStockGraph* graph, const UInt8* slot) {
    enum { StockFirstRuntimeObject = 54 };
    int builtin = CPrec_StockBuiltin(graph, slot);

    if (builtin >= 0) {
        // PCH runtime objects use the same order as the one-based IPA IDs.
        return CIRStream_ImplicitObject(builtin - StockFirstRuntimeObject + 1);
    }
    return CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(slot), 0);
}

static Boolean CPrec_ReadStockCodeReference(
    CPrecStockGraph* graph,
    const UInt8* entry,
    CIRStreamReference* reference,
    HashNameNode** fileName
) {
    reference->header.kind = entry[4];
    reference->header.flags = entry[5];
    reference->header.callCount = CPrec_StockHalfword(entry + 6);
    switch (reference->header.kind) {
        case CIRStreamRefType:
            reference->value.type = CPrec_ReadStockType(graph, entry, 0);
            return reference->value.type != NULL;
        case CIRStreamRefObject:
            reference->value.object = CPrec_ReadStockObjectRef(graph, entry);
            return reference->value.object != NULL;
        case CIRStreamRefFile: {
            const UInt8* file = CPrec_StockRecord(graph, CIRStream_ReadWord(entry), 4);
            return file && CPrec_ReadStockName(graph, file, fileName);
        }
        case CIRStreamRefSourceType:
            reference->value.type = NULL;
            return TRUE;
        case CIRStreamRefName:
            return CPrec_ReadStockName(graph, entry, &reference->value.name);
    }
    return FALSE;
}

static Boolean CPrec_ReadStockFunction(
    CPrecStockGraph* graph, Object* object, const UInt8* package
) {
    UInt32 offset = CIRStream_ReadWord(package);
    const UInt8* packed = CPrec_StockRecord(graph, offset, 0x1a);
    const UInt8* entries;
    UInt32 index, count;
    HashNameNode** fileNames;
    CIRStreamFunctionDefinition definition = {0};
    CIRStreamFunctionCode code;

    if (!packed) {
        return FALSE;
    }
    count = CIRStream_ReadWord(packed + 0x14);
    if (count > (graph->rawSize - offset - 0x1a) / 8 ||
        count > 0x7fffffffUL / sizeof(CIRStreamReference) ||
        count >= 0x7fffffffUL / sizeof(HashNameNode*)) {
        return FALSE;
    }
    // The packed header has two padding bytes before the first reference.
    entries = packed + 0x1a;
    definition.header.code.size = CIRStream_ReadWord(packed + 4);
    definition.header.code.bytes =
        CPrec_StockRecord(graph, CIRStream_ReadWord(packed), definition.header.code.size);
    if (!definition.header.code.bytes || !definition.header.code.size) {
        return FALSE;
    }
    definition.header.inlineSize = CIRStream_ReadWord(package + 8);
    definition.header.options.bytes = package + 0x0c;
    definition.header.options.size = CIRStreamFunctionOptionsSize;
    definition.header.localCount = CIRStream_ReadWord(packed + 8);
    definition.header.labelCount = CIRStream_ReadWord(packed + 0x0c);
    definition.header.typeCount = CIRStream_ReadWord(packed + 0x10);
    definition.header.referenceCount = count;
    definition.references = lalloc(count * sizeof(*definition.references));
    fileNames = lalloc((count + 1) * sizeof(*fileNames));
    fileNames[0] = NULL;
    for (index = 0; index < count; ++index) {
        CIRStreamReference* reference = &definition.references[index];
        if (!CPrec_ReadStockCodeReference(
                graph, entries + index * 8, reference, &fileNames[index + 1]
            )) {
            return FALSE;
        }
        if (reference->header.kind == CIRStreamRefFile) {
            reference->value.fileID = index + 1;
        }
    }
    return CIRStream_UnpackPrecompiledCode(
               object, cscope_root, copts.processor == CPU_PPCGEKKO, &definition, fileNames, &code
           ) &&
        CMid_LoadPrecompiledFunction(object, &code, &definition.header);
}

static Boolean CPrec_ReadStockFunctions(CPrecStockGraph* graph) {
    while (graph->pendingFunctions) {
        CPrecStockFunction* entry = graph->pendingFunctions;

        graph->pendingFunctions = entry->next;
        if (!CPrec_ReadStockFunction(graph, entry->object, entry->package)) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean CPrec_ReadStockDataLinks(
    CPrecStockGraph* graph, UInt32 offset, UInt32 size, OLinkList** tail
) {
    UInt32 remaining = graph->rawSize / 0x10;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x10);
        OLinkList* link;
        UInt32 location;
        Object* target;

        if (!record || !remaining--) {
            return FALSE;
        }
        location = CIRStream_ReadWord(record + 8);
        target = CPrec_ReadStockObjectRef(graph, record + 4);
        if (!target || size < 4 || location > size - 4) {
            return FALSE;
        }
        link = lalloc(sizeof(*link));
        link->obj = target;
        link->offset = (SInt32)location;
        link->addend = (SInt32)CIRStream_ReadWord(record + 0x0c);
        *tail = link;
        tail = &link->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockStaticData(
    CPrecStockGraph* graph, UInt32 offset, StaticData** definitions
) {
    UInt32 remaining = graph->rawSize / 8;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 8);
        CPrecStockObject* saved;
        const UInt8* symbol;
        const UInt8* data;
        UInt32 objectOffset, bufferOffset, linkOffset, size;
        Object* object;
        StaticData* entry;

        if (!record || !remaining--) {
            return FALSE;
        }
        objectOffset = CIRStream_ReadWord(record + 4);
        symbol = CPrec_StockRecord(graph, objectOffset, 0x60);
        if (!symbol || symbol[2] != DDATA) {
            return FALSE;
        }
        object = CPrec_ReadStockSymbol(graph, objectOffset, 0);
        saved = *CPrec_FindStockObject(graph, objectOffset);
        data = CPrec_StockRecord(graph, CIRStream_ReadWord(symbol + 0x4c), 0x1a);
        if (!object || !saved->pendingData || !data || data[0x18] > 1) {
            return FALSE;
        }
        if (object->sclass != TK_STATIC && !(object->qual & (Q_IMPLICIT_WEAK | Q_WEAK))) {
            return FALSE;
        }
        bufferOffset = CIRStream_ReadWord(data + 0x0c);
        linkOffset = CIRStream_ReadWord(data + 0x10);
        size = CIRStream_ReadWord(data + 0x14);
        if (size != (UInt32)object->type->size ||
            size > 0x7fffffffu ||
            (!bufferOffset && linkOffset)) {
            return FALSE;
        }
        entry = lalloc(sizeof(*entry));
        entry->object = object;
        entry->buffer = bufferOffset ? (void*)CPrec_StockRecord(graph, bufferOffset, size) : NULL;
        entry->size = (SInt32)(size | (data[0x18] ? CPREC_STATIC_READ_ONLY : 0));
        if ((bufferOffset && !entry->buffer) ||
            !CPrec_ReadStockDataLinks(graph, linkOffset, size, &entry->links)) {
            return FALSE;
        }
        object->flags &= ~OBJECT_DEFINED;
        saved->pendingData = FALSE;
        --graph->dataDefinitions;
        entry->next = *definitions;
        *definitions = entry;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockTentativeData(CPrecStockGraph* graph, UInt32 offset) {
    ObjectList** tail = &cinit_tentative;
    UInt32 remaining = graph->rawSize / 8;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 8);
        Object* object;
        ObjectList* entry;

        if (!record || !remaining--) {
            return FALSE;
        }
        object = CPrec_ReadStockObjectRef(graph, record + 4);
        if (!object || object->datatype != DDATA || !object->type || object->type->size < 0) {
            return FALSE;
        }
        OBJECT_MARK_TENTATIVE(object);
        entry = galloc(sizeof(*entry));
        entry->object = object;
        *tail = entry;
        tail = &entry->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockExpressionList(
    CPrecStockGraph* graph, UInt32 offset, ENodeList** tail, UInt32 depth
) {
    UInt32 remaining = graph->rawSize / 8;

    for (; offset;) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 8);
        ENodeList* entry;
        if (!record || !remaining--) {
            return FALSE;
        }
        entry = galloc(sizeof(*entry));
        entry->node = CPrec_ReadStockExpression(graph, CIRStream_ReadWord(record + 4), depth);
        if (!entry->node) {
            return FALSE;
        }
        entry->next = NULL;
        *tail = entry;
        tail = &entry->next;
        offset = CIRStream_ReadWord(record);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockExpressionData(
    CPrecStockGraph* graph, const UInt8* record, ENode* node, UInt32 depth
) {
    const UInt8* data = record + 0x10;
    UInt32 first = CIRStream_ReadWord(data);
    UInt32 second = CIRStream_ReadWord(data + 4);

    if (ENODE_IS_RANGE(node, EPOSTINC, EFORCELOAD) || ENODE_IS2(node, ETYPCON, EBITFIELD)) {
        node->data.monadic = CPrec_ReadStockExpression(graph, first, depth);
        if (!node->data.monadic) {
            return FALSE;
        }
        node->hascall = node->data.monadic->hascall;
        return TRUE;
    }
    if (ENODE_IS_RANGE(node, EMUL, EBSET) || ENODE_IS(node, EMFPOINTER)) {
        node->data.diadic.left = CPrec_ReadStockExpression(graph, first, depth);
        node->data.diadic.right = CPrec_ReadStockExpression(graph, second, depth);
        if (!node->data.diadic.left || !node->data.diadic.right) {
            return FALSE;
        }
        node->hascall = node->data.diadic.left->hascall || node->data.diadic.right->hascall;
        return TRUE;
    }
    switch (node->type) {
        case EINTCONST:
            node->data.intval.hi = (SInt32)first;
            node->data.intval.lo = second;
            return TRUE;
        case EFLOATCONST:
            node->data.floatval.data.words[0] = (SInt32)first;
            node->data.floatval.data.words[1] = (SInt32)second;
            return TRUE;
        case EVECTOR128CONST:
            memcpy(node->data.vector128val.uc, data, sizeof(node->data.vector128val.uc));
            return TRUE;
        case ESTRINGCONST: {
            const UInt8* text = CPrec_StockRecord(graph, second, first);
            if (!first || !text) {
                return FALSE;
            }
            node->data.string.size = (SInt32)first;
            node->data.string.data = galloc(first);
            memcpy(node->data.string.data, text, first);
            node->data.string.ispascal = data[0x0c];
            node->data.string.sequence = (SInt32)CIRStream_ReadWord(data + 8);
            return TRUE;
        }
        case ECOND:
            node->data.cond.cond = CPrec_ReadStockExpression(graph, first, depth);
            node->data.cond.expr1 = CPrec_ReadStockExpression(graph, second, depth);
            node->data.cond.expr2 =
                CPrec_ReadStockExpression(graph, CIRStream_ReadWord(data + 8), depth);
            if (!node->data.cond.cond || !node->data.cond.expr1 || !node->data.cond.expr2) {
                return FALSE;
            }
            node->hascall = node->data.cond.cond->hascall ||
                node->data.cond.expr1->hascall ||
                node->data.cond.expr2->hascall;
            return TRUE;
        case EFUNCCALL:
        case EFUNCCALLP: {
            Type* type = CPrec_ReadStockType(graph, data + 8, 0);
            if (!type || !IS_TYPE_FUNC(type)) {
                return FALSE;
            }
            node->data.funccall.funcref = CPrec_ReadStockExpression(graph, first, depth);
            node->data.funccall.functype = TYPE_FUNC(type);
            node->hascall = TRUE;
            return node->data.funccall.funcref &&
                CPrec_ReadStockExpressionList(graph, second, &node->data.funccall.args, depth);
        }
        case EOBJREF:
            node->data.objref = CPrec_ReadStockObjectRef(graph, data);
            return node->data.objref != NULL;
        case EMEMBERFUNCLITERAL:
            node->data.objref = CPrec_ReadStockObjectRef(graph, data);
            if (!CExpr_MaterializeMemberFunctionLiteral(node)) {
                return FALSE;
            }
            *node = *CInline_CopyExpression(node, CopyModeGlobal);
            return TRUE;
        case ENULLCHECK:
            node->data.nullcheck.nullcheckexpr = CPrec_ReadStockExpression(graph, first, depth);
            node->data.nullcheck.condexpr = CPrec_ReadStockExpression(graph, second, depth);
            node->data.nullcheck.precompid = (SInt32)CIRStream_ReadWord(data + 8);
            if (!node->data.nullcheck.nullcheckexpr || !node->data.nullcheck.condexpr) {
                return FALSE;
            }
            node->hascall = node->data.nullcheck.nullcheckexpr->hascall ||
                node->data.nullcheck.condexpr->hascall;
            return TRUE;
        case EPRECOMP:
            node->data.precompid = (SInt32)first;
            return TRUE;
        case ETEMP:
            node->data.temp.type = CPrec_ReadStockType(graph, data, 0);
            node->data.temp.uniqueid = (SInt32)second;
            node->data.temp.needs_dtor = data[8] != 0;
            return node->data.temp.type != NULL;
        case ETEMPLDEP:
            node->data.templdep.subtype = (TemplDepSubType)record[0x2c];
            if (node->data.templdep.subtype == TDE_SIZEOF ||
                node->data.templdep.subtype == TDE_ALIGNOF) {
                node->data.templdep.u.typeexpr.type = CPrec_ReadStockType(graph, data, 0);
                return node->data.templdep.u.typeexpr.type != NULL;
            }
            return node->data.templdep.subtype == TDE_PARAM &&
                CPrec_ReadStockParamID(data, &node->data.templdep.u.pid) &&
                node->data.templdep.u.pid.type == TPT_NONTYPE;
        default:
            return FALSE;
    }
}

static ENode* CPrec_ReadStockExpression(CPrecStockGraph* graph, UInt32 offset, UInt32 depth) {
    const UInt8* record = CPrec_StockRecord(graph, offset, StockExpressionSize);
    CPrecStockExpression** bucket;
    CPrecStockExpression* entry;
    ENode* node;
    ENodeType kind;

    if (!record || depth >= StockExpressionDepth) {
        return NULL;
    }
    if (record[0] == StockExpressionTemplate) {
        kind = ETEMPLDEP;
    } else if (!CIRStream_DecodeExpressionKind(record[0], &kind)) {
        return NULL;
    }
    bucket = &graph->expressions
                  [(offset >> 2) % (sizeof(graph->expressions) / sizeof(graph->expressions[0]))];
    for (entry = *bucket; entry; entry = entry->next) {
        if (entry->offset == offset) {
            return entry->node;
        }
    }
    entry = lalloc(sizeof(*entry));
    entry->next = *bucket;
    entry->offset = offset;
    entry->node = NULL;
    *bucket = entry;
    node = galloc(sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->type = kind;
    node->cost = record[1];
    node->flags = (UInt16)CIRStream_ReadWord(record + 8);
    node->rtype = CPrec_ReadStockType(graph, record + 4, 0);
    if (!node->rtype || !CPrec_ReadStockExpressionData(graph, record, node, depth + 1)) {
        return NULL;
    }
    entry->node = node;
    return node;
}

static Boolean CPrec_ReadStockDefaults(CPrecStockGraph* graph) {
    while (graph->pendingDefaults) {
        CPrecStockDefaultArgument* entry = graph->pendingDefaults;
        graph->pendingDefaults = entry->next;
        entry->argument->dexpr = CPrec_ReadStockExpression(graph, entry->offset, 0);
        if (!entry->argument->dexpr) {
            return FALSE;
        }
    }
    return TRUE;
}

static SInt16 CPrec_DecodeStockToken(SInt16 kind) {
    // RC1 inserts additional tokens among the common lexer ranges.
    if ((kind >= TK_STRING_WIDE && kind < 0) ||
        kind == TK_EOL ||
        (kind >= 0 && kind < 128) ||
        (kind >= 0x100 && kind <= 0x11c)) {
        return kind;
    }
    if (kind >= 0x11e && kind <= 0x126) {
        return kind - 1;
    }
    if (kind >= 0x130 && kind <= 0x131) {
        return kind - (0x130 - TK_EXPLICIT);
    }
    if (kind >= 0x145 && kind <= 0x183) {
        return kind - (0x145 - TK_ASM);
    }
    switch (kind) {
        case 0x12d:
            return TK_RESTRICT;
        case 0x12e:
            return TK_UU_ATTRIBUTE_UU;
        case 0x196:
            return TK_UU_ALIGNOF_UU;
        case 0x197:
            return TK_UU_UUIDOF;
    }
    return TK_ILLEGAL;
}

static Boolean CPrec_ReadStockTokenStream(
    CPrecStockGraph* graph, UInt32 count, UInt32 offset, TokenStream* stream
) {
    enum {
        StockTokenSize = 0x1c
    };
    const UInt8* records;
    UInt32 index;

    if (!count) {
        return !offset;
    }
    if (count > graph->rawSize / StockTokenSize || count > 0x7fffffffu / sizeof(TStreamElement)) {
        return FALSE;
    }
    records = CPrec_StockRecord(graph, offset, count * StockTokenSize);
    if (!records) {
        return FALSE;
    }
    stream->tokens = (SInt32)count;
    stream->firsttoken = galloc(count * sizeof(TStreamElement));
    memset(stream->firsttoken, 0, count * sizeof(TStreamElement));
    for (index = 0; index < count; ++index) {
        const UInt8* record = records + index * StockTokenSize;
        const UInt8* data = record + 4;
        TStreamElement* token = &stream->firsttoken[index];

        token->tokentype = CPrec_DecodeStockToken((SInt16)CPrec_StockHalfword(record));
        token->subtype = (SInt16)CPrec_StockHalfword(record + 2);
        switch (token->tokentype) {
            case TK_ILLEGAL:
                return FALSE;
            case TK_IDENTIFIER:
                if (!CPrec_ReadStockName(graph, data, &token->data.tkidentifier) ||
                    !token->data.tkidentifier) {
                    return FALSE;
                }
                break;
            case TK_INTCONST:
                token->data.tkintconst.hi = (SInt32)CIRStream_ReadWord(data);
                token->data.tkintconst.lo = CIRStream_ReadWord(data + 4);
                break;
            case TK_FLOATCONST:
                token->data.tkfloatconst.data.words[0] = (SInt32)CIRStream_ReadWord(data);
                token->data.tkfloatconst.data.words[1] = (SInt32)CIRStream_ReadWord(data + 4);
                break;
            case TK_STRING:
            case TK_STRING_WIDE: {
                UInt32 size = CIRStream_ReadWord(data + 4);
                const UInt8* bytes = CPrec_StockRecord(graph, CIRStream_ReadWord(data), size);
                UInt32 unit = token->tokentype == TK_STRING_WIDE ? stwchar.size : 1;
                UInt32 start;
                if (!size || !bytes || !unit || size % unit) {
                    return FALSE;
                }
                token->subtype &= 1; // The source lexer stores only the Pascal flag here.
                token->data.tkstring.size = (SInt32)size;
                token->data.tkstring.data = galloc(size);
                memcpy(token->data.tkstring.data, bytes, size);
                if (unit > 1) {
                    for (start = 0; start < size; start += unit) {
                        CTool_EndianConvertMem(token->data.tkstring.data + start, (short)unit);
                    }
                }
                break;
            }
        }
    }
    return TRUE;
}

static Boolean CPrec_ReadStockTemplateParams(
    CPrecStockGraph* graph, UInt32 offset, TemplParam** tail, UInt32 depth
) {
    UInt32 remaining = graph->rawSize / 0x18;

    if (depth >= 128) {
        return FALSE;
    }
    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x18);
        const UInt8* payload;
        TemplParam* param;

        if (!record || !remaining--) {
            return FALSE;
        }
        payload = record + 0x0c;
        param = galloc(sizeof(*param));
        memset(param, 0, sizeof(*param));
        if (!CPrec_ReadStockName(graph, record + 4, &param->name) ||
            !CPrec_ReadStockParamID(record + 8, &param->pid)) {
            return FALSE;
        }
        switch (param->pid.type) {
            case TPT_TYPE:
                if (CPrec_StockBuiltin(graph, payload) >= 0 || CIRStream_ReadWord(payload)) {
                    param->data.typeparam.type = CPrec_ReadStockType(graph, payload, 0);
                    if (!param->data.typeparam.type) {
                        return FALSE;
                    }
                }
                param->data.typeparam.qual = CIRStream_ReadWord(payload + 4);
                break;
            case TPT_NONTYPE:
                param->data.paramdecl.type = CPrec_ReadStockType(graph, payload, 0);
                if (!param->data.paramdecl.type) {
                    return FALSE;
                }
                param->data.paramdecl.qual = CIRStream_ReadWord(payload + 4);
                if (CIRStream_ReadWord(payload + 8)) {
                    param->data.paramdecl.defaultarg =
                        CPrec_ReadStockExpression(graph, CIRStream_ReadWord(payload + 8), 0);
                    if (!param->data.paramdecl.defaultarg) {
                        return FALSE;
                    }
                }
                break;
            case TPT_TEMPLATE:
                if (CIRStream_ReadWord(payload + 4) ||
                    !CPrec_ReadStockTemplateParams(
                        graph, CIRStream_ReadWord(payload), &param->data.templparam.plist, depth + 1
                    )) {
                    return FALSE;
                }
                break;
        }
        *tail = param;
        tail = &param->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockTemplateMember(
    CPrecStockGraph* graph, UInt32 offset, Object* object
) {
    const UInt8* record = CPrec_StockRecord(graph, offset, 0x30);
    TemplClass* owner = TEMPL_CLASS(object->nspace->theclass);
    TemplateMember* member;

    if (!record) {
        return FALSE;
    }
    member = galloc(sizeof(*member));
    memset(member, 0, sizeof(*member));
    member->object = object;
    if (!CPrec_ReadStockTemplateParams(graph, CIRStream_ReadWord(record), &member->params, 0) ||
        !CPrec_ReadStockTokenStream(
            graph,
            CIRStream_ReadWord(record + 0x14),
            CIRStream_ReadWord(record + 0x18),
            &member->stream
        )) {
        return FALSE;
    }
    member->next = owner->members;
    owner->members = member;
    return TRUE;
}

static Boolean CPrec_ReadStockTemplateArgs(
    CPrecStockGraph* graph, UInt32 offset, TemplArg** tail, UInt32 depth
) {
    UInt32 remaining = graph->rawSize / 0x12;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x12);
        TemplArg* arg;

        if (!record || !remaining--) {
            return FALSE;
        }
        arg = galloc(sizeof(*arg));
        memset(arg, 0, sizeof(*arg));
        if (!CPrec_ReadStockParamID(record + 4, &arg->pid)) {
            return FALSE;
        }
        if (arg->pid.type == TPT_NONTYPE) {
            arg->data.paramdecl.expr =
                CPrec_ReadStockExpression(graph, CIRStream_ReadWord(record + 8), 0);
            arg->data.paramdecl.is_ref = record[0x0c] != 0;
            if (!arg->data.paramdecl.expr) {
                return FALSE;
            }
        } else {
            arg->data.typeparam.type = CPrec_ReadStockType(graph, record + 8, depth);
            arg->data.typeparam.qual = CIRStream_ReadWord(record + 0x0c);
            if (!arg->data.typeparam.type) {
                return FALSE;
            }
        }
        arg->is_deduced = record[0x10] != 0;
        *tail = arg;
        tail = &arg->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockClassInstance(
    CPrecStockGraph* graph, const UInt8* record, TemplClassInst* instance
) {
    Type* type = CPrec_ReadStockEmbeddedTemplate(graph, CIRStream_ReadWord(record + 0x34), 0);
    UInt32 parentOffset = CIRStream_ReadWord(record + 0x38);

    if (!type ||
        !IS_TEMPL_CLASS(type) ||
        !CPrec_ReadStockTemplateArgs(
            graph, CIRStream_ReadWord(record + 0x3c), &instance->inst_args, 0
        ) ||
        !CPrec_ReadStockTemplateArgs(
            graph, CIRStream_ReadWord(record + 0x44), &instance->oargs, 0
        )) {
        return FALSE;
    }
    if (parentOffset) {
        Type* parent = CPrec_ReadStockEmbeddedTemplate(graph, parentOffset, 0);
        if (!parent ||
            !IS_TEMPL_CLASS_INST(parent) ||
            TYPE_CLASS(parent)->nspace != instance->theclass.nspace->parent) {
            return FALSE;
        }
        instance->parent = TEMPL_CLASS_INST(parent);
    }
    instance->templ = TEMPL_CLASS(type);
    instance->is_instantiated = record[0x40] != 0;
    instance->is_specialized = record[0x41] != 0;
    instance->is_extern = record[0x42] != 0;
    instance->static_instantiated = record[0x43] != 0;
    return TRUE;
}

static Boolean CPrec_ReadStockClassInstances(
    CPrecStockGraph* graph, UInt32 offset, TemplClassInst** tail
) {
    UInt32 remaining = graph->rawSize / StockClassInstanceSize;

    while (offset) {
        Type* type = CPrec_ReadStockTypeAt(graph, offset, 0);
        const UInt8* record;
        Type* owner;
        TemplClassInst* inst;
        if (!remaining-- || !type || !IS_TEMPL_CLASS_INST(type)) {
            return FALSE;
        }
        record = graph->raw + offset;
        owner = CPrec_ReadStockEmbeddedTemplate(graph, CIRStream_ReadWord(record + 0x34), 0);
        if (!owner || !IS_TEMPL_CLASS(owner)) {
            return FALSE;
        }
        inst = TEMPL_CLASS_INST(type);
        if (TYPE_CLASS(owner)->flags & StockClassTemplateClone) {
            // The stock master owns one list, while source clones own their instances.
            TemplClass* clone = TEMPL_CLASS(owner);
            inst->next = clone->instances;
            clone->instances = inst;
        } else {
            *tail = inst;
            tail = &inst->next;
        }
        offset = CIRStream_ReadWord(record + 0x30);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockPartialSpecs(
    CPrecStockGraph* graph, UInt32 offset, TemplPartialSpec** tail
) {
    UInt32 remaining = graph->rawSize / 0x0c;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x0c);
        Type* type;
        TemplPartialSpec* spec;

        if (!record || !remaining--) {
            return FALSE;
        }
        type = CPrec_ReadStockType(graph, record + 4, 0);
        if (!type || !IS_TEMPL_CLASS(type)) {
            return FALSE;
        }
        spec = galloc(sizeof(*spec));
        spec->templ = TEMPL_CLASS(type);
        if (!CPrec_ReadStockTemplateArgs(graph, CIRStream_ReadWord(record + 8), &spec->args, 0)) {
            return FALSE;
        }
        *tail = spec;
        tail = &spec->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static ClassList* CPrec_FindStockTemplateBase(
    CPrecStockGraph* graph, UInt32 offset, ClassList* base, UInt32 target
) {
    for (; base; base = base->next) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x12);
        if (!record) {
            return NULL;
        }
        if (offset == target) {
            return base;
        }
        offset = CIRStream_ReadWord(record);
    }
    return NULL;
}

static Boolean CPrec_ReadStockTemplateAction(
    CPrecStockGraph* graph,
    const UInt8* record,
    TemplateAction* action,
    UInt32 baseOffset,
    ClassList* bases
) {
    const UInt8* data = record + 0x10;
    Type* type;
    UInt32 offset;

    switch (record[0x1a]) {
        case 0:
            action->type = TAT_NESTEDCLASS;
            type = CPrec_ReadStockType(graph, data, 0);
            if (!type || !IS_TEMPL_CLASS(type)) {
                return FALSE;
            }
            action->u.tclasstype = TEMPL_CLASS(type);
            return TRUE;
        case 1:
            action->type = TAT_ENUMTYPE;
            type = CPrec_ReadStockType(graph, data, 0);
            if (!type || !IS_TYPE_ENUM(type)) {
                return FALSE;
            }
            action->u.enumtype = TYPE_ENUM(type);
            return TRUE;
        case 3:
            action->type = TAT_BASE;
            action->u.base.type = CPrec_ReadStockType(graph, data, 0);
            action->u.base.access = data[8];
            action->u.base.is_virtual = data[9];
            offset = CIRStream_ReadWord(data + 4);
            if (offset) {
                action->u.base.insert_after =
                    CPrec_FindStockTemplateBase(graph, baseOffset, bases, offset);
            }
            return action->u.base.type &&
                (!offset || action->u.base.insert_after) &&
                data[8] <= ACCESSNONE &&
                data[9] <= 1;
        case 4:
            action->type = TAT_OBJECTINIT;
            action->u.objectinit.object = CPrec_ReadStockObjectRef(graph, data);
            action->u.objectinit.initexpr =
                CPrec_ReadStockExpression(graph, CIRStream_ReadWord(data + 4), 0);
            return action->u.objectinit.object &&
                action->u.objectinit.object->datatype == DDATA &&
                action->u.objectinit.initexpr;
        case 5:
            action->type = TAT_USINGDECL;
            type = CPrec_ReadStockType(graph, data, 0);
            if (!type ||
                !IS_TYPE_TEMPLATE(type) ||
                TYPE_TEMPLATE(type)->dtype != TEMPLDEP_QUALNAME ||
                data[4] > ACCESSNONE) {
                return FALSE;
            }
            action->u.usingdecl.type = TYPE_TEMPLATE(type);
            action->u.usingdecl.access = data[4];
            return TRUE;
        case 6: {
            const CPrecStockObject* saved = *CPrec_FindStockObject(graph, CIRStream_ReadWord(data));
            if (!saved ||
                (saved->object->otype != OT_MEMBERVAR &&
                 saved->object->otype != OT_TYPE &&
                 saved->object->otype != OT_OBJECT)) {
                return FALSE;
            }
            action->type = TAT_OBJECTDEF;
            action->u.refobj = saved->object;
            return TRUE;
        }
        default:
            return FALSE;
    }
}

static TemplateAction** CPrec_ReadStockEnumeratorActions(
    CPrecStockGraph* graph, UInt32 enumOffset, TypeEnum* type, TemplateAction** tail
) {
    const UInt8* record = CPrec_StockRecord(graph, enumOffset, 0x16);
    UInt32 offset;
    ObjEnumConst* enumerator;
    Boolean hasDependentValue = FALSE;

    if (!record) {
        return NULL;
    }
    offset = CIRStream_ReadWord(record + 0x0a);
    // RC1 stores a dependent initializer on the enumerator's type.
    for (enumerator = type->enumlist; enumerator; enumerator = enumerator->next) {
        const UInt8* object = CPrec_StockRecord(graph, offset, 0x16);
        const UInt8* value;
        if (!object) {
            return NULL;
        }
        value = CPrec_StockDependentEnumValue(graph, object + 0x0a, enumOffset);
        offset = CIRStream_ReadWord(object + 2);
        if (value) {
            UInt32 exprOffset = CIRStream_ReadWord(value + 0x0c);
            TemplateAction* action = galloc(sizeof(*action));
            memset(action, 0, sizeof(*action));
            action->type = TAT_ENUMERATOR;
            action->u.enumerator.objenumconst = enumerator;
            if (exprOffset) {
                action->u.enumerator.initexpr = CPrec_ReadStockExpression(graph, exprOffset, 0);
                if (!action->u.enumerator.initexpr) {
                    return NULL;
                }
            } else if (!hasDependentValue) {
                return NULL;
            }
            *tail = action;
            tail = &action->next;
        }
        hasDependentValue = value != NULL;
    }
    return tail;
}

static Boolean CPrec_ReadStockClassTemplate(
    CPrecStockGraph* graph, const UInt8* record, TemplClass* templ
) {
    UInt32 parentOffset = CIRStream_ReadWord(record + StockClassTemplateHeader);
    UInt32 pspecOwner = CIRStream_ReadWord(record + 0x46);
    UInt32 baseOffset = CIRStream_ReadWord(record + 0x0e);
    UInt32 offset = CIRStream_ReadWord(record + 0x4e);
    UInt32 remaining = graph->rawSize / StockTemplateActionSize;
    TemplateAction** tail = &templ->actions;

    if (CIRStream_ReadWord(record + 0x38) ||
        !CPrec_ReadStockTemplateParams(
            graph, CIRStream_ReadWord(record + 0x3c), &templ->templ__params, 0
        ) ||
        !CPrec_ReadStockClassInstances(
            graph, CIRStream_ReadWord(record + 0x42), &templ->instances
        ) ||
        !CPrec_ReadStockPartialSpecs(graph, CIRStream_ReadWord(record + 0x4a), &templ->pspecs)) {
        return FALSE;
    }
    if (parentOffset) {
        Type* parent = CPrec_ReadStockEmbeddedTemplate(graph, parentOffset, 0);
        if (!parent ||
            !IS_TEMPL_CLASS(parent) ||
            TYPE_CLASS(parent)->nspace != templ->theclass.nspace->parent) {
            return FALSE;
        }
        templ->templ_parent = TEMPL_CLASS(parent);
    }
    if (pspecOwner) {
        Type* owner = CPrec_ReadStockTypeAt(graph, pspecOwner, 0);
        if (!owner || !IS_TEMPL_CLASS(owner) || owner == TYPE(templ)) {
            return FALSE;
        }
        templ->pspec_owner = TEMPL_CLASS(owner);
    }
    templ->lex_order_count = CPrec_StockHalfword(record + 0x52);
    templ->align = (SInt8)record[0x54];
    templ->flags = record[0x55];
    while (offset) {
        const UInt8* rawAction = CPrec_StockRecord(graph, offset, StockTemplateActionSize);
        TemplateAction* action;
        if (!rawAction || !remaining--) {
            return FALSE;
        }
        action = galloc(sizeof(*action));
        memset(action, 0, sizeof(*action));
        if (!CPrec_ReadStockTemplateAction(
                graph, rawAction, action, baseOffset, templ->theclass.bases
            )) {
            return FALSE;
        }
        *tail = action;
        tail = &action->next;
        if (action->type == TAT_ENUMTYPE) {
            tail = CPrec_ReadStockEnumeratorActions(
                graph, CIRStream_ReadWord(rawAction + 0x10), action->u.enumtype, tail
            );
            if (!tail) {
                return FALSE;
            }
        }
        offset = CIRStream_ReadWord(rawAction);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockClassTemplates(CPrecStockGraph* graph, UInt32 offset) {
    TemplClass** tail = &ctempl_templates;
    UInt32 remaining = graph->rawSize / StockClassTemplateSize;

    while (offset) {
        Type* type = CPrec_ReadStockTypeAt(graph, offset, 0);
        if (!remaining-- ||
            !type ||
            !IS_TEMPL_CLASS(type) ||
            (TYPE_CLASS(type)->flags & StockClassTemplateClone)) {
            return FALSE;
        }
        *tail = TEMPL_CLASS(type);
        tail = &(*tail)->next;
        offset = CIRStream_ReadWord(graph->raw + offset + 0x30);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockClassClones(CPrecStockGraph* graph) {
    while (graph->pendingClassClones) {
        CPrecStockType* entry = graph->pendingClassClones;
        const UInt8* record = graph->raw + entry->offset;
        Type* original =
            CPrec_ReadStockEmbeddedTemplate(graph, CIRStream_ReadWord(record + 0x34), 0);
        UInt32 outerOffset = CIRStream_ReadWord(record + 0x38);
        Type* outer = CPrec_ReadStockEmbeddedTemplate(graph, outerOffset, 0);
        const UInt8* scope = graph->raw + CIRStream_ReadWord(record + 6);
        TemplClass* clone = TEMPL_CLASS(entry->type);
        TemplClass* master;
        TemplClassInst* inst;
        TemplClassInst* instances = NULL;
        UInt32 remaining = graph->rawSize / StockClassInstanceSize;

        if (!original ||
            !IS_TEMPL_CLASS(original) ||
            (TYPE_CLASS(original)->flags & StockClassTemplateClone) ||
            !outer ||
            !IS_TEMPL_CLASS_INST(outer)) {
            return FALSE;
        }
        graph->pendingClassClones = entry->pending;
        master = TEMPL_CLASS(original);
        if (!master->templ_parent ||
            !master->templ__params ||
            CIRStream_ReadWord(scope) !=
                CIRStream_ReadWord(graph->raw + outerOffset - StockClassTemplateHeader + 6)) {
            return FALSE;
        }
        clone->theclass = master->theclass;
        clone->templ_parent = master->templ_parent;
        clone->inst_parent = TEMPL_CLASS_INST(outer);
        clone->templ__params = master->templ__params;
        clone->actions = master->actions;
        clone->lex_order_count = master->lex_order_count;
        clone->align = master->align;
        clone->flags = master->flags;
        for (inst = clone->instances; inst;) {
            TemplClassInst* next = inst->next;
            if (!remaining--) {
                return FALSE;
            }
            inst->next = instances;
            instances = inst;
            inst = next;
        }
        clone->instances = instances;
        clone->next = ctempl_templates;
        ctempl_templates = clone;
    }
    return TRUE;
}

static Boolean CPrec_ReadStockFunctionInstances(
    CPrecStockGraph* graph,
    UInt32 offset,
    UInt32 templateOffset,
    UInt32 enclosingInstance,
    TemplateFunction* templ
) {
    TemplFuncInstance** tail = &templ->instances;
    UInt32 remaining = graph->rawSize / StockFunctionInstanceSize;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, StockFunctionInstanceSize);
        const UInt8* symbol;
        UInt32 symbolOffset;
        Object* object;
        TemplFuncInstance* instance;

        if (!record ||
            !remaining-- ||
            CIRStream_ReadWord(record) != templateOffset ||
            CIRStream_ReadWord(record + 4) != enclosingInstance) {
            return FALSE;
        }
        symbolOffset = CIRStream_ReadWord(record + 0x18);
        symbol = CPrec_StockRecord(graph, symbolOffset, 0x60);
        object = CPrec_ReadStockSymbol(graph, symbolOffset, 0);
        if (!symbol ||
            CIRStream_ReadWord(symbol + 0x48) != offset ||
            !object ||
            !IS_TYPE_FUNC(object->type) ||
            object->u.func.inst ||
            object->nspace != templ->tfunc->nspace) {
            return FALSE;
        }
        instance = galloc(sizeof(*instance));
        memset(instance, 0, sizeof(*instance));
        instance->object = object;
        instance->is_instantiated = record[0x0c] != 0;
        instance->is_specialized = record[0x0d] != 0;
        instance->is_extern = record[0x0e] != 0;
        if (!CPrec_ReadStockTemplateArgs(
                graph, CIRStream_ReadWord(record + 8), &instance->args, 0
            )) {
            return FALSE;
        }
        object->u.func.inst = instance;
        --graph->functionInstances;
        *tail = instance;
        tail = &instance->next;
        if (!instance->is_specialized &&
            !instance->is_instantiated &&
            !instance->is_extern &&
            !(TYPE_FUNC(object->type)->flags & FUNC_DEFINED)) {
            CInline_AddTemplateFunctionAction(object, templ, instance);
        }
        offset = CIRStream_ReadWord(record + 0x10);
    }
    return TRUE;
}

static Boolean CPrec_LinkStockFunctionClones(CPrecStockGraph* graph, UInt32 offset) {
    TemplateFunction* templ;

    for (templ = ctempl_templatefuncs; templ; templ = templ->next) {
        const UInt8* record = graph->raw + offset;
        if (CIRStream_ReadWord(record + 4)) {
            UInt32 originalOffset = CIRStream_ReadWord(record);
            const UInt8* original = CPrec_StockRecord(graph, originalOffset, 0x36);
            Object* object;

            if (!original || CIRStream_ReadWord(original + 4)) {
                return FALSE;
            }
            object = CPrec_ReadStockSymbol(graph, CIRStream_ReadWord(original + 0x2e), 0);
            if (!object || !IS_TEMPL_FUNC(object->type) || !object->u.func.u.templ) {
                return FALSE;
            }
            templ->original = object->u.func.u.templ;
        }
        offset = CIRStream_ReadWord(record + 0x0e);
    }
    return TRUE;
}

static Boolean CPrec_ReadStockFunctionTemplates(CPrecStockGraph* graph, UInt32 offset) {
    TemplateFunction** tail = &ctempl_templatefuncs;
    UInt32 remaining = graph->rawSize / 0x36;
    UInt32 first = offset;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x36);
        UInt32 enclosingInstance;
        UInt32 parentOffset;
        UInt32 symbolOffset;
        const UInt8* symbol;
        Object* object;
        TemplateFunction* templ;

        if (!record || !remaining--) {
            return FALSE;
        }
        symbolOffset = CIRStream_ReadWord(record + 0x2e);
        symbol = CPrec_StockRecord(graph, symbolOffset, 0x60);
        object = CPrec_ReadStockSymbol(graph, symbolOffset, 0);
        if (!symbol ||
            CIRStream_ReadWord(symbol + 0x40) != offset ||
            !object ||
            !IS_TEMPL_FUNC(object->type) ||
            object->u.func.u.templ) {
            return FALSE;
        }
        enclosingInstance = CIRStream_ReadWord(record + 4);
        parentOffset = enclosingInstance ? enclosingInstance : CIRStream_ReadWord(record);
        if (parentOffset) {
            Type* parent = CPrec_ReadStockEmbeddedTemplate(graph, parentOffset, 0);
            if (!parent ||
                !(enclosingInstance ? IS_TEMPL_CLASS_INST(parent) : IS_TEMPL_CLASS(parent)) ||
                TYPE_CLASS(parent)->nspace != object->nspace) {
                return FALSE;
            }
        }
        templ = galloc(sizeof(*templ));
        memset(templ, 0, sizeof(*templ));
        templ->name = object->name;
        templ->tfunc = object;
        if (!CPrec_ReadStockTemplateParams(
                graph, CIRStream_ReadWord(record + 8), &templ->params, 0
            ) ||
            !CPrec_ReadStockTokenStream(
                graph,
                CIRStream_ReadWord(record + 0x16),
                CIRStream_ReadWord(record + 0x1a),
                &templ->stream
            )) {
            return FALSE;
        }
        object->u.func.u.templ = templ;
        --graph->functionTemplates;
        if (!CPrec_ReadStockFunctionInstances(
                graph, CIRStream_ReadWord(record + 0x32), offset, enclosingInstance, templ
            )) {
            return FALSE;
        }
        *tail = templ;
        tail = &templ->next;
        offset = CIRStream_ReadWord(record + 0x0e);
    }
    *tail = NULL;
    return CPrec_LinkStockFunctionClones(graph, first);
}

static Boolean CPrec_ReadStockInitializers(CPrecStockGraph* graph, UInt32 offset) {
    InitExpr** tail = &init_expressions;
    UInt32 remaining = graph->rawSize / 0x18;

    while (offset) {
        const UInt8* record = CPrec_StockRecord(graph, offset, 0x18);
        InitExpr* entry;

        if (!record || !remaining--) {
            return FALSE;
        }
        entry = galloc(sizeof(*entry));
        entry->expr = CPrec_ReadStockExpression(graph, CIRStream_ReadWord(record + 4), 0);
        entry->object = CPrec_ReadStockObjectRef(graph, record + 8);
        if (!entry->expr || !entry->object || entry->object->datatype != DDATA) {
            return FALSE;
        }
        *tail = entry;
        tail = &entry->next;
        offset = CIRStream_ReadWord(record);
    }
    *tail = NULL;
    return TRUE;
}

static Boolean CPrec_ReadStockDeclarations(const UInt8* image, UInt32 size) {
    CPrecStockGraph graph;
    StaticData* definitions = NULL;
    UInt32 backendSize = CIRStream_ReadWord(image + 0xd2);
    UInt32 backendOffset = CIRStream_ReadWord(image + 0xd6);

    memset(&graph, 0, sizeof(graph));
    graph.raw = image + CIRStream_ReadWord(image + 0x3a);
    graph.rawSize = CIRStream_ReadWord(image + 0x32);
    graph.objectsRemaining = graph.rawSize / 8;
    if (CIRStream_ReadWord(image + 0x86)) { // Saved thunks.
        return FALSE;
    }
    graph.builtinIndices = lalloc(graph.rawSize / 2);
    if (!CPrec_IndexStockBuiltins(image, size, graph.builtinIndices, graph.rawSize / 2) ||
        !CodeGen_ReadPrecompilerData(image + backendOffset, backendSize, &graph.sections) ||
        !CPrec_ReadStockClassTemplates(&graph, CIRStream_ReadWord(image + 0x62)) ||
        !CPrec_ReadStockClassList(&graph, CIRStream_ReadWord(image + 0x72)) ||
        !CPrec_ReadStockUsings(&graph, cscope_root, CIRStream_ReadWord(image + 0x5e)) ||
        !CPrec_ReadStockNameTable(
            &graph, cscope_root, image + 0x40da, CIRStream_ReadWord(image + 0x5a)
        ) ||
        !CPrec_ReadStockFriends(&graph, CIRStream_ReadWord(image + 0x82)) ||
        !CPrec_ReadStockScopeContents(&graph) ||
        !CPrec_ReadStockStaticData(&graph, CIRStream_ReadWord(image + 0x6a), &definitions) ||
        !CPrec_ReadStockTentativeData(&graph, CIRStream_ReadWord(image + 0x7e))) {
        return FALSE;
    }
    CParser_SetUniqueID((SInt32)CIRStream_ReadWord(image + 0x6e));
    if (!CPrec_ReadStockFunctionTemplates(&graph, CIRStream_ReadWord(image + 0x7a)) ||
        !CPrec_ReadStockInitializers(&graph, CIRStream_ReadWord(image + 0x76))) {
        return FALSE;
    }
    while (graph.pendingDefaults ||
           graph.pendingScopes ||
           graph.pendingFunctions ||
           graph.pendingTemplateClasses) {
        if (!CPrec_ReadStockDefaults(&graph) ||
            !CPrec_ReadStockScopeContents(&graph) ||
            !CPrec_ReadStockFunctions(&graph)) {
            return FALSE;
        }
        if (graph.pendingTemplateClasses) {
            CPrecStockType* entry = graph.pendingTemplateClasses;
            const UInt8* record = graph.raw + entry->offset;
            graph.pendingTemplateClasses = entry->pending;
            if (IS_TEMPL_CLASS(entry->type)
                    ? !CPrec_ReadStockClassTemplate(&graph, record, TEMPL_CLASS(entry->type))
                    : !CPrec_ReadStockClassInstance(
                          &graph, record, TEMPL_CLASS_INST(entry->type)
                      )) {
                return FALSE;
            }
        }
    }
    if (!CPrec_ReadStockClassClones(&graph) ||
        graph.dataDefinitions ||
        graph.functionTemplates ||
        graph.functionInstances) {
        return FALSE;
    }
    for (; definitions; definitions = definitions->next) {
        CPrec_DeclareStaticData(definitions);
    }
    return TRUE;
}

static void CPrec_ReadStockHeader(const UInt8* header, SInt32 size) {
    const void* image = cprec_buffer;
    void* expanded;
    size_t expandedSize;

    if (header[8] != 0xe9 ||
        header[9] != 0xf6 ||
        header[10] != 2 ||
        header[0x11] != stwchar.size ||
        header[0x12] != stbool.size) {
        CError_ErrorTerm(ERR_ILLEGAL_PRECOMPILED_HEADER_COMPILER_FLAGS_TARGET);
    }
    copts.checkprecompflags = header[0x0b];
    if (!image) {
        void* data = lalloc(size);
        CPrec_ReadData(0, data, size);
        image = data;
    }
    expandedSize = PrecompilerExpand(image, size, NULL, 0);
    if (!expandedSize || expandedSize > 0x7fffffffu) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    expanded = lalloc(expandedSize);
    if (PrecompilerExpand(image, size, expanded, expandedSize) != expandedSize ||
        !CPrec_VisitStockMacros(expanded, expandedSize, CPrec_InstallStockMacro, NULL) ||
        !CPrec_VisitStockFiles(expanded, expandedSize, CPrep_RememberPrecompiledFile, NULL) ||
        (!cparamblkptr->preprocess && !CPrec_ReadStockDeclarations(expanded, expandedSize))) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    freelheap();
    CPrep_InsertSpecialMacros();
}
#else
static void CPrec_ReadRawBuffer(void) {
    UInt8* buffer;
    UInt8* work;
    UInt8* end;
    UInt32 size;
    int ch;

    if (!cprec_buffer) {
        size = cprec_header->rawsize + (cprec_header->rawsize >> 7) + 64;
        buffer = galloc(size);
        cprec_rawbuffer = buffer;

        work = buffer + size - cprec_header->rawdatasize;
        CPrec_ReadData(cprec_header->rawdataoffset, work, cprec_header->rawdatasize);
    } else {
        buffer = galloc(cprec_header->rawsize);
        cprec_rawbuffer = buffer;
        work = (UInt8*)cprec_buffer + cprec_header->rawdataoffset;
    }

    end = work + cprec_header->rawdatasize;

    while (work < end) {
        if ((ch = *(work++)) >= 0xE0) {
            ch -= 0xE0;
            do {
                *(buffer++) = 0;
            } while (--ch >= 0);
        } else {
            do {
                *(buffer++) = *(work++);
            } while (--ch >= 0);
        }
    }

    if (work != end || buffer != (UInt8*)cprec_rawbuffer + cprec_header->rawsize) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
}
#endif

#if VERSION == VERSION_WII_1_0RC1
static void CPrec_RelocateRawBuffer(UInt32 imageSize) {
    UInt32 count = cprec_header->compressedPatchCount;
    UInt32 size = cprec_header->compressedPatchSize;
    if (count) {
        const UInt8* patches =
            CPrec_ReadRawSection(imageSize, cprec_header->compressedPatchOffset, size);
        if (!CPrec_RelocateRawData(cprec_rawbuffer, cprec_header->rawsize, patches, size, count,
                                  (UInt32)cprec_rawbuffer)) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
        freelheap();
    }
}
#else
static void CPrec_RelocateRawBuffer(void) {
    UInt8* patches;
    UInt32 offset;
    UInt32 dest;
    UInt8* patch;
    SInt32 count;
    union {
        UInt32 l;
        UInt8 b[4];
    } var;

    if ((count = cprec_header->compressedPatchCount)) {
        if (!cprec_buffer) {
            patches = lalloc(cprec_header->compressedPatchSize);
            CPrec_ReadData(
                cprec_header->compressedPatchOffset, patches, cprec_header->compressedPatchSize
            );
        } else {
            patches = (UInt8*)cprec_buffer + cprec_header->compressedPatchOffset;
        }

        offset = 0;
        patch = patches;
        dest = (UInt32)cprec_rawbuffer;
        do {
            if (!(*patch & 0x80)) {
                var.b[3] = patch[0];
                var.b[2] = patch[1];
                var.b[1] = patch[2];
                var.b[0] = patch[3];
                patch += 4;
                offset = var.l;
            } else {
                offset += (SInt8)(*patch << 1);
                patch++;
            }
            *((UInt32*)(dest + offset)) += dest;
        } while (--count > 0);

        freelheap();

        patches += cprec_header->compressedPatchSize;
        if (patch != patches) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
    }
}

#endif

#if VERSION == VERSION_WII_1_0RC1
static void CPrec_RelocateBuiltins(UInt32 imageSize) {
    UInt32 size = cprec_header->builtinPatchSize;
    UInt32 limit;
    CIRStreamReader input;
    CIRStreamReader word;
    UInt32 count;
    UInt32 index;
    UInt32 offset;

    if (!size) {
        return;
    }
    CPrec_SetupBuiltInArray();
    limit = cprec_builtins;
    if (cprec_header->version != CPREC_SOURCE_VERSION_WITH_RUNTIME_OBJECTS) {
        limit -= CPrec_AddedRuntimeBuiltinCount;
    }
    input.bytes = CPrec_ReadRawSection(imageSize, cprec_header->builtinPatchOffset, size);
    input.size = size;
    while (CIRStream_UnpackData(&input, sizeof(UInt32), &word)) {
        count = CIRStream_ReadWord(word.bytes);
        if (!count && !input.size) {
            return;
        }
        if (!count ||
            !CIRStream_UnpackData(&input, sizeof(UInt32), &word) ||
            (index = CIRStream_ReadWord(word.bytes)) >= limit ||
            count > input.size / sizeof(UInt32)) {
            break;
        }
        do {
            offset = CIRStream_ReadWord(input.bytes);
            if ((offset & 1) || offset > cprec_header->rawsize ||
                cprec_header->rawsize - offset < sizeof(void*)) {
                CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
            }
            memcpy((UInt8*)cprec_rawbuffer + offset, cprec_builtin_array + index, sizeof(void*));
            CIRStream_UnpackData(&input, sizeof(UInt32), NULL);
        } while (--count);
    }
    CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
}
#else
static void CPrec_RelocateBuiltins(void) {
    UInt32 count;
    void* builtin;
    UInt32* patches;
    UInt8* buffer;

    if (cprec_header->builtinPatchSize) {
        CPrec_SetupBuiltInArray();
        if (!cprec_buffer) {
            patches = lalloc(cprec_header->builtinPatchSize);
            CPrec_ReadData(
                cprec_header->builtinPatchOffset, patches, cprec_header->builtinPatchSize
            );
        } else {
            patches = (UInt32*)((UInt8*)cprec_buffer + cprec_header->builtinPatchOffset);
        }

        buffer = cprec_rawbuffer;
        while (1) {
            if (!(count = *(patches++))) {
                break;
            }

            builtin = cprec_builtin_array[*(patches++)];
            do {
                *((void**)(buffer + *(patches++))) = builtin;
            } while (--count);
        }
    }
}
#endif

static void CPrec_RelocateTokenStreams(void) {
    TStreamElement* tokens;
    UInt32 i;
    UInt32 count;
    CPrepFileInfo* file;
    SInt32 pos;
    UInt32* patches;
    UInt8* buffer;
    UInt32 offset;

    if (cprec_header->tokenStreamPatchSize) {
        CPrep_GetPrepPos(&file, &pos);

        if (!cprec_buffer) {
            patches = lalloc(cprec_header->tokenStreamPatchSize);
            CPrec_ReadData(
                cprec_header->tokenStreamPatchOffset, patches, cprec_header->tokenStreamPatchSize
            );
        } else {
            patches = (UInt32*)((UInt8*)cprec_buffer + cprec_header->tokenStreamPatchOffset);
        }

        buffer = cprec_rawbuffer;
        while (1) {
            if (!(offset = *(patches++))) {
                break;
            }

            tokens = (TStreamElement*)(buffer + offset);
            count = *(patches++);
            for (i = 0; i < count; i++, tokens++) {
                tokens->tokenfile = file;
                tokens->tokenoffset = pos;
            }
        }
    }
}

static void CPrec_RelocateMacroTable(void) {
    int i;
    int j;
    int count;
    UInt8* buffer;
    Macro** prec_table;
    Macro** table;
    Macro* macro;
    UInt32 offset;

    buffer = cprec_rawbuffer;
    prec_table = cprec_header->macrotable;
    table = macrohashtable;

    i = 0;
    do {
        for (macro = *table; macro; macro = macro->next) {
            macro->name = GetHashNameNodeExport(macro->name->name);
            count = macro->nargs & 0x7FFF;
            for (j = 1; j < count; j++) {
                macro->names[j - 1] = GetHashNameNodeExport(macro->names[j - 1]->name);
            }
        }

        if ((offset = (UInt32)*prec_table)) {
            if (*table) {
                macro = (Macro*)(buffer + offset);
                while (macro->next) {
                    macro = macro->next;
                }
                macro->next = *table;
            }
            *table = (Macro*)(buffer + offset);
        }

        prec_table++;
        table++;
    } while (++i < 0x800);
}

#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA -7
#endif
#if VERSION == VERSION_WII_1_0RC1
Boolean PrecompilerRead(short refnum, void* buffer, SInt32 size) {
#else
void PrecompilerRead(short refnum, void* buffer) {
#endif
    Header* header;
    SInt32 offset;
    UInt8* base;
    SInt32 index;
    SInt32 bucket;
    HashNameNode* name;
    SInt32* offsets;
    HashNameNode** hashEntries;
    StaticData* record;

    cprec_refnum = refnum;
    cprec_buffer = buffer;
    CPrep_RemoveSpecialMacros();
    if (!CScope_IsEmptySymTable()) {
        CError_ErrorTerm(ERR_ILLEGAL_USE_PRECOMPILED_HEADER);
    }

#if VERSION == VERSION_WII_1_0RC1
    if (size < (SInt32)sizeof(Header)) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
#endif
    if (cprec_buffer == NULL) {
        cprec_header = galloc(sizeof(Header));
        header = cprec_header;
        if (COS_FileSetPos(cprec_refnum, 0) != 0 ||
            COS_FileRead(cprec_refnum, header, sizeof(*header)) != 0) {
            CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
        }
    } else {
        header = cprec_buffer;
        cprec_header = header;
    }

    if (cprec_header->magic != 0xBEEFFACE) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
#if VERSION == VERSION_WII_1_0RC1
    if (cprec_header->version == 0x042e) {
        CPrec_ReadStockHeader((const UInt8*)cprec_header, size);
        cprec_refnum = 0;
        return cparamblkptr->preprocess;
    }
#endif
    if (cprec_header->version != CPREC_SOURCE_VERSION
#if VERSION == VERSION_WII_1_0RC1
        && cprec_header->version != CPREC_SOURCE_VERSION_WITH_RUNTIME_OBJECTS
        && cprec_header->version != CPREC_SOURCE_VERSION_WITHOUT_SAVED_UIDS
        && cprec_header->version != CPREC_SOURCE_VERSION_WITHOUT_FILES
#endif
    ) {
        CError_ErrorTerm(ERR_ILLEGAL_PRECOMPILED_HEADER_VERSION);
    }
    if (cprec_header->target != 2) {
        CError_ErrorTerm(ERR_ILLEGAL_PRECOMPILED_HEADER_COMPILER_FLAGS_TARGET);
    }

    copts.checkprecompflags = cprec_header->check_header_flags;
#if VERSION == VERSION_WII_1_0RC1
    CPrec_ReadRawBuffer(size);
    if (cprec_header->version >= CPREC_SOURCE_VERSION &&
        !CPrec_VisitRawFiles(
            (const UInt8*)cprec_rawbuffer, cprec_header->rawsize, cprec_header->includedFiles,
            CPrep_RememberPrecompiledFile, NULL
        )) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    CPrec_RelocateRawBuffer(size);
    CPrec_RelocateBuiltins(size);
#else
    CPrec_ReadRawBuffer();
    CPrec_RelocateRawBuffer();
    CPrec_RelocateBuiltins();
#endif

    hashEntries = name_hash_nodes;
    offsets = (SInt32*)cprec_header->nametable;
    base = cprec_rawbuffer;
    index = 0;
    do {
        offset = *offsets;
        if (offset != 0) {
            *hashEntries = (HashNameNode*)(base + offset);
        } else {
            *hashEntries = NULL;
        }
        offsets++;
        hashEntries++;
    } while (++index < 0x800);

    bucket = 0;
    do {
        name = name_hash_nodes[bucket];
        if (name) {
            do {
                name->id = -1;
                name = name->next;
            } while (name != NULL);
        }
    } while (++bucket < 0x800);

    CPrec_RelocateMacroTable();
    CPrec_RelocateTokenStreams();

    CError_ASSERT(5048, cscope_root->is_hash != 0);
    {
        SInt32 namespaceOffset;
        UInt8* namespaceBase;
        SInt32 namespaceIndex;
        SInt32* namespaceOffsets;
        NameSpaceName** namespaceEntries;

        cscope_root->names = cprec_header->root_names, namespaceEntries = cscope_root->data.hash;
        namespaceOffsets = (SInt32*)cprec_header->root_nametable;
        namespaceBase = cprec_rawbuffer;
        namespaceIndex = 0;
        do {
            namespaceOffset = *namespaceOffsets;
            if (namespaceOffset != 0) {
                *namespaceEntries = (NameSpaceName*)(namespaceBase + namespaceOffset);
            } else {
                *namespaceEntries = NULL;
            }
            namespaceOffsets++;
            namespaceEntries++;
        } while (++namespaceIndex < 0x400);
    }

    if (!cprec_header->usings) {
        cscope_root->usings = NULL;
    } else {
        cscope_root->usings =
            (struct NameSpaceList*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->usings);
    }

    if (!cprec_header->ctempl_templates) {
        ctempl_templates = NULL;
    } else {
        ctempl_templates =
            (TemplClass*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->ctempl_templates);
    }

    if (!cprec_header->ctempl_templatefuncs) {
        ctempl_templatefuncs = NULL;
    } else {
        ctempl_templatefuncs = (TemplateFunction*)((UInt8*)cprec_rawbuffer +
                                                   (UInt32)cprec_header->ctempl_templatefuncs);
    }

    if (!cprec_header->csom_stubs) {
        csom_stubs = NULL;
    } else {
        csom_stubs = (CSOMStub*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->csom_stubs);
    }

    if (!cprec_header->cprec_staticdata) {
        cprec_staticdata = NULL;
    } else {
        cprec_staticdata =
            (StaticData*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cprec_staticdata);
    }

    if (!cprec_header->callbackactions) {
        callbackactions = NULL;
    } else {
        callbackactions =
            (CallbackAction*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->callbackactions);
    }

    if (!cprec_header->cobjc_type_class) {
        cobjc_type_class = NULL;
    } else {
        cobjc_type_class =
            (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_class);
    }

    if (!cprec_header->cobjc_type_id) {
        cobjc_type_id = NULL;
    } else {
        cobjc_type_id = (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_id);
    }

    if (!cprec_header->cobjc_type_sel) {
        cobjc_type_sel = NULL;
    } else {
        cobjc_type_sel = (Type*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_type_sel);
    }

    if (!cprec_header->cobjc_selhashtable) {
        cobjc_selhashtable = NULL;
    } else {
        cobjc_selhashtable = (struct ObjCSelector**)((UInt8*)cprec_rawbuffer +
                                                     (UInt32)cprec_header->cobjc_selhashtable);
    }

    if (!cprec_header->cobjc_classdefs) {
        cobjc_classdefs = NULL;
    } else {
        cobjc_classdefs =
            (struct BClassList*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_classdefs);
    }

    if (!cprec_header->cobjc_protocols) {
        cobjc_protocols = NULL;
    } else {
        cobjc_protocols =
            (struct ObjCProtocol*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cobjc_protocols);
    }

    if (!cprec_header->init_expressions) {
        init_expressions = NULL;
    } else {
        init_expressions =
            (struct InitExpr*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->init_expressions);
    }

    if (!cprec_header->cinline_tactionlist) {
        cinline_tactionlist = NULL;
    } else {
        cinline_tactionlist =
            (CI_Action*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->cinline_tactionlist);
    }

    CParser_SetUniqueID(cprec_header->uniqueID);
#if VERSION == VERSION_WII_1_0RC1
    cinit_tentative = cprec_header->tentativeData
        ? (ObjectList*)((UInt8*)cprec_rawbuffer + (UInt32)cprec_header->tentativeData)
        : NULL;
    CPrec_ReadBackend(size);
#endif
    cobjc_selrefcount = cprec_header->cobjc_selrefcount;
    cobjc_classrefcount = cprec_header->cobjc_classrefcount;
    cobjc_stringcount = cprec_header->cobjc_stringcount;

    cprec_refnum = 0;
    if (cprec_glist.data != NULL) {
        FreeGList(&cprec_glist);
    }
    cscope_current = cscope_root;
    if (!CParser_ReInitRuntimeObjects(TRUE)) {
        CError_ErrorTerm(ERR_ILLEGAL_DATA_PRECOMPILED_HEADER);
    }
    CPrep_InsertSpecialMacros();

    if (cparamblkptr->precompile != 1) {
        record = cprec_staticdata;
        cprec_staticdata = NULL;
#if VERSION == VERSION_WII_1_0RC1
        CPrec_DeclareSourceStaticData(record);
#else
        for (; record != NULL; record = record->next) {
            CInit_DeclareData(record->object, record->buffer, record->links, record->size);
        }
#endif
    }
#if VERSION == VERSION_WII_1_0RC1
    return cparamblkptr->preprocess;
#endif
}
#if VERSION == VERSION_GC_1_3_2
#undef CError_LINE_DELTA
#define CError_LINE_DELTA 0
#endif

#pragma pack(2)
