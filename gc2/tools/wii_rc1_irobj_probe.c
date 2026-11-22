/* Minimal compiler heap and symbols for the standalone CIRStream smoke test. */
#include "compiler/CClass.h"
#include "compiler/CFunc.h"
#include "compiler/CInline.h"
#include "compiler/CIRStreamCode.h"
#include "compiler/CParser.h"
#include "compiler/CompilerTools.h"
#include "compiler/CScope.h"
#include "compiler/Exceptions.h"
#include "compiler/Switch.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"

#define SCALAR(name, type, size, kind) TypeIntegral name = {type, size, kind}
SCALAR(stbool, TYPEINT, 1, IT_BOOL);
SCALAR(stchar, TYPEINT, 1, IT_CHAR);
SCALAR(stsignedchar, TYPEINT, 1, IT_SCHAR);
SCALAR(stunsignedchar, TYPEINT, 1, IT_UCHAR);
SCALAR(stwchar, TYPEINT, 2, IT_WCHAR_T);
SCALAR(stsignedshort, TYPEINT, 2, IT_SHORT);
SCALAR(stunsignedshort, TYPEINT, 2, IT_USHORT);
SCALAR(stsignedint, TYPEINT, 4, IT_INT);
SCALAR(stunsignedint, TYPEINT, 4, IT_UINT);
SCALAR(stsignedlong, TYPEINT, 4, IT_LONG);
SCALAR(stunsignedlong, TYPEINT, 4, IT_ULONG);
SCALAR(stsignedlonglong, TYPEINT, 8, IT_LONGLONG);
SCALAR(stunsignedlonglong, TYPEINT, 8, IT_ULONGLONG);
SCALAR(stfloat, TYPEFLOAT, 4, IT_FLOAT);
SCALAR(stshortdouble, TYPEFLOAT, 8, IT_SHORTDOUBLE);
SCALAR(stdouble, TYPEFLOAT, 8, IT_DOUBLE);
SCALAR(stlongdouble, TYPEFLOAT, 8, IT_LONGDOUBLE);
SCALAR(stvec2x32float, TYPEFLOAT, 8, IT_VEC2X32FLOAT);

TypeStruct stvectorunsignedchar, stvectorsignedchar, stvectorboolchar;
TypeStruct stvectorunsignedshort, stvectorsignedshort, stvectorboolshort;
TypeStruct stvectorunsignedlong, stvectorsignedlong, stvectorboollong;
TypeStruct stvectorfloat, stvectorpixel;
TypeStruct stspevector[SPEBuiltinTypeCount];
CompilerLinkerOptions copts;
Type stvoid = {TYPEVOID, 0};
Type stillegal = {TYPEILLEGAL, 1};
FuncArg elipsis, oldstyle;

Boolean CClass_IsTrivialCopyClass(TypeClass* tclass) {
    return tclass->irCopyKind == ClassCopyTrivial;
}

static Object runtimeObjects[25];
Object* newh_func = &runtimeObjects[1];
Object* delh_func = &runtimeObjects[2];
Object* copy_func = &runtimeObjects[3];
Object* clear_func = &runtimeObjects[4];
Object* rt_ptmf_null = &runtimeObjects[5];
Object* rt_ptmf_cmpr = &runtimeObjects[6];
Object* rt_ptmf_cast = &runtimeObjects[7];
Object* rt_ptmf_test = &runtimeObjects[8];
Object* rt_ptmf_call = &runtimeObjects[9];
Object* rt_ptmf_scall = &runtimeObjects[10];
Object* rt_ptmf_call4 = &runtimeObjects[11];
Object* rt_ptmf_scall4 = &runtimeObjects[12];
Object* Rgtid_func = &runtimeObjects[13];
Object* Rdync_func = &runtimeObjects[14];
Object* carr_func = &runtimeObjects[15];
Object* darr_func = &runtimeObjects[16];
Object* cnar_func = &runtimeObjects[17];
Object* dnar_func = &runtimeObjects[18];
Object* dnar3_func = &runtimeObjects[19];
Object* Xgreg_func = &runtimeObjects[20];
Object* Xthrw_func = &runtimeObjects[21];
Object* Xicth_func = &runtimeObjects[22];
Object* Xecth_func = &runtimeObjects[23];
Object* Xunex_func = &runtimeObjects[24];

