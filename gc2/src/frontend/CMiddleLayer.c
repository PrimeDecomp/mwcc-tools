#include "compiler/CMiddleLayer.h"
#include "compiler/CCallGraph.h"
#include "compiler/BE_elf.h"
#include "compiler/CMachine.h"
#include "compiler/CError.h"
#include "compiler/CException.h"
#include "compiler/CExpr.h"
#include "compiler/CIRStreamCode.h"
#include "compiler/CInit.h"
#include "compiler/CMangler.h"
#include "compiler/CParser.h"
#include "compiler/CPrep.h"
#include "compiler/CScope.h"
#include "compiler/CodeGen.h"
#include "compiler/CompilerTools.h"
#include "compiler/FuncLevelAsmPPC.h"
#include "compiler/Intrinsics.h"
#include "compiler/ObjGen_PPC_EABI.h"
#include "compiler/objects.h"
#include "compiler/scopes.h"
#include "driver/COSToolsCLT.h"
#include "driver/DropInCompilerLinker.h"
#include "driver/Files.h"
#include "driver/Generic.h"
#include "driver/MacSpecs.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/StringExtras.h"
#include <stdio.h>
#include <string.h>

enum {
    IRForceActive = 8
};

static UInt8 cmid_ipa_mode;
static CallGraphNode* deferredData;

void CMid_Setup(void) {
    cmid_ipa_mode =
        cparamblkptr->precompile != 1 && !cparamblkptr->preprocess ? copts.ipa_mode : IPAOff;
    deferredData = NULL;
    CCallGraph_Setup();
}

Boolean CMid_InIPA(void) {
    return cmid_ipa_mode != IPAOff;
}

void CMid_ObjectAddrRef(Object* object) {
    while (object && object->datatype == DALIAS) {
        object = object->u.alias.object;
    }
    if (object) {
        // Function-level assembly is emitted immediately. Its references are already in the object.
        object->flags |= OBJECT_IPA_ROOT;
        CInline_ObjectAddrRef(object);
    }
}

void CMid_DefineFunction(Object* object, CI_FuncData* body, UInt8 oldStyleParameters) {
    CallGraphNode* node = CCallGraph_NewNode(object, CGFunction);
    if (node->definition.function.body &&
        object->u.func.u.ifuncdata == node->definition.function.body) {
        object->u.func.u.ifuncdata = body;
    }
    node->definition.function.body = body;
    node->definition.function.oldStyleParameters = oldStyleParameters;
    CIRStream_PackReferences(body, &node->definition.function.references);
}

void CMid_DefineData(
    Object* object, const void* data, OLinkList* references, SInt32 size, Boolean readOnly,
    Boolean deferData
) {
    CallGraphNode* node = CCallGraph_NewNode(object, CGData);
    OLinkList** tail = &node->definition.data.references;

    if (deferData && !node->definition.data.deferred) {
        node->definition.data.deferred = TRUE;
        node->nextDeferredData = deferredData;
        deferredData = node;
    }
    node->definition.data.bytes = NULL;
    node->definition.data.qualifiers = object->qual;
    node->definition.data.size = size;
    node->definition.data.readOnly = readOnly;
    node->definition.data.filesyminfo = copts.filesyminfo;
    node->definition.data.file = cparser_fileoffset;

    if (data && size > 0) {
        void* copy = galloc(size);
        memcpy(copy, data, size);
        node->definition.data.bytes = copy;
    }

    for (; references; references = references->next) {
        OLinkList* copy = galloc(sizeof(OLinkList));
        *copy = *references;
        *tail = copy;
        tail = &copy->next;
    }
    *tail = NULL;
}

void CMid_DefineThunkObject(
    Object* object, Object* target, SInt32 thisDelta, SInt32 returnDelta, SInt32 ctorOffset
) {
    CallGraphNode* node = CCallGraph_NewNode(object, CGThunk);
    node->definition.thunk.target = target;
    node->definition.thunk.thisDelta = thisDelta;
    node->definition.thunk.returnDelta = returnDelta;
    node->definition.thunk.ctorOffset = ctorOffset;
}

static Object* base_object(Object* object) {
    while (object && object->datatype == DALIAS) {
        object = object->u.alias.object;
    }
    return object;
}

static UInt32 graph_object_id(const CIRStreamImportedGraph* graph, Object* object) {
    UInt32 index;

    object = base_object(object);
    for (index = 0; index < graph->header.nodeCount; ++index) {
        if (graph->nodes[index].object == object) {
            break;
        }
    }
    return index;
}

static void register_graph_object(CIRStreamImportedGraph* graph, Object* object) {
    UInt32 index = graph_object_id(graph, object);
    if (object && index == graph->header.nodeCount) {
        graph->nodes[index].object = base_object(object);
        ++graph->header.nodeCount;
    }
}

