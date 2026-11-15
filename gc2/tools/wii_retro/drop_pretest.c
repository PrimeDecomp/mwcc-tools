/* A pass for Wii 1.0RC1's mwcceppc.exe, run at the start of optimizefinalbranches (after register coloring
 * and scheduling): delete a loop's pretest when it repeats the loop's own test and the loop body makes a call.
 *
 *   G: ... cmpl crN,rA,rB; bt crN.eq, X
 *   F: b T
 *      ...body (bl / bctrl / blrl somewhere)...
 *   T: cmpl crN,rA,rB; bf crN.eq, body
 *   X: ...
 *
 * RC1 layouts: PCodeBlock {next 0x00, labels 0x08, first 0x14, last 0x18, codeOffset 0x20, pcodeCount 0x28};
 * PCode {next 0x00, prev 0x04, op 0x28, args 0x2c, 14 bytes each: kind, class, then data (reg number at +4,
 * immediate at +2, label at +2)}; PCodeLabel {block 0x04}. Opcodes: b 0, bl 1, bt 5, bf 8, bctrl 19, blrl 20,
 * cmpl 85. Operand kinds: register 0, immediate 2, label 6.
 */
typedef short s16;

extern char* RC1_pcbasicblocks; /* 0x751c88 */
#define PCBASICBLOCKS RC1_pcbasicblocks
#define BLK_NEXT(b) (*(char**)((b) + 0x00))
#define BLK_FIRST(b) (*(char**)((b) + 0x14))
#define BLK_LAST(b) (*(char**)((b) + 0x18))
#define BLK_COUNT(b) (*(s16*)((b) + 0x28))
#define PC_NEXT(p) (*(char**)((p) + 0x00))
#define PC_PREV(p) (*(char**)((p) + 0x04))
#define PC_OP(p) (*(s16*)((p) + 0x28))
#define ARG(p, i) ((p) + 0x2c + 14 * (i))

/* RC1's functions and data, resolved by the patcher (gc2/tools/wii_patch_pretest.py). */
void RC1_deletepcode(char* pcode);           /* 0x59b4c0 */
int RC1_pccomputeoffsets(void);              /* 0x59b270 */
int RC1_optimizefinalbranches(int codesize); /* 0x59ca20 */
#define deletepcode RC1_deletepcode
#define pccomputeoffsets RC1_pccomputeoffsets
#define optimizefinalbranches RC1_optimizefinalbranches

static int same_reg(const char* a, const char* b) {
    return a[0] == 0 && b[0] == 0 && a[1] == b[1] && *(const s16*)(a + 4) == *(const s16*)(b + 4);
}

static int imm(const char* a) {
    return a[0] == 2 ? *(const int*)(a + 2) : -1;
}

static char* nonempty(char* b) {
    while (b && BLK_COUNT(b) == 0)
        b = BLK_NEXT(b);
    return b;
}

static char* label_block(char* arg) {
    if (arg[0] != 6)
        return 0;
    return nonempty(*(char**)(*(char**)(arg + 2) + 4));
}

static int drop_pretests(void) {
    int changed = 0;
    char* g;
    for (g = PCBASICBLOCKS; g; g = BLK_NEXT(g)) {
        char *bt, *cmp, *exit, *f, *t, *tc, *tb, *b;
        int call = 0;
        if (BLK_COUNT(g) < 2)
            continue;
        bt = BLK_LAST(g);
        cmp = PC_PREV(bt);
        if (PC_OP(bt) != 5 || PC_OP(cmp) != 85 || imm(ARG(bt, 1)) != 2 || !same_reg(ARG(bt, 0), ARG(cmp, 0)))
            continue;
        if (!(exit = label_block(ARG(bt, 2))))
            continue;
        f = nonempty(BLK_NEXT(g));
        if (!f || BLK_COUNT(f) != 1 || PC_OP(BLK_FIRST(f)) != 0)
            continue;
        t = label_block(ARG(BLK_FIRST(f), 0));
        if (!t || BLK_COUNT(t) != 2)
            continue;
        tc = BLK_FIRST(t);
        tb = BLK_LAST(t);
        if (PC_OP(tc) != 85 || PC_OP(tb) != 8 || imm(ARG(tb, 1)) != 2)
            continue;
        if (!same_reg(ARG(tc, 0), ARG(cmp, 0)) || !same_reg(ARG(tc, 1), ARG(cmp, 1)) ||
            !same_reg(ARG(tc, 2), ARG(cmp, 2)) || !same_reg(ARG(tb, 0), ARG(bt, 0)))
            continue;
        if (nonempty(BLK_NEXT(t)) != exit)
            continue;
        for (b = BLK_NEXT(f); b && b != t; b = BLK_NEXT(b)) {
            char* p = BLK_FIRST(b);
            int n;
            for (n = BLK_COUNT(b); n > 0 && p; n--, p = PC_NEXT(p)) {
                int op = PC_OP(p);
                if (op == 1 || op == 19 || op == 20)
                    call = 1;
            }
        }
        if (b != t || !call)
            continue;
        deletepcode(bt);
        deletepcode(cmp);
        changed = 1;
    }
    return changed;
}

/* Replaces the call to optimizefinalbranches in assemblefunction. */
int retro_optimizefinalbranches(int codesize) {
    if (drop_pretests())
        codesize = pccomputeoffsets();
    return optimizefinalbranches(codesize);
}