static union {
    UInt8 bytes[0x400000];
    double align;
} heap;
static UInt32 used;
static HashNameNode* names;

void* galloc(SInt32 size) {
    UInt8* result = heap.bytes + used;
    if (size < 0 || (UInt32)size > sizeof(heap.bytes) - used) {
        __builtin_trap();
    }
    used += ((UInt32)size + 7) & ~7UL;
    return result;
}

void* lalloc(SInt32 size) {
    return galloc(size);
}

void memclrw(void* address, SInt32 size) {
    UInt8* bytes = address;
    while (size--) {
        *bytes++ = 0;
    }
}

void* memcpy(void* destination, const void* source, __SIZE_TYPE__ size) {
    UInt8* out = destination;
    const UInt8* in = source;
    while (size--) {
        *out++ = *in++;
    }
    return destination;
}

static Boolean equal_string(const char* left, const char* right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

HashNameNode* GetHashNameNodeExport(const char* name) {
    HashNameNode* node;
    UInt32 length = 0;
    UInt32 index;

    for (node = names; node; node = node->next) {
        if (equal_string(node->name, name)) {
            return node;
        }
    }
    while (name[length]) {
        ++length;
    }
    node = galloc(__builtin_offsetof(HashNameNode, name) + length + 1);
    for (index = 0; index <= length; ++index) {
        node->name[index] = name[index];
    }
    node->next = names;
    names = node;
    return node;
}

NameSpaceObjectList* CScope_FindName(NameSpace* nspace, HashNameNode* name) {
    NameSpaceName* entry;
    for (entry = nspace->data.list; entry; entry = entry->next) {
        if (entry->name == name) {
            return &entry->first;
        }
    }
    return NULL;
}

NameSpace* CScope_NewHashNameSpace(HashNameNode* name) {
    NameSpace* nspace = galloc(sizeof(NameSpace));
    memclrw(nspace, sizeof(NameSpace));
    nspace->name = name;
    return nspace;
}

NameSpace* CScope_NewListNameSpace(HashNameNode* name, Boolean isGlobal) {
    NameSpace* nspace = CScope_NewHashNameSpace(name);
    nspace->is_global = isGlobal;
    return nspace;
}

void CScope_AddObject(NameSpace* nspace, HashNameNode* name, ObjBase* object) {
    NameSpaceName* entry = galloc(sizeof(NameSpaceName));
    entry->next = nspace->data.list;
    entry->name = name;
    entry->first.next = NULL;
    entry->first.object = object;
    nspace->data.list = entry;
}

static NameSpace* new_root(void) {
    UInt32 index;

    used = 0;
    names = NULL;
    for (index = 1; index < sizeof(runtimeObjects) / sizeof(runtimeObjects[0]); ++index) {
        runtimeObjects[index].otype = OT_OBJECT;
        runtimeObjects[index].datatype = index == 5 ? DDATA : DFUNC;
    }
    return CScope_NewHashNameSpace(NULL);
}

UInt32 CIRTest_RepackGraph(
    const UInt8* bytes, UInt32 size, UInt8* output, UInt32 capacity, Boolean editData
) {
    CIRStreamImportedGraph graph;
    CIRStreamWriter stream = {output, capacity};
    UInt32 index;
    UInt32 edits = 0;
    const UInt8 initializer[] = {0, 0, 0, 27, 0, 0, 0, 4};

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return 0;
    }
    if (editData) {
        for (index = 0; index < graph.header.nodeCount; ++index) {
            CIRStreamNode* node = &graph.nodes[index];
            if (node->kind != CIRStreamDataNode || !node->decl.name) {
                continue;
            }
            if (equal_string(node->decl.name->name, "global")) {
                node->definition.data.initializer.bytes = initializer;
                node->definition.data.initializer.size = sizeof(initializer);
                ++edits;
            } else if (equal_string(node->decl.name->name, "ptr") &&
                       node->definition.data.relocationCount == 1) {
                node->definition.data.relocations[0].addend = 0;
                ++edits;
            }
        }
        if (edits != 2) {
            return 0;
        }
    }
    if (!CIRStream_ExportCallGraph(&stream, &graph)) {
        return 0;
    }
    return capacity - stream.size;
}

