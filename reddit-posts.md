# Frust — Reddit/Quora post tracker

Tracks what's been written and posted for Frust on r/FrustLang and Quora, so we're not
duplicating topics or losing track of what's already out there. Add a new entry below
every time a post is drafted or actually published.

## Posted

### r/FrustLang — general Frust intro
- **Status:** posted
- **Platform:** Reddit, r/FrustLang
- **Topic:** general introduction to Frust — what it is, what's actually working.
- **Note:** drafted and posted earlier in the session that did the repo migration; exact
  text wasn't saved to a file at the time, so it isn't reproduced here verbatim.

### r/FrustLang — why `shared<T>` works the way it does
- **Status:** posted
- **Platform:** Reddit, r/FrustLang
- **Topic:** design-philosophy piece on `shared<T>` reference counting — deterministic
  drop, no GC, why it was built this way. Written using "Frust does X" phrasing
  throughout (not "my language"), per an explicit correction during drafting.
- **Note:** exact text wasn't saved to a file at the time, so it isn't reproduced here
  verbatim.

### Quora (Creative Programming space) — "How does Frust handle pointers?"
- **Status:** posted
- **Platform:** Quora, Creative Programming space (117.4K followers)
- **Topic:** answers "How does Frust handle pointers?" with both real Frust usage code
  and real internal `Codegen.h` implementation snippets (`sharedHeaderPtr`,
  `dropSharedLocal`, `retainSharedLocal`). Links to the FrustLang repo and
  https://www.reddit.com/r/FrustLang/.
- **Note:** exact text wasn't saved to a file at the time, so it isn't reproduced here
  verbatim.

### Quora (Creative Programming space) — "How does Frust implement closures?"
- **Status:** posted (2026-08-25, live within minutes)
- **Platform:** Quora, Creative Programming space
- **Topic:** answers "How does Frust implement closures?" — the `{ptr code, ptr env}`
  fat-pointer representation (shared with interface dispatch), capture-by-value into a
  malloc'd env struct, and why v1 deliberately avoided by-reference capture (no
  lifetime/escape analysis yet). Includes a real `Codegen.h` snippet from
  `compileClosureLiteral` and a verified usage example (`add_base(5)` with `base=10` →
  15, `scaled(7)` with `factor=3` → 21, matching `LANGUAGE_GAPS.md`'s own hand-verified
  numbers).
