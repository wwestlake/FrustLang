# Frust - The Functional Rust Language

**Frust** (**F**unctional **Rust**) is a modern, general-purpose, component-driven programming language designed by **LagDaemon Software Research & Development**.

Combining the safety, module system, and bare-metal performance of Rust with expression-oriented functional programming, first-class verifiable components, and dual JIT/AOT compilation via LLVM.

---

## Key Pillars of Frust

```
                              FRUST (Functional Rust)
                                         │
 ┌───────────────────┬───────────────────┼───────────────────┬───────────────────┐
 │                   │                   │                   │                   │
 ▼                   ▼                   ▼                   ▼                   ▼
First-Class         Rust-Like           Intuitive           Dual Execution      Monadic & Sum/
Components          Modules             Metaprogramming     JIT (OrcJIT) +      Product Types
(in/out/where)      (mod, pub, use)     (Type & build_time) AOT ("Hard Iron")   (Option, Result)
```

---

## 1. Syntax Overview

```frust
mod dsp {
    interface AudioFilter {
        in  signal: Stream<f32>[range: -1.0 .. 1.0]
        in  cutoff: ControlSignal[freq: <= 1000.0]
        out output: Stream<f32>[range: -1.0 .. 1.0]

        where {
            latency <= 64,
            thread_safe == true
        }
    }

    pub component LowPass(freq: f32) : AudioFilter {
        in signal
        in cutoff
        out output

        output = signal * (cutoff / 1000.0)
    }
}

use dsp::{AudioFilter, LowPass};

pub fn main() -> Result<f32, String> = {
    let filter = LowPass(440.0)
    Ok(0.75)
}
```

---

## 2. Core Specifications

* **Language Name**: `Frust` (`.frust` / `.fr`)
* **Package Manager**: `frustpack` (`frustpack.toml`)
* **Execution Modes**:
  * **Interactive / Embedded JIT**: LLVM OrcJIT for live REPL & JUCE app hot-swapping.
  * **Hard Iron AOT**: Native machine code emission (`.obj`/`.o`) linked via `lld` to standalone `.exe` / ELF binaries.
