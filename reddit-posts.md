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

## Drafted, not yet posted

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

#### 2. `shared<T>` — reference counting without a garbage collector

Frust's memory model is explicit, not GC'd: `own` is single-ownership with automatic
drop at scope end, and `shared<T>` is deterministic reference counting — retain on
copy, release on scope exit, deallocate the instant the count hits zero. No
stop-the-world pause, no background collector thread, and no ambiguity about when a
destructor runs. You always know exactly when cleanup happens by reading the code.

```frust
fn main() -> i64 = {
    let a: shared<Counter> = shared Counter { value: 0 };
    let b = a;                 // retain - refcount 2
    // both drop at end of scope, refcount hits 0, real free happens here
    0
}
```

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
