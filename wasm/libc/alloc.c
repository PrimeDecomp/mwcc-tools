/* malloc, free, realloc and calloc: MSL's pool allocator as Pro 5.3's Win32 configuration has it, which the compiler
 * depends on (what it reads of memory it never wrote is what an earlier, freed allocation left there). Adapted from
 * the Echoes decompilation's alloc.c (a later MSL, the same variable-size pools), with Pro 5.3's choices: blocks from
 * the system in 4 KiB pages, at least 64 KiB, and four fixed-size pools (12, 20, 36 and 68 bytes) of about 4000 bytes
 * each, which give their whole block back once none of its pieces is in use. All returned memory is 8-aligned. */
#include "libc.h"

/* A block from the system: a ring of blocks, each with its ring of free sub-blocks. The block's last word points to
 * that ring's start; the word before it repeats the size. */
typedef struct Block {
    struct Block* prev;
    struct Block* next;
    unsigned long max_size;
    unsigned long size;
} Block;

/* A piece of a block: its size and flags (2 allocated, 4 the previous piece is allocated), and its block (with 1
 * set, telling a variable-size piece from a fixed-size one). A free piece links to its neighbours in the ring and
 * repeats its size in its last word. */
typedef struct SubBlock {
    unsigned long size;
    Block* block;
    struct SubBlock* prev;
    struct SubBlock* next;
} SubBlock;

#define BLOCK_MIN_SIZE 65536
#define PAGE_SIZE 4096
#define BLOCK_OVERHEAD 24
#define BLOCK_HEADER 16
#define BLOCK_TRAILER 8
#define SUB_OVERHEAD 8
/* the smallest piece worth splitting off: more than the largest fixed-size pool's request */
#define SUB_MIN_SIZE ((SUB_OVERHEAD + 68 + 1 + 7) & ~7)

#define SIZE_MASK 0xFFFFFFF8
#define VARIABLE 1
#define ALLOCATED 2
#define PREV_ALLOCATED 4

#define SubBlock_size(sb) ((sb)->size & SIZE_MASK)
#define SubBlock_block(sb) ((Block*)((unsigned long)(sb)->block & ~VARIABLE))
#define SubBlock_is_free(sb) (!((sb)->size & ALLOCATED))
#define SubBlock_from_pointer(ptr) ((SubBlock*)((char*)(ptr) - SUB_OVERHEAD))
#define Block_size(b) ((b)->size & SIZE_MASK)
#define Block_start(b) (*(SubBlock**)((char*)(b) + Block_size(b) - sizeof(unsigned long)))

static void SubBlock_set_size(SubBlock* sb, unsigned long size)
{
    sb->size &= ~SIZE_MASK;
    sb->size |= size & SIZE_MASK;
    if (SubBlock_is_free(sb)) {
        *(unsigned long*)((char*)sb + size - sizeof(unsigned long)) = size;
    }
}

static void SubBlock_construct(SubBlock* sb, unsigned long size, Block* block, int prev_allocated, int allocated)
{
    sb->block = (Block*)((unsigned long)block | VARIABLE);
    sb->size = size;
    if (prev_allocated) {
        sb->size |= PREV_ALLOCATED;
    }
    if (allocated) {
        sb->size |= ALLOCATED;
        *(unsigned long*)((char*)sb + size) |= PREV_ALLOCATED;
    } else {
        *(unsigned long*)((char*)sb + size - sizeof(unsigned long)) = size;
    }
}

/* Splits SB at SIZE; a free SB's second half follows it in the ring. Returns the second half. */
static SubBlock* SubBlock_split(SubBlock* sb, unsigned long size)
{
    unsigned long old_size = SubBlock_size(sb);
    int is_free = SubBlock_is_free(sb);
    int prev_allocated = sb->size & PREV_ALLOCATED;
    SubBlock* next = (SubBlock*)((char*)sb + size);
    Block* block = SubBlock_block(sb);

    SubBlock_construct(sb, size, block, prev_allocated, !is_free);
    SubBlock_construct(next, old_size - size, block, !is_free, !is_free);
    if (is_free) {
        next->next = sb->next;
        next->next->prev = next;
        next->prev = sb;
        sb->next = next;
    }
    return next;
}

