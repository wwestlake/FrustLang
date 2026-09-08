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

## CRITICAL BUGS (found in the wild - jump the queue, fixed before anything else)

### Struct-by-value function return silently corrupts field data

**Status: DONE - 2026-08-24, found and fixed same day.** Not a gap
(missing feature) - a real, silent correctness bug in something adjacent
to LANGUAGE_GAPS.md #2, which remains correctly closed (see below). Found
because a tutorial's first factory-function example needed real,
hand-predicted values to be honest, and the values it actually got back
were wrong.

Reproduced, isolated (`isolate_struct2.frust`/`isolate_struct3.frust`,
`frust_compiler.exe` direct-run): a struct constructed and returned
DIRECTLY inline (`let p: Point = Point { x: 3, y: 4 };`, no function call
crossing) reads back correctly (`p.x` = 3, `p.y` = 4). The IDENTICAL
struct, constructed the same way but RETURNED FROM A FUNCTION
(`fn make_point(x: i64, y: i64) -> Point = { Point { x: x, y: y } }`,
called as `let p: Point = make_point(3, 4);`) reads back WRONG for BOTH
fields - `p.x` and `p.y` both print the same garbage value (72 in the
repro), not 3/4. Confirmed it's not a parameter-name-shadows-field-name
issue (renaming the function's params to `ax`/`ay` - no collision with
the struct's `x`/`y` field names at all - produces the identical wrong
result).