static Boolean register_graph_objects(CIRStreamImportedGraph* graph) {
    const CallGraphNode* node;
    const OLinkList* reloc;
    UInt32 index;
    UInt32 capacity = ccallgraph_node_count;

    for (node = ccallgraph; node; node = node->next) {
        switch (node->kind) {
            case CGFunction:
                capacity += node->definition.function.references.count;
                break;
            case CGThunk:
                ++capacity;
                break;
            case CGData:
                for (reloc = node->definition.data.references; reloc; reloc = reloc->next) {
                    ++capacity;
                }
                break;
        }
        if (capacity > 0x7fffffffUL / sizeof(CIRStreamNode)) {
            return FALSE;
        }
    }
    graph->nodes = capacity ? galloc(capacity * sizeof(CIRStreamNode)) : NULL;
    memclrw(graph->nodes, capacity * sizeof(CIRStreamNode));
    graph->header.nodeCount = ccallgraph_node_count;
    for (node = ccallgraph; node; node = node->next) {
        graph->nodes[node->id].object = node->object;
    }
    for (node = ccallgraph; node; node = node->next) {
        switch (node->kind) {
            case CGFunction: {
                const PackedReferences* refs = &node->definition.function.references;
                for (index = 0; index < refs->count; ++index) {
                    register_graph_object(graph, refs->entries[index].object);
                }
                break;
            }
            case CGThunk:
                register_graph_object(graph, node->definition.thunk.target);
                break;
            case CGData:
                for (reloc = node->definition.data.references; reloc; reloc = reloc->next) {
                    register_graph_object(graph, reloc->obj);
                }
                break;
        }
    }
    return TRUE;
}

static Boolean describe_graph_object(CIRStreamNode* node) {
    Object* object = node->object;
    CIRStreamObjectDecl* decl = &node->decl;

    decl->node.implicitObject = CIRStream_ImplicitObjectID(object);
    if (decl->node.implicitObject) {
        node->kind = CIRStreamImplicitNode;
        return TRUE;
    }
    decl->node.access = object->access;
    decl->node.datatype = object->datatype;
    decl->node.section = (SInt16)object->section;
    decl->nspace = object->nspace;
    decl->name = object->name;
    decl->type = object->type;
    decl->qualifiers = object->qual;
    decl->storageClass = object->sclass;
    decl->flags = object->flags & ~OBJECT_IPA_ROOT;
    switch (object->datatype) {
        case DDATA:
            node->kind = CIRStreamDataNode;
            break;
        case DFUNC:
        case DVFUNC:
            node->attributes = object->u.func.interruptflags;
            if (TYPE_FUNC(object->type)->flags & FUNC_INTRINSIC) {
                node->kind = CIRStreamIntrinsicNode;
                node->definition.intrinsic = object->u.func.u.intrinsicid;
                if (node->definition.intrinsic == Intrinsic_count_trailing_zero64) {
                    node->definition.intrinsic = 323;
                }
            } else {
                node->kind = CIRStreamFunctionNode;
            }
            break;
        case DABSOLUTE:
            node->kind = CIRStreamAbsoluteNode;
            node->definition.address = object->u.address;
            return TRUE;
        case DINLINEFUNC:
            node->kind = CIRStreamAssemblyNode;
            node->definition.assembly.bytes = object->u.ifunc.data;
            node->definition.assembly.size = object->u.ifunc.size;
            return TRUE;
        default:
            return FALSE;
    }
    node->linkName = CMangler_GetLinkName(object);
    return node->linkName != NULL;
}

static Boolean physical_file_record(
    CIRStreamReader* record, HashNameNode* path, const FSSpec* spec, Boolean systemInclude
) {
    OSSpec native;
    OSTime time = {0};
    const char* basename = path->name;
    const char* scan;
    HashNameNode* include;
    CIRStreamWriter measure = {NULL, 0x7fffffffUL};
    CIRStreamWriter output;

    for (scan = basename; *scan; ++scan) {
        if (*scan == '/' || *scan == '\\') {
            basename = scan + 1;
        }
    }
    if (spec && !OS_FSSpec_To_OSSpec(spec, &native)) {
        OS_GetFileTime(&native, NULL, &time);
    }
    include = GetHashNameNodeExport(basename);
    if (!CIRStream_PackSourceFile(
            &measure, path, include, time.dwLowDateTime, time.dwHighDateTime, systemInclude
        )) {
        return FALSE;
    }
    record->size = 0x7fffffffUL - measure.size;
    output.bytes = galloc(record->size);
    output.size = record->size;
    record->bytes = output.bytes;
    return CIRStream_PackSourceFile(
        &output, path, include, time.dwLowDateTime, time.dwHighDateTime, systemInclude
    );
}

static Boolean register_file(
    CIRStreamImportedGraph* graph,
    HashNameNode* path,
    const CPrepFileInfo* file,
    UInt32* fileID,
    CIRStreamReader* record
) {
    UInt32 id;

    for (id = 1; id <= graph->fileCount; ++id) {
        if (graph->fileNames[id] == path) {
            UInt8* bytes = galloc(5);
            record->bytes = bytes;
            record->size = CIRStream_PackUInt32(bytes, id);
            *fileID = id;
            return TRUE;
        }
    }
    if (!physical_file_record(
            record, path, file ? &file->textfile : NULL, file && file->isSystemInclude
        )) {
        return FALSE;
    }
    graph->fileNames[id] = path;
    graph->fileCount = *fileID = id;
    return TRUE;
}

