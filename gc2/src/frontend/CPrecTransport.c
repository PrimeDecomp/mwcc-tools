#include "compiler/CPrecTransport.h"
#include <string.h>

enum {
    PCH_HEADER_SIZE = 0x50da,
    PCH_PATH_SIZE = 0x2a,
    PCH_PATH_OFFSET = 0x2e,
    PCH_RAW_SIZE = 0x32,
    PCH_RAW_DATA_SIZE = 0x36,
    PCH_RAW_DATA_OFFSET = 0x3a,
    PCH_RELOCATION_SIZE = 0x42,
    PCH_RELOCATION_OFFSET = 0x46,
    PCH_BUILTIN_PATCH_SIZE = 0x4a,
    PCH_BUILTIN_PATCH_OFFSET = 0x4e,
    PCH_TOKEN_PATCH_SIZE = 0x52,
    PCH_TOKEN_PATCH_OFFSET = 0x56,
    PCH_UNIQUE_FILES = 0xce,
    PCH_BACKEND_SIZE = 0xd2,
    PCH_BACKEND_OFFSET = 0xd6,
    PCH_MACRO_BUCKETS = 0x20da,
    PCH_MACRO_BUCKET_COUNT = 0x800,
    PCH_MACRO_ARGUMENT_COUNT = 0x1c,
    PCH_MACRO_ARGUMENTS = 0x20,
    PCH_HASH_NAME_TEXT = 0x0a,
    PCH_FILE_KIND = 0x08,
    PCH_FILE_METADATA = 0x2a,
    PCH_FILE_INCLUDE_NAME = 0x04,
    PCH_FILE_CONDITION = 0x08,
    PCH_FILE_INCLUDE_ONCE = 0x0e,
    PCH_FILE_SYSTEM_INCLUDE = 0x0f,
    PCH_FILE_METADATA_SIZE = 0x14
};

