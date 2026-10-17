/* The wasm build's Win32 SDK: gc2's own declarations (include/compiler/win32.h) and what MSL's Win32 layer uses
 * besides. Every function is an import of the host (crates/mwcc-wasm), which implements it on the host's files. */
#ifndef MWCC_WASM_WINDOWS_H
#define MWCC_WASM_WINDOWS_H
#include "compiler/win32.h"
#include "win32_extra.h"
#endif
