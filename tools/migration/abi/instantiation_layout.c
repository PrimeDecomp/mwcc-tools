#include <stddef.h>
#define MWCC_ORACLE_TRACE 1
#include "src/oracle/Hooks.c"
struct InstantiationAlignment { char tag; TemplFuncInstance value; };
unsigned long RustAbiInstantiationLayout[] = {
    sizeof(TemplFuncInstance), offsetof(struct InstantiationAlignment, value),
    offsetof(TemplFuncInstance, object), sizeof(((TemplFuncInstance *)0)->object),
    offsetof(TemplFuncInstance, next), sizeof(((TemplFuncInstance *)0)->next),
    sizeof(Boolean), sizeof(char), (unsigned long)((char)255),
    sizeof(pendingInlineWork), sizeof(pending_prec_nodes), sizeof(deferredInlineNodes),
    sizeof(cscope_currentfunc), sizeof(data_005dfb62)
};