static Boolean reference_file(
    CIRStreamImportedGraph* graph,
    CIRStreamFunctionDefinition* definition,
    HashNameNode* path,
    const CPrepFileInfo* file,
    UInt32* fileID
) {
    CIRStreamReference* reference;
    UInt32 index;

    for (index = 0; index < definition->header.referenceCount; ++index) {
        reference = &definition->references[index];
        if (graph->fileNames[reference->value.fileID] == path) {
            *fileID = reference->value.fileID;
            return TRUE;
        }
    }
    reference = &definition->references[definition->header.referenceCount++];
    memclrw(reference, sizeof(*reference));
    reference->header.kind = 2;
    return register_file(graph, path, file, fileID, &reference->fileRecord) &&
        (reference->value.fileID = *fileID) != 0;
}

static Boolean pack_live_function(
    CIRStreamImportedGraph* graph,
    const CallGraphNode* node,
    CIRStreamFunctionDefinition* definition
) {
    CI_FuncData* body = node->definition.function.body;
    CIRStreamFunctionCode code = {0};
    CIRStreamWriter measure = {NULL, 0x7fffffffUL};
    CIRStreamWriter output;
    Statement head;
    Statement* statement;
    CIRStreamStatementLocation** locationTail = &code.sourceLocations;
    CScopeSave scope;
    CPrepFileInfo* file = body->fileoffset.file;
    HashNameNode* path = file ? CPrep_GetSourceFilePath(file) : graph->fileNames[1];
    UInt32 fileID;
    Boolean ok;

    output.bytes = galloc(CIRStreamFunctionOptionsSize);
    output.size = CIRStreamFunctionOptionsSize;
    definition->header.options.bytes = output.bytes;
    definition->header.options.size = output.size;
    definition->header.inlineSize = body->numstatements;
    if (!CIRStream_PackFunctionOptions(
            &output, &body->options, body->can_inline, node->initializer, copts.peephole
        )) {
        return FALSE;
    }
    definition->references = lalloc((2 + body->numstatements) * sizeof(CIRStreamReference));
    if (!reference_file(graph, definition, path, file, &fileID)) {
        return FALSE;
    }
    code.location[0].fileID = code.location[1].fileID = code.location[2].fileID = fileID;
    code.location[0].offset = body->fileoffset.tokenoffset;
    code.location[0].line = body->symdecloffset;
    code.location[1].line = body->functionbodyoffset;
    code.location[2].line = body->symdeclend;
    code.oldStyleParameters = node->definition.function.oldStyleParameters;
    code.callOnModuleBind = node->initializer;
    CScope_SetFunctionScope(node->object, &scope);
    CFunc_FuncGenSetup(&head, node->object);
    CInline_UnpackIFunctionData(node->object, body, &head);
    CScope_RestoreScope(&scope);
    code.statements = &head;
    code.arguments = arguments;
    code.locals = locals;
    head.sourcefilepath = body->functionbodypath;
    head.sourceoffset = body->functionbodyoffset;
    for (statement = &head; statement; statement = statement->next) {
        if (statement->sourcefilepath) {
            CIRStreamStatementLocation* location = lalloc(sizeof(*location));
            if (!reference_file(graph, definition, statement->sourcefilepath, NULL, &fileID)) {
                return FALSE;
            }
            location->next = NULL;
            location->statement = statement;
            location->location.fileID = fileID;
            location->location.offset = 0;
            location->location.line = statement->sourceoffset;
            *locationTail = location;
            locationTail = &location->next;
        }
    }
    ok = CIRStream_PackCode(&measure, graph, &code, definition);
    if (ok) {
        output.size = 0x7fffffffUL - measure.size;
        output.bytes = galloc(output.size);
        ok = CIRStream_PackCode(&output, graph, &code, definition) && !output.size;
    }
    if (CInline_CanFreeLHeap()) {
        freelheap();
    }
    return ok;
}

static Boolean pack_live_data(
    CIRStreamImportedGraph* graph, const CallGraphNode* node, CIRStreamDataDefinition* definition
) {
    const OLinkList* reloc;
    CIRStreamRelocation* output;
    const FileOffsetInfo* location = &node->definition.data.file;

    definition->size = node->definition.data.size;
    if (!definition->size) {
        return TRUE;
    }
    if (node->definition.data.bytes) {
        definition->initializer.size = definition->size;
        definition->initializer.bytes = node->definition.data.bytes;
    }
    if (node->definition.data.filesyminfo &&
        location->file &&
        !register_file(
            graph,
            CPrep_GetSourceFilePath(location->file),
            location->file,
            &definition->fileID,
            &definition->fileRecord
        )) {
        return FALSE;
    }
    definition->location[0] = location->tokenoffset;
    definition->location[1] = location->tokenline;
    definition->flags = node->definition.data.readOnly;
    for (reloc = node->definition.data.references; reloc; reloc = reloc->next) {
        ++definition->relocationCount;
    }
    if (definition->relocationCount) {
        output = definition->relocations = galloc(definition->relocationCount * sizeof(*output));
        for (reloc = node->definition.data.references; reloc; reloc = reloc->next, ++output) {
            const Object* object = reloc->obj;
            output->objectID = graph_object_id(graph, reloc->obj);
            output->offset = reloc->offset;
            output->addend = reloc->addend;
            while (object->datatype == DALIAS) {
                output->addend += object->u.alias.offset;
                object = object->u.alias.object;
            }
        }
    }
    return TRUE;
}

