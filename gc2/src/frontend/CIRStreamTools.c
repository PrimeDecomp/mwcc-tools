#include "compiler/CIRStreamTools.h"

UInt32 CIRStream_ReadWord(const UInt8* bytes) {
    return (UInt32)bytes[0] | ((UInt32)bytes[1] << 8) | ((UInt32)bytes[2] << 16) |
        ((UInt32)bytes[3] << 24);
}

void CIRStream_WriteWord(UInt8 bytes[4], UInt32 value) {
    bytes[0] = (UInt8)value;
    bytes[1] = (UInt8)(value >> 8);
    bytes[2] = (UInt8)(value >> 16);
    bytes[3] = (UInt8)(value >> 24);
}

static UInt32 pack_extended(UInt8 bytes[5], UInt8 marker, UInt32 value) {
    bytes[0] = marker;
    CIRStream_WriteWord(bytes + 1, value);
    return 5;
}

UInt32 CIRStream_PackUInt32(UInt8 bytes[5], UInt32 value) {
    if (value >= 0xff) {
        return pack_extended(bytes, 0xff, value);
    }
    bytes[0] = (UInt8)value;
    return 1;
}

UInt32 CIRStream_PackInt32(UInt8 bytes[5], SInt32 value) {
    if (value < -127 || value > 127) {
        return pack_extended(bytes, 0x80, (UInt32)value);
    }
    bytes[0] = (UInt8)value;
    return 1;
}

Boolean CIRStream_UnpackUInt32(CIRStreamReader* stream, UInt32* value) {
    UInt32 size;

    if (!stream->size) {
        return FALSE;
    }
    size = stream->bytes[0] == 0xff ? 5 : 1;
    if (stream->size < size) {
        return FALSE;
    }
    *value = size == 5 ? CIRStream_ReadWord(stream->bytes + 1) : stream->bytes[0];
    stream->bytes += size;
    stream->size -= size;
    return TRUE;
}

Boolean CIRStream_UnpackInt32(CIRStreamReader* stream, SInt32* value) {
    UInt32 size;

    if (!stream->size) {
        return FALSE;
    }
    size = stream->bytes[0] == 0x80 ? 5 : 1;
    if (stream->size < size) {
        return FALSE;
    }
    *value = size == 5 ? (SInt32)CIRStream_ReadWord(stream->bytes + 1) : (SInt8)stream->bytes[0];
    stream->bytes += size;
    stream->size -= size;
    return TRUE;
}

Boolean CIRStream_UnpackData(CIRStreamReader* stream, UInt32 size, CIRStreamReader* data) {
    if (size > stream->size) {
        return FALSE;
    }
    if (data) {
        data->bytes = stream->bytes;
        data->size = size;
    }
    if (size) {
        stream->bytes += size;
        stream->size -= size;
    }
    return TRUE;
}

Boolean CIRStream_UnpackByte(CIRStreamReader* stream, UInt8* value) {
    CIRStreamReader bytes;

    if (!CIRStream_UnpackData(stream, 1, &bytes)) {
        return FALSE;
    }
    *value = bytes.bytes[0];
    return TRUE;
}

Boolean CIRStream_UnpackUInt16(CIRStreamReader* stream, UInt16* value) {
    CIRStreamReader bytes;

    if (!CIRStream_UnpackData(stream, 2, &bytes)) {
        return FALSE;
    }
    *value = (UInt16)(bytes.bytes[0] | ((UInt16)bytes.bytes[1] << 8));
    return TRUE;
}

Boolean CIRStream_UnpackTypeHashNameNode(CIRStreamReader* stream, CIRStreamReader* name) {
    UInt32 size;

    if (!CIRStream_UnpackUInt32(stream, &size)) {
        return FALSE;
    }
    // The length includes an implicit terminator. A null name is encoded as length zero.
    return CIRStream_UnpackData(stream, size ? size - 1 : 0, name);
}

Boolean CIRStream_PackData(CIRStreamWriter* stream, const UInt8* data, UInt32 size) {
    UInt32 index;

    if (size > stream->size) {
        return FALSE;
    }
    if (stream->bytes) {
        for (index = 0; index < size; ++index) {
            stream->bytes[index] = data[index];
        }
        stream->bytes += size;
    }
    stream->size -= size;
    return TRUE;
}

Boolean CIRStream_PackByte(CIRStreamWriter* stream, UInt8 value) {
    return CIRStream_PackData(stream, &value, 1);
}

Boolean CIRStream_PackUInt16(CIRStreamWriter* stream, UInt16 value) {
    UInt8 bytes[2] = {(UInt8)value, (UInt8)(value >> 8)};
    return CIRStream_PackData(stream, bytes, sizeof(bytes));
}

Boolean CIRStream_WriteUInt32(CIRStreamWriter* stream, UInt32 value) {
    UInt8 bytes[5];
    UInt32 size = CIRStream_PackUInt32(bytes, value);
    return CIRStream_PackData(stream, bytes, size);
}

Boolean CIRStream_WriteInt32(CIRStreamWriter* stream, SInt32 value) {
    UInt8 bytes[5];
    UInt32 size = CIRStream_PackInt32(bytes, value);
    return CIRStream_PackData(stream, bytes, size);
}
