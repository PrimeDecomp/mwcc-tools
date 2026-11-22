#include "compiler/CIRStreamType.h"
#include "compiler/CClass.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/CFunc.h"
#include "compiler/CParser.h"
#include "compiler/CScope.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"

#define IR_CLASS_TEMPLATE_NAME 0x100000UL
#define IR_CLASS_TRIVIAL_COPY 0x20000UL

enum {
    IRCustomType = 23,
    IRPairedSingleType = 25,
    IRNullType = 31,
    IRIllegalType,
    IRVoidType,
    IRPatchedType,
    IRPointerType,
    IRQualifiedPointerType,
    IRArrayType,
    IREnumType,
    IRStructType,
    IRClassType,
    IRFunctionType,
    IRMethodType,
    IRMemberPointerType,
    IRBitfieldType,
    IRVectorType = 45,
    IRSPEType = 56,
    IRTypeTableBase = 63,
    MaxTypeDepth = 256,
    TypeBuckets = 1024
};

struct CIRStreamNamespace {
    CIRStreamNamespace* next;
    UInt32 index;
    NameSpace* nspace;
};

struct CIRStreamTypeEntry {
    CIRStreamTypeEntry* next;
    Type* type;
    UInt32 index;
};

// RC1 carries fields that the shared frontend type records do not store.
struct CIRStreamTypeDetails {
    CIRStreamTypeDetails* next;
    Type* type;
    union {
        struct {
            SInt32 bound;
            UInt8 flags[3];
        } array;
        struct {
            UInt16 attributes[2];
        } function;
        struct {
            UInt32 flags;
            UInt16 extra;
        } classType;
    } data;
};

static CIRStreamTypeDetails* remember_details(CIRStreamTypeContext* context, Type* type) {
    CIRStreamTypeDetails* details = galloc(sizeof(CIRStreamTypeDetails));
    details->next = context->wireDetails;
    details->type = type;
    context->wireDetails = details;
    return details;
}

static const CIRStreamTypeDetails* find_details(
    const CIRStreamTypeWriter* context, const Type* type
) {
    const CIRStreamTypeDetails* details;
    for (details = context->wireDetails; details; details = details->next) {
        if (details->type == type) {
            return details;
        }
    }
    return NULL;
}

static Boolean unpack_type(CIRStreamReader*, CIRStreamTypeContext*, Type**, UInt32);
static Boolean unpack_namespace(CIRStreamReader*, CIRStreamTypeContext*, NameSpace**, UInt32);

Boolean CIRStream_UnpackName(CIRStreamReader* stream, HashNameNode** name) {
    UInt32 size;
    UInt32 index;
    CIRStreamReader bytes;
    char* terminated;

    if (!CIRStream_UnpackUInt32(stream, &size)) {
        return FALSE;
    }
    if (!size) {
        *name = NULL;
        return TRUE;
    }
    if (size > 0x7fffffffUL || !CIRStream_UnpackData(stream, size - 1, &bytes)) {
        return FALSE;
    }
    terminated = lalloc(size);
    for (index = 0; index < bytes.size; ++index) {
        terminated[index] = (char)bytes.bytes[index];
    }
    terminated[index] = 0;
    *name = GetHashNameNodeExport(terminated);
    return TRUE;
}

static NameSpace* find_namespace(NameSpace* parent, HashNameNode* name) {
    NameSpaceObjectList* list;
    ObjNameSpace* obj;
    NameSpace* nspace;

    for (list = CScope_FindName(parent, name); list; list = list->next) {
        if (list->object->otype == OT_NAMESPACE) {
            return OBJ_NAMESPACE(list->object)->nspace;
        }
    }
    nspace = CScope_NewHashNameSpace(name);
    nspace->parent = parent;
    obj = galloc(sizeof(ObjNameSpace));
    obj->otype = OT_NAMESPACE;
    obj->access = ACCESSPUBLIC;
    obj->nspace = nspace;
    CScope_AddObject(parent, name, OBJ_BASE(obj));
    return nspace;
}

static Boolean unpack_namespace(
    CIRStreamReader* stream, CIRStreamTypeContext* context, NameSpace** result, UInt32 depth
) {
    UInt8 tag;
    UInt32 index;
    CIRStreamNamespace* entry;
    NameSpace* parent;
    HashNameNode* name;
    Type* type;

    if (depth == MaxTypeDepth || !CIRStream_UnpackByte(stream, &tag)) {
        return FALSE;
    }
    switch (tag) {
        case 0:
            *result = NULL;
            return TRUE;
        case 1:
            *result = context->root;
            return TRUE;
        case 2:
            if (!CIRStream_UnpackUInt32(stream, &index)) {
                return FALSE;
            }
            for (entry = context->namespaces; entry; entry = entry->next) {
                if (entry->index == index) {
                    *result = entry->nspace;
                    return TRUE;
                }
            }
            return FALSE;
        case 3:
            index = context->namespaceCount++;
            if (!unpack_namespace(stream, context, &parent, depth + 1) ||
                !parent ||
                !CIRStream_UnpackName(stream, &name) ||
                !name) {
                return FALSE;
            }
            entry = galloc(sizeof(CIRStreamNamespace));
            entry->index = index;
            entry->nspace = find_namespace(parent, name);
            entry->next = context->namespaces;
            context->namespaces = entry;
            *result = entry->nspace;
            return TRUE;
        case 4:
            if (!unpack_type(stream, context, &type, depth + 1) || !type || !IS_TYPE_CLASS(type)) {
                return FALSE;
            }
            *result = TYPE_CLASS(type)->nspace;
            return TRUE;
        default:
            return FALSE;
    }
}