static Boolean prepare_program_graph(CIRStreamImportedGraph* graph) {
    const CallGraphNode* node;
    UInt32 index;
    UInt32 fileCapacity = 2;
    HashNameNode* mainPath = CTool_GetPathName(&cparamblkptr->sourcefile, NULL);

    graph->root = cscope_root;
    graph->pairedSingles = copts.processor == CPU_PPCGEKKO;
    CCallGraph_Renumber();
    if (!register_graph_objects(graph)) {
        return FALSE;
    }
    for (node = ccallgraph; node; node = node->next) {
        if (node->kind == CGFunction) {
            if (!node->definition.function.body ||
                node->definition.function.body->numstatements < 0) {
                return FALSE;
            }
            fileCapacity += 2 + node->definition.function.body->numstatements;
        } else {
            ++fileCapacity;
        }
    }
    if (fileCapacity > 0x7fffffffUL / sizeof(HashNameNode*)) {
        return FALSE;
    }
    graph->fileNames = galloc(fileCapacity * sizeof(HashNameNode*));
    graph->fileNames[0] = NULL;
    graph->fileNames[1] = mainPath;
    graph->fileCount = 1;
    if (!physical_file_record(
            &graph->header.mainFile, mainPath, &cparamblkptr->sourcefile, FALSE
        )) {
        return FALSE;
    }
    for (index = 0; index < graph->header.nodeCount; ++index) {
        if (!describe_graph_object(&graph->nodes[index])) {
            return FALSE;
        }
    }
    for (node = ccallgraph; node; node = node->next) {
        CIRStreamNode* output = &graph->nodes[node->id];
        output->decl.node.root = node->initializer;
        if (node->root || (node->object->flags & OBJECT_IPA_ROOT)) {
            output->decl.flags |= IRForceActive;
        }
        switch (node->kind) {
            case CGFunction:
                if (output->kind != CIRStreamFunctionNode ||
                    !pack_live_function(graph, node, &output->definition.function)) {
                    return FALSE;
                }
                break;
            case CGData:
                if (output->kind == CIRStreamDataNode &&
                    !pack_live_data(graph, node, &output->definition.data)) {
                    return FALSE;
                }
                break;
            case CGThunk:
                output->kind = CIRStreamThunkNode;
                output->decl.qualifiers |= CIRStreamThunkQualifier;
                output->definition.thunk.targetID =
                    graph_object_id(graph, node->definition.thunk.target);
                output->definition.thunk.thisDelta = node->definition.thunk.thisDelta;
                output->definition.thunk.returnDelta = node->definition.thunk.returnDelta;
                output->definition.thunk.ctorOffset = node->definition.thunk.ctorOffset;
                break;
        }
    }
    return TRUE;
}

static void export_program_graph(void) {
    CIRStreamImportedGraph graph = {0};
    CIRStreamWriter measure = {NULL, 0x7fffffffUL};
    CIRStreamWriter output;
    GList backend;
    CWFileSpec spec;
    OSSpec native;
    SInt16 handle;
    SInt32 size;
    UInt8* bytes;
    OSErr written;

    if (!prepare_program_graph(&graph)) {
        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
        return;
    }
    if (InitGList(&backend, 500)) {
        CError_NoMem();
    }
    CodeGen_WritePrecompilerData(&backend);
    LockGList(&backend);
    graph.header.backend.bytes = (const UInt8*)*backend.data;
    graph.header.backend.size = backend.size;
    if (!CIRStream_ExportCallGraph(&measure, &graph)) {
        CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
    } else {
        size = 0x7fffffffUL - measure.size;
        bytes = galloc(size);
        output.bytes = bytes;
        output.size = size;
        if (!CIRStream_ExportCallGraph(&output, &graph) || output.size) {
            CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
        } else if (cparamblkptr->deferProgramIPA) {
            cparamblkptr->programIR = COS_NewHandle(size);
            if (!cparamblkptr->programIR) {
                CError_NoMem();
            }
            memcpy(*cparamblkptr->programIR, bytes, size);
        } else if (CWGetSuggestedObjectFileSpec(
                       cparamblkptr->context, cparamblkptr->whichfile, &spec
                   ) != cwNoErr ||
                   OS_FSSpec_To_OSSpec(&spec, &native) ||
                   OS_NameSpecChangeExtension(&native.name, ".irobj", FALSE) ||
                   OS_OSSpec_To_FSSpec(&native, &spec) ||
                   COS_FileNew(&spec, &handle, 0x43574945UL, 0x49524f62UL)) {
            CError_Error(ERR_CANNOT_CREATE_OBJECT_FILE);
        } else {
            written = COS_FileWrite(handle, bytes, size);
            if (COS_FileClose(handle) || written) {
                CError_Error(ERR_ERROR_WRITING_OBJECT_FILE);
            }
        }
    }
    UnlockGList(&backend);
    FreeGList(&backend);
}

