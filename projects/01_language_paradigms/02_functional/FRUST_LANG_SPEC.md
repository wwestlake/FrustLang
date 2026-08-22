# Frust Language Specification

Frust (Functional Rust) is the flagship language of LagDaemon Software Research & Development: a pure, expression-oriented language built for embedded dynamic code creation - living inside a running host application (the JUCE IDE) with LLVM OrcJIT hot-reloading, rather than compiling out-of-band and restarting.

This is the single spec document for the language. The grammar itself (flex/bison `.l`/`.y`) is the authority on syntax once it exists - this document covers semantics and design intent.

---

## Contents
1. [Advanced Language Features](#1-advanced-language-features)
2. [Smart Pointers & Memory](#2-smart-pointers--memory)
3. [REPL & Dynamic JIT Driver](#3-repl--dynamic-jit-driver)
4. [The Automation Layer](#4-the-automation-layer)

---

## 1. Advanced Language Features

Frust incorporates four concepts from modern PL research (Lisp, Haskell, OCaml, Unison):

1. **Lisp-style AST code quotation** (`quote` / `unquote`)
2. **Refinement types & value predicates** (`f32[-1.0 .. 1.0]`)
3. **First-class algebraic effects & resumable handlers** (`effect` / `perform` / `handle` / `resume`)
4. **AST content hashing for zero-latency OrcJIT hot-reloading**

### 1.1 AST Code Quotation (`quote` / `unquote`)

Instead of parenthetical Lisp syntax, Frust provides first-class AST code quotation inside `build_time` metaprogramming blocks.

```frust
// Metaprogram: synthesizes a fast polynomial evaluation AST
fn build_polynomial_ast(coefficients: Array<f32, 3>) -> ASTExpr = build_time {
    let a = coefficients[0]
    let b = coefficients[1]
    let c = coefficients[2]

    // quote generates AST nodes directly; unquote evaluates expressions into AST
    return quote {
        (unquote(a) * x * x) + (unquote(b) * x) + unquote(c)
    }
}
```

### 1.2 Refinement Types & Value Predicates

Refinement types attach mathematical predicates to types, letting the compiler guarantee invariant safety at compile time with no runtime `assert()` overhead:

```frust
// Range-bounded float (e.g. normalized audio sample)
type NormalizedSample = f32[-1.0 .. 1.0]

// Non-zero integer (guarantees divide-by-zero is impossible)
type NonZeroI32 = i32[!= 0]

pub fn safe_divide(numerator: i32, denominator: NonZeroI32) -> i32 = {
    return numerator / denominator // Guaranteed zero-panic compile-time safety
}

// Fixed-dimension matrix multiplication safety
fn multiply(a: Mat<3, 4>, b: Mat<4, 2>) -> Mat<3, 2>
```

### 1.3 First-Class Algebraic Effects & Resumable Handlers

Algebraic effects unify exceptions, async/await, generators, logging, and dependency injection into one clean effect system with resumable continuations:

```frust
// 1. Declare effects
effect ReadSensor -> f32
effect Log(msg: String)

// 2. Perform effect (independent of implementation)
pub fn process_telemetry() -> f32 = {
    let val = perform ReadSensor()
    perform Log("Sensor read: " + val.to_string())
    return val * 10.0
}

// 3. Handle effect (mock for unit testing, or wire to real hardware)
pub fn main() = {
    handle process_telemetry() with {
        effect ReadSensor() => resume(42.0) // Resumes execution with 42.0
        effect Log(msg) => {
            println("[LOG]: " + msg)
            resume()
        }
    }
}
```

### 1.4 AST Content-Addressing for Zero-Latency OrcJIT Hot-Reloading

Traditional JIT environments re-compile the whole file on every edit. Frust hashes the AST structure of every function/component (BLAKE3 content addressing):

```
[ Code Edit in JUCE Host UI ]
              |
              v
    [ AST Hash Generator ]
              |
    +---------+---------+
    |  Hash Match?       |
    +---------+----------+
    | YES                | NO
    v                    v
 (0ms Skip)      (Re-JIT LLVM OrcJIT Dylib)
```

If an edit doesn't change a function's semantic AST hash (comments, whitespace), OrcJIT bypasses compilation entirely - 0ms latency.

---

## 2. Smart Pointers & Memory

### 2.1 Vision: "Aiming for the Iron" Without Lifetime Friction

Rust's lifetime annotations (`'a`, `'b`, `&'a mut T`) are safe but notoriously hard to grok and write. Frust aims for the iron (OS kernels, device drivers, bare-metal audio engines) by building smart pointers directly into the language as first-class primitives - Boost/C++-style power, with intuitive syntax and zero template bloat.

### 2.2 The 5 Built-in Pointer Primitives

| Pointer Type | Syntax | Description | Use Case |
| :--- | :--- | :--- | :--- |
| **Unique (Owned)** | `Unique<T>` or `own T` | Exclusive ownership. Auto-freed on scope exit. Zero overhead. | Single-owner heap buffers, stack-to-heap allocation. |
| **Shared (Ref-Counted)** | `Shared<T>` or `shared T` | Shared ownership, atomic ref-counted, auto-managed. | Shared data graphs, UI components, multi-thread state. |
| **Weak (Observer)** | `Weak<T>` or `weak T` | Non-owning reference to `Shared<T>`. Prevents cyclic leaks. | Parent pointers, observer patterns, event listeners. |
| **Raw (Hardware)** | `Raw<T>` or `raw* T` | Direct physical memory address. Allows pointer arithmetic. | OS kernels, MMIO drivers, bare-metal hardware. |
| **Managed Ref** | `Ref<T>` or `&T` | Auto-borrowed reference, scope inferred by compiler. | Function arguments, local temporary access. |

### 2.3 Code Examples

**Unique (exclusive owned) pointer:**
```frust
fn create_buffer(size: usize) -> own AudioBuffer = {
    let buf = own AudioBuffer::new(size)
    return buf // Transferred out via move semantics
} // Automatically freed when dropped if not returned
```

**Shared & weak pointers:**
```frust
struct Node {
    value: i32,
    parent: weak Node,        // Prevents reference cycles
    children: Vector<shared Node>
}

fn add_child(parent: shared Node, val: i32) = {
    let child = shared Node {
        value: val,
        parent: shared::downgrade(parent),
        children: Vector::new()
    }
    parent.children.push(child)
}
```

**Raw hardware pointers (bare-metal OS & kernel MMIO):**
```frust
pub unsafe fn write_hardware_register(address: usize, value: u32) = {
    let ptr = address as raw* u32
    *ptr = value // Direct hardware MMIO write
}
```

### 2.4 Comparison

| Feature | Rust | C++ (Boost/std) | Frust |
| :--- | :--- | :--- | :--- |
| **Lifetimes Syntax** | Mandatory `'a`, `'b` annotations | N/A | Compiler inferred |
| **Smart Pointers** | External `Rc<T>`, `Arc<T>`, `Box<T>` | Complex templates `std::shared_ptr<T>` | Language built-in primitives |
| **OS Kernel & MMIO** | `*const T`, `*mut T` in `unsafe` | Raw pointers `T*` | Built-in `raw* T` |
| **Cycle Prevention** | `Weak<T>` | `std::weak_ptr<T>` | Built-in `weak T` |

---

## 3. REPL & Dynamic JIT Driver

### 3.1 Overview & Vision

The Frust interactive REPL (`frust i` / `frust-repl`) is a high-performance interactive shell and embedded evaluation driver built directly on LLVM OrcJIT v2. Because Frust is designed for embedded dynamic code creation, the REPL is not a bytecode interpreter - it's a real-time JIT compiler shell that parses Frust code, generates LLVM IR, compiles machine code into memory in microseconds, and invokes it instantly.

### 3.2 REPL Architecture

```
                      [ User Input / CLI / JUCE Console ]
                                     |
                                     v
                          [ Frust Lexer & Parser ]
                                     |
                                     v
                             [ AST Validation ]
                     (Refinement & Unit Type Checking)
                                     |
                                     v
                           [ LLVM IR Generator ]
                                     |
                                     v
                       [ LLVM OrcJIT (JITDylib) ]
                                     |
                                     v
                    [ Native Memory Execution & Print ]
```

### 3.3 Interactive Shell Session Example

```frust
$ frust i
Welcome to Frust (Functional Rust) v0.1.0 Interactive REPL [LLVM OrcJIT Engine]
Type :help for commands, :quit to exit.

frust> let dist = 100.0 * Meter
val dist: f32[Meter] = 100.0

frust> let time = 9.58 * Second
val time: f32[Second] = 9.58

frust> dist / time
val it: f32[Meter / Second] = 10.438413

frust> component Gain(f: f32) : AudioProcessor { in s; out o; o = s * f }
component Gain created [Interface: AudioProcessor]

frust> :type dist / time
Type: f32[Meter / Second]
Dimensions: Length^1 * Time^-1

frust> :ir dist / time
define float @__repl_expr_1() {
entry:
  ret float 0x4024E077E0000000
}
```

### 3.4 Built-in REPL Commands

| Command | Action | Description |
| :--- | :--- | :--- |
| `:type <expr>` | Inspect Type | Displays inferred type, refinement bounds, and physical units. |
| `:ir <expr>` | Show LLVM IR | Prints the LLVM IR code generated by the compiler. |
| `:ast <expr>` | Show AST | Displays the Abstract Syntax Tree structure. |
| `:load <file>` | Load Module | Dynamically loads and JIT-links a `.fr` file into memory. |
| `:reset` | Reset Environment | Clears current OrcJIT `JITDylib` symbol table. |
| `:help` | Show Help | Displays command summary. |
| `:quit` | Exit Shell | Closes REPL session. |

### 3.5 JUCE Host Integration

The REPL driver is embedded directly into the JUCE Desktop Host (`projects/02_juce_language_host`), letting developers write, evaluate, and hot-swap Frust components in real time inside the running JUCE desktop application.

---

## 4. The Automation Layer

Frust's host-interop stack has three layers, from raw to ergonomic:

1. **Raw host interface.** A host process registers native functions
   (`frust_plugin_register_host_function`, `projects/09_frust_plugin_host`);
   loaded Frust code calls them via ordinary `extern fn`. No structure, no
   contract-checking - the host just promises the symbols exist.
2. **Formal contract** (`interface` / `impl X for Y`, below). A host or
   library declares a named capability contract; a concrete type declares
   it implements that contract. Checked at compile time, not hoped for.
3. **The automation layer.** A real standard library, written in Frust
   itself, of stateful objects (structs + `impl`) that wrap layers 1/2's
   raw calls into something worth using directly - the Frust equivalent
   of a managed framework's base class library, but functional-flavored:
   an object earns its existence by holding real state worth managing (a
   handle, a buffer, a phase), not as a namespace for stateless verbs -
   plain functions already cover stateless behavior better.

### 4.1 Interfaces (`interface`, `impl X for Y`)

```frust
interface Automation {
    fn tick(&mut self, delta_time: f32) -> f32
}

struct RampAutomation {
    phase: f32,
    rate: f32
}

impl Automation for RampAutomation {
    pub fn tick(&mut self, delta_time: f32) -> f32 = {
        self.phase = self.phase + delta_time * self.rate;
        self.phase
    }
}
```

An `interface` declares a set of method signatures with no bodies - a
checked contract. `impl InterfaceName for TypeName { ... }` (alongside
the existing plain `impl TypeName { ... }` for inherent methods) makes a
concrete type satisfy that contract; the compiler rejects the block if
any declared method is missing.

A value of an interface type is a genuine fat pointer - `{ data, vtable }`
- not a plain struct pointer. Assigning a concrete struct value to an
interface-typed `let` builds that fat pointer once, using a vtable
generated per `(interface, concrete type)` pair. Calling a method on an
interface-typed value dispatches through the vtable at runtime (a real
indirect call, not a name lookup) - the same call site correctly runs
different code depending on which concrete type is actually behind the
value:

```frust
let a: Automation = RampAutomation { phase: 0.0, rate: 2.0 };
a.tick(1.0)  // dispatches to RampAutomation::tick
```

### 4.2 The `Automation` interface

The first, foundational automation-layer interface (`projects/06_frust_library/core/src/automation.fr`):
a host that wants to drive *something* over time - a game engine moving
an object, a DAW automating a parameter, a robot's motor curve - loads
any `Automation`-typed value and calls `.tick(delta_time)` once per
frame/step, without knowing or caring which concrete implementation is
behind it. Frust never needs to know what the returned value means; that
meaning belongs entirely to the host. Two reference implementations ship
in `automation.fr`: `RampAutomation` (linear accumulation) and
`DecayAutomation` (multiplicative decay) - structurally different update
rules, verified through the same `.tick()` call site to prove real
dynamic dispatch, not just that one interface implementation compiles.

Domain-specific automations (audio parameters, engine transforms) are a
separate, later concern layered on top of this - `Automation` itself is
deliberately host-agnostic.

### 4.3 Self-describing plugins (`manifest`)

```frust
manifest "{ \"name\": \"event_plugin\", \"listenedEvents\": [\"something_happened\"], \"requiredHostFunctions\": [{\"name\": \"test_mark_fired\"}] }";
```

A top-level `manifest "<raw JSON text>";` declaration - a plugin's own
metadata, compiled directly into it rather than living in a separate
companion file that can drift out of sync with the code it describes.
Deliberately narrow: not a general `const`/`static` declaration (Frust
has neither), just this one purpose-built construct, so a real plugin-
hosting need doesn't turn into open-ended language scope creep.

At most one per program - a second `manifest` declaration is a compile
error. The JSON text is compiled into a `PrivateLinkage` LLVM global
under a fixed name (`kFrustPluginManifestGlobalName`, `AST.h`) -
private linkage so it never becomes a resolvable extern JIT symbol the
plugin's own code could collide with; `frust_plugin_host`
(`projects/09_frust_plugin_host`) reads it directly off the compiled
`llvm::Module` object, before the plugin is ever linked into a JIT or
executed. `frust_plugin_load()` enforces "no manifest, no load": a
plugin with no `manifest` declaration - or one whose JSON fails to
parse, or one incompatible with the host per its declared
`intendedApplications`/`requiredHostFunctions` - is refused outright,
with no permissive fallback. See `docs/17-Plugin-Automation-Layer.md`
section 4.2 for the full manifest schema and compatibility-check rules.