static Type* builtin_type(Boolean pairedSingles, UInt32 tag) {
    static Type* const scalar[] = {
        TYPE(&stbool),
        TYPE(&stchar),
        TYPE(&stsignedchar),
        TYPE(&stunsignedchar),
        TYPE(&stwchar),
        TYPE(&stsignedshort),
        TYPE(&stunsignedshort),
        TYPE(&stsignedint),
        TYPE(&stunsignedint),
        TYPE(&stsignedlong),
        TYPE(&stunsignedlong),
        TYPE(&stsignedlonglong),
        TYPE(&stunsignedlonglong),
        TYPE(&stfloat),
        TYPE(&stshortdouble),
        TYPE(&stdouble),
        TYPE(&stlongdouble)
    };
    static Type* const vector[] = {
        TYPE(&stvectorunsignedchar),
        TYPE(&stvectorsignedchar),
        TYPE(&stvectorboolchar),
        TYPE(&stvectorunsignedshort),
        TYPE(&stvectorsignedshort),
        TYPE(&stvectorboolshort),
        TYPE(&stvectorunsignedlong),
        TYPE(&stvectorsignedlong),
        TYPE(&stvectorboollong),
        TYPE(&stvectorfloat),
        TYPE(&stvectorpixel)
    };

    if (tag < sizeof(scalar) / sizeof(scalar[0])) {
        return scalar[tag];
    }
    if (tag >= IRVectorType && tag - IRVectorType < sizeof(vector) / sizeof(vector[0])) {
        return vector[tag - IRVectorType];
    }
#if VERSION == VERSION_WII_1_0RC1
    if (tag >= IRSPEType && tag < IRTypeTableBase) {
        return TYPE(&stspevector[tag - IRSPEType]);
    }
#endif
    if (tag == IRPairedSingleType && pairedSingles) {
        return TYPE(&stvec2x32float);
    }
    if (tag == IRIllegalType) {
        return &stillegal;
    }
    if (tag == IRVoidType) {
        return &stvoid;
    }
    return NULL;
}

static Type* new_type(CIRStreamTypeContext* context, UInt32 size, TypeType kind) {
    Type* type;

    if (context->typeCount >= context->typeCapacity) {
        return NULL;
    }
    type = galloc(size);
    memclrw(type, size);
    type->type = kind;
    context->types[context->typeCount++] = type;
    return type;
}

static SInt32 array_bound(const TypePointer* array) {
    return array->size > 0 && array->target->size > 0 ? array->size / array->target->size : 0;
}

static Boolean unpack_pointer(
    CIRStreamReader* stream,
    CIRStreamTypeContext* context,
    TypePointer* pointer,
    UInt32 tag,
    UInt32 depth
) {
    SInt32 bound;
    CIRStreamReader flags;

    if (tag == IRArrayType) {
        if (!CIRStream_UnpackInt32(stream, &pointer->size) ||
            !unpack_type(stream, context, &pointer->target, depth) ||
            !pointer->target ||
            !CIRStream_UnpackUInt32(stream, &pointer->qual) ||
            !CIRStream_UnpackInt32(stream, &bound) ||
            !CIRStream_UnpackData(stream, 3, &flags)) {
            return FALSE;
        }
        if (context->keepWireDetails &&
            (flags.bytes[0] || flags.bytes[1] || flags.bytes[2] || bound != array_bound(pointer))) {
            CIRStreamTypeDetails* details = remember_details(context, TYPE(pointer));
            details->data.array.bound = bound;
            details->data.array.flags[0] = flags.bytes[0];
            details->data.array.flags[1] = flags.bytes[1];
            details->data.array.flags[2] = flags.bytes[2];
        }
        return TRUE;
    }
    pointer->size = 4;
    return (tag == IRPointerType || CIRStream_UnpackUInt32(stream, &pointer->qual)) &&
        unpack_type(stream, context, &pointer->target, depth) &&
        pointer->target;
}