static void CMid_GenerateCGNode(CallGraphNode* node) {
    Object* object = node->object;
    Boolean savedForceActive = copts.force_active;
    copts.force_active = node->forceActive;
    object->flags |= OBJECT_DEFINED | OBJECT_REFERENCED;
    switch (node->kind) {
        case CGFunction: {
            CWFileSpec savedSource = cparamblkptr->sourcefile;
            if (node->initializer && node->sourceFile) {
                cparamblkptr->sourcefile = *node->sourceFile;
            }
            CInline_GenerateGraphFunction(
                object,
                node->definition.function.body,
                node->definition.function.oldStyleParameters,
                node->initializer
            );
            cparamblkptr->sourcefile = savedSource;
            break;
        }
        case CGThunk:
            CodeGen_GenVDispatchThunk(
                object,
                node->definition.thunk.target,
                node->definition.thunk.thisDelta,
                node->definition.thunk.returnDelta,
                node->definition.thunk.ctorOffset
            );
            break;
        case CGData: {
            FileOffsetInfo savedFile = cparser_fileoffset;
            Boolean savedDebug = copts.filesyminfo;
            UInt32 savedQual = object->qual;
            cparser_fileoffset = node->definition.data.file;
            copts.filesyminfo = node->definition.data.filesyminfo;
            object->qual = node->definition.data.qualifiers;
            if (copts.filesyminfo) {
                CPrep_SetSourceFile(&cparser_fileoffset);
            }
            if (node->definition.data.readOnly) {
                ObjGen_DeclareReadOnlyData(
                    object,
                    node->definition.data.bytes,
                    node->definition.data.references,
                    node->definition.data.size
                );
            } else {
                ObjGen_DeclareData(
                    object,
                    node->definition.data.bytes,
                    node->definition.data.references,
                    node->definition.data.size
                );
            }
            object->qual = savedQual;
            copts.filesyminfo = savedDebug;
            cparser_fileoffset = savedFile;
            if (copts.filesyminfo) {
                CPrep_SetSourceFile(&cparser_fileoffset);
            }
            break;
        }
    }
    copts.force_active = savedForceActive;
}

void CMid_Finish(void) {
    CallGraphNode* node;

    if (cmid_ipa_mode == IPAOff) {
        return;
    }
    if (!anyerrors) {
        CCallGraph_ResolveReferences();
        CCallGraph_RemoveDeadNodes();
        CInit_RemoveDeadStrings();
        if (cmid_ipa_mode == IPAProgram) {
            CCallGraph_MoveDeferredDataToEnd();
            export_program_graph();
            cmid_ipa_mode = IPAOff;
            deferredData = NULL;
            return;
        }
        CInline_CallGraphInlining();
        if (!anyerrors) {
            CCallGraph_ResolveReferences();
            CCallGraph_RemoveDeadNodes();
            CInit_RemoveDeadStrings();
            CExcept_CallGraphExceptionOptimizer();
        }
    }

    cmid_ipa_mode = IPAOff;
    for (node = ccallgraph; node && !anyerrors; node = node->next) {
        if (node->kind == CGData && node->definition.data.deferred) {
            continue;
        }
        CMid_GenerateCGNode(node);
        if (CInline_CanFreeLHeap()) {
            freelheap();
        }
    }
}

void CMid_FinishDeferredData(void) {
    CallGraphNode* node;

    for (node = deferredData; node && !anyerrors; node = node->nextDeferredData) {
        if (CCallGraph_IsDefined(node->object) &&
            (node->marked || node->forceActive || (node->object->flags & OBJECT_REFERENCED))) {
            CMid_GenerateCGNode(node);
        }
    }
    deferredData = NULL;
}

Boolean CMid_IsIRFile(const FSSpec* spec) {
    OSSpec native;
    const char* extension;

    if (OS_FSSpec_To_OSSpec(spec, &native)) {
        return FALSE;
    }
    extension = strrchr(native.name.s, '.');
    return extension && !ustrcmp(extension, ".irobj");
}

static Boolean read_program_graph(const CWProgramInput* input, CIRStreamImportedGraph* graph) {
    UInt8* bytes = NULL;
    SInt16 handle;
    SInt32 size = 0;
    OSErr error;
    NameSpace* root;

    if (input->ir) {
        size = GetHandleSize(input->ir);
        if (size <= 0) {
            return FALSE;
        }
        bytes = galloc(size);
        HLock(input->ir);
        memcpy(bytes, *input->ir, size);
        HUnlock(input->ir);
    } else {
        if (FSpOpenDF(&input->spec, fsRdPerm, &handle)) {
            return FALSE;
        }
        error = COS_FileGetSize(handle, &size);
        if (!error && size > 0) {
            bytes = galloc(size);
            error = COS_FileRead(handle, bytes, size);
        }
        if (COS_FileClose(handle) || error || !bytes) {
            return FALSE;
        }
    }
    root = CScope_NewHashNameSpace(NULL);
    root->is_global = TRUE;
    return CIRStream_ImportCallGraph(bytes, size, root, copts.processor == CPU_PPCGEKKO, graph);
}

static AddressingMode import_addressing_mode(UInt8 mode) {
    return mode > AM_NEAR_ABS ? mode + 2 : mode;
}

