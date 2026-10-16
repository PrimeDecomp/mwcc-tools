#include <stddef.h>
#include "compiler/CInline.h"
#include "compiler/types.h"
#include "compiler/objects.h"
#include "compiler/enode.h"
#define FIELD(t, f) offsetof(t, f), sizeof(((t *)0)->f)
struct BindingSlotAlignment { char tag; struct InlineSlot value; };
struct BindingFormalAlignment { char tag; CInlineVar value; };
unsigned long RustAbiBindingsLayout[] = {
    FIELD(CInlineInfo, nargs), FIELD(CInlineInfo, arginfo),
    sizeof(struct InlineSlot), offsetof(struct BindingSlotAlignment, value),
    FIELD(struct InlineSlot, var), FIELD(struct InlineSlot, expr), FIELD(struct InlineSlot, arg),
    sizeof(CInlineVar), offsetof(struct BindingFormalAlignment, value),
    FIELD(CInlineVar, name), FIELD(CInlineVar, type), FIELD(CInlineVar, used), FIELD(CInlineVar, dirty),
    FIELD(TypeFunc, args), FIELD(TypeFunc, flags), FIELD(FuncArg, next), FIELD(FuncArg, name), FIELD(FuncArg, type),
    FIELD(TypePointer, qual), TYPEFUNC, TYPEPOINTER, Q_REFERENCE, FUNC_AUTO_GENERATED,
    ECOMMA, EFORCELOAD, ETYPCON, EFUNCCALL, EOBJREF, EINDIRECT
};
unsigned long RustAbiBindingsCount = sizeof(RustAbiBindingsLayout) / sizeof(RustAbiBindingsLayout[0]);
