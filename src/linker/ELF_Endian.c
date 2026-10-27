/* The linker's ELF_Endian.c: the byte swapping of the ELF structures between the host's order and the target's
   (big-endian), for the files the linker reads and writes.

   The compiler's src/backend/ELF_Endian.c is the same file as the compiler builds it: the linker's has five of its
   functions, without the tagged records, and seven of its own (the program header, the relocations without addend,
   the line records and the linker's own records). */
#define CERROR_FILE "ELF_Endian.c"
#include "compiler/common.h"
#include "compiler/ELF_Endian.h"
#include "compiler/CompilerTools.h"
#include "driver/libimp-eabi-ppc.h"

static int (*data_0049a9d0)(const char *, int);

/* The records ConvertTwoHalvesTwoWords and ConvertRecord1, 2 and 3 swap. */
typedef struct HalvesWords {
    UInt16 half[2];
    unsigned int word[2];
} HalvesWords;
typedef struct LinkerRecord {
    unsigned char kind;
    unsigned char count;
    UInt16 half;
    unsigned int word[4];
} LinkerRecord;

/* 0x00443850 */
void ELF_Endian_SwapElf32Header(struct Elf32Header *header) {
    header->type = CTool_EndianConvertWord16(header->type);
    header->machine = CTool_EndianConvertWord16(header->machine);
    header->version = CTool_EndianConvertWord32(header->version);
    header->entry = CTool_EndianConvertWord32(header->entry);
    header->program_header_offset = CTool_EndianConvertWord32(header->program_header_offset);
    header->section_header_offset = CTool_EndianConvertWord32(header->section_header_offset);
    header->flags = CTool_EndianConvertWord32(header->flags);
    header->header_size = CTool_EndianConvertWord16(header->header_size);
    header->program_header_size = CTool_EndianConvertWord16(header->program_header_size);
    header->program_header_count = CTool_EndianConvertWord16(header->program_header_count);
    header->section_header_size = CTool_EndianConvertWord16(header->section_header_size);
    header->section_header_count = CTool_EndianConvertWord16(header->section_header_count);
    header->section_name_index = CTool_EndianConvertWord16(header->section_name_index);
}

/* 0x004437c0 */
void ELF_Endian_SwapElf32Section(Elf32Section *values) {
    values->name = CTool_EndianConvertWord32(values->name);
    values->type = CTool_EndianConvertWord32(values->type);
    values->flags = CTool_EndianConvertWord32(values->flags);
    values->address = CTool_EndianConvertWord32(values->address);
    values->file_offset = CTool_EndianConvertWord32(values->file_offset);
    values->size = CTool_EndianConvertWord32(values->size);
    values->link = CTool_EndianConvertWord32(values->link);
    values->info = CTool_EndianConvertWord32(values->info);
    values->alignment = CTool_EndianConvertWord32(values->alignment);
    values->entry_size = CTool_EndianConvertWord32(values->entry_size);
}

/* 0x00443780 */
unsigned short ELF_Endian_ConvertSymbolRecord(void *data) {
    unsigned short sectionIndex;
    struct ElfSymbolRecord *symbol;
    unsigned int name = CTool_EndianConvertWord32(((struct ElfSymbolRecord *)data)->name);

    symbol = data;
    symbol->name = name;
    symbol->value = CTool_EndianConvertWord32(symbol->value);
    symbol->size = CTool_EndianConvertWord32(symbol->size);
    sectionIndex = CTool_EndianConvertWord16(symbol->sectionIndex);
    symbol->sectionIndex = sectionIndex;
    return sectionIndex;
}

/* Two halfwords and two words (12 bytes). */
/* 0x00443740 */
void ELF_Endian_ConvertTwoHalvesTwoWords(HalvesWords *data) {
    data->half[0] = CTool_EndianConvertWord16(data->half[0]);
    data->half[1] = CTool_EndianConvertWord16(data->half[1]);
    data->word[0] = CTool_EndianConvertWord32(data->word[0]);
    data->word[1] = CTool_EndianConvertWord32(data->word[1]);
}

/* An Elf32_Rel: the offset and the info. */
/* 0x00443720 */
void ELF_Endian_ConvertTwoWords(unsigned int *words) {
    words[0] = CTool_EndianConvertWord32(words[0]);
    words[1] = CTool_EndianConvertWord32(words[1]);
}