/* Merges a free SB with a free piece before it (which then stands for both). */
static SubBlock* SubBlock_merge_prev(SubBlock* sb, SubBlock** start)
{
    unsigned long prev_size;
    SubBlock* prev;

    if (sb->size & PREV_ALLOCATED) {
        return sb;
    }
    prev_size = *(unsigned long*)((char*)sb - sizeof(unsigned long));
    if (prev_size & ALLOCATED) {
        return sb;
    }
    prev = (SubBlock*)((char*)sb - prev_size);
    SubBlock_set_size(prev, prev_size + SubBlock_size(sb));
    if (*start == sb) {
        *start = (*start)->next;
    }
    sb->next->prev = sb->prev;
    sb->next->prev->next = sb->next;
    return prev;
}

/* Merges SB with a free piece after it. */
static void SubBlock_merge_next(SubBlock* sb, SubBlock** start)
{
    SubBlock* next = (SubBlock*)((char*)sb + SubBlock_size(sb));
    unsigned long size;

    if (!SubBlock_is_free(next)) {
        return;
    }
    size = SubBlock_size(sb) + SubBlock_size(next);
    SubBlock_set_size(sb, size);
    if (SubBlock_is_free(sb)) {
        *(unsigned long*)((char*)sb + size) &= ~PREV_ALLOCATED;
    } else {
        *(unsigned long*)((char*)sb + size) |= PREV_ALLOCATED;
    }
    if (*start == next) {
        *start = (*start)->next;
    }
    if (*start == next) {
        *start = 0;
    }
    next->next->prev = next->prev;
    next->prev->next = next->next;
}

/* Frees SB into BLOCK's ring, at its start, merging it with free neighbours. */
static void Block_link(Block* block, SubBlock* sb)
{
    SubBlock** start;
    unsigned long size = SubBlock_size(sb);

    sb->size &= ~ALLOCATED;
    *(unsigned long*)((char*)sb + size) &= ~PREV_ALLOCATED;
    *(unsigned long*)((char*)sb + size - sizeof(unsigned long)) = size;
    start = &Block_start(block);
    if (*start != 0) {
        sb->prev = (*start)->prev;
        sb->prev->next = sb;
        sb->next = *start;
        (*start)->prev = sb;
        *start = sb;
        *start = SubBlock_merge_prev(*start, start);
        SubBlock_merge_next(*start, start);
    } else {
        *start = sb;
        sb->prev = sb;
        sb->next = sb;
    }
    if (block->max_size < SubBlock_size(*start)) {
        block->max_size = SubBlock_size(*start);
    }
}

/* Marks SB allocated and takes it out of BLOCK's ring. */
static void Block_unlink(Block* block, SubBlock* sb)
{
    SubBlock** start;
    unsigned long size = SubBlock_size(sb);

    sb->size |= ALLOCATED;
    *(unsigned long*)((char*)sb + size) |= PREV_ALLOCATED;
    start = &Block_start(block);
    if (*start == sb) {
        *start = sb->next;
    }
    if (*start == sb) {
        *start = 0;
        block->max_size = 0;
    } else {
        sb->next->prev = sb->prev;
        sb->prev->next = sb->next;
    }
}

/* The first free piece of BLOCK, from its ring's start, of SIZE or more (split if enough is left); the ring then
 * starts after it. Without one, records the largest it saw. */
static SubBlock* Block_subBlock(Block* block, unsigned long size)
{
    SubBlock* start = Block_start(block);
    SubBlock* sb;
    unsigned long sb_size;
    unsigned long max_found;

    if (start == 0) {
        return 0;
    }
    sb = start;
    sb_size = SubBlock_size(sb);
    max_found = sb_size;
    while (sb_size < size) {
        sb = sb->next;
        sb_size = SubBlock_size(sb);
        if (max_found < sb_size) {
            max_found = sb_size;
        }
        if (sb == start) {
            block->max_size = max_found;
            return 0;
        }
    }
    if (sb_size - size >= SUB_MIN_SIZE) {
        SubBlock_split(sb, size);
    }
    Block_start(block) = sb->next;
    Block_unlink(block, sb);
    return sb;
}

static void Block_construct(Block* block, unsigned long size)
{
    SubBlock* sb = (SubBlock*)((char*)block + BLOCK_HEADER);

    block->size = size | ALLOCATED | VARIABLE;
    *(unsigned long*)((char*)block + size - BLOCK_TRAILER) = block->size;
    SubBlock_construct(sb, size - BLOCK_OVERHEAD, block, 0, 0);
    block->max_size = size - BLOCK_OVERHEAD;
    Block_start(block) = 0;
    Block_link(block, sb);
}

/* the ring of blocks, from the one the last allocation came from */
static Block* blocks;

static void link_block(Block* block)
{
    if (blocks != 0) {
        block->prev = blocks->prev;
        block->prev->next = block;
        block->next = blocks;
        blocks->prev = block;
        blocks = block;
    } else {
        blocks = block;
        block->prev = block;
        block->next = block;
    }
}