static Boolean edit_return(CIRStreamFunctionCode* code) {
    Statement* stmt;
    ENode* addition;
    ENode* constant;

    for (stmt = code->statements; stmt && stmt->type != ST_RETURN; stmt = stmt->next) {}
    if (!stmt || !stmt->expr || stmt->expr->rtype != TYPE(&stsignedint)) {
        return FALSE;
    }
    addition = galloc(sizeof(ENode));
    constant = galloc(sizeof(ENode));
    memclrw(addition, sizeof(ENode));
    memclrw(constant, sizeof(ENode));
    addition->type = EADD;
    addition->rtype = constant->rtype = TYPE(&stsignedint);
    addition->cost = stmt->expr->cost + 1;
    addition->data.diadic.left = stmt->expr;
    addition->data.diadic.right = constant;
    constant->type = EINTCONST;
    constant->data.intval.lo = 37;
    stmt->expr = addition;
    return TRUE;
}

UInt32 CIRTest_RepackBodies(
    const UInt8* bytes, UInt32 size, UInt8* output, UInt32 capacity, Boolean editEntry
) {
    CIRStreamImportedGraph graph;
    CIRStreamWriter stream = {output, capacity};
    UInt32 index;
    Boolean edited = FALSE;

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return 0;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        CIRStreamNode* node = &graph.nodes[index];
        CIRStreamFunctionDefinition* definition = &node->definition.function;
        CIRStreamWriter body;
        CIRStreamFunctionCode code;

        if (node->kind != CIRStreamFunctionNode || !definition->header.code.size) {
            continue;
        }
        if (!CIRStream_UnpackCode(&graph, index, NULL, &code) ||
            definition->header.code.size > 0x3fffff00UL) {
            return 0;
        }
        if (editEntry && node->linkName && equal_string(node->linkName->name, "__start")) {
            if (!edit_return(&code)) {
                return 0;
            }
            edited = TRUE;
        }
        body.size = 2 * definition->header.code.size + 128;
        body.bytes = galloc(body.size);
        if (!CIRStream_PackCode(&body, &graph, &code, definition)) {
            return 0;
        }
    }
    if ((editEntry && !edited) || !CIRStream_ExportCallGraph(&stream, &graph)) {
        return 0;
    }
    return capacity - stream.size;
}

Boolean CIRTest_CheckMemberLiteral(const UInt8* bytes, UInt32 size) {
    CIRStreamImportedGraph graph;
    CIRStreamFunctionCode code;
    const Statement* stmt;
    const ENode* literal;
    UInt32 index;

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return FALSE;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        const CIRStreamNode* node = &graph.nodes[index];
        if (node->linkName && equal_string(node->linkName->name, "__start")) {
            break;
        }
    }
    if (!CIRStream_UnpackCode(&graph, index, NULL, &code)) {
        return FALSE;
    }
    for (stmt = code.statements; stmt; stmt = stmt->next) {
        if (stmt->type == ST_EXPRESSION && stmt->expr && stmt->expr->type == EASS) {
            literal = stmt->expr->data.diadic.right;
            if (literal && literal->type == EMEMBERFUNCLITERAL) {
                return IS_TYPE_MEMBERPOINTER(literal->rtype) &&
                    literal->data.objref &&
                    literal->data.objref->name &&
                    equal_string(literal->data.objref->name->name, "read");
            }
        }
    }
    return FALSE;
}