/* An Elf32_Rela: the offset, the info and the addend. */
/* 0x004436f0 */
void ELF_Endian_ConvertThreeWords(unsigned int *words) {
    words[0] = CTool_EndianConvertWord32(words[0]);
    words[1] = CTool_EndianConvertWord32(words[1]);
    words[2] = CTool_EndianConvertWord32(words[2]);
}

/* An Elf32_Phdr: its eight words. */
/* 0x00443680 */
void ELF_Endian_SwapElf32ProgramHeader(unsigned int *words) {
    words[0] = CTool_EndianConvertWord32(words[0]);
    words[1] = CTool_EndianConvertWord32(words[1]);
    words[2] = CTool_EndianConvertWord32(words[2]);
    words[3] = CTool_EndianConvertWord32(words[3]);
    words[4] = CTool_EndianConvertWord32(words[4]);
    words[5] = CTool_EndianConvertWord32(words[5]);
    words[6] = CTool_EndianConvertWord32(words[6]);
    words[7] = CTool_EndianConvertWord32(words[7]);
}

/* The DWARF 1 line blocks: each a size and an address, then entries of a line, a column and an address delta. */
/* 0x004435e0 */
void swap_conversion_blocks(char *data, int remainingSize, int (*conversionMode)(const char *, int),
                            char readBeforeConversion) {
    unsigned int blockSize;
    unsigned int offset;
    unsigned int *valueAddress;
    short *shortAddress;

    data_0049a9d0 = conversionMode;
    offset = 0;
    while (remainingSize != 0) {
        if (readBeforeConversion != '\0') {
            blockSize = ((ConversionBlockHeader *)(offset + data))->size;
            valueAddress = &((ConversionBlockHeader *)(offset + data))->size;
            CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
        } else {
            valueAddress = &((ConversionBlockHeader *)(offset + data))->size;
            blockSize = CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
        }
        valueAddress = (unsigned int *)(offset + 4 + data);
        CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
        offset = offset + 8;
        remainingSize -= 8;
        do {
            if (readBeforeConversion != '\0') {
                valueAddress = &((ConversionBlockEntry *)(offset + data))->value;
                CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
            } else {
                valueAddress = &((ConversionBlockEntry *)(offset + data))->value;
                CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
            }
            shortAddress = (short *)(offset + 4 + data);
            CTool_EndianConvertInPlaceWord16Ptr(shortAddress);
            valueAddress = (unsigned int *)(offset + 6 + data);
            CTool_EndianConvertInPlaceWord32Ptr(valueAddress);
            offset = offset + 10;
            remainingSize = remainingSize - 10;
        } while (offset < blockSize);
        data = data + offset;
        offset = 0;
    }
}

/* SIZE bytes of words. */
/* 0x004435b0 */
void ELF_Endian_ConvertWords(char *data, unsigned int size) {
    unsigned int offset = 0;

    while (size != 0) {
        CTool_EndianConvertInPlaceWord32Ptr((unsigned int *)(data + offset));
        size -= 4;
        offset += 4;
    }
}

/* The three kinds of the linker's records that ELF_linker.c reads (the kind is the first byte): the words after the
   first of the first kind, a halfword and a word of the second, and a halfword and two words of the third. */
/* 0x00443570 */
void ELF_Endian_ConvertRecord1(LinkerRecord *data) {
    data->word[0] = CTool_EndianConvertWord32(data->word[0]);
    data->word[1] = CTool_EndianConvertWord32(data->word[1]);
    data->word[2] = CTool_EndianConvertWord32(data->word[2]);
    data->word[3] = CTool_EndianConvertWord32(data->word[3]);
}

/* 0x00443540 */
void ELF_Endian_ConvertRecord2(LinkerRecord *data) {
    data->half = CTool_EndianConvertWord16(data->half);
    data->word[0] = CTool_EndianConvertWord32(data->word[0]);
}

/* 0x00443510 */
void ELF_Endian_ConvertRecord3(LinkerRecord *data) {
    data->word[0] = CTool_EndianConvertWord32(data->word[0]);
    data->word[1] = CTool_EndianConvertWord32(data->word[1]);
    data->half = CTool_EndianConvertWord16(data->half);
}
