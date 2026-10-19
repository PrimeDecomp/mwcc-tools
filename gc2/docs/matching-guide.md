# Matching MWCC-compiled code: a field guide

What this project learned matching CodeWarrior for GameCube 2.6 (`mwcceppc.exe`) byte for byte, boiled down to the
method and the compiler behaviour behind it, for anyone matching code built by Metrowerks' compilers. The
repository's own conventions and pitfalls are in [agents-notes.md](agents-notes.md).

**Which compiler each lesson is about.** mwcceppc.exe is an x86 program built by MWCC x86 (Pro 5/5.3/6, front end
about 2.4), so everything here was observed on MWCC x86 output. Each lesson is tagged:

- **[FE]**: front end or IR optimizer behaviour: parsing, inlining, statement counting, type rules, data layout,
  register allocation *order*. MWCC's targets share this code, so it most likely holds for mwcceppc's own PPC output
  too (the source is in this repo: `src/frontend`, `src/optimizer`, `src/backend`). Confirm it there before relying
  on it.
- **[x86]**: the x86 code generator (register names, `lea`/`movsx` choices, padding): specific to MWCC x86.
- **[LD]**: the linker (mwld): COMMON layout, section order, alignment.

**Decompiling another Metrowerks compiler** (mwccarm for DS, the Wii mwcceppc): sections 1 and 4 are the method. Check
which host compiler built each one first:
- mwcceppc 2.6 was linked by mwld 2.50 (PE linker version) and built with Pro 5/6.
- The Wii builds and every mwccarm build (2005-2011) were linked by 3.2; Wii's CodeView names its host compiler
  "Metrowerks CodeWarrior C/C++ x86 V3.2".

So the [x86] and [LD] lessons here are from an older host, likely close but unconfirmed for those.

mwccarm's front end is also a newer generation than 2.6's: CPrepLexer, CTemplateFuncInst, CManglerIA64, CIRStream,
ExpandIR. 122 of its 153 assert file names are also in Wii 1.0RC1, and only 77 in GC 2.6. For mwccarm the Wii
compilers' CodeView (publics, module tables, global types) is the name source and nearest reference; Pro7 and this
repo cover only the code that changed little (much of the Iro optimizer, parts of the front end).

---

## 1. Method

### 1.1 Start from the right text

1. **Start from the reference's verbatim text, then apply the target version's changes.** For this compiler the
   reference is Ninji's Pro7 decompilation. Pro7's text pasted verbatim matched dozens of functions that had been
   hand-shaped toward the disassembly, and their hand-shaped versions had needed casts, locals and workarounds. Before
   calling a function "register-only", put the reference body through `fn.py variants`. A sweep doing that for every
   function in a unit found 15 exact ones in CMachine alone.
2. **Look for decompiler tells.** IDA or Ghidra output that was cleaned up but not rewritten matches often, and is
   still not the original. The tells:
   - `int r` with `(r = reg) < N` / `++r`;
   - expanded macros (`(int)stype >= 4 && stype <= 14` for `IS_TYPE_VECTOR`);
   - raw constants where an enum or opcode name exists (`0x16`, `0x1a9`);
   - `x = f(); return x;`.
   Each of these was the last 0.4% of some function (move_assigned_argument went from 99.6 to exact).
3. **A function 30-40% similar in an otherwise exact unit has the wrong body.** It may be another function under
   this name, be missing a whole branch, or be a version addition. Read the original before shaping anything.
4. **The reference has bugs and placeholders of its own.**
   - Pro7 was matched against the PPC compiler, so its text cannot tell an early `return` from a jump to a shared
     epilogue.
   - It has wrong error ids and wrong allocation sizes.
   - Many of its names are Ninji's placeholders (`x14`, `unk*`, `Foo0..3`).
   Check the pushes and the call targets before calling a difference a version change.

### 1.2 The loop for one function

1. `fn.py diff NAME`: instruction by instruction, with references resolved to names. A line counts as `=` only when
   its references agree too. A wrong global, callee or large constant can hide inside "100% similar". Masking
   addresses once hid a `& ~4` that should have been `& ~0x40000004`, and a test of `copts.logopts` where
   `copts.gprfloatcopy` was wanted.
2. Compare with IDA statement by statement for a **real** difference: a wrong member, constant, callee or parameter
   type. Many near-misses were bugs, and about a third of the fixes in one round were.
