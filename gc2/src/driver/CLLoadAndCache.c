#include "driver/CLLoadAndCache.h"
#include "driver/CLIncludeFileCache.h"
#include "driver/MacFileTypes.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NONMATCHING
extern int(__stdcall* __OS_ExtendedGetMacFileTypeHook)(const OSSpec*, OSType*);
#endif

short FixTextHandle(Handle txt) {
    char* tptr;
    UInt32 tlen;
    char* scan;

    HLock(txt);
    tptr = *txt;
    tlen = GetHandleSize(txt);

    scan = tptr;
    while (scan < (tptr + tlen)) {
        if (scan[0] == '\r') {
            if (scan[1] == '\n') {
                scan += 2;
            } else {
                scan += 1;
            }
        } else if (scan[0] == '\n') {
            scan[0] = '\r';
            scan++;
        } else {
            scan++;
        }
    }

    HUnlock(txt);
    return 0;
}

int LoadAndCacheFile(OSSpec* spec, Handle* texthandle, Boolean* precomp) {
    Handle h;
    int err;
    OSType mactype;
    int refnum;
    UInt32 ltxtlen;

    if ((*texthandle = CachedIncludeFile(spec, precomp))) {
        return 0;
    }

#ifdef NONMATCHING
    *precomp = FALSE;
#else
    *precomp = OS_GetMacFileType(spec, &mactype) == 0 && mactype != 'TEXT';
#endif
    *texthandle = NULL;

    err = OS_Open(spec, OSReadOnly, &refnum);
    if (err) {
        return err;
    }
    err = OS_GetSize(refnum, &ltxtlen);
    if (err) {
        return err;
    }

    h = NewHandle(ltxtlen + 1);
    if (!h) {
        fprintf(stderr, "\n*** Out of memory\n");
        exit(-23);
    }

    HLock(h);
    err = OS_Read(refnum, *h, &ltxtlen);
    if (err) {
        DisposeHandle(h);
        OS_Close(refnum);
        return err;
    }

    OS_Close(refnum);
#ifdef NONMATCHING
    /* Cuyler36's GC 2.6f1 (mweppc 3f63464): the type from the first 32 bytes loaded, which OS_GetMacFileType
       would open the file again to read; it still runs for an empty file, or when a hook can answer. */
    mactype = 'TEXT';
    if (!ltxtlen || !OS_GetMacFileTypeMagic(*h, ltxtlen < 32 ? ltxtlen : 32, &mactype)) {
        mactype = 'TEXT';
        if (__OS_ExtendedGetMacFileTypeHook || !ltxtlen) {
            if (OS_GetMacFileType(spec, &mactype)) {
                mactype = 'TEXT';
            }
        }
    }
    *precomp = mactype != 'TEXT';
#endif
    (*h)[ltxtlen] = 0;
    HUnlock(h);

    if (!*precomp) {
        FixTextHandle(h);
    }

    CacheIncludeFile(spec, h, *precomp);
    *texthandle = h;
    return 0;
}

void CopyFileText(Handle src, char** text, SInt32* textsize) {
    *textsize = GetHandleSize(src);
    *text = xmalloc(NULL, *textsize);

    HLock(src);
    memcpy(*text, *src, *textsize);
    *textsize -= 1;
    HUnlock(src);
}
