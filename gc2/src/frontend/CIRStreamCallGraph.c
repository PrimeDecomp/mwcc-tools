#include "compiler/CIRStreamCallGraph.h"
#include "compiler/CInline.h"
#include "compiler/CParser.h"
#include "compiler/CompilerTools.h"
#include "compiler/objects.h"
#include "compiler/ObjGen_PPC_EABI.h"

#define CALLGRAPH_MAGIC 0xbaceecabUL
#define CALLGRAPH_REVISION 0x00010002UL

enum {
    CallGraphHeaderSize = 16,
    CallGraphTrailerSize = 5,
    FilePathStorageSize = 260,
    MaxFileNesting = 256
};

typedef struct FileEntry {
    struct FileEntry* previous;
    HashNameNode* name;
} FileEntry;

typedef struct FileTable {
    UInt32 count;
    Boolean collectNames;
    FileEntry* last;
} FileTable;

static Boolean unpack_file(
    CIRStreamReader* stream, FileTable* files, UInt32 depth, CIRStreamReader* name, UInt32* id
);

static Boolean unpack_optional_file(CIRStreamReader* stream, FileTable* files, UInt32 depth) {
    CIRStreamReader present;

    return CIRStream_UnpackData(stream, 1, &present) &&
        (!present.bytes[0] || unpack_file(stream, files, depth, NULL, NULL));
}

static Boolean unpack_file_record(
    CIRStreamReader* stream, FileTable* files, UInt32* fileID, CIRStreamReader* record
) {
    const UInt8* start = stream->bytes;

    if (!unpack_file(stream, files, 0, NULL, fileID)) {
        return FALSE;
    }
    record->bytes = start;
    record->size = (UInt32)(stream->bytes - start);
    return TRUE;
}

// File references share a one-based index. A zero introduces a new descriptor.
static Boolean unpack_file(
    CIRStreamReader* stream, FileTable* files, UInt32 depth, CIRStreamReader* name, UInt32* id
) {
    UInt32 file;
    UInt32 location;
    CIRStreamReader kind;
    CIRStreamReader include;

    if (depth == MaxFileNesting || !CIRStream_UnpackUInt32(stream, &file)) {
        return FALSE;
    }
    if (file > files->count) {
        return FALSE;
    }
    if (id) {
        *id = file ? file : files->count + 1;
    }
    if (file) {
        return TRUE;
    }
    ++files->count;
    if (files->collectNames) {
        FileEntry* entry = galloc(sizeof(FileEntry));
        if (!CIRStream_UnpackName(stream, &entry->name)) {
            return FALSE;
        }
        entry->previous = files->last;
        files->last = entry;
    } else if (!CIRStream_UnpackTypeHashNameNode(stream, name)) {
        return FALSE;
    }
    if (!CIRStream_UnpackData(stream, 2, &kind)) {
        return FALSE;
    }
    switch (kind.bytes[0]) {
        case 0:
            if (!CIRStream_UnpackData(stream, FilePathStorageSize + 2 + 8, NULL) ||
                !CIRStream_UnpackData(stream, 1, &include)) {
                return FALSE;
            }
            if (include.bytes[0] &&
                (!unpack_optional_file(stream, files, depth + 1) ||
                 !CIRStream_UnpackTypeHashNameNode(stream, NULL) ||
                 !CIRStream_UnpackData(stream, 2, NULL))) {
                return FALSE;
            }
            return CIRStream_UnpackData(stream, 2, NULL);
        case 1:
        case 2:
            if (!unpack_optional_file(stream, files, depth + 1) ||
                !CIRStream_UnpackUInt32(stream, &location) ||
                !CIRStream_UnpackUInt32(stream, &location)) {
                return FALSE;
            }
            return kind.bytes[0] == 2 ||
                (unpack_optional_file(stream, files, depth + 1) &&
                 CIRStream_UnpackUInt32(stream, &location) &&
                 CIRStream_UnpackUInt32(stream, &location));
        default:
            return FALSE;
    }
}

Boolean CIRStream_PackSourceFile(
    CIRStreamWriter* stream,
    const HashNameNode* path,
    const HashNameNode* include,
    UInt32 timeLow,
    UInt32 timeHigh,
    Boolean systemInclude
) {
    UInt8 physical[FilePathStorageSize + 10];
    CIRStreamWriter output = *stream;
    UInt32 size = 0;

    if (!path) {
        return FALSE;
    }
    memclrw(physical, sizeof(physical));
    while (size < FilePathStorageSize && path->name[size]) {
        physical[size] = path->name[size];
        ++size;
    }
    if (size == FilePathStorageSize) {
        return FALSE;
    }
    // Kind zero is a physical source file. The include spelling can be
    // recorded even when there is no including file (the translation unit).
    physical[FilePathStorageSize] = 1;
    CIRStream_WriteWord(physical + FilePathStorageSize + 2, timeLow);
    CIRStream_WriteWord(physical + FilePathStorageSize + 6, timeHigh);
    if (!CIRStream_WriteUInt32(&output, 0) ||
        !CIRStream_PackTypeHashNameNode(&output, path) ||
        !CIRStream_PackByte(&output, 0) ||
        !CIRStream_PackByte(&output, 2) ||
        !CIRStream_PackData(&output, physical, sizeof(physical)) ||
        !CIRStream_PackByte(&output, include != NULL) ||
        (include &&
         (!CIRStream_PackByte(&output, 0) ||
          !CIRStream_PackTypeHashNameNode(&output, include) ||
          !CIRStream_PackUInt16(&output, systemInclude))) ||
        !CIRStream_PackByte(&output, 1) ||
        !CIRStream_PackByte(&output, 0)) {
        return FALSE;
    }
    *stream = output;
    return TRUE;
}