Root cause (confirmed by direct code read, `Codegen.h`): there is no
`sret`/`byval`-style calling-convention handling anywhere in this file -
grep for `sret`/`byval`/`StructRet` returns zero matches. Frust has never
implemented real x86-64 ABI rules for returning an aggregate struct BY
VALUE from a function (small structs need specific register-packing
rules; larger ones need a hidden pointer parameter the caller supplies)
- whatever's happening today produces silently wrong bits rather than a
compile error. Struct-return TYPE tracking (gap #2 below) is unaffected
and still correctly closed - the compiler knows `p` is a `Point`, field
access resolves to the right NAMES, it's the underlying VALUE crossing
the function-call boundary that's corrupted.

**Why this matters more than a normal gap**: every other numbered item in
this document either fails loudly (a clear compile error) or is an
honestly-scoped missing feature. This is the first found case of SILENT
data corruption in a pattern (a factory function returning a struct) that
looks completely ordinary and would be one of the first things anyone
writes. User's own call once informed: "this is a critical bug and
oversite [sic]. Document it, stop working on the tutorial and go fix it
immediately."

**Fix**: `compileStructLiteral` (`Codegen.h`) used to stack-alloca a
plain struct literal in the constructing function's own entry block -
fine as long as the value never left that function, dangling the moment
it did. Since every struct in this language is pointer-represented
(`resolveType`: "structs are always passed/held by pointer"), a bare
literal's storage genuinely has to outlive the constructing function to
be safe in general - stack allocation can't deliver that no matter how
carefully the rest of codegen is written. Fixed by mallocing instead (the
same no-refcount-header path `own`/`raw` already use via
`compileHeapStructLiteral`) - a plain struct literal now behaves exactly
like an implicit `own`: never freed (same already-accepted limitation
`own` itself has - no auto-free yet, see #7 below) rather than ever
risking silent corruption. Matches this project's own stated risk
tolerance elsewhere ("worst case is always a LEAK, never a double-free or
use-after-free", #7) - a deliberate, precedented tradeoff, not a new one.

Verified: the exact repro (`isolate_struct2.frust`/`isolate_struct3.frust`)
now reads back correctly (`p.x`=3, `p.y`=4, matched exactly - previously
both read 72). A combined feature-check program exercising structs,
interface dispatch, generics, `shared`, `Vector<T>`, and closures together
in one run (`tutorial_combined_check.frust`) produced every hand-predicted
value correctly (3, 12, 42, 10, 100, 200, 1001, 0) end to end. Full
existing `frust_plugin_host` regression suite (all 16 examples, Debug
rebuild) and a full JUCE IDE Debug rebuild + launch smoke test both clean
- this touched a core codegen path used by every plain struct literal in
the language, so the full sweep (not just the new repro) was the actual
bar for "done," per the standing rule.

Also surfaced a real, separate documentation error while investigating:
the earlier Reddit/Quora/LinkedIn `shared<T>` content (this session) used
`shared<Counter>`-style angle-bracket type-annotation syntax, which isn't
valid Frust - confirmed via direct grammar read (`type_ptr_prefix_opt`):
`shared`/`own`/`raw`/`weak` are bare prefix keywords before a plain type
name (`shared Counter`, no angle brackets), the same shape as every other
smart-pointer type annotation. Already-published content wasn't corrected
retroactively (out of scope of this fix); new content should use the
correct bare form.

### `node` reflection silently mislabeled unrecognized/generic pin types as `"int"`, and never exposed a node's own genericness at all

**Status: DONE - 2026-09-05.** Two related bugs in `compileNodeReflection`/
`nodeDataType` (`Codegen.h`), found while verifying an earlier LLM's false
claim that "generics + reflection already let a node be generic over its
output type" - they don't, today, and this is why.

**Bug 1 (fixed first, on its own):** `nodeDataType(const TypeExpr*)` only
recognized four primitive type names (`f32`/`f64`, `i32`/`i64`/`usize`,
`bool`, `String`/`string`); every other type - a bare generic node
parameter (`node pure fn interpolate<T>(...) -> T`), a struct, `Vector<T>`,
`Option<T>` - silently fell through to `"int"` with zero error, zero
indication the claim was false. Fixed: both fallback branches now return
`"any"` (`node_system::DataType::Any` on the host side - an already-
existing, already-wired wildcard `IsConnectionCompatible` already treats
as matching any concrete pin) - an honest "no precise type known" instead
of a fabricated specific one. This alone was never logged here at the
time it shipped, despite this file's own standing instruction (top of
this document) to update the moment status changes - a real miss, caught
directly by the person who owns this repo, not found independently.

**Bug 2 (found investigating the first):** even with Bug 1 fixed,
`compileNodeReflection` emitted *zero information about a node being
generic at all* - no `genericParams` list, nothing tying a pin to a
generic parameter name. A generic `node pure`/`node callable` declaration
parsed and reflected without complaint, producing a syntactically valid
manifest entry whose `frustEntryPoint` named a symbol that would never
exist as a real, callable `llvm::Function` unless some unrelated turbofish
call site elsewhere in the same program happened to monomorphize it -
and even then the real symbol is mangled (`"identity<i64>"`, not
`"identity"`), so nothing tied reflection's claim to the actual compiled
result. Fixed: `compileNodeReflection` now emits a `"genericParams": [...]`
array per node (straight from `FunctionDecl::genericParams`) and a
`"genericParam": "T"` field on any pin whose `TypeExpr::name` matches one
of the function's own generic parameter names - so a NodeSystem-side
consumer can tell a node is generic, which parameter, and on which pins,
instead of every generic pin looking identical to an ordinary untyped
`Any` one.

Verified (`FrustGenericNodeReflectionSmoke`, `apps/CreationEngine`,
new): a real `node pure fn identity<T>(x: T) -> T` compiled through the
full pipeline (`compileNodeReflection` -> `MergeNodeReflection` ->
`PluginRuntime::nodeLibraries`), confirming the manifest JSON that
actually reaches a host application carries `"genericParams": ["T"]` and
`"genericParam": "T"` on both the input and output pins - not just that
`compileNodeReflection`'s own intermediate JSON looks right in isolation.
Neither bug's fix changes what a generic node's underlying FRust function
compiles to (monomorphization still requires a real turbofish call site
somewhere, per gap #4 below) - this closes the REFLECTION honesty gap
specifically; NodeSystem-side consumption of `genericParams`/
`genericParam` to actually resolve and emit a concrete instantiation per
placed graph-node instance is separate, ongoing work outside this repo
(`shared/NodeSystem`, the Creation-Suite monorepo).

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
base of 4, `realloc`-backed), `.len()`, `.get(i)`, `v[i]` bracket read,
and (2026-09-08) `v[i] = x` bracket **write** - real in-place element
mutation, the one item originally deferred here.

**Bracket write** (`compileAssign`'s `ExprKind::Index` case, checked
before the pre-existing `Vec<N>` SSA-vector assignment case since
`Vector<T>` is a real heap pointer, not an SSA value) includes a real
bounds check - out-of-range writes are the actually dangerous case a
growable collection needs this for, so a clear runtime panic (print +
`exit(1)`, mirroring `emitRefinementCheck`'s own panic sequence
verbatim) replaces what would otherwise be silent heap corruption.
**Honest, found-but-not-fixed gap**: `.get(i)`/bracket READ still have
NO bounds check of their own (confirmed - neither ever did, before or
after this change) - reading past the end is undefined, not a clean
error. Scope stayed to write, per how this item was queued; read's own
missing check is real, separate, smaller follow-on work.

Verified (`test_vector.frust`, `frust_compiler.exe` direct-run): five
pushes, then bracket-write into a previously-pushed middle index AND
the last slot, read back via BOTH `.get()` and bracket read to confirm
both paths see the write - hand-predicted value matched exactly.
Negative test (`test_vector_oob_negative.frust`): a bracket write past
the vector's length triggers the real runtime panic, not silent
corruption. Full `frust_plugin_host` regression sweep (17 examples) and
a JUCE IDE Debug rebuild + launch smoke test both clean.

Verified (`test_vector.frust`, `frust_compiler.exe` direct-run, 2026-08-23):
an empty vector starts at length 0; five pushes land at length 5,
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

**Status: DONE - generic `impl` methods landed 2026-09-08, closing the
last open piece.** `impl<T> Box<T> { fn get(self) -> T }` - methods on
a generic STRUCT or ENUM, monomorphized lazily the first time a method
is actually called on a concrete instantiation (`getOrCreateMonomorphizedMethod`,
mirroring `getOrCreateMonomorphizedFunction` exactly one level up:
same lazy-per-call-site trigger, same mangled-name memoization via
`module.getFunction`). New grammar: `impl_decl` gained a THIRD
alternative requiring a literal `"<"` right after `"impl"` (not
`generic_params_opt`, which has an `%empty` branch that would have
conflicted with the existing interface-impl alternative's bare
`"impl" IDENT "for" ...` - found empirically via `bison -Wall`, a real
shift/reduce conflict, not theorized) - `"impl" "<" generic_param_list
">" IDENT type_generic_args_opt "{" ...`, Rust-like syntax repeating
the type args on the type name (`impl<T> Box<T>`), though only
`generic_param_list` is the real source of truth (`type_generic_args_opt`
is parsed and discarded, syntax parity only). Scoped to the plain
inherent-impl form only - `impl<T> Iface for Box<T>` (a generic
interface impl) is real, separate, out-of-scope-for-this-pass work.

**Real bug found and fixed while building this**: unlike a generic free
function (pre-scanned and monomorphized entirely in Pass 1.5, BEFORE
any Pass-2 body starts compiling - see `getOrCreateMonomorphizedFunction`'s
own comment on why that matters), a generic method's concrete
instantiation can only be known once its receiver's static type is
already known, which in general needs the CALLER's own body compilation
already under way - so `getOrCreateMonomorphizedMethod` genuinely is
called reentrantly, from inside `compileMethodCall`, itself mid-
compilation of whatever function is calling the method.
`compileFunction`'s unconditional `namedValues.clear()` (and its sibling
side tables) was silently wiping the CALLER's own in-progress local
variables the first time this was tested (`unknown identifier 'b1'` on
a name that was very much still in scope). Fixed by save/restoring the
full `namedValue*`/`sharedScopeStack`/builder-insertion-point state
around the monomorphizing `compileFunction` call, mirroring
`compileClosureLiteral`'s trampoline save/restore exactly, for the
same reason.

Two more real, smaller bugs found in the same pass, both from the same
root cause (a monomorphized method's `FunctionDecl` is a SHALLOW copy
of its template - `params[i].type`/`returnType` still point at the
template's own shared, UNSUBSTITUTED `TypeExpr`s, e.g. bare `"T"`,
outside the `currentGenericSubstitution` window that only exists while
that specific method was being compiled): argument coercion at the call
site (`compileMethodCall`) and struct/enum type INFERENCE for a
method-call's result (`inferStructTypeName`/`inferEnumTypeName` - which
previously didn't handle method-call results AT ALL, a separate,
pre-existing gap for even non-generic methods, closed here for both at
once via a new shared `inferMethodCallResultType`) both had to be
changed to resolve through the SAME temporarily-established substitution
`getOrCreateMonomorphizedMethod` itself uses, rather than trusting the
stale template pointer directly.