static Boolean unpack_function(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeFunc* function, UInt32 depth
) {
    UInt32 argc;
    UInt8 tag;
    UInt16 storage;
    UInt16 attributes[2];
    FuncArg* arg;
    FuncArg** tail = &function->args;

    if (!unpack_type(stream, context, &function->functype, depth) ||
        !function->functype ||
        !CIRStream_UnpackUInt32(stream, &function->qual) ||
        !CIRStream_UnpackUInt32(stream, &function->flags) ||
        !CIRStream_UnpackUInt16(stream, &attributes[0]) ||
        !CIRStream_UnpackUInt16(stream, &attributes[1]) ||
        !CIRStream_UnpackUInt32(stream, &argc) ||
        argc > stream->size) {
        return FALSE;
    }
    if (context->keepWireDetails && (attributes[0] || attributes[1])) {
        CIRStreamTypeDetails* details = remember_details(context, TYPE(function));
        details->data.function.attributes[0] = attributes[0];
        details->data.function.attributes[1] = attributes[1];
    }
    for (; argc; --argc) {
        if (!CIRStream_UnpackByte(stream, &tag) || tag > 3) {
            return FALSE;
        }
        if (tag >= 2) {
            if (argc != 1) {
                return FALSE;
            }
            *tail = tag == 2 ? &elipsis : &oldstyle;
            return TRUE;
        }
        arg = galloc(sizeof(FuncArg));
        memclrw(arg, sizeof(FuncArg));
        arg->is_array = tag;
        if (!CIRStream_UnpackName(stream, &arg->name) ||
            !unpack_type(stream, context, &arg->type, depth) ||
            !arg->type ||
            !CIRStream_UnpackUInt32(stream, &arg->qual) ||
            !CIRStream_UnpackUInt16(stream, &storage)) {
            return FALSE;
        }
        arg->sclass = (SInt16)storage;
        *tail = arg;
        tail = &arg->next;
    }
    return TRUE;
}

static Boolean unpack_enum(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeEnum* type, UInt32 depth
) {
    UInt32 count;
    UInt8 access;
    ObjEnumConst** tail = &type->enumlist;
    ObjEnumConst* entry;
    CIRStreamReader value;

    if (!unpack_type(stream, context, &type->enumtype, depth) ||
        !type->enumtype ||
        !unpack_namespace(stream, context, &type->nspace, depth) ||
        !CIRStream_UnpackName(stream, &type->enumname) ||
        !CIRStream_UnpackUInt32(stream, &count) ||
        count > stream->size / 10) {
        return FALSE;
    }
    type->size = type->enumtype->size;
    for (; count; --count) {
        entry = galloc(sizeof(ObjEnumConst));
        memclrw(entry, sizeof(ObjEnumConst));
        if (!CIRStream_UnpackByte(stream, &access) ||
            !CIRStream_UnpackName(stream, &entry->name) ||
            !CIRStream_UnpackData(stream, 8, &value)) {
            return FALSE;
        }
        entry->otype = OT_ENUMCONST;
        entry->access = (AccessType)access;
        entry->type = TYPE(type);
        entry->val.hi = (SInt32)CIRStream_ReadWord(value.bytes);
        entry->val.lo = CIRStream_ReadWord(value.bytes + 4);
        *tail = entry;
        tail = &entry->next;
    }
    return TRUE;
}

static Boolean unpack_struct(
    CIRStreamReader* stream, CIRStreamTypeContext* context, Type** result, UInt32 depth
) {
    SInt32 size;
    UInt16 align;
    UInt8 kind;
    UInt32 count;
    HashNameNode* name;
    TypeStruct* type;
    StructMember** tail;
    StructMember* member;

    // The stock reader registers a struct after its prefix and before its members.
    if (!CIRStream_UnpackInt32(stream, &size) ||
        !CIRStream_UnpackName(stream, &name) ||
        !CIRStream_UnpackData(stream, 2, NULL)) {
        return FALSE;
    }
    type = TYPE_STRUCT(new_type(context, sizeof(TypeStruct), TYPESTRUCT));
    if (!type ||
        !CIRStream_UnpackUInt16(stream, &align) ||
        !CIRStream_UnpackByte(stream, &kind) ||
        !CIRStream_UnpackUInt32(stream, &count) ||
        count > stream->size / 4) {
        return FALSE;
    }
    type->name = name;
    type->size = size;
    type->align = (SInt16)align;
    type->stype = (char)kind;
    tail = &type->members;
    for (; count; --count) {
        member = galloc(sizeof(StructMember));
        member->next = NULL;
        if (!unpack_type(stream, context, &member->type, depth) ||
            !member->type ||
            !CIRStream_UnpackName(stream, &member->name) ||
            !CIRStream_UnpackInt32(stream, &member->offset) ||
            !CIRStream_UnpackUInt32(stream, &member->qual)) {
            return FALSE;
        }
        *tail = member;
        tail = &member->next;
    }
    *result = TYPE(type);
    return TRUE;
}

static Boolean unpack_bases(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeClass* type, UInt32 depth
) {
    ClassList** tail = &type->bases;
    ClassList* base;
    Type* target;
    UInt32 count;

    if (!CIRStream_UnpackUInt32(stream, &count) || count > stream->size / 3) {
        return FALSE;
    }
    for (; count; --count) {
        base = galloc(sizeof(ClassList));
        memclrw(base, sizeof(ClassList));
        if (!unpack_type(stream, context, &target, depth) ||
            !target ||
            !IS_TYPE_CLASS(target) ||
            !CIRStream_UnpackInt32(stream, &base->offset) ||
            !CIRStream_UnpackByte(stream, &base->is_virtual)) {
            return FALSE;
        }
        base->base = TYPE_CLASS(target);
        *tail = base;
        tail = &base->next;
    }
    return TRUE;
}

