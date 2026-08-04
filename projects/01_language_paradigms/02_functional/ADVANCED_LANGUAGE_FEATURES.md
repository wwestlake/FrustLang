# Advanced Bleeding-Edge Features in Frust

## Overview

**Frust** incorporates the four most powerful concepts from modern programming language research across Lisp, Haskell, OCaml, and Unison:

1. **Lisp-Style AST Code Quotation** (`quote` / `unquote`)
2. **Refinement Types & Value Predicates** (`f32[-1.0 .. 1.0]`)
3. **First-Class Algebraic Effects & Resumable Handlers** (`effect` / `perform` / `handle` / `resume`)
4. **AST Content Hashing for Zero-Latency OrcJIT Hot-Reloading**

---

## 1. Lisp-Style AST Code Quotation (`quote` / `unquote`)

Instead of parenthetical Lisp syntax, Frust provides first-class AST code quotation inside `build_time` metaprogramming blocks.

```frust
// Metaprogram: Synthesizes a fast polynomial evaluation AST
fn build_polynomial_ast(coefficients: Array<f32, 3>) -> ASTExpr = build_time {
    let a = coefficients[0]
    let b = coefficients[1]
    let c = coefficients[2]

    // Quote generates AST nodes directly; unquote evaluates expressions into AST
    return quote {
        (unquote(a) * x * x) + (unquote(b) * x) + unquote(c)
    }
}
```

---

## 2. Refinement Types & Value Predicates

Refinement types attach mathematical predicates to types, allowing the compiler to guarantee invariant safety at compile time without runtime `assert()` overhead:

```frust
// Range-bounded Float (e.g. normalized audio sample)
type NormalizedSample = f32[-1.0 .. 1.0]

// Non-zero integer (guarantees divide-by-zero is impossible)
type NonZeroI32 = i32[!= 0]

pub fn safe_divide(numerator: i32, denominator: NonZeroI32) -> i32 = {
    return numerator / denominator // Guaranteed zero-panic compile-time safety!
}

// Fixed-dimension Matrix multiplication safety
fn multiply(a: Mat<3, 4>, b: Mat<4, 2>) -> Mat<3, 2>
```

---

## 3. First-Class Algebraic Effects & Resumable Handlers

Algebraic Effects unify exceptions, async/await, generators, logging, and dependency injection into a single, clean effect system with **resumable continuations**:

```frust
// 1. Declare Effects
effect ReadSensor -> f32
effect Log(msg: String)

// 2. Perform Effect (Independent of implementation!)
pub fn process_telemetry() -> f32 = {
    let val = perform ReadSensor()
    perform Log("Sensor read: " + val.to_string())
    return val * 10.0
}

// 3. Handle Effect (Mock for unit testing or wire to hardware!)
pub fn main() = {
    handle process_telemetry() with {
        effect ReadSensor() => resume(42.0) // Resumes execution with 42.0!
        effect Log(msg) => {
            println("[LOG]: " + msg)
            resume() // Resumes execution!
        }
    }
}
```

---

## 4. AST Content-Addressing for Zero-Latency OrcJIT Hot-Reloading

In traditional JIT environments, editing source code triggers full file re-compilation.

Frust hashes the AST structure of every function and component using BLAKE3 cryptographic content addressing:

```
[ Code Edit in JUCE Host UI ]
              │
              ▼
    [ AST Hash Generator ]
              │
    ┌─────────┴─────────┐
    │  Hash Match?      │
    ├─────────┬─────────┤
    │ YES     │ NO      │
    ▼         ▼         ▼
 (0ms Skip)  (Re-JIT LLVM OrcJIT Dylib)
```

If an edit in JUCE or an IDE does not change a function's semantic AST hash (e.g. comments or whitespace formatting), OrcJIT **bypasses compilation instantly with 0ms latency**!
