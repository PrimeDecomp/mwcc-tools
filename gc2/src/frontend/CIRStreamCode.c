#include "compiler/CIRStreamCode.h"
#include "compiler/CIRStream.h"
#include "compiler/CFunc.h"
#include "compiler/CMachine.h"
#include "compiler/CompilerTools.h"
#include "compiler/Exceptions.h"
#include "compiler/InlineAsm.h"
#include "compiler/InlineAsmPPC.h"
#include "compiler/Switch.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/tokens.h"

enum {
    WireEndStatements = 0x14,
    WireSetSourceFile,
    WireSetSourcePosition,
    WireClearSource,
    WireSetExceptionActions,
    WireInfoExpression = 0x42,
    WireMemberFunctionLiteral = 0x52,
    WireNullExpression = 0x53,
    WireInheritedType = 0x80,
    WireEndExceptionActions = 0x17,
    WireEndAssemblyPatches = 3,
    WireAssemblyPointerSize = 4,
    WireAssemblyHeaderSize = 8,
    WireAssemblyOperandSize = 14,
    WireAssemblyLabelDifference = 6,
    MaxExpressionDepth = 256
};

typedef struct CodeReader {
    CIRStreamReader stream;
    CIRStreamTypeContext types;
    const CIRStreamImportedGraph* graph;
    Object* const* objects;
    const CIRStreamFunctionDefinition* definition;
    HashNameNode* const* fileNames;
    Object* function;
    Object** locals;
    UInt32 nextLocal;
    CLabel* labels;
    CIRStreamFunctionCode* code;
} CodeReader;

static Boolean unpack_expression(CodeReader*, Type*, ENode**, UInt32);

static const CIRStreamReference* reference_at(CodeReader* reader, UInt32 index, UInt8 kind) {
    if (index >= reader->definition->header.referenceCount ||
        reader->definition->references[index].header.kind != kind) {
        return NULL;
    }
    return &reader->definition->references[index];
}

static Boolean unpack_file_id(CodeReader* reader, UInt32* fileID) {
    UInt32 index;
    const CIRStreamReference* reference;

    if (!CIRStream_UnpackUInt32(&reader->stream, &index)) {
        return FALSE;
    }
    *fileID = 0;
    if (index != 0xffffffffUL) {
        reference = reference_at(reader, index, CIRStreamRefFile);
        if (!reference) {
            return FALSE;
        }
        *fileID = reference->value.fileID;
    }
    return TRUE;
}

static Boolean unpack_position(CodeReader* reader, CIRStreamSourceLocation* location) {
    CIRStreamReader words;

    if (!CIRStream_UnpackData(&reader->stream, 8, &words)) {
        return FALSE;
    }
    location->offset = CIRStream_ReadWord(words.bytes);
    location->line = CIRStream_ReadWord(words.bytes + 4);
    return TRUE;
}

static Object* graph_object(CodeReader* reader, UInt32 index) {
    if (index >= reader->graph->header.nodeCount) {
        return NULL;
    }
    return reader->objects ? reader->objects[index] : reader->graph->nodes[index].object;
}

static Boolean unpack_object(CodeReader* reader, Object** result) {
    UInt8 tag;
    UInt32 index;
    const CIRStreamReference* reference;

    if (!CIRStream_UnpackByte(&reader->stream, &tag)) {
        return FALSE;
    }
    if (!tag) {
        *result = NULL;
        return TRUE;
    }
    if (!CIRStream_UnpackUInt32(&reader->stream, &index)) {
        return FALSE;
    }
    switch (tag) {
        case 1:
            reference = reference_at(reader, index, CIRStreamRefObject);
            if (!reference) {
                return FALSE;
            }
            *result = reader->graph ? graph_object(reader, reference->value.objectID)
                                    : reference->value.object;
            break;
        case 2:
            *result = index < reader->nextLocal ? reader->locals[index] : NULL;
            break;
        default:
            return FALSE;
    }
    return *result != NULL;
}

static Boolean unpack_label(CodeReader* reader, CLabel** label) {
    UInt32 index;

    if (!CIRStream_UnpackUInt32(&reader->stream, &index) ||
        index >= reader->definition->header.labelCount) {
        return FALSE;
    }
    *label = &reader->labels[index];
    return TRUE;
}

static Boolean unpack_locals(CodeReader* reader, ObjectList** list) {
    CIRStreamSourceLocation location;
    SInt32 count;
    UInt32 index;
    UInt8 storage;
    UInt8 hasSourceType;
    const CIRStreamReference* name;
    Object* local;
    ObjectList* entry;

    if (!CIRStream_UnpackInt32(&reader->stream, &count) ||
        count < 0 ||
        (UInt32)count > reader->definition->header.localCount - reader->nextLocal) {
        return FALSE;
    }
    for (; count; --count) {
        local = galloc(sizeof(Object));
        memclrw(local, sizeof(Object));
        local->otype = OT_OBJECT;
        local->access = ACCESSPUBLIC;
        local->datatype = DLOCAL;
        if (!CIRStream_UnpackUInt32(&reader->stream, &index)) {
            return FALSE;
        }
        if (index != 0xffffffffUL) {
            name = reference_at(reader, index, CIRStreamRefName);
            if (!name) {
                return FALSE;
            }
            local->name = name->value.name;
        }
        if (!CIRStream_UnpackType(&reader->stream, &reader->types, &local->type) ||
            !local->type ||
            !CIRStream_UnpackUInt32(&reader->stream, &local->qual) ||
            !CIRStream_UnpackByte(&reader->stream, &storage) ||
            (storage & 3) == 3 ||
            !CIRStream_UnpackInt32(&reader->stream, &local->u.var.uid) ||
            !unpack_file_id(reader, &location.fileID) ||
            !unpack_position(reader, &location) ||
            !CIRStream_UnpackByte(&reader->stream, &hasSourceType) ||
            hasSourceType > 1 ||
            (hasSourceType &&
             (!CIRStream_UnpackUInt32(&reader->stream, &index) ||
              !reference_at(reader, index, CIRStreamRefSourceType)))) {
            return FALSE;
        }
        local->sclass = (storage & 3) == 1 ? TK_REGISTER : (storage & 3) == 2 ? TK_AUTO : 0;
        if (storage & 0x80) {
            local->flags |= OBJECT_REFERENCED;
        }
        local->u.var.info = galloc(sizeof(VarInfo));
        memclrw(local->u.var.info, sizeof(VarInfo));
        local->u.var.info->func = reader->function;
        local->u.var.info->deftoken.tokenoffset = (SInt32)location.offset;
        local->u.var.info->deftoken.tokenline = (SInt32)location.line;
        if (location.fileID || hasSourceType || (storage & 0x7c)) {
            CIRStreamLocalInfo* info = galloc(sizeof(CIRStreamLocalInfo));
            info->object = local;
            info->fileID = location.fileID;
            info->extraStorageFlags = storage & 0x7c;
            info->hasSourceType = hasSourceType;
            info->next = reader->code->localInfo;
            reader->code->localInfo = info;
        }
        reader->locals[reader->nextLocal++] = local;
        entry = galloc(sizeof(ObjectList));
        entry->object = local;
        entry->next = NULL;
        *list = entry;
        list = &entry->next;
    }
    return TRUE;
}

static Boolean unpack_integer(CodeReader* reader, CInt64* value) {
    CIRStreamReader words;

    if (!CIRStream_UnpackData(&reader->stream, 8, &words)) {
        return FALSE;
    }
    value->hi = (SInt32)CIRStream_ReadWord(words.bytes);
    value->lo = CIRStream_ReadWord(words.bytes + 4);
    return TRUE;
}

static Boolean unpack_call(CodeReader* reader, ENode* expr, UInt32 depth) {
    ENodeList** tail = &expr->data.funccall.args;
    ENodeList* arg;
    UInt8 end;
    Type* type;

    if (!unpack_expression(reader, expr->rtype, &expr->data.funccall.funcref, depth) ||
        !expr->data.funccall.funcref) {
        return FALSE;
    }
    while (reader->stream.size && reader->stream.bytes[0] != WireNullExpression) {
        arg = galloc(sizeof(ENodeList));
        if (!unpack_expression(reader, TYPE(&stsignedint), &arg->node, depth) || !arg->node) {
            return FALSE;
        }
        arg->next = NULL;
        *tail = arg;
        tail = &arg->next;
    }
    if (!CIRStream_UnpackByte(&reader->stream, &end) ||
        !CIRStream_UnpackType(&reader->stream, &reader->types, &type) ||
        !type ||
        !IS_TYPE_FUNC(type)) {
        return FALSE;
    }
    expr->data.funccall.functype = TYPE_FUNC(type);
    expr->hascall = TRUE;
    return TRUE;
}

static CIRStreamExpressionInfo* remember_expression_info(CodeReader* reader, ENode* expression) {
    CIRStreamExpressionInfo* info = reader->code->expressionInfo;

    if (!info || info->expression != expression) {
        info = galloc(sizeof(CIRStreamExpressionInfo));
        memclrw(info, sizeof(CIRStreamExpressionInfo));
        info->expression = expression;
        info->next = reader->code->expressionInfo;
        reader->code->expressionInfo = info;
    }
    return info;
}