static Boolean unpack_virtual_bases(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeClass* type, UInt32 depth
) {
    VClassList** tail = &type->vbases;
    VClassList* base;
    Type* target;
    UInt32 count;

    if (!CIRStream_UnpackUInt32(stream, &count) || count > stream->size / 3) {
        return FALSE;
    }
    for (; count; --count) {
        base = galloc(sizeof(VClassList));
        memclrw(base, sizeof(VClassList));
        if (!unpack_type(stream, context, &target, depth) ||
            !target ||
            !IS_TYPE_CLASS(target) ||
            !CIRStream_UnpackInt32(stream, &base->offset) ||
            !CIRStream_UnpackInt32(stream, &base->voffset)) {
            return FALSE;
        }
        base->base = TYPE_CLASS(target);
        *tail = base;
        tail = &base->next;
    }
    return TRUE;
}

static Boolean unpack_members(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeClass* type, UInt32 depth
) {
    ObjMemberVar** tail = &type->ivars;
    ObjMemberVar* member;
    UInt32 count;
    UInt8 access;

    if (!CIRStream_UnpackUInt32(stream, &count) || count > stream->size / 6) {
        return FALSE;
    }
    for (; count; --count) {
        member = galloc(sizeof(ObjMemberVar));
        memclrw(member, sizeof(ObjMemberVar));
        member->otype = OT_MEMBERVAR;
        if (!CIRStream_UnpackByte(stream, &access) ||
            !CIRStream_UnpackByte(stream, &member->anonunion) ||
            !unpack_type(stream, context, &member->type, depth) ||
            !member->type ||
            !CIRStream_UnpackName(stream, &member->name) ||
            !CIRStream_UnpackUInt32(stream, &member->offset) ||
            !CIRStream_UnpackUInt32(stream, &member->qual)) {
            return FALSE;
        }
        member->access = (AccessType)access;
        *tail = member;
        tail = &member->next;
    }
    return TRUE;
}

static Boolean unpack_class(
    CIRStreamReader* stream, CIRStreamTypeContext* context, TypeClass* type, UInt32 depth
) {
    NameSpace* parent;
    HashNameNode* lookupName;
    UInt32 flags;
    UInt16 align;
    UInt16 extra;
    UInt8 mode;
    UInt8 hasVTable;

    // The enclosing class may refer to this class's members before its own
    // namespace record has finished. Give those references a stable identity.
    type->nspace = CScope_NewListNameSpace(NULL, TRUE);
    type->nspace->theclass = type;
    if (!CIRStream_UnpackInt32(stream, &type->size) ||
        !CIRStream_UnpackName(stream, &type->classname) ||
        !unpack_namespace(stream, context, &parent, depth) ||
        !CIRStream_UnpackUInt32(stream, &flags)) {
        return FALSE;
    }
    lookupName = type->classname;
    if ((flags & IR_CLASS_TEMPLATE_NAME) && !CIRStream_UnpackName(stream, &lookupName)) {
        return FALSE;
    }
    type->flags = (UInt16)flags;
    type->irCopyKind = flags & IR_CLASS_TRIVIAL_COPY ? ClassCopyTrivial : ClassCopyNontrivial;
    type->nspace->name = lookupName;
    type->nspace->parent = parent;

    if (!CIRStream_UnpackUInt16(stream, &align) ||
        !CIRStream_UnpackUInt16(stream, &extra) ||
        !CIRStream_UnpackByte(stream, &mode) ||
        !CIRStream_UnpackByte(stream, &type->eflags) ||
        !unpack_bases(stream, context, type, depth) ||
        !unpack_virtual_bases(stream, context, type, depth) ||
        !unpack_members(stream, context, type, depth) ||
        !CIRStream_UnpackByte(stream, &hasVTable)) {
        return FALSE;
    }
    type->mode = (SInt8)mode;
    type->align = (SInt16)align;
    if (context->keepWireDetails && (extra || (flags & 0xffff0000UL))) {
        CIRStreamTypeDetails* details = remember_details(context, TYPE(type));
        details->data.classType.flags = flags;
        details->data.classType.extra = extra;
    }
    if (hasVTable) {
        HashNameNode* vptr = GetHashNameNodeExport("__vptr$");
        const ObjMemberVar* member;
        const ClassList* base;

        type->vtable = galloc(sizeof(VTable));
        memclrw(type->vtable, sizeof(VTable));
        for (member = type->ivars; member; member = member->next) {
            if (member->name == vptr) {
                type->vtable->offset = member->offset;
                break;
            }
        }
        if (!member) {
            for (base = type->bases; base; base = base->next) {
                if (!base->is_virtual && !base->offset && base->base->vtable) {
                    type->vtable->offset = base->base->vtable->offset;
                    break;
                }
            }
        }
        return CIRStream_UnpackInt32(stream, &type->vtable->size);
    }
    return TRUE;
}