static Boolean import_node_section(const CIRStreamImportedGraph* graph, const CIRStreamNode* node) {
    CIRStreamSectionRole role = CIRStreamCodeSection;
    CIRStreamSection section;
    ObjGenSection* target;

    if (node->kind == CIRStreamDataNode) {
        const CIRStreamDataDefinition* data = &node->definition.data;
        UInt32 size = data->size ? data->size : node->object->type ? node->object->type->size : 0;

        if (data->flags & 1) {
            role = size && size <= (UInt32)copts.sdata2_threshold ? CIRStreamSmallConstSection
                                                                  : CIRStreamConstSection;
        } else {
            role = size && size <= (UInt32)copts.sdata_threshold ? CIRStreamSmallDataSection
                                                                 : CIRStreamDataSection;
        }
    }
    if (!CIRStream_ResolveSection(&graph->backend, node->decl.node.section, role, &section)) {
        return FALSE;
    }
    if (!section.name || !*section.name) {
        return TRUE;
    }
    target = GetSectionByName(section.name, 0, 0);
    if (!target) {
        SectionRec* rec = Initialize_Input(
            (char*)section.name,
            (char*)section.linkedName,
            import_addressing_mode(section.dataMode),
            import_addressing_mode(section.codeMode),
            0
        );
        rec->mode = section.permissions;
        Finish_Pragma_Section(rec);
        target = GetSectionByName(section.name, 0, 0);
    }
    if (!target || !target->header) {
        return FALSE;
    }
    node->object->section = target->header->index;
    return TRUE;
}

static void import_local_info(ObjectList* list) {
    for (; list; list = list->next) {
        Object* object = list->object;
        TStreamElement token = object->u.var.info->deftoken;
        if (!object->name) {
            object->name = CParser_GetUniqueName();
        }
        CFunc_SetupLocalVarInfo(object);
        object->u.var.info->deftoken = token;
    }
}

static Boolean import_member_pointers(const CIRStreamFunctionCode* code) {
    const CIRStreamExpressionInfo* info;

    for (info = code->expressionInfo; info; info = info->next) {
        ENode* expr = info->expression;
        if (expr->type == EMEMBERFUNCLITERAL && !CExpr_MaterializeMemberFunctionLiteral(expr)) {
            return FALSE;
        }
    }
    return TRUE;
}

Boolean CMid_LoadPrecompiledFunction(
    Object* object, CIRStreamFunctionCode* code, const CIRStreamFunctionHeader* header
) {
    struct CInlineBackendOptions options;
    CScopeSave scope;
    ObjectList* savedArguments = arguments;
    ObjectList* savedLocals = locals;
    CI_FuncData* body;
    Statement head = {0};

    CMach_GetBackEndOptions(&options);
    if (!code->statements ||
        (object->qual & Q_ASM) ||
        !CIRStream_UnpackFunctionOptions(header, &options) ||
        !CIRStream_TranslateAssembly(code, FALSE)) {
        return FALSE;
    }
    CScope_SetFunctionScope(object, &scope);
    if (!import_member_pointers(code)) {
        CScope_RestoreScope(&scope);
        return FALSE;
    }
    arguments = code->arguments;
    locals = code->locals;
    import_local_info(arguments);
    import_local_info(locals);
    head.next = code->statements->type == ST_NOP ? code->statements->next : code->statements;
    body = galloc(sizeof(*body));
    CInline_PackIFunctionData(body, &head, object);
    body->options = options;
    body->can_inline = header->options.bytes[0xce];
    body->flags |= CI_UseCallerInlineOptions;
    object->u.func.u.ifuncdata = body;
    arguments = savedArguments;
    locals = savedLocals;
    CScope_RestoreScope(&scope);
    return TRUE;
}

static Boolean import_function(
    const CIRStreamImportedGraph* graph, UInt32 index, const CWFileSpec* sourceFile
) {
    const CIRStreamNode* node = &graph->nodes[index];
    CIRStreamFunctionCode code;
    CIRStreamStatementLocation* location;
    CI_FuncData* body;
    CScopeSave scope;
    struct CInlineBackendOptions options;
    struct CInlineBackendOptions savedOptions;
    Statement head;
    Boolean initializer;
    CallGraphNode* definition;

    if (!CIRStream_UnpackCode(graph, index, NULL, &code) ||
        !code.statements ||
        !CIRStream_TranslateAssembly(&code, (node->object->qual & Q_ASM) != 0)) {
        return FALSE;
    }
    CMach_GetBackEndOptions(&savedOptions);
    options = savedOptions;
    if (!CIRStream_UnpackFunctionOptions(&node->definition.function.header, &options)) {
        return FALSE;
    }
    CMach_SetBackEndOptions(&options);
    CScope_SetFunctionScope(node->object, &scope);
    CFunc_FuncGenSetup(&head, node->object);
    if (!import_member_pointers(&code)) {
        CScope_RestoreScope(&scope);
        CMach_SetBackEndOptions(&savedOptions);
        return FALSE;
    }
    arguments = code.arguments;
    locals = code.locals;
    import_local_info(arguments);
    import_local_info(locals);
    for (location = code.sourceLocations; location; location = location->next) {
        const Statement* statement = location->statement;
        SInt32 line = (SInt32)location->location.line;
        Statement* stmt;
        for (stmt = (Statement*)statement;
             stmt && (!location->next || stmt != location->next->statement);
             stmt = stmt->next) {
            stmt->sourceoffset = line;
        }
    }
    head.next = code.statements->type == ST_NOP ? code.statements->next : code.statements;
    if (node->object->qual & Q_ASM) {
        Boolean savedForceActive = copts.force_active;
        copts.force_active = (node->decl.flags & IRForceActive) != 0;
        Assembler_GenerateIR(node->object);
        copts.force_active = savedForceActive;
        CScope_RestoreScope(&scope);
        CMach_SetBackEndOptions(&savedOptions);
        return !anyerrors;
    }
    initializer = node->decl.node.root || code.callOnModuleBind;
    body = galloc(sizeof(*body));
    CInline_PackIFunctionData(body, &head, node->object);
    body->can_inline = node->definition.function.header.options.bytes[0xce];
    if (!initializer &&
        ((node->object->qual & Q_INLINE) ||
         (TYPE_FUNC(node->object->type)->flags & FUNC_AUTO_INLINE))) {
        node->object->u.func.u.ifuncdata = body;
    }
    definition = CCallGraph_NewNode(node->object, CGFunction);
    definition->initializer = initializer;
    definition->sourceFile = sourceFile;
    definition->forceActive = (node->decl.flags & IRForceActive) != 0;
    definition->root |= initializer || definition->forceActive;
    CMid_DefineFunction(node->object, body, code.oldStyleParameters);
    CScope_RestoreScope(&scope);
    CMach_SetBackEndOptions(&savedOptions);
    return TRUE;
}