static Boolean unpack_string(CodeReader* reader, ENode* expr) {
    CIRStreamReader bytes;
    UInt32 index;
    UInt8 kind;

    if (!CIRStream_UnpackInt32(&reader->stream, &expr->data.string.size) ||
        expr->data.string.size < 0 ||
        !CIRStream_UnpackData(&reader->stream, expr->data.string.size, &bytes) ||
        !CIRStream_UnpackInt32(&reader->stream, &expr->data.string.sequence) ||
        expr->data.string.sequence < 0 ||
        !CIRStream_UnpackByte(&reader->stream, &expr->data.string.ispascal) ||
        !CIRStream_UnpackByte(&reader->stream, &kind)) {
        return FALSE;
    }
    expr->data.string.data = galloc(bytes.size);
    for (index = 0; index < bytes.size; ++index) {
        expr->data.string.data[index] = (char)bytes.bytes[index];
    }
    if (kind) {
        CIRStreamExpressionInfo* info = remember_expression_info(reader, expr);
        info->stringKind = kind;
    }
    return TRUE;
}

static Boolean unpack_expression_header(CodeReader* reader, UInt8 tag, ENode* expr, UInt32* flags) {
    if (!CIRStream_UnpackByte(&reader->stream, &expr->cost) ||
        !CIRStream_UnpackUInt32(&reader->stream, flags) ||
        (!(tag & WireInheritedType) &&
         !CIRStream_UnpackType(&reader->stream, &reader->types, &expr->rtype))) {
        return FALSE;
    }
    expr->flags = (UInt16)*flags;
    return expr->rtype != NULL;
}

Boolean CIRStream_DecodeExpressionKind(UInt8 tag, ENodeType* type) {
    if (tag <= EBSET) {
        *type = (ENodeType)tag;
        return TRUE;
    }
    if (tag >= 50 && tag <= 53) {
        *type = (ENodeType)(tag - 2);
        return TRUE;
    }
    if (tag >= 55 && tag <= 59) {
        *type = (ENodeType)(tag - 3);
        return TRUE;
    }
    switch (tag) {
        case 60:
            *type = ENULLCHECK;
            return TRUE;
        case 61:
            *type = EPRECOMP;
            return TRUE;
        case 62:
            *type = ELABEL;
            return TRUE;
        case 0x42:
            *type = ESCOPE;
            return TRUE;
        case 67:
            *type = EMFPOINTER;
            return TRUE;
        case 68:
            *type = ETEMP;
            return TRUE;
        case 74:
            *type = EINITTRYCATCH;
            return TRUE;
        case WireMemberFunctionLiteral:
            *type = EMEMBERFUNCLITERAL;
            return TRUE;
        case 87:
            *type = EVECTOR128CONST;
            return TRUE;
        default:
            return FALSE;
    }
}

static Boolean unpack_expression(CodeReader* reader, Type* expected, ENode** result, UInt32 depth) {
    ENode* expr;
    ENodeType type;
    UInt8 tag;
    UInt32 index;
    UInt32 flags;
    CIRStreamReader bits;

    if (depth == MaxExpressionDepth || !CIRStream_UnpackByte(&reader->stream, &tag)) {
        return FALSE;
    }
    if (tag == WireNullExpression) {
        *result = NULL;
        return TRUE;
    }
    if (!CIRStream_DecodeExpressionKind(tag & 0x7f, &type)) {
        return FALSE;
    }
    expr = galloc(sizeof(ENode));
    memclrw(expr, sizeof(ENode));
    expr->type = type;
    expr->rtype = expected;
    if (!unpack_expression_header(reader, tag, expr, &flags)) {
        return FALSE;
    }
    if ((flags & 0xffff0000UL) || type == EMEMBERFUNCLITERAL) {
        remember_expression_info(reader, expr)->extraFlags = flags & 0xffff0000UL;
    }
    *result = expr;
    ++depth;
    switch (type) {
ENODE_CASE_MONADIC:
        return unpack_expression(reader, expr->rtype, &expr->data.monadic, depth) &&
            expr->data.monadic;
ENODE_CASE_DIADIC_ALL:
        return unpack_expression(reader, expr->rtype, &expr->data.diadic.left, depth) &&
            expr->data.diadic.left &&
            unpack_expression(reader, expr->rtype, &expr->data.diadic.right, depth) &&
            expr->data.diadic.right;
        case EINTCONST:
            return unpack_integer(reader, &expr->data.intval);
        case EFLOATCONST:
            if (!CIRStream_UnpackData(&reader->stream, 8, &bits)) {
                return FALSE;
            }
            expr->data.floatval.data.words[0] = (SInt32)CIRStream_ReadWord(bits.bytes);
            expr->data.floatval.data.words[1] = (SInt32)CIRStream_ReadWord(bits.bytes + 4);
            return TRUE;
        case EVECTOR128CONST:
            if (!CIRStream_UnpackData(&reader->stream, 16, &bits)) {
                return FALSE;
            }
            for (index = 0; index < 16; ++index) {
                expr->data.vector128val.uc[index] = bits.bytes[index];
            }
            return TRUE;
        case ESTRINGCONST:
            return unpack_string(reader, expr);
        case ECOND:
            return unpack_expression(reader, expr->rtype, &expr->data.cond.cond, depth) &&
                unpack_expression(reader, expr->rtype, &expr->data.cond.expr1, depth) &&
                unpack_expression(reader, expr->rtype, &expr->data.cond.expr2, depth);
        case EFUNCCALL:
        case EFUNCCALLP:
            return unpack_call(reader, expr, depth);
        case EOBJREF:
        case EMEMBERFUNCLITERAL:
            return unpack_object(reader, &expr->data.objref) && expr->data.objref;
        case ESCOPE:
            return unpack_expression(reader, expr->rtype, &expr->data.scope.object, depth) &&
                (!expr->data.scope.object || expr->data.scope.object->type == EOBJREF) &&
                CIRStream_UnpackByte(&reader->stream, &expr->data.scope.kind);
        case EMFPOINTER:
            return unpack_expression(
                       reader, expr->rtype, &expr->data.mfpointer.accessnode, depth
                   ) &&
                unpack_expression(reader, expr->rtype, &expr->data.mfpointer.mfpointer, depth);
        case ENULLCHECK:
            return unpack_expression(
                       reader, expr->rtype, &expr->data.nullcheck.nullcheckexpr, depth
                   ) &&
                unpack_expression(reader, expr->rtype, &expr->data.nullcheck.condexpr, depth) &&
                CIRStream_UnpackInt32(&reader->stream, &expr->data.nullcheck.precompid);
        case EPRECOMP:
            return CIRStream_UnpackInt32(&reader->stream, &expr->data.precompid);
        case ELABEL:
            return unpack_label(reader, &expr->data.label);
        case ETEMP:
            return CIRStream_UnpackType(&reader->stream, &reader->types, &expr->data.temp.type) &&
                CIRStream_UnpackInt32(&reader->stream, &expr->data.temp.uniqueid) &&
                CIRStream_UnpackByte(&reader->stream, &expr->data.temp.needs_dtor);
        case EINITTRYCATCH:
            return unpack_expression(reader, expr->rtype, &expr->data.itc.initexpr, depth) &&
                unpack_expression(reader, expr->rtype, &expr->data.itc.tryexpr, depth) &&
                unpack_expression(reader, expr->rtype, &expr->data.itc.catchexpr, depth) &&
                unpack_expression(reader, expr->rtype, &expr->data.itc.result, depth);
        default:
            return FALSE;
    }
}

static Boolean unpack_switch(CodeReader* reader, SwitchInfo* info) {
    SwitchCase** tail = &info->cases;
    SwitchCase* entry;
    CInt64 max;
    UInt32 count;

    if (!CIRStream_UnpackUInt32(&reader->stream, &count) || count > reader->stream.size / 17) {
        return FALSE;
    }
    for (; count; --count) {
        entry = galloc(sizeof(SwitchCase));
        if (!unpack_label(reader, &entry->label) ||
            !unpack_integer(reader, &entry->min) ||
            !unpack_integer(reader, &max) ||
            max.hi != entry->min.hi ||
            max.lo != entry->min.lo) {
            return FALSE;
        }
        entry->next = NULL;
        *tail = entry;
        tail = &entry->next;
    }
    // The second case list carries expression-valued bounds.
    return CIRStream_UnpackUInt32(&reader->stream, &count) &&
        !count &&
        unpack_label(reader, &info->defaultlabel) &&
        CIRStream_UnpackType(&reader->stream, &reader->types, &info->exprtype) &&
        info->exprtype;
}

