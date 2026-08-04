# LagDaemon Language Goals & Rust-Like Module System

## 1. Core Language Vision & Philosophy

### Embedded Dynamic Computing Engine
The core purpose of the LagDaemon language suite is **embedded dynamic code creation**. Instead of compiling code out-of-band and restarting host applications, the language compiler is designed to be embedded directly inside a running host application (such as JUCE audio/visual apps, game engines, or OS runtimes).

> *"All programming is really dynamic code creation while another app is running—it's just that usually, the host program is the Operating System."*

### Key Strategic Goals
1. **Dynamic Hot-Reloading via LLVM OrcJIT**: Modules and functions can be edited, recompiled into memory, and swapped at runtime without stopping the host application or invalidating host memory buffers.
2. **Rust-Like Module & Submodule Hierarchy**: Clean, file-system mapping for modular code organization, explicit visibility (`pub`), and structured namespace imports (`use`).
3. **Zero-Overhead Host Interop**: Native C/C++ ABI interop allowing host applications (e.g. JUCE) to invoke JIT-compiled functions with direct pointer access and zero marshalling tax.
4. **Multi-Paradigm Composition**: Functional expressions + Rust-like module safety + Reactive dataflow graphs + Meta compile-time execution.

---

## 2. Rust-Like Module & Submodule System

### Module Organization & File Mapping
Just like Rust, modules in LagDaemon can be declared inline or mapped directly to files and directories on disk:

```
src/
├── main.ldfn                 # Root entry module
├── math/
│   ├── mod.ldfn              # Submodule root for `math`
│   ├── dsp.ldfn              # Submodule `math::dsp`
│   └── matrix.ldfn           # Submodule `math::matrix`
└── audio/
    ├── synthesizer.ldfn      # Submodule `audio::synthesizer`
    └── filter.ldfn           # Submodule `audio::filter`
```

### Module Syntax & Keywords

#### Declaring Submodules (`mod`)
```ldfn
// Declare an inline submodule
mod internal_helpers {
    pub fn helper() -> Int = 42
}

// Declare a file-backed submodule (loads math/dsp.ldfn or dsp.ldfn)
mod dsp;
```

#### Symbol Visibility (`pub`)
By default, all functions, types, and constants inside a module are **private** to that module. The `pub` keyword explicitly exports a symbol:

```ldfn
mod dsp {
    // Public function accessible outside the module
    pub fn process_sample(sample: f32, gain: f32) -> f32 =
        sample * gain

    // Private helper function (only visible inside dsp module)
    fn internal_clamp(val: f32) -> f32 =
        if val > 1.0 then 1.0 else if val < -1.0 then -1.0 else val
}
```

#### Importing Symbols (`use`)
```ldfn
// Import specific symbols
use math::dsp::process_sample;

// Import with alias
use math::matrix::Matrix as Mat;

// Glob import
use audio::filter::*;

// Nested imports
use math::{dsp::process_sample, matrix::Matrix};
```

---

## 3. Dynamic Module Hot-Reloading in LLVM OrcJIT

In traditional languages, modules are linked statically into a single binary. In our embedded architecture, each module corresponds to a dynamic **`JITDylib`** inside LLVM OrcJIT:

```
[ JUCE / Host Application ]
           │
           ▼
[ LagDaemon OrcJIT Host Driver ]
     ├── JITDylib: "main"
     ├── JITDylib: "math::dsp"    ───(Re-JIT / Hot-Swapped on File Change)
     └── JITDylib: "audio"
```

1. When a submodule file (e.g. `dsp.ldfn`) is modified on disk or edited in the JUCE host UI, the LLVM IR for `math::dsp` is regenerated.
2. The old `JITDylib` for `math::dsp` is removed or updated with a new symbol version.
3. Function pointers in the host app automatically resolve to the new memory address—**zero host restart required**.