Boolean CIRStream_CheckHeader(const UInt8* bytes, UInt32 size) {
    return size >= CallGraphHeaderSize && CIRStream_ReadWord(bytes) == CALLGRAPH_MAGIC;
}

static Boolean pack_graph_prefix(CIRStreamWriter* stream, const CIRStreamGraphHeader* graph) {
    UInt8 header[CallGraphHeaderSize];

    CIRStream_WriteWord(header, CALLGRAPH_MAGIC);
    CIRStream_WriteWord(header + 4, CALLGRAPH_REVISION);
    CIRStream_WriteWord(header + 8, graph->typeCount);
    CIRStream_WriteWord(header + 12, graph->complexUsed);
    return CIRStream_PackData(stream, header, sizeof(header)) &&
        CIRStream_PackData(stream, graph->mainFile.bytes, graph->mainFile.size) &&
        CIRStream_WriteUInt32(stream, graph->backend.size) &&
        CIRStream_PackData(stream, graph->backend.bytes, graph->backend.size) &&
        CIRStream_WriteUInt32(stream, graph->nodeCount);
}

Boolean CIRStream_PackCallGraph(CIRStreamWriter* stream, const CIRStreamGraphHeader* graph) {
    CIRStreamWriter output = *stream;
    UInt8 compact[5];
    UInt32 backendSizeLength = CIRStream_PackUInt32(compact, graph->backend.size);
    UInt32 nodeCountLength = CIRStream_PackUInt32(compact, graph->nodeCount);
    UInt32 remaining = stream->size;
    UInt32 sizes[] = {
        CallGraphHeaderSize + backendSizeLength + nodeCountLength + CallGraphTrailerSize,
        graph->mainFile.size,
        graph->backend.size,
        graph->nodes.size
    };
    UInt32 index;

    for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        if (remaining < sizes[index]) {
            return FALSE;
        }
        remaining -= sizes[index];
    }

    pack_graph_prefix(&output, graph);
    CIRStream_PackData(&output, graph->nodes.bytes, graph->nodes.size);
    CIRStream_WriteUInt32(&output, CALLGRAPH_MAGIC);
    *stream = output;
    return TRUE;
}

Boolean CIRStream_UnpackCallGraphHeader(
    const UInt8* bytes, UInt32 size, CIRStreamGraphHeader* header
) {
    CIRStreamGraphHeader graph;
    CIRStreamReader stream;
    FileTable files = {0};
    UInt32 backendSize;
    const UInt8* trailer;

    if (size < CallGraphHeaderSize + CallGraphTrailerSize ||
        !CIRStream_CheckHeader(bytes, size) ||
        CIRStream_ReadWord(bytes + 4) != CALLGRAPH_REVISION) {
        return FALSE;
    }
    trailer = bytes + size - CallGraphTrailerSize;
    if (trailer[0] != 0xff || CIRStream_ReadWord(trailer + 1) != CALLGRAPH_MAGIC) {
        return FALSE;
    }

    graph.typeCount = CIRStream_ReadWord(bytes + 8);
    graph.complexUsed = bytes[12];
    stream.bytes = bytes + CallGraphHeaderSize;
    stream.size = size - CallGraphHeaderSize - CallGraphTrailerSize;
    graph.mainFile.bytes = stream.bytes;
    if (!unpack_file(&stream, &files, 0, &graph.mainFileName, NULL)) {
        return FALSE;
    }
    graph.fileCount = files.count;
    graph.mainFile.size = (UInt32)(stream.bytes - graph.mainFile.bytes);
    if (!CIRStream_UnpackUInt32(&stream, &backendSize) ||
        !CIRStream_UnpackData(&stream, backendSize, &graph.backend) ||
        !CIRStream_UnpackUInt32(&stream, &graph.nodeCount) ||
        graph.nodeCount > stream.size) {
        return FALSE;
    }
    graph.nodes = stream;
    *header = graph;
    return TRUE;
}

static Boolean unpack_records(CIRStreamReader block, UInt32 stride, CIRStreamReader* records) {
    UInt32 count;

    if (block.size < 12 ||
        CIRStream_ReadWord(block.bytes) != 1 ||
        CIRStream_ReadWord(block.bytes + 8) != stride) {
        return FALSE;
    }
    count = CIRStream_ReadWord(block.bytes + 4);
    return CIRStream_UnpackData(&block, 12, NULL) &&
        block.size % stride == 0 &&
        count == block.size / stride &&
        CIRStream_UnpackData(&block, count * stride, records);
}