static Boolean unpack_exception_action(CodeReader* reader, ExceptionAction* action) {
    switch (action->type) {
        case EAT_DESTROYLOCAL:
            return unpack_object(reader, &action->data.destroy_local.local) &&
                unpack_object(reader, &action->data.destroy_local.dtor);
        case EAT_DESTROYLOCALCOND:
            return unpack_object(reader, &action->data.destroy_local_cond.local) &&
                unpack_object(reader, &action->data.destroy_local_cond.dtor) &&
                unpack_object(reader, &action->data.destroy_local_cond.cond);
        case EAT_DESTROYLOCALOFFSET:
            return unpack_object(reader, &action->data.destroy_local_offset.local) &&
                unpack_object(reader, &action->data.destroy_local_offset.dtor) &&
                CIRStream_UnpackInt32(&reader->stream, &action->data.destroy_local_offset.offset);
        case EAT_DESTROYLOCALPOINTER:
            return unpack_object(reader, &action->data.destroy_local_pointer.pointer) &&
                unpack_object(reader, &action->data.destroy_local_pointer.dtor);
        case EAT_DESTROYLOCALARRAY:
            return unpack_object(reader, &action->data.destroy_local_array.localarray) &&
                unpack_object(reader, &action->data.destroy_local_array.dtor) &&
                CIRStream_UnpackInt32(
                       &reader->stream, &action->data.destroy_local_array.elements
                ) &&
                CIRStream_UnpackInt32(
                       &reader->stream, &action->data.destroy_local_array.element_size
                );
        case EAT_DESTROYPARTIALARRAY:
            return unpack_object(reader, &action->data.destroy_partial_array.arraypointer) &&
                unpack_object(reader, &action->data.destroy_partial_array.arraycounter) &&
                unpack_object(reader, &action->data.destroy_partial_array.dtor) &&
                unpack_object(reader, &action->data.destroy_partial_array.element_size);
        case EAT_DESTROYMEMBER:
        case EAT_DESTROYBASE:
            return unpack_object(reader, &action->data.destroy_member.objectptr) &&
                unpack_object(reader, &action->data.destroy_member.dtor) &&
                CIRStream_UnpackInt32(&reader->stream, &action->data.destroy_member.offset);
        case EAT_DESTROYMEMBERCOND:
            return unpack_object(reader, &action->data.destroy_member_cond.objectptr) &&
                unpack_object(reader, &action->data.destroy_member_cond.dtor) &&
                unpack_object(reader, &action->data.destroy_member_cond.cond) &&
                CIRStream_UnpackInt32(&reader->stream, &action->data.destroy_member_cond.offset);
        case EAT_DESTROYMEMBERARRAY:
            return unpack_object(reader, &action->data.destroy_member_array.objectptr) &&
                unpack_object(reader, &action->data.destroy_member_array.dtor) &&
                CIRStream_UnpackInt32(&reader->stream, &action->data.destroy_member_array.offset) &&
                CIRStream_UnpackInt32(
                       &reader->stream, &action->data.destroy_member_array.elements
                ) &&
                CIRStream_UnpackInt32(
                       &reader->stream, &action->data.destroy_member_array.element_size
                );
        case EAT_DELETEPOINTER:
        case EAT_DELETELOCALPOINTER:
            return unpack_object(reader, &action->data.delete_pointer.pointerobject) &&
                unpack_object(reader, &action->data.delete_pointer.deletefunc);
        case EAT_DELETEPOINTERCOND:
            return unpack_object(reader, &action->data.delete_pointer_cond.pointerobject) &&
                unpack_object(reader, &action->data.delete_pointer_cond.deletefunc) &&
                unpack_object(reader, &action->data.delete_pointer_cond.cond);
        case EAT_CATCHBLOCK:
            return unpack_object(reader, &action->data.catch_block.catch_object) &&
                unpack_object(reader, &action->data.catch_block.catch_info_object) &&
                unpack_label(reader, &action->data.catch_block.catch_label) &&
                unpack_object(reader, &action->data.catch_block.catch_typeid) &&
                CIRStream_UnpackType(
                       &reader->stream, &reader->types, &action->data.catch_block.catch_type
                ) &&
                CIRStream_UnpackUInt32(&reader->stream, &action->data.catch_block.catch_qual);
        case EAT_ACTIVECATCHBLOCK:
            return unpack_object(reader, &action->data.active_catch_block.catch_info_object) &&
                CIRStream_UnpackByte(&reader->stream, &action->data.active_catch_block.call_dtor);
        case EAT_SPECIFICATION: {
            SInt32 count;
            SInt32 index;

            if (!CIRStream_UnpackInt32(&reader->stream, &count) ||
                count < 0 ||
                (UInt32)count > reader->stream.size ||
                (UInt32)count > 0x7fffffffUL / sizeof(Object*)) {
                return FALSE;
            }
            action->data.specification.unexp_ids = count;
            if (count) {
                action->data.specification.unexp_id = galloc(count * sizeof(Object*));
            }
            for (index = 0; index < count; ++index) {
                if (!unpack_object(reader, &action->data.specification.unexp_id[index])) {
                    return FALSE;
                }
            }
            return unpack_label(reader, &action->data.specification.unexp_label) &&
                unpack_object(reader, &action->data.specification.unexp_info_object);
        }
        case EAT_TERMINATE:
            return TRUE;
        default:
            return FALSE;
    }
}

static Boolean unpack_exception_actions(CodeReader* reader, ExceptionAction** tail) {
    UInt8 tag;

    while (CIRStream_UnpackByte(&reader->stream, &tag)) {
        ExceptionAction* action;

        if (tag == WireEndExceptionActions) {
            *tail = NULL;
            return TRUE;
        }
        if (!tag || tag >= EAT_NACTIONS) {
            return FALSE;
        }
        action = galloc(sizeof(ExceptionAction));
        memclrw(action, sizeof(ExceptionAction));
        action->type = (ExceptionActionType)tag;
        if (!unpack_exception_action(reader, action)) {
            return FALSE;
        }
        *tail = action;
        tail = &action->prev;
    }
    return FALSE;
}

static Boolean assembly_patch_fits(UInt32 size, UInt32 offset) {
    return size >= WireAssemblyPointerSize && offset <= size - WireAssemblyPointerSize;
}

static Boolean unpack_assembly(CodeReader* reader, CIRStreamAssembly* assembly) {
    CIRStreamAssemblyPatch** tail = &assembly->patches;
    CIRStreamAssemblyPatch* patch;
    UInt32 size;
    UInt32 offset;
    UInt8 kind;

    if (!CIRStream_UnpackUInt32(&reader->stream, &size) ||
        !CIRStream_UnpackData(&reader->stream, size, &assembly->instruction)) {
        return FALSE;
    }
    while (CIRStream_UnpackByte(&reader->stream, &kind)) {
        if (kind == WireEndAssemblyPatches) {
            return TRUE;
        }
        if (kind > CIRStreamAssemblyType ||
            !CIRStream_UnpackUInt32(&reader->stream, &offset) ||
            !assembly_patch_fits(size, offset)) {
            return FALSE;
        }
        patch = galloc(sizeof(CIRStreamAssemblyPatch));
        patch->kind = (CIRStreamAssemblyPatchKind)kind;
        patch->offset = offset;
        patch->next = NULL;
        switch (patch->kind) {
            case CIRStreamAssemblyObject:
                if (!unpack_object(reader, &patch->value.object)) {
                    return FALSE;
                }
                break;
            case CIRStreamAssemblyLabel:
                if (!unpack_label(reader, &patch->value.label)) {
                    return FALSE;
                }
                break;
            case CIRStreamAssemblyType:
                if (!CIRStream_UnpackType(&reader->stream, &reader->types, &patch->value.type)) {
                    return FALSE;
                }
                break;
        }
        *tail = patch;
        tail = &patch->next;
    }
    return FALSE;
}

