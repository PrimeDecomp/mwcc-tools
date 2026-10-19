#include <stddef.h>
#define MWCC_ORACLE_TRACE 1
#include "src/oracle/Hooks.c"
#define FIELD(t, f) offsetof(t, f), sizeof(((t *)0)->f)
unsigned long RustAbiFrontendLayout[] = {
    FIELD(Object, datatype), FIELD(Object, nspace), FIELD(Object, name), FIELD(Object, type), FIELD(Object, qual), FIELD(Object, u.func.u),
    FIELD(NameSpace, parent), FIELD(NameSpace, name), FIELD(TypeFunc, flags),
    FIELD(ENode, type), FIELD(ENode, flags), FIELD(ENode, data.objref), FIELD(ENode, data.longval), FIELD(ENode, data.intval.lo),
    FIELD(ENode, data.diadic.right), FIELD(ENode, data.funccall.args), FIELD(ENode, data.cond.expr2),
    FIELD(ENodeList, next), FIELD(ENodeList, node), FIELD(CInlineInfo, nstmts),
    FIELD(CPrecNode, obj), FIELD(CPrecNode, kind), sizeof(data_00582468), sizeof(data_005dfb66), sizeof(data_005dfb68), sizeof(anyerrors),
    EARGOBJ, ELOCOBJ, EASS, EMEMBER, ECOND, EOBJREF, TYPEFUNC, Q_INLINE, ENODE_FLAG_80,
    offsetof(struct CompilerLinkerOptions, inlineAuto), offsetof(struct CompilerLinkerOptions, inlineLimit),
    offsetof(struct CompilerLinkerOptions, alwaysinline), offsetof(CInlineInfo, kind)
};
unsigned long RustAbiFrontendCount = sizeof(RustAbiFrontendLayout) / sizeof(RustAbiFrontendLayout[0]);