Boolean CIRStream_UnpackBackendHeader(CIRStreamReader backend, CIRStreamBackendHeader* header) {
    CIRStreamReader blocks[6];
    CIRStreamBackendHeader info;
    UInt32 start;
    UInt32 end;
    UInt32 index;
    UInt32 stringSize;

    if (backend.size < CIRStreamBackendHeaderSize ||
        CIRStream_ReadWord(backend.bytes) != 2 ||
        CIRStream_ReadWord(backend.bytes + 8) != CIRStreamBackendHeaderSize) {
        return FALSE;
    }

    start = CIRStreamBackendHeaderSize;
    for (index = 0; index < 6; ++index) {
        end = CIRStream_ReadWord(backend.bytes + 12 + index * 4);
        if (end < start || end > backend.size || (end & 3)) {
            return FALSE;
        }
        blocks[index].bytes = backend.bytes + start;
        blocks[index].size = end - start;
        start = end;
    }
    if (start != backend.size ||
        !unpack_records(blocks[0], CIRStreamBackendSectionSize, &info.sections) ||
        !unpack_records(blocks[1], CIRStreamBackendSectionAttributeSize, &info.sectionAttributes) ||
        !unpack_records(blocks[2], CIRStreamBackendInterruptSize, &info.interrupts) ||
        !unpack_records(blocks[5], CIRStreamBackendFileSize, &info.files) ||
        blocks[3].size != CIRStreamBackendDefaultsSize ||
        CIRStream_ReadWord(blocks[3].bytes) != 1 ||
        CIRStream_ReadWord(blocks[3].bytes + 4) != CIRStreamSectionRoleCount ||
        blocks[4].size < 8 ||
        CIRStream_ReadWord(blocks[4].bytes) != 1) {
        return FALSE;
    }
    info.flags = CIRStream_ReadWord(backend.bytes + 4);
    info.defaultSections.bytes = blocks[3].bytes + 8;
    info.defaultSections.size = CIRStreamSectionRoleCount * 4;
    stringSize = CIRStream_ReadWord(blocks[4].bytes + 4);
    if (!CIRStream_UnpackData(&blocks[4], 8, NULL) ||
        !CIRStream_UnpackData(&blocks[4], stringSize, &info.strings) ||
        blocks[4].size > 3) {
        return FALSE;
    }
    *header = info;
    return TRUE;
}

const char* CIRStream_BackendString(const CIRStreamBackendHeader* backend, UInt32 offset) {
    UInt32 end;

    for (end = offset; end < backend->strings.size; ++end) {
        if (!backend->strings.bytes[end]) {
            return (const char*)backend->strings.bytes + offset;
        }
    }
    return NULL;
}

// Section pointers in the backend block are one-based record numbers.
static Boolean section_name(
    const CIRStreamBackendHeader* backend, UInt32 record, const char** name
) {
    UInt32 offset;

    *name = NULL;
    if (!record) {
        return TRUE;
    }
    if (record > backend->sections.size / CIRStreamBackendSectionSize) {
        return FALSE;
    }
    offset = CIRStream_ReadWord(
        backend->sections.bytes + (record - 1) * CIRStreamBackendSectionSize + 32
    );
    *name = CIRStream_BackendString(backend, offset);
    return *name != NULL;
}

Boolean CIRStream_ResolveSection(
    const CIRStreamBackendHeader* backend,
    SInt32 selector,
    CIRStreamSectionRole role,
    CIRStreamSection* result
) {
    CIRStreamSection section;
    const UInt8* record;
    UInt32 sectionID;
    UInt32 index;

    if ((UInt32)role >= CIRStreamSectionRoleCount) {
        return FALSE;
    }
    if (selector < 0) {
        // Negative selectors refer to a set of sections for an individual declaration.
        index = (UInt32)(-(selector + 1));
        if (index >= backend->interrupts.size / CIRStreamBackendInterruptSize) {
            return FALSE;
        }
        record = backend->interrupts.bytes + index * CIRStreamBackendInterruptSize;
        if (CIRStream_ReadWord(record + 12) != (UInt32)selector) {
            return FALSE;
        }
        index = CIRStream_ReadWord(record + 4) == 1 ? CIRStream_ReadWord(record + 8) : 0;
        if (!index) {
            index = CIRStream_ReadWord(record + 16 + role * 4);
        }
    } else if (selector > 0) {
        index = (UInt32)selector;
    } else {
        if (backend->defaultSections.size < ((UInt32)role + 1) * 4) {
            return FALSE;
        }
        index = CIRStream_ReadWord(backend->defaultSections.bytes + role * 4);
    }
    if (index >= backend->sectionAttributes.size / CIRStreamBackendSectionAttributeSize) {
        return FALSE;
    }
    record = backend->sectionAttributes.bytes + index * CIRStreamBackendSectionAttributeSize;
    sectionID = CIRStream_ReadWord(record + 4);
    if (CIRStream_ReadWord(record + 8) != index ||
        !section_name(backend, sectionID, &section.name) ||
        !section_name(backend, CIRStream_ReadWord(record + 16), &section.linkedName)) {
        return FALSE;
    }
    section.permissions = sectionID
        ? CIRStream_ReadWord(backend->sections.bytes + (sectionID - 1) * CIRStreamBackendSectionSize + 8)
        : 0;
    section.dataMode = record[12];
    section.codeMode = record[13];
    *result = section;
    return TRUE;
}

Boolean CIRStream_UnpackNodeHeader(CIRStreamReader* stream, CIRStreamNodeHeader* header) {
    CIRStreamReader input = *stream;
    CIRStreamReader flags;
    CIRStreamReader selector;
    CIRStreamNodeHeader node = {0};
    UInt32 tag;
    UInt32 size;

    if (!CIRStream_UnpackUInt32(&input, &tag)) {
        return FALSE;
    }
    if (tag >= 2) {
        node.implicitObject = tag - 1;
    } else {
        if (!CIRStream_UnpackData(&input, 3, &flags)) {
            return FALSE;
        }
        node.root = (Boolean)tag;
        node.access = flags.bytes[0];
        node.datatype = flags.bytes[1];
        if (flags.bytes[2]) {
            if (!CIRStream_UnpackUInt32(&input, &size) ||
                size != 4 ||
                !CIRStream_UnpackData(&input, size, &selector)) {
                return FALSE;
            }
            node.section = (SInt32)CIRStream_ReadWord(selector.bytes);
        } else if (!CIRStream_UnpackInt32(&input, &node.section)) {
            return FALSE;
        }
    }
    *header = node;
    *stream = input;
    return TRUE;
}