static Boolean unpack_statements(CodeReader* reader, CIRStreamFunctionCode* code) {
    CIRStreamSourceLocation location = {0};
    Statement** tail = &code->statements;
    CIRStreamCodeInfo** infoTail = &code->info;
    CIRStreamStatementLocation** locationTail = &code->sourceLocations;
    CIRStreamAssembly** assemblyTail = &code->assembly;
    Boolean locationChanged = FALSE;
    ExceptionAction* actions = NULL;
    Statement* stmt;
    UInt8 tag;

    while (CIRStream_UnpackByte(&reader->stream, &tag)) {
        switch (tag) {
            case WireEndStatements:
                return !reader->stream.size;
            case WireSetSourceFile:
                if (!unpack_file_id(reader, &location.fileID)) {
                    return FALSE;
                }
                locationChanged = TRUE;
                continue;
            case WireSetSourcePosition:
                if (!unpack_position(reader, &location)) {
                    return FALSE;
                }
                locationChanged = TRUE;
                continue;
            case WireClearSource:
                memclrw(&location, sizeof(location));
                locationChanged = TRUE;
                continue;
            case WireSetExceptionActions:
                if (!unpack_exception_actions(reader, &actions)) {
                    return FALSE;
                }
                continue;
        }
        stmt = galloc(sizeof(Statement));
        memclrw(stmt, sizeof(Statement));
        stmt->type = (StatementType)tag;
        stmt->sourceoffset = (SInt32)location.offset;
        stmt->sourcefilepath = reader->fileNames[location.fileID];
        if (locationChanged) {
            CIRStreamStatementLocation* entry = galloc(sizeof(CIRStreamStatementLocation));
            entry->statement = stmt;
            entry->location = location;
            entry->next = NULL;
            *locationTail = entry;
            locationTail = &entry->next;
            locationChanged = FALSE;
        }
        stmt->dobjstack = actions;
        if (!CIRStream_UnpackByte(&reader->stream, &stmt->flags) ||
            !CIRStream_UnpackUInt16(&reader->stream, &stmt->value)) {
            return FALSE;
        }
        switch (stmt->type) {
            case ST_NOP:
                break;
            case ST_LABEL:
                if (!unpack_label(reader, &stmt->label) || stmt->label->stmt) {
                    return FALSE;
                }
                stmt->label->stmt = stmt;
                break;
            case ST_GOTO:
                if (!unpack_label(reader, &stmt->label)) {
                    return FALSE;
                }
                break;
            case ST_ASM: {
                CIRStreamAssembly* assembly = galloc(sizeof(CIRStreamAssembly));
                memclrw(assembly, sizeof(CIRStreamAssembly));
                assembly->statement = stmt;
                if (!unpack_assembly(reader, assembly)) {
                    return FALSE;
                }
                *assemblyTail = assembly;
                assemblyTail = &assembly->next;
                break;
            }
            case ST_EXPRESSION:
            case ST_RETURN:
            case ST_BEGINCATCH:
            case ST_ENDCATCH:
            case ST_ENDCATCHDTOR:
            case ST_GOTOEXPR:
            case ST_IFGOTO:
            case ST_IFNGOTO:
            case ST_SWITCH:
                // RC1's gen_INFO emits no code. Keep its payload beside the
                // statement so the shared executable IR sees an ST_NOP.
                if (stmt->type == ST_EXPRESSION &&
                    reader->stream.size &&
                    (reader->stream.bytes[0] & 0x7f) == WireInfoExpression) {
                    CIRStreamCodeInfo* info = galloc(sizeof(CIRStreamCodeInfo));
                    ENode header;

                    header.rtype = TYPE(&stsignedint);
                    if (!CIRStream_UnpackByte(&reader->stream, &tag) ||
                        !unpack_expression_header(reader, tag, &header, &info->flags) ||
                        !unpack_expression(reader, header.rtype, &info->operand, 0) ||
                        !CIRStream_UnpackByte(&reader->stream, &info->kind)) {
                        return FALSE;
                    }
                    stmt->type = ST_NOP;
                    info->statement = stmt;
                    info->type = header.rtype;
                    info->cost = header.cost;
                    info->next = NULL;
                    *infoTail = info;
                    infoTail = &info->next;
                    break;
                }
                if (!unpack_expression(reader, TYPE(&stsignedint), &stmt->expr, 0) ||
                    (!stmt->expr &&
                     (stmt->type == ST_SWITCH ||
                      stmt->type == ST_IFGOTO ||
                      stmt->type == ST_IFNGOTO))) {
                    return FALSE;
                }
                if (stmt->type == ST_IFGOTO || stmt->type == ST_IFNGOTO) {
                    if (!unpack_label(reader, &stmt->label)) {
                        return FALSE;
                    }
                } else if (stmt->type == ST_SWITCH) {
                    SwitchInfo* info = galloc(sizeof(SwitchInfo));
                    memclrw(info, sizeof(SwitchInfo));
                    if (!unpack_switch(reader, info)) {
                        return FALSE;
                    }
                    stmt->label = (CLabel*)info;
                }
                break;
            default:
                return FALSE;
        }
        *tail = stmt;
        tail = &stmt->next;
    }
    return FALSE;
}

static Boolean unpack_code(CodeReader reader, CIRStreamFunctionCode* result) {
    CIRStreamFunctionCode code = {0};
    const CIRStreamFunctionHeader* header = &reader.definition->header;
    UInt32 index;

    reader.code = &code;
    if (!reader.function ||
        !header->code.size ||
        header->localCount > header->code.size / 15 ||
        header->labelCount > header->code.size / 5 ||
        header->typeCount > header->code.size ||
        header->localCount > 0x7fffffffUL / sizeof(Object*) ||
        header->labelCount > 0x7fffffffUL / sizeof(CLabel) ||
        header->typeCount > 0x7fffffffUL / sizeof(Type*) ||
        header->referenceCount > 0x7fffffffUL / sizeof(Type*)) {
        return FALSE;
    }
    reader.stream = header->code;
    reader.locals = galloc(header->localCount * sizeof(Object*));
    reader.labels = galloc(header->labelCount * sizeof(CLabel));
    memclrw(reader.labels, header->labelCount * sizeof(CLabel));
    reader.types.typeCapacity = header->typeCount;
    reader.types.types = galloc(header->typeCount * sizeof(Type*));
    reader.types.typePatchCount = header->referenceCount;
    reader.types.typePatches = galloc(header->referenceCount * sizeof(Type*));
    for (index = 0; index < header->referenceCount; ++index) {
        const CIRStreamReference* reference = &reader.definition->references[index];
        reader.types.typePatches[index] =
            reference->header.kind == CIRStreamRefType ? reference->value.type : NULL;
    }
    for (index = 0; index < 3; ++index) {
        if (!unpack_file_id(&reader, &code.location[index].fileID) ||
            !unpack_position(&reader, &code.location[index])) {
            return FALSE;
        }
    }
    if (!unpack_locals(&reader, &code.arguments) ||
        !unpack_locals(&reader, &code.locals) ||
        reader.nextLocal != header->localCount ||
        !CIRStream_UnpackByte(&reader.stream, &code.oldStyleParameters) ||
        !CIRStream_UnpackByte(&reader.stream, &code.callOnModuleBind) ||
        !unpack_statements(&reader, &code) ||
        reader.types.typeCount != header->typeCount) {
        return FALSE;
    }
    for (index = 0; index < header->labelCount; ++index) {
        if (!reader.labels[index].stmt) {
            return FALSE;
        }
    }
    *result = code;
    return TRUE;
}

Boolean CIRStream_UnpackCode(
    const CIRStreamImportedGraph* graph,
    UInt32 nodeID,
    Object* const* objects,
    CIRStreamFunctionCode* result
) {
    CodeReader reader = {0};

    if (nodeID >= graph->header.nodeCount || graph->nodes[nodeID].kind != CIRStreamFunctionNode) {
        return FALSE;
    }
    reader.graph = graph;
    reader.objects = objects;
    reader.definition = &graph->nodes[nodeID].definition.function;
    reader.fileNames = graph->fileNames;
    reader.function = graph_object(&reader, nodeID);
    reader.types.root = graph->root;
    reader.types.pairedSingles = graph->pairedSingles;
    return unpack_code(reader, result);
}

Boolean CIRStream_UnpackPrecompiledCode(
    Object* function,
    NameSpace* root,
    Boolean pairedSingles,
    const CIRStreamFunctionDefinition* definition,
    HashNameNode* const* fileNames,
    CIRStreamFunctionCode* result
) {
    CodeReader reader = {0};

    reader.definition = definition;
    reader.fileNames = fileNames;
    reader.function = function;
    reader.types.root = root;
    reader.types.pairedSingles = pairedSingles;
    return unpack_code(reader, result);
}

typedef struct CodePatch {
    struct CodePatch* next;
    UInt32 index;
    CIRStreamReference reference;
} CodePatch;

static UInt16 assembly_short(const UInt8* bytes) {
    return bytes[0] | ((UInt16)bytes[1] << 8);
}

static const CIRStreamAssemblyPatch* assembly_reference(
    const CIRStreamAssembly* assembly, UInt32 offset, CIRStreamAssemblyPatchKind kind
) {
    const CIRStreamAssemblyPatch* patch;
    for (patch = assembly->patches; patch; patch = patch->next) {
        if (patch->offset == offset && patch->kind == kind) {
            return patch;
        }
    }
    return NULL;
}

static Boolean translate_assembly_operand(
    const CIRStreamAssembly* assembly, UInt32 offset, IAOperand* out
) {
    const UInt8* bytes = assembly->instruction.bytes + offset;
    const CIRStreamAssemblyPatch* first;
    const CIRStreamAssemblyPatch* second;

    out->type = bytes[0];
    out->negated = bytes[1];
    switch (bytes[0]) {
        case IAOpnd_Placeholder:
            return TRUE;
        case IAOpnd_Imm:
            out->u.imm.value = CIRStream_ReadWord(bytes + 6);
            return TRUE;
        case IAOpnd_Reg:
            first = assembly_reference(assembly, offset + 6, CIRStreamAssemblyObject);
            out->u.reg.num = CIRStream_ReadWord(bytes + 2);
            out->u.reg.object = first ? first->value.object : NULL;
            out->u.reg.effect = (SInt16)assembly_short(bytes + 10);
            out->u.reg.rclass = (SInt16)assembly_short(bytes + 12);
            return TRUE;
        case IAOpnd_GlobalObj:
        case IAOpnd_LocalObj:
            first = assembly_reference(assembly, offset + 6, CIRStreamAssemblyObject);
            if (!first || !first->value.object) {
                return FALSE;
            }
            out->u.obj.offset = CIRStream_ReadWord(bytes + 2);
            out->u.obj.obj = first->value.object;
            out->u.obj.reftype = (SInt16)assembly_short(bytes + 10);
            return TRUE;
        case IAOpnd_Lab:
            first = assembly_reference(assembly, offset + 2, CIRStreamAssemblyLabel);
            if (!first || !first->value.label) {
                return FALSE;
            }
            out->u.lab.label = first->value.label;
            return TRUE;
        case WireAssemblyLabelDifference:
            first = assembly_reference(assembly, offset + 2, CIRStreamAssemblyLabel);
            second = assembly_reference(assembly, offset + 6, CIRStreamAssemblyLabel);
            if (!first || !second || !first->value.label || !second->value.label) {
                return FALSE;
            }
            out->type = IAOpnd_LabDiff;
            out->u.labdiff.label1 = first->value.label;
            out->u.labdiff.label2 = second->value.label;
            out->u.labdiff.offset = CIRStream_ReadWord(bytes + 10);
            return TRUE;
        default:
            return FALSE;
    }
}