static void unlink_block(Block* block)
{
    Block* result = block->next;

    if (result == block) {
        result = 0;
    }
    if (blocks == block) {
        blocks = result;
    }
    if (result != 0) {
        result->prev = block->prev;
        result->prev->next = result;
    }
    block->next = 0;
    block->prev = 0;
}

static Block* link_new_block(unsigned long size)
{
    Block* block;

    size = (size + BLOCK_OVERHEAD + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (size < BLOCK_MIN_SIZE) {
        size = BLOCK_MIN_SIZE;
    }
    block = __sys_alloc(size);
    if (block == 0) {
        return 0;
    }
    Block_construct(block, size);
    link_block(block);
    return block;
}

static void* allocate_from_var_pools(unsigned long size)
{
    Block* block;
    SubBlock* sb;

    size = (size + SUB_OVERHEAD + 7) & ~7;
    if (size < SUB_MIN_SIZE) {
        size = SUB_MIN_SIZE;
    }
    block = blocks != 0 ? blocks : link_new_block(size);
    if (block == 0) {
        return 0;
    }
    for (;;) {
        if (size <= block->max_size && (sb = Block_subBlock(block, size)) != 0) {
            blocks = block;
            break;
        }
        block = block->next;
        if (block == blocks) {
            block = link_new_block(size);
            if (block == 0) {
                return 0;
            }
            sb = Block_subBlock(block, size);
            break;
        }
    }
    return (char*)sb + SUB_OVERHEAD;
}

static void deallocate_from_var_pools(void* ptr)
{
    SubBlock* sb = SubBlock_from_pointer(ptr);
    Block* block = SubBlock_block(sb);
    SubBlock* first;

    Block_link(block, sb);
    first = (SubBlock*)((char*)block + BLOCK_HEADER);
    if (SubBlock_is_free(first) && SubBlock_size(first) == Block_size(block) - BLOCK_OVERHEAD) {
        unlink_block(block);
        __sys_free(block);
    }
}

/* The fixed-size pools: a pool's blocks (each a variable-size allocation of about 4000 bytes, its header and then
 * its pieces) share one list of free pieces. A piece's word before the client space is its block (1 clear). */
typedef struct FixBlock {
    struct FixBlock* reserved;
    struct FixBlock* next;
    unsigned long client_size;
} FixBlock;

typedef struct FixSubBlock {
    FixBlock* block;
    struct FixSubBlock* next;
} FixSubBlock;

typedef struct FixStart {
    FixBlock* next;
    FixSubBlock* start;
    unsigned long n_allocated;
} FixStart;

#define FIX_POOLS 4
#define FIX_ALLOC_SIZE 4000
#define FIX_MAX_SIZE 68
#define FIX_BLOCK_HEADER 12
#define FIX_SUB_OVERHEAD 4

static const unsigned long fix_pool_sizes[FIX_POOLS] = {12, 20, 36, FIX_MAX_SIZE};
static FixStart fix_start[FIX_POOLS];

#define is_variable(ptr) (*(unsigned long*)((char*)(ptr) - sizeof(unsigned long)) & VARIABLE)

static unsigned long client_size(void* ptr)
{
    if (!is_variable(ptr)) {
        return ((FixSubBlock*)((char*)ptr - FIX_SUB_OVERHEAD))->block->client_size;
    }
    return SubBlock_size(SubBlock_from_pointer(ptr)) - SUB_OVERHEAD;
}

/* Cuts CHUNK into POOL's pieces, ahead of the pool's free list. */
static void FixBlock_construct(FixBlock* block, FixBlock* next, int pool, char* chunk, unsigned long chunk_size)
{
    unsigned long stride = fix_pool_sizes[pool] + FIX_SUB_OVERHEAD;
    unsigned long n = chunk_size / stride;
    unsigned long i;
    char* p = chunk;

    block->reserved = 0;
    block->next = next;
    block->client_size = fix_pool_sizes[pool];
    for (i = 0; i < n - 1; ++i) {
        ((FixSubBlock*)p)->block = block;
        ((FixSubBlock*)p)->next = (FixSubBlock*)(p + stride);
        p += stride;
    }
    ((FixSubBlock*)p)->block = block;
    ((FixSubBlock*)p)->next = fix_start[pool].start;
    fix_start[pool].start = (FixSubBlock*)chunk;
}

static void* allocate_from_fixed_pools(unsigned long size)
{
    int i = 0;
    FixSubBlock* p;

    while (size > fix_pool_sizes[i]) {
        ++i;
    }
    if (fix_start[i].start == 0) {
        char* block = allocate_from_var_pools(FIX_ALLOC_SIZE);
        if (block == 0) {
            return 0;
        }
        FixBlock_construct((FixBlock*)block, fix_start[i].next, i, block + FIX_BLOCK_HEADER,
            client_size(block) - FIX_BLOCK_HEADER);
        fix_start[i].next = (FixBlock*)block;
    }
    p = fix_start[i].start;
    fix_start[i].start = p->next;
    ++fix_start[i].n_allocated;
    return (char*)p + FIX_SUB_OVERHEAD;
}

static void deallocate_from_fixed_pools(void* ptr, unsigned long size)
{
    int i = 0;
    FixSubBlock* p;

    while (size > fix_pool_sizes[i]) {
        ++i;
    }
    p = (FixSubBlock*)((char*)ptr - FIX_SUB_OVERHEAD);
    p->next = fix_start[i].start;
    fix_start[i].start = p;
    if (--fix_start[i].n_allocated == 0) {
        FixBlock* block = fix_start[i].next;
        while (block != 0) {
            FixBlock* next = block->next;
            deallocate_from_var_pools(block);
            block = next;
        }
        fix_start[i].next = 0;
        fix_start[i].start = 0;
    }
}

void* malloc(size_t size)
{
    if (size == 0) {
        return 0;
    }
    if (size <= FIX_MAX_SIZE) {
        return allocate_from_fixed_pools(size);
    }
    return allocate_from_var_pools(size);
}

void free(void* ptr)
{
    unsigned long size;

    if (ptr == 0) {
        return;
    }
    size = client_size(ptr);
    if (size <= FIX_MAX_SIZE) {
        deallocate_from_fixed_pools(ptr, size);
    } else {
        deallocate_from_var_pools(ptr);
    }
}

/* Grows a variable-size piece into a free piece after it when that is enough, and shrinks one in place (freeing
 * what is cut off when it is worth a piece); otherwise moves. */
void* realloc(void* ptr, size_t size)
{
    unsigned long current_size;
    unsigned long sz;
    SubBlock* sb;
    void* newptr;

    if (ptr == 0) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return 0;
    }
    current_size = client_size(ptr);
    if (size > current_size) {
        if (is_variable(ptr)) {
            sz = (size + SUB_OVERHEAD + 7) & ~7;
            if (sz < SUB_MIN_SIZE) {
                sz = SUB_MIN_SIZE;
            }
            sb = SubBlock_from_pointer(ptr);
            SubBlock_merge_next(sb, &Block_start(SubBlock_block(sb)));
            if (SubBlock_size(sb) >= sz) {
                if (SubBlock_size(sb) - sz >= SUB_MIN_SIZE) {
                    Block_link(SubBlock_block(sb), SubBlock_split(sb, sz));
                }
                return ptr;
            }
        }
        newptr = malloc(size);
        if (newptr == 0) {
            return 0;
        }
        memcpy(newptr, ptr, current_size);
        free(ptr);
        return newptr;
    }
    if (is_variable(ptr)) {
        sz = (size + SUB_OVERHEAD + 7) & ~7;
        if (sz < SUB_MIN_SIZE) {
            sz = SUB_MIN_SIZE;
        }
        sb = SubBlock_from_pointer(ptr);
        if (SubBlock_size(sb) - sz >= SUB_MIN_SIZE) {
            Block_link(SubBlock_block(sb), SubBlock_split(sb, sz));
        }
    }
    return ptr;
}

void* calloc(size_t nmemb, size_t size)
{
    void* result;

    size *= nmemb;
    result = malloc(size);
    if (result != 0) {
        memset(result, 0, size);
    }
    return result;
}

/* Memory from the system (GlobalAlloc), 8 bytes more, so that it can be aligned to 8: the word before the returned
 * memory is 1 when it starts 4 bytes in, 0 when 8. */
void* __sys_alloc(size_t size)
{
    char* ptr = (char*)GlobalAlloc(GMEM_FIXED, size + 8);

    if (ptr == 0) {
        return 0;
    }
    if ((unsigned long)ptr % 8 == 0) {
        ptr += 4;
        *(unsigned long*)ptr = 0;
    } else {
        *(unsigned long*)ptr = 1;
    }
    return ptr + 4;
}

void __sys_free(void* p)
{
    char* ptr = (char*)p - 4;

    if (*(unsigned long*)ptr == 0) {
        ptr -= 4;
    }
    GlobalFree((HGLOBAL)ptr);
}
