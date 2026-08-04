# Project 02: JUCE Language Host & Interactive Workbench

## Overview
A cross-platform JUCE desktop application designed to host, visualize, and interact with our custom multi-paradigm language tools and LLVM OrcJIT execution engine.

---

## Features & Integration Points
* **Interactive REPL Console**: GUI code editor & console that compiles and executes custom language snippets in real-time via LLVM OrcJIT.
* **Visual Reactive Dataflow Graph**: Node-graph editor leveraging JUCE graphics to visualize declarative dataflow networks and audio/DSP node connections.
* **C++ / C ABI Bridge**: Direct interop between JUCE application state and JIT-compiled language functions.
* **WebAssembly Exporter**: Export selected language modules to `.wasm` directly from the host app.

---

## Build Requirements
* **CMake 3.22+**
* **JUCE Framework** (fetched via CMake FetchContent or local JUCE)
* **LLVM 18+** with `LLVM_ENABLE_RTTI=ON`
  * **Windows**: `D:\vcpkg\packages\llvm_x64-windows`
  * **Linux (WSL)**: `~/llvm-lagdaemon-install`