Boolean CIRStream_UnpackFunctionHeader(CIRStreamReader* stream, CIRStreamFunctionHeader* header) {
    CIRStreamReader input = *stream;
    CIRStreamFunctionHeader function = {0};
    UInt32 codeSize;

    if (!CIRStream_UnpackUInt32(&input, &codeSize)) {
        return FALSE;
    }
    if (codeSize &&
        (!CIRStream_UnpackUInt32(&input, &function.inlineSize) ||
         !CIRStream_UnpackData(&input, CIRStreamFunctionOptionsSize, &function.options) ||
         !CIRStream_UnpackUInt32(&input, &function.referenceCount) ||
         !CIRStream_UnpackData(&input, codeSize, &function.code) ||
         !CIRStream_UnpackUInt32(&input, &function.localCount) ||
         !CIRStream_UnpackUInt32(&input, &function.labelCount) ||
         !CIRStream_UnpackUInt32(&input, &function.typeCount) ||
         function.referenceCount > input.size / 4)) {
        return FALSE;
    }
    *header = function;
    *stream = input;
    return TRUE;
}

/* Offsets in RC1's serialized CMach_GetBackEndOptions record. */
#define FUNCTION_OPTION_BYTES(X) \
    X(0x0e, optimizationlevel) \
    X(0x0f, optimizesize) \
    X(0x10, optimizewithasm) \
    X(0x11, exceptions) \
    X(0x1d, commonsubs) \
    X(0x1e, loopinvariants) \
    X(0x1f, propagation) \
    X(0x20, deadstore) \
    X(0x21, strengthreduction) \
    X(0x22, strengthreductionstrict) \
    X(0x23, deadcode) \
    X(0x25, lifetimes) \
    X(0x26, unrolling) \
    X(0x27, vectorizeloops) \
    X(0x28, multiplepasses) \
    X(0x29, opt_pointer_analysis) \
    X(0x2a, opt_pointer_analysis_mode) \
    X(0x2b, unrollfactor) \
    X(0x35, removeemptyloops) \
    X(0x67, optimize_tail_call) \
    X(0x6a, scheduling) \
    X(0x6b, schedule_factor) \
    X(0x6c, profile) \
    X(0x6d, traceback) \
    X(0x6e, disable_registers) \
    X(0x6f, fp_contract) \
    X(0x70, no_register_save_helpers) \
    X(0x71, unroll_speculative) \
    X(0x76, altivec_model) \
    X(0x77, altivec_vrsave) \
    X(0x79, cats) \
    X(0x7a, force_cats) \
    X(0x84, pool_data) \
    X(0x85, common) \
    X(0x88, codegen_pic) \
    X(0x8a, no_comment_records) \
    X(0x8b, rel109_offset) \
    X(0x9a, e500_floatingpoint) \
    X(0x9b, e500v2_floatingpoint) \
    X(0x9f, use_lmw_stmw) \
    X(0xa1, switch_tables) \
    X(0xb0, gprfloatcopy) \
    X(0xbc, ppc_no_fp_blockmove) \
    X(0xcc, legacy_struct_alignment)

#define FUNCTION_OPTION_SHORTS(X) \
    X(0x68, processor) \
    X(0x72, unroll_instr_limit) \
    X(0x74, unroll_factor_limit)

#define FUNCTION_OPTION_WORDS(X) \
    X(0x7c, sdata_threshold) \
    X(0x80, sdata2_threshold)

Boolean CIRStream_UnpackFunctionOptions(
    const CIRStreamFunctionHeader* header, struct CInlineBackendOptions* options
) {
    const UInt8* bytes = header->options.bytes;

    if (header->options.size != CIRStreamFunctionOptionsSize || bytes[0x78] > 7) {
        return FALSE;
    }
#define READ_BYTE(offset, member) options->member = bytes[offset];
#define READ_SHORT(offset, member) \
    options->member = (SInt16)(bytes[offset] | ((UInt16)bytes[offset + 1] << 8));
#define READ_WORD(offset, member) options->member = (SInt32)CIRStream_ReadWord(bytes + offset);
    FUNCTION_OPTION_BYTES(READ_BYTE)
    FUNCTION_OPTION_SHORTS(READ_SHORT)
    FUNCTION_OPTION_WORDS(READ_WORD)
#undef READ_WORD
#undef READ_SHORT
#undef READ_BYTE
    options->function_align = (UInt8)(1U << bytes[0x78]);
    return TRUE;
}

Boolean CIRStream_PackFunctionOptions(
    CIRStreamWriter* stream,
    const struct CInlineBackendOptions* options,
    UInt8 canInline,
    Boolean initializer
) {
    UInt8 bytes[CIRStreamFunctionOptionsSize];
    UInt8 alignment = options->function_align;

    if (!alignment || (alignment & (alignment - 1))) {
        return FALSE;
    }
    memclrw(bytes, sizeof(bytes));
    while (alignment > 1) {
        alignment >>= 1;
        ++bytes[0x78];
    }
#define WRITE_BYTE(offset, member) bytes[offset] = options->member;
#define WRITE_SHORT(offset, member) \
    bytes[offset] = (UInt8)options->member; \
    bytes[offset + 1] = (UInt8)((UInt16)options->member >> 8);
#define WRITE_WORD(offset, member) CIRStream_WriteWord(bytes + offset, (UInt32)options->member);
    FUNCTION_OPTION_BYTES(WRITE_BYTE)
    FUNCTION_OPTION_SHORTS(WRITE_SHORT)
    FUNCTION_OPTION_WORDS(WRITE_WORD)
#undef WRITE_WORD
#undef WRITE_SHORT
#undef WRITE_BYTE
    bytes[0xb7] = CPU_IS_SPE(options->processor);
    bytes[0xce] = canInline;
    bytes[0xcf] = initializer;
    return CIRStream_PackData(stream, bytes, sizeof(bytes));
}

