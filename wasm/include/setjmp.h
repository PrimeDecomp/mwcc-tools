/* The wasm build's setjmp: clang lowers setjmp and longjmp to wasm exceptions (-mllvm -wasm-enable-sjlj), with the
 * runtime functions of wasm/src/sjlj.c. The buffer is that runtime's record. */
#ifndef _SETJMP_H
#define _SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct __jmp_buf_tag {
    void* func_invocation_id;
    unsigned int label;
    struct {
        void* env;
        int val;
    } arg;
} jmp_buf[1];
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
