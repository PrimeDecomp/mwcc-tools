#include <stddef.h>
#define MWCC_ORACLE_TRACE 1
/* The verifier provides an isolated copy of the frozen source at this path. */
#include "src/oracle/Hooks.c"
struct OriginAlignment { char tag; Origin value; };
#define FIELD(t, f) offsetof(t, f), sizeof(((t *)0)->f)
unsigned long RustAbiOriginsLayout[] = {
    sizeof(Origin), offsetof(struct OriginAlignment, value), FIELD(Origin, funcref), FIELD(Origin, parent), FIELD(Origin, link),
    ORIGIN_SIZE, sizeof(origins),
    sizeof(ENode), FIELD(ENode, type), FIELD(ENode, data.monadic), FIELD(ENode, data.diadic.right),
    FIELD(ENode, data.cond.cond), FIELD(ENode, data.cond.expr1), FIELD(ENode, data.cond.expr2),
    FIELD(ENode, data.funccall.funcref), FIELD(ENode, data.funccall.args),
    sizeof(ENodeList), FIELD(ENodeList, next), FIELD(ENodeList, node),
    sizeof(Statement), FIELD(Statement, next), FIELD(Statement, type), FIELD(Statement, expr), FIELD(Statement, sourceoffset),
    EPOSTINC, EPOSTDEC, EPREINC, EPREDEC, EINDIRECT, EMONMIN, EBINNOT, ELOGNOT, EFORCELOAD, ETYPCON, EBITFIELD,
    EMUL, EMULV, EDIV, EMODULO, EADDV, ESUBV, EADD, ESUB, ESHL, ESHR, ELESS, EGREATER, ELESSEQU, EGREATEREQU,
    EEQU, ENOTEQU, EAND, EXOR, EOR, ELAND, ELOR, EASS, EMULASS, EDIVASS, EMODASS, EADDASS, ESUBASS,
    ESHLASS, ESHRASS, EANDASS, EXORASS, EORASS, ECOMMA, EPMODULO, EROTL, EROTR, EBCLR, EBTST, EBSET, ENULLCHECK, EMFPOINTER,
    ECOND, ECONDASS, EFUNCCALL, EFUNCCALLP, EOBJREF,
    ST_EXPRESSION, ST_SWITCH, ST_IFGOTO, ST_IFNGOTO, ST_RETURN, ST_GOTOEXPR
};
unsigned long RustAbiOriginsCount = sizeof(RustAbiOriginsLayout) / sizeof(RustAbiOriginsLayout[0]);