static Boolean import_data(const CIRStreamImportedGraph* graph, const CIRStreamNode* node) {
    const CIRStreamDataDefinition* data = &node->definition.data;
    OLinkList* head = NULL;
    OLinkList** tail = &head;
    UInt32 index;
    CallGraphNode* definition;

    for (index = 0; index < data->relocationCount; ++index) {
        const CIRStreamRelocation* relocation = &data->relocations[index];
        Object* target = graph->nodes[relocation->objectID].object;
        OLinkList* entry;
        if (!target) {
            return FALSE;
        }
        entry = lalloc(sizeof(*entry));
        entry->obj = target;
        entry->offset = relocation->offset;
        entry->addend = relocation->addend;
        entry->next = NULL;
        *tail = entry;
        tail = &entry->next;
    }
    CMid_DefineData(node->object, data->initializer.bytes, head, data->size, data->flags & 1, FALSE);
    definition = CCallGraph_NewNode(node->object, CGData);
    definition->forceActive = (node->decl.flags & IRForceActive) != 0;
    definition->root |= definition->forceActive;
    return TRUE;
}

typedef struct ProgramSymbol {
    struct ProgramSymbol* next;
    HashNameNode* name;
    CIRStreamNode* selected;
} ProgramSymbol;

enum {
    ProgramSymbolBuckets = 4096
};

static ProgramSymbol* find_program_symbol(ProgramSymbol** symbols, HashNameNode* name) {
    ProgramSymbol* symbol;
    UInt32 bucket = (UInt16)name->hashval % ProgramSymbolBuckets;

    for (symbol = symbols[bucket]; symbol; symbol = symbol->next) {
        if (symbol->name == name) {
            return symbol;
        }
    }
    symbol = galloc(sizeof(*symbol));
    symbol->name = name;
    symbol->selected = NULL;
    symbol->next = symbols[bucket];
    symbols[bucket] = symbol;
    return symbol;
}

static int program_symbol_rank(const CIRStreamNode* node) {
    switch (node->kind) {
        case CIRStreamFunctionNode:
            if (!node->definition.function.header.code.size) {
                return 0;
            }
            break;
        case CIRStreamDataNode:
            if (!node->definition.data.size) {
                return 0;
            }
            if (!node->definition.data.initializer.bytes) {
                return 1;
            }
            break;
        default:
            break;
    }
    return node->object->qual & (Q_WEAK | Q_IMPLICIT_WEAK) ? 2 : 3;
}

static Boolean program_symbol_has_linkage(const CIRStreamNode* node) {
    return node->object &&
        node->linkName &&
        !node->decl.node.root &&
        (node->kind == CIRStreamFunctionNode ||
         node->kind == CIRStreamDataNode ||
         node->kind == CIRStreamThunkNode ||
         node->kind == CIRStreamAssemblyNode) &&
        !CParser_HasInternalLinkage(node->object);
}

