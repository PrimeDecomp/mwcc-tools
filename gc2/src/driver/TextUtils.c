#include "driver/TextUtils.h"
#include "driver/Memory.h"
#include "driver/MsDos.h"
#include "driver/Resources.h"
#include "driver/ResourceStrings.h"
#include "driver/StringUtils.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

StringPtr c2pstr(char* s) {
    unsigned int l;

    l = strlen(s);
    if (l > 255) {
        l = 255;
    }
    memmove(s + 1, s, l);
    s[0] = l;
    return (StringPtr)s;
}

char* __stdcall p2cstr(StringPtr s) {
    unsigned int l = s[0];
    memmove(s, s + 1, l);
    s[l] = 0;
    return (char*)s;
}

// (dead in 2.6: the linker dropped it)
SInt16 CharacterByteType(char* textBuf, SInt16 textOffset, ScriptCode script) {
    return 0;
}

// (dead in 2.6: the linker dropped it)
SInt16 CharByte(void) {
    return 0;
}

// (dead in 2.6: the linker dropped it)
void GetDateTime(UInt32* secs) {
    OSTime time;
    OS_GetTime(&time);
    OS_TimeToMac(time, secs);
}

// (dead in 2.6: the linker dropped it)
Boolean EqualString(ConstStringPtr strA, ConstStringPtr strB, Boolean caseSens, Boolean diacSens) {
    int i, length;
    Boolean equal;

    equal = FALSE;
    length = strA[0];
    if (strA && strB && length == strB[0]) {
        if (caseSens) {
            equal = !memcmp(strA + 1, strB + 1, length);
        } else {
            equal = TRUE;
            i = 0;
            while (equal && i < length) {
                equal = toupper(strA[i]) == toupper(strB[i]);
                ++i;
            }
        }
    }

    return equal;
}

// (2.6's locals: declared in this order, with a copy of the locked handle's address for the
// bound, where Pro7 keeps the end in dptr; Pro7's order and its end pointer compile otherwise.
// The ellipsis' third dot is stored after the advance, as the code has it.)
void __stdcall GetIndString(StringPtr theString, SInt16 strListID, SInt16 index) {
    StringPtr ptr;
    StringPtr strings;
    SInt16 idc;
    const char* ret;
    Handle strs;
    SInt16 last;
    StringPtr dptr;
    SInt32 len;
    unsigned char tmp[256];

    ret = Res_GetResource(strListID, index);
    if (ret) {
        strcpy((char*)tmp, ret);
        c2pstr((char*)tmp);
    } else {
        sprintf((char*)tmp, "[Resource string id=%d index=%d not found]", strListID, index);
        c2pstr((char*)tmp);
        strs = GetResource('STR#', strListID);
        if (strs) {
            last = (((unsigned char)(*strs)[0]) << 8) + ((unsigned char)(*strs)[1]);
            if (index > 0 && index <= last) {
                len = GetHandleSize(strs);
                HLock(strs);
                strings = (StringPtr)*strs;
                ptr = strings + 2;
                idc = index;
                while (ptr < strings + len && --idc) {
                    ptr += *ptr + 1;
                }
                if (ptr < strings + len) {
                    _pstrcpy(tmp, ptr);
                }
                HUnlock(strs);
            }
        }
    }
    ptr = tmp + 1;
    dptr = theString + 1;
    while (ptr <= tmp + tmp[0]) {
        if (*ptr == 0xD4) {
            *dptr = '`';
        } else if (*ptr == 0xD5) {
            *dptr = '\'';
        } else if (*ptr == 0xD2 || *ptr == 0xD3) {
            *dptr = '"';
        } else if (*ptr == 0xC9 && dptr - theString < 253) {
            *dptr = '.';
            dptr[1] = '.';
            dptr += 2;
            *dptr = '.';
        } else {
            *dptr = *ptr;
        }
        ++ptr;
        ++dptr;
    }
    *theString = dptr - theString - 1;
}

char* __stdcall getindstring(char* theString, SInt16 strListID, SInt16 index) {
    GetIndString((StringPtr)theString, strListID, index);
    return p2cstr((StringPtr)theString);
}

// (dead in 2.6: the linker dropped it, but its "%d" is in the unit's .data)
void NumToString(SInt32 theNum, StringPtr theString) {
    sprintf((char*)theString, "%d", theNum);
    c2pstr((char*)theString);
}

// (dead in 2.6: the linker dropped it)
void StringToNum(ConstStringPtr theString, SInt32* theNum) {
    p2cstr((StringPtr)theString);
    sscanf((char*)theString, "%d", theNum);
    c2pstr((char*)theString);
}
