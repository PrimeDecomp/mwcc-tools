#ifndef COMPILER_BE_ELF_H
#define COMPILER_BE_ELF_H

#include "compiler/common.h"
#include "compiler/CompilerTools.h"

#ifdef __cplusplus
extern "C" {
#endif

// What a section is declared with (Initialize_Input; ObjGen's `#pragma section` and
// __declspec(section) records): 16 bytes (galloc(0x10) in Wii's elf_define_section)
struct SectionRec {
    // `#pragma section`'s options as given (parse_section_pragma)
    UInt8 flags;
    // which of the default object sections (copts.textSection ...) it becomes: 1 code, 2 data,
    // 4 const, 8 sdata, 0x10 sconst (InitElfSection)
    UInt8 typebits;
    // its data_mode and code_mode (AddressingMode)
    UInt8 dataMode;
    UInt8 codeMode;
    // the R/W/X attributes parse_section_pragma accumulates (2.6 ORs and reads all four bytes)
    UInt32 mode;
    char* sectionName;
    // the uninitialized section paired with it (.bss for .data), or NULL
    char* linkedSectionName;
};
// The object file being written: its sections by kind and its counters, cleared by InitElfFile
// (Wii's COMMON `ELFfile`). TODO: invented type and member names, except those of the globals the
// members were split into before (section_list, dwarf_info_section ...)
// (2-byte packing: file_symbol at offset 2, after the Boolean)
#pragma pack(push, 2)
typedef struct ElfFile {
    // set once ObjGen has set up the DWARF sections (Wii: the global `debug_sections_inited`)
    Boolean debug_sections_inited;
    // the file's STT_FILE symbol (InitElfFile)
    BE_SymNode* file_symbol;
    SInt16 sectionHeaderCount;
    ObjGenSection* section_list;
    // the sections InitElfSection files by kind
    ObjGenSection* null_section;
    ObjGenSection* ctors_section;
    ObjGenSection* dtors_section;
    ObjGenSection* extab_section;
    ObjGenSection* extabindex_section;
    // .data's linked .bss (InitElfSections)
    ObjGenSection* data_section_linked_symbol;
    ObjGenSection* dwarf_info_section;
    ObjGenSection* dwarf_line_section;
    ObjGenSection* rela_sections;
    ObjGenSection* symtab_section;
    ObjGenSection* symbol_string_table_section;
    ObjGenSection* shstrtab_section;
    ObjGenSection* comment_section;
    // SHN_COMMON and SHN_ABS stand-ins (kinds 16 and 17)
    ObjGenSection* common_section;
    ObjGenSection* abs_section;
    // TODO: placeholder name: nothing in 2.6 reads or writes this word
    UInt32 unused48;
    SectionSymbolAttributes* section_symbol_attributes;
    UInt16 section_symbol_attribute_count;
} ElfFile;
#pragma pack(pop)

extern ElfFile ELFfile;
extern GList elfmodule;
extern ObjGenRelocation* allrelocations;
extern SInt32 previous_ps_ID;
extern InterruptGenerationRecord* interrupt_generation_records;

extern void BackPatchRelocationAddend(
    ObjGenRelocation* reloc, unsigned int value, BE_SymNode* symbol
);
extern ObjGenRelocation* WriteDebugRelocation(
    ObjGenSection* context, int offset, BE_SymNode* symbol, void* source, int value, int adjustment
);
extern void BackEnd_WriteElf(void);
extern void TransferSectionData(GList* dst, GList* src);
extern void CleanupElfSections(void);
extern void InitElfFile(void);
extern void DoParseDeclSpec(char* name, DeclInfo* declinfo);
extern void DoElfSystemSection(char* name, UInt8 kind, SInt8 align, SInt32 initialSize);
extern ObjGenSection* CloneSection(ObjGenSection* input, SInt32 context, UInt8 flags);
extern SectionRec* Initialize_Input(
    char* name, char* linkedName, UInt8 dataMode, UInt8 codeMode, UInt8 typebits
);
extern ObjGenSection* DoElfContentSection(
    SectionRec* rec,
    short kind,
    SInt8 align,
    SInt32 size,
    int relocationSize,
    UInt32 permissions,
    char allocateData,
    char createEntry
);
extern ObjGenSection* GetSectionByName(const char* name, SInt32 context, UInt8 flags);
extern ObjGenSection* InitElfSection(
    char* name,
    UInt8 kind,
    SInt8 align,
    SInt32 initialSize,
    SectionRec* rec,
    UInt32 permissions,
    struct ObjGenSymbolLink* symbolLink
);
extern SectionSymbolAttributes* Initialize_Specific(
    ObjGenSection* section,
    UInt8 dataMode,
    UInt8 codeMode,
    ObjGenSection* linked,
    ObjGenSection* auxiliary
);
extern void SectionAlign(GList* list, SInt32 alignment);

#ifdef __cplusplus
}
#endif

#endif