Boolean CIRStream_TranslateAssembly(CIRStreamFunctionCode* code, Boolean functionAssembly) {
    const CIRStreamAssembly* assembly;

    for (assembly = code->assembly; assembly; assembly = assembly->next) {
        const UInt8* bytes = assembly->instruction.bytes;
        Statement* statement = (Statement*)assembly->statement;
        InlineAsm* out;
        UInt32 count;
        UInt32 opcode;
        Opcode speOpcode;
        UInt32 index;

        if (!assembly->instruction.size && !assembly->patches) {
            statement->type = ST_NOP;
            continue;
        }
        if (assembly->instruction.size < WireAssemblyHeaderSize) {
            return FALSE;
        }
        count = assembly_short(bytes + 6);
        if (count > 0x7fff ||
            assembly->instruction.size !=
                WireAssemblyHeaderSize + count * WireAssemblyOperandSize) {
            return FALSE;
        }
        opcode = CIRStream_ReadWord(bytes);
        if (bytes[4] & IAFlag_Directive) {
            if (!functionAssembly) {
                return FALSE;
            }
            switch (opcode) {
                case IADirective_Entry: {
                    IAOperand operand;
                    IAEntryPoint* entry;
                    if (count != 1 ||
                        bytes[WireAssemblyHeaderSize] != IAOpnd_GlobalObj ||
                        !translate_assembly_operand(assembly, WireAssemblyHeaderSize, &operand)) {
                        return FALSE;
                    }
                    entry = galloc(sizeof(*entry));
                    memclrw(entry, sizeof(*entry));
                    entry->opcode = IADirective_Entry;
                    entry->flags = IAFlag_Directive;
                    entry->object = operand.u.obj.obj;
                    statement->expr = (ENode*)entry;
                    continue;
                }
                case IADirective_NoFrAlloc:
                case IADirective_FrFree:
                    if (count) {
                        return FALSE;
                    }
                    break;
                case IADirective_FrAlloc:
                    if (count && (count != 1 || bytes[WireAssemblyHeaderSize] != IAOpnd_Imm)) {
                        return FALSE;
                    }
                    break;
                default:
                    return FALSE;
            }
        } else if ((speOpcode = find_spe_opcode(opcode)) != PC_INVALID) {
            opcode = speOpcode;
        } else if (opcode >= PC_ICCCI + 1 && opcode <= PC_DCBA + 1) {
            --opcode;
        } else if (opcode >= PC_DSS - 1 && opcode <= PC_PS_MADDS1 - 1) {
            ++opcode;
        } else if (opcode > PC_ICBT) {
            return FALSE;
        }
        out = galloc(sizeof(*out) + count * sizeof(IAOperand));
        out->opcode = (Opcode)opcode;
        out->flags = bytes[4];
        out->flags2 = bytes[5];
        out->argcount = (SInt16)count;
        for (index = 0; index < count; ++index) {
            if (!translate_assembly_operand(
                    assembly,
                    WireAssemblyHeaderSize + index * WireAssemblyOperandSize,
                    &out->args[index]
                )) {
                return FALSE;
            }
        }
        statement->expr = (ENode*)out;
    }
    return TRUE;
}

typedef struct CodeWriter {
    CIRStreamWriter stream;
    const CIRStreamImportedGraph* graph;
    const CIRStreamFunctionCode* code;
    CIRStreamTypeWriter types;
    CodePatch* patches;
    UInt32 patchCount;
    UInt32 localCount;
    UInt32 labelCount;
    const CLabel** labels;
} CodeWriter;

static Boolean same_patch(const CIRStreamReference* left, const CIRStreamReference* right) {
    if (left->header.kind != right->header.kind) {
        return FALSE;
    }
    switch (left->header.kind) {
        case CIRStreamRefType:
            return left->value.type == right->value.type;
        case CIRStreamRefObject:
            return left->value.objectID == right->value.objectID;
        case CIRStreamRefFile:
            return left->value.fileID == right->value.fileID;
        case CIRStreamRefName:
            return left->value.name == right->value.name;
        case CIRStreamRefSourceType:
            return TRUE;
    }
    return FALSE;
}

static CodePatch* pack_patch(CodeWriter* writer, const CIRStreamReference* reference) {
    CodePatch* patch;

    for (patch = writer->patches; patch; patch = patch->next) {
        if (same_patch(&patch->reference, reference)) {
            return patch;
        }
    }
    patch = lalloc(sizeof(CodePatch));
    patch->next = writer->patches;
    patch->index = writer->patchCount++;
    patch->reference = *reference;
    writer->patches = patch;
    return patch;
}

static Boolean pack_type_patch(void* context, Type* type, UInt32* index) {
    CodeWriter* writer = context;
    CIRStreamReference ref = {0};

    ref.header.kind = CIRStreamRefType;
    ref.value.type = type;
    *index = pack_patch(writer, &ref)->index;
    return TRUE;
}

static Boolean pack_file_id(CodeWriter* writer, UInt32 fileID) {
    CodePatch* patch;

    if (!fileID) {
        return CIRStream_WriteUInt32(&writer->stream, 0xffffffffUL);
    }
    for (patch = writer->patches; patch; patch = patch->next) {
        if (patch->reference.header.kind == CIRStreamRefFile &&
            patch->reference.value.fileID == fileID) {
            return CIRStream_WriteUInt32(&writer->stream, patch->index);
        }
    }
    return FALSE;
}

static UInt32 file_id_for_name(CodeWriter* writer, const HashNameNode* path) {
    UInt32 index;

    if (path) {
        for (index = 1; index <= writer->graph->fileCount; ++index) {
            if (writer->graph->fileNames[index] == path) {
                return index;
            }
        }
    }
    return 0;
}

static Boolean pack_words(CodeWriter* writer, UInt32 first, UInt32 second) {
    UInt8 bytes[8];

    CIRStream_WriteWord(bytes, first);
    CIRStream_WriteWord(bytes + 4, second);
    return CIRStream_PackData(&writer->stream, bytes, sizeof(bytes));
}

static Boolean pack_object(CodeWriter* writer, const Object* object, Boolean directCall) {
    CIRStreamReference ref = {0};
    CodePatch* patch;
    UInt32 index;
    const ObjectList* list;

    while (object && object->datatype == DALIAS && !object->u.alias.offset) {
        object = object->u.alias.object;
    }
    if (!object) {
        return CIRStream_PackByte(&writer->stream, 0);
    }
    if (object->datatype == DLOCAL) {
        index = 0;
        for (list = writer->code->arguments; list; list = list->next, ++index) {
            if (list->object == object) {
                break;
            }
        }
        if (!list) {
            for (list = writer->code->locals; list; list = list->next, ++index) {
                if (list->object == object) {
                    break;
                }
            }
        }
        return list &&
            CIRStream_PackByte(&writer->stream, 2) &&
            CIRStream_WriteUInt32(&writer->stream, index);
    }
    for (index = 0; index < writer->graph->header.nodeCount; ++index) {
        if (writer->graph->nodes[index].object == object) {
            break;
        }
    }
    if (index == writer->graph->header.nodeCount) {
        return FALSE;
    }
    ref.header.kind = CIRStreamRefObject;
    ref.value.objectID = index;
    patch = pack_patch(writer, &ref);
    if (directCall && (object->datatype == DFUNC || object->datatype == DVFUNC)) {
        if (patch->reference.header.callCount != 0xffffU) {
            ++patch->reference.header.callCount;
        }
    } else {
        patch->reference.header.flags |= CIRStream_AddressTaken;
    }
    return CIRStream_PackByte(&writer->stream, 1) &&
        CIRStream_WriteUInt32(&writer->stream, patch->index);
}

static Boolean pack_label(CodeWriter* writer, const CLabel* label) {
    UInt32 index;

    for (index = 0; index < writer->labelCount; ++index) {
        if (writer->labels[index] == label) {
            return CIRStream_WriteUInt32(&writer->stream, index);
        }
    }
    return FALSE;
}

