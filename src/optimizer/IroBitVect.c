#define CERROR_FILE "IroBitVect.c"
#include "compiler/common.h"
#include "compiler/IroBitVect.h"
#include "compiler/CError.h"
#include "compiler/CompilerTools.h"
#include <string.h>

unsigned int IroBitVect_AllocateBitVector(BitVector **vector, unsigned int bitCount)
{
    bitCount = (bitCount >> 5) + 1U;
    *vector = oalloc(bitCount * sizeof((*vector)->bits[0]) + sizeof((*vector)->size));
    (*vector)->size = bitCount;
    memset((*vector)->bits, 0, (*vector)->size * sizeof((*vector)->bits[0]));
}

void IroBitVect_ClearBit(UInt32 bit, BitVector *vector)
{
    if (bit >> 5 < vector->size) {
        vector->bits[bit >> 5] &= ~(1U << (bit & 0x1f));
    } else {
        CError_FATAL(BY_VERSION(64, 73));
    }
}

void IroBitVect_Intersect(BitVector *source, BitVector *destination)
{
    unsigned int index;

    for (index = 0; index < destination->size; index = index + 1) {
        destination->bits[index] &= source->bits[index];
    }
}

void IroBitVect_Or(BitVector *sourceData, BitVector *destinationData)
{
    UInt32 *source = &sourceData->size;
    UInt32 *destination = &destinationData->size;
    UInt32 index;
    UInt32 count;

    count = *source;
    if (*destination < count) {
        count = *destination;
    }
    for (index = 0; index < count; index = index + 1) {
        destination[index + 1] |= source[index + 1];
    }
}

BY_VERSION(unsigned int, Boolean) IroBitVect_Intersects(BitVector *left, BitVector *right)
{
    BY_VERSION(unsigned int, UInt32) wordCount;
    BY_VERSION(unsigned int, UInt32) wordIndex;

    wordCount = left->size;
#if VERSION >= VERSION_GC_1_3
    if (right->size < wordCount) {
#else
    if (right->size < left->size) {
#endif
        wordCount = right->size;
    }
    for (wordIndex = 0; wordIndex < wordCount; wordIndex++) {
        if ((left->bits[wordIndex] & right->bits[wordIndex]) != 0) {
            return 1;
        }
    }
    return 0;
}

BY_VERSION(int, char) IroBitVect_AreEqual(BitVector *a, BitVector *b)
{
    unsigned int i;

    for (i = 0; i < a->size; i++) {
        if (a->bits[i] != b->bits[i])
            return 0;
    }
    return 1;
}

void IroBitVect_Subtract(BitVector *source, BitVector *destination)
{
    unsigned int i;

    for (i = 0; i < destination->size; i++) {
        destination->bits[i] &= ~source->bits[i];
    }
}

void IroBitVect_CopyBitVector(BitVector *source, BitVector *destination)
{
    memcpy(destination->bits, source->bits, destination->size << 2);
}

void IroBitVect_ClearBitVector(BitVector *vector)
{
    unsigned int word_count;
    word_count = vector->size;
    memset(vector->bits, 0, word_count << 2U);
}

void IroBitVect_SetAllBits(BitVector *array)
{
    memset(array->bits, 255, array->size * sizeof(unsigned int));
}

BY_VERSION(int, Boolean) IroBitVect_ContainsSubset(BitVector *subset, BitVector *superset)
{
    BY_VERSION(unsigned int, int) wordIndex;

#if VERSION >= VERSION_GC_1_3
    for (wordIndex = 0; wordIndex < subset->size; wordIndex++) {
#else
    for (wordIndex = 0; wordIndex < subset->size; wordIndex = wordIndex + 1) {
#endif
        if (superset->size < wordIndex) {
            if (subset->bits[wordIndex] != 0) {
                return 0;
            }
        } else if ((~superset->bits[wordIndex] & subset->bits[wordIndex]) != 0) {
            return 0;
        }
    }
    return 1;
}

unsigned int is_bitvector_empty(struct BitVector *values)
{
    unsigned int index;

    for (index = 0; index < values->size; ++index) {
        if (values->bits[index] != 0) {
            return 0;
        }
    }
    return 1;
}
