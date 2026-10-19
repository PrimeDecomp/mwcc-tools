/* The wasm module's entry (the host calls _start): lib/runtime/maincrt.c's mainCRTStartup without its x86 exception
 * frame and MWCC's static initializers (clang's run first, from __wasm_call_ctors). */
#include <stdlib.h>

void __wasm_call_ctors(void);
void _CRTStartup(void);
int _InitializeMainThreadData(void);
void _SetupArgs(void);
extern int __argc;
extern char** __argv;
int main(int argc, char** argv);

void _start(void)
{
    __wasm_call_ctors();
    _CRTStartup();
    if (!_InitializeMainThreadData()) {
        exit(-1);
    }
    _SetupArgs();
    exit(main(__argc, __argv));
}