static Boolean unpack_type(
    CIRStreamReader* stream, CIRStreamTypeContext* context, Type** result, UInt32 depth
) {
    UInt32 tag;
    UInt32 index;
    Type* type;

    if (depth == MaxTypeDepth || !CIRStream_UnpackUInt32(stream, &tag)) {
        return FALSE;
    }
    ++depth;
    if (tag == IRNullType) {
        *result = NULL;
        return TRUE;
    }
    if (tag >= IRTypeTableBase) {
        index = tag - IRTypeTableBase;
        if (index >= context->typeCount) {
            return FALSE;
        }
        *result = context->types[index];
        return TRUE;
    }
    if (tag == IRPatchedType) {
        if (!CIRStream_UnpackUInt32(stream, &index) ||
            index >= context->typePatchCount ||
            !context->typePatches[index]) {
            return FALSE;
        }
        *result = context->typePatches[index];
        return TRUE;
    }
    type = builtin_type(context->pairedSingles, tag);
#if VERSION == VERSION_WII_1_0RC1
    if (!type && CPU_IS_SPE(copts.processor) && tag >= IRCustomType &&
        tag - IRCustomType < SPEBuiltinTypeCount) {
        type = TYPE(&stspevector[tag - IRCustomType]);
    }
#endif
    if (type) {
        *result = type;
        return TRUE;
    }
    switch (tag) {
        case IRPointerType:
        case IRQualifiedPointerType:
        case IRArrayType:
            type = new_type(
                context, sizeof(TypePointer), tag == IRArrayType ? TYPEARRAY : TYPEPOINTER
            );
            if (!type || !unpack_pointer(stream, context, TYPE_POINTER(type), tag, depth)) {
                return FALSE;
            }
            break;
        case IREnumType:
            type = new_type(context, sizeof(TypeEnum), TYPEENUM);
            if (!type || !unpack_enum(stream, context, TYPE_ENUM(type), depth)) {
                return FALSE;
            }
            break;
        case IRStructType:
            return unpack_struct(stream, context, result, depth);
        case IRClassType:
            type = new_type(context, sizeof(TypeClass), TYPECLASS);
            if (!type || !unpack_class(stream, context, TYPE_CLASS(type), depth)) {
                return FALSE;
            }
            break;
        case IRFunctionType:
        case IRMethodType: {
            TypeMemberFunc* method;
            Type* owner;
            type = new_type(
                context, tag == IRMethodType ? sizeof(TypeMemberFunc) : sizeof(TypeFunc), TYPEFUNC
            );
            if (!type || !unpack_function(stream, context, TYPE_FUNC(type), depth)) {
                return FALSE;
            }
            if (tag == IRMethodType) {
                method = TYPE_METHOD(type);
                if (!unpack_type(stream, context, &owner, depth) ||
                    (owner && !IS_TYPE_CLASS(owner)) ||
                    !CIRStream_UnpackInt32(stream, &method->vtbl_index) ||
                    !CIRStream_UnpackByte(stream, &method->is_static)) {
                    return FALSE;
                }
                method->theclass = TYPE_CLASS(owner);
            }
            break;
        }
        case IRMemberPointerType: {
            TypeMemberPointer* pointer = TYPE_MEMBER_POINTER(
                new_type(context, sizeof(TypeMemberPointer), TYPEMEMBERPOINTER)
            );
            if (!pointer ||
                !CIRStream_UnpackInt32(stream, &pointer->size) ||
                !unpack_type(stream, context, &pointer->ty1, depth) ||
                !pointer->ty1 ||
                !unpack_type(stream, context, &pointer->ty2, depth) ||
                !pointer->ty2 ||
                !CIRStream_UnpackUInt32(stream, &pointer->qual)) {
                return FALSE;
            }
            type = TYPE(pointer);
            break;
        }
        case IRBitfieldType: {
            TypeBitfield* field =
                TYPE_BITFIELD(new_type(context, sizeof(TypeBitfield), TYPEBITFIELD));
            CIRStreamReader bits;
            if (!field ||
                !unpack_type(stream, context, &field->bitfieldtype, depth) ||
                !field->bitfieldtype ||
                !CIRStream_UnpackData(stream, 2, &bits)) {
                return FALSE;
            }
            field->size = field->bitfieldtype->size;
            field->offset = (char)bits.bytes[0];
            field->bitlength = (char)bits.bytes[1];
            type = TYPE(field);
            break;
        }
        default:
            return FALSE;
    }
    *result = type;
    return TRUE;
}

Boolean CIRStream_UnpackType(CIRStreamReader* stream, CIRStreamTypeContext* context, Type** type) {
    return unpack_type(stream, context, type, 0);
}

Boolean CIRStream_UnpackNameSpace(
    CIRStreamReader* stream, CIRStreamTypeContext* context, NameSpace** nspace
) {
    return unpack_namespace(stream, context, nspace, 0);
}

static Boolean pack_type(CIRStreamWriter*, CIRStreamTypeWriter*, Type*, UInt32);

Boolean CIRStream_PackTypeHashNameNode(CIRStreamWriter* stream, const HashNameNode* name) {
    UInt32 length = 0;

    if (!name) {
        return CIRStream_WriteUInt32(stream, 0);
    }
    while (name->name[length]) {
        ++length;
    }
    return CIRStream_WriteUInt32(stream, length + 1) &&
        CIRStream_PackData(stream, (const UInt8*)name->name, length);
}

