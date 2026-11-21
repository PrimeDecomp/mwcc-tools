/* Preserve the Gekko stack-save rule from the GameCube backend. */
#include "rc1_codegen.h"

enum {
    PC_STFD = 0x9a,
    PC_PSQ_L = 0x192,
    PC_PSQ_ST = 0x196,
    SPR_GQR0 = 0x390,
    VolatilePCode = 0x80,
    PairedSaveSlotSize = 16,
    PairedSaveOffset = 8,
    PairedDisplacementSign = 0x800,
    PairedDisplacementRange = 0x1000,
};

extern short RC1_processor;
extern u8 StackFrame_has_vectors;
extern int RC1_fpr_save_count;
extern int RC1_fpr_save_offset;
extern int RC1_fpr_slot_size;
extern Type stdouble;
extern u8 RC1_UseSaveHelper(int register_class);
extern void RC1_SaveNonvolatileFprs(short base, int offset);
extern void RC1_RestoreNonvolatileFprs(short base, int offset);
extern void RC1_SetPCodeFlags(unsigned flags, unsigned extended_flags);
extern void load_fpr(Type*, short target, short base, Object*, int displacement);

static int needs_legacy_paired_save(int offset) {
    /* RC1 already handles scalar saves and in-range D-form offsets. The older
       backend keeps D-form up to the full 12-bit range of the save area. */
    int end = offset + RC1_fpr_save_count * PairedSaveSlotSize;
    return RC1_processor == GekkoProcessor && StackFrame_has_vectors &&
        RC1_fpr_slot_size == PairedSaveSlotSize && RC1_fpr_save_count > 0 &&
        end >= PairedDisplacementSign - 1 && end < PairedDisplacementRange &&
        !RC1_UseSaveHelper(RegFPR);
}

static int paired_displacement(int slot) {
    /* RC1's encoder requires a signed displacement. Preserve the original
       compiler's 12-bit field, including offsets that wrap into negatives. */
    int field = (slot + PairedSaveOffset) & (PairedDisplacementRange - 1);
    return field < PairedDisplacementSign ? field : field - PairedDisplacementRange;
}

void retro_SaveNonvolatileFprs(short base, int offset) {
    int first_slot = offset + RC1_fpr_save_offset;
    if (!needs_legacy_paired_save(first_slot)) {
        RC1_SaveNonvolatileFprs(base, offset);
        return;
    }

    for (int i = 1; i <= RC1_fpr_save_count; ++i) {
        int reg = 32 - i;
        int slot = first_slot + (RC1_fpr_save_count - i) * PairedSaveSlotSize;
        emitpcode(PC_STFD, reg, base, NULL, slot);
        RC1_SetPCodeFlags(VolatilePCode, 0);
        emitpcode(PC_PSQ_ST, reg, base, NULL, paired_displacement(slot), 0, SPR_GQR0);
        RC1_SetPCodeFlags(VolatilePCode, 0);
    }
}

void retro_RestoreNonvolatileFprs(short base, int offset) {
    int first_slot = offset + RC1_fpr_save_offset;
    if (!needs_legacy_paired_save(first_slot)) {
        RC1_RestoreNonvolatileFprs(base, offset);
        return;
    }

    for (int i = 1; i <= RC1_fpr_save_count; ++i) {
        int reg = 32 - i;
        int slot = first_slot + (RC1_fpr_save_count - i) * PairedSaveSlotSize;
        emitpcode(PC_PSQ_L, reg, base, NULL, paired_displacement(slot), 0, SPR_GQR0);
        RC1_SetPCodeFlags(VolatilePCode, 0);
        load_fpr(&stdouble, reg, base, NULL, slot);
        RC1_SetPCodeFlags(VolatilePCode, 0);
    }
}
