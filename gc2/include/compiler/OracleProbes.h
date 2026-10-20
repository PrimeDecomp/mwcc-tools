#ifndef COMPILER_ORACLEPROBES_H
#define COMPILER_ORACLEPROBES_H

#include "compiler/common.h"

// The oracle's probes. The oracle built from these sources (ORACLE_PROBES) reports here what the
// oracle linked from the original's code reports from probes at its instructions, with the same
// records. Each call names the original's site. Without ORACLE_PROBES, ORACLE_PROBE is nothing.

#ifdef ORACLE_PROBES

// The arenas (`pv.arena.*`). PAIR is the allocation's request and commit sites in the original: 1
// to 4 galloc, lalloc, aalloc and oalloc, 5 InitNameHash, 6 and 7 GetHashNameNodeExport's two, 8
// and 9 GetHashNameNode's. FRAME identifies the allocating function's frame: a request and its
// commit give the same.
void OracleProbe_ArenaRequest(UInt32 pair, SInt32 requested, void* frame);
void OracleProbe_ArenaCommit(
    UInt32 pair, SInt32 reserved, void* address, const void* heap, void* frame
);
// (MoreHeapSpace calls the heap's error callback)
void OracleProbe_ArenaCallback(void);

// The source nodes (`pv.source.*`): parse_statement has stored STATEMENT's expression, EXPRESSION.
void OracleProbe_SourceEmission(void* statement, void* expression);

// The scalarization bindings (`pv.scalar.binding`): IRO_ScalarizeClassDataMembers is about to make
// PART's temporary (create_temp_object, whose hook takes PART).
void OracleProbe_ScalarTemporary(void* part);

// The functions in the order their bodies are expanded (`inl.expand`): CInline_Expand is about to
// expand FUNCTION's (expanding_function).
void OracleProbe_Expand(void* function);

// The PCode before the first scheduling pass (`st.presched`, MWCC_ORACLE_STAGES): FUNCTION's.
void OracleProbe_Presched(void* function);

// The optimizer's dump after the phase NAME (MWCC_ORACLE_IRO_PHASES): whether to dump it, where
// IRO_DumpAfterPhase was told DUMP.
unsigned char OracleProbe_DumpPhase(const char* name, unsigned char dump);

// Copy propagation (`cp.*`, MWCC_ORACLE_COPYPROP): copypropagatestouse has decided whether the copy
// COPY propagates into USE (ACCEPTED), by the test REASON (0 the use is a move, 1 it reads and
// writes the register, 2 the source is redefined between them, 3 the copy does not reach the use's
// block, 4 the source is redefined in that block, 5 none failed); propagateandremovecopy has
// REMOVED COPY, or kept it.
void OracleProbe_CopyUse(void* copy, void* use, int reason, int accepted);
void OracleProbe_CopyRemove(void* copy, int removed);

// The peephole (`ph.*`, MWCC_ORACLE_PEEPHOLE): installpattern has installed FUNC, NAME; FUNC is
// about to try INSTRUCTION, and has changed it (or removed it); INSTRUCTION was dead.
void OracleProbe_PeepholePattern(void* func, const char* name);
void OracleProbe_PeepholeTry(void* instruction, void* func);
void OracleProbe_PeepholeHit(void* instruction);
void OracleProbe_PeepholeDead(void* instruction);

// The alias analysis (`al.*`, MWCC_ORACLE_ALIAS; optimizer/Alias.c): gather_alias_info has left
// FUNCTION's alias table (COLORING: as coloring is, COUNT aliases), each alias INDEX of TYPE (0 an
// object, 1 a part of one, 2 a set) with its OBJECT, OFFSET and SIZE and, for a set, its MEMBERS
// (a bit vector over the COUNT indices), and each instruction with an alias, PCODE, and its alias
// INDEX. At level 2, a query: whether A may alias B, or the object OBJECT (KIND 0 may_alias, 1
// may_alias_scheduling, 2 may_alias_object), and its RESULT.
void OracleProbe_AliasBegin(void* function, int coloring, int count);
void OracleProbe_Alias(
    int index, int type, void* object, SInt32 offset, SInt32 size, const UInt32* members
);
void OracleProbe_AliasAccess(void* pcode, int index);
void OracleProbe_AliasEnd(void);
void OracleProbe_AliasQuery(int kind, void* a, void* b, void* object, int result);

