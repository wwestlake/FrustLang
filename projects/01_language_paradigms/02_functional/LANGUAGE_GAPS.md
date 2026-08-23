# Frust: known language gaps - numbered, tracked, in attack order

**Standing instruction, the thing that was missing before**: update an
item's `Status` line the moment it changes - when work starts, mark it
`IN PROGRESS`; when it's built and verified, mark it `DONE` with the
commit/date; if something makes it moot, mark it `CLOSED - moot` with
why. This document was written once (2026-08-20) and never touched
again despite real progress happening underneath it (`own`/`raw` heap
struct construction shipped with zero update here) - that gap in the
process, not the document's existence, was the actual problem. Don't
repeat it.

Order below is dependency-driven (what unblocks what), not severity-
ranked - decided 2026-08-22, explicitly the assistant's call to make
("I am not going to dictate order, you find the best order").

## CLOSED - moot

**No platform-conditional compilation.** Originally blocked
`thread.fr`/`mutex.fr`/`procspawn.fr` from having a Linux/pthread
counterpart alongside their Win32 implementation. Moot as of the
Windows-only standing directive (`AGENTS.md`, 2026-08-22) - there is no
Linux counterpart to conditionally compile for. Not being built.

## 1. Pointer arithmetic + raw dereference (`*ptr`, `ptr + n`)

**Status: OPEN - up next.**