static void resolve_program_symbols(CIRStreamImportedGraph* graphs, UInt32 count) {
    ProgramSymbol** symbols = galloc(ProgramSymbolBuckets * sizeof(*symbols));
    UInt32 unit;
    UInt32 index;
    UInt32 unique = 0;

    memset(symbols, 0, ProgramSymbolBuckets * sizeof(*symbols));
    for (unit = 0; unit < count; ++unit) {
        for (index = 0; index < graphs[unit].header.nodeCount; ++index) {
            CIRStreamNode* node = &graphs[unit].nodes[index];
            if (program_symbol_has_linkage(node)) {
                ProgramSymbol* symbol = find_program_symbol(symbols, node->linkName);
                if (!symbol->selected ||
                    program_symbol_rank(symbol->selected) < program_symbol_rank(node)) {
                    symbol->selected = node;
                } else if (program_symbol_rank(node) == 3 &&
                           program_symbol_rank(symbol->selected) == 3) {
                    CError_SetNullErrorToken();
                    CError_Error(ERR_OBJECT_REDEFINED, node->object);
                }
            }
        }
    }
    for (unit = 0; unit < count; ++unit) {
        for (index = 0; index < graphs[unit].header.nodeCount; ++index) {
            CIRStreamNode* node = &graphs[unit].nodes[index];
            ProgramSymbol* symbol;
            if (!node->object ||
                !node->linkName ||
                (node->kind != CIRStreamFunctionNode &&
                 node->kind != CIRStreamDataNode &&
                 node->kind != CIRStreamThunkNode &&
                 node->kind != CIRStreamAssemblyNode)) {
                continue;
            }
            symbol = find_program_symbol(symbols, node->linkName);
            if (program_symbol_has_linkage(node)) {
                node->object = symbol->selected->object;
            } else if (symbol->selected) {
                char name[40];
                do {
                    snprintf(name, sizeof(name), "$ipa$%lu", (unsigned long)unique++);
                    node->linkName = GetHashNameNodeExport(name);
                    symbol = find_program_symbol(symbols, node->linkName);
                } while (symbol->selected);
                if (node->kind == CIRStreamDataNode) {
                    node->object->u.data.linkname = node->linkName;
                } else {
                    node->object->u.func.linkname = node->linkName;
                }
                symbol->selected = node;
            } else {
                symbol->selected = node;
            }
        }
    }
    for (unit = 0; unit < count; ++unit) {
        for (index = 0; index < graphs[unit].header.nodeCount; ++index) {
            CIRStreamNode* node = &graphs[unit].nodes[index];
            if (program_symbol_has_linkage(node) &&
                find_program_symbol(symbols, node->linkName)->selected != node) {
                // Keep the canonical object available to references from this input.
                node->kind = CIRStreamImplicitNode;
            }
        }
    }
}

static Boolean import_program_graph(const CIRStreamImportedGraph* graph, const CWFileSpec* spec) {
    UInt32 index;

    for (index = 0; index < graph->header.nodeCount; ++index) {
        const CIRStreamNode* node = &graph->nodes[index];
        Boolean ok = TRUE;
        switch (node->kind) {
            case CIRStreamFunctionNode:
                ok = import_node_section(graph, node) &&
                    (!node->definition.function.header.code.size ||
                     import_function(graph, index, spec));
                break;
            case CIRStreamDataNode:
                ok = import_node_section(graph, node) &&
                    (!node->definition.data.size || import_data(graph, node));
                break;
            case CIRStreamThunkNode:
                ok = import_node_section(graph, node);
                if (ok) {
                    CMid_DefineThunkObject(
                        node->object,
                        graph->nodes[node->definition.thunk.targetID].object,
                        node->definition.thunk.thisDelta,
                        node->definition.thunk.returnDelta,
                        node->definition.thunk.ctorOffset
                    );
                }
                break;
            case CIRStreamAssemblyNode:
                ok = FALSE;
                break;
            default:
                break;
        }
        if (!ok) {
            return FALSE;
        }
    }
    return TRUE;
}

void CMid_CompileEnded(void) {
    CWProgramInput standalone = {cparamblkptr->sourcefile, NULL};
    const CWProgramInput* inputs = cparamblkptr->programInputs;
    UInt32 count = cparamblkptr->programInputCount;
    CIRStreamImportedGraph* graphs;
    UInt32 unit;

    if (!count) {
        inputs = &standalone;
        count = 1;
    }
    graphs = galloc(count * sizeof(*graphs));
    CError_SetNullErrorToken();
    for (unit = 0; unit < count; ++unit) {
        UInt32 index;
        if (!read_program_graph(&inputs[unit], &graphs[unit])) {
            CError_Error(ERR_FILE_CANNOT_OPENED, CTool_GetPathName(&inputs[unit].spec, NULL)->name);
            return;
        }
        for (index = 0; index < graphs[unit].header.nodeCount; ++index) {
            CIRStreamNode* node = &graphs[unit].nodes[index];
            if (node->object && node->kind != CIRStreamImplicitNode) {
                // The wire force-active bit overlaps the frontend's lazy callback bit.
                node->object->flags &= ~OBJECT_LAZY;
            }
            if (node->kind == CIRStreamIntrinsicNode) {
                Object* runtime = Intrinsics_FindRuntimeObject(node->decl.name);
                if (!runtime) {
                    CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
                    return;
                }
                node->object->u.func.u.intrinsicid = runtime->u.func.u.intrinsicid;
            }
        }
    }
    if (count > 1) {
        resolve_program_symbols(graphs, count);
        if (anyerrors) {
            return;
        }
    }
    copts.filesyminfo = FALSE;
    CCallGraph_Setup();
    cmid_ipa_mode = IPAFile;
    for (unit = 0; unit < count; ++unit) {
        if (!import_program_graph(&graphs[unit], &inputs[unit].spec)) {
            CError_SetNullErrorToken();
            CError_Error(ERR_UNIMPLEMENTED_C_FEATURE);
            cmid_ipa_mode = IPAOff;
            return;
        }
    }
    CMid_Finish();
}