static Boolean pack_locals(CodeWriter* writer, const ObjectList* list) {
    const ObjectList* entry;
    UInt32 count = 0;

    for (entry = list; entry; entry = entry->next) {
        ++count;
    }
    if (count > 0x7fffffffUL || !CIRStream_WriteInt32(&writer->stream, (SInt32)count)) {
        return FALSE;
    }
    writer->localCount += count;
    for (entry = list; entry; entry = entry->next) {
        const Object* object = entry->object;
        const TStreamElement* token;
        const CIRStreamLocalInfo* info;
        CIRStreamReference ref = {0};
        UInt32 nameID = 0xffffffffUL;
        UInt32 fileID = 0;
        UInt8 storage;

        if (!object || object->datatype != DLOCAL || !object->type) {
            return FALSE;
        }
        switch (object->sclass) {
            case 0:
                storage = 0;
                break;
            case TK_REGISTER:
                storage = 1;
                break;
            case TK_AUTO:
                storage = 2;
                break;
            default:
                return FALSE;
        }
        if (object->flags & OBJECT_REFERENCED) {
            storage |= 0x80;
        }
        if (object->name) {
            ref.header.kind = CIRStreamRefName;
            ref.value.name = object->name;
            nameID = pack_patch(writer, &ref)->index;
        }
        for (info = writer->code->localInfo; info && info->object != object; info = info->next) {}
        token = object->u.var.info ? &object->u.var.info->deftoken : NULL;
        if (info) {
            fileID = info->fileID;
            storage |= info->extraStorageFlags;
        }
        if (!CIRStream_WriteUInt32(&writer->stream, nameID) ||
            !CIRStream_PackType(&writer->stream, &writer->types, object->type) ||
            !CIRStream_WriteUInt32(&writer->stream, object->qual) ||
            !CIRStream_PackByte(&writer->stream, storage) ||
            !CIRStream_WriteInt32(&writer->stream, object->u.var.uid) ||
            !pack_file_id(writer, fileID) ||
            !pack_words(
                writer, token ? (UInt32)token->tokenoffset : 0, token ? (UInt32)token->tokenline : 0
            ) ||
            !CIRStream_PackByte(&writer->stream, info && info->hasSourceType)) {
            return FALSE;
        }
        if (info && info->hasSourceType) {
            ref.header.kind = CIRStreamRefSourceType;
            if (!CIRStream_WriteUInt32(&writer->stream, pack_patch(writer, &ref)->index)) {
                return FALSE;
            }
        }
    }
    return TRUE;
}

static Boolean pack_expression(CodeWriter*, Type*, const ENode*, UInt32, Boolean);

static Boolean pack_expression_header(
    CodeWriter* writer, UInt8 tag, UInt8 cost, UInt32 flags, Type* type, Type* expected
) {
    if (!type) {
        return FALSE;
    }
    return CIRStream_PackByte(&writer->stream, tag | (type == expected ? WireInheritedType : 0)) &&
        CIRStream_PackByte(&writer->stream, cost) &&
        CIRStream_WriteUInt32(&writer->stream, flags) &&
        (type == expected || CIRStream_PackType(&writer->stream, &writer->types, type));
}

static Boolean pack_call(CodeWriter* writer, const ENode* expr, UInt32 depth) {
    const ENodeList* arg;

    if (!expr->data.funccall.funcref ||
        !expr->data.funccall.functype ||
        !pack_expression(writer, expr->rtype, expr->data.funccall.funcref, depth, TRUE)) {
        return FALSE;
    }
    for (arg = expr->data.funccall.args; arg; arg = arg->next) {
        if (!arg->node || !pack_expression(writer, TYPE(&stsignedint), arg->node, depth, FALSE)) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(&writer->stream, WireNullExpression) &&
        CIRStream_PackType(&writer->stream, &writer->types, TYPE(expr->data.funccall.functype));
}

static Boolean pack_expression(
    CodeWriter* writer, Type* expected, const ENode* expr, UInt32 depth, Boolean directCall
) {
    UInt8 tag;
    ENodeType type;
    UInt32 flags;
    const CIRStreamExpressionInfo* info;

    if (depth == MaxExpressionDepth) {
        return FALSE;
    }
    if (!expr) {
        return CIRStream_PackByte(&writer->stream, WireNullExpression);
    }
    for (tag = 0; tag <= 87; ++tag) {
        if (CIRStream_DecodeExpressionKind(tag, &type) && type == expr->type) {
            break;
        }
    }
    flags = expr->flags;
    for (info = writer->code->expressionInfo; info && info->expression != expr; info = info->next) {
    }
    if (info) {
        flags |= info->extraFlags;
    }
    if (tag > 87 ||
        !pack_expression_header(writer, tag, expr->cost, flags, expr->rtype, expected)) {
        return FALSE;
    }
    ++depth;
    switch (expr->type) {
ENODE_CASE_MONADIC:
        return expr->data.monadic &&
            pack_expression(writer, expr->rtype, expr->data.monadic, depth, FALSE);
ENODE_CASE_DIADIC_ALL:
        return expr->data.diadic.left &&
            expr->data.diadic.right &&
            pack_expression(writer, expr->rtype, expr->data.diadic.left, depth, FALSE) &&
            pack_expression(writer, expr->rtype, expr->data.diadic.right, depth, FALSE);
        case EINTCONST:
            return pack_words(writer, (UInt32)expr->data.intval.hi, expr->data.intval.lo);
        case EFLOATCONST:
            return pack_words(
                writer,
                (UInt32)expr->data.floatval.data.words[0],
                (UInt32)expr->data.floatval.data.words[1]
            );
        case EVECTOR128CONST:
            return CIRStream_PackData(
                &writer->stream, expr->data.vector128val.uc, sizeof(expr->data.vector128val.uc)
            );
        case ESTRINGCONST:
            return expr->data.string.size >= 0 &&
                CIRStream_WriteInt32(&writer->stream, expr->data.string.size) &&
                CIRStream_PackData(
                       &writer->stream, (const UInt8*)expr->data.string.data, expr->data.string.size
                ) &&
                CIRStream_WriteInt32(&writer->stream, expr->data.string.sequence) &&
                CIRStream_PackByte(&writer->stream, expr->data.string.ispascal) &&
                CIRStream_PackByte(&writer->stream, info ? info->stringKind : 0);
        case ECOND:
            return pack_expression(writer, expr->rtype, expr->data.cond.cond, depth, FALSE) &&
                pack_expression(writer, expr->rtype, expr->data.cond.expr1, depth, FALSE) &&
                pack_expression(writer, expr->rtype, expr->data.cond.expr2, depth, FALSE);
        case EFUNCCALL:
        case EFUNCCALLP:
            return pack_call(writer, expr, depth);
        case EOBJREF:
            return expr->data.objref && pack_object(writer, expr->data.objref, directCall);
        case EMEMBERFUNCLITERAL:
            return expr->data.objref && pack_object(writer, expr->data.objref, FALSE);
        case ESCOPE:
            return pack_expression(writer, expr->rtype, expr->data.scope.object, depth, FALSE) &&
                CIRStream_PackByte(&writer->stream, expr->data.scope.kind);
        case EMFPOINTER:
            return pack_expression(
                       writer, expr->rtype, expr->data.mfpointer.accessnode, depth, FALSE
                   ) &&
                pack_expression(writer, expr->rtype, expr->data.mfpointer.mfpointer, depth, FALSE);
        case ENULLCHECK:
            return pack_expression(
                       writer, expr->rtype, expr->data.nullcheck.nullcheckexpr, depth, FALSE
                   ) &&
                pack_expression(writer, expr->rtype, expr->data.nullcheck.condexpr, depth, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, expr->data.nullcheck.precompid);
        case EPRECOMP:
            return CIRStream_WriteInt32(&writer->stream, expr->data.precompid);
        case ELABEL:
            return pack_label(writer, expr->data.label);
        case ETEMP:
            return CIRStream_PackType(&writer->stream, &writer->types, expr->data.temp.type) &&
                CIRStream_WriteInt32(&writer->stream, expr->data.temp.uniqueid) &&
                CIRStream_PackByte(&writer->stream, expr->data.temp.needs_dtor);
        case EINITTRYCATCH:
            return pack_expression(writer, expr->rtype, expr->data.itc.initexpr, depth, FALSE) &&
                pack_expression(writer, expr->rtype, expr->data.itc.tryexpr, depth, FALSE) &&
                pack_expression(writer, expr->rtype, expr->data.itc.catchexpr, depth, FALSE) &&
                pack_expression(writer, expr->rtype, expr->data.itc.result, depth, FALSE);
        default:
            return FALSE;
    }
}

static Boolean pack_switch(CodeWriter* writer, const SwitchInfo* info) {
    const SwitchCase* entry;
    UInt32 count = 0;

    if (!info || !info->exprtype) {
        return FALSE;
    }
    for (entry = info->cases; entry; entry = entry->next) {
        ++count;
    }
    if (!CIRStream_WriteUInt32(&writer->stream, count)) {
        return FALSE;
    }
    for (entry = info->cases; entry; entry = entry->next) {
        if (!pack_label(writer, entry->label) ||
            !pack_words(writer, (UInt32)entry->min.hi, entry->min.lo) ||
            !pack_words(writer, (UInt32)entry->min.hi, entry->min.lo)) {
            return FALSE;
        }
    }
    return CIRStream_WriteUInt32(&writer->stream, 0) &&
        pack_label(writer, info->defaultlabel) &&
        CIRStream_PackType(&writer->stream, &writer->types, info->exprtype);
}

static Boolean pack_exception_action(CodeWriter* writer, const ExceptionAction* action) {
    if (action->type <= EAT_NOP ||
        action->type >= EAT_NACTIONS ||
        !CIRStream_PackByte(&writer->stream, action->type)) {
        return FALSE;
    }
    switch (action->type) {
        case EAT_DESTROYLOCAL:
            return pack_object(writer, action->data.destroy_local.local, FALSE) &&
                pack_object(writer, action->data.destroy_local.dtor, FALSE);
        case EAT_DESTROYLOCALCOND:
            return pack_object(writer, action->data.destroy_local_cond.local, FALSE) &&
                pack_object(writer, action->data.destroy_local_cond.dtor, FALSE) &&
                pack_object(writer, action->data.destroy_local_cond.cond, FALSE);
        case EAT_DESTROYLOCALOFFSET:
            return pack_object(writer, action->data.destroy_local_offset.local, FALSE) &&
                pack_object(writer, action->data.destroy_local_offset.dtor, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_local_offset.offset);
        case EAT_DESTROYLOCALPOINTER:
            return pack_object(writer, action->data.destroy_local_pointer.pointer, FALSE) &&
                pack_object(writer, action->data.destroy_local_pointer.dtor, FALSE);
        case EAT_DESTROYLOCALARRAY:
            return pack_object(writer, action->data.destroy_local_array.localarray, FALSE) &&
                pack_object(writer, action->data.destroy_local_array.dtor, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_local_array.elements) &&
                CIRStream_WriteInt32(
                       &writer->stream, action->data.destroy_local_array.element_size
                );
        case EAT_DESTROYPARTIALARRAY:
            return pack_object(writer, action->data.destroy_partial_array.arraypointer, FALSE) &&
                pack_object(writer, action->data.destroy_partial_array.arraycounter, FALSE) &&
                pack_object(writer, action->data.destroy_partial_array.dtor, FALSE) &&
                pack_object(writer, action->data.destroy_partial_array.element_size, FALSE);
        case EAT_DESTROYMEMBER:
        case EAT_DESTROYBASE:
            return pack_object(writer, action->data.destroy_member.objectptr, FALSE) &&
                pack_object(writer, action->data.destroy_member.dtor, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_member.offset);
        case EAT_DESTROYMEMBERCOND:
            return pack_object(writer, action->data.destroy_member_cond.objectptr, FALSE) &&
                pack_object(writer, action->data.destroy_member_cond.dtor, FALSE) &&
                pack_object(writer, action->data.destroy_member_cond.cond, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_member_cond.offset);
        case EAT_DESTROYMEMBERARRAY:
            return pack_object(writer, action->data.destroy_member_array.objectptr, FALSE) &&
                pack_object(writer, action->data.destroy_member_array.dtor, FALSE) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_member_array.offset) &&
                CIRStream_WriteInt32(&writer->stream, action->data.destroy_member_array.elements) &&
                CIRStream_WriteInt32(
                       &writer->stream, action->data.destroy_member_array.element_size
                );
        case EAT_DELETEPOINTER:
        case EAT_DELETELOCALPOINTER:
            return pack_object(writer, action->data.delete_pointer.pointerobject, FALSE) &&
                pack_object(writer, action->data.delete_pointer.deletefunc, FALSE);
        case EAT_DELETEPOINTERCOND:
            return pack_object(writer, action->data.delete_pointer_cond.pointerobject, FALSE) &&
                pack_object(writer, action->data.delete_pointer_cond.deletefunc, FALSE) &&
                pack_object(writer, action->data.delete_pointer_cond.cond, FALSE);
        case EAT_CATCHBLOCK:
            return pack_object(writer, action->data.catch_block.catch_object, FALSE) &&
                pack_object(writer, action->data.catch_block.catch_info_object, FALSE) &&
                pack_label(writer, action->data.catch_block.catch_label) &&
                pack_object(writer, action->data.catch_block.catch_typeid, FALSE) &&
                CIRStream_PackType(
                       &writer->stream, &writer->types, action->data.catch_block.catch_type
                ) &&
                CIRStream_WriteUInt32(&writer->stream, action->data.catch_block.catch_qual);
        case EAT_ACTIVECATCHBLOCK:
            return pack_object(writer, action->data.active_catch_block.catch_info_object, FALSE) &&
                CIRStream_PackByte(&writer->stream, action->data.active_catch_block.call_dtor);
        case EAT_SPECIFICATION: {
            SInt32 count = action->data.specification.unexp_ids;
            SInt32 index;

            if (count < 0 ||
                (count && !action->data.specification.unexp_id) ||
                !CIRStream_WriteInt32(&writer->stream, count)) {
                return FALSE;
            }
            for (index = 0; index < count; ++index) {
                if (!pack_object(writer, action->data.specification.unexp_id[index], FALSE)) {
                    return FALSE;
                }
            }
            return pack_label(writer, action->data.specification.unexp_label) &&
                pack_object(writer, action->data.specification.unexp_info_object, FALSE);
        }
        case EAT_TERMINATE:
            return TRUE;
        default:
            return FALSE;
    }
}

