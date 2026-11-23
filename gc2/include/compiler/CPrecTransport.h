#ifndef COMPILER_CPRECTRANSPORT_H
#define COMPILER_CPRECTRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A null output validates the compressed stream without copying it. */
int CPrec_ExpandRawData(
    const unsigned char* input, size_t inputSize, unsigned char* output, size_t outputSize
);
/* Applies pointer fixups in place. Discard the data if the patch stream is invalid. */
int CPrec_RelocateRawData(
    unsigned char* data,
    size_t dataSize,
    const unsigned char* patches,
    size_t patchSize,
    size_t count,
    unsigned int base
);

int PrecompilerCheckCompressed(const void* image, size_t imageSize);
/* Returns the expanded image size, or zero on failure. A null output queries the size.
 * The input and output buffers must be disjoint. */
size_t PrecompilerExpand(const void* image, size_t imageSize, void* output, size_t capacity);

enum {
    CPrecStockBuiltinCount = 82
};

/* Records builtin index plus one at each patched halfword offset. */
int CPrec_IndexStockBuiltins(
    const void* expanded, size_t imageSize, unsigned char* indices, size_t indexCount
);
const char* CPrec_StockHashName(const unsigned char* raw, size_t rawSize, size_t offset);

typedef struct CPrecStockMacro {
    const unsigned char* raw;
    size_t rawSize;
    const unsigned char* arguments;
    const unsigned char* replacement;
    size_t replacementSize;
    const char* name;
    unsigned short argumentCount;
    unsigned short flags;
} CPrecStockMacro;

enum {
    CPrecStockMacroVariadic = 1,
    CPrecStockMacroGnuVariadic = 2,
    CPrecMacroLeadingSpace = 1,
    CPrecMacroParameter = 4,
    CPrecMacroStringify = 8,
    CPrecMacroUnexpanded = 16,
    CPrecMacroSpelling = 32,
    CPrecMacroPaste = -18
};

typedef struct CPrecMacroToken {
    int kind;
    unsigned char flags;
    const char* spelling;
    size_t length;
    char quote;
    const char* prefix;
} CPrecMacroToken;

/** Visits macro records in an expanded RC1 PCH. The visitor returns zero to stop with failure. */
int CPrec_VisitStockMacros(
    const void* expanded,
    size_t imageSize,
    int (*visit)(const CPrecStockMacro* macro, void* context),
    void* context
);

typedef struct CPrecStockFile {
    const char* path;
    const char* includeName;
    const char* condition;
    int includeOnce;
    int systemInclude;
} CPrecStockFile;

enum {
    CPrecStockFileKindOffset = 0x08,
    CPrecStockFileMetadataOffset = 0x2a,
    CPrecStockFileIncludeNameOffset = 0x04,
    CPrecStockFileConditionOffset = 0x08,
    CPrecStockFileIncludeOnceOffset = 0x0e,
    CPrecStockFileSystemIncludeOffset = 0x0f,
    CPrecStockFileMetadataSize = 0x14
};

int CPrec_VisitRawFiles(
    const unsigned char* raw,
    size_t rawSize,
    size_t offset,
    int (*visit)(const CPrecStockFile* file, void* context),
    void* context
);

int CPrec_VisitStockFiles(
    const void* expanded,
    size_t imageSize,
    int (*visit)(const CPrecStockFile* file, void* context),
    void* context
);
const char* CPrec_StockMacroArgument(const CPrecStockMacro* macro, size_t index);
int decodemacrotoken(const unsigned char** stream, size_t* remaining, CPrecMacroToken* token);

#ifdef __cplusplus
}
#endif

#endif
