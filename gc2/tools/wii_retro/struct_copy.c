/* Wii 1.0RC1 alignment and block-copy intrinsics. Use declared alignment
 * for non-local operands and keep RC1's alignment for local storage.
 */
#include "rc1_codegen.h"

extern u8 RC1_structalignment;
extern u8 RC1_oldalignment;
extern int RC1_GetQUALalign(Type*, u32);
extern short RC1_GetQualifiedTypeAlign(Type*, int);
extern void clear_block(Operand*, int, int);

enum {
    PowerPCAlignment = 2,
};

static inline int in_parameter_area(Object* object) {
    VarInfo* info = Registers_GetVarInfo(object);
    if (!info || !(info->flags & InParameterArea))
        return 0;
    return 1;
}

extern inline int retro_ArgumentAlignment(Type* type) {
    u8 mode = RC1_structalignment;
    u8 oldalignment = RC1_oldalignment;
    int alignment;

    if (type->kind == StructureType)
        return ((TypeStruct*)type)->alignment;
    if (type->kind == ClassType)
        return ((TypeClass*)type)->alignment;

    RC1_structalignment = PowerPCAlignment;
    RC1_oldalignment = 0;
    alignment = RC1_GetQualifiedTypeAlign(type, 0);
    RC1_structalignment = mode;
    RC1_oldalignment = oldalignment;
    return alignment;
}

int CMach_GetObjectAlignment(Object* object) {
    if (object->address_kind == LocalObject) {
        if (in_parameter_area(object))
            return retro_ArgumentAlignment(object->type);
        return RC1_AllocationAlignment(object->type, object->qualifiers);
    } else {
#ifdef WII_RETRO_STOCK_ALIGNMENT
        if (!RC1_legacy_struct_alignment)
            return RC1_AllocationAlignment(object->type, object->qualifiers);
#endif
        {
            Type* type = object->type;
            short alignment = (short)RC1_GetQUALalign(type, object->qualifiers);
            return alignment ? alignment : RC1_GetQualifiedTypeAlign(type, 1);
        }
    }
}

static inline int intrinsic_alignment(const Operand* operand, const ENode* expr) {
    if (operand->object)
        return CMach_GetObjectAlignment(operand->object);
    if (expr->type) {
#ifdef WII_RETRO_STOCK_ALIGNMENT
        if (!RC1_legacy_struct_alignment)
            return RC1_AllocationAlignment(expr->type, 0);
#endif
        return CMach_GetTypeAlign(expr->type, 0);
    }
    return 1;
}

void retro_ClearIntrinsic(ENode* dest, int size, Operand* output) {
    Operand dest_operand, scratch;

    init_operand(&dest_operand);
    init_operand(&scratch);
    cgdispatch[dest->kind](dest, -1, -1, output);
    dest_operand = *output;
    indirect(&dest_operand, dest);
    clear_block(&dest_operand, size, intrinsic_alignment(&dest_operand, dest));
}

void retro_CopyIntrinsic(ENode* dest, ENode* source, int size, Operand* output) {
    Operand dest_operand, source_operand;

    init_operand(&dest_operand);
    init_operand(&source_operand);
    cgdispatch[source->kind](source, -1, -1, &source_operand);
    indirect(&source_operand, source);
    cgdispatch[dest->kind](dest, -1, -1, output);
    dest_operand = *output;
    indirect(&dest_operand, dest);
    move_block(&dest_operand, &source_operand, size, intrinsic_alignment(&dest_operand, dest));
}
