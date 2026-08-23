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

**Status: DONE - 2026-08-23.** `*ptr` read/write and `ptr + n`/`ptr - n`
element-stride arithmetic both have real codegen now. Also found and
fixed a related pre-existing gap while implementing this:
`resolveType` never checked `TypeExpr::isRawPointer` at all, so
`raw* i64` previously resolved to plain `i64` (the pointee's VALUE
type) instead of a pointer - would have made every operation this item
adds operate on the wrong LLVM type. Fixed as part of this same
change (`resolveType` now returns a pointer type immediately when
`isRawPointer` is set, before alias resolution or anything else).

Scope actually shipped, honestly stated: `*expr`/`*expr = val` only
work when `expr` is a plain named variable or parameter (an
`inferRawPointeeTypeName` lookup, mirroring `inferStructTypeName`'s
"named binding" convention) - dereferencing a compound expression
directly (e.g. `*(ptr + 1)` inline, with no intermediate `let`) isn't
supported; bind the arithmetic result to an explicitly `raw* T`-typed
`let` first, same convention already established for structs. Pointer
arithmetic supports `ptr + n`/`n + ptr`/`ptr - n`; `ptr - ptr`
(pointer difference) and `n - ptr` are not supported (would need
sizeof-based division, a separate, not-yet-needed feature).

Verified (`test_pointer_arithmetic.frust`, run via `frust_compiler.exe`
directly): a single `raw* i64` write-then-read round-trip (`*p = 42`,
confirmed `*p == 42`), and three independently pointer-arithmetic-
derived pointers (`buf`, `buf + 1`, `buf + 2`) into a real 3-element
buffer, each written a distinct value and read back correctly at its
own offset (100/200/300, not all landing on the same slot) - real,
hand-predicted exit code (0 = every check passed) matched exactly.
Full existing `frust_plugin_host` regression suite (every example
harness) and the JUCE IDE both rebuilt and re-verified clean - this
touched `resolveType`/`compileUnary`/`compileAssign`/`compileBinary`,
core codegen paths used everywhere else in the language, so a full
regression pass (not just the new test) was the actual bar for "done."

