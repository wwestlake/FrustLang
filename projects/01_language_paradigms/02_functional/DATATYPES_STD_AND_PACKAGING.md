# Datatypes, Standard Library & Package Management Specification

## 1. Dual Compilation Engine: JIT + Bare-Metal AOT ("Hard Iron")

Our LLVM architecture supports **both** execution modes from the exact same LLVM IR frontend:

```
                  ┌──────────────────────┐
                  │ LagDaemon Language   │
                  │ Frontend (AST -> IR) │
                  └──────────┬───────────┘
                             │
                             ▼
                    [ LLVM IR Module ]
                             │
             ┌───────────────┴───────────────┐
             ▼                               ▼
    [ LLVM OrcJIT Engine ]        [ LLVM TargetMachine + LLD ]
    - Instant JIT Execution       - Compiles to native .obj / .o
    - Live JUCE App Hot-Swapping  - Links to standalone .exe / ELF
    - Interactive REPL Loop       - "Hard Iron" native bare-metal
```

---

## 2. Core Datatype System

### Primitive Numeric & Basic Types
| Category | Types |
| :--- | :--- |
| **Signed Integers** | `i8`, `i16`, `i32`, `i64`, `isize` |
| **Unsigned Integers** | `u8`, `u16`, `u32`, `u64`, `usize` |
| **Floating Point** | `f16`, `f32`, `f64` |
| **Booleans & Characters**| `bool` (`true`/`false`), `char` (UTF-32 codepoint) |
| **Strings & Slices** | `str` (UTF-8 string slice), `String` (heap-allocated growable) |

### Product Types (Tuples & Structural Records)
```ldfn
// Tuple Product Type
type Point2D = (f32, f32)

// Named Product Record
record Player {
    id: u64,
    name: String,
    position: (f32, f32),
    health: f32
}
```

### Sum Types (Algebraic Data Types / Tagged Unions)
```ldfn
// Sum Type (Enum Variants)
type Shape =
    | Circle(f32)                 // Radius
    | Rectangle(f32, f32)         // Width, Height
    | Point

// Monadic Standard Sum Types
type Option<T> =
    | Some(T)
    | None

type Result<T, E> =
    | Ok(T)
    | Err(E)
```

---

## 3. Standard Library (`std`) & LibC Core

The standard library wraps native `libc` and OS capabilities with zero-cost FFI bindings:

* **`std::libc`**: Direct low-level C FFI bindings (`malloc`, `free`, `memcpy`, `printf`, `open`, `read`, `write`).
* **`std::io`**: High-level safe input/output streams, file reading/writing, and console formatting.
* **`std::math`**: Optimized math primitives (`sin`, `cos`, `tan`, `sqrt`, `pow`, SIMD vectors).
* **`std::collections`**: Dynamic `Vector<T>`, `HashMap<K, V>`, `RingBuffer<T>`.

---

## 4. Package Manager & Build Tool Architecture (`ldpack`)

A Cargo-like package manager and build system managing dependency graphs, versioning, and build targets:

```
my_project/
├── ldpack.toml               # Package manifest
├── src/
│   ├── main.ldfn             # Executable entrypoint
│   └── lib.ldfn              # Package library root
└── target/
    ├── debug/                # JIT / Debug build artifacts
    └── release/              # AOT Bare-Metal Executables (.exe / ELF)
```

### Package Manifest Example (`ldpack.toml`)
```toml
[package]
name = "dsp_filter_pack"
version = "0.1.0"
authors = ["LagDaemon R&D"]
edition = "2026"

[dependencies]
std = "1.0.0"
juce_bridge = { path = "../02_juce_language_host" }

[build]
default_target = "jit" # Options: "jit" (OrcJIT) or "aot" (Native Binary)
```