static unsigned int readHalfword(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int readWord(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
        ((unsigned int)p[3] << 24);
}

static void writeWord(unsigned char* p, unsigned int value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

int CPrec_ExpandRawData(
    const unsigned char* input, size_t inputSize, unsigned char* output, size_t outputSize
) {
    while (inputSize) {
        unsigned int tag = *input++;
        size_t run = tag >= 0xe0 ? tag - 0xe0 + 1 : tag + 1;
        --inputSize;

        if (run > outputSize) {
            return 0;
        }
        if (tag >= 0xe0) {
            if (output) {
                memset(output, 0, run);
            }
        } else {
            if (run > inputSize) {
                return 0;
            }
            if (output) {
                memcpy(output, input, run);
            }
            input += run;
            inputSize -= run;
        }
        if (output) {
            output += run;
        }
        outputSize -= run;
    }
    return outputSize == 0;
}

int CPrec_RelocateRawData(
    unsigned char* data,
    size_t dataSize,
    const unsigned char* patches,
    size_t patchSize,
    size_t count,
    unsigned int base
) {
    unsigned int offset = 0;

    if (dataSize > 0x7fffffffu || count > patchSize) {
        return 0;
    }
    while (count--) {
        unsigned int tag;
        if (!patchSize) {
            return 0;
        }
        tag = *patches;
        if (tag & 0x80) {
            /* High-bit tags store a signed seven-bit delta in halfwords. */
            int delta = (int)(tag & 0x3f) - (int)(tag & 0x40);
            offset += 2 * delta;
            ++patches;
            --patchSize;
        } else {
            if (patchSize < 4) {
                return 0;
            }
            offset = (tag << 24) | ((unsigned int)patches[1] << 16) |
                ((unsigned int)patches[2] << 8) | patches[3];
            patches += 4;
            patchSize -= 4;
        }

        if (offset > dataSize || dataSize - offset < 4) {
            return 0;
        }
        writeWord(data + offset, readWord(data + offset) + base);
    }
    return patchSize == 0;
}

static int isStockHeader(const unsigned char* image, size_t imageSize) {
    return imageSize >= PCH_HEADER_SIZE &&
        readWord(image) == 0xbeefface &&
        image[4] == 0x2e &&
        image[5] == 0x04;
}

int PrecompilerCheckCompressed(const void* image, size_t imageSize) {
    const unsigned char* input = image;
    return isStockHeader(input, imageSize) &&
        readWord(input + PCH_RAW_SIZE) != readWord(input + PCH_RAW_DATA_SIZE);
}

size_t PrecompilerExpand(const void* image, size_t imageSize, void* output, size_t capacity) {
    static const unsigned short sections[][2] = {
        {PCH_PATH_OFFSET, PCH_PATH_SIZE},
        {PCH_RELOCATION_OFFSET, PCH_RELOCATION_SIZE},
        {PCH_BUILTIN_PATCH_OFFSET, PCH_BUILTIN_PATCH_SIZE},
        {PCH_TOKEN_PATCH_OFFSET, PCH_TOKEN_PATCH_SIZE},
        {PCH_BACKEND_OFFSET, PCH_BACKEND_SIZE},
    };
    const unsigned char* input = image;
    unsigned char* result = output;
    size_t rawOffset, storedSize, rawSize, storedEnd, expandedEnd, expandedSize, uniqueFiles;
    size_t i;

    if (imageSize > 0xffffffffu || !isStockHeader(input, imageSize)) {
        return 0;
    }

    rawOffset = readWord(input + PCH_RAW_DATA_OFFSET);
    storedSize = readWord(input + PCH_RAW_DATA_SIZE);
    rawSize = readWord(input + PCH_RAW_SIZE);
    if (rawOffset < PCH_HEADER_SIZE ||
        rawOffset > imageSize ||
        storedSize > imageSize - rawOffset ||
        rawSize > 0xffffffffu - (imageSize - storedSize)) {
        return 0;
    }

    storedEnd = rawOffset + storedSize;
    expandedEnd = rawOffset + rawSize;
    expandedSize = expandedEnd + (imageSize - storedEnd);
    uniqueFiles = readWord(input + PCH_UNIQUE_FILES);
    /* Stock applies the same offset adjustment to the unique-file list root. */
    if (uniqueFiles >= storedEnd) {
        if (uniqueFiles - storedEnd > 0xffffffffu - expandedEnd) {
            return 0;
        }
        uniqueFiles = expandedEnd + (uniqueFiles - storedEnd);
    }

    for (i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        size_t offset = readWord(input + sections[i][0]);
        size_t size = readWord(input + sections[i][1]);
        if (offset > imageSize ||
            size > imageSize - offset ||
            (size &&
             (offset < PCH_HEADER_SIZE || (offset < storedEnd && offset + size > rawOffset)))) {
            return 0;
        }
    }

    if (result && capacity < expandedSize) {
        return 0;
    }

    if (storedSize != rawSize) {
        if (!CPrec_ExpandRawData(
                input + rawOffset, storedSize, result ? result + rawOffset : NULL, rawSize
            )) {
            return 0;
        }
    } else if (result) {
        memcpy(result + rawOffset, input + rawOffset, rawSize);
    }

    if (result) {
        memcpy(result, input, rawOffset);
        memcpy(result + expandedEnd, input + storedEnd, imageSize - storedEnd);
        writeWord(result + PCH_RAW_DATA_SIZE, (unsigned int)rawSize);

        for (i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
            size_t field = sections[i][0];
            size_t offset = readWord(input + field);
            if (offset >= storedEnd) {
                writeWord(result + field, (unsigned int)(expandedEnd + (offset - storedEnd)));
            }
        }
        writeWord(result + PCH_UNIQUE_FILES, (unsigned int)uniqueFiles);
    }
    return expandedSize;
}

const char* CPrec_StockHashName(const unsigned char* raw, size_t rawSize, size_t offset) {
    const unsigned char* text;

    if (!offset || offset > rawSize || rawSize - offset <= PCH_HASH_NAME_TEXT) {
        return NULL;
    }
    text = raw + offset + PCH_HASH_NAME_TEXT;
    return memchr(text, 0, rawSize - offset - PCH_HASH_NAME_TEXT) ? (const char*)text : NULL;
}

static const unsigned char* stockRawGraph(
    const unsigned char* image, size_t imageSize, size_t* rawSize
) {
    size_t offset;

    if (!isStockHeader(image, imageSize)) {
        return NULL;
    }
    offset = readWord(image + PCH_RAW_DATA_OFFSET);
    *rawSize = readWord(image + PCH_RAW_SIZE);
    if (offset < PCH_HEADER_SIZE ||
        offset > imageSize ||
        *rawSize > imageSize - offset ||
        *rawSize != readWord(image + PCH_RAW_DATA_SIZE)) {
        return NULL;
    }
    return image + offset;
}

int CPrec_IndexStockBuiltins(
    const void* expanded, size_t imageSize, unsigned char* indices, size_t indexCount
) {
    const unsigned char* image = expanded;
    const unsigned char* patches;
    size_t rawSize, offset, size;

    if (!stockRawGraph(image, imageSize, &rawSize) || indexCount != rawSize / 2) {
        return 0;
    }
    offset = readWord(image + PCH_BUILTIN_PATCH_OFFSET);
    size = readWord(image + PCH_BUILTIN_PATCH_SIZE);
    if (offset > imageSize || size > imageSize - offset || (size & 3)) {
        return 0;
    }
    memset(indices, 0, indexCount);
    if (!size) {
        return 1;
    }
    patches = image + offset;
    while (size >= 4) {
        size_t count = readWord(patches);
        unsigned int index;

        if (!count) {
            return size == 4;
        }
        if (size < 8 || count > (size - 8) / 4) {
            return 0;
        }
        index = readWord(patches + 4);
        if (index >= CPrecStockBuiltinCount) {
            return 0;
        }
        patches += 8;
        size -= 8 + count * 4;
        for (; count; --count, patches += 4) {
            size_t slot = readWord(patches);
            if ((slot & 1) || slot > rawSize || rawSize - slot < 4 || indices[slot / 2]) {
                return 0;
            }
            indices[slot / 2] = (unsigned char)(index + 1);
        }
    }
    return 0;
}

int CPrec_VisitStockFiles(
    const void* expanded,
    size_t imageSize,
    int (*visit)(const CPrecStockFile* file, void* context),
    void* context
) {
    const unsigned char* image = expanded;
    size_t rawSize;
    const unsigned char* raw = stockRawGraph(image, imageSize, &rawSize);
    size_t offset;
    size_t seen = 0;

    if (!raw) {
        return 0;
    }
    for (offset = readWord(image + PCH_UNIQUE_FILES); offset; offset = readWord(raw + offset + 4)) {
        const unsigned char* record;
        const unsigned char* metadata;
        size_t metadataOffset;
        size_t nameOffset;
        size_t conditionOffset;
        CPrecStockFile file;

        if (offset > rawSize ||
            rawSize - offset <= PCH_FILE_KIND ||
            ++seen > rawSize / (PCH_FILE_KIND + 1)) {
            return 0;
        }
        record = raw + offset;
        if (record[PCH_FILE_KIND] != 0) {
            continue;
        }
        if (rawSize - offset < PCH_FILE_METADATA + 4) {
            return 0;
        }
        metadataOffset = readWord(record + PCH_FILE_METADATA);
        if (!metadataOffset ||
            metadataOffset > rawSize ||
            rawSize - metadataOffset < PCH_FILE_METADATA_SIZE) {
            return 0;
        }
        metadata = raw + metadataOffset;
        file.path = CPrec_StockHashName(raw, rawSize, readWord(record));
        nameOffset = readWord(metadata + PCH_FILE_INCLUDE_NAME);
        file.includeName = nameOffset ? CPrec_StockHashName(raw, rawSize, nameOffset) : NULL;
        conditionOffset = readWord(metadata + PCH_FILE_CONDITION);
        file.condition = NULL;
        if (conditionOffset) {
            if (conditionOffset >= rawSize ||
                !memchr(raw + conditionOffset, 0, rawSize - conditionOffset)) {
                return 0;
            }
            file.condition = (const char*)raw + conditionOffset;
        }
        file.includeOnce = metadata[PCH_FILE_INCLUDE_ONCE] != 0;
        file.systemInclude = metadata[PCH_FILE_SYSTEM_INCLUDE] != 0;
        if (!file.path ||
            !*file.path ||
            (nameOffset && !file.includeName) ||
            !visit(&file, context)) {
            return 0;
        }
    }
    return 1;
}

const char* CPrec_StockMacroArgument(const CPrecStockMacro* macro, size_t index) {
    if (!macro->argumentCount || index >= (size_t)macro->argumentCount - 1) {
        return NULL;
    }
    return CPrec_StockHashName(macro->raw, macro->rawSize, readWord(macro->arguments + index * 4));
}

int CPrec_VisitStockMacros(
    const void* expanded,
    size_t imageSize,
    int (*visit)(const CPrecStockMacro* macro, void* context),
    void* context
) {
    const unsigned char* image = expanded;
    size_t offset;
    size_t rawSize;
    const unsigned char* raw = stockRawGraph(image, imageSize, &rawSize);
    size_t bucket;
    size_t seen = 0;

    if (!raw) {
        return 0;
    }
    for (bucket = 0; bucket < PCH_MACRO_BUCKET_COUNT; ++bucket) {
        offset = readWord(image + PCH_MACRO_BUCKETS + bucket * 4);
        while (offset) {
            CPrecStockMacro macro;
            const unsigned char* record;
            size_t text;
            size_t arguments;

            if (offset > rawSize ||
                rawSize - offset < PCH_MACRO_ARGUMENTS ||
                ++seen > rawSize / PCH_MACRO_ARGUMENTS) {
                return 0;
            }
            record = raw + offset;
            macro.raw = raw;
            macro.rawSize = rawSize;
            macro.name = CPrec_StockHashName(raw, rawSize, readWord(record + 4));
            macro.argumentCount = readHalfword(record + PCH_MACRO_ARGUMENT_COUNT);
            macro.flags = readHalfword(record + PCH_MACRO_ARGUMENT_COUNT + 2);
            macro.arguments = record + PCH_MACRO_ARGUMENTS;
            arguments = macro.argumentCount ? macro.argumentCount - 1 : 0;
            text = readWord(record + 8);
            macro.replacementSize = readWord(record + 12);
            if (!macro.name ||
                !*macro.name ||
                arguments > (rawSize - offset - PCH_MACRO_ARGUMENTS) / 4 ||
                text > rawSize ||
                macro.replacementSize > rawSize - text ||
                (!text && macro.replacementSize)) {
                return 0;
            }
            macro.replacement = raw + text;
            if (!visit(&macro, context)) {
                return 0;
            }
            offset = readWord(record);
        }
    }
    return 1;
}

int decodemacrotoken(const unsigned char** stream, size_t* remaining, CPrecMacroToken* token) {
    /* The scanner's extended punctuation table starts at token number 363. */
    static const char* const punctuation[] = {
        "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=",  "|=", "||",  "&&", "==",
        "!=", "<=", ">=", "<<", ">>", "++",  "--",  "->", "...", ".*", "->*", "::"
    };
    static const char* const literalPrefixes[] = {"", "L", "__L32"};
    const unsigned char* input = *stream;
    size_t size = *remaining;
    unsigned int word;
    int kind;

    if (size < 2) {
        return 0;
    }
    word = readHalfword(input);
    kind = word & 0x3fff;
    if (!kind) {
        return 0;
    }
    if (kind >= 0x2000) {
        kind -= 0x4000;
    }
    input += 2;
    size -= 2;

    token->kind = kind;
    token->flags = word & 0x4000 ? CPrecMacroLeadingSpace : 0;
    token->spelling = NULL;
    token->length = 0;
    token->quote = 0;
    token->prefix = "";
    if (word & 0x8000) {
        if (!size) {
            return 0;
        }
        token->flags |= *input++;
        --size;
    }
    if (token->flags & CPrecMacroSpelling) {
        const unsigned char* end = memchr(input, 0, size);
        if (!end) {
            return 0;
        }
        token->spelling = (const char*)input;
        token->length = end - input;
        size -= token->length + 1;
        input = end + 1;
        if (kind <= -9 && kind >= -11) {
            token->quote = '\'';
            token->prefix = literalPrefixes[-9 - kind];
        } else if (kind <= -12 && kind >= -14) {
            token->quote = '"';
            token->prefix = literalPrefixes[-12 - kind];
        }
    } else if (!(token->flags & CPrecMacroParameter) && kind != CPrecMacroPaste) {
        if (kind >= 363 && (size_t)(kind - 363) < sizeof(punctuation) / sizeof(punctuation[0])) {
            token->spelling = punctuation[kind - 363];
            token->length = strlen(token->spelling);
        } else if (kind < 0x20 || kind > 0xff) {
            return 0;
        }
    }
    *stream = input;
    *remaining = size;
    return 1;
}