static Boolean pack_namespace(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, NameSpace* nspace, UInt32 depth
) {
    CIRStreamNamespace* entry;

    if (depth == MaxTypeDepth) {
        return FALSE;
    }
    if (!nspace || nspace == context->root) {
        return CIRStream_PackByte(stream, nspace != NULL);
    }
    if (nspace->theclass) {
        return CIRStream_PackByte(stream, 4) &&
            pack_type(stream, context, TYPE(nspace->theclass), depth + 1);
    }
    for (entry = context->namespaces; entry; entry = entry->next) {
        if (entry->nspace == nspace) {
            return CIRStream_PackByte(stream, 2) && CIRStream_WriteUInt32(stream, entry->index);
        }
    }
    if (!nspace->parent || !nspace->name) {
        return FALSE;
    }
    entry = lalloc(sizeof(CIRStreamNamespace));
    entry->index = context->namespaceCount++;
    entry->nspace = nspace;
    entry->next = context->namespaces;
    context->namespaces = entry;
    return CIRStream_PackByte(stream, 3) &&
        pack_namespace(stream, context, nspace->parent, depth + 1) &&
        CIRStream_PackTypeHashNameNode(stream, nspace->name);
}

// As in CTTool_FindHashedType, only pointer wrappers compare by structure.
static Boolean same_packed_type(Type* left, Type* right) {
    while (left != right) {
        if (!left ||
            !right ||
            !IS_TYPE_POINTER_ONLY(left) ||
            !IS_TYPE_POINTER_ONLY(right) ||
            left->size != right->size ||
            TYPE_POINTER(left)->qual != TYPE_POINTER(right)->qual) {
            return FALSE;
        }
        left = TPTR_TARGET(left);
        right = TPTR_TARGET(right);
    }
    return TRUE;
}

static Boolean CIRStream_GetTypeID(
    CIRStreamTypeWriter* context, Type* type, UInt32* index, Boolean* newType
) {
    Type* target = type;
    UInt32 hash = 0;
    UInt32 depth = 0;
    const UInt8* address = (const UInt8*)&target;
    UInt32 byte;
    CIRStreamTypeEntry** bucket;
    CIRStreamTypeEntry* entry;

    *newType = TRUE;
    if (IS_TYPE_BITFIELD(type)) {
        // Stock assigns a fresh ID to every bit field record.
        *index = context->typeCount++;
        return TRUE;
    }
    while (target && IS_TYPE_POINTER_ONLY(target)) {
        if (++depth == MaxTypeDepth) {
            return FALSE;
        }
        hash += TYPE_POINTER(target)->qual + (UInt32)target->size;
        target = TPTR_TARGET(target);
    }
    for (byte = 0; byte < sizeof(target); ++byte) {
        hash = hash * 33 + address[byte];
    }
    if (!context->types) {
        context->types = lalloc(TypeBuckets * sizeof(*context->types));
        memclrw(context->types, TypeBuckets * sizeof(*context->types));
    }
    bucket = &context->types[hash % TypeBuckets];
    for (entry = *bucket; entry; entry = entry->next) {
        if (same_packed_type(type, entry->type)) {
            *index = entry->index;
            *newType = FALSE;
            return TRUE;
        }
    }
    entry = lalloc(sizeof(CIRStreamTypeEntry));
    entry->index = *index = context->typeCount++;
    entry->type = type;
    entry->next = *bucket;
    *bucket = entry;
    return TRUE;
}

static Boolean pack_pointer(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypePointer* pointer, UInt32 depth
) {
    UInt32 qual = pointer->qual & ~Q_IS_OBJC_ID;
    static const UInt8 arrayFlags[3] = {0};

    if (!pointer->target) {
        return FALSE;
    }
    if (pointer->type == TYPEARRAY) {
        const CIRStreamTypeDetails* details = find_details(context, TYPE(pointer));
        SInt32 bound = details ? details->data.array.bound : array_bound(pointer);
        const UInt8* flags = details ? details->data.array.flags : arrayFlags;
        return CIRStream_PackByte(stream, IRArrayType) &&
            CIRStream_WriteInt32(stream, pointer->size) &&
            pack_type(stream, context, pointer->target, depth) &&
            CIRStream_WriteUInt32(stream, pointer->qual) &&
            CIRStream_WriteInt32(stream, bound) &&
            CIRStream_PackData(stream, flags, sizeof(arrayFlags));
    }
    return CIRStream_PackByte(stream, qual ? IRQualifiedPointerType : IRPointerType) &&
        (!qual || CIRStream_WriteUInt32(stream, qual)) &&
        pack_type(stream, context, pointer->target, depth);
}