static Boolean check_aggregate_args(TypeFunc* type) {
    FuncArg* arg = type->args;
    TypeClass* cell;
    ObjMemberVar* member;
    TypeEnum* shade;
    ObjEnumConst* constant;
    Boolean red = FALSE;
    Boolean green = FALSE;

    if (!arg ||
        !IS_TYPE_POINTER(arg->type) ||
        !IS_TYPE_CLASS(TPTR_TARGET(arg->type)) ||
        !arg->next ||
        arg->next == &elipsis ||
        !IS_TYPE_ENUM(arg->next->type) ||
        arg->next->next != &elipsis) {
        return FALSE;
    }
    cell = TYPE_CLASS(TPTR_TARGET(arg->type));
    if (!cell->classname || !equal_string(cell->classname->name, "Cell")) {
        return FALSE;
    }
    member = cell->ivars;
    if (!member || !IS_TYPE_POINTER(member->type) || TPTR_TARGET(member->type) != TYPE(cell)) {
        return FALSE;
    }
    member = member->next;
    if (!member ||
        member->type->type != TYPEBITFIELD ||
        TYPE_BITFIELD(member->type)->bitlength != 3) {
        return FALSE;
    }
    member = member->next;
    if (!member ||
        !IS_TYPE_ARRAY(member->type) ||
        TPTR_TARGET(member->type) != TYPE(&stsignedint) ||
        member->type->size != 12 ||
        member->next) {
        return FALSE;
    }
    shade = TYPE_ENUM(arg->next->type);
    for (constant = shade->enumlist; constant; constant = constant->next) {
        if (equal_string(constant->name->name, "Red")) {
            red = constant->val.hi == -1 && constant->val.lo == 0xfffffffd;
        }
        if (equal_string(constant->name->name, "Green")) {
            green = constant->val.hi == 0 && constant->val.lo == 257;
        }
    }
    return red && green;
}

static Boolean check_class_decl(CIRStreamObjectDecl* decl, TypeClass** seen) {
    TypeClass* type;
    NameSpace* nspace = decl->nspace;

    if (!nspace) {
        return FALSE;
    }
    if (nspace->theclass) {
        type = nspace->theclass;
        if (!(TYPE_FUNC(decl->type)->flags & FUNC_METHOD) ||
            TYPE_METHOD(decl->type)->theclass != type ||
            !equal_string(decl->name->name, "read")) {
            return FALSE;
        }
        nspace = nspace->parent;
    } else {
        FuncArg* arg = TYPE_FUNC(decl->type)->args;
        if (!arg || !IS_TYPE_POINTER(arg->type) || !IS_TYPE_CLASS(TPTR_TARGET(arg->type))) {
            return FALSE;
        }
        type = TYPE_CLASS(TPTR_TARGET(arg->type));
    }
    if (!type->classname ||
        !equal_string(type->classname->name, "Cell") ||
        type->nspace->parent != nspace ||
        !nspace ||
        !nspace->name ||
        !equal_string(nspace->name->name, "Inner") ||
        !nspace->parent ||
        !nspace->parent->name ||
        !equal_string(nspace->parent->name->name, "Outer") ||
        !type->bases ||
        !type->bases->base->classname ||
        !equal_string(type->bases->base->classname->name, "Base") ||
        !type->ivars ||
        !type->ivars->name ||
        !equal_string(type->ivars->name->name, "value") ||
        type->ivars->type != TYPE(&stsignedint) ||
        (*seen && *seen != type)) {
        return FALSE;
    }
    *seen = type;
    return TRUE;
}

enum {
    CheckPlainFunctions,
    CheckForwardCall,
    CheckAggregates,
    CheckClass,
    CheckControlFlow,
    CheckLiterals,
    CheckSourceFiles,
    CheckFunctionOptions
};

static Boolean patch_test_type(void* context, Type* type, UInt32* index) {
    *index = 255;
    return type == context;
}

