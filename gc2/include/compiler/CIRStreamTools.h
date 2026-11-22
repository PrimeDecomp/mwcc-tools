#ifndef COMPILER_CIRSTREAMTOOLS_H
#define COMPILER_CIRSTREAMTOOLS_H

#include "compiler/common.h"

typedef struct CIRStreamReader {
    const UInt8* bytes;
    UInt32 size;
} CIRStreamReader;

typedef struct CIRStreamWriter {
    // A null buffer measures the encoded size by consuming capacity.
    UInt8* bytes;
    UInt32 size;
} CIRStreamWriter;

UInt32 CIRStream_ReadWord(const UInt8* bytes);
void CIRStream_WriteWord(UInt8 bytes[4], UInt32 value);
UInt32 CIRStream_PackUInt32(UInt8 bytes[5], UInt32 value);
UInt32 CIRStream_PackInt32(UInt8 bytes[5], SInt32 value);
Boolean CIRStream_PackData(CIRStreamWriter* stream, const UInt8* data, UInt32 size);
Boolean CIRStream_PackByte(CIRStreamWriter* stream, UInt8 value);
Boolean CIRStream_PackUInt16(CIRStreamWriter* stream, UInt16 value);
Boolean CIRStream_WriteUInt32(CIRStreamWriter* stream, UInt32 value);
Boolean CIRStream_WriteInt32(CIRStreamWriter* stream, SInt32 value);
Boolean CIRStream_UnpackByte(CIRStreamReader* stream, UInt8* value);
Boolean CIRStream_UnpackUInt16(CIRStreamReader* stream, UInt16* value);
Boolean CIRStream_UnpackUInt32(CIRStreamReader* stream, UInt32* value);
Boolean CIRStream_UnpackInt32(CIRStreamReader* stream, SInt32* value);
Boolean CIRStream_UnpackData(CIRStreamReader* stream, UInt32 size, CIRStreamReader* data);
Boolean CIRStream_UnpackTypeHashNameNode(CIRStreamReader* stream, CIRStreamReader* name);

#endif
