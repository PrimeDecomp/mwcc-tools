#include <stddef.h>
#include "compiler/IroVars.h"
#include "compiler/IroLoop.h"
#include "compiler/IroCSE.h"
#include "compiler/enode.h"
#include "compiler/CFunc.h"
#include "compiler/Registers.h"
#include "compiler/IroDump.h"
#define FIELD(t, f) offsetof(t, f), sizeof(((t *)0)->f)
unsigned long RustAbiScalarCseLayout[] = {
    sizeof(IROLinear), FIELD(IROLinear, type), FIELD(IROLinear, nodetype), FIELD(IROLinear, index),
    FIELD(IROLinear, stmt), FIELD(IROLinear, expr), FIELD(IROLinear, u.node), FIELD(IROLinear, u.diadic.right), FIELD(IROLinear, next),
    sizeof(IROExpr), FIELD(IROExpr, index), FIELD(IROExpr, linear), FIELD(IROExpr, temp),
    sizeof(ENode), FIELD(ENode, type), FIELD(ENode, data.objref), FIELD(ENode, data.monadic),
    FIELD(ENode, data.diadic.right), FIELD(ENode, data.funccall.funcref), FIELD(ENode, data.funccall.args), FIELD(ENode, data.intval.lo),
    sizeof(ENodeList), FIELD(ENodeList, node), FIELD(ENodeList, next),
    sizeof(IROVarPart), FIELD(IROVarPart, flags), FIELD(IROVarPart, var), offsetof(IROVarPart, x12),
    FIELD(IROVarPart, type), FIELD(IROVarPart, next), FIELD(IROVarPart, uses),
    sizeof(VarRecord), FIELD(VarRecord, object), FIELD(VarRecord, noregister),
    sizeof(IROElmList), FIELD(IROElmList, next), FIELD(Statement, sourceoffset),
    FIELD(VarInfo, reg), FIELD(VarInfo, rclass), FIELD(VarInfo, noregister),
    sizeof(Type), FIELD(Type, type), FIELD(Type, size),
    (char)0x80 < 0, sizeof(long), IROLinearOperand, IROLinearOp1Arg, IROLinearOp2Arg,
    EOBJREF, EINDIRECT, EFORCELOAD, ETYPCON, EADD, ECOMMA, EFUNCCALL, EINTCONST, EMUL,
    sizeof(INT_005882b8)
};