Boolean CIRTest_CheckTypeWriter(void) {
    UInt8 buffer[128];
    CIRStreamWriter output = {buffer, sizeof(buffer)};
    CIRStreamReader encoded;
    CIRStreamTypeWriter writer = {0};
    CIRStreamTypeWriter patchWriter = {0};
    CIRStreamTypeContext reader = {0};
    Type* types[2];
    Type* record;
    Type* ref;
    TypeStruct source = {0};
    TypePointer pointer = {TYPEPOINTER, 4, TYPE(&source), Q_CONST};
    TypePointer equivalent = pointer;
    StructMember value = {NULL, TYPE(&stsignedint), NULL, 4, 0};
    StructMember next = {&value, TYPE(&pointer), NULL, 0, 0};

    writer.root = new_root();
    reader.root = CScope_NewHashNameSpace(NULL);
    reader.typeCapacity = 2;
    reader.types = types;
    source.type = TYPESTRUCT;
    source.size = 8;
    source.name = GetHashNameNodeExport("Cell");
    source.members = &next;
    source.align = 4;
    next.name = GetHashNameNodeExport("next");
    value.name = GetHashNameNodeExport("value");

    if (!CIRStream_PackType(&output, &writer, TYPE(&source)) ||
        !CIRStream_PackType(&output, &writer, TYPE(&equivalent)) ||
        writer.typeCount != 2) {
        return FALSE;
    }
    encoded.bytes = buffer;
    encoded.size = sizeof(buffer) - output.size;
    if (!CIRStream_UnpackType(&encoded, &reader, &record) ||
        !record ||
        !IS_TYPE_CLASS(record) ||
        TYPE_CLASS(record)->classname != source.name ||
        record->size != 8 ||
        !CIRStream_UnpackType(&encoded, &reader, &ref) ||
        encoded.size ||
        ref != TYPE_CLASS(record)->ivars->type ||
        !IS_TYPE_POINTER_ONLY(ref) ||
        TPTR_TARGET(ref) != record ||
        TYPE_POINTER(ref)->qual != Q_CONST) {
        return FALSE;
    }
    output.bytes = buffer;
    output.size = sizeof(buffer);
    patchWriter.patchType = patch_test_type;
    patchWriter.patchContext = &pointer;
    return CIRStream_PackType(&output, &patchWriter, TYPE(&pointer)) &&
        !patchWriter.typeCount &&
        output.size == sizeof(buffer) - 6 &&
        buffer[0] == 34 &&
        buffer[1] == 255 &&
        CIRStream_ReadWord(buffer + 2) == 255;
}

static Boolean check_forward_body(
    const CIRStreamImportedGraph* graph, const CIRStreamFunctionCode* code
) {
    const Statement* stmt;
    const ENode* expr;
    const ENode* arg;
    const ENode* call;
    const ENode* constant;

    if (!code->arguments ||
        code->arguments->next ||
        !code->arguments->object->name ||
        !equal_string(code->arguments->object->name->name, "n") ||
        code->locals ||
        code->arguments->object->u.var.info->func != graph->nodes[0].object ||
        code->location[0].fileID != 1 ||
        code->location[0].offset != 22 ||
        code->location[0].line != 1) {
        return FALSE;
    }
    for (stmt = code->statements; stmt && stmt->type != ST_RETURN; stmt = stmt->next) {}
    if (!stmt || !stmt->expr || stmt->expr->type != EADD) {
        return FALSE;
    }
    call = stmt->expr->data.diadic.left;
    constant = stmt->expr->data.diadic.right;
    if (call->type != EFUNCCALL ||
        constant->type != EINTCONST ||
        constant->data.intval.hi ||
        constant->data.intval.lo != 1 ||
        !call->data.funccall.args ||
        call->data.funccall.args->next) {
        return FALSE;
    }
    expr = call->data.funccall.funcref;
    arg = call->data.funccall.args->node;
    return expr->type == EOBJREF &&
        expr->data.objref == graph->nodes[1].object &&
        arg->type == EINDIRECT &&
        arg->data.monadic->type == EOBJREF &&
        arg->data.monadic->data.objref == code->arguments->object;
}