3. Try the catalogued shapes (section 3). `fn.py variants NAME FILE` compiles alternatives in parallel and lists the
   other functions of the unit whose verdict changes, which shows inlining and deferred coupling.
4. `fn.py decl-order NAME --swaps` searches the declaration order of locals. Hoist block-local declarations first so
   it can move them.
5. Merge and split locals as a search. The original often has a fresh variable where the reference reuses one, and
   the reverse. A search will happily accept a merge with overlapping lifetimes at 98%, so check each one by hand.
6. **Record every attempt with its result**, failures included, in the ledger (`docs/nearmiss-2.6.md`). Attempts got
   lost between rounds until there was one place for them.
7. **Time-box it.** When the code is equivalent statement by statement and only registers differ, write down what
   you tried and move on. Most such cases were solved later by a fix somewhere else (a header type, a callee's
   prototype, a neighbour in a deferred unit).

### 1.3 Treat "needed" hacks as evidence

A shape that only matches through a hack (`volatile`, a cast, a dead local, a pointer-to-local, a `#pragma` around
one function) usually means a wrong type or shape somewhere else. Look one level out:

- the field it is stored to (`AccessType`, not `UInt8`);
- the callee's parameter type (`pragma_on_off_reset(UInt32)`);
- an inlined helper's prototype;
- the reference's own cast (`UInt32* value = (UInt32*)data` keeps `data` in its slot).

When the hack really is needed, keep it with a `TODO` saying what the original evidently did.

**Workarounds that turned out to be the original's code:**
- `if (0 && ...)` around a body that compiles to nothing yet is never inlined. The original had a body there behind
  a build-time constant (a target-ABI macro such as Mac/AIX power alignment, which EABI lacks). A function that needs
  `dont_inline`, or that is short a register against the reference, may be one of these: whole Mac/AIX branches of
  Pro7 compiled out by macros that fold to 0.
- `break;` after `return` in every case of a switch. It is dead, but it counts as a statement and keeps a function
  over the auto-inline limit (Pro7 does this 66 times).

### 1.4 The unit, not the function

Link progress counts a unit only once all of it links byte for byte. What blocks a unit whose functions are all
exact:

- **Its boundary.** Recover it from the asserts' file names, Wii CodeView module tables, 8-aligned starts of
  `.data` runs, dead functions' literals, and the unit's function-pointer tables.
- **Flags.** Check `-inline auto,deferred`, `-sym on/off`, `-O` level, `-bool`, `-RTTI` and `-inline off`
  (section 2.6).
- **Statics.** Their order, `static` against global, packing at the end of the file, and initialized against
  tentative definitions (section 2.4).
- **COMMONs** the original lacks, or COMMONs under names that break hash order (section 2.5).
- **Dead functions.** The linker drops them, but their literals and statics stay in the unit's data, and assert line
  numbers locate them.

`tools/relink.py VERSION --only SOURCE` links one unit and says where the bytes first differ. Read the result per
section: .text, .data and .bss each tell a different story.

---

## 2. Compiler behaviour

### 2.1 Inlining [FE]

- **The auto-inline limit counts statements, not size.** With `-inline auto`, a function without `inline` is inlined
  only if it has at most 15 counted statements (`CInline_IsSmallFunction` in Pro7; 2.6's own limit for the code it
  compiles is 30). At nesting level 1 the limit is 10, and at level 2 it is 7.
  - An if/else counts as IFNGOTO + GOTO plus its bodies; labels do not count.
  - A comma expression joins two statements into one. A ternary allocation counts as one.
  - **When the original inlines a static that ours keeps out of line, or the reverse, count its statements.** One
    statement makes the difference.
- **`inline` is often not the fix.** An `inline` function's out-of-line copy is emitted right after its first user,
  out of source order. MWCC also takes `inline` from a static's *first* declaration.
- **Block-scoped declarations keep a function from being inlined.** They also decide how far a recursive caller
  unrolls.
- **`static inline` drops the out-of-line copy** of a static inlined at every call. A plain static keeps one, along
  with its switch tables in .data. An "extra jump table" in a unit's data usually means a missing `static inline`.