#undef FUNCTION_OPTION_WORDS
#undef FUNCTION_OPTION_SHORTS
#undef FUNCTION_OPTION_BYTES

Boolean CIRStream_UnpackObjectDecl(
    CIRStreamReader* stream, CIRStreamTypeContext* context, CIRStreamObjectDecl* decl
) {
    if (!CIRStream_UnpackNodeHeader(stream, &decl->node)) {
        return FALSE;
    }
    if (decl->node.implicitObject) {
        return TRUE;
    }
    return CIRStream_UnpackNameSpace(stream, context, &decl->nspace) &&
        CIRStream_UnpackName(stream, &decl->name) &&
        CIRStream_UnpackType(stream, context, &decl->type) &&
        CIRStream_UnpackUInt32(stream, &decl->qualifiers) &&
        CIRStream_UnpackUInt16(stream, &decl->storageClass) &&
        CIRStream_UnpackUInt16(stream, &decl->flags);
}

Boolean CIRStream_UnpackReferenceHeader(CIRStreamReader* stream, CIRStreamReferenceHeader* header) {
    CIRStreamReader data;

    if (stream->size < 4 || stream->bytes[3] > 4) {
        return FALSE;
    }
    CIRStream_UnpackData(stream, 4, &data);
    header->callCount = (UInt16)(data.bytes[0] | ((UInt16)data.bytes[1] << 8));
    header->flags = data.bytes[2];
    header->kind = data.bytes[3];
    return TRUE;
}

static Boolean CIRStream_UnpackReference(
    CIRStreamReader* stream,
    CIRStreamTypeContext* context,
    FileTable* files,
    UInt32 nodeCount,
    CIRStreamReference* reference
) {
    if (!CIRStream_UnpackReferenceHeader(stream, &reference->header)) {
        return FALSE;
    }
    switch (reference->header.kind) {
        case CIRStreamRefType:
            return CIRStream_UnpackType(stream, context, &reference->value.type);
        case CIRStreamRefObject:
            return CIRStream_UnpackUInt32(stream, &reference->value.objectID) &&
                reference->value.objectID < nodeCount;
        case CIRStreamRefFile:
            return unpack_file_record(
                stream, files, &reference->value.fileID, &reference->fileRecord
            );
        case CIRStreamRefSourceType:
            reference->value.type = NULL;
            return TRUE;
        case CIRStreamRefName:
            return CIRStream_UnpackName(stream, &reference->value.name);
        default:
            return FALSE;
    }
}

