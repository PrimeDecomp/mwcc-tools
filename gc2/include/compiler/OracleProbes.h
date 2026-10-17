#ifndef COMPILER_ORACLEPROBES_H
#define COMPILER_ORACLEPROBES_H

#include "compiler/common.h"

// The oracle's probes. The oracle built from these sources (ORACLE_PROBES) reports here what the oracle linked from
// the original's code reports from probes at its instructions, with the same records. Each call names the
// original's site. Without ORACLE_PROBES, ORACLE_PROBE is nothing.

#ifdef ORACLE_PROBES

// The arenas (`pv.arena.*`). PAIR is the allocation's request and commit sites in the original: 1 to 4 galloc,
// lalloc, aalloc and oalloc, 5 InitNameHash, 6 and 7 GetHashNameNodeExport's two, 8 and 9 GetHashNameNode's. FRAME
// identifies the allocating function's frame: a request and its commit give the same.
void OracleProbe_ArenaRequest(UInt32 pair, SInt32 requested, void* frame);
void OracleProbe_ArenaCommit(UInt32 pair, SInt32 reserved, void* address, const void* heap, void* frame);
// (MoreHeapSpace calls the heap's error callback)
void OracleProbe_ArenaCallback(void);

// The source nodes (`pv.source.*`): parse_statement has stored STATEMENT's expression, EXPRESSION.
void OracleProbe_SourceEmission(void* statement, void* expression);

// The scalarization bindings (`pv.scalar.binding`): IRO_ScalarizeClassDataMembers is about to make PART's temporary
// (create_temp_object, whose hook takes PART).
void OracleProbe_ScalarTemporary(void* part);

// The register counters (`pv.function.*`, `pv.category.*`, `pv.counter.change`). ROLE is the original's site's: 2
// close_temp_registers has raised RCLASS's counter to its last temporary, 3 check_temp_registers has rewound it, 4
// init_registers and 5 colorinstructions have reset it, 6 CodeGen_Generator begins FUNCTION and 7 ends it, 8
// set_last_exception_registers has set the last exception registers, 9 open_fe_temp_registers has opened the
// front-end temporaries.
void OracleProbe_Counter(UInt32 role, SInt32 rclass, void* function);

#define ORACLE_PROBE(call) call

#else

#define ORACLE_PROBE(call)

#endif

#endif
