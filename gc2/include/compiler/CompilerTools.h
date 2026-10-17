#ifndef COMPILER_COMPILERTOOLS_H
#define COMPILER_COMPILERTOOLS_H

#include <setjmp.h>
#include "compiler/common.h"
#include "compiler/types.h"
#include "compiler/win32.h"

struct FSSpec;

#ifdef __cplusplus
extern "C" {
#endif

extern void CompilerGetCString(short index, char* string);
extern unsigned char* CTool_CtoPstr(char* cstr);

typedef struct HeapBlock {
    struct HeapBlock* next;
    Handle blockhandle;
    SInt32 blocksize;
    SInt32 blockfree;
} HeapBlock;

typedef struct HeapMem {
    HeapBlock* blocks;
    // the minimum size for a new block
    SInt32 allocsize;
    HeapBlock* curblock;
    char* curfreep;
    SInt32 curfree;
} HeapMem;

typedef struct HeapInfo {
    // the number of blocks in the heap
    SInt32 blocks;
    // the heap's allocsize
    SInt32 allocsize;
    // the bytes of all its blocks
    SInt32 total_size;
    // the free bytes of all its blocks
    SInt32 total_free;
    SInt32 average_block_size;
    SInt32 average_block_free;
    // the largest contiguous free space
    SInt32 largest_free_block;
} HeapInfo;

#pragma pack(push, 2)
struct GList {
    char** data;
    SInt32 size;
    SInt32 hndlsize;
    SInt32 growsize;
};
#pragma pack(pop)

extern long hash_name_id;
extern HashNameNode** name_hash_nodes;
extern void (*GListErrorProc)(void);

extern short InitGList(GList* gl, SInt32 size);
extern void FreeGList(GList* gl);
extern void LockGList(GList* gl);
extern void UnlockGList(GList* gl);
extern void ShrinkGList(GList* gl);
extern void AppendGListData(GList* gl, const void* data, SInt32 size);
extern void AppendGListNoData(GList* gl, SInt32 size);
extern void AppendGListByte(GList* gl, SInt8 thebyte);
extern void AppendGListWord(GList* gl, SInt16 theword);
extern void AppendGListTargetEndianWord(GList* gl, UInt16 theword);
extern void AppendGListLong(GList* gl, SInt32 theword);
extern void AppendGListTargetEndianLong(GList* gl, UInt32 theword);
extern void AppendGListID(GList* gl, const char* name);
extern void AppendGListName(GList* gl, const char* name);
extern void RemoveGListData(GList* gl, SInt32 size);
extern SInt16 GetGListByte(GList* gl);
extern SInt16 GetGListWord(GList* gl);
extern SInt32 GetGListLong(GList* gl);
extern short GetGListID(GList* gl, char* name);
extern void GetGListData(GList* gl, char* where, SInt32 size);

extern SInt16 CHash(const char* string);
extern HashNameNode* GetHashNameNode(const char* name);
extern HashNameNode* GetHashNameNodeHash(const char* name, SInt16 hashval);
extern HashNameNode* GetHashNameNodeHash2(const char* name, SInt16 hashval);
extern HashNameNode* GetHashNameNodeExport(const char* name);
extern SInt32 GetHashNameNodeExportID(HashNameNode* node);
extern HashNameNode* GetHashNameNodeByID(SInt32 id);
extern void NameHashExportReset(void);
extern void NameHashWriteNameTable(GList* glist);
extern void NameHashWriteTargetEndianNameTable(GList* glist);
extern void InitNameHash(void);

typedef void (*heaperror_t)(void);

extern SInt32 CTool_TotalHeapSize(void);
extern void CTool_GetHeapInfo(HeapInfo* result, unsigned char heapID);
extern short initheaps(heaperror_t heaperrorproc);
extern short initgheap(heaperror_t heaperrorproc);
extern heaperror_t getheaperror(void);
extern void setheaperror(heaperror_t heaperrorproc);
extern void releaseheaps(void);
extern void releasegheap(void);
extern void releaseoheap(void);
extern void* galloc(SInt32 s);
extern void* lalloc(SInt32 s);
extern void* aalloc(SInt32 s);
extern void* oalloc(SInt32 s);
extern void* balloc(SInt32 s);

extern void locklheap(void);
extern void unlocklheap(void);
extern void freelheap(void);
extern void freeaheap(void);
extern void freeoheap(void);
extern void freebheap(void);

extern char* ScanHex(char* string, UInt32* result, Boolean* overflow);
extern char* ScanOct(char* string, UInt32* result, Boolean* overflow);
extern char* ScanDec(char* string, UInt32* result, Boolean* overflow);

extern void OldUnmangle(char* name, char* out, Boolean full);

extern short hash(char* a);
extern void memclr(void* ptr, SInt32 size);
extern void memclrw(void* ptr, SInt32 size);
extern void CToLowercase(char* a, char* b);
extern short getbit(SInt32 l);
extern UInt16 CTool_EndianConvertWord16(UInt16 theword);
extern UInt32 CTool_EndianConvertWord32(UInt32 theword);
// (Pro7: UInt8*; 2.6's ELF_Endian passes a UInt8** to it without a cast, so 2.6 takes void*)
extern void CTool_EndianConvertMem(void* data, short len);
// (the host's read of a word in target order; 2.6 inlines it)
static inline UInt32 CTool_EndianReadWord32(void* ptr) {
    return *(UInt32*)ptr;
}
extern void CTool_EndianConvertWord64(CInt64 ci, char* result);
extern UInt16 CTool_EndianConvertInPlaceWord16Ptr(UInt16* x);
extern UInt32 CTool_EndianConvertInPlaceWord32Ptr(UInt32* x);
extern void CTool_EndianConvertVector128();
extern HashNameNode* CTool_GetPathName(const struct FSSpec* fss, SInt32* moddateptr);
extern int strcat_safe(char* dest, const char* src, SInt32 len);

#ifdef __cplusplus
}
#endif

#endif
