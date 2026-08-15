# Frust Language Specification

Frust (Functional Rust) is the flagship language of LagDaemon Software Research & Development: a pure, expression-oriented language built for embedded dynamic code creation - living inside a running host application (the JUCE IDE) with LLVM OrcJIT hot-reloading, rather than compiling out-of-band and restarting.

This is the single spec document for the language. The grammar itself (flex/bison `.l`/`.y`) is the authority on syntax once it exists - this document covers semantics and design intent.

---

## Contents
1. [Advanced Language Features](#1-advanced-language-features)
2. [Smart Pointers & Memory](#2-smart-pointers--memory)
3. [REPL & Dynamic JIT Driver](#3-repl--dynamic-jit-driver)

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