Why it was picked first: the most foundational open gap - collections
(#3) need indexed pointer access, closures (#6) need captured-
environment access - and already proven to actively block real work
this session before it was fixed (`metrics.frust`, the IDE's real
code-metrics plugin, needed a host-side C++ helper specifically
because counting repeated substring occurrences in pure Frust means
re-searching text past a previous match, which needs advancing a
search position, which needed this).

## 2. Struct-return type tracking

**Status: DONE - found already closed, 2026-08-23, while starting
work on #1.** Was believed open based on the 2026-08-20 audit (which
never re-checked the actual code, exactly the process gap this
document's rewrite is meant to fix). Direct re-read of
`inferStructTypeName` (`Codegen.h`, ~line 315) found it now DOES
handle `ExprKind::Call` (a dedicated branch, `functionDeclsByName`
lookup, checks the callee's declared return type against
`structTypes`) - confirmed via `git log -S` to have landed in commit
`1512cf2` ("Real heap-allocated struct construction (own/raw)"),
earlier this same session, as a needed side effect of that work (a
heap-allocating constructor function like `automation.fr`'s
`new_ramp`/`new_decay` needed its return type tracked for correctness).
Scoped to free-function calls only (not method-call results - a
narrower scope than "fully general," named honestly in the code
comment itself, not silently overclaimed). No further work needed for
this item; left numbered here as a record that it's closed, not
renumbered away.

## 3. Growable collections (a real `Vector<T>`/dynamic array)

**Status: DONE - 2026-08-23.** Shipped as a compiler-intrinsic-style
built-in, exactly as scoped below - real generics (#4) not needed for
this. Naming resolved cleanly: `Vector<T>` was already the name the
spec doc (`FRUST_LANG_SPEC.md` section 2.3) and two separate code
comments anticipated for this ("the unimplemented growable-collection
`Vector<T>`") - no collision with `Vec<N>` (the fixed-size float math
vector) to resolve at all, and no rename needed either.

Representation: a `Vector<T>` value is a pointer to one shared,
element-type-erased heap header (`vectorHeaderType()`:
`{ ptr data, i64 length, i64 capacity }`) - T only matters for
computing element size/stride at each push/get/index site (the same
"opaque pointer erases identity, only the static type ever knew"
reasoning as `namedValueStructType`/`namedValueRawPointeeType`, now
joined by `namedValueVectorElementType`). `Vector::new()` is special-
cased in the `Let` branch of `compileExpr`, not in `compileCall` -
Frust has no real generic function syntax, so `Vector::new()`'s call
site has no way to know T on its own; only the enclosing `let`'s type
annotation (`let v: Vector<i64> = Vector::new();`) ever carries it.
`malloc`/`realloc` are wired directly by the compiler
(`module.getOrInsertFunction`, same pattern already used for `printf`
in `compileProgram`'s own prologue) - Vector<T> doesn't depend on the
user's own source declaring `extern fn malloc`.

Shipped: `.push(x)` (real amortized growth - doubles capacity from a
base of 4, `realloc`-backed), `.len()`, `.get(i)`, and `v[i]` bracket
read. Scope cut, named honestly: `v[i] = x` (bracket WRITE) isn't
shipped this pass - `.push()` covers building a vector, `.get()`/`v[i]`
cover reading it back; in-place element mutation via brackets is a
real, deliberately-deferred follow-on, not silently dropped.

Verified (`test_vector.frust`, `frust_compiler.exe` direct-run): an
empty vector starts at length 0; five pushes land at length 5,
exercising BOTH the initial grow-from-0 (capacity 0 -> 4 on the 1st
push) and the regrow-past-4 (capacity 4 -> 8 on the 5th push) code
paths, not just the easy no-growth case; `.get(0)`/`.get(2)`/`.get(4)`
and `v[0]`/`v[4]` all read back the correct values at the correct
offsets. Real, hand-predicted exit code matched exactly. Full existing
`frust_plugin_host` regression suite and the JUCE IDE both rebuilt and
re-verified clean.

Originally scoped (kept for the record):

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

**Status: PARTIAL (structs) - 2026-08-23.** Generic STRUCTS are real -
`struct Box<T> { value: T }`, `struct Pair<A, B> { first: A, second: B
}` - monomorphized (real per-instantiation LLVM struct types, not type
erasure/boxing), matching the project's own stated "zero-overhead,
aiming for the iron" philosophy (`FRUST_LANG_SPEC.md` section 2.1).
Generic FREE FUNCTIONS and generic `impl` methods are explicitly OUT
OF SCOPE for this pass - a real, separate, larger follow-on (see
below), not silently folded in.

New grammar: `struct Name<T>` / `struct Name<A, B>` (`generic_params_opt`,
a bare identifier list - distinct from `type_generic_args_opt`, which
CONSUMES type arguments at a use site like `Box<i64>`; this DECLARES
the parameter names). `StructDecl` gained `genericParams`.

Codegen: a generic struct's bare name is deliberately never eagerly
resolved to an LLVM type (`indexStructs` now skips it, storing the
template in `genericStructTemplates` instead) - only concrete
instantiations are, monomorphized lazily on first real use
(`getOrCreateMonomorphizedStruct`, memoized under a mangled name like
`"Box<i64>"` in the SAME `structTypes`/`structFieldIndex` maps every
other struct already lives in, so field access/method calls/sizeof
all work with zero further special-casing). `resolveType` triggers
monomorphization for a type annotation like `Box<i64>`; struct-literal
CONSTRUCTION (`Box { value: 42 }`, or `own Box { value: 42 }`) is
special-cased in the `Let` branch of `compileExpr` - same "anchor via
the enclosing let's type annotation" convention already established
for `Vector::new()` (#3) and raw pointers (#1), since the literal's
own syntax never carries the concrete type arguments itself.

Scope cuts, named honestly:
- A generic struct literal used as a bare sub-expression with no
  enclosing typed `let` (e.g. passed directly as a function argument)
  is not supported - the concrete type arguments have nowhere else to
  come from yet.
- Nested generics (a field typed `Vector<T>` inside `struct Box<T>`)
  are not substituted - `resolveType` would resolve `T` as an unknown
  name and fall through to `i64`. Real, deferred limitation.
- Generic free functions/methods (`fn identity<T>(x: T) -> T`,
  `impl<T> Box<T> { ... }`) - not started. `struct_decl`/`function_decl`
  still have no type-parameter list in the grammar for functions/impls,
  only for `struct_decl` now.

Verified (`test_generics.frust`, `frust_compiler.exe` direct-run): two
DIFFERENT instantiations of the same generic struct (`Box<i64>` and
`Box<f32>`) each construct and read back correctly - proves genuine
per-type monomorphization, not one hardcoded case; a two-type-parameter
struct (`Pair<i64, f32>`, the real stand-in shape for #5's eventual
`Result<T, E>`) with two different field types also correct. Real,
hand-predicted exit code matched exactly. Full existing
`frust_plugin_host` regression suite and the JUCE IDE both rebuilt and
re-verified clean.

## 5. Result/Option (structured error handling)

**Status: PARTIAL - 2026-08-23.** `Result<T, E>`/`Option<T>` now exist
as real, usable generic structs -
`06_frust_library/core/src/result.fr`/`option.fr` - built as real
Frust code using #4's generics, not another compiler intrinsic like
`Vector<T>`: the first genuinely useful thing built on top of generics,
not just a synthetic test of the feature.

```frust
struct Option<T> { has_value: bool, value: T }
struct Result<T, E> { is_ok: bool, ok_value: T, err_value: E }
```

Found and fixed a real, would-be-silent gap while verifying this:
`resolveStructTypeName` (what item #2's struct-return-type-tracking
fix relies on) only ever checked a struct's BARE name against
`structTypes` - for a function returning a GENERIC struct
(`fn safe_divide(...) -> Result<i64, String>`), that lookup would fail
(only the MANGLED instantiation, `"Result<i64,String>"`, is ever in
`structTypes` - see #4), silently breaking field access
(`.is_ok`/`.ok_value`) on the returned value with no clear error.
Fixed by having `resolveStructTypeName` monomorphize and return the
mangled name too, mirroring `resolveType`'s own generic-struct branch.

Honest limitations, stated directly rather than worked around: no
`Result::ok(x)`/`Option::some(x)` constructor sugar (needs generic FREE
FUNCTIONS - explicitly out of scope for #4's structs-only pass this
session) - construct via a direct struct literal instead
(`Result { is_ok: true, ok_value: 42 }`); no enforced safety against
reading `.ok_value` on an `Err`/`.value` on a `None` (same "no
default-value story, caller's responsibility" stance every struct
literal already has in this codebase). `06_frust_library/core`'s
existing sentinel-return convention (null/-1/0) is UNCHANGED - adopting
`Result`/`Option` throughout that library is real, separate, not-yet-
started follow-on work, named here so it isn't a silent surprise.

Verified (`test_result_option.frust`, `frust_compiler.exe` direct-run):
`safe_divide(a, b) -> Result<i64, String>`, a real function returning a
generic struct, exercised on BOTH the Ok (10/2) and Err (10/0)
branches, correct in each case; `Option<i64>` exercised on both Some
and None. Real, hand-predicted exit code matched exactly. Full
existing `frust_plugin_host` regression suite and the JUCE IDE both
rebuilt and re-verified clean.

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

---

## Future features (NOT gaps - queued after all 9 items above close)

User's own distinction, explicit: these are new language features the
user WANTS, not foundational things missing from what already exists -
kept in a clearly separate section so they don't get conflated with
the numbered gap-closing sequence above, and not started until that
sequence is done.

### Pattern matching + parameter/destructuring unpacking (F#-style)

**Status: QUEUED - not started, not numbered into 1-9 above.**

Confirmed by direct grammar read, 2026-08-23: no `match`/`case`/`when`
keyword exists anywhere in `frust.y`'s token list - the only branching
construct is `if`/`else` (including `else if` chains). No
destructuring anywhere either: `let` bindings (`"let" mut_opt IDENT
type_annot_opt "=" expr`) always bind a single identifier, never a
tuple/struct-shaped pattern (`let (a, b) = pair`, `let { x, y } =
point`); function parameters (`param: IDENT ":" type_expr`) are the
same - always one name, one type, never a destructured shape; same for
`for` loop variables. A real, separate feature from anything in the
numbered list above - not blocking any of items 1-9, and not blocked
by them either, just deliberately sequenced after.
