/* The runtime clang's wasm setjmp/longjmp lowering calls (-mllvm -wasm-enable-sjlj): setjmp records which call of
 * which function it was made in, and longjmp throws the buffer's argument record with the __c_longjmp tag
 * (sjlj_tag.s), which that function's landing pad catches and tests. */
#include <setjmp.h>

struct jmp_buf_impl {
    void* func_invocation_id;
    unsigned int label;
    struct {
        void* env;
        int val;
    } arg;
};

void __wasm_setjmp(void* env, unsigned int label, void* func_invocation_id)
{
    struct jmp_buf_impl* jb = env;
    jb->func_invocation_id = func_invocation_id;
    jb->label = label;
}

unsigned int __wasm_setjmp_test(void* env, void* func_invocation_id)
{
    struct jmp_buf_impl* jb = env;
    return jb->func_invocation_id == func_invocation_id ? jb->label : 0;
}

void __wasm_longjmp(void* env, int val)
{
    struct jmp_buf_impl* jb = env;
    jb->arg.env = env;
    jb->arg.val = val ? val : 1;
    __builtin_wasm_throw(1, &jb->arg);
}
