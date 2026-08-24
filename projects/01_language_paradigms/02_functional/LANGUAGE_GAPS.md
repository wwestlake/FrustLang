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

**Status: PARTIAL (structs + free functions; methods still open) -
2026-08-24.** Generic STRUCTS are real - `struct Box<T> { value: T }`,
`struct Pair<A, B> { first: A, second: B }` - monomorphized (real
per-instantiation LLVM struct types, not type erasure/boxing),
matching the project's own stated "zero-overhead, aiming for the iron"
philosophy (`FRUST_LANG_SPEC.md` section 2.1). **Generic FREE
FUNCTIONS are now real too**: `fn identity<T>(x: T) -> T`, called with
explicit turbofish type arguments (`identity::<i64>(5)`) - Frust has
no type inference, so a generic call needs its own explicit source,
same as every other generic-instantiation site in this codebase.
`fn Result::ok<T, E>(v: T) -> Result<T, E>` also works - a function's
declared NAME may now be a qualified path (`ident_path` instead of a
bare `IDENT`), which is what gives Result/Option their real
constructor sugar (#5). Generic `impl` METHODS (`impl<T> Box<T> {
fn get(self) -> T }`) remain genuinely not started - `impl_decl` still
has no generic-parameter-list syntax at all; the qualified-free-
function trick above covers what #5 actually needed without them.

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
- Generic free functions now DONE (turbofish call syntax + lazy
  monomorphization, mirroring the struct approach exactly - see the
  updated status above). Generic `impl` methods remain not started -
  `impl_decl` still has no type-parameter list in the grammar at all.
- Monomorphization for explicit-generic-arg CALLS happens in a
  dedicated pre-scan pass (`compileProgram`'s "Pass 1.5") BEFORE
  regular function bodies compile, specifically because
  `getOrCreateMonomorphizedFunction` is not safe to call reentrantly
  (`compileFunction` doesn't save/restore `namedValues`/the builder's
  insert point, only its own coroutine-context locals) - a generic
  function's OWN body calling ANOTHER explicit-generic-arg function is
  therefore not supported yet (a clear compile error, not silent
  corruption, if it's ever attempted).

Verified (`test_generics.frust`, `frust_compiler.exe` direct-run): two
DIFFERENT instantiations of the same generic struct (`Box<i64>` and
`Box<f32>`) each construct and read back correctly - proves genuine
per-type monomorphization, not one hardcoded case; a two-type-parameter
struct (`Pair<i64, f32>`, the real stand-in shape for #5's eventual
`Result<T, E>`) with two different field types also correct. Real,
hand-predicted exit code matched exactly.

Generic free functions verified separately (`test_generic_fn.frust`):
`identity::<i64>(42)` and `identity::<f64>(3.5)` both correct in the
SAME program (two different instantiations of one template, same
"prove it's not one hardcoded case" discipline). Also required a real
grammar fix found empirically, not theorized: a `postfix_expr "::" "<"
type_arg_list ">"` rule was provably unreachable (confirmed by an
actual syntax error) - `ident_path`'s own left-recursive `"::" IDENT`
rule already owns every `::` following an identifier, and bison's
default shift-preference always wins that conflict, so no amount of
grammar-level cleverness at the `postfix_expr` level could ever reach
the new rule. Fixed by lexing `"::<"` as its own single token
(`frust.l`) - the ambiguity disappears entirely once the parser never
sees a bare `::` immediately before `<` in the first place. Full
`frust_plugin_host` regression suite (16 examples) and the JUCE IDE
both rebuilt and re-verified clean.

## 5. Result/Option (structured error handling)

**Status: DONE - 2026-08-24, constructor sugar included.** `Result<T, E>`/`Option<T>` now exist
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

**Constructor sugar now real** (2026-08-24, once #4's generic free
functions landed): `Result::ok`/`Result::err`/`Option::some`/
`Option::none` are real generic free functions whose declared NAME is
itself a qualified path (`fn Result::ok<T, E>(v: T) -> Result<T, E>`):

```frust
let r: Result<i64, String> = Result::ok::<i64, String>(42);
let e: Result<i64, String> = Result::err::<i64, String>("division by zero");
let o: Option<i64> = Option::some::<i64>(99);
let n: Option<i64> = Option::none::<i64>();
```

Verified (`test_result_sugar.frust`/`test_option_sugar.frust`,
`frust_compiler.exe` direct-run): both Ok/Err and Some/None branches,
including the zero-argument `Option::none::<i64>()` case (T comes
purely from the turbofish, no argument to infer it from) - hand-
predicted exit codes matched exactly.

Remaining honest limitation: no enforced safety against reading
`.ok_value` on an `Err`/`.value` on a `None` (same "no default-value
story, caller's responsibility" stance every struct literal already
has in this codebase - real pattern matching would be the actual fix,
already separately queued). `06_frust_library/core`'s existing
sentinel-return convention (null/-1/0) is UNCHANGED - adopting
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

**Status: DONE.**

`|params| -> RetType { body }` real closure literals, represented as
the exact same fat pointer (`{ ptr code, ptr env }`) `wrapAsInterface`
already builds for interface dispatch - deliberate reuse, not a second
mechanism. Capture is by value only, copied into a real heap-allocated
(malloc'd) env struct at the closure literal's own construction site -
a named, deliberate v1 limitation (by-reference capture would need
real lifetime/escape analysis this project doesn't have). Free
variables are found via a new recursive AST walk
(`collectFreeVariables`, mirrors `hasPerform`'s shape); `perform`
inside a closure body is rejected outright (no coroutine-trampoline
machinery for a closure's own generated function). A closure is
self-describing (params/return type are in its own literal syntax,
unlike `Vector::new()`), but its call-time SIGNATURE still has to be
recorded under its bound name at the `let` site
(`namedValueClosureSignature`) so `compileCall` can dispatch to it
correctly - checked before `compileIndirectCall`'s generic (and wrong,
for a fat-pointer aggregate) plain-pointer-callee assumption. Verified
with a real test: two independent closures each capturing a different
outer variable, called with real arguments, both correct
(`add_base(5)` with `base=10` → 15; `scaled(7)` with `factor=3` → 21).
Known, named limitation: no shadowing awareness in capture analysis -
a closure-body `let` reusing an outer variable's name is misidentified
as referencing the outer capture; avoid reusing captured names for
closure-locals. Also known limitation: only `let`-bound (named)
closures can be called - an immediately-invoked closure literal
(`|x| {...}(5)`) isn't handled by `compileCall`'s dispatch yet, and a
closure-typed function PARAMETER isn't wired up either - both real,
separate, smaller follow-ons if ever needed, not silently folded in.

## 7. `own`/`shared`/`weak` smart pointers (real reference counting / move semantics)

**Status: PARTIAL, `shared` now real (2026-08-23) - `own` heap
allocation still has no automatic drop, `weak` remains entirely
unimplemented.**

Deliberately scoped down from "full" after tracing a real risk: `own`
has exactly one owner and no refcount, so an automatic drop needs real
move-tracking (was this value returned out, reassigned, passed
elsewhere?) - Frust has never had that analysis, and getting it wrong
means a certain double-free in code that works today, not just a
missing feature. `shared` doesn't have that problem (it's safe by
construction - a count only frees at genuine zero), so that's what
shipped:

- `shared Foo { ... }` mallocs one block: an 8-byte i64 strong-count
  header immediately followed by the payload struct. The pointer bound
  to a `let` is still just the payload address (header + 8), so every
  existing struct-field/method-call code path works completely
  unchanged - only construction/binding/drop know about the header.
- Real strong refcounting: a fresh construction starts at 1;
  `let b = a;` (rebinding an EXISTING shared value - a second strong
  owner) retains (increments) rather than starting fresh - a real bug
  caught and fixed while tracing the design, before it ever shipped
  (without the retain, `a` and `b` would each independently decrement
  the SAME header at their own scope exits, the second one reading
  already-freed memory).
- Automatic scope-exit drop: every `{ ... }` block tracks the
  shared-owning locals `let`-bound directly in it (`sharedScopeStack`);
  they're dropped (in reverse) at the block's own natural exit, or by
  `return`/`break`/`continue` when control leaves early (`return` walks
  every open scope back to the function boundary; `break`/`continue`
  only walk scopes opened since the nearest loop began, tracked via
  `loopSharedScopeDepth` parallel to the existing `loopStack`). A
  block's own tail identifier (or an explicit `return name`) is
  recognized and skipped - ownership propagates out untouched rather
  than being dropped prematurely.
- **Named, deliberate scope cuts** (worst case is always a LEAK, never
  a double-free or use-after-free - the whole point of stopping here):
  a `shared` value returned out of its constructing function is
  untracked by the caller (no call-boundary/return-type tracking this
  pass - the constructing function's own copy correctly isn't dropped,
  but the caller has no way to know it now holds a live strong
  reference, so it's never dropped by this mechanism either); a
  shared-typed function PARAMETER isn't tracked either (no
  increment/decrement at call boundaries); no shadowing-awareness
  (mirrors the same named cut in closures' capture analysis).
- Verified with a real test: 2000 real alloc/drop cycles through a
  loop (stress + a hand-predicted summed value, proving no heap
  corruption across many real malloc/free cycles through the new
  header math), plus a real aliasing case (`let b = a;`, both dropped
  independently at their own scope exits, correct field reads right up
  to each drop point). Full `frust_plugin_host` regression sweep (14
  examples) and JUCE IDE rebuild both clean.

`own`'s automatic-free and `weak` (needs a genuinely different
mechanism - a control block that can outlive the payload) both remain
real, separate, deliberately-not-attempted follow-ons - `own`
construction is unchanged from its prior heap-malloc-only behavior;
`weak` construction is still rejected with a clear error.

## 8. Multi-file plugins (`frust_plugin_host`)

**Status: DONE.**

`frust_plugin_load()`'s primary file's own `use self::X;` lines now
resolve against sibling files in the same directory (`X.frust` tried
first - the plugin-file convention, then `X.fr` - the library/pod-file
convention), merged directly into the compiled program. New shared
helper `ResolveSelfUses` (`ModuleLoader.h`/`.cpp`) implements the
resolution once; `ResolveImports`' existing pod-import self-use loop
was refactored to call it too rather than keep a second, duplicate
copy - which also fixed a real latent bug in that loop along the way
(it checked `!errors.empty()` against the whole shared/accumulated
errors vector rather than comparing counts before/after, the same
class of bug already fixed once in the OUTER pod loop - meant one
early self-use file failing inside a multi-file POD would silently
poison every later self-use file in that same pod too). Verified with
a real two-file plugin (a main file with `use self::helper;` calling a
function only the sibling file declares) - hand-predicted result
matched exactly. Full `frust_plugin_host` regression sweep (15
examples, including the new one) and JUCE IDE rebuild both clean.

## 9. Bool-across-FFI convention

**Status: DONE - verified clean, no restriction needed.**

Never empirically tested before this session (the IDE's four real
plugins, 2026-08-22, deliberately avoided it in favor of `i64` 0/1,
untested register-width ABI behavior being the concern). Tested for
real rather than just documenting the avoidance: a Frust `bool` (LLVM
`i1`) crosses the real C ABI cleanly in BOTH directions - a C++ caller
reading a Frust function's returned `bool` gets the exact byte
(`0x01`/`0x00`, no dirty bits above bit 0, not just "truthy"), and a
Frust `extern fn` passing a `bool` ARGUMENT to a real host-registered
C function also arrives clean. Verified with a dedicated harness that
checks the raw byte pattern via `memcpy`, not just C++ truthiness
(which would silently mask a dirty-upper-bits bug, since any nonzero
byte reads as true). **No convention change needed** - `bool` was
already safe to use directly across the FFI boundary the whole time;
existing code using `i64` 0/1 doesn't need retrofitting, it just never
needed the workaround in the first place.

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