static Boolean check_control_flow(const CIRStreamFunctionCode* code) {
    const Statement* stmt;
    const SwitchInfo* info;
    const SwitchCase* entry;
    UInt32 cases = 0;
    UInt32 branches = 0;
    Boolean jump = FALSE;

    if (!code->arguments ||
        !code->locals ||
        !code->locals->object->name ||
        !equal_string(code->locals->object->name->name, "sum") ||
        !code->info ||
        code->info->statement->type != ST_NOP ||
        code->info->kind != 5 ||
        !code->info->operand ||
        code->info->operand->type != EOBJREF ||
        code->info->operand->data.objref != code->locals->object) {
        return FALSE;
    }
    for (stmt = code->statements; stmt; stmt = stmt->next) {
        switch (stmt->type) {
            case ST_GOTO:
            case ST_IFGOTO:
            case ST_IFNGOTO:
                if (!stmt->label ||
                    !stmt->label->stmt ||
                    stmt->label->stmt->type != ST_LABEL ||
                    stmt->label->stmt->label != stmt->label) {
                    return FALSE;
                }
                if (stmt->type == ST_GOTO) {
                    jump = TRUE;
                } else if (stmt->expr &&
                           (stmt->expr->type == EGREATER || stmt->expr->type == EAND)) {
                    ++branches;
                }
                break;
            case ST_SWITCH:
                info = (const SwitchInfo*)stmt->label;
                if (!info ||
                    !info->defaultlabel ||
                    !info->defaultlabel->stmt ||
                    info->exprtype != TYPE(&stsignedint)) {
                    return FALSE;
                }
                for (entry = info->cases; entry; entry = entry->next) {
                    if (!entry->label ||
                        !entry->label->stmt ||
                        entry->label->stmt->type != ST_LABEL ||
                        entry->min.hi ||
                        (entry->min.lo != 2 && entry->min.lo != 7)) {
                        return FALSE;
                    }
                    cases |= 1U << entry->min.lo;
                }
                break;
            default:
                break;
        }
    }
    return jump && branches == 2 && cases == ((1U << 2) | (1U << 7));
}

static Boolean check_literals(const CIRStreamFunctionCode* code) {
    const Statement* stmt;
    const ENode* str;
    const ENode* number;
    const ENodeList* args;

    for (stmt = code->statements; stmt && stmt->type != ST_RETURN; stmt = stmt->next) {}
    if (!stmt || !stmt->expr || stmt->expr->type != EFUNCCALL) {
        return FALSE;
    }
    args = stmt->expr->data.funccall.args;
    if (!args || !args->next || args->next->next) {
        return FALSE;
    }
    str = args->node;
    if (str->type == ETYPCON) {
        str = str->data.monadic;
    }
    number = args->next->node;
    return str->type == ESTRINGCONST &&
        str->data.string.size == 4 &&
        !str->data.string.ispascal &&
        equal_string(str->data.string.data, "Wii") &&
        number->type == EFLOATCONST &&
        number->rtype == TYPE(&stdouble) &&
        number->data.floatval.data.words[0] == 0 &&
        number->data.floatval.data.words[1] == 0x400c0000;
}

static Boolean check_source_file(
    const CIRStreamImportedGraph* graph,
    const CIRStreamNode* node,
    const CIRStreamFunctionCode* code
) {
    const Statement* stmt = code->statements;
    UInt32 fileID = code->location[0].fileID;
    const char* name;
    const char* base;

    for (; stmt && stmt->type != ST_RETURN; stmt = stmt->next) {}
    if (!stmt ||
        !fileID ||
        fileID > graph->fileCount ||
        !stmt->sourcefilepath ||
        stmt->sourcefilepath != graph->fileNames[fileID]) {
        return FALSE;
    }
    base = name = stmt->sourcefilepath->name;
    for (; *name; ++name) {
        if (*name == '\\' || *name == '/') {
            base = name + 1;
        }
    }
    if (equal_string(node->decl.name->name, "included")) {
        return fileID > 1 && equal_string(base, "source_locations.h");
    }
    return fileID == 1 && equal_string(base, "include.c");
}