Also found, NOT fixed (real, separate, logged honestly): `compileFunction`'s
method `self`-binding unconditionally assumed a struct receiver
(`namedValueStructType["self"] = fn.selfTypeName`) - now fixed as part
of this same pass to check `enumVariantIndex` first, since an impl
block's `Self` type can now legitimately be an enum
(`impl<T> Option2<T> { ... }`) too, not just a struct.

**Two more real gaps found while testing, confirmed NOT new but not
fixed here either:**
- `if`/`while`/`for`'s condition has the SAME bare-identifier-vs-
  struct-literal grammar ambiguity `match` had (see the algebraic-data-
  types section above) - `if has1 { ... }` fails to parse for the exact
  same reason `match has1 { ... }` did. Unfixed here (would require the
  same parenthesization convention, a real breaking-change discussion
  for how heavily `if`/`while` are already used unparenthesized
  throughout every example in this repo) - worked around in this
  session's own tests with `if has1 == true { ... }` instead.
- `as` casting (`v as i64`) is not implemented for ANY type combination
  at all - confirmed with a minimal, fully non-generic repro (`let v:
  f64 = 3.5; (v as i64)`), "codegen does not support this expression
  kind yet". Not a generics-specific gap, a general one, logged here
  because this is where it was found.

Verified (`test_generic_methods.frust`, `frust_compiler.exe` direct-run):
`Box<T>` (a generic struct) with a T-returning method AND a T-typed-
PARAMETER method (`replace`), exercised on two DIFFERENT concrete
instantiations (`Box<i64>`/`Box<f64>`) in one program - proves real
per-type monomorphization, not one hardcoded case, and proves
substitution reaches both directions (param and return); `Option2<T>`
(a generic ENUM) with a method whose body uses `match` on `self` -
proves the mechanism works identically for enum receivers, and proves
the `self`-binding fix. Hand-predicted value matched exactly. Full
`frust_plugin_host` regression sweep (17 examples) and a JUCE IDE Debug
rebuild + launch smoke test both clean.

