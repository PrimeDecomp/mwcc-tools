#include "driver/LowMem.h"
#include "driver/MsDos.h"

static OSErr memErr;

UInt32 LMGetTicks(void) {
    return (OS_GetMilliseconds() * 60) / 1000;
}

// (dead in 2.6: the linker dropped it)
UInt32 LMGetTime(void) {
    return OS_GetMilliseconds() / 1000;
}

OSErr LMGetMemErr(void) {
    return memErr;
}

// (dead in 2.6: the linker dropped it)
void LMSetMemErr(OSErr value) {
    memErr = value;
}