static Boolean check_function_options(const CIRStreamFunctionHeader* function) {
    struct CInlineBackendOptions options = {0};

    options.inlinelevel = 19;
    if (!CIRStream_UnpackFunctionOptions(function, &options)) {
        return FALSE;
    }
    return options.processor == CPU_PPC750 &&
        options.optimizationlevel == 1 &&
        options.removeemptyloops &&
        options.optimize_tail_call &&
        options.optimizesize &&
        options.function_align == 64 &&
        options.sdata_threshold == 79 &&
        options.sdata2_threshold == 113 &&
        options.unroll_instr_limit == 333 &&
        options.unroll_factor_limit == 123 &&
        options.fp_contract &&
        options.legacy_struct_alignment &&
        options.force_cats &&
        options.inlinelevel == 19;
}

// The fixtures contain ordinary function nodes, including a forward reference to an external function.
UInt32 CIRTest_CheckFunctions(const UInt8* bytes, UInt32 size, UInt8 fixture) {
    CIRStreamImportedGraph graph;
    NameSpace* root = new_root();
    UInt32 index;
    Type* calleeType = NULL;
    TypeClass* seenClass = NULL;
    CIRStreamNode* node;
    CIRStreamObjectDecl* decl;
    CIRStreamFunctionHeader* function;
    CIRStreamReference* reference;
    CIRStreamFunctionCode code;

    if (!CIRStream_ImportCallGraph(bytes, size, root, TRUE, &graph)) {
        return 8;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        node = &graph.nodes[index];
        decl = &node->decl;
        if (node->kind != CIRStreamFunctionNode ||
            decl->node.datatype != DFUNC ||
            (fixture != CheckClass && decl->nspace != root) ||
            !decl->name ||
            !decl->type ||
            TYPE_FUNC(decl->type)->functype != TYPE(&stsignedint)) {
            return 1;
        }
        function = &node->definition.function.header;
        if (function->code.size && !CIRStream_UnpackCode(&graph, index, NULL, &code)) {
            return 100 + index;
        }
        if (!index &&
            function->code.size &&
            fixture == CheckControlFlow &&
            !check_control_flow(&code)) {
            return 12;
        }
        if (!index && function->code.size && fixture == CheckLiterals && !check_literals(&code)) {
            return 13;
        }
        if (fixture == CheckSourceFiles &&
            (!function->code.size || !check_source_file(&graph, node, &code))) {
            return 14;
        }
        if (fixture == CheckFunctionOptions && !check_function_options(function)) {
            return 16;
        }
        if (!node->linkName || (fixture != CheckClass && node->linkName != decl->name)) {
            return 2;
        }
        if (fixture == CheckForwardCall &&
            !index &&
            (!equal_string(decl->name->name, "entry") ||
             !TYPE_FUNC(decl->type)->args ||
             TYPE_FUNC(decl->type)->args->type != TYPE(&stsignedint) ||
             !function->code.size ||
             function->referenceCount != 6 ||
             function->localCount != 1)) {
            return 3;
        }
        if (fixture == CheckAggregates && !check_aggregate_args(TYPE_FUNC(decl->type))) {
            return 9;
        }
        if (fixture == CheckClass && !check_class_decl(decl, &seenClass)) {
            return 10;
        }
        if (fixture == CheckForwardCall && !index) {
            reference = node->definition.function.references;
            if (reference[2].header.kind == 0 &&
                reference[2].value.type &&
                IS_TYPE_POINTER(reference[2].value.type)) {
                calleeType = TPTR_TARGET(reference[2].value.type);
            }
            if (reference[3].header.kind != 1 ||
                reference[3].header.callCount != 1 ||
                reference[3].header.flags ||
                reference[3].value.objectID != 1) {
                return 5;
            }
            if (!check_forward_body(&graph, &code)) {
                return 11;
            }
            --function->code.size;
            if (CIRStream_UnpackCode(&graph, index, NULL, &code)) {
                return 15;
            }
            ++function->code.size;
        }
        if (fixture == CheckForwardCall &&
            index == 1 &&
            (!equal_string(decl->name->name, "leaf") ||
             decl->type != calleeType ||
             function->code.size)) {
            return 6;
        }
    }
    return 0;
}