- **An inlined helper's shape reaches its callers.**
  - The helper's parameter types (an enum against a `char`), its local order and its local types renumber registers
    in every caller.
  - A helper written `if (...) return 1; return 0;` materialises a value where `return a && b` branches.
  - A macro whose arms each `return` gives separate epilogues; a shared skip-and-return jumps to one.

### 2.2 Deferred inlining [FE]

- **`-inline auto,deferred`**: the whole file is parsed before any code is generated.
  - Functions come out in **reverse source order**, so a unit whose functions descend in address was built deferred.
  - A function defined later can be inlined into an earlier one.
  - Write such a unit in the reference's order and build it with the flag.
- **A recursive function inlines itself once.**
- **Neighbours are coupled.** A change that emits no code in one function (an unused local, `(void)0;`) can flip
  registers in the next function generated. When a function differs only by a reload, vary its neighbour in binary
  order and watch `variants`' "others:".
- **Literal order.** A deferred build creates a function's literal section when it generates the function, so a dead
  function's literal sitting before a live function's jump table says where the dead one was defined.

### 2.3 Types decide code [FE, register effects x86]

- **`int` and `long` differ**, though both are 32 bits.
  - A `long` local is computed in its stack slot, while an `int` goes through a register.
  - Mixing `int` and `long` adds a register copy.
  - A parameter's type swaps callee-saved registers in the function *and its callers*.
  - When only registers differ and declaration order does nothing, give locals, parameters and callees the
    reference's exact integer types.
- **`size_t` is not `UInt32`** (`unsigned int` against `unsigned long`). With `memcpy` declared, a `UInt32` size is
  copied to a second variable for the call.
- **Enum-typed fields are reloaded; byte fields are not.** MWCC does not reuse a value it loaded through an enum
  type. When the original reloads a field, the field is the enum. A `volatile` that "fixed" a reload means the type
  is wrong.
- **Byte-sized enums.** An enum whose values fit in a byte is a byte, and its enumerators are byte constants.
  - A plain literal where the original passes an enumerator needs a cast, or a parameter of the enum type.
  - Define flag enums as `1L << n`: one `0x80000000` enumerator makes the whole enum unsigned.
  - An enum's size and signedness come from all its values, so copy every enumerator when testing the reference's
    enum. Pro7's `Opcode` without its `PC_INVALID = -1` was an unsigned 2-byte enum and cost 56 exact functions; with
    it, the enum was signed and three functions changed, all exact again with the reference's prototypes.
- **`#define` constants and enumerators compile differently in comparisons.**
  - `(int) shortvar == K` gives `movsx` then a 32-bit compare. A bare `shortvar == DEFINE` gives a 16-bit compare.
  - Turning a set of `#define`s into an enum once cost 20 functions.
  - A halfword field compared with a byte enumerator gives `movzx; test` where `cmp word` was wanted.
- **`!x` and `x == 0` differ**, most at -O1. `!(a < b)` materialises a value where `a >= b` branches. The constant's
  signedness matters too: `x & ~0x80000000` is not `x & 0x7FFFFFFF`.
- **A parameter loaded in the prologue** (into a scratch register beside the others) was usually declared as the
  struct type the function dereferences, or as `void*` copied into a typed local.
- **Test integer widths one declaration at a time.** Changing a set of prototypes from `SInt32` to `int` together
  hides which one matters. A single callee parameter cost one caller over half its similarity (computedominators
  fell to 45.2 from `bitvectorintersect` alone), and a return type can follow the reference where its parameters
  cannot. A recorded "2.6 differs from the reference here" can also go stale: after unrelated header fixes, a
  `UInt32` once worth an exact function compiled byte-identical as the reference's `SInt32`. Re-test such
  departures in later rounds.
- **Varargs reporters take `short` ids**, and the callers' registers move when they are declared `int`.
- **A type one level out is the usual fix for a "fakematch".** Check the callee's prototype, the field's declared
  type, and the inlined helper's parameter. Pro7's `SInt32` field types fixed functions that `int` broke, and the
  reverse.

### 2.4 Statics and data layout [FE]

- **Statics are allocated in reverse declaration order.** Function-local statics take their function's place.
  - Use the .bss addresses to recover the declaration order.
  - When the functions' order gives the wrong order, the original declared the statics at file scope.
  - 2.6 declares statics near their first user, not in a block at the top of the file.
