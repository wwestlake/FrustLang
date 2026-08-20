# Frust: known language gaps

A real accounting of what's rough in the language today, so "what's
missing" is a document, not tribal knowledge. Written as part of the
Phase 1 hardening pass toward rolling Frust into Creation Suite as a
plugin/node-authoring language (see the session plan). Each item below
is a deliberate, named deferral - not an oversight - with the reason it
was deferred and what would actually be needed to close it.

Items already fixed this pass (typed indirect calls, `Vec<N>` indexed
assignment, bitwise operators, a corrected "no i32" story) are **not**
listed here - see `Codegen.h`'s and `string.fr`'s own comments, and the
git history, for those.

## No closures

Only the named-function + packed-buffer pattern works today (see
`core/src/task.fr`, `curry.fr` in the frust-library submodule) - a
function's address can be taken and passed around (`Codegen.h`'s
`ExprKind::Identifier` address-of decay) and called back indirectly
(`compileIndirectCall`/`compileTypedIndirectCall`), but there's no way
to capture surrounding variables into an anonymous callable. A real
closure type needs a captured-environment representation (heap-allocate
the captured values, a fat pointer pairing code address + environment
address, and a calling convention that knows to pass the environment
implicitly) - a genuinely bigger compiler feature, not a small addition.

**Why deferred:** the node system (Phase 3) doesn't need closures if
generated nodes compile to named top-level functions, which they will.
Revisit if/when a real ergonomic need shows up that the named-function
pattern can't cover.

## `own`/`shared`/`weak` smart pointers

Parsed (`SmartPtrKind` in `frust.y`'s `type_ptr_prefix_opt`), zero
codegen. `FRUST_LANG_SPEC.md` designs these as the language's real
memory-management story; today every value is either a plain scalar, a
`String` (an untyped, unowned pointer), or a stack-allocated struct.

**Why deferred:** not needed for function/struct-based plugin or node
code, which is what this pass is actually building toward. A real
implementation needs reference counting (`shared`) and move semantics
(`own`) wired through the whole codegen path - substantial, standalone
work.

## Raw pointer dereference (`*ptr`)

Parsed (`UnaryOp::Deref` exists in the grammar), explicitly
unimplemented in `Codegen.h` - prints `"frust: codegen does not support
pointer dereference yet"` and bails, rather than silently misbehaving.

**Why deferred:** `core/src/buffer.fr`'s `get_i64`/`set_i64`/`get_ptr`/
`set_ptr` (small runtime helpers exported from `Main.cpp`, the same
pattern as `frust_print_str`) already cover every practical need this
session hit for indexed/pointer-based memory access. A real `*ptr`
operator is a separate, more general feature with its own design
questions (what does `*ptr = x` mean for an arbitrarily-typed pointer,
how does that interact with `own`/`shared` above).

## Struct values returned from a function lose type tracking

`inferStructTypeName` (`Codegen.h`) only follows a variable bound
directly to a struct literal (`let c: Counter = Counter { ... };`), not
one returned from a function call (`let c: Counter = make_counter();`).
Calling a method or accessing a field on such a variable fails with
"cannot call a method on an expression of unknown struct type" -
documented repeatedly in the frust-library submodule (e.g.
`random.fr`'s header, which works around it by requiring direct struct-
literal construction instead of a factory function).

**Why deferred:** a proper fix means tracking struct types through
function return values generally (the function's declared return type
already says what struct it is - the type IS knowable, just not
currently propagated to the caller's `namedValueStructType` map). Real,
contained-looking work, but touches a path used everywhere structs
flow, so it's being scoped and verified carefully rather than rushed
into this pass.

## No structured error/`Result` type

Failures report to stderr and return a null/sentinel value (a null
`String`, a `-1`, a `0`) - the caller has to know the convention for
whatever function it's calling. A real `Result<T, E>` needs generics,
which Frust doesn't have (`Vec<N>` is an int-parameterized fixed-size
float vector for linear algebra, not a general generic type - see
`resolveType`'s comment on it).

**Why deferred:** generics are a substantial, standalone type-system
feature. Node-generated code (Phase 3) follows the same "caller checks
a sentinel" convention already used throughout `core` - consistent, not
a regression, just not the ideal.

## `Vec<N>` naming

`Vec<N>` is a fixed-size, compile-time-`N` **float** vector (linear
algebra - `dot`/`length`/`normalize` builtins, genuine SSA vector
values), not a general fixed-size array of any element type. The name
invites the wrong assumption (confirmed - this exact confusion caused a
wasted investigation earlier this session before `Vec<N>`'s real nature
was understood). Not fixed this pass (a rename is a real, if mechanical,
breaking change); named here so it's a known trap, not silently
confusing.

## No platform-conditional compilation

No `#cfg`-equivalent mechanism - a single pod can't carry both a
Windows and a Linux implementation of the same module side by side.
Blocks the Linux/pthread port of `thread.fr`/`mutex.fr`/`procspawn.fr`/
`process_task.fr`, which is explicitly paused (not part of this plan).
Would need either compiler support (conditional compilation directives)
or frate-level support (a platform-tagged source-file list in
`frate.json`) - a real, scoped feature for whenever the Pi work resumes.

## Multi-file plugins

`frust_plugin_host`'s `frust_plugin_load()` reads exactly one `.frust`
file - no `use self::`-style multi-file plugin support (unlike `frate`
pods, which do support this via an explicit compiled file list in
`lib.fr`). Named here (not in Phase 2's scope either) since it's a real
gap for anything beyond a small single-file plugin.