static Boolean pack_exception_actions(CodeWriter* writer, const ExceptionAction* chain) {
    if (!CIRStream_PackByte(&writer->stream, WireSetExceptionActions)) {
        return FALSE;
    }
    for (; chain; chain = chain->prev) {
        if (!pack_exception_action(writer, chain)) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(&writer->stream, WireEndExceptionActions);
}

static Boolean pack_location_change(
    CodeWriter* writer, const CIRStreamSourceLocation* current, const CIRStreamSourceLocation* next
) {
    Boolean differentFile = next->fileID != current->fileID;
    Boolean differentPosition = next->offset != current->offset || next->line != current->line;

    if (!differentFile && !differentPosition) {
        return TRUE;
    }
    if (!next->fileID && !next->offset && !next->line) {
        return CIRStream_PackByte(&writer->stream, WireClearSource);
    }
    if (differentFile &&
        (!CIRStream_PackByte(&writer->stream, WireSetSourceFile) ||
         !pack_file_id(writer, next->fileID))) {
        return FALSE;
    }
    return !differentPosition ||
        (CIRStream_PackByte(&writer->stream, WireSetSourcePosition) &&
         pack_words(writer, next->offset, next->line));
}

static Boolean pack_assembly(CodeWriter* writer, const CIRStreamAssembly* assembly) {
    const CIRStreamAssemblyPatch* patch;
    UInt8* instruction;
    UInt32 size = assembly->instruction.size;

    if (!CIRStream_WriteUInt32(&writer->stream, size)) {
        return FALSE;
    }
    instruction = writer->stream.bytes;
    if (!CIRStream_PackData(&writer->stream, assembly->instruction.bytes, size)) {
        return FALSE;
    }
    for (patch = assembly->patches; patch; patch = patch->next) {
        if (!assembly_patch_fits(size, patch->offset) ||
            !CIRStream_PackByte(&writer->stream, patch->kind) ||
            !CIRStream_WriteUInt32(&writer->stream, patch->offset)) {
            return FALSE;
        }
        if (instruction) {
            CIRStream_WriteWord(instruction + patch->offset, 0);
        }
        switch (patch->kind) {
            case CIRStreamAssemblyObject:
                if (!pack_object(writer, patch->value.object, FALSE)) {
                    return FALSE;
                }
                break;
            case CIRStreamAssemblyLabel:
                if (!pack_label(writer, patch->value.label)) {
                    return FALSE;
                }
                break;
            case CIRStreamAssemblyType:
                if (!CIRStream_PackType(&writer->stream, &writer->types, patch->value.type)) {
                    return FALSE;
                }
                break;
            default:
                return FALSE;
        }
    }
    return CIRStream_PackByte(&writer->stream, WireEndAssemblyPatches);
}

static Boolean pack_assembly_operand(CIRStreamWriter* stream, const IAOperand* operand) {
    UInt8 bytes[WireAssemblyOperandSize] = {0};
    CIRStreamWriter tail = {bytes + 10, 4};

    bytes[0] = operand->type;
    bytes[1] = operand->negated;
    switch (operand->type) {
        case IAOpnd_Placeholder:
        case IAOpnd_Lab:
            break;
        case IAOpnd_Imm:
            CIRStream_WriteWord(bytes + 6, operand->u.imm.value);
            break;
        case IAOpnd_Reg:
            CIRStream_WriteWord(bytes + 2, operand->u.reg.num);
            CIRStream_PackUInt16(&tail, operand->u.reg.effect);
            CIRStream_PackUInt16(&tail, operand->u.reg.rclass);
            break;
        case IAOpnd_GlobalObj:
        case IAOpnd_LocalObj:
            CIRStream_WriteWord(bytes + 2, operand->u.obj.offset);
            CIRStream_PackUInt16(&tail, operand->u.obj.reftype);
            break;
        case IAOpnd_LabDiff:
            bytes[0] = WireAssemblyLabelDifference;
            CIRStream_WriteWord(bytes + 10, operand->u.labdiff.offset);
            break;
        default:
            return FALSE;
    }
    return CIRStream_PackData(stream, bytes, sizeof(bytes));
}

static Boolean pack_assembly_label(CodeWriter* writer, UInt32 offset, const CLabel* label) {
    return CIRStream_PackByte(&writer->stream, CIRStreamAssemblyLabel) &&
        CIRStream_WriteUInt32(&writer->stream, offset) &&
        pack_label(writer, label);
}

static Boolean pack_assembly_operand_references(
    CodeWriter* writer, UInt32 offset, const IAOperand* operand
) {
    const Object* object;

    switch (operand->type) {
        case IAOpnd_Reg:
            if (!operand->u.reg.object) {
                return TRUE;
            }
            object = operand->u.reg.object;
            break;
        case IAOpnd_GlobalObj:
        case IAOpnd_LocalObj:
            object = operand->u.obj.obj;
            break;
        case IAOpnd_Lab:
            return pack_assembly_label(writer, offset + 2, operand->u.lab.label);
        case IAOpnd_LabDiff:
            return pack_assembly_label(writer, offset + 6, operand->u.labdiff.label2) &&
                pack_assembly_label(writer, offset + 2, operand->u.labdiff.label1);
        default:
            return TRUE;
    }
    return object &&
        CIRStream_PackByte(&writer->stream, CIRStreamAssemblyObject) &&
        CIRStream_WriteUInt32(&writer->stream, offset + 6) &&
        pack_object(writer, object, FALSE);
}

static Boolean pack_live_assembly(CodeWriter* writer, const InlineAsm* instruction) {
    UInt8 header[WireAssemblyHeaderSize];
    CIRStreamWriter fields = {header + 4, 4};
    SInt32 opcode;
    SInt32 index;
    UInt32 size;

    if (!instruction || instruction->argcount < 0 || (instruction->flags & IAFlag_Directive)) {
        return FALSE;
    }
    opcode = instruction->opcode;
    // The opcodeinfo tables agree through ICBT. RC1 adds ICBT_E and replaces
    // the three ISEL variants with one opcode before the AltiVec range.
    if (IS_SPE_OPCODE(opcode)) {
        opcode = spe_opcodeinfo[opcode - PC_EVOR].nativeOpcode;
    } else if (opcode >= PC_ICCCI && opcode <= PC_DCBA) {
        ++opcode;
    } else if (opcode >= PC_DSS && opcode <= PC_PS_MADDS1) {
        --opcode;
    } else if (opcode < PC_B || opcode > PC_ICBT) {
        return FALSE;
    }
    size = sizeof(header) + WireAssemblyOperandSize * instruction->argcount;
    CIRStream_WriteWord(header, opcode);
    CIRStream_PackByte(&fields, instruction->flags);
    CIRStream_PackByte(&fields, instruction->flags2);
    CIRStream_PackUInt16(&fields, instruction->argcount);
    if (!CIRStream_WriteUInt32(&writer->stream, size) ||
        !CIRStream_PackData(&writer->stream, header, sizeof(header))) {
        return FALSE;
    }
    for (index = 0; index < instruction->argcount; ++index) {
        if (!pack_assembly_operand(&writer->stream, &instruction->args[index])) {
            return FALSE;
        }
    }
    for (index = instruction->argcount; index-- > 0;) {
        if (!pack_assembly_operand_references(
                writer, sizeof(header) + WireAssemblyOperandSize * index, &instruction->args[index]
            )) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(&writer->stream, WireEndAssemblyPatches);
}

static Boolean pack_statement(
    CodeWriter* writer,
    const Statement* stmt,
    const CIRStreamCodeInfo* info,
    const CIRStreamAssembly* assembly
) {
    Type* expected = TYPE(&stsignedint);
    StatementType kind = info ? ST_EXPRESSION : stmt->type;

    if (!CIRStream_PackByte(&writer->stream, kind) ||
        !CIRStream_PackByte(&writer->stream, stmt->flags) ||
        !CIRStream_PackUInt16(&writer->stream, stmt->value)) {
        return FALSE;
    }
    if (info) {
        return pack_expression_header(
                   writer, WireInfoExpression, info->cost, info->flags, info->type, expected
               ) &&
            pack_expression(writer, info->type, info->operand, 0, FALSE) &&
            CIRStream_PackByte(&writer->stream, info->kind);
    }
    switch (kind) {
        case ST_NOP:
            return TRUE;
        case ST_LABEL:
        case ST_GOTO:
            return pack_label(writer, stmt->label);
        case ST_ASM:
            return assembly ? pack_assembly(writer, assembly)
                            : pack_live_assembly(writer, (const InlineAsm*)stmt->expr);
        case ST_EXPRESSION:
        case ST_RETURN:
        case ST_BEGINCATCH:
        case ST_ENDCATCH:
        case ST_ENDCATCHDTOR:
        case ST_GOTOEXPR:
            return pack_expression(writer, expected, stmt->expr, 0, FALSE);
        case ST_IFGOTO:
        case ST_IFNGOTO:
            return stmt->expr &&
                pack_expression(writer, expected, stmt->expr, 0, FALSE) &&
                pack_label(writer, stmt->label);
        case ST_SWITCH:
            return stmt->expr &&
                pack_expression(writer, expected, stmt->expr, 0, FALSE) &&
                pack_switch(writer, (const SwitchInfo*)stmt->label);
        default:
            return FALSE;
    }
}

static Boolean pack_statements(CodeWriter* writer) {
    const Statement* stmt;
    const CIRStreamStatementLocation* source = writer->code->sourceLocations;
    const CIRStreamCodeInfo* info = writer->code->info;
    const CIRStreamAssembly* assembly = writer->code->assembly;
    const ExceptionAction* actions = NULL;
    CIRStreamSourceLocation location = {0};

    for (stmt = writer->code->statements; stmt; stmt = stmt->next) {
        CIRStreamSourceLocation next = location;
        const CIRStreamCodeInfo* stmtInfo = NULL;
        const CIRStreamAssembly* stmtAssembly = NULL;

        if (source && source->statement == stmt) {
            next = source->location;
            source = source->next;
        } else if (!writer->code->sourceLocations) {
            next.fileID = file_id_for_name(writer, stmt->sourcefilepath);
            next.offset = (UInt32)stmt->sourceoffset;
        }
        if (!pack_location_change(writer, &location, &next)) {
            return FALSE;
        }
        location = next;
        if (actions != stmt->dobjstack) {
            actions = stmt->dobjstack;
            if (!pack_exception_actions(writer, actions)) {
                return FALSE;
            }
        }
        if (info && info->statement == stmt) {
            stmtInfo = info;
            info = info->next;
        }
        if (assembly && assembly->statement == stmt && stmt->type == ST_ASM) {
            stmtAssembly = assembly;
            assembly = assembly->next;
        }
        if (!pack_statement(writer, stmt, stmtInfo, stmtAssembly)) {
            return FALSE;
        }
    }
    return !source && !info && !assembly && CIRStream_PackByte(&writer->stream, WireEndStatements);
}

Boolean CIRStream_PackCode(
    CIRStreamWriter* stream,
    const CIRStreamImportedGraph* graph,
    const CIRStreamFunctionCode* code,
    CIRStreamFunctionDefinition* definition
) {
    CodeWriter writer = {0};
    const Statement* stmt;
    CodePatch* patch;
    UInt32 index;

    writer.stream = *stream;
    writer.graph = graph;
    writer.code = code;
    writer.types.root = graph->root;
    writer.types.pairedSingles = graph->pairedSingles;
    writer.types.patchType = pack_type_patch;
    writer.types.patchContext = &writer;

    // Keep file descriptors in the caller's order. Their records may introduce
    // IDs used by subsequent nodes. Rebuild the other patches from the body.
    for (index = 0; index < definition->header.referenceCount; ++index) {
        const CIRStreamReference* reference = &definition->references[index];
        if (reference->header.kind == CIRStreamRefFile) {
            if (!reference->value.fileID ||
                reference->value.fileID > graph->fileCount ||
                !reference->fileRecord.size) {
                return FALSE;
            }
            pack_patch(&writer, reference);
        }
    }
    for (stmt = code->statements; stmt; stmt = stmt->next) {
        writer.labelCount += stmt->type == ST_LABEL;
    }
    if (writer.labelCount) {
        if (writer.labelCount > 0x7fffffffUL / sizeof(*writer.labels)) {
            return FALSE;
        }
        writer.labels = lalloc(writer.labelCount * sizeof(*writer.labels));
        index = 0;
        for (stmt = code->statements; stmt; stmt = stmt->next) {
            if (stmt->type == ST_LABEL) {
                if (!stmt->label || stmt->label->stmt != stmt) {
                    return FALSE;
                }
                writer.labels[index++] = stmt->label;
            }
        }
    }
    for (index = 0; index < 3; ++index) {
        if (!pack_file_id(&writer, code->location[index].fileID) ||
            !pack_words(&writer, code->location[index].offset, code->location[index].line)) {
            return FALSE;
        }
    }
    if (!pack_locals(&writer, code->arguments) ||
        !pack_locals(&writer, code->locals) ||
        !CIRStream_PackByte(&writer.stream, code->oldStyleParameters) ||
        !CIRStream_PackByte(&writer.stream, code->callOnModuleBind) ||
        !pack_statements(&writer) ||
        writer.patchCount > 0x7fffffffUL / sizeof(CIRStreamReference)) {
        return FALSE;
    }
    if (!stream->bytes) {
        *stream = writer.stream;
        return TRUE;
    }
    definition->header.code.bytes = stream->bytes;
    definition->header.code.size = stream->size - writer.stream.size;
    definition->header.localCount = writer.localCount;
    definition->header.labelCount = writer.labelCount;
    definition->header.typeCount = writer.types.typeCount;
    definition->header.referenceCount = writer.patchCount;
    definition->references =
        writer.patchCount ? galloc(writer.patchCount * sizeof(CIRStreamReference)) : NULL;
    for (patch = writer.patches; patch; patch = patch->next) {
        definition->references[patch->index] = patch->reference;
    }
    *stream = writer.stream;
    return TRUE;
}
