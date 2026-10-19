/* The wasm build's varargs: the target's own (gc2's include/libc/stdarg.h walks x86's stack, which wasm32 does not
 * lay out the same way: a double is 8-aligned there). */
#ifndef _STDARG_H
#define _STDARG_H
#ifndef __va_list__
#define __va_list__
typedef __builtin_va_list va_list;
#endif
#define va_start(ap, parm) __builtin_va_start(ap, parm)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
/* MSL's x86 idiom: the variadic arguments after PARM, as a value */
#define __va_start(parm)                      ({                                            __builtin_va_list __msl_ap;               __builtin_va_start(__msl_ap, parm);         __msl_ap;                             })
#endif
