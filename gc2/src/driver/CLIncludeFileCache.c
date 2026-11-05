#include "driver/CLIncludeFileCache.h"
#include "driver/Memory.h"
#include "driver/MemUtils.h"
#include "driver/MsDos.h"
#include "driver/oslib.h"

typedef struct CacheEntry {
    struct CacheEntry* next;
    struct CacheEntry* prev;
    int locked;
    int precompiled;
    Handle text;
    OSSpec spec;
    UInt32 size;
    UInt32 when;
} CacheEntry;

enum {
    CACHE_SIZE = 0x800000
};

static CacheEntry* cachelist;
static CacheEntry* freelist;
static UInt32 availablecache;

#ifdef NONMATCHING
/* Cuyler36's GC 2.6f1 (mweppc 3f63464): the cache's entries indexed by spec (open addressing), beside the list,
   which keeps its order and allocation. A key with several entries finds the newest, as the list's scan does;
   once the table is full (fastIncludeComplete false), a miss scans the list. */
#include <ctype.h>
#include <string.h>
enum { FAST_INCLUDE_SIZE = 4096 };
static CacheEntry* fastInclude[FAST_INCLUDE_SIZE];
static Boolean fastIncludeComplete;
#define FAST_INCLUDE_DELETED ((CacheEntry*)1)

/* OS_EqualSpec compares without case, so the hash does too. */
static UInt32 FastIncludeHash(const OSSpec* spec) {
    const char* p;
    UInt32 hash = 2166136261u;
    int component;
    int ch;

    for (component = 0; component < 2; component++) {
        p = component ? spec->name.s : spec->path.s;
        while (*p) {
            ch = *p++;
            if (ch >= 0 && ch < 128) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch += 'a' - 'A';
                }
            } else {
                ch = tolower(ch);
            }
            hash = (hash ^ (unsigned char)ch) * 16777619u;
        }
        hash = (hash ^ 0xffu) * 16777619u;
    }
    return hash;
}

static CacheEntry* FastIncludeFind(const OSSpec* spec) {
    UInt32 slot = FastIncludeHash(spec) & (FAST_INCLUDE_SIZE - 1);
    UInt32 i;
    CacheEntry* c;

    for (i = 0; i < FAST_INCLUDE_SIZE; i++, slot = (slot + 1) & (FAST_INCLUDE_SIZE - 1)) {
        c = fastInclude[slot];
        if (!c) {
            return NULL;
        }
        if (c != FAST_INCLUDE_DELETED && OS_EqualSpec(&c->spec, spec)) {
            return c;
        }
    }
    return NULL;
}

static void FastIncludeInsert(CacheEntry* entry) {
    UInt32 slot = FastIncludeHash(&entry->spec) & (FAST_INCLUDE_SIZE - 1);
    UInt32 deleted = FAST_INCLUDE_SIZE;
    UInt32 i;
    CacheEntry* c;

    for (i = 0; i < FAST_INCLUDE_SIZE; i++, slot = (slot + 1) & (FAST_INCLUDE_SIZE - 1)) {
        c = fastInclude[slot];
        if (c == FAST_INCLUDE_DELETED) {
            if (deleted == FAST_INCLUDE_SIZE) {
                deleted = slot;
            }
        } else if (!c) {
            fastInclude[deleted != FAST_INCLUDE_SIZE ? deleted : slot] = entry;
            return;
        } else if (OS_EqualSpec(&c->spec, &entry->spec)) {
            fastInclude[slot] = entry;
            return;
        }
    }
    if (deleted != FAST_INCLUDE_SIZE) {
        fastInclude[deleted] = entry;
    } else {
        fastIncludeComplete = FALSE;
    }
}

static void FastIncludeRemove(CacheEntry* entry) {
    UInt32 slot = FastIncludeHash(&entry->spec) & (FAST_INCLUDE_SIZE - 1);
    UInt32 i;
    CacheEntry* c;
    CacheEntry* other;

    for (i = 0; i < FAST_INCLUDE_SIZE; i++, slot = (slot + 1) & (FAST_INCLUDE_SIZE - 1)) {
        c = fastInclude[slot];
        if (!c) {
            return;
        }
        if (c == entry) {
            fastInclude[slot] = FAST_INCLUDE_DELETED;
            /* the next newest entry of the same spec, if any, as the list's scan would find it */
            for (other = cachelist; other; other = other->next) {
                if (other != entry && OS_EqualSpec(&other->spec, &entry->spec)) {
                    fastInclude[slot] = other;
                    break;
                }
            }
            return;
        }
    }
}
#endif

