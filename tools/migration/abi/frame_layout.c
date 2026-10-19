/* Compile with each historical source compiler; read the array from its COFF. */
#include <stddef.h>
#include "compiler/objects.h"
#include "compiler/types.h"
#include "compiler/Registers.h"
#include "compiler/CodeGen.h"

struct ObjectListAlignment { char tag; ObjectList value; };
struct TemporaryAlignment { char tag; struct TemporaryObjectEntry value; };
struct ObjectAlignment { char tag; Object value; };
struct NameAlignment { char tag; HashNameNode value; };
struct TypeAlignment { char tag; Type value; };
struct InfoAlignment { char tag; VarInfo value; };

unsigned long RustAbiFrameLayout[] = {
    sizeof(void *), DLOCAL,
    sizeof(ObjectList), offsetof(ObjectList, next), offsetof(ObjectList, object),
    offsetof(struct ObjectListAlignment, value),
    sizeof(struct TemporaryObjectEntry), offsetof(struct TemporaryObjectEntry, next),
    offsetof(struct TemporaryObjectEntry, object), offsetof(struct TemporaryAlignment, value),
    sizeof(Object), offsetof(Object, datatype), sizeof(((Object *)0)->datatype),
    offsetof(Object, name), offsetof(Object, type), offsetof(Object, u.var.info),
    offsetof(Object, u.var.uid), sizeof(((Object *)0)->u.var.uid), offsetof(struct ObjectAlignment, value),
    sizeof(HashNameNode), offsetof(HashNameNode, name), sizeof(((HashNameNode *)0)->name),
    offsetof(struct NameAlignment, value),
    sizeof(Type), offsetof(Type, size), sizeof(((Type *)0)->size), offsetof(struct TypeAlignment, value),
    sizeof(VarInfo), offsetof(VarInfo, reg), sizeof(((VarInfo *)0)->reg),
    offsetof(VarInfo, used), sizeof(((VarInfo *)0)->used), offsetof(struct InfoAlignment, value)
};