static Boolean pack_function(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypeFunc* function, UInt32 depth
) {
    const FuncArg* arg;
    const CIRStreamTypeDetails* details = find_details(context, TYPE(function));
    UInt32 count = 0;
    Boolean method = IS_TYPEFUNC_METHOD(function);

    for (arg = function->args; arg; arg = arg->next) {
        ++count;
    }
    if (!function->functype ||
        !CIRStream_PackByte(stream, method ? IRMethodType : IRFunctionType) ||
        !pack_type(stream, context, function->functype, depth) ||
        !CIRStream_WriteUInt32(stream, function->qual) ||
        !CIRStream_WriteUInt32(stream, function->flags) ||
        !CIRStream_PackUInt16(stream, details ? details->data.function.attributes[0] : 0) ||
        !CIRStream_PackUInt16(stream, details ? details->data.function.attributes[1] : 0) ||
        !CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (arg = function->args; arg; arg = arg->next) {
        if (arg == &elipsis || arg == &oldstyle) {
            if (arg->next || !CIRStream_PackByte(stream, arg == &elipsis ? 2 : 3)) {
                return FALSE;
            }
        } else if (!arg->type ||
                   !CIRStream_PackByte(stream, arg->is_array != 0) ||
                   !CIRStream_PackTypeHashNameNode(stream, arg->name) ||
                   !pack_type(stream, context, arg->type, depth) ||
                   !CIRStream_WriteUInt32(stream, arg->qual) ||
                   !CIRStream_PackUInt16(stream, (UInt16)arg->sclass)) {
            return FALSE;
        }
    }
    if (method) {
        const TypeMemberFunc* member = TYPE_METHOD(function);
        return pack_type(stream, context, TYPE(member->theclass), depth) &&
            CIRStream_WriteInt32(stream, member->vtbl_index) &&
            CIRStream_PackByte(stream, member->is_static);
    }
    return TRUE;
}

static Boolean pack_enum(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypeEnum* type, UInt32 depth
) {
    const ObjEnumConst* entry;
    UInt32 count = 0;
    UInt8 value[8];

    for (entry = type->enumlist; entry; entry = entry->next) {
        ++count;
    }
    if (!type->enumtype ||
        !CIRStream_PackByte(stream, IREnumType) ||
        !pack_type(stream, context, type->enumtype, depth) ||
        !pack_namespace(stream, context, type->nspace, depth) ||
        !CIRStream_PackTypeHashNameNode(stream, type->enumname) ||
        !CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (entry = type->enumlist; entry; entry = entry->next) {
        CIRStream_WriteWord(value, (UInt32)entry->val.hi);
        CIRStream_WriteWord(value + 4, entry->val.lo);
        if (!CIRStream_PackByte(stream, entry->access) ||
            !CIRStream_PackTypeHashNameNode(stream, entry->name) ||
            !CIRStream_PackData(stream, value, sizeof(value))) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean pack_class_bases(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypeClass* type, UInt32 depth
) {
    const ClassList* base;
    const VClassList* vbase;
    UInt32 count = 0;

    for (base = type->bases; base; base = base->next) {
        ++count;
    }
    if (!CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (base = type->bases; base; base = base->next) {
        if (!base->base ||
            !pack_type(stream, context, TYPE(base->base), depth) ||
            !CIRStream_WriteInt32(stream, base->offset) ||
            !CIRStream_PackByte(stream, base->is_virtual)) {
            return FALSE;
        }
    }
    count = 0;
    for (vbase = type->vbases; vbase; vbase = vbase->next) {
        ++count;
    }
    if (!CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (vbase = type->vbases; vbase; vbase = vbase->next) {
        if (!vbase->base ||
            !pack_type(stream, context, TYPE(vbase->base), depth) ||
            !CIRStream_WriteInt32(stream, vbase->offset) ||
            !CIRStream_WriteInt32(stream, vbase->voffset)) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean pack_class_member(
    CIRStreamWriter* stream,
    CIRStreamTypeWriter* context,
    UInt8 access,
    UInt8 anonunion,
    Type* type,
    const HashNameNode* name,
    UInt32 offset,
    UInt32 qual,
    UInt32 depth
) {
    return type &&
        CIRStream_PackByte(stream, access) &&
        CIRStream_PackByte(stream, anonunion) &&
        pack_type(stream, context, type, depth) &&
        CIRStream_PackTypeHashNameNode(stream, name) &&
        CIRStream_WriteUInt32(stream, offset) &&
        CIRStream_WriteUInt32(stream, qual);
}

static Boolean pack_class(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypeClass* type, UInt32 depth
) {
    const ObjMemberVar* member;
    const CIRStreamTypeDetails* details = find_details(context, TYPE(type));
    UInt32 count = 0;
    UInt32 flags = type->flags;
    HashNameNode* templateName = NULL;

    if (!type->nspace || (flags & CLASS_IS_TEMPL)) {
        return FALSE;
    }
    if (details) {
        flags |= details->data.classType.flags & 0xffff0000UL;
    }
    if (CClass_IsTrivialCopyClass(type)) {
        flags |= IR_CLASS_TRIVIAL_COPY;
    }
    if (type->nspace->name != type->classname ||
        (flags & (CLASS_IS_TEMPL_INST | IR_CLASS_TEMPLATE_NAME))) {
        flags = (flags & ~CLASS_IS_TEMPL_INST) | IR_CLASS_TEMPLATE_NAME;
        templateName = type->nspace->name;
    }
    for (member = type->ivars; member; member = member->next) {
        ++count;
    }
    if (!CIRStream_PackByte(stream, IRClassType) ||
        !CIRStream_WriteInt32(stream, type->size) ||
        !CIRStream_PackTypeHashNameNode(stream, type->classname) ||
        !pack_namespace(stream, context, type->nspace->parent, depth) ||
        !CIRStream_WriteUInt32(stream, flags) ||
        ((flags & IR_CLASS_TEMPLATE_NAME) &&
         !CIRStream_PackTypeHashNameNode(stream, templateName)) ||
        !CIRStream_PackUInt16(stream, (UInt16)type->align) ||
        !CIRStream_PackUInt16(stream, details ? details->data.classType.extra : 0) ||
        !CIRStream_PackByte(stream, (UInt8)type->mode) ||
        !CIRStream_PackByte(stream, type->eflags) ||
        !pack_class_bases(stream, context, type, depth) ||
        !CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (member = type->ivars; member; member = member->next) {
        if (!pack_class_member(
                stream,
                context,
                member->access,
                member->anonunion,
                member->type,
                member->name,
                member->offset,
                member->qual,
                depth
            )) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(stream, type->vtable != NULL) &&
        (!type->vtable || CIRStream_WriteInt32(stream, type->vtable->size));
}

static Boolean pack_struct(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, TypeStruct* type, UInt32 depth
) {
    const StructMember* member;
    UInt32 count = 0;
    UInt32 flags = 0x270004;

    // Stock encodes C structures as class records with no bases or vtable.
    if (type->members) {
        flags |= CLASS_COMPLETED;
    }
    if (!type->name) {
        flags |= 0x400; // Local type.
    }
    for (member = type->members; member; member = member->next) {
        ++count;
    }
    if (!CIRStream_PackByte(stream, IRClassType) ||
        !CIRStream_WriteInt32(stream, type->size) ||
        !CIRStream_PackTypeHashNameNode(stream, type->name) ||
        !pack_namespace(stream, context, context->root, depth) ||
        !CIRStream_WriteUInt32(stream, flags) ||
        !CIRStream_PackUInt16(stream, (UInt16)type->align) ||
        !CIRStream_PackUInt16(stream, 0) ||
        !CIRStream_PackByte(stream, (UInt8)type->stype) ||
        !CIRStream_PackByte(stream, CLASS_EFLAGS_IMPORT) ||
        !CIRStream_WriteUInt32(stream, 0) ||
        !CIRStream_WriteUInt32(stream, 0) ||
        !CIRStream_WriteUInt32(stream, count)) {
        return FALSE;
    }
    for (member = type->members; member; member = member->next) {
        if (!pack_class_member(
                stream,
                context,
                ACCESSPUBLIC,
                0,
                member->type,
                member->name,
                (UInt32)member->offset,
                member->qual,
                depth
            )) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(stream, 0);
}

static Boolean pack_type(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, Type* type, UInt32 depth
) {
    UInt32 tag;
    UInt32 index;
    Boolean newType;

    if (depth == MaxTypeDepth) {
        return FALSE;
    }
    if (!type) {
        return CIRStream_PackByte(stream, IRNullType);
    }
#if VERSION == VERSION_WII_1_0RC1
    if (IS_TYPE_SPE(type)) {
        return CIRStream_PackByte(stream, IRSPEType + TYPE_STRUCT(type)->stype - STRUCT_SPE_U16);
    }
#endif
    for (tag = 0; tag < IRTypeTableBase; ++tag) {
        if (builtin_type(context->pairedSingles, tag) == type) {
            return CIRStream_PackByte(stream, (UInt8)tag);
        }
    }
    if (context->patchType) {
        return context->patchType(context->patchContext, type, &index) &&
            CIRStream_PackByte(stream, IRPatchedType) &&
            CIRStream_WriteUInt32(stream, index);
    }
    if (!CIRStream_GetTypeID(context, type, &index, &newType)) {
        return FALSE;
    }
    if (!newType) {
        return CIRStream_WriteUInt32(stream, IRTypeTableBase + index);
    }
    ++depth;
    switch (type->type) {
        case TYPEPOINTER:
        case TYPEARRAY:
            return pack_pointer(stream, context, TYPE_POINTER(type), depth);
        case TYPEENUM:
            return pack_enum(stream, context, TYPE_ENUM(type), depth);
        case TYPESTRUCT:
            return pack_struct(stream, context, TYPE_STRUCT(type), depth);
        case TYPECLASS:
            return pack_class(stream, context, TYPE_CLASS(type), depth);
        case TYPEFUNC:
            return pack_function(stream, context, TYPE_FUNC(type), depth);
        case TYPEMEMBERPOINTER: {
            const TypeMemberPointer* member = TYPE_MEMBER_POINTER(type);
            return member->ty1 &&
                member->ty2 &&
                CIRStream_PackByte(stream, IRMemberPointerType) &&
                CIRStream_WriteInt32(stream, member->size) &&
                pack_type(stream, context, member->ty1, depth) &&
                pack_type(stream, context, member->ty2, depth) &&
                CIRStream_WriteUInt32(stream, member->qual);
        }
        case TYPEBITFIELD: {
            const TypeBitfield* field = TYPE_BITFIELD(type);
            return field->bitfieldtype &&
                CIRStream_PackByte(stream, IRBitfieldType) &&
                pack_type(stream, context, field->bitfieldtype, depth) &&
                CIRStream_PackByte(stream, (UInt8)field->offset) &&
                CIRStream_PackByte(stream, (UInt8)field->bitlength);
        }
        default:
            return FALSE;
    }
}

Boolean CIRStream_PackType(CIRStreamWriter* stream, CIRStreamTypeWriter* context, Type* type) {
    return pack_type(stream, context, type, 0);
}

Boolean CIRStream_PackNameSpace(
    CIRStreamWriter* stream, CIRStreamTypeWriter* context, NameSpace* nspace
) {
    return pack_namespace(stream, context, nspace, 0);
}