static CacheEntry* makecacheentry(void) {
    CacheEntry* c;

    if (freelist) {
        c = freelist;
        freelist = c->next;
    } else {
        c = xmalloc("include file cache", sizeof(CacheEntry));
    }
    return c;
}

static void insertcacheentry(CacheEntry* c) {
    if (cachelist) {
        cachelist->prev = c;
    }
    c->next = cachelist;
    c->prev = NULL;
    cachelist = c;
#ifdef NONMATCHING
    FastIncludeInsert(c);
#endif
}

static void deletecacheentry(CacheEntry* c) {
#ifdef NONMATCHING
    FastIncludeRemove(c);
#endif
    if (c->next) {
        c->next->prev = c->prev;
    }
    if (c->prev) {
        c->prev->next = c->next;
    } else {
        cachelist = c->next;
    }
}

void InitializeIncludeCache(void) {
#ifdef NONMATCHING
    memset(fastInclude, 0, sizeof(fastInclude));
    fastIncludeComplete = TRUE;
#endif
    cachelist = freelist = NULL;
    availablecache = CACHE_SIZE;
}

void CleanupIncludeCache(void) {
    CacheEntry* c;
    CacheEntry* c1;

    for (c = cachelist; c; c = c1) {
        c1 = c->next;
        if (c->text) {
            DisposeHandle(c->text);
        }
        xfree(c);
    }

    for (c = freelist; c; c = c1) {
        c1 = c->next;
        xfree(c);
    }

    InitializeIncludeCache();
}

void CacheIncludeFile(OSSpec* spec, Handle text, Boolean precompiled) {
    SInt32 size;
    CacheEntry* c;
    CacheEntry* lru;

    size = GetHandleSize(text);
    if (precompiled) {
        lru = NULL;
        for (c = cachelist; c; c = c->next) {
            if (c->precompiled) {
                lru = c;
                break;
            }
        }
        if (lru) {
            DisposeHandle(lru->text);
            lru->text = NULL;
            deletecacheentry(lru);
            lru->next = freelist;
            freelist = lru;
        }
    } else {
        if ((SInt32)size > (SInt32)CACHE_SIZE) {
            return;
        } else {
            while ((UInt32)size > availablecache) {
                lru = NULL;
                for (c = cachelist; c; c = c->next) {
                    if (!c->locked && !c->precompiled && (lru == NULL || c->when < lru->when)) {
                        lru = c;
                    }
                }

                if (lru) {
                    DisposeHandle(lru->text);
                    lru->text = NULL;
                    availablecache += lru->size;
                    deletecacheentry(lru);
                    lru->next = freelist;
                    freelist = lru;
                } else {
                    return;
                }
            }
        }
    }

    c = makecacheentry();
    c->locked = 1;
    c->precompiled = precompiled;
    c->spec = *spec;
    c->text = text;
    c->size = size;
    c->when = OS_GetMilliseconds();
    insertcacheentry(c);

    if (!precompiled) {
        availablecache -= size;
    }
}

Handle CachedIncludeFile(OSSpec* spec, Boolean* precompiled) {
    CacheEntry* c;

#ifdef NONMATCHING
    c = FastIncludeFind(spec);
    if (!c && !fastIncludeComplete) {
        for (c = cachelist; c; c = c->next) {
            if (OS_EqualSpec(&c->spec, spec)) {
                break;
            }
        }
    }
    if (c) {
        c->when = OS_GetMilliseconds();
        *precompiled = c->precompiled;
        c->locked++;
        return c->text;
    }
    return NULL;
#endif
    for (c = cachelist; c; c = c->next) {
        if (OS_EqualSpec(&c->spec, spec)) {
            c->when = OS_GetMilliseconds();
            *precompiled = c->precompiled;
            c->locked++;
            return c->text;
        }
    }

    return NULL;
}

void FreeIncludeFile(Handle text) {
    CacheEntry* c;

    for (c = cachelist; c; c = c->next) {
        if (c->text == text) {
            c->locked--;
            return;
        }
    }

    DisposeHandle(text);
}