- **Full text:**

  > Frust closures are `|params| -> RetType { body }` literals that compile to a fat
  > pointer — literally `{ ptr code, ptr env }`, the exact same representation the
  > compiler already builds for interface dispatch (deliberate reuse, not a second
  > mechanism). Capture is by value only: at the closure literal's own construction
  > site, every free variable the body references gets copied into a real
  > heap-allocated (malloc'd) env struct, and the closure value returned is just the
  > trampoline function pointer plus a pointer to that env.
  >
  > Concretely, from `Codegen.h`'s `compileClosureLiteral`:
  >
  > ```cpp
  > std::vector<llvm::Type*> capturedTypes;
  > for (auto& name : captured) capturedTypes.push_back(namedValues[name]->getType());
  > llvm::StructType* envTy = llvm::StructType::get(context, capturedTypes);
  >
  > uint64_t envSize = module.getDataLayout().getTypeAllocSize(envTy);
  > llvm::Value* envPtr = builder.CreateCall(getMallocFn(),
  >     {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), envSize)});
  > for (size_t i = 0; i < captured.size(); ++i) {
  >     llvm::Value* fieldPtr = builder.CreateStructGEP(envTy, envPtr, (unsigned)i);
  >     builder.CreateStore(namedValues[captured[i]], fieldPtr);
  > }
  > ```
  >
  > Free variables are found first via a recursive AST walk (`collectFreeVariables`) —
  > the compiler statically figures out exactly what the closure body reads from its
  > enclosing scope, builds a struct type just wide enough to hold copies of those
  > values, and mallocs it once per closure construction. In Frust source:
  >
  > ```frust
  > fn main() -> i64 = {
  >     let base: i64 = 10;
  >     let add_base = |x: i64| -> i64 { x + base };
  >
  >     let factor: i64 = 3;
  >     let scaled = |x: i64| -> i64 { x * factor };
  >
  >     add_base(5) + scaled(7)   // 15 + 21 = 36
  > }
  > ```
  >
  > `add_base` and `scaled` each get their own malloc'd env holding a copy of
  > `base`/`factor` at the moment the closure literal ran — mutating the outer `base`
  > afterward wouldn't be seen by `add_base`, since it copied the value rather than
  > referencing the variable. That's a deliberate v1 tradeoff: by-reference capture
  > only works safely if the compiler can prove the referenced variable outlives the
  > closure, which needs real lifetime/escape analysis Frust doesn't have yet.
  > Capture-by-value sidesteps that entirely — the copy is safe by construction.
  >
  > (Repo: github.com/wwestlake/FrustLang · r/FrustLang for more)

## Drafted, not yet posted

### r/FrustLang — how Frust implements algebraic effects using real LLVM coroutines
- **Status:** drafted 2026-08-25, not yet confirmed posted
- **Platform:** intended for r/FrustLang
- **Topic:** `effect`/`perform`/`handle`/`resume` compiled on real LLVM `coro.*`
  intrinsics (`coro.save`/`coro.suspend`/`coro.done`/`coro.promise`) — the same
  primitives C++20 coroutines use. Includes the real `test_effects.frust` example
  (`Write(val: f64)` effect, handled by printing and resuming).
- **Draft text:**

  > **How Frust implements algebraic effects using real LLVM coroutines**
  >
  > `effect`/`perform`/`handle`/`resume` in Frust isn't parsed-and-ignored syntax —
  > it's real, working codegen built directly on LLVM's `coro.*` intrinsics, the same
  > primitives C++20 coroutines compile down to. A function containing `perform` gets
  > compiled as a coroutine: `perform Write(123.456)` packs the effect's id and
  > arguments into the coroutine's promise, calls `llvm.coro.suspend`, and hands
  > control back to whatever is driving the coroutine — the `handle` expression.
  > `handle` runs a loop checking `llvm.coro.done`, and when it isn't done, reads the
  > resumed effect id back out of the promise and dispatches to the matching
  > `effect NAME(...) => { ... }` case, which calls `resume(value)` to hand a value
  > back and continue the coroutine from exactly where it suspended.
  >
  > That means a Frust program can genuinely suspend mid-function, let a handler
  > decide what happens next, and resume right where it left off — real control-flow
  > interception, not exceptions, not callback threading.
  >
  > ```frust
  > extern fn frust_print_f64(val: f64) -> f64;
  >
  > effect Write(val: f64)
  >
  > fn do_io() -> f64 = {
  >     perform Write(123.456);
  >     perform Write(789.012);
  >     42.0
  > }
  >
  > fn main() -> f64 = {
  >     handle do_io() with {
  >         effect Write(val) => {
  >             frust_print_f64(val);
  >             resume(0.0)
  >         }
  >     }
  > }
  > ```
  >
  > Every `perform Write(...)` suspends `do_io`, hands the value to the handler in
  > `main`, prints it, and resumes right where it left off — `do_io` runs to
  > completion and returns `42.0` once both performs are handled.

### 5-post series — main serious features (2026-08-24)
- **Status:** drafted, not yet posted
- **Platform:** intended for r/FrustLang (5 separate short posts)
- **Topic:** one language feature per post, ~2 paragraphs + a short verified code
  example each. Syntax in every example was pulled directly from the grammar
  (`frust.y`) and `Codegen.h`, not reconstructed from memory.

---

#### 1. Real interface dispatch — not duck typing, actual dynamic dispatch

Frust has `interface`/`impl X for Y`, and it's not sugar — a function that takes an
interface-typed parameter genuinely dispatches through a vtable at runtime, so the same
call site can run different code depending on what concrete type got passed in. No
inheritance hierarchy required, no "does this struct happen to have the right method
names" duck typing — you declare conformance explicitly with `impl`, and the compiler
enforces it.

```frust
interface Automation {
    fn tick(input: f32) -> f32;
}

impl Automation for Motor {
    fn tick(input: f32) -> f32 = { input * 2.0 }
}

fn run_step(a: Automation, x: f32) -> f32 = { a.tick(x) }
```

---

#### 2. `shared` — reference counting without a garbage collector

Frust's memory model is explicit, not GC'd: `own` is single-ownership with automatic
drop at scope end, and `shared` is deterministic reference counting — retain on
copy, release on scope exit, deallocate the instant the count hits zero. No
stop-the-world pause, no background collector thread, and no ambiguity about when a
destructor runs. You always know exactly when cleanup happens by reading the code.

```frust
fn main() -> i64 = {
    let a: shared Counter = shared Counter { value: 0 };
    let b = a;                 // retain - refcount 2
    // both drop at end of scope, refcount hits 0, real free happens here
    0
}
```

(Corrected 2026-08-24: the type annotation is `shared Counter`, a bare
prefix keyword before the plain type name — `shared<Counter>` isn't
valid Frust syntax. Caught before this post went out; see
`LANGUAGE_GAPS.md`'s "CRITICAL BUGS" section for the full story of how
this was found.)

---

#### 3. Generics — real monomorphization, not type erasure

`struct Box<T>` and `fn identity<T>(x: T) -> T` are genuine generics: each concrete
instantiation gets its own compiled, specialized version (same strategy Rust and C++
templates use), not a boxed/erased runtime representation. Turbofish syntax picks the
concrete type explicitly at the call site when it can't be inferred from arguments
alone.

```frust
fn identity<T>(x: T) -> T = { x }

fn main() -> i64 = {
    let a = identity::<i64>(42);
    let b = identity::<f32>(3.14);
    0
}
```

---

#### 4. Live `quote`/`unquote`/`build_time` — metaprogramming that runs while your program is already running

This is the one people don't expect: `build_time { }` blocks can `quote { }` real AST
nodes (with `unquote(expr)` splicing live runtime values in as literals), hand that
AST to the compiler's own JIT, and get back a freshly-compiled function pointer — all
from inside a program that's already executing. It works because a running `.frust`
program shares a process with the same compiler pipeline that built it, so "generate
code from live data and compile it in on the spot" is just calling back into that
pipeline, not a separate execution model bolted on.

```frust
fn build_line_eval(m: f32, b: f32) -> ASTExpr = build_time {
    quote {
        (unquote(m) * x) + unquote(b)
    }
}

extern fn frust_jit_eval_f32(ast: ASTExpr, x: f32) -> f32;

fn main() -> i64 = {
    let ast = build_line_eval(2.0, 1.0);   // y = 2x + 1, specialized just now
    let y = frust_jit_eval_f32(ast, 5.0);  // JIT-compiles and runs it: 11.0
    0
}
```

---

#### 5. Embeddable by design — Frust runs inside your app, not just next to it

Frust compiles to native x86, but it also JITs and links in-process, which means it's
built to be *embedded*: the JUCE-based IDE in this repo hosts Frust plugins directly
in its own process, calling into JIT-compiled Frust functions like any other C
function pointer. That makes Frust usable as a real scripting/extension layer for a
host application — not a separate process you shell out to, not a subprocess you pipe
JSON through, but code that runs in the same address space as your app.

```cpp
// host app (C++), loading and calling a Frust plugin in-process
FrustPluginHandle plugin = frust_plugin_load("linter.frust");
void* fn = frust_plugin_get_fn(plugin, "check_line");
auto check = reinterpret_cast<int64_t(*)(const char*)>(fn);
int64_t issues = check("// TODO: fix this");
```
