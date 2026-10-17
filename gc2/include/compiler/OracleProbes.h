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

#define ORACLE_PROBE(call) call

#else

#define ORACLE_PROBE(call)

#endif

#endif