static Boolean unpack_graph_data(
    CIRStreamReader* stream, FileTable* files, UInt32 nodeCount, CIRStreamNode* node
) {
    CIRStreamDataDefinition* data = &node->definition.data;
    UInt8 present;
    UInt32 index;
    CIRStreamRelocation* reloc;

    if (!CIRStream_UnpackName(stream, &node->linkName) ||
        !CIRStream_UnpackUInt32(stream, &data->size)) {
        return FALSE;
    }
    if (data->size) {
        if (!CIRStream_UnpackByte(stream, &present) ||
            (present &&
             (!unpack_file_record(stream, files, &data->fileID, &data->fileRecord) ||
              !CIRStream_UnpackUInt32(stream, &data->location[0]) ||
              !CIRStream_UnpackUInt32(stream, &data->location[1]))) ||
            !CIRStream_UnpackByte(stream, &present) ||
            (present && !CIRStream_UnpackData(stream, data->size, &data->initializer)) ||
            !CIRStream_UnpackUInt32(stream, &data->relocationCount) ||
            data->relocationCount > stream->size / 3 ||
            data->relocationCount > 0x7fffffffUL / sizeof(CIRStreamRelocation)) {
            return FALSE;
        }
        if (data->relocationCount) {
            data->relocations = galloc(data->relocationCount * sizeof(CIRStreamRelocation));
        }
        for (index = 0; index < data->relocationCount; ++index) {
            reloc = &data->relocations[index];
            if (!CIRStream_UnpackUInt32(stream, &reloc->objectID) ||
                reloc->objectID >= nodeCount ||
                !CIRStream_UnpackInt32(stream, &reloc->offset) ||
                !CIRStream_UnpackInt32(stream, &reloc->addend)) {
                return FALSE;
            }
        }
        if (!CIRStream_UnpackByte(stream, &data->flags)) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean unpack_graph_function(
    CIRStreamReader* stream,
    CIRStreamTypeContext* context,
    FileTable* files,
    UInt32 nodeCount,
    CIRStreamNode* node
) {
    CIRStreamFunctionHeader* function = &node->definition.function.header;
    UInt32 index;

    if (!node->decl.type ||
        !IS_TYPE_FUNC(node->decl.type) ||
        !CIRStream_UnpackName(stream, &node->linkName)) {
        return FALSE;
    }
    if (TYPE_FUNC(node->decl.type)->flags & FUNC_INTRINSIC) {
        node->kind = CIRStreamIntrinsicNode;
        return CIRStream_UnpackInt32(stream, &node->definition.intrinsic);
    }
    if (node->decl.qualifiers & CIRStreamThunkQualifier) {
        node->kind = CIRStreamThunkNode;
        return CIRStream_UnpackUInt32(stream, &node->definition.thunk.targetID) &&
            node->definition.thunk.targetID < nodeCount &&
            CIRStream_UnpackInt32(stream, &node->definition.thunk.thisDelta) &&
            CIRStream_UnpackInt32(stream, &node->definition.thunk.returnDelta) &&
            CIRStream_UnpackInt32(stream, &node->definition.thunk.ctorOffset);
    }
    node->kind = CIRStreamFunctionNode;
    if (!CIRStream_UnpackFunctionHeader(stream, function) ||
        function->referenceCount > 0x7fffffffUL / sizeof(CIRStreamReference)) {
        return FALSE;
    }
    if (function->referenceCount) {
        node->definition.function.references =
            galloc(function->referenceCount * sizeof(CIRStreamReference));
    }
    for (index = 0; index < function->referenceCount; ++index) {
        if (!CIRStream_UnpackReference(
                stream, context, files, nodeCount, &node->definition.function.references[index]
            )) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean unpack_graph_node(
    CIRStreamReader* stream,
    CIRStreamTypeContext* context,
    FileTable* files,
    UInt32 nodeCount,
    CIRStreamNode* node
) {
    UInt32 size;

    if (!CIRStream_UnpackObjectDecl(stream, context, &node->decl)) {
        return FALSE;
    }
    if (node->decl.node.implicitObject) {
        node->kind = CIRStreamImplicitNode;
        return TRUE;
    }
    switch (node->decl.node.datatype) {
        case DDATA:
            node->kind = CIRStreamDataNode;
            if (!unpack_graph_data(stream, files, nodeCount, node)) {
                return FALSE;
            }
            break;
        case DFUNC:
        case DVFUNC:
            if (!unpack_graph_function(stream, context, files, nodeCount, node)) {
                return FALSE;
            }
            break;
        case DABSOLUTE:
            node->kind = CIRStreamAbsoluteNode;
            return CIRStream_UnpackUInt32(stream, &node->definition.address);
        case DINLINEFUNC:
            node->kind = CIRStreamAssemblyNode;
            return CIRStream_UnpackUInt32(stream, &size) &&
                CIRStream_UnpackData(stream, size, &node->definition.assembly);
        default:
            return FALSE;
    }
    return CIRStream_UnpackUInt16(stream, &node->attributes) &&
        CIRStream_UnpackByte(stream, &node->extra);
}

static Boolean CIRStream_UnpackNodes(
    const CIRStreamGraphHeader* graph,
    CIRStreamTypeContext* context,
    FileTable* files,
    CIRStreamNode** result
) {
    CIRStreamReader stream = graph->nodes;
    UInt32 index;
    CIRStreamNode* nodes = NULL;

    if (graph->nodeCount > stream.size || graph->nodeCount > 0x7fffffffUL / sizeof(CIRStreamNode)) {
        return FALSE;
    }
    if (graph->nodeCount) {
        nodes = galloc(graph->nodeCount * sizeof(CIRStreamNode));
        memclrw(nodes, graph->nodeCount * sizeof(CIRStreamNode));
    }
    for (index = 0; index < graph->nodeCount; ++index) {
        if (!unpack_graph_node(&stream, context, files, graph->nodeCount, &nodes[index])) {
            return FALSE;
        }
    }
    if (stream.size || context->typeCount != graph->typeCount) {
        return FALSE;
    }
    *result = nodes;
    return TRUE;
}

// RC1's implicit object IDs use the order in the table at 0x4BDE70.
// Entries are addresses because the parser recreates these objects per TU.
static Object* const* const runtime[] = {
    NULL,           &newh_func,     &delh_func,      &copy_func,    &clear_func,
    &rt_ptmf_null,  &rt_ptmf_cmpr,  &rt_ptmf_cast,   &rt_ptmf_test, &rt_ptmf_call,
    &rt_ptmf_scall, &rt_ptmf_call4, &rt_ptmf_scall4, &Rgtid_func,   &Rdync_func,
    &carr_func,     &darr_func,     &cnar_func,      &dnar_func,    &dnar3_func,
    &Xgreg_func,    &Xthrw_func,    &Xicth_func,     &Xecth_func,   &Xunex_func,
};

static Object* implicit_runtime_object(UInt32 id) {
    return id && id < sizeof(runtime) / sizeof(runtime[0]) ? *runtime[id] : NULL;
}

UInt32 CIRStream_ImplicitObjectID(const Object* object) {
    UInt32 id;

    if (object) {
        for (id = 1; id < sizeof(runtime) / sizeof(runtime[0]); ++id) {
            if (*runtime[id] == object) {
                return id;
            }
        }
    }
    return 0;
}

static Object* materialize_object(const CIRStreamNode* node) {
    const CIRStreamObjectDecl* decl = &node->decl;
    Object* object;

    if (node->kind == CIRStreamImplicitNode) {
        return implicit_runtime_object(decl->node.implicitObject);
    }
    object = galloc(sizeof(Object));
    memclrw(object, sizeof(Object));
    object->otype = OT_OBJECT;
    object->access = (AccessType)decl->node.access;
    object->datatype = decl->node.datatype;
    object->nspace = decl->nspace;
    object->name = decl->name;
    object->type = decl->type;
    object->qual = decl->qualifiers;
    object->sclass = (SInt16)decl->storageClass;
    object->flags = decl->flags;
    switch (object->datatype) {
        case DDATA:
            object->u.data.linkname = node->linkName;
            break;
        case DFUNC:
        case DVFUNC:
            object->u.func.linkname = node->linkName;
            object->u.func.interruptflags = node->attributes;
            if ((object->qual & Q_INTERRUPT) && !object->u.func.interruptflags) {
                object->u.func.interruptflags = Interrupt_On;
            }
            if (node->kind == CIRStreamIntrinsicNode) {
                object->u.func.u.intrinsicid = node->definition.intrinsic;
            }
            break;
        case DABSOLUTE:
            object->u.address = node->definition.address;
            break;
    }
    return object;
}

Boolean CIRStream_ImportCallGraph(
    const UInt8* bytes,
    UInt32 size,
    NameSpace* root,
    Boolean pairedSingles,
    CIRStreamImportedGraph* result
) {
    CIRStreamImportedGraph graph = {0};
    CIRStreamTypeContext context = {0};
    FileTable files = {0};
    CIRStreamReader mainFile;
    FileEntry* file;
    UInt32 index;

    if (!CIRStream_UnpackCallGraphHeader(bytes, size, &graph.header) ||
        !CIRStream_UnpackBackendHeader(graph.header.backend, &graph.backend) ||
        graph.header.typeCount > graph.header.nodes.size ||
        graph.header.typeCount > 0x7fffffffUL / sizeof(Type*)) {
        return FALSE;
    }
    context.root = root;
    context.pairedSingles = pairedSingles;
    context.keepWireDetails = TRUE;
    context.typeCapacity = graph.header.typeCount;
    if (context.typeCapacity) {
        context.types = galloc(context.typeCapacity * sizeof(Type*));
    }
    files.collectNames = TRUE;
    mainFile = graph.header.mainFile;
    if (!unpack_file(&mainFile, &files, 0, NULL, NULL) ||
        !CIRStream_UnpackNodes(&graph.header, &context, &files, &graph.nodes) ||
        files.count >= 0x7fffffffUL / sizeof(HashNameNode*)) {
        return FALSE;
    }
    graph.fileCount = files.count;
    graph.fileNames = galloc((files.count + 1) * sizeof(HashNameNode*));
    graph.fileNames[0] = NULL;
    index = files.count;
    for (file = files.last; file; file = file->previous) {
        graph.fileNames[index--] = file->name;
    }
    for (index = 0; index < graph.header.nodeCount; ++index) {
        graph.nodes[index].object = materialize_object(&graph.nodes[index]);
    }
    graph.root = root;
    graph.pairedSingles = pairedSingles;
    graph.wireTypes = context.wireDetails;
    *result = graph;
    return TRUE;
}

static Boolean pack_object_decl(
    CIRStreamWriter* stream, CIRStreamTypeWriter* types, const CIRStreamObjectDecl* decl
) {
    const CIRStreamNodeHeader* header = &decl->node;
    UInt8 selector[4];

    if (header->implicitObject) {
        return header->implicitObject != 0xffffffffUL &&
            CIRStream_WriteUInt32(stream, header->implicitObject + 1);
    }
    if (!CIRStream_WriteUInt32(stream, header->root != 0) ||
        !CIRStream_PackByte(stream, header->access) ||
        !CIRStream_PackByte(stream, header->datatype) ||
        !CIRStream_PackByte(stream, header->section != 0)) {
        return FALSE;
    }
    if (header->section) {
        CIRStream_WriteWord(selector, (UInt32)header->section);
        if (!CIRStream_WriteUInt32(stream, sizeof(selector)) ||
            !CIRStream_PackData(stream, selector, sizeof(selector))) {
            return FALSE;
        }
    } else if (!CIRStream_WriteInt32(stream, 0)) {
        return FALSE;
    }
    return CIRStream_PackNameSpace(stream, types, decl->nspace) &&
        CIRStream_PackTypeHashNameNode(stream, decl->name) &&
        CIRStream_PackType(stream, types, decl->type) &&
        CIRStream_WriteUInt32(stream, decl->qualifiers) &&
        CIRStream_PackUInt16(stream, decl->storageClass) &&
        CIRStream_PackUInt16(stream, decl->flags);
}

static Boolean pack_reference(
    CIRStreamWriter* stream,
    CIRStreamTypeWriter* types,
    UInt32 nodeCount,
    const CIRStreamReference* reference
) {
    const CIRStreamReferenceHeader* header = &reference->header;

    if (!CIRStream_PackUInt16(stream, header->callCount) ||
        !CIRStream_PackByte(stream, header->flags) ||
        !CIRStream_PackByte(stream, header->kind)) {
        return FALSE;
    }
    switch (header->kind) {
        case 0:
            return CIRStream_PackType(stream, types, reference->value.type);
        case 1:
            return reference->value.objectID < nodeCount &&
                CIRStream_WriteUInt32(stream, reference->value.objectID);
        case 2:
            return reference->fileRecord.size &&
                CIRStream_PackData(stream, reference->fileRecord.bytes, reference->fileRecord.size);
        case 3:
            return TRUE;
        case 4:
            return CIRStream_PackTypeHashNameNode(stream, reference->value.name);
        default:
            return FALSE;
    }
}

static Boolean pack_graph_function(
    CIRStreamWriter* stream, CIRStreamTypeWriter* types, UInt32 nodeCount, const CIRStreamNode* node
) {
    const CIRStreamFunctionHeader* function = &node->definition.function.header;
    UInt32 index;

    if (!CIRStream_WriteUInt32(stream, function->code.size)) {
        return FALSE;
    }
    if (!function->code.size) {
        return TRUE;
    }
    if (function->options.size != CIRStreamFunctionOptionsSize ||
        !CIRStream_WriteUInt32(stream, function->inlineSize) ||
        !CIRStream_PackData(stream, function->options.bytes, function->options.size) ||
        !CIRStream_WriteUInt32(stream, function->referenceCount) ||
        !CIRStream_PackData(stream, function->code.bytes, function->code.size) ||
        !CIRStream_WriteUInt32(stream, function->localCount) ||
        !CIRStream_WriteUInt32(stream, function->labelCount) ||
        !CIRStream_WriteUInt32(stream, function->typeCount)) {
        return FALSE;
    }
    for (index = 0; index < function->referenceCount; ++index) {
        if (!pack_reference(
                stream, types, nodeCount, &node->definition.function.references[index]
            )) {
            return FALSE;
        }
    }
    return TRUE;
}

static Boolean pack_graph_data(
    CIRStreamWriter* stream, UInt32 nodeCount, const CIRStreamDataDefinition* data
) {
    const CIRStreamRelocation* reloc;
    UInt32 index;

    if (!CIRStream_WriteUInt32(stream, data->size)) {
        return FALSE;
    }
    if (!data->size) {
        return TRUE;
    }
    if (!CIRStream_PackByte(stream, data->fileRecord.size != 0) ||
        (data->fileRecord.size &&
         (!CIRStream_PackData(stream, data->fileRecord.bytes, data->fileRecord.size) ||
          !CIRStream_WriteUInt32(stream, data->location[0]) ||
          !CIRStream_WriteUInt32(stream, data->location[1]))) ||
        !CIRStream_PackByte(stream, data->initializer.size != 0) ||
        (data->initializer.size &&
         (data->initializer.size != data->size ||
          !CIRStream_PackData(stream, data->initializer.bytes, data->initializer.size))) ||
        !CIRStream_WriteUInt32(stream, data->relocationCount)) {
        return FALSE;
    }
    for (index = 0; index < data->relocationCount; ++index) {
        reloc = &data->relocations[index];
        if (reloc->objectID >= nodeCount ||
            !CIRStream_WriteUInt32(stream, reloc->objectID) ||
            !CIRStream_WriteInt32(stream, reloc->offset) ||
            !CIRStream_WriteInt32(stream, reloc->addend)) {
            return FALSE;
        }
    }
    return CIRStream_PackByte(stream, data->flags);
}

static Boolean pack_graph_node(
    CIRStreamWriter* stream, CIRStreamTypeWriter* types, UInt32 nodeCount, const CIRStreamNode* node
) {
    if (!pack_object_decl(stream, types, &node->decl)) {
        return FALSE;
    }
    switch (node->kind) {
        case CIRStreamImplicitNode:
            return node->decl.node.implicitObject != 0;
        case CIRStreamAbsoluteNode:
            return CIRStream_WriteUInt32(stream, node->definition.address);
        case CIRStreamAssemblyNode:
            return CIRStream_WriteUInt32(stream, node->definition.assembly.size) &&
                CIRStream_PackData(
                       stream, node->definition.assembly.bytes, node->definition.assembly.size
                );
        default:
            break;
    }
    if (!CIRStream_PackTypeHashNameNode(stream, node->linkName)) {
        return FALSE;
    }
    switch (node->kind) {
        case CIRStreamDataNode:
            if (!pack_graph_data(stream, nodeCount, &node->definition.data)) {
                return FALSE;
            }
            break;
        case CIRStreamFunctionNode:
            if (!pack_graph_function(stream, types, nodeCount, node)) {
                return FALSE;
            }
            break;
        case CIRStreamIntrinsicNode:
            if (!CIRStream_WriteInt32(stream, node->definition.intrinsic)) {
                return FALSE;
            }
            break;
        case CIRStreamThunkNode:
            if (node->definition.thunk.targetID >= nodeCount ||
                !CIRStream_WriteUInt32(stream, node->definition.thunk.targetID) ||
                !CIRStream_WriteInt32(stream, node->definition.thunk.thisDelta) ||
                !CIRStream_WriteInt32(stream, node->definition.thunk.returnDelta) ||
                !CIRStream_WriteInt32(stream, node->definition.thunk.ctorOffset)) {
                return FALSE;
            }
            break;
        default:
            return FALSE;
    }
    return CIRStream_PackUInt16(stream, node->attributes) &&
        CIRStream_PackByte(stream, node->extra);
}

Boolean CIRStream_ExportCallGraph(CIRStreamWriter* stream, const CIRStreamImportedGraph* graph) {
    CIRStreamWriter output = *stream;
    CIRStreamTypeWriter types = {0};
    UInt32 index;

    types.root = graph->root;
    types.pairedSingles = graph->pairedSingles;
    types.wireDetails = graph->wireTypes;
    if (!pack_graph_prefix(&output, &graph->header)) {
        return FALSE;
    }
    for (index = 0; index < graph->header.nodeCount; ++index) {
        if (!pack_graph_node(&output, &types, graph->header.nodeCount, &graph->nodes[index])) {
            return FALSE;
        }
    }
    if (!CIRStream_WriteUInt32(&output, CALLGRAPH_MAGIC)) {
        return FALSE;
    }
    if (stream->bytes) {
        CIRStream_WriteWord(stream->bytes + 8, types.typeCount);
    }
    *stream = output;
    return TRUE;
}