The most foundational open gap. Collections (#3) need indexed pointer
access; closures (#6) need captured-environment access. Already proven
to actively block real work this session - `metrics.frust` (the IDE's
real code-metrics plugin) needed a host-side C++ helper
(`host_count_occurrences`) specifically because counting repeated
substring occurrences in pure Frust means re-searching text past a
previous match, which needs advancing a search position, which needs
this.

Current state (`Codegen.h`, `compileUnary`'s `UnaryOp::Deref` case,
~line 954): prints "codegen does not support pointer dereference yet"
and bails - fails loudly rather than silently misbehaving, a real
asset to build on, not a bug to route around.

Design: Frust's opaque-pointer LLVM strategy means a raw pointer's
LLVM type carries no pointee-type info (the same reason
`namedValueStructType` exists as a side table for structs, since a
struct pointer's real identity is otherwise unrecoverable once behind
`PointerType::getUnqual`). `*ptr` needs the pointee type from the
STATIC Frust type (`TypeExpr::isRawPointer`/`.name` for `raw* T` -
already parsed, zero codegen). Plan: a new side table
(`namedValueRawPointeeType`, mirroring `namedValueStructType`)
populated wherever a `raw* T`-typed `let`/param is bound; `*ptr` reads
it to know the `CreateLoad` type; `*ptr = val` (deref-assignment,
`compileAssign`) does the matching `CreateStore`; pointer + integer
arithmetic as a new case in `compileBinary` when one operand's
declared type is a raw pointer (GEP-based - byte vs. element stride is
a real design call made during implementation).

## 2. Struct-return type tracking

**Status: OPEN - right after #1.**

`inferStructTypeName` (`Codegen.h`, ~line 306) explicitly doesn't
handle `ExprKind::Call` - a struct-returning function's result loses
its type, so `let c: Counter = make_counter(); c.method()` fails with
"cannot call a method on an expression of unknown struct type"
(documented workaround already in the frust-library submodule -
`random.fr`'s header requires direct struct-literal construction
instead of a factory function because of exactly this).

Now genuinely small to close: this session's `functionDeclsByName`
registry (built for interface-typed parameters) already maps a call's
target function to its real declared `FunctionDecl`, including its
return `TypeExpr`. `inferStructTypeName` just needs a new
`ExprKind::Call` branch that looks the callee up in that registry and
checks whether its return type names a known struct. High ergonomic
payoff (factory-function patterns work) for a small, contained fix -
worth doing early so later features don't have to route around it.

## 3. Growable collections (a real `Vec<T>`/dynamic array)

**Status: OPEN.**

The single biggest "is this a real language" unlock. Built on #1
(indexed pointer access) and `mem.fr`'s already-existing, already-
verified `alloc`/`realloc`/`dealloc` (extern fn wrappers around
malloc/realloc/free). Ships as a compiler-intrinsic-style built-in
FIRST (same precedent `Vec<N>`/`resolveType` already sets for a
special-cased generic name), rather than waiting on full user-
definable generics (#4) - gets a real, useful container into use fast.

Resolves `Vec<N>`'s existing name collision as part of this work:
`Vec<N>` today is a fixed-size, compile-time-`N` **float** vector for
linear algebra (`dot`/`length`/`normalize` builtins, a genuine LLVM
vector type) - not a general array, a naming trap that already caused
one wasted investigation this session before being understood. A real
decision (new name for the growable kind, or rename the math one) gets
made during implementation, not pre-decided here.

## 4. Generics (real, user-definable types/functions)

**Status: OPEN.**

The largest single item - a genuine type-system feature
(monomorphization vs. type erasure is a real, consequential choice for
implementation time, not decided here). Confirmed current state: the
grammar's `<...>` (`type_generic_args_opt`) only ever consumes type
arguments for hardcoded special cases (`Vec<N>`, interfaces) -
`struct_decl`/`function_decl` have no type-parameter list in the
grammar at all. Needed properly for #5 and for a general `Vec<T>`
beyond #3's built-in special case.

## 5. Result/Option (structured error handling)

**Status: OPEN. Depends on #4.**

Failures today report to stderr and return a sentinel (null `String`,
`-1`, `0`) - the caller has to already know the convention for
whatever it's calling. Real `Result<T, E>`/`Option<T>` needs generics
(#4). Lands with real breaking-change implications for
`06_frust_library/core`, which uses the sentinel convention throughout
today - named here explicitly so that's not a surprise later.

## 6. Closures

**Status: OPEN.**

Only the named-function + packed-buffer pattern works today (see
`core/src/task.fr`/`curry.fr`) - a function's address can be taken and
called back indirectly (`compileIndirectCall`/`compileTypedIndirectCall`),
but there's no way to capture surrounding variables into an anonymous
callable. Needs a heap-allocated captured-environment representation,
a fat pointer (code address + environment address), and a calling
convention that passes the environment implicitly - benefits from #1's
pointer work and this session's already-proven heap-struct-literal
pattern (`compileHeapStructLiteral`, built for `own`).

## 7. `own`/`shared`/`weak` smart pointers (real reference counting / move semantics)

**Status: PARTIAL - `own` heap allocation has real codegen
(`compileHeapStructLiteral`, shipped this session, 2026-08-21).
`shared`/`weak` (reference counting) remain zero-codegen, parsed only
(`SmartPtrKind` in `frust.y`'s `type_ptr_prefix_opt`).**

The SAFETY layer - sequenced after #1/#6 deliberately, so it's
designed once raw-pointer patterns are well-understood in practice,
not designed in the abstract first. A real implementation needs
reference counting (`shared`) and move semantics (`own`, for the parts
not already covered) wired through the whole codegen path -
substantial, standalone work even with #1-#6 done first.

## 8. Multi-file plugins (`frust_plugin_host`)

**Status: OPEN. Tooling/DX, not a core language gap - lower priority
than 1-7.**

`frust_plugin_load()` reads exactly one `.frust` file - no
`use self::`-style multi-file plugin support (unlike frate pods, which
do support this via an explicit compiled file list in `lib.fr`).
Confirmed directly: `ModuleLoader.cpp`'s `ResolveImports` treats
`use self::X` as a no-op unless frate passes the referenced file in as
a separate compiler argument - `frust_plugin_load` has no such
mechanism. Real gap for any plugin bigger than one file.

## 9. Bool-across-FFI convention

**Status: OPEN - minor, contained either way.**

No prior example anywhere in this codebase had ever returned a Frust
`bool` (LLVM i1) across the C ABI to a host before this session -
building the IDE's four real plugins (`linter`/`metrics`/
`plugin_generator`/`symbol_finder`, 2026-08-22) deliberately avoided
it (untested register-width ABI behavior) in favor of `i64` (0/1)
everywhere, matching every other host-visible boolean result in this
codebase. Either formalize `i64` (0/1) as the documented convention
for anything crossing the FFI boundary, or actually verify/fix the i1
ABI lowering properly and lift the restriction. Minor either way -
named so it isn't silently rediscovered again.