UInt32 CIRTest_CheckExceptions(const UInt8* bytes, UInt32 size) {
    CIRStreamImportedGraph graph;
    CIRStreamFunctionCode code;
    Object* guard = NULL;
    const ObjectList* local;
    const Statement* stmt;
    const ExceptionAction* action;
    UInt32 index;
    Boolean destroysGuard = FALSE;
    Boolean hasCatch = FALSE;
    Boolean activeCatch = FALSE;

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return 1;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        const CIRStreamNode* node = &graph.nodes[index];
        if (node->kind == CIRStreamFunctionNode &&
            node->decl.name &&
            equal_string(node->decl.name->name, "run_guarded")) {
            break;
        }
    }
    if (index == graph.header.nodeCount || !CIRStream_UnpackCode(&graph, index, NULL, &code)) {
        return 2;
    }
    for (local = code.locals; local; local = local->next) {
        if (local->object->name && equal_string(local->object->name->name, "guard")) {
            guard = local->object;
        }
    }
    if (!guard) {
        return 3;
    }
    for (stmt = code.statements; stmt; stmt = stmt->next) {
        for (action = stmt->dobjstack; action; action = action->prev) {
            switch (action->type) {
                case EAT_DESTROYLOCAL:
                    destroysGuard |= action->data.destroy_local.local == guard &&
                        action->data.destroy_local.dtor &&
                        action->data.destroy_local.dtor->datatype == DFUNC;
                    break;
                case EAT_CATCHBLOCK:
                    hasCatch |= action->data.catch_block.catch_label->stmt &&
                        action->data.catch_block.catch_label->stmt->type == ST_LABEL &&
                        !action->data.catch_block.catch_typeid;
                    break;
                case EAT_ACTIVECATCHBLOCK:
                    activeCatch |= action->data.active_catch_block.catch_info_object != NULL;
                    break;
                default:
                    break;
            }
        }
    }
    return destroysGuard && hasCatch && activeCatch ? 0 : 4;
}

Boolean CIRTest_CheckData(const UInt8* bytes, UInt32 size) {
    CIRStreamImportedGraph graph;
    CIRStreamNode* node;
    CIRStreamRelocation* reloc;
    CIRStreamSection section;
    UInt32 index;
    Boolean global = FALSE;
    Boolean pointer = FALSE;

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return FALSE;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        node = &graph.nodes[index];
        if (node->kind != CIRStreamDataNode ||
            !node->decl.name ||
            node->decl.node.section != -2 ||
            !CIRStream_ResolveSection(
                &graph.backend, node->decl.node.section, CIRStreamDataSection, &section
            ) ||
            !section.name ||
            !equal_string(section.name, ".more") ||
            !section.linkedName ||
            !equal_string(section.linkedName, ".morebss")) {
            return FALSE;
        }
        if (equal_string(node->decl.name->name, "global")) {
            global = node->definition.data.initializer.size == 4 &&
                node->definition.data.initializer.bytes[3] == 10 &&
                !node->definition.data.relocationCount;
        }
        if (equal_string(node->decl.name->name, "ptr")) {
            reloc = node->definition.data.relocations;
            pointer = node->definition.data.relocationCount == 1 &&
                !reloc->offset &&
                reloc->addend == 4 &&
                graph.nodes[reloc->objectID].decl.name &&
                equal_string(graph.nodes[reloc->objectID].decl.name->name, "global");
        }
    }
    return global && pointer;
}

Boolean CIRTest_CheckThunk(const UInt8* bytes, UInt32 size) {
    CIRStreamImportedGraph graph;
    CIRStreamNode* node;
    CIRStreamNode* target;
    UInt32 index;

    if (!CIRStream_ImportCallGraph(bytes, size, new_root(), TRUE, &graph)) {
        return FALSE;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        node = &graph.nodes[index];
        if (node->kind == CIRStreamThunkNode) {
            target = &graph.nodes[node->definition.thunk.targetID];
            if (node->definition.thunk.thisDelta &&
                target->kind == CIRStreamFunctionNode &&
                target->decl.name &&
                equal_string(target->decl.name->name, "right")) {
                return TRUE;
            }
        }
    }
    return FALSE;
}