### Original struct + free-function generics (2026-08-24, kept for the record)

**Status at the time: PARTIAL (structs + free functions; methods still
open).** Generic STRUCTS are real - `struct Box<T> { value: T }`,
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
- A generic `node pure`/`node callable` declaration (a Schematic node
  generic over its pin type) reflects honestly now - see the CRITICAL
  BUGS entry above ("`node` reflection silently mislabeled...") - but
  there is still no compiler-side mechanism to REQUEST a specific
  monomorphization other than a literal turbofish call site somewhere in
  the compiled program; a NodeSystem-generated graph gets one "for free"
  today because graph-to-FRust compilation emits real source text
  (confirmed directly, `shared/NodeSystem/src/frust_codegen.cpp`), so a
  generated turbofish call site is indistinguishable from a hand-written
  one to Pass 1.5 - no new compiler API is needed for this, only for
  NodeSystem itself to learn how to emit that call correctly per placed
  node instance (separate, ongoing work outside this repo).

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

**Status: DONE - rewritten as real enums, 2026-09-08.** `result.fr`/
`option.fr` used to be a flag-struct hack (`is_ok`/`has_value` boolean +
both fields always physically present, kept below for the historical
record) with an honestly-named-but-real limitation: nothing stopped
reading `.ok_value` on an `Err`. Per this project's own "design the
right system, don't patch the placeholder" precedent, once real `enum`/
`match` landed (LANGUAGE_GAPS.md's algebraic-data-types work), the
struct hack was deleted outright and replaced:

```frust
enum Result<T, E> { Ok(T), Err(E) }
enum Option<T> { Some(T), None }
```

That's the ENTIRE new file content for each - `synthesizeEnumVariantConstructor`
(Codegen.h) auto-generates `Result::Ok`/`Result::Err`/`Option::Some`/
`Option::None` from the bare `enum` declaration, so the old hand-written
`fn Result::ok<T,E>(...)`/etc. constructor-sugar functions aren't needed
at all anymore - deleted, not kept alongside. Renamed to capitalized
variant-constructor form (`Result::Ok`, was `Result::ok`) to match the
idiomatic Rust/F# convention every other variant name in this language
now uses - confirmed via repo-wide search there were zero real call
sites anywhere outside the definitions themselves to break.

```frust
let r: Result<i64, String> = Result::Ok::<i64, String>(42);
let e: Result<i64, String> = Result::Err::<i64, String>("division by zero");
match (r) {
    Result::Ok(v) => v,
    Result::Err(msg) => -1,
}
```

**This is the actual fix for the old "no enforced safety" limitation**:
there is no `.ok_value` field to misread anymore - an enum's payload is
only ever reachable through `match`, and `match` requires the `Err` case
be handled too (or a `_` wildcard), a real compile error otherwise.

Verified (`test_result_option.frust`, `frust_compiler.exe` direct-run,
re-run against the rewritten enum-based definitions - inlines the exact
new file content, since this direct-run harness has no pod-import
wiring exercised): `safe_divide(a, b) -> Result<i64, String>` exercised
on both the `Ok` (10/2) and `Err` (10/0) branches via `match`, plus
`Option<i64>` on both `Some` and `None` - hand-predicted exit value
matched exactly. New negative test (`test_result_negative.frust`):
`r.ok_value` on a `Result` is now a real compile error ("codegen does
not support this member-access expression yet"), not silent UB - proves
the safety fix is real, not just documented. Pure library-source change
(no grammar/`Codegen.h` edits) - doesn't need the full regression sweep
item #10/enum/match required.

### Original struct-hack implementation (2026-08-24, kept for the record)

`Result<T, E>`/`Option<T>` used to exist as real, usable generic
structs - built as real Frust code using #4's generics, not another
compiler intrinsic like `Vector<T>`: the first genuinely useful thing
built on top of generics, not just a synthetic test of the feature.

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

**Status: DONE (2026-09-08).** `own`'s automatic drop and `weak`'s
control-block/`.upgrade()` mechanism both landed this session (see the
`weak` write-up below, after `own`'s), each for a real but deliberately
narrow scope - closing out the last open item in this document.
The highest-risk item in this whole document by its own original
reasoning (below) - a wrong answer here is a real double-free, not just
a missing feature - so the scope stayed strictly to what could be
PROVEN safe by a real (if conservative) AST scan, not extended to cover
every case `shared` does.

**What ships**: a `let`-bound `own T { ... }` local gets a real
scope-exit `free()` (straight `free()`, no header, no refcount - unlike
`dropSharedLocal`, an `own` value is never header-prefixed) IF AND ONLY
IF `ownValueEscapes` (`Codegen.h`) can prove, by scanning the rest of
its own directly-enclosing block, that the name is never: passed as a
Call argument, used as the RHS-target of ANOTHER `let` (a rebinding/
aliasing use - `own` has no refcount to make a second owner safe the
way `shared`'s retain does, so a rebind is simply never tracked at all,
not even conditionally), or referenced AT ALL inside a nested
control-flow construct (`if`/`while`/`loop`/`for`/`match`/`handle`) -
this last check is deliberately broader than strictly necessary (it
doesn't matter WHAT the reference inside the nested construct is doing,
ANY reference disqualifies tracking) because a value propagating out
through a nested block's own tail (`if cond { x } else { other }`)
needs cross-block reasoning this minimal pass doesn't attempt, and
getting that specific case wrong would free a pointer still in active
use. An explicit `return name;` and the block's own direct tail
identifier are handled the SAME way `shared` already does (a `skipName`
parameter threaded through `Block`/`Return`, now also gating
`ownScopeStack`, mirroring `sharedScopeStack` one-to-one including the
`break`/`continue`/loop-depth machinery) - NOT flagged as escaping by
`ownValueEscapes` itself, since a real, separate skip-at-that-exact-
exit mechanism already covers them correctly.

**A real reentrancy risk was checked and closed as part of this work**:
`getOrCreateMonomorphizedMethod` (item #4/generic-impl-methods) already
needed a full `namedValue*`/`sharedScopeStack` save/restore around its
own reentrant `compileFunction` call - `ownScopeStack`/
`loopOwnScopeDepth` were added to that exact save/restore list (and to
every other site `sharedScopeStack` is saved/restored/cleared -
`compileClosureLiteral`'s trampoline, `compileFunction`, `compileAnonymous`)
so this feature doesn't reintroduce the same class of bug fixed for
generic methods.

**Real, surprising finding while verifying this**: the ORIGINAL
verification plan (a large stress loop, watching process memory
externally to prove no leak growth, mirroring `shared`'s own 2000-cycle
test) turned out not to work FOR THIS SPECIFIC COMPILER - LLVM's own
`-O2` optimizer (run unconditionally on every compiled program, see
`Main.cpp`'s `optimizeModule`) proves a malloc'd block that never
escapes a function has no observable effect and deletes the ENTIRE
malloc/store/free sequence as dead code, regardless of whether this
pass's own `free()` was present, correct, or even reached - confirmed
directly: a 5-million-iteration stress-test program's `main()` compiled
down to a bare `ret i64 5000000`, zero `malloc`/`free` calls anywhere
in the post-optimization IR. Runtime memory measurement therefore can't
distinguish "this pass correctly freed it" from "the optimizer deleted
the allocation as dead code" - **verified against `output_pre_opt.ll`
directly instead** (frust_compiler's own pre-optimization IR dump,
reflecting exactly what THIS pass emits, unclouded by later
optimization) for six real scenarios: a simple non-escaping local (real
`malloc`→stores→load→`call void @free`→`ret`, value safely loaded into
an SSA register BEFORE the free, no use-after-free), a tail-returned
value (`ret ptr %0`, zero `free` calls), an argument-passed value
(`call @read_it(ptr %0)`, zero `free` calls), a value referenced inside
a nested `if` (zero `free` calls), an explicit non-tail `return`
(zero `free` calls, the dummy intervening statement confirming this
isn't just the tail-identifier check firing), and a `let b = a;`
rebind (zero `free` calls for either name). All six matched the
intended shape exactly.

**Named, deliberate scope cut** (worst case is always a LEAK, never a
double-free or use-after-free - same bar `shared` was held to): an
`own` value returned out of its OWN constructing function is correctly
NOT freed there (via the skipName mechanism above), but the RECEIVING
caller gets no NEW tracking of its own for that value - same
call-boundary limitation `shared` already has. Confirmed real `own`
usage elsewhere in this repo (`06_frust_library/core/src/automation.fr`'s
`RampAutomation`/`DecayAutomation` constructors) is entirely
UNAFFECTED by this change either way - both are bare function-tail
`own` literals, never `let`-bound, so this pass's own tracking logic
(which only ever triggers from the `Let` branch) never even runs for
them; the existing `frust_plugin_host` regression examples that
exercise `automation.fr` (`automation_example`, `multi_plugin_stress_example`)
confirm this empirically too.

Full `frust_plugin_host` regression sweep (17 examples) and a JUCE IDE
Debug rebuild + launch smoke test both clean.

### `weak` implementation (2026-09-08)

Needed a control block that can outlive the payload - `shared`'s header
grows from 8 bytes (`i64 strongCount`) to 16 (`{ i64 strongCount, i64
weakCount }`). `weak T` construction (`compileWeakNew`) increments ONLY
`weakCount` - the payload itself is untouched, no clone/move. The
PAYLOAD still frees the moment `strongCount` hits 0 (a `.upgrade()`
after that point must never read freed payload bytes) - but
`dropSharedLocal` now checks BOTH counts before freeing the 16-byte
HEADER block itself, since a surviving `weak` reference still needs it
to observe that `strongCount` is now 0. `.upgrade()`
(`compileWeakMethodCall`) returns a REAL `Option<T>` rather than ever
handing back a possibly-dangling pointer: loads `strongCount`, branches
on `> 0`, and calls the SAME synthesized `Option::Some`/`Option::None`
constructor functions this document's real-enum work (item #5) already
produces, via `getOrCreateMonomorphizedFunction` - reentrant-safe (a
full `namedValue*`/`sharedScopeStack`/`ownScopeStack`/builder-IP
save-restore around it, mirroring every other reentrant monomorphization
site in this file).

**Named, deliberate scope cut** (worst case is always a LEAK, never a
double-free or use-after-free - same bar `own`/`shared` were each held
to): no automatic weak-drop tracking in this pass - `weakCount` only
ever increments, never decrements at a `weak` local's own scope exit. A
`weak` reference that outlives its enclosing scope without being
explicitly consumed leaves the 16-byte header block permanently
allocated (never the payload, which stays correctly bounded by
`strongCount` alone) - a real, bounded leak, same call-boundary-shaped
limitation `shared` already has.

**Real bug found and fixed while verifying this**: `.upgrade()`'s
target-type parameter was originally passed as `const std::string&`,
aliased directly to a live node inside the `namedValueWeakType` map (the
receiver's own weak-type record). The first of `.upgrade()`'s two
reentrant monomorphization calls (`Option::Some<T>`) compiles that
variant constructor's body, which - like every function body - clears
`namedValueWeakType` as part of its own local-scope reset; that clear
invalidated the still-referenced map node mid-flight. The reference
dangled silently through the first call (already evaluated before the
clear) but was read again constructing the SECOND call's argument
(`Option::None<T>`), now pointing at freed memory - manifesting as a
genuine hang, not a crash: the Windows Debug CRT heap allocator
deadlocked inside that read (confirmed directly - a live `Microsoft
Visual C++ Runtime Library` "abort() has been called" dialog sat
blocked behind the process, near-zero CPU across a 20-second timeout,
found by inspecting the hidden window via UI Automation). Fixed by
taking the parameter BY VALUE (`std::string targetTypeName`, a real
copy made before any nested call can invalidate the source) instead of
by reference.

**A second, independent bug surfaced by the same test**: binding an
enum variant's payload in a `match` arm (`PatternKind::Binding` in
`compilePatternTest`) only ever recorded the bound name's raw LLVM
value (`namedValues`), never its STATIC type - so `Option::Some(f) =>
f.v` (a struct-typed payload) failed with "codegen does not support
this member-access expression yet", `f` never having been registered in
`namedValueStructType`. This was a pre-existing gap in this document's
original enum/match work (items #2-3), just never exercised before
(`test_enum.frust`/`test_match.frust` only ever bind primitive- and
nested-enum-typed payloads, never a struct-typed one immediately
field-accessed). Fixed by adding `enumVariantFieldStructType` (mirrors
the existing `enumVariantFieldEnumType` table exactly, including through
a monomorphized generic's substituted concrete type - e.g. `Option<Foo>`'s
`Some(T)` resolves `T`→`Foo` here even though the sibling enum-field
table deliberately leaves a substituted field unresolved), threading a
new `structTypeName` parameter through `compilePatternTest`'s recursion
so a `Binding` pattern registers `namedValueStructType`/
`namedValueEnumType` for whatever type its bound value statically is.
Also fixed a matching, previously-latent leak this surfaced:
`compileMatch`'s per-arm scoping only ever saved/restored `namedValues`,
never `namedValueStructType`/`namedValueEnumType` - now all three are
saved/restored per arm, matching item #10's own Block-scoping fix.

Verified: `test_weak_upgrade_live.frust` (a live `shared`, `weak` taken,
`.upgrade()` while the strong owner is still alive → hand-predicted
`42`, reading the upgraded `Some(Foo)`'s own `.v` field) and
`test_weak_upgrade_dead.frust` (the strong owner dropped inside a helper
function before `.upgrade()` runs → hand-predicted `-1`, the `None`
branch) both match exactly. Full prior-item regression suite (every
`test_*.frust` positive and negative case, items 1-7) re-run clean
against both the 16-byte header change and the new struct-field-binding
fix. Full `frust_plugin_host` regression sweep (17 examples) and a JUCE
IDE Debug rebuild + launch smoke test both clean.

### Original `shared` implementation (2026-08-23, kept for the record)

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

`own`'s automatic-free and `weak` (a control block that outlives the
payload) were, at the time this paragraph was written, real, separate,
deliberately-not-attempted follow-ons - both have since landed; see the
write-ups above.

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

## 10. Real block-level lexical scoping

**Status: DONE - 2026-09-08.** Fixed as the first step of the algebraic-
data-types push (a `match` expression's arms need real per-arm scoping,
so this had to land first). The `ExprKind::Block` case (`Codegen.h`) now
snapshots `namedValues`/`namedValueStructType`/`namedValueRawPointeeType`/
`namedValueVectorElementType`/`namedValueInterfaceType`/
`namedValueClosureSignature`/`namedValueSharedType` on entry and restores
all seven on every exit path (fall-through, and the early-return-on-
compile-failure path), mirroring the closure-literal trampoline's own
whole-map save/restore exactly (see below) but scoped per block instead
of per closure. `sharedScopeStack` itself was deliberately left untouched
- it already had its own correct per-block push/pop lifecycle for drop
tracking; this fix is purely about name *visibility*.

Verified (`test_block_scope.frust`, `frust_compiler.exe` direct-run): an
outer `let x: i64 = 100` with an `if`-body `let x: i64 = 999` shadowing
it - hand-predicted the outer `x` survives untouched (`main() => 100`,
not `999`, which is what the old flat-map bug would have produced).
Negative case (`test_block_scope_negative.frust`): a `let y` introduced
ONLY inside an `if`-body, referenced after the block ends - hand-
predicted and confirmed a real compile error (`unknown identifier 'y'`),
not silent success. Full `frust_plugin_host` regression sweep (all 17
examples, Debug rebuild) and a full JUCE IDE Debug rebuild + launch
smoke test both clean - this touched the single most-used codegen path
in the whole compiler (every `{ }` body, including every function's own
top-level block), so the full sweep was the actual bar for "done," per
the standing rule.

**Original finding (2026-08-24, kept for the record), answering a real
Quora question ("What are the scoping rules for Frust?"):**

Confirmed by direct read of `Codegen.h`: `namedValues` (the
name -> `llvm::Value*` table every `let`/parameter binds into) is a
single flat `std::unordered_map`, cleared once at the start of each
function/closure body (`namedValues.clear()`) - but never pushed/
popped per nested block. That means Frust's actual current scoping is
function-level, not block-level: a `let` declared inside an `if`/
`while` body writes into the SAME map as the function's top-level
bindings, and stays bound (visible, and still holding its value) for
the rest of the function after that block ends - a name isn't removed
or restored just because the block that introduced it exited.

Two existing, narrower mechanisms already prove the right shape exists
elsewhere in the codebase, just not generalized to every block:
- Closures literals (#6) save/restore the ENTIRE `namedValues` map
  around their own body compilation (`savedNamedValues`, `Codegen.h`
  ~line 3242/3256) - coarse (whole-map swap, not scope-aware), but
  proves save/restore around a nested compile is already a pattern
  this codebase uses.
- The coroutine-parameter-shadowing case (~line 2991-3012) saves and
  restores individual NAMES around one nested compile - closer to what
  real block scoping would need, but scoped narrowly to that one
  feature, not a general per-block mechanism.

Real fix, not attempted yet: give block compilation (wherever an
`if`/`while`/bare `{ }` body is compiled) a save-the-changed-names/
restore-on-exit step around itself - record which names in
`namedValues` get newly added or overwritten inside the block, and
restore the pre-block state (removing new names entirely, restoring
prior values for shadowed ones) when the block's compilation returns -
mirroring the closure literal's whole-map save/restore, but scoped per
block rather than per closure.

Why numbered here rather than folded into #6's "no shadowing
awareness" limitation: closures' shadowing gap is about capture
ANALYSIS misidentifying which outer names a closure body references;
this is a more basic gap in the codegen's own scope tracking that
affects `if`/`while` bodies even with no closures involved at all.

---

## Future features (NOT gaps - queued after all 9 items above close)

User's own distinction, explicit: these are new language features the
user WANTS, not foundational things missing from what already exists -
kept in a clearly separate section so they don't get conflated with
the numbered gap-closing sequence above, and not started until that
sequence is done.

### Real algebraic data types: `enum` (discriminated unions) + `match`

**Status: DONE - 2026-09-08.** F#/Rust-style discriminated unions, not a
C-style tag-only enum: `enum Shape { Circle(f64), Rect(f64, f64), Point }`,
each variant carrying its own typed payload (including other enums/
structs - real nesting, generics too: `enum Choice<A, B> { Left(A),
Right(B) }`, monomorphized exactly like generic structs).

Sequenced as three pieces, in dependency order: real block-level lexical
scoping (gap #10, above - had to land first so `match` arms get correct
per-arm scoping for free), then `enum` grammar/codegen, then `match`.

**Representation**: pointer-represented like structs - one malloc'd
`{ i64 tag, <payload bytes> }` block (`Codegen.h`: `enumVariantIndex`/
`enumVariantPayloadType`/`enumPayloadSize`, mirroring `structTypes`/
`structFieldIndex`/`genericStructTemplates` one-to-one; `genericEnumTemplates`/
`getOrCreateMonomorphizedEnum` mirror the generic-struct machinery
exactly). Each variant's payload is its OWN LLVM struct type (no single
aggregate fits every variant's differently-shaped payload) - a real,
deliberate v1 limitation carried over from generic structs: a payload
field typed as a bare generic parameter isn't resolved for nested-pattern
purposes (`enumVariantFieldEnumType`'s own comment), though it can still
be bound via a plain Binding pattern.

**Construction**: `EnumName::VariantName(args...)` - one compiler-
synthesized `FunctionDecl` per variant (`synthesizeEnumVariantConstructor`),
reusing the exact qualified-path-function convention `Result::ok`
already established (LANGUAGE_GAPS.md #5) - no new call-site machinery,
a generic variant constructor rides the SAME turbofish + Pass-1.5
pre-scan every other generic function already uses. A no-payload variant
can be referenced bare (`Piece::King`, no `()`) - special-cased in the
`Path` expression case since it never reaches `compileCall`'s dispatch.

**`match`**: recursive pattern grammar (`_` wildcard, bindings, INT/
FLOAT/STRING/bool literals, `EnumName::Variant(pattern, ...)`, a struct-
pattern shape that PARSES but isn't wired up in codegen yet - real,
named v1 gap, a clear compile error not silent wrong behavior) -
compiled as a chain of tag/literal/binding checks (`compilePatternTest`),
deliberately not a flat LLVM `switch` (can't express nested checks like
`Node::Pair(Node::Leaf(Shape::Circle(r)), _)` reaching through two enum
layers in one arm). Exhaustiveness is a real compile error (every
variant covered, or a `_`/binding catch-all) at the TOP level only -
nested sub-patterns aren't separately checked. A non-exhaustive match
that somehow still reaches runtime (shouldn't happen given the compile-
time check, but no real semantic-analysis pass exists to PROVE it - see
this file's own header comment) fails loudly via a runtime panic
(print + `exit(1)`), never silent undefined behavior.

**Real grammar ambiguity found and fixed while building this**: `match
scrutinee { ... }` with a bare-identifier scrutinee is genuinely
ambiguous with `struct_literal` (`ident_path "{" ... "}"`) - bison's
default shift-preference greedily extends a bare identifier toward a
struct literal instead of ending the scrutinee at `match`'s own "{",
so `match c1 { SomeVariant(x) => ... }` failed to parse at all before
this was found. Fixed by requiring parens around the scrutinee -
`match (c1) { ... }` - same convention `switch (x) { ... }` uses in C/
C++/Java/JavaScript, for exactly this reason; parens structurally
prevent struct_literal from being reachable at all (`)` isn't part of
`ident_path`'s own grammar). **`if`/`while`/`for`'s conditions have the
SAME latent ambiguity, unfixed** - none of their existing tests happen
to trigger it (none use a bare identifier immediately followed by `{`),
found by the same reasoning while diagnosing match's failure, real and
worth knowing about, but out of scope for this pass.

Verified: `test_enum.frust` (two different instantiations of the same
generic enum, `Choice<i64,f64>` and `Choice<bool,i64>`, proving real
per-type monomorphization; a non-generic three-variant enum with
multi-field and no-payload variants), `test_match.frust` (a pattern
reaching through two enum layers in one arm, a no-payload variant
pattern, a wildcard nested inside a variant pattern, a top-level
wildcard fallback - hand-predicted exact values, all matched), and a
negative test (`test_match_negative.frust`, a deliberately non-
exhaustive match with no `_` - confirmed a real compile error, not
silent success). Full `frust_plugin_host` regression sweep (all 17
examples) and a JUCE IDE Debug rebuild + launch smoke test both clean.

**Named, deliberate v1 scope cuts** (state honestly, not silently
dropped): no arm guard clauses (`Circle(r) if r > 0.0 => ...`); struct
patterns parse but aren't implemented in codegen (clear compile error);
string literal patterns rejected too - this language's own `==` already
only does POINTER comparison for `String` (see `compileBinary`'s `Eq`
case), so a string pattern would silently never match a runtime string
rather than actually compare content; nested sub-patterns aren't
separately checked for exhaustiveness, only the top level.

### `let`/function-param/`for`-loop destructuring (F#-style, irrefutable patterns)

**Status: QUEUED - not started.** The other half of the original ask,
deliberately scoped separately from `match` above: these need
*irrefutable* patterns only (a destructuring `let`/param can never fail
to match - no variant tag ever fails), a meaningfully smaller problem
than `match`'s general refutable patterns, which is why `match` shipped
first. `let` bindings (`"let" mut_opt IDENT type_annot_opt "=" expr`)
still always bind a single identifier, never a tuple/struct-shaped
pattern (`let (a, b) = pair`, `let { x, y } = point`); function
parameters and `for` loop variables are the same - always one name, one
type, never a destructured shape.