// The value numbering (`vn.*`, MWCC_ORACLE_VN; optimizer/ValueNumbering.c): removecommonsubexpressions
// begins FUNCTION's pass (AGGRESSIVE: its flag), an extended basic block at BLOCK, the block BLOCK and
// the instruction PCODE, and ends (REMOVED: whether it removed any); a load or CSE operation's
// OUTCOME (0 not a candidate, 1 an available value's, OTHER, is reused, 2 the store OTHER's value is,
// REASON its kind, 3 it is kept, for REASON: see ValueNumbering.c's oracle_kept), with its alias
// ALIAS (-1: none); the instruction PCODE kills the memory value of the alias ALIAS, or every value.
void OracleProbe_VnBegin(void* function, int aggressive);
void OracleProbe_VnExtendedBlock(void* block);
void OracleProbe_VnBlock(void* block);
void OracleProbe_VnInstruction(void* pcode);
void OracleProbe_VnDecision(void* pcode, int outcome, int reason, void* other, int alias);
void OracleProbe_VnKill(int alias, void* pcode);
void OracleProbe_VnKillAll(void* pcode);
void OracleProbe_VnEnd(int removed);

// The PCode at each stage the compiler's debug listing names (`st.stage`, MWCC_ORACLE_LISTING): the
// sites that list the blocks under copts.debuglisting list them under ORACLE_LISTING(copts.debuglisting)
// too, which is also true when the trace asks (OracleProbe_Listing), and pclistblocks (a stub in 2.x)
// reports FUNCTION's blocks after STAGE.
int OracleProbe_Listing(void);
void OracleProbe_ListBlocks(void* function, const char* stage);

// The register counters (`pv.function.*`, `pv.category.*`, `pv.counter.change`). ROLE is the
// original's site's: 2 close_temp_registers has raised RCLASS's counter to its last temporary, 3
// check_temp_registers has rewound it, 4 init_registers and 5 colorinstructions have reset it, 6
// CodeGen_Generator begins FUNCTION and 7 ends it, 8 set_last_exception_registers has set the last
// exception registers, 9 open_fe_temp_registers has opened the front-end temporaries.
void OracleProbe_Counter(UInt32 role, SInt32 rclass, void* function);

// The values the next counter increments bind their births to, at the original's sites that do
// (`pv.value.object`, `pv.value.materialization`, `pv.value.array`, `pv.value.call_result`): an
// object and its VarInfo (assign_register_to_variable, assign_GPR_pair), an operand and its object
// (the symbol's address in Operands.c), an array's promotion and the element's index
// (LoopOptimization.c), or a call, its result's operand and its type (call_function).
void OracleProbe_Binding(const void* a, const void* b, const void* c);

// The register counters' increments (`pv.value.birth`; ALLOC_REG, include/compiler/Registers.h):
// the write at LINE of FILE has incremented RCLASS's counter from OLD. Returns OLD.
SInt32 OracleProbe_CounterIncrement(const char* file, int line, SInt32 rclass, SInt32 old);

// A call the original inlines (`#define f(...) ORACLE_INLINED(f(__VA_ARGS__))` after f's
// definition, or ORACLE_INLINED(f(...)) before it): its counter increments have the original's
// sites at that call, LINE of FILE (the macro's closing parenthesis's), until it returns.
int OracleProbe_InlinedBegin(const char* file, int line);
void OracleProbe_InlinedEnd(int* depth);
#ifdef __wasm__
// Whether the run traces: the WebAssembly module (crates/mwcc-wasm) has the probes in every run, and a run that
// does not trace skips them (wasm/src/oracle.c sets it).
extern int OracleTrace_On;
#define ORACLE_TRACING OracleTrace_On
#else
#define ORACLE_TRACING 1
#endif

#define ORACLE_INLINED(call) \
    (ORACLE_TRACING ? ({ \
        int oracle_inlined __attribute__((cleanup(OracleProbe_InlinedEnd))) = \
            OracleProbe_InlinedBegin(__FILE__, __LINE__); \
        call; \
    }) : (call))

#define ORACLE_PROBE(call) \
    do { \
        if (ORACLE_TRACING) { \
            call; \
        } \
    } while (0)

#define ORACLE_LISTING(listing) ((listing) || (ORACLE_TRACING && OracleProbe_Listing()))

#ifdef __wasm__
// A function's scope in the trace (wasm/src/oracle.c): its inline expansion and code generation, between
// ORACLE_SCOPE(1) and ORACLE_SCOPE(0). The host keeps a scope's records only for the functions a run asks for
// (MWCC_ORACLE_FUNCTIONS, crates/mwcc-wasm).
void OracleTrace_Scope(int entering);
#define ORACLE_SCOPE(entering) ORACLE_PROBE(OracleTrace_Scope(entering))
#else
#define ORACLE_SCOPE(entering)
#endif

#else

#define ORACLE_PROBE(call)
#define ORACLE_LISTING(listing) (listing)
#define ORACLE_INLINED(call) call
#define ORACLE_SCOPE(entering)

#endif

#endif