- **A tentative static (`static T x;`) and an initialized one (`static T x = 0;`) are laid out in different
  places.** Tentative statics come before the zero templates MWCC makes for local aggregate initializers
  (`UInt32 m[4] = {0};`). Explicitly zero-initialized statics come after them.
- **Measure packing per record.** Many `#pragma pack(push, 2)` blocks (inherited from the Mac's mac68k alignment)
  wrap records that lay out the same without them. Compare clang's `-fdump-record-layouts` with and without the
  pragma: offsets and sizes, and also `align=`. Narrowing the packing can raise a record's alignment from 2 to 4,
  which is harmless only while the record is never a global or a member of another record. A record with a byte
  member followed by pointers is where the packing is load-bearing.
- **An unknown member after a byte may be an alignment hole.** Under 2-byte packing a `UInt8` followed by an
  `SInt16` leaves one byte; a placeholder there is padding if nothing reads or writes it. Gaps too large for
  alignment, or between two byte members, are real fields.
- **The packing in force at the end of the file lays out .bss.** `#pragma pack(push)...pop` around the declarations
  does nothing. A unit with 2-byte-aligned statics needs `#pragma pack(2)` (or `options align=...`) still in effect
  at the end. A header that pushes twice and pops once leaks packing into every includer.
- **An unreferenced initialized static** is emitted at parse time, before a deferred unit's function literals.
- **Literals.**
  - MWCC emits a literal at its first use and pools equal literals within a unit.
  - Each dead function's literals stay in the unit's .data in definition order.
  - "Globals" generated from the original's .data are usually string literals at their uses.
- **A local aggregate with an initializer** is copied from a zeroed template in .bss.
- **A switch's jump table gives the order of its cases in the source.** Its targets, in case order, are the blocks'
  offsets. Explicit case labels that only fall into the error branch still fill the table.
- **Literal allocation sizes hide bugs.** A `galloc(10)` for a 0x16-byte struct overran the heap in the compiled
  compiler. Write `sizeof(T)` and check the original's `push`. Where the original pushes a literal smaller than
  `sizeof(T)`, your struct is too big.

### 2.5 Linking and COMMON [LD]

- **A non-`static` uninitialized global is COMMON**, even when written `= 0` in some forms. mwld lays out COMMON by
  the first object defining it, and within one alignment by **a hash of the name** (`split.py`'s `mwld_hash`).
  - Neighbouring real names must have increasing hashes, which checks a guessed name. Take the neighbours from the
    current symbols.txt: a bound from an older layout (a neighbour since renamed or removed) once ruled out
    the reference's real name (`cerror_synchdata`).
  - A COMMON the original lacks moves the whole COMMON tail and breaks every unit's relink at the tail's
    references.
- **Object sections start 8-aligned**, with exceptions after a 12-byte table: check the gap before trusting a cut.
- **One object, two files.** A function writing another unit's static means one object: only COMMONs are reachable
  across objects. Assert file names in .data tell `#include`d files apart from merged code.
- **The linker drops uncalled functions** but keeps their data, and a dead function calling a symbol the original
  lacks fails the relink.
- **Import thunks** (`jmp [__imp__X]` in .text) need their own symbols.

### 2.6 Flags are part of the shape

- **`-sym on`/`off` changes the code**, not only the debug info: registers in field-copy runs, spill choices, and a
  switch selector [x86 confirmed; FE suspected].
  - It applies per unit (the original build had a per-file debug column). A per-function `#pragma sym off` works,
    and a pragma right after a function's `}` already applies to that function.
  - Sweeping `-sym` and deferred settled several units before any hand-shaping.
- **The `-O` level and per-function COFF section alignment.** A unit's sections are 16-aligned under -O4,p and
  1-aligned under -O1 and -O4,s. A unit starting off the 16-byte grid is not -O4,p. -O1 units keep dead stores.
- **C++ units.**
  - A struct copy compiled as a call to an implicit `operator=` means `.cpp` with `-inline off`.
  - Runtime units need `-RTTI off`.
  - Driver units need `-bool off`, or `a && b` becomes byte arithmetic.
- **`-O4,s` packs 6-byte import thunks.**
- **Pragmas in the source can be stand-ins for flags.** A file-top `#pragma sym`, `scheduling off`, or a compiler
  override often stood in for the build's real flags.

### 2.7 Register allocation and spilling [x86 for the registers; FE for the order]

- **Registers are allocated in order of declaration and first use.** Hence decl-order searches. A variable's first
  use in an early loop can decide the whole function.
- **A swap that moves when you fix it means two variables, not one.** When the fix for one region's register swaps
  a pair in another region (a seesaw), the original declared fresh variables in both regions: a variable reused
  across regions (a switch's cases, two loops) is one candidate whose split fixes one side. vformatpcode needed its
  'T' case value and its 's' case loop counter both as case-block locals; each alone moved the swap. Search the
  splits in combination.
- **Frame slots are assigned top-down, and every local of 4 bytes or more is 4-aligned** (pro6, measured): `char
  buf[257]` sits at frame top - 260, and a later `char` goes into the 3-byte hole above it.
- **Address-taken locals are laid out ascending in declaration order.** Read the order off a call's `lea [esp+N]`
  pushes (minus the bytes already pushed). A wrong order shifts the whole frame and stalls every other search; a frame
  "8 bytes larger, locals missing" was register pressure once the order was right.
- **Stack slots show scope.** Function-level locals are laid out first in declaration order, and block-locals after
  them in ascending source order. Slots descending through a run of values are function-level declarations written
  in reverse. Address-taken locals sit at the top of the frame in declaration order.
- **A file-level static that is not address-taken is cached in a register across calls.** A global is not.
- **A value 2.6 reloads after every store, as if address-taken**, can sometimes be reproduced with `(void)&x`.
  Always look for what really took the address first. Several cases are still unexplained.
- **A parameter that is copied is reloaded after every store.** When a parameter stays in its home slot and the
  function copies it (`T* nd = param;`, or an inlined callee's parameter temporary), MWCC propagates the copy and
  then reloads the parameter from its slot after each store, where an uncopied one stays in a register between
  statements. DoTransformations24's "address-exposed" `nd` was this. It needs a parameter (copies of a spilled local
  change nothing), goes with `#pragma opt_propagation off`, and goes again with more copies or more uses (the
  parameter then gets a register).
- **Assignments that only exist to be assigned** change coloring even though nothing reads them again
  (`IS_TYPE_FUNC(type = obj->type)`). Sometimes the variable has to be one of the function's existing ones.
- **An argument assigned inside the call** (`f(t = x, ...)`) is loaded after the other pushes.
- **Comma expressions in place of two statements**, and chained assignments (`a = b = 0`), change scheduling and
  registers.
- **A parameter reused as a temporary** (`outputReg = OBJECT_REG(obj)` where the reference has a new local) gains
  uses and so an earlier callee-saved register; a swap of two parameters' registers from the prologue on can be this.
- **Operand order decides `lea` against an in-place update**: `a + b - 1` with `a` dead after it is `add a, b; dec a`;
  with the live operand first the result goes to a new register through `lea`.
- **A cast assignment is not copy-propagated** (`p = (T*)v` keeps a register copy of `v`; `p = v` folds away), and a
  conversion of a loop counter (`rows[(SInt8)ci]` with `char ci`) keeps IRO from strength-reducing the row pointers.
- **A memory variable is reloaded after every store once the host sees stores as aliasing the stack**: an
  address-taken local always is, and once any local's address escapes (to a call or a global) every spilled value
  is. A dead `&x` changes nothing. Without any escape in the code, this state has also been seen to switch with the
  mere number of locals in the function (rewrite_opassign: one or three extra unused locals, not two or four).

### 2.8 Control-flow shapes [FE]

- **Early `return` against if/else.** Several spellings each produce a different shape:
  - an early `return` where the reference has `if/else`;
  - `if (f()) { return TRUE; } return FALSE;` against `return f() != 0;`;
  - an inverted if/else;
  - an early `break` against `if (!result) {...}`.
- **The test of an assignment can sit in either place.** `if (!(x = f()))` and `x = f(); if (!x)` occur in both
  directions.
- **A test whose branch generates nothing survives only with an expression statement** as its body
  (`{ (void) 0; }`). An empty block, a lone `;` and an `&&` chain each drop it.
- **Loops.**
  - `limit > i` tests occur.
  - A loop entered with a jump to its test, even with constant bounds, has an `int` counter against an unsigned
    bound.
  - `while (1) { if (!c) break; ... }` is a loop tested only at the top.
  - `for (i = n; i--; p++)` and `while (i--) {...; p++;}` compile differently.
- **Assert spelling matters.** `if (c) FATAL`, `ASSERT(c)`, an expression-form assert and a do/while each compile
  differently. A stringifying assert macro must survive formatting, or the string bytes change.
- **A dead `jmp` after the last `ret` is an emptied loop preheader**: MWCC hoisted something that cost nothing.
- **A block of `+` lines in a diff** is usually a call or check the reference lacks. Read the original there.

### 2.9 Misc traps

- **An undefined macro still compiles.** MWCC treats a missing `MACRO(x)` as an implicit call, so deleting or moving
  a macro silently breaks code. Run a clang syntax check after moving code; implicit declarations are bugs.
- **Header edits move code in functions that use none of them.** Declaration counts and even the include
  directory's name length have flipped functions. A function that matches only under one set of declarations does
  not have its original source yet: look for its wrong type rather than shaping headers around it.
- **A deferred unit that `#include`s a .c file** compiles that file's functions after the whole unit is parsed, so
  declarations anywhere in the unit, even unused ones, can flip them. Include it where the reference does (after
  every header).
- **Dropping includes renumbers MWCC's anonymous `@N` literal labels** in the unit's .data. A symbols.txt diff after
  an include cleanup is expected and does not affect linking.
- **A dropped prototype changes the calling convention silently.** Trimming a header that declared a `__stdcall`
  function leaves its callers with an implicit cdecl call. Calling conventions come from the defining unit's exact
  code, not from the Wii compilers' decorations (Wii calls 2.6's cdecl Resources functions `__stdcall`).
- **Braces change code rarely**, but not never (one body in 3165).
- **A name matched by a trivial body is a guess.** MSL defines `fgetc`/`getc` and `atexit`/`__atexit` with one body.
- **The host build's own bugs reproduce.** 2.6 was compiled against Pro 6's MSL headers and linked with Pro 5's
  library, so its `errno` reads the wrong word of the thread-local data.

---

## 3. Recurring near-miss patterns, and what fixed them

| Symptom | Usual cause |
|---|---|
| One register pair swapped, everything else equal | declaration order; `int` against `long`; a helper's parameter type; a dead assignment |
| Registers renumbered across the whole function | an inlined helper's shape; a different function inlined; a callee's prototype |
| Original reloads after a store | an enum-typed field; a value whose address is taken somewhere; a spilled pointer parameter; a callee's parameter type (`short` against a signed 2-byte enum) |
| Original keeps a value in a stack slot | a `long` local; a CSE temporary for `(size_t)x`; address taken |
| One callee-saved register more or fewer | a `short` compared without `(int)`; a parameter's integer type; compiled-out code |
| Frame 4-8 bytes larger | locals missing (a split variable, a parameter copy, an initialized aggregate) |
| Extra or missing jump table in .data | a `static` that should be `static inline`; dead functions; cases in another order |
| "100% similar" but bytes differ | a wrong reference (callee, global, constant): read the `~` lines |
| Function out of line where the original inlines it (or the reverse) | the statement count; block scopes; `inline` on the first declaration |
| Neighbour flips when you change this one | a deferred unit; vary in binary order |

---

## 4. The toolkit in this repo

Every tool is a command over `tools/mw/` and prints its usage with `--help`; the README lists them. The ones this
guide relies on: `fn.py show|diff|variants|decl-order` (one function), `unit.py check`, `status.py --todo`,
`relink.py --only` and `layout.py` (one unit), `headers.py bisect` and `includes.py trim|fix` (headers),
`evidence.py`, `bindings.py refs [--wii]` and `rename.py` (names), `equivalent.py` and `clang_check.py` (behaviour),
`update_assert_lines.py` (asserts).

### 4.1 Sources of truth, best first

1. The original's bytes, read with relocations: never trust a decompiler's constants or arguments. IDA treats some
   calls as noreturn, drops code after them, and drops arguments.
2. The reference source's verbatim text.
3. A later build's CodeView: public names, module tables and global types.
4. Assert line numbers. A constant offset from the reference's lines over a span means the reference's text is
   there, dead functions included.
5. Strings in .data, and data layout.
6. The ledger: what has already been tried.
