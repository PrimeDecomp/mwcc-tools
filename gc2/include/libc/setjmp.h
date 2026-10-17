#ifndef MWCC_HOST_PROBE_SETJMP_H
#define MWCC_HOST_PROBE_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

/* MSL's x86 setjmp (lib/msl/MSL_Common/Include/csetjmp, __INTEL__). */
typedef int jmp_buf[10];
int _Setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#define setjmp(env) _Setjmp(env)

#ifdef __cplusplus
}
#endif

#endif
